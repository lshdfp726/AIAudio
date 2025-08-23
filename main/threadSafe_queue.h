/**
 * @file i2s_mic.h
 * @brief ESP32 队列模块接口
 */

#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/// threadSafe_queue.h 文件
typedef struct {
    void** items;           // 存储指针的数组
    int capacity;           // 队列容量
    int count;              // 当前元素数量
    int head;               // 队头索引（出队位置）
    int tail;               // 队尾索引（入队位置）
    pthread_mutex_t mutex;  // 互斥锁
    pthread_cond_t not_empty; // 非空条件变量
    pthread_cond_t not_full;  // 非满条件变量
    bool stopped;           // 队列停止标志
    bool discard_oldest;    // 队列满时是否丢弃最旧数据
} ThreadSafeQueue;

// 初始化队列
int queue_init(ThreadSafeQueue* queue, int capacity, bool discard_when_full);

// 入队操作（支持阻塞和非阻塞模式）
int queue_push(ThreadSafeQueue* queue, void* item, bool block);

// 出队操作（支持阻塞和非阻塞模式）
int queue_pop(ThreadSafeQueue* queue, void** item, bool block);

// 停止队列
void queue_stop(ThreadSafeQueue* queue);

// 销毁队列
void queue_destroy(ThreadSafeQueue* queue);
#endif