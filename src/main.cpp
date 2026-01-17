#include "http2_client.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== HTTP/2 Client Test ===" << std::endl;
    std::cout << "Connecting to example.com..." << std::endl << std::endl;

    try {
        // 创建HTTP/2客户端
        http2::Http2Client client("example.com", 443);

        // 连接到服务器
        if (!client.connect()) {
            std::cerr << "Failed to connect to server" << std::endl;
            return 1;
        }

        std::cout << "Successfully connected to example.com:443" << std::endl << std::endl;

        // 发送GET请求
        std::cout << "Sending GET request to /" << std::endl;
        auto response = client.get("/");

        // 显示响应信息
        std::cout << "\n=== Response Headers ===" << std::endl;
        std::cout << "Status Code: " << response.status_code << std::endl;
        std::cout << "\nHeaders:" << std::endl;
        
        for (const auto& [name, value] : response.headers) {
            std::cout << "  " << name << ": " << value << std::endl;
        }

        // 显示响应体大小
        std::cout << "\nResponse Body Size: " << response.body.size() << " bytes" << std::endl;

        // 显示响应体前256字节
        if (!response.body.empty()) {
            std::cout << "\nResponse Body (first 256 bytes):" << std::endl;
            std::cout << "---" << std::endl;
            
            size_t display_size = std::min(size_t(256), response.body.size());
            for (size_t i = 0; i < display_size; ++i) {
                char c = response.body[i];
                if (c >= 32 && c < 127) {
                    std::cout << c;
                } else if (c == '\n') {
                    std::cout << "\n";
                } else if (c == '\r') {
                    // 跳过\r
                } else if (c == '\t') {
                    std::cout << "\t";
                } else {
                    std::cout << ".";
                }
            }
            
            if (response.body.size() > 256) {
                std::cout << "\n... (truncated, " << (response.body.size() - 256) << " more bytes)";
            }
            std::cout << "\n---" << std::endl;
        }

        // 关闭连接
        client.disconnect();
        std::cout << "\nConnection closed" << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
