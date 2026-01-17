#include "header_parser.h"
#include "hpack.h"
#include <algorithm>
#include <cctype>

namespace http2 {

std::vector<std::pair<std::string, std::string>> HeaderParser::parseHeaders(
    const uint8_t* buffer,
    size_t length) {
    // Parse header block using HPACK decompression
    std::vector<std::pair<std::string, std::string>> headers;
    
    if (buffer == nullptr || length == 0) {
        return headers;
    }
    
    // Convert buffer to vector for HPACK decoder
    std::vector<uint8_t> data(buffer, buffer + length);
    
    try {
        headers = HPACK::decode(data);
    } catch (const std::exception& e) {
        // Return empty on decode failure
    }
    
    return headers;
}

bool HeaderParser::validateHeaders(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    // TODO: Implement header validation
    for (const auto& header : headers) {
        if (!isValidHeaderName(header.first) || !isValidHeaderValue(header.second)) {
            return false;
        }
    }
    return true;
}

bool HeaderParser::isValidHeaderName(const std::string& name) {
    // TODO: Implement header name validation according to RFC 7540
    if (name.empty()) {
        return false;
    }
    return true;
}

bool HeaderParser::isValidHeaderValue(const std::string& value) {
    // TODO: Implement header value validation according to RFC 7540
    // RFC 7540: Header field values are subject to similar restrictions
    // Empty values should be rejected
    if (value.empty()) {
        return false;
    }
    return true;
}

} // namespace http2
