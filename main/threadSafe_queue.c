#include "threadSafe_queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char* TAG = "threadSafe";
static uint32_t log_counter = 0;


int queue_init(ThreadSafeQueue* queue, int capacity, bool discard_when_full) {
    queue->items = (void**)malloc(capacity * sizeof(void*));
    if (!queue->items) return -1;
    
    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->stopped = false;
    queue->discard_oldest = discard_when_full;
    
    if (pthread_mutex_init(&queue->mutex, NULL) != 0 ||
        pthread_cond_init(&queue->not_empty, NULL) != 0 ||
        pthread_cond_init(&queue->not_full, NULL) != 0) {
        free(queue->items);
        return -1;
    }
    
    return 0;
}

int queue_push(ThreadSafeQueue* queue, void* item, bool block) {
     pthread_mutex_lock(&queue->mutex);
    
    // 检查队列是否停止
    if (queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // 处理队列已满的情况
    if (queue->count >= queue->capacity) {
        if (!queue->discard_oldest) {
            // 不允许丢弃旧数据，根据block参数决定是等待还是返回
            if (!block) {
                pthread_mutex_unlock(&queue->mutex);
                return -1; // 非阻塞模式直接返回失败
            }
            
            // 阻塞模式等待队列有空间
            while (queue->count >= queue->capacity && !queue->stopped) {
                pthread_cond_wait(&queue->not_full, &queue->mutex);
            }
            
            if (queue->stopped) {
                pthread_mutex_unlock(&queue->mutex);
                return -1;
            }
        } else {
            // 允许丢弃旧数据，移除最老的元素
            if (queue->count > 0) {
                queue->head = (queue->head + 1) % queue->capacity;
                queue->count--;
                if (log_counter ++ % 100 == 0) {
                    ESP_LOGW(TAG, "队列已满，丢弃最老的音频帧");
                }
            }
        }
    }
    
        // 将新元素加入队列
    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    
    // 通知等待的消费者队列非空
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int queue_pop(ThreadSafeQueue* queue, void** item, bool block) {
    pthread_mutex_lock(&queue->mutex);
    
    // 检查队列是否停止且为空
    if (queue->stopped && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // 如果队列为空，根据block参数决定是等待还是返回
    if (queue->count == 0) {
        if (!block) {
            pthread_mutex_unlock(&queue->mutex);
            return -1; // 非阻塞模式直接返回失败
        }
        
        // 阻塞模式等待队列有数据
        while (queue->count == 0 && !queue->stopped) {
            pthread_cond_wait(&queue->not_empty, &queue->mutex);
        }
        
        if (queue->stopped && queue->count == 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    
    // 从队列获取元素
    *item = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    
    // 通知等待的生产者队列非满
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

void queue_stop(ThreadSafeQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    // 唤醒所有等待的线程
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

void queue_destroy(ThreadSafeQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_mutex_unlock(&queue->mutex);
    
    // 销毁条件变量和互斥锁
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
    
    // 释放内存
    free(queue->items);
}