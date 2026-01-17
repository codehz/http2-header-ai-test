# HPACK 表管理功能实现总结

## 概述

本文档总结了在 HTTP/2 项目中实现的 HPACK（Header Compression for HTTP/2）表管理功能。该实现按照 RFC 7541 标准，包括静态表、动态表和统一的表管理接口。

## 实现的类

### 1. StaticTable（静态表）

**位置**: `include/hpack.h` 和 `src/hpack.cpp`

**功能说明**:
- 实现了 RFC 7541 附录 B 中的 61 个预定义 HTTP/2 标准头字段
- 索引范围：1-61（遵循 RFC 7541 标准）
- 所有查询操作都会将头字段名称自动转换为小写

**主要方法**:
- `getByIndex(size_t index) -> HeaderField` - 通过索引获取头字段（1-61）
- `getIndexByNameValue(const std::string& name, const std::string& value) -> int` - 通过名值对查询，返回索引或 -1
- `getIndexByName(const std::string& name) -> int` - 仅按名称查询，返回索引或 -1
- `size() -> size_t` - 返回静态表大小（61）

**包含的 61 个头字段**:
- 伪头字段: `:authority`, `:method` (GET/POST), `:path` (/、/index.html), `:scheme` (http、https), `:status` (200、204、206、304、400、404、500)
- 通用头字段: accept、accept-charset、accept-encoding、accept-language、age、allow、authorization、cache-control、content-length、content-type、cookie、date、expect、expires、host、if-modified-since、last-modified、location、referer、server、user-agent 等
- 完整列表包括 61 个标准 HTTP 头字段

### 2. DynamicTable（动态表）

**位置**: `include/hpack.h` 和 `src/hpack.cpp`

**功能说明**:
- 存储动态编码的头字段，用于后续请求的压缩
- 新条目添加到表的前端，最旧条目在后端
- 当表超过最大大小时自动淘汰最旧条目（FIFO）
- 支持可配置的最大大小（默认 4096 字节）

**大小计算规则（RFC 7541）**:
$$\text{EntrySize} = 32 + \text{NameLength} + \text{ValueLength}$$

**主要方法**:
- `insert(const HeaderField& field)` - 在表前端插入新条目，自动淘汰超大的旧条目
- `get(size_t index) -> HeaderField` - 通过索引获取（0-based，0 是最新条目）
- `getIndexByNameValue(const std::string& name, const std::string& value) -> int` - 查询名值对
- `getIndexByName(const std::string& name) -> int` - 仅按名称查询
- `clear()` - 清空表
- `setMaxSize(size_t size)` - 调整最大大小并淘汰旧条目
- `size() -> size_t` - 返回表当前占用大小（字节）
- `entryCount() -> size_t` - 返回条目数量

**淘汰策略**:
- 当插入新条目导致表大小超过最大值时，从表后端不断淘汰旧条目
- 如果单个条目大小超过最大表大小，该条目被丢弃，表被清空
- 支持运行时调整最大大小，自动进行必要的淘汰

### 3. HeaderTable（统一表管理）

**位置**: `include/hpack.h` 和 `src/hpack.cpp`

**功能说明**:
- 将静态表和动态表集成为一个统一的接口
- 提供统一的索引方案：1-61 对应静态表，62+ 对应动态表
- 查询时优先搜索动态表（因为更新），然后搜索静态表

**主要方法**:
- `getByIndex(size_t index) -> HeaderField` - 通过统一索引获取（1-61 静态表，62+ 动态表）
- `getIndexByNameValue(const std::string& name, const std::string& value) -> int` - 搜索并返回统一索引
- `getIndexByName(const std::string& name) -> int` - 仅按名称搜索
- `insertDynamic(const HeaderField& field)` - 向动态表添加
- `setDynamicTableMaxSize(size_t size)` - 调整动态表大小
- `clearDynamic()` - 清空动态表

**索引规则**:
```
1-61:   静态表索引（RFC 7541）
62-n:   动态表索引（62 = 动态表第 0 个条目）
```

## 特性实现

### 名称小写化
所有头字段名称在插入和查询时都自动转换为小写，符合 HTTP/2 规范要求头字段名称为小写。

### RFC 7541 合规性
- 遵循 RFC 7541 附录 B 的 61 个静态表条目
- 条目大小计算遵循 RFC 7541 规范
- 索引从 1 开始（而非 0），符合 RFC 标准

### 大小管理
- 精确跟踪表占用的字节数
- 支持动态调整表大小，自动淘汰旧条目
- 防止超大条目破坏表结构

## 测试覆盖

### StaticTable 测试（10 个测试）
- 伪头字段查询（:method、:path、:scheme、:status）
- 名值对查询和单名称查询
- 名称小写化转换
- 索引范围验证
- 常见 HTTP 头字段验证

### DynamicTable 测试（11 个测试）
- 条目插入和获取
- 名值对和名称查询
- 名称小写化
- 超过最大大小时的淘汰
- 表清空和大小调整
- 非常大的条目处理
- 条目大小计算

### HeaderTable 测试（9 个测试）
- 静态表访问（索引 1-61）
- 动态表访问（索引 62+）
- 混合表查询（名值对和名称）
- 优先级验证（动态表优先于静态表）
- 动态表管理（插入、大小调整、清空）
- 综合 HTTP/2 请求场景

### 测试结果
- **总计**: 30 个新的 HPACK 表管理测试
- **通过率**: 100% (30/30)
- 所有测试均通过，验证了实现的正确性

## 文件修改

### include/hpack.h
- 添加了 `StaticTable`、`DynamicTable` 和 `HeaderTable` 三个类的声明
- 包含详细的中文注释和方法说明

### src/hpack.cpp
- 实现了 61 个静态表条目（RFC 7541 附录 B）
- 完整实现了三个类的所有方法
- 包含了名称小写化的辅助函数 `toLower()`
- 修复了 `IntegerEncoder::decodeInteger()` 中的边界检查，正确处理不完整的编码

### test/test_hpack.cpp
- 添加了 `StaticTableTest` 类（10 个测试）
- 添加了 `DynamicTableTest` 类（11 个测试）
- 添加了 `HeaderTableTest` 类（9 个测试）
- 每个测试包含详细的中文注释说明

## 编译与构建

```bash
# 进入项目目录
cd /home/codehz/Projects/http2-test

# 清除旧的构建
rm -rf build

# 创建并进入构建目录
mkdir build && cd build

# 配置和编译
cmake .. && make

# 运行所有测试
./http2-test-runner

# 运行特定的表管理测试
./http2-test-runner --gtest_filter="*Table*"
```

## 使用示例

### 使用静态表
```cpp
#include "hpack.h"
using namespace http2;

// 获取静态表中的 :method GET (索引 2)
HeaderField field = StaticTable::getByIndex(2);
// field.name = ":method", field.value = "GET"

// 查询索引
int idx = StaticTable::getIndexByNameValue(":method", "POST");  // 返回 3
idx = StaticTable::getIndexByName("content-type");              // 返回 31
```

### 使用动态表
```cpp
// 创建一个最大大小为 4096 字节的动态表
DynamicTable table(4096);

// 插入新头字段
table.insert({"x-custom-header", "custom-value"});

// 查询
HeaderField field = table.get(0);  // 获取最新插入的条目
int idx = table.getIndexByName("x-custom-header");  // 返回 0

// 调整表大小
table.setMaxSize(2048);  // 自动淘汰旧条目
```

### 使用统一表管理
```cpp
// 创建统一表管理器
HeaderTable table(4096);

// 访问静态表条目
HeaderField field = table.getByIndex(2);  // :method GET

// 向动态表添加
table.insertDynamic({"x-request-id", "123"});

// 统一查询（动态表优先）
int idx = table.getIndexByNameValue(":method", "GET");     // 返回 2 (静态表)
idx = table.getIndexByNameValue("x-request-id", "123");    // 返回 62 (动态表)
```

## 性能特性

- **O(n) 查询性能**: 线性搜索静态表和动态表
- **O(1) 获取性能**: 通过索引直接访问
- **O(n) 淘汰性能**: 最坏情况下需要遍历所有旧条目
- **内存使用**: 精确跟踪每个条目的字节占用

## 扩展建议

1. **优化查询性能**: 可以添加哈希表加速名值对查询（O(1) 而非 O(n)）
2. **Huffman 编码**: 目前字符串编码不支持 Huffman，可在 `StringCoder` 中实现
3. **动态表改进**: 可以使用更复杂的淘汰策略（如 LRU）而非 FIFO
4. **索引性能**: 考虑为静态表建立索引以加速查询

## 参考资源

- RFC 7541: HPACK - Header Compression for HTTP/2
  - Appendix B: Static Table Definition
  - Section 4.3: Dynamic Table
  - Section 6.1: Integer Representation
  - Section 6.2: String Literal Representation

## 结论

本实现提供了完整的、按照 RFC 7541 标准的 HPACK 表管理功能，包括：
- 61 个预定义的静态表条目
- 可配置的动态表，支持自动淘汰
- 统一的表管理接口
- 全面的测试覆盖（30 个测试用例）
- 详细的中文注释和文档

所有功能都经过充分测试，并正确处理了边界情况和异常情况。
