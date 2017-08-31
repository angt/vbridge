#pragma once

#include "display.h"

#include <X11/extensions/XShm.h>

typedef struct image image_t;

struct image {
    image_info_t info;
    XImage *id;
    XShmSegmentInfo shm;
    Drawable drawable;
    GC gc;
};

void image_create (image_t *, Drawable, int, int);
void image_delete (image_t *);
void image_get    (image_t *, int, int);
void image_put    (image_t *, int, int, int, int, int, int);
