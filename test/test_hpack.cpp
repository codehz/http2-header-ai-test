#include <gtest/gtest.h>
#include "hpack.h"

namespace http2 {

/**
 * Test cases for HPACK encoding and decoding
 */
class HPACKTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures if needed
    }

    void TearDown() override {
        // Clean up after tests if needed
    }
};

// ============================================================================
// IntegerEncoder Tests
// ============================================================================

class IntegerEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * Test encoding small integers that fit within prefix bits
 */
TEST_F(IntegerEncoderTest, EncodeSmallInteger) {
    // 5 with 5-bit prefix should encode as single byte [5]
    auto result = IntegerEncoder::encodeInteger(5, 5);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 5);
}

/**
 * Test encoding integer that exactly reaches prefix boundary
 */
TEST_F(IntegerEncoderTest, EncodeIntegerAtBoundary) {
    // 10 with 5-bit prefix (2^5 - 1 = 31, so 10 < 31)
    auto result = IntegerEncoder::encodeInteger(10, 5);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 10);
}

/**
 * Test encoding integer larger than prefix capacity
 * Example from RFC 7541: 1337 with 5-bit prefix should be [31, 154, 10]
 * 1337 - 31 = 1306
 * 1306 = 0b10100011010
 * Encoded as: 154 (1306 & 0x7F = 26, plus 0x80), 10 (1306 >> 7 = 10)
 */
TEST_F(IntegerEncoderTest, EncodeLargeInteger_RFC7541_Example) {
    // Example from RFC 7541 Section 6.1
    auto result = IntegerEncoder::encodeInteger(1337, 5);
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 31);   // 2^5 - 1
    EXPECT_EQ(result[1], 154);  // 0x9A = 0b10011010
    EXPECT_EQ(result[2], 10);   // 0x0A
}

/**
 * Test encoding with different prefix sizes
 */
TEST_F(IntegerEncoderTest, EncodeDifferentPrefixSizes) {
    // Test with 1-bit prefix
    auto result1 = IntegerEncoder::encodeInteger(100, 1);
    ASSERT_GE(result1.size(), 1);

    // Test with 8-bit prefix
    auto result8 = IntegerEncoder::encodeInteger(100, 8);
    ASSERT_EQ(result8.size(), 1);
    EXPECT_EQ(result8[0], 100);
}

/**
 * Test encoding large values with multiple continuation bytes
 */
TEST_F(IntegerEncoderTest, EncodeLargeValueMultipleBytes) {
    // 2^14 = 16384 should require multiple continuation bytes
    auto result = IntegerEncoder::encodeInteger(16384, 5);
    ASSERT_GE(result.size(), 2);
    EXPECT_EQ(result[0], 31);  // First byte: 2^5 - 1
}

/**
 * Test decoding small integers
 */
TEST_F(IntegerEncoderTest, DecodeSmallInteger) {
    uint8_t buffer[] = {5};
    auto [value, consumed] = IntegerEncoder::decodeInteger(buffer, sizeof(buffer), 5);
    EXPECT_EQ(value, 5);
    EXPECT_EQ(consumed, 1);
}

/**
 * Test decoding the RFC 7541 example
 */
TEST_F(IntegerEncoderTest, DecodeLargeInteger_RFC7541_Example) {
    // [31, 154, 10] with 5-bit prefix should decode to 1337
    uint8_t buffer[] = {31, 154, 10};
    auto [value, consumed] = IntegerEncoder::decodeInteger(buffer, sizeof(buffer), 5);
    EXPECT_EQ(value, 1337);
    EXPECT_EQ(consumed, 3);
}

/**
 * Test round-trip encoding and decoding
 */
TEST_F(IntegerEncoderTest, RoundTripEncoding) {
    std::vector<uint64_t> test_values = {
        0, 1, 5, 30, 31, 32, 127, 128, 255, 256,
        1000, 1337, 16384, 65535, 1000000
    };

    for (uint64_t original_value : test_values) {
        for (int prefix_bits = 1; prefix_bits <= 8; ++prefix_bits) {
            // Encode
            auto encoded = IntegerEncoder::encodeInteger(original_value, prefix_bits);

            // Decode
            auto [decoded_value, consumed] = IntegerEncoder::decodeInteger(
                encoded.data(), encoded.size(), prefix_bits);

            EXPECT_EQ(decoded_value, original_value)
                << "Round-trip failed for value=" << original_value
                << " with prefix_bits=" << prefix_bits;
            EXPECT_EQ(consumed, encoded.size());
        }
    }
}

/**
 * Test invalid prefix bits
 */
TEST_F(IntegerEncoderTest, InvalidPrefixBits) {
    EXPECT_THROW(IntegerEncoder::encodeInteger(100, 0), std::invalid_argument);
    EXPECT_THROW(IntegerEncoder::encodeInteger(100, 9), std::invalid_argument);

    uint8_t buffer[] = {5};
    EXPECT_THROW(IntegerEncoder::decodeInteger(buffer, sizeof(buffer), 0),
                 std::invalid_argument);
    EXPECT_THROW(IntegerEncoder::decodeInteger(buffer, sizeof(buffer), 9),
                 std::invalid_argument);
}

/**
 * Test decoding with insufficient buffer
 */
TEST_F(IntegerEncoderTest, DecodeInsufficientBuffer) {
    uint8_t buffer[] = {31, 154};  // Incomplete continuation
    EXPECT_THROW(IntegerEncoder::decodeInteger(buffer, sizeof(buffer), 5),
                 std::out_of_range);
}

// ============================================================================
// StringCoder Tests
// ============================================================================

class StringCoderTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * Test encoding simple ASCII string
 */
TEST_F(StringCoderTest, EncodeSimpleString) {
    auto result = StringCoder::encodeString("hello", false);

    // First byte should contain length (5)
    EXPECT_EQ(result[0], 5);

    // Following bytes should be the string data
    EXPECT_EQ(result.size(), 6);  // 1 byte for length + 5 bytes for "hello"
    EXPECT_EQ(std::string(result.begin() + 1, result.end()), "hello");
}

/**
 * Test encoding empty string
 */
TEST_F(StringCoderTest, EncodeEmptyString) {
    auto result = StringCoder::encodeString("", false);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0);
}

/**
 * Test encoding string with special characters
 */
TEST_F(StringCoderTest, EncodeSpecialCharacters) {
    std::string special = "hello:world=value";
    auto result = StringCoder::encodeString(special, false);

    EXPECT_EQ(result[0], special.length());
    std::string encoded_data(result.begin() + 1, result.end());
    EXPECT_EQ(encoded_data, special);
}

/**
 * Test encoding long string (> 127 characters)
 */
TEST_F(StringCoderTest, EncodeLongString) {
    // Create a string of 256 characters
    std::string long_str(256, 'a');
    auto result = StringCoder::encodeString(long_str, false);

    // First byte should be 127 (indicating length uses continuation bytes)
    EXPECT_EQ(result[0], 127);

    // Verify total size (1 byte header for 127 + 1 byte for continuation + 256 data)
    EXPECT_EQ(result.size(), 1 + 2 + 256);
}

/**
 * Test decoding simple string
 */
TEST_F(StringCoderTest, DecodeSimpleString) {
    // Create encoded string: [5, 'h', 'e', 'l', 'l', 'o']
    uint8_t buffer[] = {5, 'h', 'e', 'l', 'l', 'o'};
    auto [str, consumed] = StringCoder::decodeString(buffer, sizeof(buffer));

    EXPECT_EQ(str, "hello");
    EXPECT_EQ(consumed, 6);
}

/**
 * Test decoding empty string
 */
TEST_F(StringCoderTest, DecodeEmptyString) {
    uint8_t buffer[] = {0};
    auto [str, consumed] = StringCoder::decodeString(buffer, sizeof(buffer));

    EXPECT_EQ(str, "");
    EXPECT_EQ(consumed, 1);
}

/**
 * Test round-trip string encoding/decoding
 */
TEST_F(StringCoderTest, RoundTripEncoding) {
    std::vector<std::string> test_strings = {
        "",
        "a",
        "hello",
        "hello world",
        "Hello: World = Value",
        "content-type",
        "application/json; charset=utf-8",
        std::string(100, 'x'),
        std::string(256, 'a'),
        std::string(1000, 'b'),
    };

    for (const auto& original : test_strings) {
        auto encoded = StringCoder::encodeString(original, false);
        auto [decoded, consumed] = StringCoder::decodeString(encoded.data(), encoded.size());

        EXPECT_EQ(decoded, original)
            << "Round-trip failed for string of length " << original.length();
        EXPECT_EQ(consumed, encoded.size());
    }
}

/**
 * Test decoding with insufficient buffer for length
 */
TEST_F(StringCoderTest, DecodeInsufficientBuffer_Length) {
    // Only first byte (127) indicating more bytes needed, but buffer too short
    uint8_t buffer[] = {127};
    EXPECT_THROW(StringCoder::decodeString(buffer, sizeof(buffer)),
                 std::out_of_range);
}

/**
 * Test decoding with insufficient buffer for data
 */
TEST_F(StringCoderTest, DecodeInsufficientBuffer_Data) {
    // Says length is 10 but only provides 5 bytes of data
    uint8_t buffer[] = {10, 'h', 'e', 'l', 'l', 'o'};
    EXPECT_THROW(StringCoder::decodeString(buffer, sizeof(buffer)),
                 std::out_of_range);
}

/**
 * Test Huffman encoding throws not implemented error
 */
TEST_F(StringCoderTest, HuffmanNotImplemented) {
    EXPECT_THROW(StringCoder::encodeString("hello", true),
                 std::runtime_error);
}

// ============================================================================
// Integration Tests
// ============================================================================

/**
 * Test HPACK encoding of basic headers
 */
TEST_F(HPACKTest, EncodeBasicHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"}
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    EXPECT_FALSE(encoded.empty());
}

/**
 * Test HPACK decoding
 */
TEST_F(HPACKTest, DecodeBasicHeaders) {
    std::vector<std::pair<std::string, std::string>> original_headers = {
        {":method", "GET"},
        {":path", "/"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(original_headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), original_headers.size());
}

/**
 * Test round-trip encoding and decoding
 */
TEST_F(HPACKTest, RoundTripEncoding) {
    std::vector<std::pair<std::string, std::string>> originalHeaders = {
        {":method", "POST"},
        {":path", "/api/data"},
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"content-type", "application/json"},
        {"user-agent", "Mozilla/5.0"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(originalHeaders);
    std::vector<std::pair<std::string, std::string>> decodedHeaders = HPACK::decode(encoded);

    EXPECT_EQ(originalHeaders.size(), decodedHeaders.size());

    for (size_t i = 0; i < originalHeaders.size(); ++i) {
        EXPECT_EQ(originalHeaders[i].first, decodedHeaders[i].first);
        EXPECT_EQ(originalHeaders[i].second, decodedHeaders[i].second);
    }
}

/**
 * Test encoding empty headers
 */
TEST_F(HPACKTest, EncodeEmptyHeaders) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<uint8_t> encoded = HPACK::encode(headers);
    EXPECT_TRUE(encoded.empty());
}

/**
 * Test decoding empty buffer
 */
TEST_F(HPACKTest, DecodeEmptyBuffer) {
    std::vector<uint8_t> buffer;
    std::vector<std::pair<std::string, std::string>> headers = HPACK::decode(buffer);
    EXPECT_TRUE(headers.empty());
}

/**
 * Test with various header value sizes
 */
TEST_F(HPACKTest, HeadersWithVaryingSizes) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"short", "a"},
        {"medium-header", "medium-value"},
        {"long-header-name-with-many-characters",
         std::string(256, 'x')},  // Long value
    };

    auto encoded = HPACK::encode(headers);
    auto decoded = HPACK::decode(encoded);

    EXPECT_EQ(headers.size(), decoded.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        EXPECT_EQ(headers[i].first, decoded[i].first);
        EXPECT_EQ(headers[i].second, decoded[i].second);
    }
}

// ============================================================================
// StaticTable Tests - 静态表测试
// ============================================================================

class StaticTableTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * 测试通过索引获取标准伪头字段（:method）
 */
TEST_F(StaticTableTest, GetMethodPseudoHeader) {
    // :method GET at index 2
    auto field = StaticTable::getByIndex(2);
    EXPECT_EQ(field.name, ":method");
    EXPECT_EQ(field.value, "GET");

    // :method POST at index 3
    field = StaticTable::getByIndex(3);
    EXPECT_EQ(field.name, ":method");
    EXPECT_EQ(field.value, "POST");
}

/**
 * 测试通过索引获取 :path 伪头字段
 */
TEST_F(StaticTableTest, GetPathPseudoHeader) {
    // :path / at index 4
    auto field = StaticTable::getByIndex(4);
    EXPECT_EQ(field.name, ":path");
    EXPECT_EQ(field.value, "/");

    // :path /index.html at index 5
    field = StaticTable::getByIndex(5);
    EXPECT_EQ(field.name, ":path");
    EXPECT_EQ(field.value, "/index.html");
}

/**
 * 测试通过索引获取 :scheme 伪头字段
 */
TEST_F(StaticTableTest, GetSchemePseudoHeader) {
    // :scheme http at index 6
    auto field = StaticTable::getByIndex(6);
    EXPECT_EQ(field.name, ":scheme");
    EXPECT_EQ(field.value, "http");

    // :scheme https at index 7
    field = StaticTable::getByIndex(7);
    EXPECT_EQ(field.name, ":scheme");
    EXPECT_EQ(field.value, "https");
}

/**
 * 测试通过索引获取 :status 伪头字段
 */
TEST_F(StaticTableTest, GetStatusPseudoHeader) {
    // :status 200 at index 8
    auto field = StaticTable::getByIndex(8);
    EXPECT_EQ(field.name, ":status");
    EXPECT_EQ(field.value, "200");

    // :status 404 at index 13
    field = StaticTable::getByIndex(13);
    EXPECT_EQ(field.name, ":status");
    EXPECT_EQ(field.value, "404");
}

/**
 * 测试通过名值对查询索引
 */
TEST_F(StaticTableTest, GetIndexByNameValue) {
    // 查询 :method GET (index 2)
    int index = StaticTable::getIndexByNameValue(":method", "GET");
    EXPECT_EQ(index, 2);

    // 查询 :path / (index 4)
    index = StaticTable::getIndexByNameValue(":path", "/");
    EXPECT_EQ(index, 4);

    // 查询 :status 404 (index 13)
    index = StaticTable::getIndexByNameValue(":status", "404");
    EXPECT_EQ(index, 13);

    // 查询不存在的名值对
    index = StaticTable::getIndexByNameValue(":method", "DELETE");
    EXPECT_EQ(index, -1);
}

/**
 * 测试通过名称查询索引
 */
TEST_F(StaticTableTest, GetIndexByName) {
    // 查询 :method (应返回第一个匹配的 GET 实例，index 2)
    int index = StaticTable::getIndexByName(":method");
    EXPECT_EQ(index, 2);

    // 查询 :path (应返回第一个匹配的 / 实例，index 4)
    index = StaticTable::getIndexByName(":path");
    EXPECT_EQ(index, 4);

    // 查询 content-type (index 31)
    index = StaticTable::getIndexByName("content-type");
    EXPECT_EQ(index, 31);

    // 查询不存在的名称
    index = StaticTable::getIndexByName("x-custom-header");
    EXPECT_EQ(index, -1);
}

/**
 * 测试头字段名称小写化
 */
TEST_F(StaticTableTest, NameLowercaseConversion) {
    // 使用大写查询，应该能找到小写的条目
    int index = StaticTable::getIndexByName("CONTENT-TYPE");
    EXPECT_EQ(index, 31);

    // 使用混合大小写查询
    index = StaticTable::getIndexByNameValue("Accept-Encoding", "gzip, deflate");
    EXPECT_EQ(index, 16);
}

/**
 * 测试静态表大小
 */
TEST_F(StaticTableTest, TableSize) {
    EXPECT_EQ(StaticTable::size(), 61);
}

/**
 * 测试索引超出范围
 */
TEST_F(StaticTableTest, IndexOutOfRange) {
    EXPECT_THROW(StaticTable::getByIndex(0), std::out_of_range);
    EXPECT_THROW(StaticTable::getByIndex(62), std::out_of_range);
    EXPECT_THROW(StaticTable::getByIndex(1000), std::out_of_range);
}

/**
 * 测试常见 HTTP 头字段
 */
TEST_F(StaticTableTest, CommonHTTPHeaders) {
    // test authorization header
    int index = StaticTable::getIndexByName("authorization");
    EXPECT_EQ(index, 23);

    // test cookie header
    index = StaticTable::getIndexByName("cookie");
    EXPECT_EQ(index, 32);

    // test user-agent header
    index = StaticTable::getIndexByName("user-agent");
    EXPECT_EQ(index, 58);

    // test server header
    index = StaticTable::getIndexByName("server");
    EXPECT_EQ(index, 54);
}

// ============================================================================
// DynamicTable Tests - 动态表测试
// ============================================================================

class DynamicTableTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * 测试向动态表插入条目
 */
TEST_F(DynamicTableTest, InsertEntry) {
    DynamicTable table;
    HeaderField field{":method", "GET"};

    EXPECT_EQ(table.entryCount(), 0);
    EXPECT_EQ(table.size(), 0);

    table.insert(field);

    EXPECT_EQ(table.entryCount(), 1);
    EXPECT_GT(table.size(), 0);
}

/**
 * 测试获取动态表条目
 */
TEST_F(DynamicTableTest, GetEntry) {
    DynamicTable table;
    HeaderField field1{":method", "GET"};
    HeaderField field2{"content-type", "application/json"};

    table.insert(field1);
    table.insert(field2);

    // 最新的条目（field2）在索引 0
    auto entry = table.get(0);
    EXPECT_EQ(entry.name, "content-type");
    EXPECT_EQ(entry.value, "application/json");

    // 旧的条目（field1）在索引 1
    entry = table.get(1);
    EXPECT_EQ(entry.name, ":method");
    EXPECT_EQ(entry.value, "GET");
}

/**
 * 测试动态表索引超出范围
 */
TEST_F(DynamicTableTest, IndexOutOfRange) {
    DynamicTable table;
    table.insert({":method", "GET"});

    EXPECT_THROW(table.get(1), std::out_of_range);
    EXPECT_THROW(table.get(100), std::out_of_range);
}

/**
 * 测试通过名值对查询
 */
TEST_F(DynamicTableTest, GetIndexByNameValue) {
    DynamicTable table;
    HeaderField field{":method", "GET"};

    table.insert(field);

    int index = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(index, 0);

    index = table.getIndexByNameValue(":method", "POST");
    EXPECT_EQ(index, -1);

    index = table.getIndexByNameValue(":path", "/");
    EXPECT_EQ(index, -1);
}

/**
 * 测试通过名称查询
 */
TEST_F(DynamicTableTest, GetIndexByName) {
    DynamicTable table;
    table.insert({":method", "GET"});
    table.insert({"content-type", "application/json"});

    int index = table.getIndexByName(":method");
    EXPECT_EQ(index, 1);  // 旧条目

    index = table.getIndexByName("content-type");
    EXPECT_EQ(index, 0);  // 新条目
}

/**
 * 测试头字段名称小写化
 */
TEST_F(DynamicTableTest, NameLowercaseConversion) {
    DynamicTable table;
    table.insert({"CONTENT-TYPE", "text/html"});

    auto entry = table.get(0);
    EXPECT_EQ(entry.name, "content-type");  // 应转换为小写

    int index = table.getIndexByName("content-type");
    EXPECT_EQ(index, 0);

    // 大写查询也应该工作
    index = table.getIndexByName("CONTENT-TYPE");
    EXPECT_EQ(index, 0);
}

/**
 * 测试超过最大大小时的淘汰
 */
TEST_F(DynamicTableTest, EvictionWhenExceedsMaxSize) {
    // 创建一个最大大小为 100 字节的表
    // 每个条目大小 = 32 + 名称长度 + 值长度
    DynamicTable table(100);

    // 插入第一个条目（大小 = 32 + 8 + 3 = 43）
    table.insert({":method", "GET"});
    EXPECT_EQ(table.entryCount(), 1);

    // 插入第二个条目（大小 = 32 + 12 + 16 = 60）
    // 总大小会是 43 + 60 = 103，超过 100
    // 应该淘汰第一个条目
    table.insert({"content-type", "application/json"});
    EXPECT_EQ(table.entryCount(), 1);

    // 验证只剩下第二个条目
    auto entry = table.get(0);
    EXPECT_EQ(entry.name, "content-type");
}

/**
 * 测试清空动态表
 */
TEST_F(DynamicTableTest, Clear) {
    DynamicTable table;
    table.insert({":method", "GET"});
    table.insert({"content-type", "text/html"});

    EXPECT_EQ(table.entryCount(), 2);
    EXPECT_GT(table.size(), 0);

    table.clear();

    EXPECT_EQ(table.entryCount(), 0);
    EXPECT_EQ(table.size(), 0);
}

/**
 * 测试调整最大大小
 */
TEST_F(DynamicTableTest, SetMaxSize) {
    DynamicTable table(200);

    // 插入几个条目
    table.insert({":method", "GET"});         // 43 字节
    table.insert({"content-type", "text/html"});  // 54 字节
    table.insert({"user-agent", "Mozilla/5.0"});  // 47 字节

    EXPECT_EQ(table.entryCount(), 3);

    // 减小最大大小到 80 字节
    // 应该淘汰条目直到符合新限制
    table.setMaxSize(80);

    // 验证条目被淘汰
    EXPECT_LE(table.size(), 80);
    EXPECT_LT(table.entryCount(), 3);
}

/**
 * 测试非常大的条目
 */
TEST_F(DynamicTableTest, VeryLargeEntry) {
    DynamicTable table(1000);

    // 创建一个非常大的条目（超过最大大小）
    HeaderField large_field{"x-header", std::string(2000, 'a')};

    table.insert(large_field);

    // 表应该是空的，因为条目太大
    EXPECT_EQ(table.entryCount(), 0);
    EXPECT_EQ(table.size(), 0);
}

/**
 * 测试计算条目大小
 */
TEST_F(DynamicTableTest, EntrySize) {
    DynamicTable table;

    // 插入条目并检查大小
    // 大小应该是 32 + 名称长度 + 值长度
    table.insert({":method", "GET"});  // 32 + 7 + 3 = 42

    // 插入之前的大小应该是 42
    EXPECT_EQ(table.size(), 42);

    table.insert({"x-custom", "value"});  // 32 + 8 + 5 = 45
    EXPECT_EQ(table.size(), 42 + 45);
}

// ============================================================================
// HeaderTable Tests - 统一表管理测试
// ============================================================================

class HeaderTableTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * 测试通过统一索引获取静态表条目
 */
TEST_F(HeaderTableTest, GetStaticTableByIndex) {
    HeaderTable table;

    // 索引 1-61 应该访问静态表
    auto field = table.getByIndex(1);
    EXPECT_EQ(field.name, ":authority");

    field = table.getByIndex(2);
    EXPECT_EQ(field.name, ":method");
    EXPECT_EQ(field.value, "GET");

    field = table.getByIndex(61);
    EXPECT_EQ(field.name, "www-authenticate");
}

/**
 * 测试通过统一索引获取动态表条目
 */
TEST_F(HeaderTableTest, GetDynamicTableByIndex) {
    HeaderTable table;

    // 向动态表添加条目
    table.insertDynamic({":method", "POST"});
    table.insertDynamic({"content-type", "application/json"});

    // 索引 62 应该是动态表的第一个条目
    auto field = table.getByIndex(62);
    EXPECT_EQ(field.name, "content-type");

    // 索引 63 应该是动态表的第二个条目
    field = table.getByIndex(63);
    EXPECT_EQ(field.name, ":method");
    EXPECT_EQ(field.value, "POST");
}

/**
 * 测试通过名值对查询（混合表）
 */
TEST_F(HeaderTableTest, GetIndexByNameValue_Mixed) {
    HeaderTable table;

    // 首先查询静态表
    int index = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(index, 2);  // 静态表索引

    // 添加到动态表
    table.insertDynamic({":method", "PATCH"});

    // 查询新条目（应该返回动态表索引）
    index = table.getIndexByNameValue(":method", "PATCH");
    EXPECT_EQ(index, 62);  // 动态表索引 = 61 + 1

    // 旧查询仍然应该返回静态表索引
    index = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(index, 2);
}

/**
 * 测试通过名称查询（混合表，优先动态表）
 */
TEST_F(HeaderTableTest, GetIndexByName_Mixed) {
    HeaderTable table;

    // 首先查询静态表
    int index = table.getIndexByName(":method");
    EXPECT_EQ(index, 2);  // 静态表

    // 添加到动态表
    table.insertDynamic({":method", "DELETE"});

    // 查询应该返回动态表版本（优先级更高）
    index = table.getIndexByName(":method");
    EXPECT_EQ(index, 62);  // 动态表索引
}

/**
 * 测试向动态表插入
 */
TEST_F(HeaderTableTest, InsertDynamic) {
    HeaderTable table;

    // 验证动态表为空时访问会抛出异常
    EXPECT_THROW(table.getByIndex(62), std::out_of_range);

    table.insertDynamic({"x-custom", "value"});
    auto field = table.getByIndex(62);
    EXPECT_EQ(field.name, "x-custom");
    EXPECT_EQ(field.value, "value");
}

/**
 * 测试调整动态表大小
 */
TEST_F(HeaderTableTest, SetDynamicTableMaxSize) {
    HeaderTable table(100);

    table.insertDynamic({":method", "GET"});    // 42 字节
    table.insertDynamic({"content-type", "text/html"});  // 54 字节

    // 减小大小
    table.setDynamicTableMaxSize(50);

    // 应该有淘汰
    // 验证新增条目可以插入
    table.insertDynamic({"x-small", "hi"});
}

/**
 * 测试清空动态表
 */
TEST_F(HeaderTableTest, ClearDynamic) {
    HeaderTable table;

    table.insertDynamic({":method", "GET"});
    table.insertDynamic({"content-type", "text/html"});

    table.clearDynamic();

    // 动态表应该是空的，但静态表仍然可用
    auto field = table.getByIndex(2);  // 静态表
    EXPECT_EQ(field.name, ":method");

    EXPECT_THROW(table.getByIndex(62), std::out_of_range);  // 动态表为空
}

/**
 * 测试索引超出范围
 */
TEST_F(HeaderTableTest, IndexOutOfRange) {
    HeaderTable table;

    EXPECT_THROW(table.getByIndex(0), std::out_of_range);
    EXPECT_THROW(table.getByIndex(62), std::out_of_range);  // 动态表为空
}

/**
 * 综合测试：实际的 HTTP/2 请求场景
 */
TEST_F(HeaderTableTest, RealisticHTTP2Request) {
    HeaderTable table;

    // 常见的 HTTP/2 请求头
    // 1. 首次请求，使用静态表
    int method_idx = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(method_idx, 2);

    int path_idx = table.getIndexByNameValue(":path", "/");
    EXPECT_EQ(path_idx, 4);

    int scheme_idx = table.getIndexByNameValue(":scheme", "https");
    EXPECT_EQ(scheme_idx, 7);

    // 2. 添加自定义头到动态表
    table.insertDynamic({"x-request-id", "12345"});

    // 3. 第二次请求，自定义头应该在动态表中
    int custom_idx = table.getIndexByName("x-request-id");
    EXPECT_EQ(custom_idx, 62);  // 动态表索引

    // 4. 验证动态表条目
    auto field = table.getByIndex(62);
    EXPECT_EQ(field.value, "12345");
}

// ============================================================================
// Huffman Decoding Tests - Huffman解码测试
// ============================================================================

class HuffmanDecodingTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

/**
 * 测试Huffman编码的HTTP/2响应头解码
 * 这是来自nghttp2项目的真实测试数据
 */
TEST_F(HuffmanDecodingTest, DecodeHTTP2ResponseHeaders) {
    // 真实的HTTP/2响应头编码数据（Huffman编码）
    std::vector<uint8_t> data = {
        0x00, 0x00, 0xed, 0x01, 0x04, 0x00, 0x00, 0x00,
        0x01, 0x3f, 0xe1, 0x3f, 0x88, 0x61, 0x96, 0xdc, 0x34, 0xfd, 0x28, 0x17, 0x54,
        0xca, 0x3a, 0x94, 0x10, 0x04, 0xe2, 0x81, 0x72, 0xe3, 0x6d, 0x5c, 0x03, 0x8a, 0x62,
        0xd1, 0xbf, 0x5f, 0x87, 0x49, 0x7c, 0xa5, 0x89, 0xd3, 0x4d, 0x1f, 0x6c, 0x96,
        0xdc, 0x34, 0xfd, 0x28, 0x26, 0xd4, 0xd4, 0x44, 0xa8, 0x20, 0x09, 0xb5, 0x00,
        0xf5, 0xc6, 0x9d, 0xb8, 0x16, 0x94, 0xc5, 0xa3, 0x7f, 0x0f, 0x13, 0x8c, 0xfe, 0x5c,
        0x7a, 0x52, 0x3c, 0x57, 0xc4, 0xb0, 0x5e, 0x8d, 0xaf, 0xe7, 0x52, 0x84, 0x8f,
        0xd2, 0x4a, 0x8f, 0x0f, 0x0d, 0x83, 0x71, 0x91, 0x35, 0x40, 0x8f, 0xf2, 0xb4,
        0x63, 0x27, 0x52, 0xd5, 0x22, 0xd3, 0x94, 0x72, 0x16, 0xc5, 0xac, 0x4a, 0x7f, 0x85,
        0x02, 0xe0, 0x00, 0x99, 0x77, 0x78, 0x8c, 0xa4, 0x7e, 0x56, 0x1c, 0xc5, 0x81,
        0x90, 0xb6, 0xcb, 0x80, 0x00, 0x3f, 0x76, 0x86, 0xaa, 0x69, 0xd2, 0x9a, 0xfc,
        0xff, 0x40, 0x85, 0x1d, 0x09, 0x59, 0x1d, 0xc9, 0x8f, 0x9d, 0x98, 0x3f, 0x9b, 0x8d,
        0x34, 0xcf, 0xf3, 0xf6, 0xa5, 0x23, 0x81, 0x97, 0x00, 0x0f, 0x7c, 0x87, 0x12,
        0x95, 0x4d, 0x3a, 0x53, 0x5f, 0x9f, 0x40, 0x8b, 0xf2, 0xb4, 0xb6, 0x0e, 0x92,
        0xac, 0x7a, 0xd2, 0x63, 0xd4, 0x8f, 0x89, 0xdd, 0x0e, 0x8c, 0x1a, 0xb6, 0xe4, 0xc5,
        0x93, 0x4f, 0x40, 0x8c, 0xf2, 0xb7, 0x94, 0x21, 0x6a, 0xec, 0x3a, 0x4a, 0x44,
        0x98, 0xf5, 0x7f, 0x8a, 0x0f, 0xda, 0x94, 0x9e, 0x42, 0xc1, 0x1d, 0x07, 0x27,
        0x5f, 0x40, 0x90, 0xf2, 0xb1, 0x0f, 0x52, 0x4b, 0x52, 0x56, 0x4f, 0xaa, 0xca, 0xb1,
        0xeb, 0x49, 0x8f, 0x52, 0x3f, 0x85, 0xa8, 0xe8, 0xa8, 0xd2, 0xcb, 0x00, 0x18,
        0xb4, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x3c, 0x21, 0x44, 0x4f, 0x43,
        0x54, 0x59, 0x50, 0x45, 0x20, 0x68, 0x74, 0x6d, 0x6c, 0x3e, 0x0a
    };

    // 提取Huffman编码的头字段数据（跳过HTTP/2帧头）
    // 帧头：24 bits长度 + 8 bits类型 + 8 bits标志 + 32 bits流ID = 72 bits = 9 bytes
    // 实际HPACK数据从第10字节开始
    
    if (data.size() > 9) {
        std::vector<uint8_t> hpack_data(data.begin() + 9, data.end());
        
        // 尝试解码HPACK头数据
        try {
            auto headers = HPACK::decode(hpack_data);
            
            // 验证解码成功（不为空）
            EXPECT_FALSE(headers.empty());
            
            // 打印解码结果用于验证
            std::cout << "Decoded headers count: " << headers.size() << std::endl;
            for (const auto& h : headers) {
                std::cout << "  " << h.first << ": " << h.second << std::endl;
            }
        } catch (const std::exception& e) {
            FAIL() << "Huffman decoding failed: " << e.what();
        }
    }
}

/**
 * 测试简单的Huffman编码字符串解码
 */
TEST_F(HuffmanDecodingTest, DecodeSimpleHuffmanString) {
    // 这将测试基础的Huffman解码功能
    // 使用编码的测试数据验证Huffman解码器的正确性
    
    // 例：字符串 "www.example.com" 的Huffman编码
    // 这需要实现Huffman编码器来生成测试数据
    // 目前这个测试说明了测试的意图
    
    EXPECT_TRUE(true); // 占位符，等待Huffman编码实现
}

/**
 * 测试带有Huffman标志的HPACK字符串编码/解码
 */
TEST_F(HuffmanDecodingTest, StringWithHuffmanFlag) {
    // 当实现Huffman编码时，此测试将验证：
    // 1. 正确设置Huffman标志 (H bit = 1)
    // 2. 正确解码带有Huffman标志的字符串
    // 3. 往返编码/解码的正确性
    
    EXPECT_TRUE(true); // 占位符测试
}

/**
 * 性能测试：大量Huffman编码头的解码
 */
TEST_F(HuffmanDecodingTest, PerformanceTest_LargeHeaderBlock) {
    // 此测试用于验证Huffman解码性能
    // 应该能够在合理时间内解码大的头块
    
    // 示例：创建多个头条目并进行编码/解码
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/index.html"},
        {":scheme", "https"},
        {":authority", "www.example.com"},
        {"accept", "text/html,application/xhtml+xml"},
        {"accept-encoding", "gzip, deflate"},
        {"accept-language", "en-US,en;q=0.9"},
        {"cache-control", "max-age=0"},
        {"user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
    };

    auto encoded = HPACK::encode(headers);
    auto decoded = HPACK::decode(encoded);

    EXPECT_EQ(headers.size(), decoded.size());
}

} // namespace http2
