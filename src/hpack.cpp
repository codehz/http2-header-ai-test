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

    if (huffman) {
        throw std::runtime_error("Huffman-encoded strings are not yet supported");
    }

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

    std::string result(reinterpret_cast<const char*>(data + bytes_consumed),
                      reinterpret_cast<const char*>(data + bytes_consumed + len));

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
