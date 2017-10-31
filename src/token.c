#include "token.h"
#include "buffer-static.h"

int
token_recv(buffer_t *dst, buffer_t *src)
{
    if (!dst->data) {
        if (buffer_read_size(src) < 4)
            return 1;

        const size_t size = buffer_read_32(src);

        if (!SIZE_OK(size) || size > CONFIG_BUFFER_SIZE)
            return 0;

        buffer_setup(dst, safe_calloc(size + 1, 1), size);
    }

    buffer_copy(dst, src);

    if (buffer_write_size(dst))
        return 1;

    return 0;
}

int
token_send(buffer_t *dst, buffer_t *src)
{
    if (src->data == src->read) {
        if (buffer_write_size(dst) < 4 + 1)
            return 1;

        buffer_write_32(dst, buffer_read_size(src));
    }

    buffer_copy(dst, src);

    if (buffer_read_size(src))
        return 1;

    safe_free(src->data);

    byte_set(src, 0, sizeof(buffer_t));

    return 0;
}
