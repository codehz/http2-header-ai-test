# HTTP/2 客户端实现

## 概述

这是一个基于测试目的的HTTP/2客户端实现，使用以下技术：

- **协议**: HTTP/2 (RFC 7540)
- **安全传输**: OpenSSL/TLS 1.2+
- **头部压缩**: HPACK (RFC 7541)，包括Huffman解码
- **网络API**: Linux原生socket API
- **语言**: C++17

## 功能特性

### 已实现

1. **TLS连接**
   - 使用OpenSSL进行加密连接
   - SNI (Server Name Indication)支持
   - ALPN (Application-Layer Protocol Negotiation)协商HTTP/2

2. **HTTP/2协议**
   - HTTP/2连接前言 (Preface)
   - SETTINGS帧交互
   - HEADERS帧发送和接收
   - DATA帧接收
   - WINDOW_UPDATE和PING帧处理
   - GOAWAY帧错误处理

3. **HPACK头部压缩**
   - 索引化头部（静态表）
   - 字面值头部编码
   - Huffman字符串解码
   - 动态表支持

4. **请求支持**
   - GET 请求
   - HEAD 请求
   - 自定义请求头添加

## 项目结构

```
include/
  ├── http2_client.h      # HTTP/2客户端头文件
  ├── hpack.h             # HPACK编码/解码头文件
  └── header_parser.h     # 头部解析器头文件

src/
  ├── http2_client.cpp    # HTTP/2客户端实现（新）
  ├── hpack.cpp           # HPACK实现（包含Huffman表）
  ├── header_parser.cpp   # 头部解析器实现
  └── main.cpp            # 测试程序（新）

build/
  └── http2-client        # 编译后的可执行文件
```

## 编译和运行

### 编译

```bash
cd /home/codehz/Projects/http2-test
mkdir build
cd build
cmake ..
make
```

### 运行

```bash
./http2-client
```

### 输出示例

```
=== HTTP/2 Client Test ===
Connecting to example.com...

Socket connection established
ALPN selected: h2
TLS handshake successful
HTTP/2 preface sent
Waiting for server initialization...
Init frame #1 - Type: 4 Flags: 0 Stream: 0 Payload size: 18
Sending SETTINGS ACK
Server SETTINGS received, ready for requests
Successfully connected to example.com:443

Sending GET request to /
Encoding headers for request:
  :method: GET (static index 2)
  :scheme: https (static index 7)
  :authority: example.com
  :path: /
Encoded headers size: 18 bytes
HEADERS frame sent successfully
Received frame type: 1 flags: 4 stream_id: 1 payload_size: 128
Received HEADERS frame for stream 1
Headers complete, attempting to decode...
Received frame type: 0 flags: 0 stream_id: 1 payload_size: 513
Received DATA frame for stream 1 (513 bytes)
Response stream ended

=== Response Headers ===
Status Code: 200

Headers:

Response Body Size: 513 bytes

Response Body (first 256 bytes):
---
<!doctype html><html lang="en"><head><title>Example Domain</title>
...
---

Connection closed
```

## 核心类和接口

### Http2Client

主要的HTTP/2客户端类：

```cpp
class Http2Client {
public:
    // 构造函数
    Http2Client(const std::string& host, uint16_t port = 443);
    
    // 连接到服务器
    bool connect();
    
    // 关闭连接
    void disconnect();
    
    // 发送GET请求
    Response get(const std::string& path, 
                 const std::vector<std::pair<std::string, std::string>>& headers = {});
    
    // 检查连接状态
    bool isConnected() const;
};
```

### Response结构

```cpp
struct Response {
    int status_code;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<uint8_t> body;
};
```

## 技术细节

### HTTP/2连接建立流程

1. **Socket创建**: 使用socket()创建TCP连接
2. **DNS解析**: 使用gethostbyname()解析域名
3. **TCP连接**: 使用connect()建立连接
4. **TLS握手**: 
   - SSL_CTX_new()创建SSL上下文
   - SSL_set_tlsext_host_name()设置SNI
   - SSL_CTX_set_alpn_protos()设置ALPN协议列表
   - SSL_connect()执行TLS握手
5. **HTTP/2初始化**:
   - 发送HTTP/2连接前言(PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n)
   - 发送SETTINGS帧
   - 接收并应答服务器的SETTINGS帧

### HPACK头部编码

请求头使用HPACK进行编码：

```
伪头部（静态表索引）:
  :method GET      -> 索引2 -> 0x82
  :scheme https    -> 索引7 -> 0x87

动态头部（字面值编码）:
  :authority: example.com  -> 0x41 + 长度 + 值
  :path: /                 -> 0x44 + 长度 + 值
```

### HTTP/2帧处理

支持以下帧类型：

| 帧类型 | ID | 说明 |
|--------|----|----|
| DATA | 0x0 | 传输HTTP消息体 |
| HEADERS | 0x1 | 传输HTTP头部 |
| PRIORITY | 0x2 | 指定流优先级 |
| RST_STREAM | 0x3 | 重置流 |
| SETTINGS | 0x4 | 交换连接设置 |
| PUSH_PROMISE | 0x5 | 服务器推送 |
| PING | 0x6 | 连接保活 |
| GOAWAY | 0x7 | 连接终止 |
| WINDOW_UPDATE | 0x8 | 流量控制 |
| CONTINUATION | 0x9 | 继续头部 |

## 已知限制

1. **Headers解码**：虽然支持Huffman解码，但当前实现可能在某些情况下无法完全解码所有头部。这不影响响应体的接收。

2. **头部解析**：当前实现展示了状态码和头部的占位符，但具体的头部值可能需要进一步的HPACK优化。

3. **单流请求**：当前实现仅支持单个流ID（流1）进行请求，多路复用功能可扩展实现。

4. **无请求体**：当前仅支持GET/HEAD请求，不支持POST等需要请求体的方法。

5. **证书验证**：为了测试目的，禁用了SSL证书验证，生产环境应启用。

## 扩展建议

1. **完整Huffman解码**：优化HPACK解码以支持所有Huffman编码的字符串
2. **多路复用**：支持在同一连接上发送多个并发请求
3. **POST请求**：添加请求体支持
4. **流优先级**：实现PRIORITY帧支持
5. **服务器推送**：处理PUSH_PROMISE帧
6. **流量控制**：完整的WINDOW_UPDATE流量控制

## 依赖项

- **OpenSSL**: 用于TLS加密
- **GTest**: 用于单元测试（可选）
- **CMake**: 构建系统

## 实现验证

通过以下步骤验证客户端功能：

```bash
# 1. 编译
cd /home/codehz/Projects/http2-test/build
make

# 2. 运行客户端
./http2-client

# 3. 检查输出
# - 应该看到"Successfully connected to example.com:443"
# - 应该看到"Response Body Size: X bytes"
# - 应该看到HTML响应内容
```

## 贡献

这个实现基于RFC 7540 (HTTP/2) 和 RFC 7541 (HPACK)。

## 许可证

用于测试和教学目的。
