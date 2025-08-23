/**
 * @file i2s_mic.h
 * @brief ESP32 I2S麦克风模块接口
 */

#ifndef _I2S_MIC_H_
#define _I2S_MIC_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  // vTaskDelay 定义在此处
#include "hal/i2s_types.h"  // 新增：包含 I2S 位深度等类型定义
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2S麦克风配置结构体
 */
typedef struct {
    int bclk_gpio;              // 位时钟GPIO
    int lrclk_gpio;             // 左右声道时钟GPIO
    int data_in_gpio;           // 数据输入GPIO
    uint32_t sample_rate;       // 采样率，例如16000Hz
    i2s_data_bit_width_t bit_depth;  // 位深度
    uint32_t buffer_size;       // 缓冲区大小
} i2s_mic_config_t;

/**
 * @brief 默认配置
 */
extern const i2s_mic_config_t i2s_mic_default_config;

/**
 * @brief 初始化I2S麦克风
 * @param config 配置参数
 * @return ESP_OK表示成功，其他值表示错误
 */
esp_err_t i2s_mic_init(const i2s_mic_config_t* config);


/**
 * @brief 读取麦克风数据
 * @param buffer 数据缓冲区
 * @param length 缓冲区长度（字节）
 * @param bytes_read 实际读取的字节数
 * @param timeout 超时时间（RTOS ticks）
 * @return ESP_OK表示成功，其他值表示错误
 */
esp_err_t i2s_mic_read(void* buffer, size_t length, size_t* bytes_read, uint32_t timeout);

/**
 * @brief 获取麦克风状态
 * @return true表示正在运行，false表示已停止
 */
bool i2s_mic_is_running(void);

/**
 * @brief 释放I2S麦克风资源
 * @return ESP_OK表示成功，其他值表示错误
 */
esp_err_t i2s_mic_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // _I2S_MIC_H_    