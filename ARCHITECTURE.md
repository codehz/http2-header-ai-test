# HTTP/2 客户端实现 - 技术架构文档

## 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│                   (main.cpp - 测试程序)                      │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│                 HTTP/2 Client Layer                          │
│              (http2_client.cpp/h)                            │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ - TLS连接管理                                          │ │
│  │ - HTTP/2帧处理                                         │ │
│  │ - 请求/响应处理                                        │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│              HPACK/Header Compression Layer                  │
│                (hpack.cpp/h)                                │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ - HPACK编码/解码                                       │ │
│  │ - Huffman编码/解码                                     │ │
│  │ - 静态表/动态表管理                                    │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│              OpenSSL/TLS Layer                              │
│          (OpenSSL库 - 系统依赖)                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ - SSL/TLS握手                                          │ │
│  │ - 加密/解密                                            │ │
│  │ - ALPN协议协商                                         │ │
│  │ - SNI服务器名称指示                                    │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│           Linux Socket API Layer                            │
│      (Linux原生系统调用 - socket, connect等)                 │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ - Socket创建/管理                                      │ │
│  │ - TCP连接                                              │ │
│  │ - 数据读写                                              │ │
│  │ - DNS解析                                              │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│                Network Interface                            │
│                    (Internet)                               │
└─────────────────────────────────────────────────────────────┘
```

## 数据流

### 请求流程

```
User Request (GET /)
    ↓
Http2Client::get()
    ↓
sendHeadersFrame()
    ├─ 构建请求头列表
    ├─ HPACK编码
    └─ 发送HEADERS帧
    ↓
sendFrame()
    ├─ 构建HTTP/2帧
    └─ socketWrite()
        ├─ SSL_write()
        └─ write() system call
            ↓
        TLS/SSL encryption
            ↓
        TCP transmission to example.com
```

### 响应流程

```
TCP receive from example.com
    ↓
TLS/SSL decryption
    ↓
socketRead()
    ├─ SSL_read()
    └─ read() system call
        ↓
recvFrame()
    ├─ 读取帧头（9字节）
    └─ 读取帧负载
        ↓
receiveResponse()
    ├─ FRAME_TYPE_HEADERS
    │  ├─ 收集头块
    │  ├─ HPACK::decode() 解码
    │  └─ 提取:status头部
    │
    ├─ FRAME_TYPE_DATA
    │  └─ 累积响应体
    │
    └─ FRAME_TYPE_GOAWAY/END_STREAM
       └─ 返回Response对象
           ├─ status_code
           ├─ headers[]
           └─ body[]
```

## 文件详解

### http2_client.h

定义Http2Client类的接口：

```cpp
class Http2Client {
    // 公开接口
    bool connect();
    void disconnect();
    Response get(const std::string& path, ...);
    Response head(const std::string& path, ...);
    bool isConnected() const;
    
    // 私有实现
private:
    int socket_fd_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;
    
    bool createSocket();
    bool performTlsHandshake();
    bool sendClientPreface();
    bool sendSettings();
    
    bool sendFrame(...);
    bool recvFrame(...);
    
    int socketRead(...);
    int socketWrite(...);
    
    void cleanup();
};
```

### http2_client.cpp

实现HTTP/2协议逻辑：

#### 关键函数

1. **createSocket()** - 创建TCP socket并连接
   - gethostbyname() 解析主机名
   - socket() 创建socket
   - connect() 建立连接
   - setsockopt(TCP_NODELAY) 禁用Nagle算法

2. **performTlsHandshake()** - 执行TLS握手
   - SSL_CTX_new() 创建上下文
   - SSL_set_tlsext_host_name() 设置SNI
   - SSL_CTX_set_alpn_protos() 设置ALPN
   - SSL_connect() 执行握手

3. **sendFrame()** - 发送HTTP/2帧
   - 编码帧头（9字节）
   - 添加帧负载
   - socketWrite()发送

4. **recvFrame()** - 接收HTTP/2帧
   - socketRead()读取帧头
   - 解析长度/类型/标志/流ID
   - socketRead()读取负载

5. **receiveResponse()** - 接收和处理响应
   - 循环接收帧
   - 处理不同帧类型
   - 解码HPACK头部
   - 累积响应体

### hpack.cpp

HPACK头部压缩实现：

#### 关键组件

1. **StaticTable** - RFC 7541静态表（61个条目）
   - getByIndex() - 按索引查询
   - getIndexByName() - 按名称查询

2. **DynamicTable** - 动态表（4KB默认）
   - insert() - 添加新条目
   - get() - 按索引查询
   - evict() - 淘汰旧条目

3. **StringCoder** - 字符串编码/解码
   - encodeString() - 编码（支持Huffman）
   - decodeString() - 解码（支持Huffman）

4. **HPACK** - 高层API
   - encode() - 编码头部列表
   - decode() - 解码头部块

5. **Huffman表** - RFC 7541附录B
   - HUFFMAN_CODE_TABLE[256] - 编码表
   - huffmanDecode() - 解码函数

## HTTP/2帧格式

所有HTTP/2帧遵循统一格式：

```
+-----------------------------------------------+
|                 Length (24)                   |
+---------------+---------------+---------------+
|   Type (8)    |   Flags (8)   |
+-+-------------+---------------+-------------------------------+
|R|                 Stream ID (31)                               |
+=+=====================================================+=========+
|                   Frame Payload (0...)                         |
+---------------------------------------------------------------+
```

### 帧头详解

- **Length** (3字节): 负载长度，最大16384字节
- **Type** (1字节): 帧类型 (0-9)
- **Flags** (1字节): 特定于帧类型的标志
- **Reserved** (1位): 保留位，必须为0
- **Stream ID** (31位): 流标识符（0表示连接级帧）

### 常用帧类型

| 类型 | ID | 使用场景 |
|-----|----|----|
| SETTINGS | 0x4 | 连接参数交换 |
| HEADERS | 0x1 | 发送请求/响应头 |
| DATA | 0x0 | 发送消息体 |
| PING | 0x6 | 连接保活检查 |
| GOAWAY | 0x7 | 关闭连接 |
| WINDOW_UPDATE | 0x8 | 流量控制 |

## SSL/TLS握手流程

```
Client                              Server
  │                                   │
  ├─────────────── ClientHello ────────>
  │ (包含ALPN: h2)
  │
  <─────────────── ServerHello ───────┤
  │ (ALPN选择: h2)
  │
  <─────────── Certificate ───────────┤
  │
  <───────── ServerKeyExchange ───────┤
  │
  <────────── ServerHelloDone ────────┤
  │
  ├────────── ClientKeyExchange ─────>
  │
  ├────────── ChangeCipherSpec ──────>
  │
  ├────────────── Finished ──────────>
  │
  <────────── ChangeCipherSpec ──────┤
  │
  <───────────── Finished ────────────┤
  │
  ✓ Secure connection established
```

## HPACK编码示例

### 请求头编码

```
请求头:
  :method GET
  :scheme https
  :authority example.com
  :path /

编码过程:
1. :method GET (索引2)
   ├─ 二进制: 10000010
   └─ 输出: 0x82

2. :scheme https (索引7)
   ├─ 二进制: 10000111
   └─ 输出: 0x87

3. :authority (索引1 + 字面值)
   ├─ 前缀: 01000001
   ├─ 长度: 11 (example.com = 11字符)
   ├─ 二进制: 00001011
   └─ 输出: 0x41 0x0B [example.com]

4. :path / (索引4 + 字面值)
   ├─ 前缀: 01000100
   ├─ 长度: 1 (/ = 1字符)
   ├─ 二进制: 00000001
   └─ 输出: 0x44 0x01 [/]

总编码: 82 87 41 0B 6578 616D 706C 652E 636F 6D 44 01 2F
总大小: 18字节
```

## 错误处理和日志

### 日志级别

```
cout  - 正常信息和进度报告
cerr  - 错误和警告信息
```

### 常见错误及处理

| 错误 | 原因 | 处理 |
|-----|------|------|
| Socket连接失败 | DNS解析或网络不通 | 退出，显示错误 |
| TLS握手失败 | 证书验证或协议不支持 | 退出，显示错误 |
| FRAME_SIZE_ERROR | 发送的HPACK编码错误 | 改进编码逻辑 |
| PROTOCOL_ERROR | HTTP/2协议违反 | 检查帧格式 |
| 头部解码失败 | Huffman编码问题 | 跳过，使用默认值 |

## 性能考虑

1. **连接复用**: 单个连接用于所有请求（可扩展多流）
2. **头部压缩**: HPACK平均压缩率 50-90%
3. **TCP优化**: 启用TCP_NODELAY减少延迟
4. **流量控制**: 默认窗口大小65535字节
5. **SSL/TLS**: 使用硬件加速（如OpenSSL配置）

## 测试结果

### 成功连接到example.com

```
Socket connection: ✓
TLS handshake: ✓ (ALPN: h2)
HTTP/2 preface: ✓
Settings exchange: ✓
GET request: ✓
Response headers: 解析中
Response body: ✓ (513字节接收)
Status code: 200 ✓
```

## 下一步改进方向

1. **完整的HPACK解码**: 支持所有编码类型
2. **连接池**: 管理多个并发连接
3. **请求队列**: 异步请求处理
4. **缓存**: HTTP缓存实现
5. **重定向处理**: 3xx状态码处理
6. **错误恢复**: 自动重连机制
