#pragma once

#include "common.h"

#define TILE_SIZE  8
#define COLOR_MAX 12

typedef struct tycho tycho_t;
typedef struct tycho_state tycho_state_t;
typedef struct tycho_coder tycho_coder_t;
typedef struct tycho_model tycho_model_t;
typedef struct tycho_tiles tycho_tiles_t;
typedef struct tycho_tile  tycho_tile_t;

struct tycho_tile {
    uint32_t hash;
    uint8_t count;
    uint8_t depth;
    uint8_t depth_stop;
    uint8_t depth_step;
    uint8_t color[COLOR_MAX*3];
    uint8_t index[TILE_SIZE*TILE_SIZE];
};

struct tycho_tiles {
    tycho_tile_t *tile;
    unsigned w; // XXX
    unsigned h; // XXX
    unsigned wn;
    unsigned hn;
};

struct tycho_model {
    unsigned bits;
    unsigned mask;
    uint16_t *p;
};

struct tycho_coder {
    uint32_t x;
    uint32_t lim[2];
    int k;
};

struct tycho_state {
    uint8_t count;
    uint8_t i, j, k;
    uint8_t pmax;
};

struct tycho {
    tycho_tiles_t tiles;
    tycho_tiles_t tiles_old;

    unsigned tile;

    uint8_t created;
    uint8_t flush;
    uint8_t redraw;

    tycho_state_t state;

    struct {
        unsigned ctx;
        tycho_model_t model;
    } count, color[3];

    struct {
        tycho_model_t model[COLOR_MAX];
    } index;

    tycho_coder_t coder;
};

void tycho_create       (tycho_t *);
void tycho_delete       (tycho_t *);
void tycho_setup        (tycho_t *, unsigned, unsigned);
void tycho_tiles_create (tycho_tiles_t *, unsigned, unsigned);
void tycho_tiles_delete (tycho_tiles_t *);
int  tycho_tiles_resize (tycho_tiles_t *, unsigned, unsigned);
void tycho_tiles_copy   (tycho_tiles_t *, tycho_tiles_t *);
void tycho_model_create (tycho_model_t *, unsigned, unsigned);
void tycho_model_delete (tycho_model_t *);
