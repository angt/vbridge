#include "tga.h"

int
tga_read(const char *filename, image_info_t *image)
{
    if (!image)
        return -1;

    int fd = safe_open(filename, O_RDONLY | O_BINARY);

    if (fd < 0)
        return -1;

    static unsigned char header[18];

    safe_read(fd, header, sizeof(header));

    if (header[0] || header[1] || header[2] != 2 || header[16] != 32) {
        warning("bad tga file\n");
        safe_close(fd);
        return -1;
    }

    int w = header[12] | header[13] << 8;
    int h = header[14] | header[15] << 8;

    char *data = safe_malloc(w * h * 4);

    for (int i = h; i--;)
        safe_read(fd, data + i * w * 4, w * 4);

    safe_close(fd);

    image->data = (uint32_t *)data;
    image->stride = w;
    image->w = w;
    image->h = h;

    return 0;
}

int
tga_write(const char *filename, image_info_t *image)
{
    if (!image)
        return -1;

    int fd = safe_open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);

    if (fd < 0)
        return -1;

    int w = image->w;
    int h = image->h;

    char *data = (char *)image->data;

    static unsigned char header[18];

    header[2] = 2;
    header[12] = w;
    header[13] = w >> 8;
    header[14] = h;
    header[15] = h >> 8;
    header[16] = 32;

    safe_write(fd, header, sizeof(header));

    for (int i = 0; i < w * h; i++)
        data[i * 4 + 3] = '\xff';

    for (int i = h; i--;)
        safe_write(fd, data + i * w * 4, w * 4);

    safe_close(fd);

    return 0;
}
