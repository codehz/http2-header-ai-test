#include <gtest/gtest.h>
#include "header_parser.h"

namespace http2 {

/**
 * Test cases for HeaderParser
 */
class HeaderParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures if needed
    }

    void TearDown() override {
        // Clean up after tests if needed
    }
};

/**
 * Test header name validation
 */
TEST_F(HeaderParserTest, ValidHeaderNames) {
    // TODO: Implement test
    EXPECT_TRUE(HeaderParser::isValidHeaderName("content-type"));
    EXPECT_TRUE(HeaderParser::isValidHeaderName("custom-header"));
    EXPECT_FALSE(HeaderParser::isValidHeaderName(""));
}

/**
 * Test header value validation
 */
TEST_F(HeaderParserTest, ValidHeaderValues) {
    // TODO: Implement test
    EXPECT_TRUE(HeaderParser::isValidHeaderValue("application/json"));
    EXPECT_TRUE(HeaderParser::isValidHeaderValue("utf-8"));
    EXPECT_FALSE(HeaderParser::isValidHeaderValue(""));
}

/**
 * Test headers validation
 */
TEST_F(HeaderParserTest, ValidateHeaders) {
    // TODO: Implement test
    std::vector<std::pair<std::string, std::string>> validHeaders = {
        {"content-type", "application/json"},
        {"content-length", "256"}
    };

    EXPECT_TRUE(HeaderParser::validateHeaders(validHeaders));
}

/**
 * Test parsing header block
 */
TEST_F(HeaderParserTest, ParseHeaderBlock) {
    // Create a header block using proper HPACK format
    // Literal Header Field without Indexing (0000 0000) + Name + Value
    
    // Build header using HPACK format
    std::vector<uint8_t> buffer;
    
    // Literal without indexing flag (0x00)
    buffer.push_back(0x00);
    
    // Name: "content-type" (12 bytes)
    // RFC 7541 string encoding: [H bit + length bits]...[data]
    // H=0 (not Huffman), length=12 < 127, so just one byte for length
    buffer.push_back(0x0C);  // 12 bytes
    buffer.insert(buffer.end(), {'c', 'o', 'n', 't', 'e', 'n', 't', '-', 't', 'y', 'p', 'e'});
    
    // Value: "application/json" (16 bytes)
    buffer.push_back(0x10);  // 16 bytes
    buffer.insert(buffer.end(), {'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'j', 's', 'o', 'n'});

    std::vector<std::pair<std::string, std::string>> headers = 
        HeaderParser::parseHeaders(buffer.data(), buffer.size());
    
    // With proper HPACK decoding, this should return at least one header
    EXPECT_FALSE(headers.empty());
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0].first, "content-type");
    EXPECT_EQ(headers[0].second, "application/json");
}

} // namespace http2
