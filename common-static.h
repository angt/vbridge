#pragma once

#include "common.h"

static inline void byte_copy (void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return;

    char *restrict d = dst;
    const char *restrict s = src;

    while (size--)
        *d++ = *s++;
}

static inline void *byte_dup (const char *src, size_t size)
{
    char *ret = safe_malloc(size);
    byte_copy(ret, src, size);
    return ret;
}

static inline void byte_set (void *dst, const char value, size_t size)
{
    if (!dst)
        return;

    char *restrict d = dst;

    while (size--)
        *d++ = value;
}

static inline void byte_set_safe (void *dst, const char value, size_t size)
{
    if (!dst)
        return;

    volatile char *restrict d = dst;

    while (size--)
        *d++ = value;
}

_pure_
static inline int byte_cmp (const void *ba, const void *bb, size_t size)
{
    if (!ba || !bb || !size)
        return 1;

    const char *restrict a = ba;
    const char *restrict b = bb;

    while (size--)
        if (*a++!=*b++)
            return 1;

    return 0;
}

_pure_
static inline int str_empty (const char *restrict str)
{
    return !str || !str[0];
}

_pure_
static inline char *str_skip_space (char *restrict str)
{
    if (!str)
        return NULL;

    while ((*str==' ') || (*str=='\t'))
        str++;

    return str;
}

_pure_
static inline char *str_skip_char_space (char *restrict str)
{
    if (!str)
        return NULL;

    while (((*str>=' ') && (*str<='~')) || (*str<0) || (*str=='\t'))
        str++;

    return str;
}

_pure_
static inline char *str_skip_line (char *restrict str)
{
    if (!str)
        return NULL;

    while (*str && (*str++!='\n'));

    return str;
}

_pure_
static inline char *str_skip_char (char *restrict str)
{
    if (!str)
        return NULL;

    while (((*str>' ') && (*str<='~')) || (*str<0))
        str++;

    return str;
}

_pure_
static inline size_t str_find (const char *restrict str, const char c)
{
    if (!str)
        return 0;

    size_t i = 0;

    while (str[i] && str[i]!=c)
        i++;

    return i;
}

_pure_
static inline size_t str_len (const char *restrict str)
{
    return str_find(str, 0);
}

_pure_
static inline int str_contains (const char *restrict str, const char c)
{
    return str?(str[str_find(str, c)]==c):0;
}

_pure_
static inline int str_cmp (const char *restrict sa, const char *restrict sb)
{
    if (!sa || !sb)
        return 1;

     while (*sa==*sb++)
         if (!*sa++)
             return 0;

    return 1;
}
