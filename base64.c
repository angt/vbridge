#include "base64.h"
#include "buffer-static.h"

static struct base64_global {
    uint8_t *map, *unmap;
} global = {
    .map = (uint8_t *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz"
                      "0123456789+/"
};

void
base64_init(void)
{
    if (global.unmap)
        return;

    global.unmap = safe_calloc(1, 256);

    for (uint8_t i = 0; i < 64; i++)
        global.unmap[global.map[i]] = i;
}

static void
encode(uint8_t *const w, const size_t ws,
       const uint8_t *const r, const size_t rs)
{
    const uint8_t c0 = rs > 0 ? r[0] : 0;
    const uint8_t c1 = rs > 1 ? r[1] : 0;
    const uint8_t c2 = rs > 2 ? r[2] : 0;

    if (ws > 0) w[0] = global.map[c0 >> 2];
    if (ws > 1) w[1] = global.map[0x3F & ((c0 << 4) | (c1 >> 4))];
    if (ws > 2) w[2] = global.map[0x3F & ((c1 << 2) | (c2 >> 6))];
    if (ws > 3) w[3] = global.map[0x3F & c2];
}

void
base64_encode(buffer_t *dst, buffer_t *src)
{
    base64_init();

    uint8_t *w = dst->write;
    uint8_t *r = src->read;

    const size_t count = MIN(buffer_read_size(src) / 3,
                             buffer_write_size(dst) / 4);

    for (size_t i = 0; i < count; w += 4, r += 3, i++)
        encode(w, 4, r, 3);

    const size_t ws = dst->end - w;
    const size_t rs = src->write - r;

    if (rs && ws > rs) {
        encode(w, ws, r, rs);
        r += rs;
        w += rs + 1;
    }

    dst->write = w;
    src->read = r;
}

static void
decode(uint8_t *const w, const size_t ws,
       const uint8_t *const r, const size_t rs)
{
    const uint8_t c0 = rs > 0 ? global.unmap[r[0]] : 0;
    const uint8_t c1 = rs > 1 ? global.unmap[r[1]] : 0;
    const uint8_t c2 = rs > 2 ? global.unmap[r[2]] : 0;
    const uint8_t c3 = rs > 3 ? global.unmap[r[3]] : 0;

    if (ws > 0) w[0] = (c0 << 2) | (c1 >> 4);
    if (ws > 1) w[1] = (c1 << 4) | (c2 >> 2);
    if (ws > 2) w[2] = (c2 << 6) | (c3);
}

void
base64_decode(buffer_t *dst, buffer_t *src)
{
    base64_init();

    uint8_t *w = dst->write;
    uint8_t *r = src->read;

    const size_t count = MIN(buffer_read_size(src) / 4,
                             buffer_write_size(dst) / 3);

    for (size_t i = 0; i < count; w += 3, r += 4, i++)
        decode(w, 3, r, 4);

    const size_t ws = dst->end - w;
    const size_t rs = src->write - r;

    if (rs > 1 && ws >= rs - 1) {
        decode(w, ws, r, rs);
        r += rs;
        w += rs - 1;
    }

    dst->write = w;
    src->read = r;
}
