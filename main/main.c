/**
 * @file main.c
 * @brief 使用I2S麦克风模块的示例
 */

#include "esp_log.h"
#include "i2s_mic.h"
#include "threadSafe_queue.h"

#define WIFI_MODE
#define WEBSOCKET

#ifdef WIFI_MODE
#include "lsh_wifi.h"
#ifdef WEBSOCKET
#include "lsh_webSocket.h"
#else
#include "lsh_socket.h"
#endif
#else
#include "lsh_blue.h"
#endif
#include <errno.h>   // 包含 errno 定义
#include "nvs_flash.h"  // 用于存储WiFi/BF配置

typedef struct AudioFrame {
    int32_t *data;
    int sample_count;
} AudioFrame;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// 定义信号强弱阈值（24位有效信号范围内）
#define STRONG_THRESHOLD  (1 << 16)  // 65536（强信号阈值）
#define WEAK_THRESHOLD    (1 << 10)  // 1024（弱信号阈值）
static const char* TAG = "MAIN";
#define BUFFER_SIZE     1024         // 缓冲区大小（字节），注意：应是4的倍数（32位样本）
#define FRAME_SAMPLES   (BUFFER_SIZE / 4)  // 每帧样本数

// 全局队列
static ThreadSafeQueue audio_queue;
static uint32_t log_counter = 0;
static uint32_t send_log_counter = 0;
// Socket参数
#define PORT 12345

// 在FIR滤波前增加增益
#define PREAMPLIFIER_GAIN 2.0f
// 新的FIR系数：截止频率 300hz - 3kHz 人声频率范围，采样率16kHz，17阶
const float fir_coeffs[17] = {
    -0.0007f, -0.0001f, 0.0017f, 0.0048f, 0.0093f, 0.0151f,
    0.0217f, 0.0283f, 0.0340f, 0.0378f, 0.0390f, 0.0378f,
    0.0340f, 0.0283f, 0.0217f, 0.0151f, 0.0093f
};

// 截止频率 6kHz
// const float fir_coeffs[17] = {
//     0.0014f, 0.0020f, 0.0035f, 0.0063f, 0.0105f, 0.0162f,
//     0.0231f, 0.0307f, 0.0377f, 0.0430f, 0.0458f, 0.0458f,
//     0.0430f, 0.0377f, 0.0307f, 0.0231f, 0.0162f
// };



#define POOL_SIZE 10
static AudioFrame frame_pool[POOL_SIZE];
static int32_t data_pool[POOL_SIZE][FRAME_SAMPLES]; // 预分配数据缓冲区
static int pool_next = 0; // 下一个可用帧的索引

// 从内存池获取帧（无需 malloc）
AudioFrame* get_frame_from_pool(uint32_t sample_count) {
    if (sample_count > FRAME_SAMPLES) {
        ESP_LOGE(TAG, "样本数超过内存池最大限制");
        return NULL;
    }
    AudioFrame* frame = &frame_pool[pool_next];
    frame->sample_count = sample_count;
    frame->data = data_pool[pool_next]; // 指向预分配的缓冲区
    pool_next = (pool_next + 1) % POOL_SIZE; // 循环复用
    return frame;
}

// 使用环形缓冲区优化FIR滤波器
void fir_filter(int32_t* input, int32_t* output, size_t count) {
    static int32_t history[17] = {0};
    static int history_idx = 0;
    
    for (size_t i = 0; i < count; i++) {
        // 保存新输入到历史缓冲区
        history[history_idx] = input[i];
        
        // 卷积计算 模拟 y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + ... + bN*x[n-N]
        output[i] = 0;
        for (int j = 0; j < 17; j++) {
            int idx = (history_idx - j + 17) % 17; // 环形缓冲区索引
            output[i] += history[idx] * fir_coeffs[j]; //每个循环卷积17次
        }
        
        // 更新历史索引，
        history_idx = (history_idx + 1) % 17;
    }
}

// static bool process_audio_frame(int32_t* samples, uint32_t sample_count) {
//     static const uint32_t silence_threshold = 25; // 根据16位数据重新校准阈值
    
//     uint32_t sum = 0;
//     int16_t max_val = 0;

//     for (uint32_t i = 0; i < sample_count; i++)
//     {
//         // 16位数据处理：直接取低16位有效数据（无需移位）
//         int16_t valid_sample = (int16_t)(samples[i] & 0xFFFF);  // 仅保留低16位
        
//         sum += abs(valid_sample);
//         if (abs(valid_sample) > max_val) {
//             max_val = abs(valid_sample);
//         }
//     }
    
//     uint32_t avg_energy = sum / sample_count;
//     bool has_sound = avg_energy > silence_threshold;

//     if (log_counter % 100 == 0) {
//         ESP_LOGI(TAG, "音频帧 | 样本数: %u | 平均能量: %u | 最大幅度: %d | 状态: %s",
//             sample_count, avg_energy, max_val, has_sound ? "有声音" : "安静");
//     }
//     return has_sound;
// }



static bool process_audio_frame(int32_t* samples, uint32_t sample_count) {
    static const uint32_t silence_threshold = 25; //实际测试手机音乐播放平均音频阈值大概这范围


    uint32_t sum = 0;
    int16_t max_val = 0;

    for (uint32_t i = 0; i < sample_count; i++)
    {
        int32_t raw_24bit = samples[i] >> 8;  // 丢弃低 8 位填充，得到 24 位有效数据
        int16_t valid_sample = (int16_t)(raw_24bit >> 8);  // 右移 8 位，转为 16 位（缩放）
        sum += abs(valid_sample); // 需要计算绝对值
        if (abs(valid_sample) > max_val) {
            max_val = abs(valid_sample);
        }
    }
    uint32_t avg_energy = sum / sample_count;
    bool has_sound = avg_energy > silence_threshold;

    if (log_counter % 100 == 0) {
        ESP_LOGI(TAG, "音频帧 | 样本数: %u | 平均能量: %u | 最大幅度: %u | 状态: %s",
            sample_count, avg_energy, max_val, has_sound ? "有声音" : "安静");
    }
    return has_sound;
}



// 麦克风数据采集任务（增强版）
static void mic_record_task(void* arg) {
   static AudioFrame frames[3]; //3缓冲
   static int32_t frame_data[3][FRAME_SAMPLES];
   static int32_t filtered_frame[3][FRAME_SAMPLES];

    for (int i = 0; i < 3; i++)
    {
        frames[i].data = frame_data[i];
        frames[i].sample_count = 0;
    }

    int current_frame = 0;
    size_t bytes_read = 0;

    while (1) {
        memset(frames[current_frame].data, 0, FRAME_SAMPLES * sizeof(int32_t));
        esp_err_t err = i2s_mic_read(frames[current_frame].data, BUFFER_SIZE, &bytes_read, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "读取I2S数据失败: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        //一个样本采样点位深是32位，所以，当前采集到的总的数据字节/4 = 总的样本数量
        uint32_t sample_count = bytes_read / 4;
        if (sample_count == 0) continue;

        // 打印前5个原始样本的十六进制值
        // for (int i = 0; i < 5; i++) {
        //     ESP_LOGI(TAG, "原始32位数据: 0x%08X", frames[current_frame].data[i]);
        // }
        fir_filter(frames[current_frame].data, filtered_frame[current_frame], sample_count);
        // memcpy(filtered_frame[current_frame], frames[current_frame].data, sample_count);

        bool has_sound = process_audio_frame(filtered_frame[current_frame], sample_count);
        log_counter ++;
        // 只对有声音的帧进行发送（可根据需求调整）
        if (has_sound) {
            AudioFrame* frame = get_frame_from_pool(sample_count);
            if (frame == NULL) {
                ESP_LOGE(TAG, "内存池已满，丢弃当前帧");
                continue;
            }
            memcpy(frame->data, filtered_frame[current_frame], FRAME_SAMPLES * sizeof(int32_t));

            if(log_counter % 100 == 0) {
                ESP_LOGW(TAG, "frame_to_send->sample_count: %d", sample_count);
            }
            // 入队（非阻塞模式，队列满时自动丢弃最旧数据）
            if (queue_push(&audio_queue, frame, false) != 0) {
                if (log_counter % 100 == 0) { // 减少日志输出频率
                    ESP_LOGW(TAG, "队列已满，正在丢弃最旧的音频帧");
                }
            }
        }
        // 切换到下一帧缓冲区
        current_frame = (current_frame + 1) % 3;
        if (log_counter % 100 == 0) {
            size_t free_heap = esp_get_free_heap_size();
            size_t min_free_heap = esp_get_minimum_free_heap_size();
            ESP_LOGI(TAG, "当前空闲堆：%u KB，历史最小空闲堆：%u KB", 
                    free_heap / 1024, min_free_heap / 1024);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

int calShift_bits(int32_t* input, int sample_count) {
    // 1. 计算当前帧的峰值幅度（最大绝对值）
    int32_t peak = 0;
    for (int i = 0; i < sample_count; i++) {
        int32_t abs_val = abs(input[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }

    // 2. 根据峰值选择移位位数
    int shift_bits;
    if (peak > STRONG_THRESHOLD) {
        shift_bits = 8;  // 强信号：右移8位，保留低8位细节
    } else if (peak < WEAK_THRESHOLD) {
        shift_bits = 14; // 弱信号：右移14位，过滤低14位噪声
    } else {
        shift_bits = 11; // 中等信号：折中右移11位（可自定义）
    }
    return shift_bits;
}
#ifdef WIFI_MODE
static void network_send_task(void *arg) {

#ifdef WEBSOCKET
    AudioFrame *frame = NULL;

    int16_t *buffer_16bit = (int16_t *)malloc(FRAME_SAMPLES * sizeof(int16_t));
    

    while (1)
    {
        // 从队列获取帧（阻塞模式，队列为空时等待）
        if (queue_pop(&audio_queue, (void**)&frame, true) != 0) {
            ESP_LOGW(TAG, "队列已停止，退出发送任务");
            break;
        }
        // ESP_LOGE(TAG, "取出的帧地址：%p，内存池范围：%p ~ %p",
                // frame, frame_pool, frame_pool + POOL_SIZE - 1);

        int shift_bits = calShift_bits(frame->data, frame->sample_count);
        for (uint32_t i = 0; i < frame->sample_count; i++) {
            // 1. 转换为16位（根据数据对齐方式调整移位位数）
            int32_t temp = (int32_t)(frame->data[i] >> 16);  // 临时用32位存储，避免中间溢出
            
            // 2. 放大10倍
            temp *= 10;
            
            // 3. 限幅（核心步骤，必须保留）
            if (temp > 32767) {
                buffer_16bit[i] = 32767;
            } else if (temp < -32768) {
                buffer_16bit[i] = -32768;
            } else {
                buffer_16bit[i] = (int16_t)temp;
            }
        }

        if (frame->sample_count > 0 && frame->data != NULL) {
            webSocketSendMessage(buffer_16bit,frame->sample_count * sizeof(int16_t));
        }

    }

    free(buffer_16bit);
    vTaskDelete(NULL);
#else
printf("Socket 发送线程启动\n");
    AudioFrame *frame = NULL;

    int16_t *buffer_16bit = (int16_t *)malloc(FRAME_SAMPLES * sizeof(int16_t));

    while (1)
    {
        // 从队列获取帧（阻塞模式，队列为空时等待）
        if (queue_pop(&audio_queue, (void**)&frame, true) != 0) {
            ESP_LOGW(TAG, "队列已停止，退出发送任务");
            break;
        }
        ESP_LOGE(TAG, "取出的帧地址：%p，内存池范围：%p ~ %p",
                frame, frame_pool, frame_pool + POOL_SIZE - 1);
    
        for (uint32_t i = 0; i < frame->sample_count; i++)
        {
            buffer_16bit[i] = (uint16_t)(frame->data[i] >> 14);
        }

        if (frame->sample_count > 0 && frame->data != NULL) {
            ssize_t ret = sendMessage(buffer_16bit,frame->sample_count * sizeof(int16_t), 0);
        }

        // 关键：释放动态分配的内存
        // free(frame->data); // 先释放数据缓冲区
        // free(frame); // 再释放帧结构体
        // frame = NULL;      // 避免野指针
    }

    free(buffer_16bit);
    vTaskDelete(NULL);
#endif
}
#else

static void blue_send_task(void *arg) {
    AudioFrame *frame = NULL;

    int16_t *buffer_16bit = (int16_t *)malloc(FRAME_SAMPLES * sizeof(int16_t));
    if (buffer_16bit == NULL) {
        ESP_LOGE(TAG, "buffer_16bit 分配失败");
        vTaskDelete(NULL);
    }

    while (1)
    {
         // 从队列获取帧（阻塞模式，队列为空时等待）
        if (queue_pop(&audio_queue, (void**)&frame, true) != 0) {
            ESP_LOGW(TAG, "队列已停止，退出发送任务");
            break;
        }
        ESP_LOGE(TAG, "取出的帧地址：%p，内存池范围：%p ~ %p",
                frame, frame_pool, frame_pool + POOL_SIZE - 1);
        for (uint32_t i = 0; i < frame->sample_count; i++)
        {
            buffer_16bit[i] = (uint16_t)(frame->data[i] >> 14);
        }

        // 通过 BLE 通知发送数据（一次最多发送 20 字节，超过需分片）
        uint32_t total_len = frame->sample_count * sizeof(int16_t);
        // 确保总长度是偶数（16bit 样本必须是 2 字节的倍数）
        if (total_len % 2 != 0) {
            total_len--;  // 截断最后一个不完整的字节，后续考虑补0
        }
        uint32_t offset = 0;
        while (offset < total_len) {
            uint32_t send_len = MIN(20, total_len - offset); // BLE 通知最大 20 字节
            void *buffer = (uint8_t*)&buffer_16bit + offset;
            blueSendMessage(buffer, send_len);
            offset += send_len;
            vTaskDelay(pdMS_TO_TICKS(1)); // 避免发送过快
        }
        ESP_LOGI(TAG, "通过 BLE 发送音频数据：%d 字节", total_len);
    }

    free(buffer_16bit);
    vTaskDelete(NULL);
}
#endif

void beginSend(void) {
    // xTaskCreate(network_send_task, "net_send", 8192, NULL, 4, NULL);
}

void app_main(void) {
    // 初始化NVS（WiFi配置需要存储在NVS中）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#ifdef WIFI_MODE
    lsh_wifi_init_sta();
    #ifdef WEBSOCKET
        ret = websocket_init(PORT);
        setBeginCallback(beginSend);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "websocket初始化失败: %d", ret);
            return ;
        }
    #else
        createSocket(PORT);
    #endif
#else
    lsh_blue_init();
#endif
    i2s_mic_config_t mic_config = i2s_mic_default_config;

    mic_config.sample_rate = 16000;
    mic_config.bclk_gpio = GPIO_NUM_36;
    mic_config.lrclk_gpio = GPIO_NUM_37;
    mic_config.data_in_gpio = GPIO_NUM_35;
    mic_config.buffer_size = BUFFER_SIZE;
    mic_config.bit_depth = I2S_DATA_BIT_WIDTH_32BIT;

    esp_err_t err = i2s_mic_init(&mic_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "麦克风初始化失败: %s", esp_err_to_name(err));
        return ;
    }

    if (queue_init(&audio_queue, 10, true) != 0) {
        ESP_LOGE(TAG, "队列初始化失败");
        i2s_mic_deinit();
        return;
    }

    // 创建数据处理任务
    xTaskCreate(mic_record_task, "mic_process", 8192, NULL, 5, NULL);
    
#ifdef WIFI_MODE
    // 创建网络发送任务
    xTaskCreate(network_send_task, "net_send", 8192, NULL, 4, NULL);
#else
    xTaskCreate(blue_send_task, "net_send", 8192, NULL, 4, NULL);
#endif
    // 主循环可以执行其他任务
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 程序结束时释放资源（实际上不会执行到这里）
    queue_stop(&audio_queue);
    queue_destroy(&audio_queue);
    i2s_mic_deinit();
}