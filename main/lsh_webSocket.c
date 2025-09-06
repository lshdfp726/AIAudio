#include "lsh_webSocket.h"

#include "esp_http_server.h"
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"

static uint32_t log_counter = 0;
static const char* TAG = "WEBSOCKET";
static httpd_handle_t server = NULL; //
static httpd_req_t* client_req = NULL;
static int socketfd = 0;

static client_connected_cb_t connected_callback = NULL;
static client_connected_cb_t disConnected_callback = NULL;
void setBeginCallback(client_connected_cb_t callback) {
    connected_callback = callback;
}
void setEndCallback(client_connected_cb_t callback) {
    disConnected_callback = callback;
}


static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        int sockfd = httpd_req_to_sockfd(req);
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        // 获取客户端IP地址
        if (getpeername(sockfd, (struct sockaddr*)&client_addr, &client_addr_len) == 0) {
            ESP_LOGI(TAG, "客户端连接: %s:%d (req=%p)",
                    inet_ntoa(client_addr.sin_addr),
                    ntohs(client_addr.sin_port),
                    req);
                    if (connected_callback) connected_callback();
        } else {
            ESP_LOGI(TAG, "客户端连接 (req=%p)", req);
        }
        
        // 保存客户端请求指针
        client_req = req;
        socketfd = sockfd;
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0 , sizeof(httpd_ws_frame_t));

       // 读取客户端发送的帧
    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "接收WebSocket帧失败: %s", esp_err_to_name(err));
        client_req = NULL; // 标记连接失效
        return err;
    }
    // 处理不同类型的帧
    switch (ws_pkt.type) {
        case HTTPD_WS_TYPE_PING:
            // 自动回复Pong（ESP-IDF底层已处理，无需手动回复）
            ESP_LOGI(TAG, "收到Ping帧，自动回复Pong");
            break;
        case HTTPD_WS_TYPE_CLOSE:
            // 处理客户端关闭请求
            ESP_LOGI(TAG, "客户端请求关闭连接");
            // if (disConnected_callback) disConnected_callback();
            client_req = NULL;
            return ESP_OK;
        case HTTPD_WS_TYPE_TEXT:
            // 处理文本数据（示例）
            ESP_LOGI(TAG, "收到文本数据: %.*s", ws_pkt.len, (char*)ws_pkt.payload);
            break;
        case HTTPD_WS_TYPE_BINARY:
            // 处理二进制数据（示例）
            ESP_LOGI(TAG, "收到二进制数据，长度: %d字节", ws_pkt.len);
            break;
        default:
            ESP_LOGW(TAG, "未知帧类型: %d", ws_pkt.type);
            break;
    }

    return ESP_OK;
}

esp_err_t websocket_init(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动HTTP服务器失败: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t ws_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &ws_uri);

    ESP_LOGI(TAG, "WebSocket服务器启动成功，端口: %d", port);
    return ESP_OK;
}

// 发送二进制数据（如音频帧）
esp_err_t webSocketSendMessage(void *data, size_t len) {
    if (server == NULL || client_req == NULL) {
        if(log_counter % 100 == 0) {
            ESP_LOGE(TAG, "无有效连接，发送失败");
        }
        log_counter ++;
        return ESP_FAIL;
    }
    // 构建WebSocket二进制帧
    httpd_ws_frame_t ws_pkt = {
       .type = HTTPD_WS_TYPE_BINARY,    // 二进制帧类型
       .final = true,                   // 单帧完成
       .payload = (uint8_t*)data,
       .len = len
    };

    // 发送帧（异步非阻塞模式）
    esp_err_t err = httpd_ws_send_frame_async(server, socketfd, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "发送二进制帧失败: %s", esp_err_to_name(err));
        client_req = NULL; // 标记连接失效
        return err;
    }

    // ESP_LOGI(TAG, "发送二进制数据，长度: %d字节", len);
    return ESP_OK;
}