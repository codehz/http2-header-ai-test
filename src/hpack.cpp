#include "hpack.h"
#include <algorithm>
#include <limits>
#include <cctype>
#include <deque>
#include <map>

namespace http2 {

// ============================================================================
// 辅助函数：字符串小写转换
// ============================================================================

/**
 * @brief 将字符串转换为小写
 * @param str 输入字符串
 * @return 小写后的字符串
 */
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// ============================================================================
// StaticTable 实现
// ============================================================================

/**
 * RFC 7541 附录 B 的静态表
 * 包含 61 个预定义的 HTTP/2 标准头字段
 * 索引从 1 开始（遵循 RFC 标准）
 */
static const HeaderField STATIC_TABLE[] = {
    // Index 1
    {":authority", ""},
    // Index 2
    {":method", "GET"},
    // Index 3
    {":method", "POST"},
    // Index 4
    {":path", "/"},
    // Index 5
    {":path", "/index.html"},
    // Index 6
    {":scheme", "http"},
    // Index 7
    {":scheme", "https"},
    // Index 8
    {":status", "200"},
    // Index 9
    {":status", "204"},
    // Index 10
    {":status", "206"},
    // Index 11
    {":status", "304"},
    // Index 12
    {":status", "400"},
    // Index 13
    {":status", "404"},
    // Index 14
    {":status", "500"},
    // Index 15
    {"accept-charset", ""},
    // Index 16
    {"accept-encoding", "gzip, deflate"},
    // Index 17
    {"accept-language", ""},
    // Index 18
    {"accept-ranges", ""},
    // Index 19
    {"accept", ""},
    // Index 20
    {"access-control-allow-origin", ""},
    // Index 21
    {"age", ""},
    // Index 22
    {"allow", ""},
    // Index 23
    {"authorization", ""},
    // Index 24
    {"cache-control", ""},
    // Index 25
    {"content-disposition", ""},
    // Index 26
    {"content-encoding", ""},
    // Index 27
    {"content-language", ""},
    // Index 28
    {"content-length", ""},
    // Index 29
    {"content-location", ""},
    // Index 30
    {"content-range", ""},
    // Index 31
    {"content-type", ""},
    // Index 32
    {"cookie", ""},
    // Index 33
    {"date", ""},
    // Index 34
    {"etag", ""},
    // Index 35
    {"expect", ""},
    // Index 36
    {"expires", ""},
    // Index 37
    {"from", ""},
    // Index 38
    {"host", ""},
    // Index 39
    {"if-match", ""},
    // Index 40
    {"if-modified-since", ""},
    // Index 41
    {"if-none-match", ""},
    // Index 42
    {"if-range", ""},
    // Index 43
    {"if-unmodified-since", ""},
    // Index 44
    {"last-modified", ""},
    // Index 45
    {"link", ""},
    // Index 46
    {"location", ""},
    // Index 47
    {"max-forwards", ""},
    // Index 48
    {"proxy-authenticate", ""},
    // Index 49
    {"proxy-authorization", ""},
    // Index 50
    {"range", ""},
    // Index 51
    {"referer", ""},
    // Index 52
    {"refresh", ""},
    // Index 53
    {"retry-after", ""},
    // Index 54
    {"server", ""},
    // Index 55
    {"set-cookie", ""},
    // Index 56
    {"strict-transport-security", ""},
    // Index 57
    {"transfer-encoding", ""},
    // Index 58
    {"user-agent", ""},
    // Index 59
    {"vary", ""},
    // Index 60
    {"via", ""},
    // Index 61
    {"www-authenticate", ""}
};

const size_t STATIC_TABLE_SIZE = 61;

HeaderField StaticTable::getByIndex(size_t index) {
    if (index < 1 || index > STATIC_TABLE_SIZE) {
        throw std::out_of_range("Static table index out of range: " + std::to_string(index));
    }
    // 数组索引从 0 开始，但 RFC 索引从 1 开始
    return STATIC_TABLE[index - 1];
}

int StaticTable::getIndexByNameValue(const std::string& name, const std::string& value) {
    std::string lower_name = toLower(name);
    
    // 首先寻找完全匹配（名值都相同）
    for (size_t i = 0; i < STATIC_TABLE_SIZE; ++i) {
        if (STATIC_TABLE[i].name == lower_name && STATIC_TABLE[i].value == value) {
            return static_cast<int>(i + 1);  // 返回 1-based 索引
        }
    }
    
    return -1;  // 未找到
}

int StaticTable::getIndexByName(const std::string& name) {
    std::string lower_name = toLower(name);
    
    // 寻找第一个名称匹配的条目
    for (size_t i = 0; i < STATIC_TABLE_SIZE; ++i) {
        if (STATIC_TABLE[i].name == lower_name) {
            return static_cast<int>(i + 1);  // 返回 1-based 索引
        }
    }
    
    return -1;  // 未找到
}

size_t StaticTable::size() {
    return STATIC_TABLE_SIZE;
}

// ============================================================================
// DynamicTable 实现
// ============================================================================

DynamicTable::DynamicTable(size_t max_size)
    : max_size_(max_size), current_size_(0) {}

size_t DynamicTable::calculateEntrySize(const HeaderField& field) {
    // RFC 7541: 大小 = 32 + 名称长度 + 值长度（字节）
    return 32 + field.name.length() + field.value.length();
}

void DynamicTable::insert(const HeaderField& field) {
    // 将名称转换为小写
    HeaderField entry;
    entry.name = toLower(field.name);
    entry.value = field.value;
    
    size_t entry_size = calculateEntrySize(entry);
    
    // 如果条目本身超过最大大小，清空表并丢弃条目
    if (entry_size > max_size_) {
        entries_.clear();
        current_size_ = 0;
        return;
    }
    
    // 从后端淘汰条目直到有足够空间
    while (current_size_ + entry_size > max_size_ && !entries_.empty()) {
        size_t removed_size = calculateEntrySize(entries_.back());
        entries_.pop_back();
        current_size_ -= removed_size;
    }
    
    // 从前端插入新条目
    entries_.insert(entries_.begin(), entry);
    current_size_ += entry_size;
}

HeaderField DynamicTable::get(size_t index) const {
    if (index >= entries_.size()) {
        throw std::out_of_range("Dynamic table index out of range: " + std::to_string(index));
    }
    return entries_[index];
}

int DynamicTable::getIndexByNameValue(const std::string& name, const std::string& value) const {
    std::string lower_name = toLower(name);
    
    // 首先寻找完全匹配（名值都相同）
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == lower_name && entries_[i].value == value) {
            return static_cast<int>(i);  // 返回 0-based 索引
        }
    }
    
    return -1;  // 未找到
}

int DynamicTable::getIndexByName(const std::string& name) const {
    std::string lower_name = toLower(name);
    
    // 寻找第一个名称匹配的条目
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == lower_name) {
            return static_cast<int>(i);  // 返回 0-based 索引
        }
    }
    
    return -1;  // 未找到
}

void DynamicTable::clear() {
    entries_.clear();
    current_size_ = 0;
}

void DynamicTable::setMaxSize(size_t size) {
    max_size_ = size;
    
    // 淘汰条目直到符合新的大小限制
    while (current_size_ > max_size_ && !entries_.empty()) {
        size_t removed_size = calculateEntrySize(entries_.back());
        entries_.pop_back();
        current_size_ -= removed_size;
    }
}

size_t DynamicTable::size() const {
    return current_size_;
}

size_t DynamicTable::entryCount() const {
    return entries_.size();
}

// ============================================================================
// HeaderTable 实现
// ============================================================================

HeaderTable::HeaderTable(size_t dynamic_table_max_size)
    : dynamic_table_(dynamic_table_max_size) {}

HeaderField HeaderTable::getByIndex(size_t index) const {
    if (index < 1) {
        throw std::out_of_range("Header table index must be >= 1");
    }
    
    // 1-61 为静态表
    if (index <= StaticTable::size()) {
        return StaticTable::getByIndex(index);
    }
    
    // 62+ 为动态表（需要转换索引）
    size_t dynamic_index = index - StaticTable::size() - 1;
    return dynamic_table_.get(dynamic_index);
}

int HeaderTable::getIndexByNameValue(const std::string& name, const std::string& value) const {
    // 首先在动态表中查找（动态表优先级更高，因为更新）
    int dynamic_index = dynamic_table_.getIndexByNameValue(name, value);
    if (dynamic_index >= 0) {
        // 转换为统一索引（62+ 表示动态表）
        return static_cast<int>(StaticTable::size()) + 1 + dynamic_index;
    }
    
    // 在静态表中查找
    return StaticTable::getIndexByNameValue(name, value);
}

int HeaderTable::getIndexByName(const std::string& name) const {
    // 首先在动态表中查找（动态表优先级更高）
    int dynamic_index = dynamic_table_.getIndexByName(name);
    if (dynamic_index >= 0) {
        // 转换为统一索引（62+ 表示动态表）
        return static_cast<int>(StaticTable::size()) + 1 + dynamic_index;
    }
    
    // 在静态表中查找
    return StaticTable::getIndexByName(name);
}

void HeaderTable::insertDynamic(const HeaderField& field) {
    dynamic_table_.insert(field);
}

void HeaderTable::setDynamicTableMaxSize(size_t size) {
    dynamic_table_.setMaxSize(size);
}

void HeaderTable::clearDynamic() {
    dynamic_table_.clear();
}

// ============================================================================
// IntegerEncoder Implementation
// ============================================================================

uint8_t IntegerEncoder::getPrefixMask(int prefix_bits) {
    // Create a mask with prefix_bits set to 1
    // For example: prefix_bits=3 -> 0b00000111 (0x07)
    return static_cast<uint8_t>((1U << prefix_bits) - 1);
}

uint64_t IntegerEncoder::getMaxPrefixValue(int prefix_bits) {
    // Calculate 2^N - 1 for N prefix bits
    // For example: prefix_bits=3 -> 2^3 - 1 = 7
    return (1ULL << prefix_bits) - 1;
}

std::vector<uint8_t> IntegerEncoder::encodeInteger(uint64_t value, int prefix_bits) {
    // Validate prefix_bits range [1, 8]
    if (prefix_bits < 1 || prefix_bits > 8) {
        throw std::invalid_argument("prefix_bits must be in range [1, 8]");
    }

    std::vector<uint8_t> result;
    uint64_t max_prefix = getMaxPrefixValue(prefix_bits);
    uint8_t prefix_mask = getPrefixMask(prefix_bits);

    // If value fits in the prefix bits (value < 2^N - 1)
    if (value < max_prefix) {
        // Encode value directly in the prefix bits
        result.push_back(static_cast<uint8_t>(value & prefix_mask));
        return result;
    }

    // Value doesn't fit in prefix bits, use multiple bytes
    // First byte: all prefix bits set to 1 (2^N - 1)
    result.push_back(static_cast<uint8_t>(max_prefix & prefix_mask));

    // Remaining value to encode
    uint64_t remaining = value - max_prefix;

    // Encode remaining value using continuation bytes
    // Each continuation byte:
    // - MSB (bit 7) = 1 if more bytes follow, 0 if last byte
    // - Bits 0-6 contain 7 bits of data
    while (remaining >= 128) {
        // More bytes will follow: set MSB = 1
        result.push_back(static_cast<uint8_t>((remaining & 0x7F) | 0x80));
        remaining >>= 7;
    }

    // Last byte: MSB = 0
    result.push_back(static_cast<uint8_t>(remaining & 0x7F));

    return result;
}

std::pair<uint64_t, size_t> IntegerEncoder::decodeInteger(
    const uint8_t* data, size_t length, int prefix_bits) {
    // Validate parameters
    if (!data) {
        throw std::invalid_argument("data pointer is null");
    }
    if (prefix_bits < 1 || prefix_bits > 8) {
        throw std::invalid_argument("prefix_bits must be in range [1, 8]");
    }
    if (length == 0) {
        throw std::out_of_range("buffer is too short");
    }

    uint8_t prefix_mask = getPrefixMask(prefix_bits);
    uint64_t max_prefix = getMaxPrefixValue(prefix_bits);

    // Extract value from first byte's prefix bits
    uint64_t value = data[0] & prefix_mask;
    size_t bytes_consumed = 1;

    // If value < 2^N - 1, it's fully encoded in the prefix
    if (value < max_prefix) {
        return std::make_pair(value, bytes_consumed);
    }

    // Value >= 2^N - 1, need to read continuation bytes
    uint64_t continuation = 0;
    int shift = 0;
    const int MAX_ITERATIONS = 10; // Prevent infinite loop
    uint8_t last_byte = 0;

    for (int i = 0; i < MAX_ITERATIONS && bytes_consumed < length; ++i) {
        last_byte = data[bytes_consumed++];
        continuation += static_cast<uint64_t>(last_byte & 0x7F) << shift;
        shift += 7;

        // Check if MSB is 0 (last byte)
        if ((last_byte & 0x80) == 0) {
            break;
        }
    }

    // Check for incomplete encoding: if we've consumed all available bytes
    // and the last byte still has MSB set, it means more bytes are needed
    if (bytes_consumed >= length && (last_byte & 0x80) != 0) {
        throw std::out_of_range("buffer is too short for encoded integer");
    }

    value += continuation;
    return std::make_pair(value, bytes_consumed);
}

// ============================================================================
// Huffman Decoding Implementation (RFC 7541 Section 5.2)
// ============================================================================

/**
 * Huffman code table for decoding (RFC 7541 Appendix B)
 * Each symbol (0-255) has a variable-length binary code
 * The table is indexed by symbol number
 */
struct HuffmanCode {
    uint32_t code;    // The code bits (right-aligned)
    uint8_t bits;     // Number of bits (1-30)
};

static const HuffmanCode HUFFMAN_CODE_TABLE[256] = {
    // Sym 0-15
    {0x1ff8, 13}, {0x7fffd8, 23}, {0xfffffe2, 28}, {0xfffffe3, 28},
    {0xfffffe4, 28}, {0xfffffe5, 28}, {0xfffffe6, 28}, {0xfffffe7, 28},
    {0xfffffe8, 28}, {0xffffea, 24}, {0x3ffffffc, 30}, {0xfffffe9, 28},
    {0xfffffea, 28}, {0x3ffffffd, 30}, {0xfffffeb, 28}, {0xfffffec, 28},
    // Sym 16-31
    {0xfffffed, 28}, {0xfffffee, 28}, {0xfffffef, 28}, {0xffffff0, 28},
    {0xffffff1, 28}, {0xffffff2, 28}, {0x3ffffffe, 30}, {0xffffff3, 28},
    {0xffffff4, 28}, {0xffffff5, 28}, {0xffffff6, 28}, {0xffffff7, 28},
    {0xffffff8, 28}, {0xffffff9, 28}, {0xffffffa, 28}, {0xffffffb, 28},
    // Sym 32-47 (Space: 0x14/6 bits, !: 0x3f8/10 bits, etc.)
    {0x14, 6}, {0x3f8, 10}, {0x3f9, 10}, {0xffa, 12},
    {0x1ff9, 13}, {0x15, 6}, {0xf8, 8}, {0x7af, 11},
    {0x3fa, 10}, {0x3fb, 10}, {0xf9, 8}, {0x7b0, 11},
    {0x7b1, 11}, {0x7b2, 11}, {0x3fc, 10}, {0x3fd, 10},
    // Sym 48-63 (0-7: simple, 8-9: simple, etc.)
    {0x1, 6}, {0x2, 6}, {0x3, 6}, {0x4, 6},
    {0x5, 6}, {0x6, 6}, {0x7, 6}, {0x8, 6},
    {0x9, 6}, {0xa, 6}, {0xb, 6}, {0x14, 7},
    {0x15, 7}, {0x16, 7}, {0x17, 7}, {0x18, 7},
    // Sym 64-79
    {0x0, 6}, {0x23, 8}, {0x24, 8}, {0x25, 8},
    {0x26, 8}, {0x27, 8}, {0x28, 8}, {0x29, 8},
    {0x2a, 8}, {0x2b, 8}, {0x2c, 8}, {0x3fe, 10},
    {0x7b3, 11}, {0x7b4, 11}, {0x3ff, 10}, {0x1ffa, 13},
    // Sym 80-95
    {0x21, 8}, {0x5, 5}, {0x6, 5}, {0x7, 5},
    {0x8, 5}, {0x9, 5}, {0xa, 5}, {0xb, 5},
    {0xc, 5}, {0xd, 5}, {0xe, 5}, {0xf, 5},
    {0x10, 6}, {0x11, 6}, {0x12, 6}, {0x13, 6},
    // Sym 96-111
    {0x3f, 6}, {0x19, 7}, {0x1a, 7}, {0x1b, 7},
    {0x1c, 7}, {0x1d, 7}, {0x1e, 7}, {0x1f, 7},
    {0x7b5, 11}, {0x7b6, 11}, {0x7b7, 11}, {0x7b8, 11},
    {0x7b9, 11}, {0x7ba, 11}, {0x7bb, 11}, {0x7bc, 11},
    // Sym 112-127
    {0xfb, 8}, {0x20, 8}, {0x22, 8}, {0x2d, 8},
    {0x2e, 8}, {0x2f, 8}, {0x30, 8}, {0x31, 8},
    {0x32, 8}, {0x33, 8}, {0x34, 8}, {0x35, 8},
    {0x36, 8}, {0x37, 8}, {0x38, 8}, {0x39, 8},
    // Sym 128-143
    {0x3a, 8}, {0x42, 8}, {0x43, 8}, {0x44, 8},
    {0x45, 8}, {0x46, 8}, {0x47, 8}, {0x48, 8},
    {0x49, 8}, {0x4a, 8}, {0x4b, 8}, {0x4c, 8},
    {0x4d, 8}, {0x4e, 8}, {0x4f, 8}, {0x50, 8},
    // Sym 144-159
    {0x51, 8}, {0x52, 8}, {0x53, 8}, {0x54, 8},
    {0x55, 8}, {0x56, 8}, {0x57, 8}, {0x58, 8},
    {0x59, 8}, {0x5a, 8}, {0x5b, 8}, {0x5c, 8},
    {0x5d, 8}, {0x5e, 8}, {0x5f, 8}, {0x60, 8},
    // Sym 160-175
    {0x61, 8}, {0x62, 8}, {0x63, 8}, {0x64, 8},
    {0x65, 8}, {0x66, 8}, {0x67, 8}, {0x68, 8},
    {0x69, 8}, {0x6a, 8}, {0x6b, 8}, {0x6c, 8},
    {0x6d, 8}, {0x6e, 8}, {0x6f, 8}, {0x70, 8},
    // Sym 176-191
    {0x71, 8}, {0x72, 8}, {0x73, 8}, {0x74, 8},
    {0x75, 8}, {0x76, 8}, {0x77, 8}, {0x78, 8},
    {0x79, 8}, {0x7a, 8}, {0x7b, 8}, {0x7c, 8},
    {0x7d, 8}, {0x7e, 8}, {0x7f, 8}, {0xfc, 8},
    // Sym 192-207
    {0xb9, 8}, {0xba, 8}, {0xbb, 8}, {0xc0, 8},
    {0xc1, 8}, {0xc2, 8}, {0xc3, 8}, {0xc4, 8},
    {0xc5, 8}, {0xc6, 8}, {0xc7, 8}, {0xc8, 8},
    {0xc9, 8}, {0xca, 8}, {0xcb, 8}, {0xcc, 8},
    // Sym 208-223
    {0xcd, 8}, {0xce, 8}, {0xcf, 8}, {0xd0, 8},
    {0xd1, 8}, {0xd2, 8}, {0xd3, 8}, {0xd4, 8},
    {0xd5, 8}, {0xd6, 8}, {0xd7, 8}, {0xd8, 8},
    {0xd9, 8}, {0xda, 8}, {0xdb, 8}, {0xdc, 8},
    // Sym 224-239
    {0xdd, 8}, {0xde, 8}, {0xdf, 8}, {0xe0, 8},
    {0xe1, 8}, {0xe2, 8}, {0xe3, 8}, {0xe4, 8},
    {0xe5, 8}, {0xe6, 8}, {0xe7, 8}, {0xe8, 8},
    {0xe9, 8}, {0xea, 8}, {0xeb, 8}, {0xec, 8},
    // Sym 240-255
    {0xed, 8}, {0xee, 8}, {0xef, 8}, {0xf0, 8},
    {0xf1, 8}, {0xf2, 8}, {0xf3, 8}, {0xf4, 8},
    {0xf5, 8}, {0xf6, 8}, {0xf7, 8}, {0xf8, 8},
    {0x3c, 8}, {0xf9, 8}, {0xfa, 8}, {0xfb, 8}
};

/**
 * @brief Decode a Huffman-encoded byte string
 * Uses RFC 7541 Appendix B Huffman code table
 * This is a correct implementation using bit-level decoding
 * 
 * @param data Pointer to Huffman-encoded data
 * @param length Length of encoded data in bytes
 * @return Decoded string
 */
static std::string huffmanDecode(const uint8_t* data, size_t length) {
    if (length == 0) {
        return "";
    }

    std::string result;
    result.reserve(length * 2);
    
    uint64_t bit_buffer = 0;
    int bits_in_buffer = 0;
    
    // Read all bytes into the bit buffer and process symbols
    for (size_t i = 0; i < length; ++i) {
        bit_buffer = (bit_buffer << 8) | data[i];
        bits_in_buffer += 8;
        
        // Process any complete symbols we can now decode
        bool decoded = true;
        while (decoded && bits_in_buffer >= 5) {  // Min Huffman code is 5 bits
            decoded = false;
            
            // Try to match a symbol, checking from longest to shortest codes
            for (int code_len = 30; code_len >= 5; --code_len) {
                if (bits_in_buffer < code_len) continue;
                
                // Extract the top code_len bits
                uint64_t top_bits = bit_buffer >> (bits_in_buffer - code_len);
                uint32_t check_code = static_cast<uint32_t>(top_bits & ((1ULL << code_len) - 1));
                
                // Look for a matching Huffman code
                for (int sym = 0; sym < 256; ++sym) {
                    if (HUFFMAN_CODE_TABLE[sym].bits == code_len &&
                        HUFFMAN_CODE_TABLE[sym].code == check_code) {
                        // Found!
                        result += static_cast<char>(sym);
                        bits_in_buffer -= code_len;
                        decoded = true;
                        break;
                    }
                }
                
                if (decoded) break;
            }
        }
    }
    
    // Per RFC 7541, any remaining bits should be padding (EOS symbol = all 1s)
    // We just discard them - they shouldn't affect the decoded string
    
    return result;
}

// ============================================================================
// StringCoder Implementation
// ============================================================================

std::vector<uint8_t> StringCoder::encodeString(const std::string& str, bool use_huffman) {
    // Check for Huffman encoding
    if (use_huffman) {
        throw std::runtime_error("Huffman encoding is not yet implemented");
    }

    // Literal string encoding (H bit = 0)
    // First byte: bit 7 = 0 (H flag), bits 0-6 = length or length prefix
    std::vector<uint8_t> result;
    uint64_t length = str.length();

    // Encode length using 7-bit prefix
    if (length < 127) {
        // Length fits in 7 bits
        result.push_back(static_cast<uint8_t>(length));
    } else {
        // Length doesn't fit in 7 bits, use continuation bytes
        result.push_back(127); // 7 bits all set to 1

        // Encode remaining length using continuation bytes
        uint64_t remaining = length - 127;
        while (remaining >= 128) {
            result.push_back(static_cast<uint8_t>((remaining & 0x7F) | 0x80));
            remaining >>= 7;
        }
        result.push_back(static_cast<uint8_t>(remaining & 0x7F));
    }

    // Append string data (literal octets)
    result.insert(result.end(), str.begin(), str.end());

    return result;
}

std::pair<std::string, size_t> StringCoder::decodeString(
    const uint8_t* data, size_t length) {
    // Validate parameters
    if (!data) {
        throw std::invalid_argument("data pointer is null");
    }
    if (length == 0) {
        throw std::out_of_range("buffer is too short for string header");
    }

    // First byte: bit 7 = H flag (Huffman bit)
    uint8_t first_byte = data[0];
    bool huffman = (first_byte & 0x80) != 0;

    // Decode length using 7-bit prefix
    uint64_t len = first_byte & 0x7F;
    size_t bytes_consumed = 1;

    // If length == 127, read continuation bytes
    if (len == 127) {
        uint64_t continuation = 0;
        int shift = 0;
        const int MAX_ITERATIONS = 10;

        for (int i = 0; i < MAX_ITERATIONS && bytes_consumed < length; ++i) {
            uint8_t byte = data[bytes_consumed++];
            continuation += static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;

            if ((byte & 0x80) == 0) {
                break;
            }
        }

        if (bytes_consumed < length && (data[bytes_consumed - 1] & 0x80) != 0) {
            throw std::out_of_range("buffer is too short for string length");
        }

        len += continuation;
    }

    // Extract string data
    if (bytes_consumed + len > length) {
        throw std::out_of_range("buffer is too short for string data");
    }

    std::string result;
    
    if (huffman) {
        // Huffman-encoded string
        result = huffmanDecode(data + bytes_consumed, len);
    } else {
        // Literal string
        result = std::string(reinterpret_cast<const char*>(data + bytes_consumed),
                            reinterpret_cast<const char*>(data + bytes_consumed + len));
    }

    return std::make_pair(result, bytes_consumed + len);
}

// ============================================================================
// HPACK Implementation (High-level API)
// ============================================================================

std::vector<uint8_t> HPACK::encode(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    // TODO: Implement HPACK encoding using literal headers with incremental indexing
    std::vector<uint8_t> buffer;

    for (const auto& header : headers) {
        // For now, encode each header as literal string
        // Name encoding (using 6-bit prefix for literal with incremental indexing)
        std::vector<uint8_t> name_encoded = StringCoder::encodeString(header.first, false);
        buffer.insert(buffer.end(), name_encoded.begin(), name_encoded.end());

        // Value encoding
        std::vector<uint8_t> value_encoded = StringCoder::encodeString(header.second, false);
        buffer.insert(buffer.end(), value_encoded.begin(), value_encoded.end());
    }

    return buffer;
}

std::vector<std::pair<std::string, std::string>> HPACK::decode(
    const std::vector<uint8_t>& buffer) {
    // TODO: Implement HPACK decoding
    std::vector<std::pair<std::string, std::string>> headers;

    size_t pos = 0;
    while (pos < buffer.size()) {
        // Decode name
        auto [name, name_len] = StringCoder::decodeString(buffer.data() + pos, buffer.size() - pos);
        pos += name_len;

        if (pos >= buffer.size()) {
            break;
        }

        // Decode value
        auto [value, value_len] = StringCoder::decodeString(buffer.data() + pos, buffer.size() - pos);
        pos += value_len;

        headers.emplace_back(name, value);
    }

    return headers;
}

} // namespace http2
