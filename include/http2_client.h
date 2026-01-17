#ifndef HTTP2_CLIENT_H
#define HTTP2_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <openssl/ssl.h>

namespace http2 {

/**
 * @class Http2Client
 * @brief HTTP/2 客户端实现，支持TLS连接和HTTP/2通信
 * 
 * 使用Linux socket API和OpenSSL库实现安全的HTTP/2连接。
 */
class Http2Client {
public:
    /**
     * @brief HTTP/2响应结构
     */
    struct Response {
        int status_code;
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<uint8_t> body;
    };

    /**
     * @brief 构造函数
     * 
     * @param host 目标主机名或IP地址
     * @param port 目标端口，默认为443（HTTPS）
     */
    Http2Client(const std::string& host, uint16_t port = 443);

    /**
     * @brief 析构函数，自动关闭连接
     */
    ~Http2Client();

    /**
     * @brief 初始化SSL/TLS连接
     * 
     * @return true 如果连接成功，false 如果失败
     */
    bool connect();

    /**
     * @brief 关闭连接
     */
    void disconnect();

    /**
     * @brief 发送HTTP/2 GET请求
     * 
     * @param path 请求路径，例如 "/"
     * @param headers 可选的自定义请求头
     * @return Response 响应对象
     */
    Response get(const std::string& path, 
                 const std::vector<std::pair<std::string, std::string>>& headers = {});

    /**
     * @brief 发送HTTP/2 HEAD请求
     * 
     * @param path 请求路径
     * @param headers 可选的自定义请求头
     * @return Response 响应对象（无body）
     */
    Response head(const std::string& path,
                  const std::vector<std::pair<std::string, std::string>>& headers = {});

    /**
     * @brief 检查是否已连接
     * 
     * @return true 如果已连接，false 如果未连接
     */
    bool isConnected() const;

private:
    std::string host_;
    uint16_t port_;
    int socket_fd_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;
    
    // HTTP/2帧类型
    static constexpr uint8_t FRAME_TYPE_DATA = 0x0;
    static constexpr uint8_t FRAME_TYPE_HEADERS = 0x1;
    static constexpr uint8_t FRAME_TYPE_PRIORITY = 0x2;
    static constexpr uint8_t FRAME_TYPE_RST_STREAM = 0x3;
    static constexpr uint8_t FRAME_TYPE_SETTINGS = 0x4;
    static constexpr uint8_t FRAME_TYPE_PUSH_PROMISE = 0x5;
    static constexpr uint8_t FRAME_TYPE_PING = 0x6;
    static constexpr uint8_t FRAME_TYPE_GOAWAY = 0x7;
    static constexpr uint8_t FRAME_TYPE_WINDOW_UPDATE = 0x8;
    static constexpr uint8_t FRAME_TYPE_CONTINUATION = 0x9;

    // HTTP/2帧头标志
    static constexpr uint8_t FLAG_END_STREAM = 0x1;
    static constexpr uint8_t FLAG_END_HEADERS = 0x4;
    static constexpr uint8_t FLAG_PADDED = 0x8;
    static constexpr uint8_t FLAG_PRIORITY = 0x20;

    /**
     * @brief 建立原始socket连接
     * 
     * @return true 如果成功，false 如果失败
     */
    bool createSocket();

    /**
     * @brief 执行TLS握手
     * 
     * @return true 如果成功，false 如果失败
     */
    bool performTlsHandshake();

    /**
     * @brief 发送HTTP/2连接前言（PRI * HTTP/2.0）
     * 
     * @return true 如果成功，false 如果失败
     */
    bool sendClientPreface();

    /**
     * @brief 发送SETTINGS帧
     * 
     * @return true 如果成功，false 如果失败
     */
    bool sendSettings();

    /**
     * @brief 发送HTTP/2帧
     * 
     * @param type 帧类型
     * @param flags 帧标志
     * @param stream_id 流ID
     * @param payload 帧负载
     * @return true 如果成功，false 如果失败
     */
    bool sendFrame(uint8_t type, uint8_t flags, uint32_t stream_id, 
                   const std::vector<uint8_t>& payload);

    /**
     * @brief 接收HTTP/2帧
     * 
     * @param type 输出参数：帧类型
     * @param flags 输出参数：帧标志
     * @param stream_id 输出参数：流ID
     * @param payload 输出参数：帧负载
     * @return true 如果成功，false 如果失败或连接关闭
     */
    bool recvFrame(uint8_t& type, uint8_t& flags, uint32_t& stream_id,
                   std::vector<uint8_t>& payload);

    /**
     * @brief 发送HEADERS帧（包含编码的请求头）
     * 
     * @param stream_id 流ID
     * @param method HTTP方法（GET、POST等）
     * @param path 请求路径
     * @param headers 额外的请求头
     * @param end_stream 是否为最后一帧
     * @return true 如果成功，false 如果失败
     */
    bool sendHeadersFrame(uint32_t stream_id, const std::string& method,
                          const std::string& path,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          bool end_stream);

    /**
     * @brief 接收响应头和数据
     * 
     * @param stream_id 流ID
     * @return Response 响应对象
     */
    Response receiveResponse(uint32_t stream_id);

    /**
     * @brief 从socket读取数据
     * 
     * @param buffer 缓冲区
     * @param size 读取大小
     * @return 实际读取的字节数，-1表示错误
     */
    int socketRead(uint8_t* buffer, size_t size);

    /**
     * @brief 向socket写入数据
     * 
     * @param buffer 缓冲区
     * @param size 写入大小
     * @return 实际写入的字节数，-1表示错误
     */
    int socketWrite(const uint8_t* buffer, size_t size);

    /**
     * @brief 清理资源
     */
    void cleanup();
};

} // namespace http2

#endif // HTTP2_CLIENT_H
