#include "lsh_blue.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_bt.h"

// #include "freertos/task.h"  // vTaskDelay 定义在此处

// 自定义 BLE 服务和特征值 UUID（可通过在线工具生成）
#define AUDIO_SERVICE_UUID        0x1234
#define AUDIO_CHAR_UUID           0x5678

static const char* TAG = "BLE_AUDIO";
static uint8_t ble_addr_type;
static uint16_t audio_char_handle; // 音频特征值句柄
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;  // 蓝牙连接句柄
static ble_addr_t g_peer_addr;  // 存储连接设备的地址


//广播配置（名称 ESP32—Audio）
struct ble_gap_adv_params adv_params;

// 打印BLE地址的工具函数（替代缺失的print_ble_addr）
static void print_ble_addr(const ble_addr_t *addr) {
    ESP_LOGI(TAG, "BLE地址: %02x:%02x:%02x:%02x:%02x:%02x",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

// BLE 连接状态回调
int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE连接成功 (连接句柄: %d)", g_conn_handle);
            } else {
                ESP_LOGE(TAG, "连接失败 (状态码: %d)", event->connect.status);
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            }
            return 0;
        
        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
            if (event->identity_resolved.conn_handle == g_conn_handle) {
                g_peer_addr = event->identity_resolved.peer_id_addr;
                ESP_LOGI(TAG, "获取设备地址成功");
                print_ble_addr(&g_peer_addr);
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE 已断开连接，重新开始广播...");
            ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER,
                            &adv_params, NULL, NULL);
            return 0;

        default:
            return 0;
    }
}

//ble 服务初始化（创建音频传输服务和特征值）
void gatt_svr_init() {
    //注册GATT
    struct ble_gatt_svc_def gatt_svcs[] = {
        {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = BLE_UUID16_DECLARE(AUDIO_SERVICE_UUID),
            .characteristics = (struct  ble_gatt_chr_def[]) {
                {
                    .uuid = BLE_UUID16_DECLARE(AUDIO_SERVICE_UUID),
                    .access_cb = NULL, //只读特征值无需回调
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, //支持通知
                    .val_handle = &audio_char_handle, //注册服务之后这个值会被填充

                },
                {0} //特征值列表结束
            }
        },
        {0}  //服务列表结束
    };
    ESP_LOGW(TAG, "ble_gatts_count_cfg");
    ble_gatts_count_cfg(gatt_svcs);
    ESP_LOGW(TAG, "ble_gatts_add_svcs");
    ble_gatts_add_svcs(gatt_svcs);
}

void ble_on_sync(void) {
    //获取本地BLE地址类型
    ble_hs_id_infer_auto(0, &ble_addr_type);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    const char *name = "ESP32-Audio";

    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0 ,sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; //非定向连接
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; //通用可发现
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER,
                    &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "开始广播...");
}

// 1. Nimble协议栈任务（单独任务中运行，避免阻塞）
static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "启动Nimble协议栈事件循环");
    nimble_port_run(); // 阻塞执行协议栈逻辑
    nimble_port_freertos_deinit(); // 任务退出时清理
}

int lsh_blue_init(void) {
    // 初始化Nimble HCI和控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "蓝牙控制器初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "蓝牙控制器初始化成功");
    // 步骤2：启动蓝牙控制器（BLE模式）
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "蓝牙控制器启动失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "蓝牙控制器启动成功");

    // err = esp_nimble_hci_deinit();
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "HCI 反初始化失败: %d", err);
    //     return -1;
    // }
    // 步骤1：初始化HCI和蓝牙控制器
    err = esp_nimble_hci_init();  // 仅初始化HCI
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HCI初始化失败: %d", err);
        return -1;
    }
    // 关键：等待HCI层与控制器完成握手
    ESP_LOGI(TAG, "等待HCI与控制器同步...");
    vTaskDelay(pdMS_TO_TICKS(200));  // 给HCI层与控制器足够时间同步

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nimble端口初始化失败: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGW(TAG, "注册gatt服务开始");
    gatt_svr_init();
    ESP_LOGW(TAG, "注册gatt服务结束");
    ble_hs_cfg.sync_cb = ble_on_sync;
    // 步骤5：创建独立任务运行Nimble事件循环（关键！）
    xTaskCreate(
        nimble_host_task,    // 任务函数
        "nimble_host",       // 任务名称
        4096,                // 栈大小（根据需求调整）
        NULL,                // 任务参数
        5,                   // 优先级（高于普通任务）
        NULL                 // 任务句柄（可选）
    );
    return 0;
}

int blueSendMessage(void *buffer, size_t size) {
    if(g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "未连接，无法发送数据");
        return ESP_FAIL;
    }

    // 创建内存缓冲区（根据实际Nimble API修正）
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buffer, size);
    if (om == NULL) {
        ESP_LOGE(TAG, "创建内存缓冲区失败");
        return ESP_FAIL;
    }
    int rc = ble_gattc_notify_custom(g_conn_handle, audio_char_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败 (错误码: %d)", rc);
        os_mbuf_free_chain(om);  // 释放内存
        return ESP_FAIL;
    }

    return ESP_OK;
}