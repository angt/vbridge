#pragma once

#include "common.h"

#ifdef COLOR_FORMAT_ABGR
#define COLOR_A_SHIFT  24
#define COLOR_B_SHIFT  16
#define COLOR_G_SHIFT   8
#define COLOR_R_SHIFT   0
#define COLOR_A       255
#else
#define COLOR_A_SHIFT  24
#define COLOR_R_SHIFT  16
#define COLOR_G_SHIFT   8
#define COLOR_B_SHIFT   0
#define COLOR_A         0
#endif

_const_
static inline uint32_t color_rgb (uint32_t r, uint32_t g, uint32_t b)
{
    return (b<<COLOR_B_SHIFT)|(g<<COLOR_G_SHIFT)|(r<<COLOR_R_SHIFT)|(COLOR_A<<COLOR_A_SHIFT);
}

_const_
static inline uint32_t color_argb (uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    return (b<<COLOR_B_SHIFT)|(g<<COLOR_G_SHIFT)|(r<<COLOR_R_SHIFT)|(a<<COLOR_A_SHIFT);
}

_const_
static inline uint32_t color_get_a (uint32_t c) {
    return 0xFF&(c>>COLOR_A_SHIFT);
}

_const_
static inline uint32_t color_get_r (uint32_t c) {
    return 0xFF&(c>>COLOR_R_SHIFT);
}

_const_
static inline uint32_t color_get_g (uint32_t c) {
    return 0xFF&(c>>COLOR_G_SHIFT);
}

_const_
static inline uint32_t color_get_b (uint32_t c) {
    return 0xFF&(c>>COLOR_B_SHIFT);
}
