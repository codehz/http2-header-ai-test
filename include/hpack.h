#ifndef HTTP2_HPACK_H
#define HTTP2_HPACK_H

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <stdexcept>

namespace http2 {

/**
 * @struct HeaderField
 * @brief Represents a header field with name and value
 */
struct HeaderField {
    std::string name;
    std::string value;
};

/**
 * @class IntegerEncoder
 * @brief Encodes and decodes integers according to RFC 7541 section 6.1
 * 
 * The integer representation allows for effective compression of integers
 * in the 1-8 bit range. Integers are encoded in two stages:
 * 1. The value modulo 2^N is placed in the N least significant bits
 * 2. If the value is less than 2^N - 1, it is encoded as is
 * 3. Otherwise, the value 2^N - 1 is placed in the N least significant bits,
 *    and the remaining value is encoded in one or more octets
 */
class IntegerEncoder {
public:
    /**
     * @brief Encode an integer value using specified prefix bits
     * 
     * @param value The value to encode (unsigned 64-bit integer)
     * @param prefix_bits The number of prefix bits (1-8)
     * @return Encoded bytes as vector
     * @throws std::invalid_argument if prefix_bits is not in range [1, 8]
     */
    static std::vector<uint8_t> encodeInteger(uint64_t value, int prefix_bits);

    /**
     * @brief Decode an integer from buffer
     * 
     * @param data Pointer to the start of encoded data
     * @param length Total length of available data
     * @param prefix_bits The number of prefix bits (1-8)
     * @return Pair of (decoded_value, bytes_consumed)
     * @throws std::invalid_argument if prefix_bits is not in range [1, 8]
     * @throws std::out_of_range if buffer is too short
     */
    static std::pair<uint64_t, size_t> decodeInteger(
        const uint8_t* data, size_t length, int prefix_bits);

private:
    // Helper to calculate the mask for given prefix bits
    static uint8_t getPrefixMask(int prefix_bits);
    // Helper to calculate 2^N - 1 for given prefix bits
    static uint64_t getMaxPrefixValue(int prefix_bits);
};

/**
 * @class StringCoder
 * @brief Encodes and decodes string values according to RFC 7541 section 6.2
 * 
 * A string is represented as follows:
 * 1. The MSB of the first byte is a Huffman bit (H flag):
 *    - 0: string is encoded as literal octets
 *    - 1: string is encoded using Huffman coding (not implemented here)
 * 2. The remaining 7 bits of the first byte, plus continuation bytes,
 *    encode the length of the string
 */
class StringCoder {
public:
    /**
     * @brief Encode a string with optional Huffman encoding
     * 
     * @param str The string to encode
     * @param use_huffman If true, use Huffman encoding; if false, use literal encoding
     * @return Encoded bytes
     * @note Huffman encoding is not yet implemented; use_huffman=true will throw
     */
    static std::vector<uint8_t> encodeString(const std::string& str, bool use_huffman = false);

    /**
     * @brief Decode a string from buffer
     * 
     * @param data Pointer to the start of encoded data
     * @param length Total length of available data
     * @return Pair of (decoded_string, bytes_consumed)
     * @throws std::out_of_range if buffer is too short
     * @throws std::runtime_error if Huffman decoding is encountered (not implemented)
     */
    static std::pair<std::string, size_t> decodeString(const uint8_t* data, size_t length);

private:
    // Huffman encoding/decoding would be implemented here
};

/**
 * @class StaticTable
 * @brief HTTP/2 HPACK 静态表实现（RFC 7541 附录 B）
 * 
 * 静态表包含 61 个预定义的 HTTP/2 标准头字段。
 * 索引范围：1-61（遵循 RFC 7541 标准）
 */
class StaticTable {
public:
    /**
     * @brief 通过索引获取头字段
     * 
     * @param index 索引值（1-61），基于 RFC 7541 标准
     * @return HeaderField 对应的头字段
     * @throws std::out_of_range 如果索引超出范围
     */
    static HeaderField getByIndex(size_t index);

    /**
     * @brief 通过名值对查询头字段索引
     * 
     * @param name 头字段名称（自动转换为小写）
     * @param value 头字段值
     * @return 索引值（1-61），若不存在返回 -1
     */
    static int getIndexByNameValue(const std::string& name, const std::string& value);

    /**
     * @brief 通过名称查询头字段索引
     * 
     * @param name 头字段名称（自动转换为小写）
     * @return 索引值（1-61），若不存在返回 -1
     */
    static int getIndexByName(const std::string& name);

    /**
     * @brief 获取静态表大小
     * @return 始终返回 61
     */
    static size_t size();
};

/**
 * @class DynamicTable
 * @brief HTTP/2 HPACK 动态表实现
 * 
 * 动态表用于存储动态编码的头字段。新条目添加到表的前端，
 * 当超过最大大小时从后端淘汰旧条目。
 * 
 * 条目大小计算方式（RFC 7541）：
 *   大小 = 32 + 字段名长度 + 字段值长度（单位：字节）
 */
class DynamicTable {
public:
    /**
     * @brief 构造函数
     * 
     * @param max_size 动态表最大大小（字节），默认 4096
     */
    explicit DynamicTable(size_t max_size = 4096);

    /**
     * @brief 向动态表前端插入新头字段
     * 
     * @param field 要插入的头字段（名称自动转换为小写）
     * 
     * 如果插入导致表大小超过最大大小，会自动从表后端淘汰旧条目
     */
    void insert(const HeaderField& field);

    /**
     * @brief 通过索引获取头字段
     * 
     * @param index 索引值（0-based），0 是最新的条目
     * @return HeaderField 对应的头字段
     * @throws std::out_of_range 如果索引超出范围
     */
    HeaderField get(size_t index) const;

    /**
     * @brief 通过名值对查询头字段索引
     * 
     * @param name 头字段名称（自动转换为小写）
     * @param value 头字段值
     * @return 索引值（0-based），若不存在返回 -1
     */
    int getIndexByNameValue(const std::string& name, const std::string& value) const;

    /**
     * @brief 通过名称查询头字段索引
     * 
     * @param name 头字段名称（自动转换为小写）
     * @return 索引值（0-based），若不存在返回 -1
     */
    int getIndexByName(const std::string& name) const;

    /**
     * @brief 清空动态表
     */
    void clear();

    /**
     * @brief 设置动态表最大大小并进行必要的淘汰
     * 
     * @param size 新的最大大小（字节）
     * 
     * 如果新的最大大小小于当前占用大小，会淘汰条目直到符合新限制
     */
    void setMaxSize(size_t size);

    /**
     * @brief 获取动态表实际占用大小
     * @return 字节数
     */
    size_t size() const;

    /**
     * @brief 获取动态表中的条目数
     * @return 条目数
     */
    size_t entryCount() const;

private:
    std::vector<HeaderField> entries_;  // 存储头字段条目
    size_t max_size_;                    // 最大大小（字节）
    size_t current_size_;                // 当前占用大小（字节）

    /**
     * @brief 计算头字段的大小（RFC 7541）
     * @param field 头字段
     * @return 大小 = 32 + 名称长度 + 值长度
     */
    static size_t calculateEntrySize(const HeaderField& field);
};

/**
 * @class HeaderTable
 * @brief 统一的 HPACK 表管理类
 * 
 * 集成了静态表和动态表，提供统一的索引接口。
 * 索引规则：1-61 对应静态表，62+ 对应动态表
 */
class HeaderTable {
public:
    /**
     * @brief 构造函数
     * 
     * @param dynamic_table_max_size 动态表最大大小（字节），默认 4096
     */
    explicit HeaderTable(size_t dynamic_table_max_size = 4096);

    /**
     * @brief 通过统一索引获取头字段
     * 
     * @param index 索引值（1-61 为静态表，62+ 为动态表）
     * @return HeaderField 对应的头字段
     * @throws std::out_of_range 如果索引超出范围
     */
    HeaderField getByIndex(size_t index) const;

    /**
     * @brief 通过名值对查询头字段索引（同时搜索静态表和动态表）
     * 
     * @param name 头字段名称（自动转换为小写）
     * @param value 头字段值
     * @return 索引值（1-61 为静态表，62+ 为动态表），若不存在返回 -1
     */
    int getIndexByNameValue(const std::string& name, const std::string& value) const;

    /**
     * @brief 通过名称查询头字段索引（同时搜索静态表和动态表）
     * 
     * @param name 头字段名称（自动转换为小写）
     * @return 索引值（1-61 为静态表，62+ 为动态表），若不存在返回 -1
     */
    int getIndexByName(const std::string& name) const;

    /**
     * @brief 向动态表添加新头字段
     * 
     * @param field 要添加的头字段（名称自动转换为小写）
     */
    void insertDynamic(const HeaderField& field);

    /**
     * @brief 设置动态表的最大大小
     * 
     * @param size 新的最大大小（字节）
     */
    void setDynamicTableMaxSize(size_t size);

    /**
     * @brief 清空动态表
     */
    void clearDynamic();

private:
    DynamicTable dynamic_table_;  // 动态表
};

/**
 * @class HPACK
 * @brief HPACK 编码和解码用于 HTTP/2 头压缩
 * 
 * HPACK（HTTP/2 的头部压缩）用于压缩通过连接传输的 HTTP 头字段。
 */
class HPACK {
public:
    /**
     * @brief 使用 HPACK 编码头字段
     * @param headers 头字段名-值对向量
     * @return 编码后的字节缓冲区
     */
    static std::vector<uint8_t> encode(const std::vector<std::pair<std::string, std::string>>& headers);

    /**
     * @brief 解码 HPACK 编码的缓冲区
     * @param buffer 编码后的字节缓冲区
     * @return 解码后的头字段名-值对
     */
    static std::vector<std::pair<std::string, std::string>> decode(const std::vector<uint8_t>& buffer);

private:
    // 私有实现细节将在此添加
};

} // namespace http2

#endif // HTTP2_HPACK_H
