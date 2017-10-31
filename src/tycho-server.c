#include "tycho-server.h"
#include "tycho-static.h"
#include "color-static.h"

typedef struct cmap cmap_t;

struct cmap {
    uint32_t h, r, g, b;
};

static struct tycho_server_global {
    tycho_tiles_t tiles;
    struct {
        unsigned min;
        unsigned max;
    } quality;
} global;

static unsigned
build_cmap(cmap_t *const restrict cmap,
           const uint32_t *const restrict data,
           uint8_t *const restrict index,
           const unsigned depth)
{
    for (unsigned k = 0; k < COLOR_MAX; k++)
        cmap[k] = (cmap_t){0, 0, 0, 0};

    const uint32_t p = (0xFF >> (8 - depth)) << (8 - depth);
    const uint32_t mask = p | (p << 8) | (p << 16);

    unsigned count = 0;

    for (unsigned j = 0; j < TILE_SIZE; j++) {
        for (unsigned i = 0; i < TILE_SIZE; i++) {
            uint32_t c = data[j * TILE_SIZE + i];
            const uint32_t r = color_get_r(c);
            const uint32_t g = color_get_g(c);
            const uint32_t b = color_get_b(c);
            const uint32_t y = (r + (g << 1) + b) >> 2;
            const uint32_t u = (b + (255 - y)) >> 1;
            const uint32_t v = (r + (255 - y)) >> 1;
            c = color_rgb(y, u, v);
            const uint32_t h = c & mask;
            unsigned k = 0;
            for (; k < count; k++) {
                if (((cmap[k].h & mask) == h))
                    break;
            }
            if _0_(k == count) {
                if _0_(k == COLOR_MAX)
                    return 0;
                cmap[k].h = c & 0xFFFFFF;
                count++;
            }
            cmap[k].h += 1 << 24;
            cmap[k].r += y;
            cmap[k].g += u;
            cmap[k].b += v;
            index[j * TILE_SIZE + i] = k;
        }
    }

    return count;
}

static int
tile_write(tycho_tile_t *const restrict tile,
           const image_info_t *const restrict image)
{
    uint32_t hash = 0;
    uint32_t tile_data[TILE_SIZE * TILE_SIZE];

    const unsigned w = image->w;
    const unsigned h = image->h;

    for (unsigned j = 0; j < TILE_SIZE; j++) {
        for (unsigned i = 0; i < TILE_SIZE; i++) {
            const uint32_t c = _1_(j < h && i < w) ? image->data[j * image->stride + i] : 0;
            tile_data[j * TILE_SIZE + i] = c;
            hash = (hash << 5) - hash + c;
        }
    }

    hash |= 1;

    unsigned count, depth;

    if (tile->hash == hash) {
        if (tile->depth_stop)
            return 0;
        if (tile->depth_step < tile->depth - global.quality.min + 1) {
            tile->depth_step++;
            return 0;
        }
        depth = tile->depth + 1;
    } else {
        tile->depth_stop = 0;
        depth = global.quality.min;
    }

    cmap_t cmap[COLOR_MAX];
    uint8_t index[TILE_SIZE * TILE_SIZE];

    for (;;) {
        count = build_cmap(cmap, tile_data, index, depth);
        if (count)
            break;
        tile->depth_stop = 1;
        if (hash == tile->hash)
            return 0;
        depth--;
    }

    for (unsigned k = 0; k < count; k++) {
        const uint32_t n = cmap[k].h >> 24;
        tile->color[k * 3 + 0] = cmap[k].r / n;
        tile->color[k * 3 + 1] = cmap[k].g / n;
        tile->color[k * 3 + 2] = cmap[k].b / n;
    }

    if (count > 1) {
        for (unsigned i = 0; i < TILE_SIZE * TILE_SIZE; i++)
            tile->index[i] = index[i];
    }

    if (depth == global.quality.max)
        tile->depth_stop = 1;

    tile->depth = depth;
    tile->depth_step = 0;
    tile->hash = hash;
    tile->count = count;

    return 1;
}

int
tycho_set_image(image_info_t *image)
{
    const unsigned w = image->w;
    const unsigned h = image->h;

    tycho_tiles_resize(&global.tiles, w, h);

    const unsigned wn = global.tiles.wn;
    const unsigned hn = global.tiles.hn;

    unsigned tile = 0;
    int ret = 0;

    for (unsigned j = 0; j < hn; j++) {
        for (unsigned i = 0; i < wn; i++) {
            image_info_t tile_image = {
                .data = &image->data[(j * image->stride + i) * TILE_SIZE],
                .w = _1_(i != w / TILE_SIZE) ? TILE_SIZE : w % TILE_SIZE,
                .h = _1_(j != h / TILE_SIZE) ? TILE_SIZE : h % TILE_SIZE,
                .stride = image->stride,
            };
            ret += tile_write(&global.tiles.tile[tile], &tile_image);
            tile++;
        }
    }

    return ret;
}

void
tycho_set_quality(unsigned min, unsigned max)
{
    if (max > 8)
        max = 8;

    if (min > max)
        min = max;

    global.quality.min = min;
    global.quality.max = max;
}

_pure_ static int
tile_are_equal(const tycho_tile_t *const restrict ta,
               const tycho_tile_t *const restrict tb)
{
    if (ta->count != tb->count)
        return 0;

    for (unsigned k = 0; k < ta->count * 3; k++)
        if (ta->color[k] != tb->color[k])
            return 0;

    if (ta->count <= 1)
        return 1;

    for (unsigned i = 0; i < TILE_SIZE * TILE_SIZE; i++)
        if (ta->index[i] != tb->index[i])
            return 0;

    return 1;
}

static int
buffer_write_tile(tycho_t *const restrict tycho,
                  buffer_t *const restrict buffer,
                  tycho_tile_t *const restrict tile,
                  tycho_tile_t *const restrict tile_old)
{
    tycho_state_t st = tycho->state;

    if (!st.count) {
        uint8_t count = tile->count;
        if (tile_are_equal(tile, tile_old))
            count = 0;
        if (encode(&tycho->coder, buffer, &tycho->count.model, count, 4, tycho->count.ctx))
            goto save_state;
        tycho->count.ctx = ((tycho->count.ctx << 4) | count) & 0xFFF;
        if (!count)
            return -1;
        byte_set(&st, 0, sizeof(st));
        st.i = 1;
        st.count = count;
    }

    for (; st.k < st.count * 3; st.k++) {
        uint8_t c = tile->color[st.k];
        if (encode(&tycho->coder, buffer, &tycho->color[st.k % 3].model, c, 8, tycho->color[st.k % 3].ctx))
            goto save_state;
        tycho->color[st.k % 3].ctx = c;
    }

    if _1_(st.count > 1) {
        static const int lg[] = {1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4};
        tycho_model_t *const restrict model = tycho->index.model;
        for (; st.j < TILE_SIZE; st.j++) {
            for (; st.i < TILE_SIZE; st.i++) {
                const uint8_t p = tile->index[st.j * TILE_SIZE + st.i];
                uint32_t ctx = 0;
                if (st.i)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - 1];
                if (st.j)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - TILE_SIZE] << 4;
                if (st.i && st.j)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - TILE_SIZE - 1] << 8;
                if (encode(&tycho->coder, buffer, &model[st.pmax], p, lg[st.pmax], ctx))
                    goto save_state;
                if (st.pmax < p)
                    st.pmax = p;
            }
            st.i = 0;
        }
    }

    tycho->state.count = 0;
    return 0;

save_state:
    tycho->state = st;
    return 1;
}

void
tycho_setup_server(tycho_t *tycho)
{
    tycho_setup(tycho, global.tiles.w, global.tiles.h);

    tycho_tiles_t tmp = tycho->tiles;
    tycho->tiles = tycho->tiles_old;
    tycho->tiles_old = tmp;

    tycho_tiles_copy(&tycho->tiles, &global.tiles);
}

int
tycho_send(tycho_t *tycho, buffer_t *buffer)
{
    const size_t count = tycho->tiles.wn * tycho->tiles.hn;

    for (; tycho->tile < count; tycho->tile++) {
        int ret = buffer_write_tile(tycho, buffer,
                                    &tycho->tiles.tile[tycho->tile],
                                    &tycho->tiles_old.tile[tycho->tile]);
        if (ret == 1)
            return 1;
    }

    if (tycho->flush) {
        if (encoder_flush(&tycho->coder, buffer))
            return 1;
        tycho->flush = 0;
    }

    return 0;
}
