/**
 * @file i2s_mic.h
 * @brief ESP32 蓝牙模块接口
 */

#ifndef _LSH_BLUE_H_
#define _LSH_BLUE_H_

#include <stddef.h>

int lsh_blue_init(void);

int blueSendMessage(void *buffer, size_t size);
#endif