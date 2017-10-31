#include "tycho.h"
#include "common-static.h"
#include "tycho-static.h"

void
tycho_tiles_create(tycho_tiles_t *tiles, unsigned w, unsigned h)
{
    if (!tiles)
        return;

    const unsigned wn = DIV(w, TILE_SIZE);
    const unsigned hn = DIV(h, TILE_SIZE);

    tiles->tile = safe_calloc(wn * hn, sizeof(tycho_tile_t));

    tiles->w = w;
    tiles->h = h;

    tiles->wn = wn;
    tiles->hn = hn;
}

void
tycho_tiles_delete(tycho_tiles_t *tiles)
{
    if (!tiles)
        return;

    safe_free(tiles->tile);

    byte_set(tiles, 0, sizeof(tycho_tiles_t));
}

int
tycho_tiles_resize(tycho_tiles_t *tiles, unsigned w, unsigned h)
{
    if (!tiles)
        return 0;

    if (w == tiles->w && h == tiles->h)
        return 0;

    const unsigned wn = DIV(w, TILE_SIZE);
    const unsigned hn = DIV(h, TILE_SIZE);

    if (wn == tiles->wn && hn == tiles->hn) {
        tiles->w = w;
        tiles->h = h;
        return 1;
    }

    const tycho_tiles_t old = *tiles;
    tycho_tiles_create(tiles, w, h);

    const unsigned jj = MIN(hn, old.hn);
    const unsigned ii = MIN(wn, old.wn);

    for (unsigned j = 0; j < jj; j++)
        for (unsigned i = 0; i < ii; i++)
            tiles->tile[j * wn + i] = old.tile[j * old.wn + i];

    safe_free(old.tile);

    return 1;
}

void
tycho_tiles_copy(tycho_tiles_t *dst, tycho_tiles_t *src)
{
    if (!dst || !src)
        return;

    const unsigned wn = src->wn;
    const unsigned hn = src->hn;

    if (wn != dst->wn || hn != dst->hn) {
        safe_free(dst->tile);
        dst->tile = safe_malloc(wn * hn * sizeof(tycho_tile_t));
        dst->wn = wn;
        dst->hn = hn;
    }

    dst->w = src->w;
    dst->h = src->h;

    byte_copy(dst->tile, src->tile, wn * hn * sizeof(tycho_tile_t));
}

void
tycho_model_create(tycho_model_t *model, unsigned bits, unsigned mask)
{
    if (!model)
        return;

    const unsigned count = (1 << bits) * (mask + 1);

    model->p = safe_malloc(sizeof(uint16_t) * count);

    for (unsigned i = 0; i < count; i++)
        model->p[i] = 1u << 15;

    model->bits = bits;
    model->mask = mask;
}

void
tycho_model_delete(tycho_model_t *model)
{
    if (!model)
        return;

    safe_free(model->p);

    byte_set(model, 0, sizeof(tycho_model_t));
}

void
tycho_setup(tycho_t *tycho, unsigned w, unsigned h)
{
    if (!tycho)
        return;

    if (!tycho->created)
        tycho_create(tycho);

    coder_setup(&tycho->coder);

    tycho->flush = 1;
    tycho->tile = 0;
    tycho->redraw = tycho_tiles_resize(&tycho->tiles, w, h);
}

void
tycho_create(tycho_t *tycho)
{
    if (!tycho)
        return;

    tycho_model_create(&tycho->count.model, 4, 0xFFF);

    for (size_t i = 0; i < COUNT(tycho->color); i++)
        tycho_model_create(&tycho->color[i].model, 8, 0xFF);

    for (size_t i = 0; i < COUNT(tycho->index.model); i++)
        tycho_model_create(&tycho->index.model[i], 4, 0xFFF);

    tycho->created = 1;
}

void
tycho_delete(tycho_t *tycho)
{
    if (!tycho)
        return;

    tycho_tiles_delete(&tycho->tiles);
    tycho_tiles_delete(&tycho->tiles_old);

    tycho_model_delete(&tycho->count.model);

    for (size_t i = 0; i < COUNT(tycho->color); i++)
        tycho_model_delete(&tycho->color[i].model);

    for (size_t i = 0; i < COUNT(tycho->index.model); i++)
        tycho_model_delete(&tycho->index.model[i]);

    byte_set(tycho, 0, sizeof(tycho_t));
}
