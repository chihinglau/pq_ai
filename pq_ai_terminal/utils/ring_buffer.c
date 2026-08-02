/**
 * @file ring_buffer.c
 * @brief 单生产者单消费者无锁环形缓冲实现
 */

#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>

int ring_buffer_init(ring_buffer_t *rb, uint32_t size)
{
    if (rb == NULL || size == 0) return -1;
    rb->buffer = (uint8_t *)malloc(size);
    if (rb->buffer == NULL) return -1;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    return 0;
}

void ring_buffer_deinit(ring_buffer_t *rb)
{
    if (rb == NULL) return;
    if (rb->buffer) {
        free(rb->buffer);
        rb->buffer = NULL;
    }
    rb->size = 0;
    rb->head = 0;
    rb->tail = 0;
}

uint32_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, uint32_t len)
{
    uint32_t free_bytes, write_len, i;
    if (rb == NULL || data == NULL || len == 0) return 0;

    free_bytes = ring_buffer_free(rb);
    write_len = (len > free_bytes) ? free_bytes : len;

    for (i = 0; i < write_len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    return write_len;
}

uint32_t ring_buffer_read(ring_buffer_t *rb, uint8_t *data, uint32_t len)
{
    uint32_t avail, read_len, i;
    if (rb == NULL || data == NULL || len == 0) return 0;

    avail = ring_buffer_available(rb);
    read_len = (len > avail) ? avail : len;

    for (i = 0; i < read_len; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    return read_len;
}

uint32_t ring_buffer_available(const ring_buffer_t *rb)
{
    if (rb == NULL) return 0;
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return rb->size - rb->tail + rb->head;
    }
}

uint32_t ring_buffer_free(const ring_buffer_t *rb)
{
    if (rb == NULL || rb->size == 0) return 0;
    return rb->size - 1 - ring_buffer_available(rb);
}
