/**
 * @file i2s_mic.h
 * @brief ESP32 socket模块接口
 */

#ifndef _LSH_SOCKET_H_
#define _LSH_SOCKET_H_

#include <stddef.h>
int createSocket(int port);
int sendMessage(void *dataptr, size_t size,int flags);

#endif