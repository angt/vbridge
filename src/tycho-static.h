#pragma once

#include "tycho.h"
#include "buffer-static.h"

static inline void update_lim (tycho_coder_t *const restrict coder)
{
    coder->lim[0]<<=8;
    coder->lim[1] = (coder->lim[1]<<8)|255;
}

_pure_
static inline uint32_t get_mid (const tycho_coder_t *const restrict coder, const uint32_t p)
{
    const uint32_t d = coder->lim[1]-coder->lim[0];
    return coder->lim[0]+(d>>16)*p; //+(((d&((1<<16)-1))*p)>>16);
}

_const_
static inline int32_t update_p (int32_t p, int32_t bit)
{
    return p+(((bit<<16)-p)>>6);
}

static inline void coder_setup (tycho_coder_t *coder)
{
    coder->lim[0] = 0;
    coder->lim[1] = ~0;
    coder->k = 1;
}

static inline int encoder_flush (tycho_coder_t *coder, buffer_t *buffer)
{
    if (buffer_write_size(buffer)<4)
        return 1;

    buffer_write_32(buffer, coder->lim[1]);

    return 0;
}

static inline int decoder_flush (tycho_coder_t *coder, buffer_t *buffer)
{
    if (buffer_read_size(buffer)<4)
        return 1;

    coder->x = buffer_read_32(buffer);

    return 0;
}

static inline int decode (tycho_coder_t *const restrict coder,
                          buffer_t *const restrict buffer,
                          tycho_model_t *const restrict model,
                          uint8_t *const restrict byte,
                          const int min,
                          const uint32_t ctx)
{
    uint16_t *const restrict p = &model->p[ctx<<model->bits];
    int k = coder->k;

    for (int i=min+CLZ(k)-32; i>=0; --i) {
        while _0_((coder->lim[1]^coder->lim[0])<(1<<24)) {
            if _0_(buffer_read_size(buffer)<1) {
                coder->k = k;
                return 1;
            }
            update_lim(coder);
            coder->x = (coder->x<<8)|buffer_read(buffer);
        }

        const uint32_t pk = p[k];
        const uint32_t mid = get_mid(coder, pk);
        const int32_t bit = coder->x<=mid;

        coder->lim[bit] = mid+(1^bit);
        p[k] = update_p(pk, bit);
        k = (k<<1)|bit;
    }

    *byte = k&((1<<min)-1);

    coder->k = 1;

    return 0;
}

static inline int encode (tycho_coder_t *const restrict coder,
                          buffer_t *const restrict buffer,
                          tycho_model_t *const restrict model,
                          const uint8_t byte,
                          const int min,
                          const uint32_t ctx)
{
    uint16_t *const restrict p = &model->p[ctx<<model->bits];
    int k = coder->k;

    for (int i=min+CLZ(k)-32; i>=0; --i) {
        while _0_((coder->lim[1]^coder->lim[0])<(1<<24)) {
            if _0_(buffer_write_size(buffer)<1) {
                coder->k = k;
                return 1;
            }
            buffer_write(buffer, coder->lim[1]>>24);
            update_lim(coder);
        }

        const uint32_t pk = p[k];
        const uint32_t mid = get_mid(coder, pk);
        const int32_t bit = (byte>>i)&1;

        coder->lim[bit] = mid+(1^bit);
        p[k] = update_p(pk, bit);
        k = (k<<1)|bit;
    }

    coder->k = 1;

    return 0;
}
