/**
 * @file i2s_mic.c
 * @brief ESP32 I2S麦克风模块实现
 */

#include "i2s_mic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2S_MIC";
static i2s_chan_handle_t i2s_rx_chan = NULL;  // I2S接收通道句柄
static bool is_running = false;                // 运行状态标志


#define I2S_STD_CUSTOM_CLK_DEFAULT_CONFIG(rate) { \
    .sample_rate_hz = rate, \
    .clk_src = I2S_CLK_SRC_DEFAULT, \
    .ext_clk_freq_hz = 0, \
    .mclk_multiple = I2S_MCLK_MULTIPLE_384, \
    .bclk_div = 16, \
}

#define I2S_STD_CUSTOM_PCM_SLOT_DEFAULT_CONFIG(bits_per_sample, mono_or_stereo)  { \
    .data_bit_width = bits_per_sample, \
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, \
    .slot_mode = mono_or_stereo, \
    .slot_mask = I2S_STD_SLOT_LEFT, \
    .ws_width = 1, \
    .ws_pol = true, \
    .bit_shift = true, \
    .left_align = true, \
    .big_endian = false, \
    .bit_order_lsb = false \
}


// 默认配置
const i2s_mic_config_t i2s_mic_default_config = {
    .bclk_gpio = GPIO_NUM_36,
    .lrclk_gpio = GPIO_NUM_37,
    .data_in_gpio = GPIO_NUM_35,
    .sample_rate = 16000,
    .bit_depth = I2S_DATA_BIT_WIDTH_32BIT,
    .buffer_size = 1024,
};

esp_err_t i2s_mic_init(const i2s_mic_config_t* config) {
    if (i2s_rx_chan != NULL) {
        ESP_LOGW(TAG, "I2S麦克风已经初始化");
        return ESP_OK;
    }

    if (config == NULL) {
        config = &i2s_mic_default_config;
    }
    
    // 1. 配置I2S通道参数
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_AUTO,              // 自动选择I2S控制器
        .role = I2S_ROLE_MASTER,         // 主模式
        .dma_desc_num = 8,               // DMA描述符数量
        .dma_frame_num = config->buffer_size / 4,  // 每帧大小
        .intr_priority = 0,              // 中断优先级
    };

    // 2. 创建I2S接收通道
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &i2s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建I2S接收通道失败: %s", esp_err_to_name(err));
        return err;
    }


    // 3. 配置I2S标准模式参数
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_CUSTOM_PCM_SLOT_DEFAULT_CONFIG(config->bit_depth, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = config->bclk_gpio,
            .ws = config->lrclk_gpio,
            .dout = I2S_GPIO_UNUSED,
            .din = config->data_in_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = 1,
                .ws_inv = false
            }
        },
    };

    // 4. 初始化通道为标准模式
    err = i2s_channel_init_std_mode(i2s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "初始化I2S标准模式失败: %s", esp_err_to_name(err));
        i2s_del_channel(i2s_rx_chan);
        i2s_rx_chan = NULL;
        return err;
    }

    // 5. 启用I2S通道
    err = i2s_channel_enable(i2s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启用I2S通道失败: %s", esp_err_to_name(err));
        i2s_del_channel(i2s_rx_chan);
        i2s_rx_chan = NULL;
        return err;
    }

    ESP_LOGI(TAG, "I2S麦克风初始化成功");
    return ESP_OK;
}

esp_err_t i2s_mic_read(void* buffer, size_t length, size_t* bytes_read, uint32_t timeout) {
    if (!i2s_rx_chan) {
        ESP_LOGE(TAG, "I2S通道未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    return i2s_channel_read(i2s_rx_chan, buffer, length, bytes_read, timeout);
}

bool i2s_mic_is_running(void) {
    return is_running;
}

esp_err_t i2s_mic_deinit(void) {
    // 释放I2S资源
    if (i2s_rx_chan) {
        i2s_channel_disable(i2s_rx_chan);
        i2s_del_channel(i2s_rx_chan);
        i2s_rx_chan = NULL;
    }
    
    return ESP_OK;
}    