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
    // Create a simple header block with name-value pairs
    // Format: [string_length][string_data][string_length][string_data]
    // Example: "content-type" + "application/json"
    
    // Build header data: "content-type" (12 bytes) + "application/json" (16 bytes)
    uint8_t buffer[] = {
        // First string length (with Huffman bit = 0)
        0x0C,  // 12 bytes for "content-type"
        'c', 'o', 'n', 't', 'e', 'n', 't', '-', 't', 'y', 'p', 'e',
        // Second string length (with Huffman bit = 0)
        0x10,  // 16 bytes for "application/json"
        'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'j', 's', 'o', 'n'
    };
    size_t length = sizeof(buffer);

    std::vector<std::pair<std::string, std::string>> headers = 
        HeaderParser::parseHeaders(buffer, length);
    
    // With proper HPACK decoding, this should return at least one header
    EXPECT_FALSE(headers.empty());
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0].first, "content-type");
    EXPECT_EQ(headers[0].second, "application/json");
}

} // namespace http2
