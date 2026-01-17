#include "hpack.h"
#include <algorithm>
#include <limits>
#include <cctype>
#include <deque>
#include <map>
#include <iostream>

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

// Huffman codes from RFC 7541 Appendix B (extracted from nghttp2)
static const HuffmanCode HUFFMAN_CODE_TABLE[256] = {
    {0x1ff8, 13}, {0x7fffd8, 23}, {0xfffffe2, 28}, {0xfffffe3, 28},  // Sym 0-3
    {0xfffffe4, 28}, {0xfffffe5, 28}, {0xfffffe6, 28}, {0xfffffe7, 28},  // Sym 4-7
    {0xfffffe8, 28}, {0xffffea, 24}, {0x3ffffffc, 30}, {0xfffffe9, 28},  // Sym 8-11
    {0xfffffea, 28}, {0x3ffffffd, 30}, {0xfffffeb, 28}, {0xfffffec, 28},  // Sym 12-15
    {0xfffffed, 28}, {0xfffffee, 28}, {0xfffffef, 28}, {0xffffff0, 28},  // Sym 16-19
    {0xffffff1, 28}, {0xffffff2, 28}, {0x3ffffffe, 30}, {0xffffff3, 28},  // Sym 20-23
    {0xffffff4, 28}, {0xffffff5, 28}, {0xffffff6, 28}, {0xffffff7, 28},  // Sym 24-27
    {0xffffff8, 28}, {0xffffff9, 28}, {0xffffffa, 28}, {0xffffffb, 28},  // Sym 28-31
    {0x14, 6}, {0x3f8, 10}, {0x3f9, 10}, {0xffa, 12},  // Sym 32-35
    {0x1ff9, 13}, {0x15, 6}, {0xf8, 8}, {0x7fa, 11},  // Sym 36-39
    {0x3fa, 10}, {0x3fb, 10}, {0xf9, 8}, {0x7fb, 11},  // Sym 40-43
    {0xfa, 8}, {0x16, 6}, {0x17, 6}, {0x18, 6},  // Sym 44-47
    {0x0, 5}, {0x1, 5}, {0x2, 5}, {0x19, 6},  // Sym 48-51
    {0x1a, 6}, {0x1b, 6}, {0x1c, 6}, {0x1d, 6},  // Sym 52-55
    {0x1e, 6}, {0x1f, 6}, {0x5c, 7}, {0xfb, 8},  // Sym 56-59
    {0x7ffc, 15}, {0x20, 6}, {0xffb, 12}, {0x3fc, 10},  // Sym 60-63
    {0x1ffa, 13}, {0x21, 6}, {0x5d, 7}, {0x5e, 7},  // Sym 64-67
    {0x5f, 7}, {0x60, 7}, {0x61, 7}, {0x62, 7},  // Sym 68-71
    {0x63, 7}, {0x64, 7}, {0x65, 7}, {0x66, 7},  // Sym 72-75
    {0x67, 7}, {0x68, 7}, {0x69, 7}, {0x6a, 7},  // Sym 76-79
    {0x6b, 7}, {0x6c, 7}, {0x6d, 7}, {0x6e, 7},  // Sym 80-83
    {0x6f, 7}, {0x70, 7}, {0x71, 7}, {0x72, 7},  // Sym 84-87
    {0xfc, 8}, {0x73, 7}, {0xfd, 8}, {0x1ffb, 13},  // Sym 88-91
    {0x7fff0, 19}, {0x1ffc, 13}, {0x3ffc, 14}, {0x22, 6},  // Sym 92-95
    {0x7ffd, 15}, {0x3, 5}, {0x23, 6}, {0x4, 5},  // Sym 96-99
    {0x24, 6}, {0x5, 5}, {0x25, 6}, {0x26, 6},  // Sym 100-103
    {0x27, 6}, {0x6, 5}, {0x74, 7}, {0x75, 7},  // Sym 104-107
    {0x28, 6}, {0x29, 6}, {0x2a, 6}, {0x7, 5},  // Sym 108-111
    {0x2b, 6}, {0x76, 7}, {0x2c, 6}, {0x8, 5},  // Sym 112-115
    {0x9, 5}, {0x2d, 6}, {0x77, 7}, {0x78, 7},  // Sym 116-119
    {0x79, 7}, {0x7a, 7}, {0x7b, 7}, {0x7ffe, 15},  // Sym 120-123
    {0x7fc, 11}, {0x3ffd, 14}, {0x1ffd, 13}, {0xffffffc, 28},  // Sym 124-127
    {0xfffe6, 20}, {0x3fffd2, 22}, {0xfffe7, 20}, {0xfffe8, 20},  // Sym 128-131
    {0x3fffd3, 22}, {0x3fffd4, 22}, {0x3fffd5, 22}, {0x7fffd9, 23},  // Sym 132-135
    {0x3fffd6, 22}, {0x7fffda, 23}, {0x7fffdb, 23}, {0x7fffdc, 23},  // Sym 136-139
    {0x7fffdd, 23}, {0x7fffde, 23}, {0xffffeb, 24}, {0x7fffdf, 23},  // Sym 140-143
    {0xffffec, 24}, {0xffffed, 24}, {0x3fffd7, 22}, {0x7fffe0, 23},  // Sym 144-147
    {0xffffee, 24}, {0x7fffe1, 23}, {0x7fffe2, 23}, {0x7fffe3, 23},  // Sym 148-151
    {0x7fffe4, 23}, {0x1fffdc, 21}, {0x3fffd8, 22}, {0x7fffe5, 23},  // Sym 152-155
    {0x3fffd9, 22}, {0x7fffe6, 23}, {0x7fffe7, 23}, {0xffffef, 24},  // Sym 156-159
    {0x3fffda, 22}, {0x1fffdd, 21}, {0xfffe9, 20}, {0x3fffdb, 22},  // Sym 160-163
    {0x3fffdc, 22}, {0x7fffe8, 23}, {0x7fffe9, 23}, {0x1fffde, 21},  // Sym 164-167
    {0x7fffea, 23}, {0x3fffdd, 22}, {0x3fffde, 22}, {0xfffff0, 24},  // Sym 168-171
    {0x1fffdf, 21}, {0x3fffdf, 22}, {0x7fffeb, 23}, {0x7fffec, 23},  // Sym 172-175
    {0x1fffe0, 21}, {0x1fffe1, 21}, {0x3fffe0, 22}, {0x1fffe2, 21},  // Sym 176-179
    {0x7fffed, 23}, {0x3fffe1, 22}, {0x7fffee, 23}, {0x7fffef, 23},  // Sym 180-183
    {0xfffea, 20}, {0x3fffe2, 22}, {0x3fffe3, 22}, {0x3fffe4, 22},  // Sym 184-187
    {0x7ffff0, 23}, {0x3fffe5, 22}, {0x3fffe6, 22}, {0x7ffff1, 23},  // Sym 188-191
    {0x3ffffe0, 26}, {0x3ffffe1, 26}, {0xfffeb, 20}, {0x7fff1, 19},  // Sym 192-195
    {0x3fffe7, 22}, {0x7ffff2, 23}, {0x3fffe8, 22}, {0x1ffffec, 25},  // Sym 196-199
    {0x3ffffe2, 26}, {0x3ffffe3, 26}, {0x3ffffe4, 26}, {0x7ffffde, 27},  // Sym 200-203
    {0x7ffffdf, 27}, {0x3ffffe5, 26}, {0xfffff1, 24}, {0x1ffffed, 25},  // Sym 204-207
    {0x7fff2, 19}, {0x1fffe3, 21}, {0x3ffffe6, 26}, {0x7ffffe0, 27},  // Sym 208-211
    {0x7ffffe1, 27}, {0x3ffffe7, 26}, {0x7ffffe2, 27}, {0xfffff2, 24},  // Sym 212-215
    {0x1fffe4, 21}, {0x1fffe5, 21}, {0x3ffffe8, 26}, {0x3ffffe9, 26},  // Sym 216-219
    {0xffffffd, 28}, {0x7ffffe3, 27}, {0x7ffffe4, 27}, {0x7ffffe5, 27},  // Sym 220-223
    {0xfffec, 20}, {0xfffff3, 24}, {0xfffed, 20}, {0x1fffe6, 21},  // Sym 224-227
    {0x3fffe9, 22}, {0x1fffe7, 21}, {0x1fffe8, 21}, {0x7ffff3, 23},  // Sym 228-231
    {0x3fffea, 22}, {0x3fffeb, 22}, {0x1ffffee, 25}, {0x1ffffef, 25},  // Sym 232-235
    {0xfffff4, 24}, {0xfffff5, 24}, {0x3ffffea, 26}, {0x7ffff4, 23},  // Sym 236-239
    {0x3ffffeb, 26}, {0x7ffffe6, 27}, {0x3ffffec, 26}, {0x3ffffed, 26},  // Sym 240-243
    {0x7ffffe7, 27}, {0x7ffffe8, 27}, {0x7ffffe9, 27}, {0x7ffffea, 27},  // Sym 244-247
    {0x7ffffeb, 27}, {0xffffffe, 28}, {0x7ffffec, 27}, {0x7ffffed, 27},  // Sym 248-251
    {0x7ffffee, 27}, {0x7ffffef, 27}, {0x7fffff0, 27}, {0x3ffffee, 26}   // Sym 252-255
};

/**
 * @brief Decode a Huffman-encoded byte string using RFC 7541 Appendix B
 * Uses a linear search decoder optimized for correctness
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
    size_t byte_idx = 0;

    // Main decode loop - continue while we have data or enough bits to decode
    while (byte_idx < length || bits_in_buffer >= 5) {
        // Load more bytes into buffer if needed (up to 30 bits for longest code)
        while (bits_in_buffer < 30 && byte_idx < length) {
            bit_buffer = (bit_buffer << 8) | data[byte_idx++];
            bits_in_buffer += 8;
        }

        // Try to decode a symbol from the buffer
        bool found = false;

        // Try all possible symbols
        for (int sym = 0; sym < 256; ++sym) {
            uint8_t code_len = HUFFMAN_CODE_TABLE[sym].bits;
            uint32_t code = HUFFMAN_CODE_TABLE[sym].code;

            // Check if we have enough bits
            if (bits_in_buffer < code_len) {
                continue;
            }

            // Extract the top code_len bits from buffer
            uint64_t test_code = bit_buffer >> (bits_in_buffer - code_len);
            test_code &= ((1ULL << code_len) - 1);

            // Check if it matches
            if (test_code == code) {
                result += static_cast<char>(sym);
                bits_in_buffer -= code_len;
                // Keep only the remaining bits
                bit_buffer &= ((1ULL << bits_in_buffer) - 1);
                found = true;
                break;
            }
        }

        if (!found) {
            // No valid code found - remaining bits should be padding (all 1s)
            // According to RFC 7541, padding consists of the most significant
            // bits of the EOS symbol (which is all 1s)
            break;
        }
    }

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
        uint8_t last_byte = 0;

        for (int i = 0; i < MAX_ITERATIONS && bytes_consumed < length; ++i) {
            last_byte = data[bytes_consumed++];
            continuation += static_cast<uint64_t>(last_byte & 0x7F) << shift;
            shift += 7;

            if ((last_byte & 0x80) == 0) {
                break;
            }
        }

        // Check for incomplete encoding
        if (bytes_consumed <= length && (last_byte & 0x80) != 0) {
            throw std::out_of_range("buffer is too short for string length");
        }

        len += continuation;
    }

    // Extract string data
    if (bytes_consumed + len > length) {
        std::cerr << "String data overflow: bytes_consumed=" << bytes_consumed 
                  << " string_length=" << len << " buffer_length=" << length << std::endl;
        throw std::out_of_range("buffer is too short for string data");
    }

    std::string result;
    
    if (huffman) {
        // Huffman-encoded string
        try {
            result = huffmanDecode(data + bytes_consumed, len);
        } catch (const std::exception& e) {
            std::cerr << "Huffman decoding failed: " << e.what() << std::endl;
            throw;
        }
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

// ============================================================================
// HPACK Implementation (High-level API)
// ============================================================================

// Static instance of HeaderTable for stateful decoding/encoding
static thread_local HeaderTable g_header_table(4096);

std::vector<uint8_t> HPACK::encode(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    // Implement HPACK encoding using literal headers without indexing
    // This uses the format: 0000xxxx (literal without indexing, new name)
    std::vector<uint8_t> buffer;

    for (const auto& header : headers) {
        // Literal without indexing: 0000 0000 (no index)
        buffer.push_back(0x00);
        
        // Encode header name as string
        std::vector<uint8_t> name_encoded = StringCoder::encodeString(header.first, false);
        buffer.insert(buffer.end(), name_encoded.begin(), name_encoded.end());

        // Encode header value as string
        std::vector<uint8_t> value_encoded = StringCoder::encodeString(header.second, false);
        buffer.insert(buffer.end(), value_encoded.begin(), value_encoded.end());
    }

    return buffer;
}

std::vector<std::pair<std::string, std::string>> HPACK::decode(
    const std::vector<uint8_t>& buffer) {
    std::vector<std::pair<std::string, std::string>> headers;

    if (buffer.empty()) {
        return headers;
    }

    size_t pos = 0;
    
    while (pos < buffer.size()) {
        try {
            if (pos >= buffer.size()) break;
            
            uint8_t first_byte = buffer[pos];
            
            // Determine the encoding type based on the bit pattern
            if ((first_byte & 0x80) != 0) {
                // Indexed Header Field Representation (1xxxxxxx)
                if (pos + 1 > buffer.size()) break;
                
                auto [index, bytes_consumed] = IntegerEncoder::decodeInteger(
                    buffer.data() + pos, buffer.size() - pos, 7);
                pos += bytes_consumed;
                
                if (index == 0) {
                    std::cerr << "Invalid index 0 for indexed header field" << std::endl;
                    break;
                }
                
                try {
                    HeaderField field = g_header_table.getByIndex(index);
                    headers.emplace_back(field.name, field.value);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to retrieve header at index " << index << std::endl;
                }
                
            } else if ((first_byte & 0xC0) == 0x40) {
                // Literal Header Field with Incremental Indexing (01xxxxxx)
                if (pos + 1 > buffer.size()) break;
                
                auto [index, bytes_consumed] = IntegerEncoder::decodeInteger(
                    buffer.data() + pos, buffer.size() - pos, 6);
                pos += bytes_consumed;
                
                if (pos >= buffer.size()) break;
                
                std::string name, value;
                
                if (index == 0) {
                    // New name
                    auto [name_decoded, name_len] = StringCoder::decodeString(
                        buffer.data() + pos, buffer.size() - pos);
                    name = name_decoded;
                    pos += name_len;
                } else {
                    // Name from table
                    try {
                        HeaderField field = g_header_table.getByIndex(index);
                        name = field.name;
                    } catch (const std::exception& e) {
                        break;
                    }
                }
                
                if (pos >= buffer.size()) break;
                
                // Decode value
                auto [value_decoded, value_len] = StringCoder::decodeString(
                    buffer.data() + pos, buffer.size() - pos);
                value = value_decoded;
                pos += value_len;
                
                headers.emplace_back(name, value);
                
                // Add to dynamic table
                g_header_table.insertDynamic({name, value});
                
            } else if ((first_byte & 0xF0) == 0x00) {
                // Literal Header Field without Indexing (0000xxxx)
                if (pos + 1 > buffer.size()) break;
                
                auto [index, bytes_consumed] = IntegerEncoder::decodeInteger(
                    buffer.data() + pos, buffer.size() - pos, 4);
                pos += bytes_consumed;
                
                if (pos >= buffer.size()) break;
                
                std::string name, value;
                
                if (index == 0) {
                    // New name
                    auto [name_decoded, name_len] = StringCoder::decodeString(
                        buffer.data() + pos, buffer.size() - pos);
                    name = name_decoded;
                    pos += name_len;
                } else {
                    // Name from table
                    try {
                        HeaderField field = g_header_table.getByIndex(index);
                        name = field.name;
                    } catch (const std::exception& e) {
                        break;
                    }
                }
                
                if (pos >= buffer.size()) break;
                
                // Decode value
                auto [value_decoded, value_len] = StringCoder::decodeString(
                    buffer.data() + pos, buffer.size() - pos);
                value = value_decoded;
                pos += value_len;
                
                headers.emplace_back(name, value);
                
            } else if ((first_byte & 0xF0) == 0x10) {
                // Literal Header Field Never Indexed (0001xxxx)
                if (pos + 1 > buffer.size()) break;
                
                auto [index, bytes_consumed] = IntegerEncoder::decodeInteger(
                    buffer.data() + pos, buffer.size() - pos, 4);
                pos += bytes_consumed;
                
                if (pos >= buffer.size()) break;
                
                std::string name, value;
                
                if (index == 0) {
                    // New name
                    auto [name_decoded, name_len] = StringCoder::decodeString(
                        buffer.data() + pos, buffer.size() - pos);
                    name = name_decoded;
                    pos += name_len;
                } else {
                    // Name from table
                    try {
                        HeaderField field = g_header_table.getByIndex(index);
                        name = field.name;
                    } catch (const std::exception& e) {
                        break;
                    }
                }
                
                if (pos >= buffer.size()) break;
                
                // Decode value
                auto [value_decoded, value_len] = StringCoder::decodeString(
                    buffer.data() + pos, buffer.size() - pos);
                value = value_decoded;
                pos += value_len;
                
                headers.emplace_back(name, value);
                
            } else if ((first_byte & 0xE0) == 0x20) {
                // Dynamic Table Size Update (001xxxxx)
                if (pos + 1 > buffer.size()) break;
                
                auto [size, bytes_consumed] = IntegerEncoder::decodeInteger(
                    buffer.data() + pos, buffer.size() - pos, 5);
                pos += bytes_consumed;
                
                g_header_table.setDynamicTableMaxSize(size);
                
            } else {
                // Unknown encoding, skip this byte
                pos++;
            }
            
        } catch (const std::exception& e) {
            // Silently skip on error to avoid incomplete header blocks
            pos++;
        }
    }

    return headers;
}

} // namespace http2
