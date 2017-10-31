#include "ucs_to_keysym.h"
#include "ucs_to_keysym-static.h"

unsigned
ucs_to_keysym(unsigned ucs)
{
    int min = 0;
    int max = sizeof(keysyms) / sizeof(keysyms[0]) - 1;
    int mid;

    if ((ucs == 0) ||
        (ucs >= 0x20 && ucs <= 0x7E) ||
        (ucs >= 0xA0 && ucs <= 0xFF))
        return ucs;

    while (min <= max) {
        mid = (max + min) / 2;

        if (keysyms[mid].ucs < ucs)
            min = mid + 1;
        else if (keysyms[mid].ucs > ucs)
            max = mid - 1;
        else
            return keysyms[mid].keysym;
    }

    if (ucs >= 0x100 && ucs <= 0x10FFFF)
        return ucs | 0x1000000;

    return 0;
}
