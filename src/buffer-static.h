#pragma once

#include "common-static.h"

static inline void buffer_setup (buffer_t *buffer, void *data, size_t size)
{
    if (!data)
        data = safe_malloc(ALIGN(size));

    buffer->data  = data;
    buffer->write = data;
    buffer->read  = data;
    buffer->end   = data;
    buffer->end  += size;
}

static inline void buffer_format (buffer_t *buffer)
{
    buffer->write = buffer->data;
    buffer->read  = buffer->data;
}

_pure_
static inline size_t buffer_size (buffer_t *buffer)
{
    return buffer->end-buffer->data;
}

_pure_
static inline size_t buffer_write_size (buffer_t *buffer)
{
    return buffer->end-buffer->write;
}

_pure_
static inline size_t buffer_read_size (buffer_t *buffer)
{
    return buffer->write-buffer->read;
}

static inline uint8_t buffer_read (buffer_t *buffer)
{
    assert(buffer_read_size(buffer)>=1);

    const uint8_t *const restrict r = buffer->read;
    const uint8_t r0 = r[0];
    buffer->read += 1;
    return r0;
}

static inline uint16_t buffer_read_16 (buffer_t *buffer)
{
    assert(buffer_read_size(buffer)>=2);

    const uint8_t *const restrict r = buffer->read;
    const uint16_t r0 = r[0];
    const uint16_t r1 = r[1];
    buffer->read += 2;
    return (r0<<8)|r1;
}

static inline uint32_t buffer_read_32 (buffer_t *buffer)
{
    assert(buffer_read_size(buffer)>=4);

    const uint8_t *const restrict r = buffer->read;
    const uint32_t r0 = r[0];
    const uint32_t r1 = r[1];
    const uint32_t r2 = r[2];
    const uint32_t r3 = r[3];
    buffer->read += 4;
    return (r0<<24)|(r1<<16)|(r2<<8)|r3;
}

static inline void buffer_write (buffer_t *buffer, const uint8_t value)
{
    assert(buffer_write_size(buffer)>=1);

    uint8_t *const restrict w = buffer->write;
    w[0] = value;
    buffer->write += 1;
}

static inline void buffer_write_16 (buffer_t *buffer, const uint16_t value)
{
    assert(buffer_write_size(buffer)>=2);

    uint8_t *const restrict w = buffer->write;
    w[0] = value>>8;
    w[1] = value;
    buffer->write += 2;
}

static inline void buffer_write_32 (buffer_t *buffer, const uint32_t value)
{
    assert(buffer_write_size(buffer)>=4);

    uint8_t *const restrict w = buffer->write;
    w[0] = value>>24;
    w[1] = value>>16;
    w[2] = value>>8;
    w[3] = value;
    buffer->write += 4;
}

static inline void buffer_read_data (buffer_t *buffer, void *data, const size_t size)
{
    assert(buffer_read_size(buffer)>=size);

    byte_copy(data, buffer->read, size);
    buffer->read += size;
}

static inline void buffer_write_data (buffer_t *buffer, const void *data, const size_t size)
{
    assert(buffer_write_size(buffer)>=size);

    byte_copy(buffer->write, data, size);
    buffer->write += size;
}

static inline void buffer_copy (buffer_t *dst, buffer_t *src)
{
    const size_t src_size = buffer_read_size(src);
    const size_t dst_size = buffer_write_size(dst);
    const size_t size = MIN(src_size, dst_size);
    byte_copy(dst->write, src->read, size);
    dst->write += size;
    src->read += size;
}

static inline void buffer_from_string (buffer_t *buffer, void *str)
{
    const size_t size = str_len(str)+1;
    buffer_setup(buffer, NULL, size);
    buffer_write_data(buffer, str, size);
}

static inline void buffer_shift (buffer_t *buffer)
{
    if (buffer->read==buffer->write) {
        buffer_format(buffer);
    } else {
        const uint8_t *src = PALIGN_DOWN(buffer->read);
        const size_t size = ALIGN(buffer->write-src);
        if (buffer->data+size<src) {
            byte_copy(buffer->data, src, size);
            buffer->read  -= src-buffer->data;
            buffer->write -= src-buffer->data;
        }
    }
}
