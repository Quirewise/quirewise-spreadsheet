#ifndef QUIREWISE_TEXT_BUFFER_H
#define QUIREWISE_TEXT_BUFFER_H

/* Shared bounded byte buffer for format adapters that build text output. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct QwTextBuffer {
    char *data;
    size_t size;
    size_t capacity;
    size_t limit;
} QwTextBuffer;

static inline int qw_text_buffer_append(QwTextBuffer *buffer,
                                         const char *data,
                                         size_t size)
{
    size_t required;
    size_t capacity;
    char *grown;

    if (!buffer || (!data && size) ||
        buffer->size == SIZE_MAX ||
        size > SIZE_MAX - buffer->size - 1u) {
        return 0;
    }
    required = buffer->size + size + 1u;
    if (required > buffer->limit) return 0;
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 4096u;
        while (capacity < required) {
            if (capacity > buffer->limit / 2u) {
                capacity = buffer->limit;
                break;
            }
            capacity *= 2u;
        }
        if (capacity < required) return 0;
        grown = (char *)realloc(buffer->data, capacity);
        if (!grown) return 0;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (size) memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return 1;
}

#endif
