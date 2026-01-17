#ifndef HTTP2_HEADER_PARSER_H
#define HTTP2_HEADER_PARSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace http2 {

/**
 * @class HeaderParser
 * @brief Parser for HTTP/2 headers
 * 
 * Handles parsing and validation of HTTP/2 headers according to RFC 7540
 */
class HeaderParser {
public:
    /**
     * @brief Parse header block from buffer
     * @param buffer Raw header block bytes
     * @param length Length of header block
     * @return Parsed headers as key-value pairs
     */
    static std::vector<std::pair<std::string, std::string>> parseHeaders(
        const uint8_t* buffer, 
        size_t length
    );

    /**
     * @brief Validate header fields
     * @param headers Headers to validate
     * @return true if headers are valid, false otherwise
     */
    static bool validateHeaders(const std::vector<std::pair<std::string, std::string>>& headers);

    /**
     * @brief Check if header name is valid
     * @param name Header name to check
     * @return true if name is valid, false otherwise
     */
    static bool isValidHeaderName(const std::string& name);

    /**
     * @brief Check if header value is valid
     * @param value Header value to check
     * @return true if value is valid, false otherwise
     */
    static bool isValidHeaderValue(const std::string& value);

private:
    // Private implementation details will be added here
};

} // namespace http2

#endif // HTTP2_HEADER_PARSER_H
