#include "tycho-client.h"
#include "color-static.h"
#include "tycho-static.h"

static int
buffer_read_tile(tycho_t *const restrict tycho,
                 buffer_t *const restrict buffer,
                 tycho_tile_t *const restrict tile)

{
    tycho_state_t st = tycho->state;

    if (!st.count) {
        uint8_t count;
        if (decode(&tycho->coder, buffer, &tycho->count.model, &count, 4, tycho->count.ctx))
            goto save_state;
        tycho->count.ctx = ((tycho->count.ctx << 4) | count) & 0xFFF;
        if (!count)
            return -1;
        tile->count = count;
        byte_set(&st, 0, sizeof(st));
        st.i = 1;
        st.count = count;
    }

    for (; st.k < st.count * 3; st.k++) {
        uint8_t c;
        if (decode(&tycho->coder, buffer, &tycho->color[st.k % 3].model, &c, 8, tycho->color[st.k % 3].ctx))
            goto save_state;
        tycho->color[st.k % 3].ctx = c;
        tile->color[st.k] = c;
    }

    if _1_(st.count > 1) {
        static const int lg[] = {1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4};
        tycho_model_t *const restrict model = tycho->index.model;
        for (; st.j < TILE_SIZE; st.j++) {
            for (; st.i < TILE_SIZE; st.i++) {
                uint8_t p;
                uint32_t ctx = 0;
                if (st.i)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - 1];
                if (st.j)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - TILE_SIZE] << 4;
                if (st.i && st.j)
                    ctx |= tile->index[st.j * TILE_SIZE + st.i - TILE_SIZE - 1] << 8;
                if (decode(&tycho->coder, buffer, &model[st.pmax], &p, lg[st.pmax], ctx))
                    goto save_state;
                tile->index[st.j * TILE_SIZE + st.i] = p;
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

static void
draw_tile(tycho_tile_t *const restrict tile,
          image_info_t *const restrict image)
{
    if (!tile->count)
        return;

    uint32_t colormap[COLOR_MAX];

    for (unsigned k = 0; k < tile->count; k++) {
        const int32_t y = tile->color[k * 3 + 0];
        const int32_t u = tile->color[k * 3 + 1];
        const int32_t v = tile->color[k * 3 + 2];
        const int32_t r = CLAMP((v << 1) + y - 255, 0, 255);
        const int32_t b = CLAMP((u << 1) + y - 255, 0, 255);
        const int32_t g = CLAMP(y - v - u + 255, 0, 255);
        colormap[k] = color_rgb(r, g, b);
    }

    if (tile->count > 1) {
        for (int j = 0; j < image->h; j++) {
            for (int i = 0; i < image->w; i++) {
                uint8_t p = tile->index[j * TILE_SIZE + i];
                image->data[j * image->stride + i] = colormap[p];
            }
        }
    } else {
        for (int j = 0; j < image->h; j++)
            for (int i = 0; i < image->w; i++)
                image->data[j * image->stride + i] = colormap[0];
    }
}

int
tycho_recv(tycho_t *tycho, buffer_t *buffer, image_info_t *image)
{
    const unsigned wn = tycho->tiles.wn;
    const unsigned hn = tycho->tiles.hn;

    const unsigned w = image->w;
    const unsigned h = image->h;

    if (tycho->flush) {
        if (decoder_flush(&tycho->coder, buffer))
            return 1;
        tycho->flush = 0;
    }

    for (unsigned j = tycho->tile / wn; j < hn; j++) {
        for (unsigned i = tycho->tile % wn; i < wn; i++) {
            int ret = buffer_read_tile(tycho, buffer, &tycho->tiles.tile[tycho->tile]);

            if (ret == 1)
                return 1;

            if (ret == 0 || tycho->redraw) {
                image_info_t tile_image = {
                    .data = &image->data[(j * image->stride + i) * TILE_SIZE],
                    .w = _1_(i != w / TILE_SIZE) ? TILE_SIZE : w % TILE_SIZE,
                    .h = _1_(j != h / TILE_SIZE) ? TILE_SIZE : h % TILE_SIZE,
                    .stride = image->stride,
                };
                draw_tile(&tycho->tiles.tile[tycho->tile], &tile_image);
            }

            tycho->tile++;
        }
    }

    return 0;
}
