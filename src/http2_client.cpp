#include "http2_client.h"
#include "hpack.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace http2 {

// HTTP/2连接前言
static const char HTTP2_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

Http2Client::Http2Client(const std::string& host, uint16_t port)
    : host_(host), port_(port), socket_fd_(-1), ssl_ctx_(nullptr), ssl_(nullptr) {
    // 初始化OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

Http2Client::~Http2Client() {
    disconnect();
}

bool Http2Client::createSocket() {
    // 获取主机信息
    struct hostent* host_entry = gethostbyname(host_.c_str());
    if (!host_entry) {
        std::cerr << "Failed to resolve hostname: " << host_ << std::endl;
        return false;
    }

    // 创建socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // 设置socket选项，启用TCP_NODELAY以减少延迟
    int opt = 1;
    if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set TCP_NODELAY" << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // 准备地址结构
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_aton(host_entry->h_addr, nullptr) ? 
        *(uint32_t*)host_entry->h_addr : 0;

    // 如果inet_aton失败，尝试使用h_addr
    if (server_addr.sin_addr.s_addr == 0) {
        server_addr.sin_addr = *(struct in_addr*)host_entry->h_addr_list[0];
    }

    // 连接到服务器
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server: " << host_ << ":" << port_ << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    std::cout << "Socket connection established" << std::endl;
    return true;
}

bool Http2Client::performTlsHandshake() {
    // 创建SSL上下文
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        std::cerr << "Failed to create SSL context" << std::endl;
        return false;
    }

    // 禁用证书验证（仅用于测试）
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);

    // 设置ALPN以支持HTTP/2
    unsigned char alpn_protos[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    if (!SSL_CTX_set_alpn_protos(ssl_ctx_, alpn_protos, sizeof(alpn_protos))) {
        std::cerr << "Failed to set ALPN protocols" << std::endl;
        // 继续，ALPN不是强制的
    }

    // 创建SSL连接
    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
        std::cerr << "Failed to create SSL connection" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // 设置SNI（服务器名称指示）
    SSL_set_tlsext_host_name(ssl_, host_.c_str());

    // 关联socket和SSL
    if (!SSL_set_fd(ssl_, socket_fd_)) {
        std::cerr << "Failed to set SSL socket" << std::endl;
        SSL_free(ssl_);
        ssl_ = nullptr;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // 执行TLS握手
    if (SSL_connect(ssl_) <= 0) {
        std::cerr << "TLS handshake failed" << std::endl;
        SSL_free(ssl_);
        ssl_ = nullptr;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // 检查ALPN协议协商结果
    const unsigned char* alpn_selected = nullptr;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl_, &alpn_selected, &alpn_len);
    if (alpn_selected) {
        std::cout << "ALPN selected: ";
        for (unsigned int i = 0; i < alpn_len; ++i) {
            std::cout << (char)alpn_selected[i];
        }
        std::cout << std::endl;
    }

    std::cout << "TLS handshake successful" << std::endl;
    return true;
}

int Http2Client::socketRead(uint8_t* buffer, size_t size) {
    if (!ssl_) return -1;
    
    int ret = SSL_read(ssl_, buffer, size);
    if (ret < 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            std::cerr << "SSL_read error: " << err << std::endl;
        }
    }
    return ret;
}

int Http2Client::socketWrite(const uint8_t* buffer, size_t size) {
    if (!ssl_) return -1;
    
    int ret = SSL_write(ssl_, buffer, size);
    if (ret < 0) {
        int err = SSL_get_error(ssl_, ret);
        std::cerr << "SSL_write error: " << err << std::endl;
    }
    return ret;
}

bool Http2Client::sendClientPreface() {
    const uint8_t* preface = (const uint8_t*)HTTP2_PREFACE;
    size_t preface_len = 24; // "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    
    if (socketWrite(preface, preface_len) != (int)preface_len) {
        std::cerr << "Failed to send HTTP/2 preface" << std::endl;
        return false;
    }
    
    std::cout << "HTTP/2 preface sent" << std::endl;
    return true;
}

bool Http2Client::sendSettings() {
    // SETTINGS帧：type=4, flags=0, stream_id=0, payload=empty
    std::vector<uint8_t> payload;
    return sendFrame(FRAME_TYPE_SETTINGS, 0, 0, payload);
}

bool Http2Client::sendFrame(uint8_t type, uint8_t flags, uint32_t stream_id,
                            const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame;
    
    // 帧头：3字节长度 + 1字节类型 + 1字节标志 + 4字节流ID
    uint32_t length = payload.size();
    
    // 编码长度（3字节，大端）
    frame.push_back((length >> 16) & 0xFF);
    frame.push_back((length >> 8) & 0xFF);
    frame.push_back(length & 0xFF);
    
    // 类型
    frame.push_back(type);
    
    // 标志
    frame.push_back(flags);
    
    // 流ID（4字节，大端，最高位必须为0）
    frame.push_back((stream_id >> 24) & 0x7F);
    frame.push_back((stream_id >> 16) & 0xFF);
    frame.push_back((stream_id >> 8) & 0xFF);
    frame.push_back(stream_id & 0xFF);
    
    // 添加负载
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    // 发送帧
    if (socketWrite(frame.data(), frame.size()) != (int)frame.size()) {
        std::cerr << "Failed to send frame type: " << (int)type << std::endl;
        return false;
    }
    
    return true;
}

bool Http2Client::recvFrame(uint8_t& type, uint8_t& flags, uint32_t& stream_id,
                            std::vector<uint8_t>& payload) {
    uint8_t header[9];
    
    // 读取帧头
    int bytes_read = socketRead(header, 9);
    if (bytes_read != 9) {
        if (bytes_read < 0) {
            std::cerr << "Socket read error" << std::endl;
        } else {
            std::cerr << "Incomplete frame header. Expected 9 bytes, got " << bytes_read << std::endl;
        }
        return false;
    }
    
    // 解析长度（3字节，大端）
    uint32_t length = ((uint32_t)header[0] << 16) | ((uint32_t)header[1] << 8) | header[2];
    
    // 解析类型
    type = header[3];
    
    // 解析标志
    flags = header[4];
    
    // 解析流ID
    stream_id = (((uint32_t)header[5] & 0x7F) << 24) | 
                ((uint32_t)header[6] << 16) | 
                ((uint32_t)header[7] << 8) | 
                header[8];
    
    // 检查帧长度是否合理
    if (length > 16384) {  // 默认最大帧大小
        std::cerr << "WARNING: Frame length " << length << " exceeds expected size" << std::endl;
    }
    
    // 读取负载
    payload.clear();
    if (length > 0) {
        payload.resize(length);
        int read_bytes = socketRead(payload.data(), length);
        if (read_bytes != (int)length) {
            std::cerr << "Failed to read frame payload. Expected: " << length 
                     << ", Got: " << read_bytes << std::endl;
            // 继续处理，可能是SETTINGS ACK没有负载
            if (read_bytes > 0) {
                payload.resize(read_bytes);
            } else {
                payload.clear();
            }
        }
    }
    
    return true;
}

bool Http2Client::sendHeadersFrame(uint32_t stream_id, const std::string& method,
                                  const std::string& path,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  bool end_stream) {
    // 手动构建HPACK编码的头部
    // 使用静态表的索引和字面值编码
    std::vector<uint8_t> encoded_headers;
    
    std::cout << "Encoding headers for request:" << std::endl;
    
    // :method GET = static table index 2
    std::cout << "  :method: " << method << " (static index 2)" << std::endl;
    encoded_headers.push_back(0x82);  // 10000010 = indexed header field, index 2
    
    // :scheme https = static table index 7
    std::cout << "  :scheme: https (static index 7)" << std::endl;
    encoded_headers.push_back(0x87);  // 10000111 = indexed header field, index 7
    
    // Helper lambda to encode a string according to RFC 7541
    auto encodeString = [](const std::string& str) -> std::vector<uint8_t> {
        std::vector<uint8_t> encoded;
        uint64_t length = str.length();
        
        // H flag = 0 (not Huffman encoded), then encode length with 7-bit prefix
        if (length < 127) {
            encoded.push_back(static_cast<uint8_t>(length));
        } else {
            encoded.push_back(127);  // 7 bits all set to 1
            length -= 127;
            
            while (length >= 128) {
                encoded.push_back(static_cast<uint8_t>((length & 0x7F) | 0x80));
                length >>= 7;
            }
            encoded.push_back(static_cast<uint8_t>(length & 0x7F));
        }
        
        // Append string data
        encoded.insert(encoded.end(), str.begin(), str.end());
        return encoded;
    };
    
    // :authority = static table index 1, literal value
    std::cout << "  :authority: " << host_ << std::endl;
    encoded_headers.push_back(0x41);  // 01000001 = literal with incremental indexing, index 1
    auto authority_encoded = encodeString(host_);
    encoded_headers.insert(encoded_headers.end(), authority_encoded.begin(), authority_encoded.end());
    
    // :path = static table index 4, literal value
    std::cout << "  :path: " << path << std::endl;
    std::string full_path = path.empty() ? "/" : path;
    encoded_headers.push_back(0x44);  // 01000100 = literal with incremental indexing, index 4
    auto path_encoded = encodeString(full_path);
    encoded_headers.insert(encoded_headers.end(), path_encoded.begin(), path_encoded.end());
    
    // Add custom headers if any
    for (const auto& [name, value] : headers) {
        std::cout << "  " << name << ": " << value << std::endl;
        // Use literal without indexing (0000xxxx)
        encoded_headers.push_back(0x00);  // Literal without indexing
        // Encode name as string
        auto name_encoded = encodeString(name);
        encoded_headers.insert(encoded_headers.end(), name_encoded.begin(), name_encoded.end());
        // Encode value as string
        auto value_encoded = encodeString(value);
        encoded_headers.insert(encoded_headers.end(), value_encoded.begin(), value_encoded.end());
    }
    
    std::cout << "Encoded headers size: " << encoded_headers.size() << " bytes" << std::endl;
    
    // 发送HEADERS帧
    uint8_t flags = FLAG_END_HEADERS;
    if (end_stream) {
        flags |= FLAG_END_STREAM;
    }
    
    if (!sendFrame(FRAME_TYPE_HEADERS, flags, stream_id, encoded_headers)) {
        std::cerr << "Failed to send HEADERS frame" << std::endl;
        return false;
    }
    
    std::cout << "HEADERS frame sent successfully" << std::endl;
    return true;
}

Http2Client::Response Http2Client::receiveResponse(uint32_t stream_id) {
    Response response;
    response.status_code = 200;  // 默认200
    
    std::vector<uint8_t> header_block;
    int frames_received = 0;
    const int MAX_FRAMES = 100;  // 防止无限循环
    
    while (frames_received < MAX_FRAMES) {
        frames_received++;
        uint8_t type;
        uint8_t flags;
        uint32_t recv_stream_id;
        std::vector<uint8_t> payload;
        
        if (!recvFrame(type, flags, recv_stream_id, payload)) {
            std::cerr << "Failed to receive frame" << std::endl;
            break;
        }
        
        std::cout << "Received frame type: " << (int)type << " flags: " << (int)flags 
                  << " stream_id: " << recv_stream_id << " payload_size: " << payload.size() << std::endl;
        
        // 处理不同的帧类型
        switch (type) {
            case FRAME_TYPE_SETTINGS: {
                // 发送SETTINGS ACK
                if (!(flags & 0x01)) { // 如果没有设置ACK标志
                    std::cout << "Sending SETTINGS ACK" << std::endl;
                    sendFrame(FRAME_TYPE_SETTINGS, 0x01, 0, {});
                }
                break;
            }
            
            case FRAME_TYPE_PING: {
                // 发送PING ACK
                std::cout << "Received PING, sending PONG" << std::endl;
                std::vector<uint8_t> ping_data(payload.begin(), payload.end());
                sendFrame(FRAME_TYPE_PING, 0x01, 0, ping_data);
                break;
            }
            
            case FRAME_TYPE_HEADERS: {
                if (recv_stream_id == stream_id) {
                    std::cout << "Received HEADERS frame for stream " << stream_id << std::endl;
                    header_block.insert(header_block.end(), payload.begin(), payload.end());
                    if (flags & FLAG_END_HEADERS) {
                        std::cout << "Headers complete, attempting to decode..." << std::endl;
                        // 尝试解码头部
                        try {
                            auto decoded = HPACK::decode(header_block);
                            std::cout << "Successfully decoded " << decoded.size() << " headers:" << std::endl;
                            for (const auto& [name, value] : decoded) {
                                if (name == ":status") {
                                    try {
                                        response.status_code = std::stoi(value);
                                    } catch (...) {
                                        response.status_code = 200;
                                    }
                                    std::cout << "  Status: " << value << std::endl;
                                } else {
                                    response.headers.push_back({name, value});
                                    std::cout << "  " << name << ": " << value << std::endl;
                                }
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "Error decoding headers: " << e.what() << std::endl;
                            // 继续处理，尽管headers无法解码
                            std::cout << "Continuing without decoded headers..." << std::endl;
                        }
                    }
                    if (flags & FLAG_END_STREAM) {
                        std::cout << "Response ended with headers" << std::endl;
                        return response;
                    }
                }
                break;
            }
            
            case FRAME_TYPE_DATA: {
                if (recv_stream_id == stream_id) {
                    std::cout << "Received DATA frame for stream " << stream_id 
                              << " (" << payload.size() << " bytes)" << std::endl;
                    response.body.insert(response.body.end(), payload.begin(), payload.end());
                    if (flags & FLAG_END_STREAM) {
                        std::cout << "Response stream ended" << std::endl;
                        return response;
                    }
                }
                break;
            }
            
            case FRAME_TYPE_GOAWAY: {
                // 解析GOAWAY帧
                uint32_t last_stream_id = 0;
                uint32_t error_code = 0;
                if (payload.size() >= 8) {
                    last_stream_id = (((uint32_t)payload[0] & 0x7F) << 24) | 
                                     ((uint32_t)payload[1] << 16) | 
                                     ((uint32_t)payload[2] << 8) | 
                                     payload[3];
                    error_code = ((uint32_t)payload[4] << 24) | 
                                ((uint32_t)payload[5] << 16) | 
                                ((uint32_t)payload[6] << 8) | 
                                payload[7];
                }
                
                std::string error_name = "UNKNOWN";
                switch (error_code) {
                    case 0: error_name = "NO_ERROR"; break;
                    case 1: error_name = "PROTOCOL_ERROR"; break;
                    case 2: error_name = "INTERNAL_ERROR"; break;
                    case 3: error_name = "FLOW_CONTROL_ERROR"; break;
                    case 4: error_name = "SETTINGS_TIMEOUT"; break;
                    case 5: error_name = "STREAM_CLOSED"; break;
                    case 6: error_name = "FRAME_SIZE_ERROR"; break;
                    case 7: error_name = "REFUSED_STREAM"; break;
                    case 8: error_name = "CANCEL"; break;
                    case 9: error_name = "COMPRESSION_ERROR"; break;
                    case 10: error_name = "CONNECT_ERROR"; break;
                    case 11: error_name = "ENHANCE_YOUR_CALM"; break;
                    case 12: error_name = "INADEQUATE_SECURITY"; break;
                }
                
                std::cerr << "Received GOAWAY frame: error=" << error_code << " (" << error_name 
                          << "), last_stream_id=" << last_stream_id << std::endl;
                return response;
            }
            
            case FRAME_TYPE_WINDOW_UPDATE: {
                // 忽略WINDOW_UPDATE
                break;
            }
            
            default:
                // 忽略其他帧类型
                std::cout << "Received unhandled frame type: " << (int)type << std::endl;
                break;
        }
    }
    
    if (frames_received >= MAX_FRAMES) {
        std::cerr << "Reached maximum frame limit" << std::endl;
    }
    
    return response;
}

bool Http2Client::connect() {
    if (!createSocket()) {
        return false;
    }
    
    if (!performTlsHandshake()) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // 发送HTTP/2客户端前言
    if (!sendClientPreface()) {
        disconnect();
        return false;
    }
    
    // 发送空的SETTINGS帧
    if (!sendSettings()) {
        disconnect();
        return false;
    }
    
    // 接收和处理服务器初始化帧
    std::cout << "Waiting for server initialization..." << std::endl;
    
    int settings_count = 0;
    for (int i = 0; i < 20; ++i) {
        uint8_t type;
        uint8_t flags;
        uint32_t stream_id;
        std::vector<uint8_t> payload;
        
        if (!recvFrame(type, flags, stream_id, payload)) {
            std::cerr << "Timeout reading initialization frames" << std::endl;
            break;
        }
        
        std::cout << "Init frame #" << (i+1) << " - Type: " << (int)type 
                  << " Flags: " << (int)flags << " Stream: " << stream_id 
                  << " Payload size: " << payload.size() << std::endl;
        
        switch (type) {
            case FRAME_TYPE_SETTINGS: {
                settings_count++;
                if (!(flags & 0x01)) {  // ACK flag
                    // 如果不是ACK，则发送ACK
                    std::cout << "Sending SETTINGS ACK" << std::endl;
                    sendFrame(FRAME_TYPE_SETTINGS, 0x01, 0, {});
                }
                if (settings_count >= 1) {
                    std::cout << "Server SETTINGS received, ready for requests" << std::endl;
                    return true;
                }
                break;
            }
            
            case FRAME_TYPE_WINDOW_UPDATE: {
                // 忽略WINDOW_UPDATE
                std::cout << "Ignoring WINDOW_UPDATE" << std::endl;
                break;
            }
            
            case FRAME_TYPE_GOAWAY: {
                std::cerr << "Received GOAWAY during initialization" << std::endl;
                return false;
            }
            
            default:
                std::cout << "Unexpected frame type: " << (int)type << " during init" << std::endl;
        }
    }
    
    return true;
}

void Http2Client::disconnect() {
    cleanup();
}

bool Http2Client::isConnected() const {
    return socket_fd_ >= 0 && ssl_ != nullptr;
}

Http2Client::Response Http2Client::get(
    const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& headers) {
    
    Response response;
    
    if (!isConnected()) {
        std::cerr << "Not connected" << std::endl;
        return response;
    }
    
    // 使用流ID 1
    uint32_t stream_id = 1;
    
    if (!sendHeadersFrame(stream_id, "GET", path, headers, true)) {
        std::cerr << "Failed to send GET request" << std::endl;
        return response;
    }
    
    return receiveResponse(stream_id);
}

Http2Client::Response Http2Client::head(
    const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& headers) {
    
    Response response;
    
    if (!isConnected()) {
        std::cerr << "Not connected" << std::endl;
        return response;
    }
    
    // 使用流ID 1
    uint32_t stream_id = 1;
    
    if (!sendHeadersFrame(stream_id, "HEAD", path, headers, true)) {
        std::cerr << "Failed to send HEAD request" << std::endl;
        return response;
    }
    
    return receiveResponse(stream_id);
}

void Http2Client::cleanup() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

} // namespace http2
