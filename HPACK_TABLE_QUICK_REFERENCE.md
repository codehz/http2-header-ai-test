# HPACK 表管理 - 快速参考指南

## 三个核心类的对比

| 特性 | StaticTable | DynamicTable | HeaderTable |
|------|-----------|------------|-----------|
| **目的** | 预定义的标准头字段 | 存储动态头字段 | 统一管理静态和动态表 |
| **条目数** | 固定 61 个 | 可变（0-n） | 61 + 动态表条目 |
| **索引** | 1-61 | 0-based | 1-61(静态) + 62+(动态) |
| **可变性** | 不可变 | 可变，自动淘汰 | 读写，可管理 |
| **大小** | 固定 | 可配置最大大小 | 仅动态表可配置 |
| **查询速度** | O(n) | O(n) | O(n) |
| **用途** | 快速参考 | 存储新头字段 | 编解码时的全局接口 |

## 静态表中的常用索引

```
索引  名称                值
---- -------------------- ------------------
  1   :authority
  2   :method              GET
  3   :method              POST
  4   :path                /
  5   :path                /index.html
  6   :scheme              http
  7   :scheme              https
  8   :status              200
  9   :status              204
 10   :status              206
 11   :status              304
 12   :status              400
 13   :status              404
 14   :status              500
 15   accept-charset
 16   accept-encoding      gzip, deflate
 23   authorization
 24   cache-control
 28   content-length
 31   content-type
 32   cookie
 33   date
 38   host
 51   referer
 54   server
 55   set-cookie
 58   user-agent
 61   www-authenticate
```

## 动态表大小计算

```
条目大小 = 32 + 名称长度 + 值长度（字节）

例子：
- ("x-custom", "value")  = 32 + 8 + 5 = 45 字节
- (":method", "GET")     = 32 + 7 + 3 = 42 字节
- ("Authorization", "Bearer token")
                         = 32 + 13 + 12 = 57 字节
```

## API 速查表

### StaticTable
```cpp
// 获取
HeaderField field = StaticTable::getByIndex(2);        // :method GET

// 查询
int idx = StaticTable::getIndexByNameValue(":method", "GET");  // 2
idx = StaticTable::getIndexByName(":method");                   // 2

// 大小
size_t size = StaticTable::size();  // 61
```

### DynamicTable
```cpp
DynamicTable table(4096);  // 最大 4096 字节

// 插入
table.insert({"x-header", "value"});

// 访问
HeaderField field = table.get(0);  // 最新条目

// 查询
int idx = table.getIndexByNameValue("x-header", "value");  // 0
idx = table.getIndexByName("x-header");                    // 0

// 管理
table.setMaxSize(2048);        // 调整大小，自动淘汰
table.clear();                 // 清空
size_t bytes = table.size();   // 当前字节数
size_t count = table.entryCount();  // 条目数
```

### HeaderTable
```cpp
HeaderTable table(4096);  // 创建，动态表最大 4096 字节

// 统一访问（1-61 为静态表，62+ 为动态表）
HeaderField field = table.getByIndex(2);   // 静态表：:method GET
field = table.getByIndex(62);              // 动态表：第一个条目

// 统一查询（优先动态表）
int idx = table.getIndexByNameValue(":method", "GET");    // 2 (静态)
idx = table.getIndexByName(":method");                     // 2 (静态)

// 动态表管理
table.insertDynamic({"x-request-id", "123"});
table.setDynamicTableMaxSize(2048);
table.clearDynamic();

// 查询返回值
// - 1-61:  静态表索引
// - 62+:   动态表索引
// - -1:    未找到
```

## 常见使用模式

### 模式 1：查询和使用静态表
```cpp
// 查询常见头
int method_idx = StaticTable::getIndexByNameValue(":method", "GET");
int path_idx = StaticTable::getIndexByNameValue(":path", "/");
// 返回: 2, 4
```

### 模式 2：管理动态表中的自定义头
```cpp
DynamicTable dyn_table(4096);

// 第一个请求 - 添加自定义头
dyn_table.insert({"x-correlation-id", "abc123"});
dyn_table.insert({"x-user-id", "user42"});

// 第二个请求 - 复用已有的头
int idx = dyn_table.getIndexByName("x-correlation-id");  // 1
// 编码时可以使用较小的索引，节省空间
```

### 模式 3：处理超大头字段值
```cpp
DynamicTable table(1000);  // 小表

// 尝试插入非常大的条目
HeaderField huge = {"x-large", std::string(2000, 'x')};
table.insert(huge);
// 条目被丢弃，表保持为空（条目大小 > 最大大小）
```

### 模式 4：用 HeaderTable 进行完整的头编码决策
```cpp
HeaderTable table(4096);

// 查询 :method POST
// 结果：3（静态表）- 使用索引编码节省空间
int idx = table.getIndexByNameValue(":method", "POST");

// 添加自定义头
table.insertDynamic({"x-trace-id", "12345"});

// 下次查询自定义头
// 结果：62（动态表）- 使用动态索引
idx = table.getIndexByNameValue("x-trace-id", "12345");
```

## 常见陷阱

### 1. 忘记名称小写化
```cpp
// ✓ 正确 - 自动转换为小写
int idx = StaticTable::getIndexByName("CONTENT-TYPE");

// 都可以工作，不需要手动转换
idx = StaticTable::getIndexByName("content-type");
idx = StaticTable::getIndexByName("Content-Type");
```

### 2. 混淆索引编号方案
```cpp
HeaderTable table(4096);

// ✓ 正确用法
auto field1 = table.getByIndex(2);    // 静态表，:method GET
auto field2 = table.getByIndex(62);   // 动态表第一个条目

// ✗ 错误 - 索引从 1 开始，不是 0
auto field3 = table.getByIndex(0);    // 抛出异常
```

### 3. 忽视动态表淘汰
```cpp
DynamicTable table(100);

// 插入 43 字节条目
table.insert({":method", "GET"});      // 剩余空间 57 字节

// 插入 60 字节条目会超过限制
table.insert({"content-type", "application/json"});  
// 第一个条目被淘汰，只保留第二个

// 检查条目数
assert(table.entryCount() == 1);  // 不是 2！
```

### 4. HeaderTable 查询优先级误解
```cpp
HeaderTable table(4096);

// 静态表中存在 :method GET (索引 2)
int idx1 = table.getIndexByNameValue(":method", "GET");
// 返回: 2 (静态表)

// 添加新的 :method POST 到动态表
table.insertDynamic({":method", "POST"});

// 静态表中也有 POST (索引 3)，但动态表优先
int idx2 = table.getIndexByName(":method");
// 返回: 62 (新插入的动态表条目)
// 而不是 2 或 3！
```

## 性能提示

1. **避免频繁查询**: 缓存查询结果
2. **预分配大小**: 如果知道需要多少条目，设置合适的初始大小
3. **定期清理**: 定期调用 `setMaxSize()` 清理不需要的条目
4. **批量操作**: 一次插入多个相关头字段时考虑顺序

## 错误处理

```cpp
try {
    auto field = StaticTable::getByIndex(100);  // 超出范围
} catch (const std::out_of_range& e) {
    std::cerr << "索引错误: " << e.what() << std::endl;
}

try {
    auto field = DynamicTable table;
    field = table.get(100);  // 表为空或索引太大
} catch (const std::out_of_range& e) {
    std::cerr << "动态表错误: " << e.what() << std::endl;
}
```

## 测试命令

```bash
# 编译
cd /home/codehz/Projects/http2-test/build
cmake .. && make

# 运行所有测试
./http2-test-runner

# 运行表管理相关测试
./http2-test-runner --gtest_filter="*Table*"

# 运行特定测试类
./http2-test-runner --gtest_filter="StaticTableTest*"
./http2-test-runner --gtest_filter="DynamicTableTest*"
./http2-test-runner --gtest_filter="HeaderTableTest*"
```

## RFC 7541 参考

- **总条目数**: 61
- **动态表初始大小**: 4096 字节（可配置）
- **条目大小公式**: 32 + name.length + value.length
- **淘汰策略**: FIFO（先进先出）
- **头字段名**: 总是小写
- **索引起始**: 1（不是 0）
