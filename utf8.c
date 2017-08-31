#include "utf8.h"
#include "utf8-static.h"

#define MASK(c) (0xFF >> (c))

size_t
utf8_to_utf32(uint32_t *code, const char *str)
{
    if _0_(!str || !code)
        return 0;

    const size_t count = utf8_count(str[0]);

    if _0_(!count)
        return 0;

    uint32_t ret = str[0] & MASK(count);

    for (size_t i = 1; i < count; i++) {
        if (!utf8_chain(str[i]))
            return 0;
        ret <<= 6;
        ret |= str[i] & 0x3F;
    }

    if _0_(utf8_count_utf32(ret) != count)
        return 0;

    *code = ret;

    return count;
}

size_t
utf8_from_utf32(char *str, uint32_t code)
{
    if _0_(!str)
        return 0;

    const size_t count = utf8_count_utf32(code);

    if _0_(!count)
        return 0;

    if _1_(count == 1) {
        str[0] = code;
        return 1;
    }

    for (size_t i = 1; i < count; i++) {
        str[count - i] = 0x80 | (code & 0x3F);
        code >>= 6;
    }

    str[0] = (code & MASK(count)) | ~MASK(count);

    return count;
}
