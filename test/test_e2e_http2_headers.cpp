#include <gtest/gtest.h>
#include "hpack.h"
#include "header_parser.h"
#include <memory>

namespace http2 {

/**
 * @class E2EHeadersTest
 * @brief 端到端测试：HTTP/2 头处理完整流程
 * 
 * 这些测试覆盖从编码到解码的完整 HTTP/2 头处理流程，
 * 包括真实的 HTTP/2 请求和响应场景
 */
class E2EHeadersTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/**
 * 场景 1: 简单 GET 请求
 * 一个最小化的 HTTP/2 GET 请求头
 */
TEST_F(E2EHeadersTest, SimpleGETRequest) {
    std::vector<std::pair<std::string, std::string>> request_headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"},
    };

    // 编码
    std::vector<uint8_t> encoded = HPACK::encode(request_headers);
    EXPECT_FALSE(encoded.empty());

    // 解码
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);
    EXPECT_EQ(decoded.size(), request_headers.size());

    // 验证所有头
    for (size_t i = 0; i < request_headers.size(); ++i) {
        EXPECT_EQ(decoded[i].first, request_headers[i].first);
        EXPECT_EQ(decoded[i].second, request_headers[i].second);
    }
}

/**
 * 场景 2: 复杂 POST 请求 with 多个自定义头
 */
TEST_F(E2EHeadersTest, ComplexPOSTRequest) {
    std::vector<std::pair<std::string, std::string>> request_headers = {
        {":method", "POST"},
        {":path", "/api/v1/users"},
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"content-type", "application/json; charset=utf-8"},
        {"content-length", "256"},
        {"user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
        {"accept", "application/json, text/plain, */*"},
        {"accept-encoding", "gzip, deflate, br"},
        {"accept-language", "en-US,en;q=0.9"},
        {"authorization", "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"},
        {"x-request-id", "550e8400-e29b-41d4-a716-446655440000"},
        {"x-api-version", "2.0"},
    };

    // 编码
    std::vector<uint8_t> encoded = HPACK::encode(request_headers);
    EXPECT_FALSE(encoded.empty());

    // 解码
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);
    EXPECT_EQ(decoded.size(), request_headers.size());

    // 验证关键头
    bool found_auth = false;
    bool found_request_id = false;
    for (const auto& header : decoded) {
        if (header.first == "authorization") {
            found_auth = true;
            EXPECT_EQ(header.second, "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9");
        }
        if (header.first == "x-request-id") {
            found_request_id = true;
            EXPECT_EQ(header.second, "550e8400-e29b-41d4-a716-446655440000");
        }
    }
    EXPECT_TRUE(found_auth);
    EXPECT_TRUE(found_request_id);
}

/**
 * 场景 3: HTTP 响应头编码和解码
 */
TEST_F(E2EHeadersTest, HTTPResponseHeaders) {
    std::vector<std::pair<std::string, std::string>> response_headers = {
        {":status", "200"},
        {"content-type", "application/json"},
        {"content-length", "1024"},
        {"cache-control", "public, max-age=3600"},
        {"etag", "\"abc123xyz789\""},
        {"server", "nginx/1.18.0"},
        {"x-powered-by", "Node.js/16.13.0"},
        {"access-control-allow-origin", "*"},
    };

    // 编码和解码
    std::vector<uint8_t> encoded = HPACK::encode(response_headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), response_headers.size());

    // 检查状态码
    EXPECT_EQ(decoded[0].first, ":status");
    EXPECT_EQ(decoded[0].second, "200");
}

/**
 * 场景 4: 重复请求（测试动态表中的缓存）
 * 模拟同一个客户端发送多个相似的请求
 */
TEST_F(E2EHeadersTest, RepeatedRequestsWithDynamicTable) {
    // 设置头表管理器
    HeaderTable table;

    // 第一次请求
    std::vector<std::pair<std::string, std::string>> request1 = {
        {":method", "GET"},
        {":path", "/api/users"},
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"user-agent", "MyApp/1.0"},
        {"x-session-id", "sess-12345"},
    };

    // 添加到动态表
    for (const auto& header : request1) {
        table.insertDynamic({header.first, header.second});
    }

    // 第二次请求（大部分头相同）
    std::vector<std::pair<std::string, std::string>> request2 = {
        {":method", "GET"},
        {":path", "/api/posts"},  // 不同的路径
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"user-agent", "MyApp/1.0"},  // 相同
        {"x-session-id", "sess-12345"},  // 相同
    };

    // 验证重复的头可以在动态表中找到
    EXPECT_NE(table.getIndexByNameValue("user-agent", "MyApp/1.0"), -1);
    EXPECT_NE(table.getIndexByNameValue("x-session-id", "sess-12345"), -1);
    EXPECT_NE(table.getIndexByNameValue(":scheme", "https"), -1);
}

/**
 * 场景 5: 大的 Header 值处理
 * 测试包含大值的头（例如 Cookie、Authorization）
 */
TEST_F(E2EHeadersTest, LargeHeaderValues) {
    // 创建一个大的 Cookie 值（模拟真实场景）
    std::string large_cookie = "sessionid=abcdef123456; ";
    large_cookie += std::string(500, 'a');  // 添加 500 个字符
    large_cookie += "; path=/; HttpOnly; Secure";

    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"cookie", large_cookie},
    };

    // 编码和解码
    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    // 验证 Cookie 头完整性
    EXPECT_EQ(decoded.size(), headers.size());
    bool found_cookie = false;
    for (const auto& header : decoded) {
        if (header.first == "cookie") {
            found_cookie = true;
            EXPECT_EQ(header.second, large_cookie);
        }
    }
    EXPECT_TRUE(found_cookie);
}

/**
 * 场景 6: 特殊字符和国际化字符
 */
TEST_F(E2EHeadersTest, SpecialAndUnicodeCharacters) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "POST"},
        {":path", "/api/search?q=café"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"content-type", "text/plain; charset=utf-8"},
        {"x-custom-header", "Value with special chars: !@#$%^&*()"},
    };

    // 编码和解码
    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    // 验证
    EXPECT_EQ(decoded.size(), headers.size());
    EXPECT_EQ(decoded[1].second, "/api/search?q=café");
}

/**
 * 场景 7: HTTP 错误响应
 */
TEST_F(E2EHeadersTest, ErrorResponseHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":status", "404"},
        {"content-type", "text/html; charset=utf-8"},
        {"content-length", "256"},
        {"server", "nginx/1.18.0"},
        {"cache-control", "no-cache, no-store, must-revalidate"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded[0].first, ":status");
    EXPECT_EQ(decoded[0].second, "404");
}

/**
 * 场景 8: WebSocket 升级请求
 */
TEST_F(E2EHeadersTest, WebSocketUpgradeRequest) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/chat"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"upgrade", "websocket"},
        {"connection", "Upgrade"},
        {"sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ=="},
        {"sec-websocket-version", "13"},
        {"user-agent", "MyWebSocketClient/1.0"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());

    // 验证关键 WebSocket 头
    bool found_upgrade = false;
    bool found_ws_key = false;
    for (const auto& header : decoded) {
        if (header.first == "upgrade") found_upgrade = true;
        if (header.first == "sec-websocket-key") found_ws_key = true;
    }
    EXPECT_TRUE(found_upgrade);
    EXPECT_TRUE(found_ws_key);
}

/**
 * 场景 9: 重定向响应
 */
TEST_F(E2EHeadersTest, RedirectResponse) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":status", "301"},
        {"location", "https://example.com/new-path"},
        {"content-length", "0"},
        {"cache-control", "public, max-age=31536000"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    bool found_location = false;
    for (const auto& header : decoded) {
        if (header.first == "location") {
            found_location = true;
            EXPECT_EQ(header.second, "https://example.com/new-path");
        }
    }
    EXPECT_TRUE(found_location);
}

/**
 * 场景 10: 压缩响应头（GZIP 编码）
 */
TEST_F(E2EHeadersTest, CompressedResponseHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":status", "200"},
        {"content-type", "text/html; charset=utf-8"},
        {"content-encoding", "gzip"},
        {"content-length", "512"},
        {"vary", "Accept-Encoding"},
        {"cache-control", "public, max-age=3600"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    bool found_gzip = false;
    for (const auto& header : decoded) {
        if (header.first == "content-encoding") {
            found_gzip = true;
            EXPECT_EQ(header.second, "gzip");
        }
    }
    EXPECT_TRUE(found_gzip);
}

/**
 * 场景 11: 服务器推送（Server Push）
 */
TEST_F(E2EHeadersTest, ServerPushHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/style.css"},
        {":scheme", "https"},
        {":authority", "example.com"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());
}

/**
 * 场景 12: 大量头字段（模拟真实浏览器请求）
 */
TEST_F(E2EHeadersTest, RealBrowserRequestHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/page?id=123"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"user-agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"},
        {"accept", "text/html,application/xhtml+xml,application/xml;q=0.9"},
        {"accept-encoding", "gzip, deflate, br"},
        {"accept-language", "en-US,en;q=0.9,fr;q=0.8"},
        {"cache-control", "max-age=0"},
        {"cookie", "session_id=abc123; preferences=dark_mode"},
        {"dnt", "1"},
        {"referer", "https://google.com/"},
        {"sec-ch-ua", "\" Not A;Brand\";v=\"99\", \"Chromium\";v=\"90\""},
        {"sec-fetch-dest", "document"},
        {"sec-fetch-mode", "navigate"},
        {"sec-fetch-site", "none"},
        {"sec-fetch-user", "?1"},
        {"upgrade-insecure-requests", "1"},
        {"x-forwarded-for", "192.168.1.100"},
        {"x-forwarded-proto", "https"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());

    // 验证编码大小合理（压缩效率）
    // 原始大小大约 1000+ 字节，压缩后应该小于这个
    EXPECT_LT(encoded.size(), 2000);
}

/**
 * 场景 13: 多次编码解码周期
 */
TEST_F(E2EHeadersTest, MultipleCycles) {
    std::vector<std::pair<std::string, std::string>> original_headers = {
        {":method", "POST"},
        {":path", "/api/data"},
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"content-type", "application/json"},
    };

    // 第一个周期
    auto encoded1 = HPACK::encode(original_headers);
    auto decoded1 = HPACK::decode(encoded1);
    EXPECT_EQ(decoded1.size(), original_headers.size());

    // 第二个周期（使用第一个周期的输出）
    auto encoded2 = HPACK::encode(decoded1);
    auto decoded2 = HPACK::decode(encoded2);
    EXPECT_EQ(decoded2.size(), original_headers.size());

    // 验证数据完整性
    for (size_t i = 0; i < original_headers.size(); ++i) {
        EXPECT_EQ(decoded2[i].first, original_headers[i].first);
        EXPECT_EQ(decoded2[i].second, original_headers[i].second);
    }
}

/**
 * 场景 14: 空值头字段
 */
TEST_F(E2EHeadersTest, EmptyValueHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"x-empty-header", ""},
        {"accept-charset", ""},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());

    // 验证空值被保留
    int empty_count = 0;
    for (const auto& header : decoded) {
        if (header.second.empty()) {
            empty_count++;
        }
    }
    EXPECT_GE(empty_count, 2);  // 至少有 2 个空值
}

/**
 * 场景 15: HTTP/2 PUSH_PROMISE 帧头
 */
TEST_F(E2EHeadersTest, PushPromiseHeaders) {
    // 服务器推送 CSS 文件
    std::vector<std::pair<std::string, std::string>> push_headers = {
        {":method", "GET"},
        {":path", "/styles/main.css"},
        {":scheme", "https"},
        {":authority", "example.com"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(push_headers);
    EXPECT_FALSE(encoded.empty());

    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);
    EXPECT_EQ(decoded.size(), push_headers.size());

    // 验证路径指向 CSS 文件
    EXPECT_EQ(decoded[1].second, "/styles/main.css");
}

/**
 * 场景 16: 条件请求头
 */
TEST_F(E2EHeadersTest, ConditionalRequestHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/resource"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"if-match", "\"e1ca7e5\""},
        {"if-none-match", "\"e1ca7e4\""},
        {"if-modified-since", "Wed, 21 Oct 2015 07:28:00 GMT"},
        {"if-unmodified-since", "Wed, 21 Oct 2015 07:28:00 GMT"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());
}

/**
 * 场景 17: 内容协商头
 */
TEST_F(E2EHeadersTest, ContentNegotiationHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/api/data"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"accept", "application/json, application/xml;q=0.9, */*;q=0.8"},
        {"accept-encoding", "gzip, deflate, br"},
        {"accept-language", "zh-CN,zh;q=0.9,en;q=0.8"},
        {"accept-charset", "utf-8"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());
}

/**
 * 场景 18: 跨域请求（CORS）
 */
TEST_F(E2EHeadersTest, CORSRequestHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "OPTIONS"},
        {":path", "/api/resource"},
        {":scheme", "https"},
        {":authority", "api.example.com"},
        {"origin", "https://client.example.com"},
        {"access-control-request-method", "POST"},
        {"access-control-request-headers", "content-type, authorization"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());
}

/**
 * 场景 19: 文件上传请求头
 */
TEST_F(E2EHeadersTest, FileUploadHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "POST"},
        {":path", "/upload"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"content-type", "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW"},
        {"content-length", "12345"},
        {"accept", "*/*"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), headers.size());
}

/**
 * 场景 20: 客户端身份验证失败响应
 */
TEST_F(E2EHeadersTest, UnauthorizedResponseHeaders) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {":status", "401"},
        {"content-type", "application/json"},
        {"content-length", "100"},
        {"www-authenticate", "Bearer realm=\"API\", charset=\"UTF-8\""},
        {"cache-control", "no-store"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded[0].first, ":status");
    EXPECT_EQ(decoded[0].second, "401");
}

/**
 * 场景 21: 静态和动态表协同工作
 */
TEST_F(E2EHeadersTest, StaticAndDynamicTableInteraction) {
    HeaderTable table;

    // 第一个请求使用静态表
    int method_idx = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(method_idx, 2);  // 静态表索引

    // 添加自定义头到动态表
    table.insertDynamic({"x-custom-header", "custom-value"});

    // 查询自定义头应该在动态表中
    int custom_idx = table.getIndexByNameValue("x-custom-header", "custom-value");
    EXPECT_EQ(custom_idx, 62);  // 动态表索引 = 61 + 1

    // 查询静态表的头仍然应该有效
    method_idx = table.getIndexByNameValue(":method", "GET");
    EXPECT_EQ(method_idx, 2);
}

/**
 * 场景 22: 流头尾部 (HPACK 尾部块)
 */
TEST_F(E2EHeadersTest, TrailerHeaders) {
    std::vector<std::pair<std::string, std::string>> trailer_headers = {
        {"x-checksum", "abc123def456"},
        {"x-timestamp", "1234567890"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(trailer_headers);
    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);

    EXPECT_EQ(decoded.size(), trailer_headers.size());
}

/**
 * 场景 23: HTTP 分块传输编码和尾部头
 */
TEST_F(E2EHeadersTest, ChunkedEncodingWithTrailers) {
    // 主响应头
    std::vector<std::pair<std::string, std::string>> main_headers = {
        {":status", "200"},
        {"transfer-encoding", "chunked"},
        {"content-type", "text/plain"},
    };

    std::vector<uint8_t> encoded_main = HPACK::encode(main_headers);
    std::vector<std::pair<std::string, std::string>> decoded_main = HPACK::decode(encoded_main);

    EXPECT_EQ(decoded_main.size(), main_headers.size());

    // 尾部头（在块结束后）
    std::vector<std::pair<std::string, std::string>> trailer_headers = {
        {"x-checksum-md5", "5d41402abc4b2a76b9719d911017c592"},
    };

    std::vector<uint8_t> encoded_trailer = HPACK::encode(trailer_headers);
    std::vector<std::pair<std::string, std::string>> decoded_trailer = HPACK::decode(encoded_trailer);

    EXPECT_EQ(decoded_trailer.size(), trailer_headers.size());
}

/**
 * 场景 24: 长连接头管理
 */
TEST_F(E2EHeadersTest, PersistentConnectionHeaders) {
    // 第一个请求
    std::vector<std::pair<std::string, std::string>> headers1 = {
        {":method", "GET"},
        {":path", "/page1"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"connection", "keep-alive"},
    };

    std::vector<uint8_t> encoded1 = HPACK::encode(headers1);
    std::vector<std::pair<std::string, std::string>> decoded1 = HPACK::decode(encoded1);

    EXPECT_EQ(decoded1.size(), headers1.size());

    // 第二个请求（在同一连接上）
    std::vector<std::pair<std::string, std::string>> headers2 = {
        {":method", "GET"},
        {":path", "/page2"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"connection", "keep-alive"},
    };

    std::vector<uint8_t> encoded2 = HPACK::encode(headers2);
    std::vector<std::pair<std::string, std::string>> decoded2 = HPACK::decode(encoded2);

    EXPECT_EQ(decoded2.size(), headers2.size());
}

/**
 * 场景 25: 应用层协议协商 (ALPN)
 */
TEST_F(E2EHeadersTest, ALPNProtocolHeaders) {
    // 这通常在 TLS 握手中，但我们可以验证后续的 HTTP/2 头
    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"user-agent", "MyClient/1.0"},
    };

    std::vector<uint8_t> encoded = HPACK::encode(headers);
    EXPECT_FALSE(encoded.empty());

    std::vector<std::pair<std::string, std::string>> decoded = HPACK::decode(encoded);
    EXPECT_EQ(decoded.size(), headers.size());
}

} // namespace http2
