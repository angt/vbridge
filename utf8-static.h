#pragma once

#include "common.h"

_const_
static inline int utf8_chain (const char c)
{
    return (c&0xC0)==0x80;
}

_const_
static inline size_t utf8_count_utf32 (const uint32_t c)
{
    if _1_(!(c>>7))
        return 1;

    if _1_(c<0x800)
        return 2;

    if _1_(c<0xD800)
        return 3;

    if _0_(c<0xE000)
        return 0;

    if _1_(c<=0x10FFFF)
        return 4;

    return 0;
}

_const_
static inline size_t utf8_count (const char c)
{
    const uint32_t uc = c&0xFF;

    if _1_(!(uc>>7))
        return 1;

    if _0_(uc<=0xC1)
        return 0;

    if _0_(uc>=0xF5)
        return 0;

    return CLZ(~(uc<<24));
}

_pure_
static inline size_t utf8_prev (const char *restrict str, size_t i)
{
    if (!str)
        return i;

    while (i>0 && utf8_chain(str[--i]));

    return i;
}

_pure_
static inline int utf8_check (const char *restrict str, const size_t i)
{
    if (!str)
        return 0;

    for (size_t j=1; j<=i; j++)
        if (!utf8_chain(str[j]))
            return 0;

    return 1;
}

_pure_
static inline size_t utf8_len (const char *restrict str)
{
    if (!str)
        return 0;

    size_t i = 0;

    while (*str) {
        if (!utf8_chain(*str))
            i++;
        str++;
    }

    return i;
}
