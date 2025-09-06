/**
 * @file i2s_mic.h
 * @brief ESP32 websocket模块接口
 */

#ifndef _LSH_WEBSOCKET_H_
#define _LSH_WEBSOCKET_H_

#include <stddef.h>
#include "esp_err.h"

// 定义客户端连接回调函数类型
typedef void (*client_connected_cb_t)(void);

esp_err_t websocket_init(uint16_t port);
esp_err_t webSocketSendMessage(void *dataPtr, size_t size);

void setBeginCallback(client_connected_cb_t callback);
void setEndCallback(client_connected_cb_t callback);
#endif