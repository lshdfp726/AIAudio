#include "lsh_wifi.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <netinet/in.h>

static const char* TAG = "lsh_wifi";

#define WIFI_SSID "flyingbear1"
#define WIFI_PASS "flyingbear8899"
// #define WIFI_SSID "lsh726Ip"
// #define WIFI_PASS "87654321"
// 事件处理函数（用于监听WiFi连接状态）
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();  // 启动后尝试连接
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();  // 断开后重连
        ESP_LOGI(TAG, "WiFi断开，尝试重连...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "获取IP地址: " IPSTR, IP2STR(&event->ip_info.ip));  // 打印IP
    }
}


// 初始化WiFi
void lsh_wifi_init_sta(void) {
    // 1. 初始化TCP/IP协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    // 2. 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 3. 创建STA模式的网络接口
    esp_netif_create_default_wifi_sta();

    // 4. 配置WiFi参数
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. 注册事件处理函数（监听连接状态和IP获取）
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // 6. 配置WiFi连接参数（SSID和密码）
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            // 若WiFi无密码，需添加：.threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  // 设置为STA模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());  // 启动WiFi

    ESP_LOGI(TAG, "WiFi初始化完成，正在连接...");
}
