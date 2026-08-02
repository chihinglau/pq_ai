/**
 * @file ring_buffer.h
 * @brief 单生产者单消费者无锁环形缓冲
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 环形缓冲区结构体
 */
typedef struct {
    uint8_t  *buffer;
    uint32_t size;
    volatile uint32_t head;
    volatile uint32_t tail;
} ring_buffer_t;

/**
 * @brief 初始化环形缓冲
 * @param rb 环形缓冲指针
 * @param size 缓冲区大小（字节）
 * @return 0成功
 */
int ring_buffer_init(ring_buffer_t *rb, uint32_t size);

/**
 * @brief 释放环形缓冲
 * @param rb 环形缓冲指针
 */
void ring_buffer_deinit(ring_buffer_t *rb);

/**
 * @brief 写入数据
 * @param rb 环形缓冲指针
 * @param data 数据源
 * @param len 长度
 * @return 实际写入字节数
 */
uint32_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, uint32_t len);

/**
 * @brief 读取数据
 * @param rb 环形缓冲指针
 * @param data 目标缓冲区
 * @param len 期望读取长度
 * @return 实际读取字节数
 */
uint32_t ring_buffer_read(ring_buffer_t *rb, uint8_t *data, uint32_t len);

/**
 * @brief 可读数据量
 */
uint32_t ring_buffer_available(const ring_buffer_t *rb);

/**
 * @brief 剩余空间
 */
uint32_t ring_buffer_free(const ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */
