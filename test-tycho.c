#include "buffer-static.h"
#include "option.h"
#include "tga.h"
#include "tycho-client.h"
#include "tycho-server.h"

static uint32_t
pcg32(void)
{
    static uint64_t s = 159;
    const uint32_t a = ((s >> 18u) ^ s) >> 27u;
    const uint32_t b = s >> 59u;
    s = s * 6364136223846793005ULL + 15726070495360670683ULL;
    return (a >> b) | (a << ((-b) & 31));
}

static int
image_random(image_info_t *image, _unused_ int i)
{
    if (!image->data) {
        image->data = safe_malloc(image->w * image->h * 4);
        image->stride = image->w;
    }

    for (int j = 0; j < image->h; j++) {
        for (int i = 0; i < image->w; i++)
            image->data[j * image->stride + i] = pcg32() & 0xFFFFFF;
    }

    return 0;
}

static int
image_tga(image_info_t *image, const char *filename, int i)
{
    safe_free(image->data);

    byte_set(image, 0, sizeof(image_info_t));

    char name[256];
    snprintf(name, sizeof(name), "%s-%d.tga", filename, i);

    struct stat st;

    if (stat(name, &st))
        return 1;

    if (tga_read(name, image))
        error("tga read error\n");

    return 0;
}

int
main(int argc, char **argv)
{
    char *filename = NULL;

    image_info_t image = {
        .w = 1920,
        .h = 1080,
    };

    int count = 1000;
    size_t size = 8 * 1024 * 1024;

    unsigned min = CONFIG_QUALITY_MIN;
    unsigned max = CONFIG_QUALITY_MAX;

    int dont_decode = 0;
    char *dump = NULL;

    // tga
    option(opt_file, &filename, NULL, NULL);

    //rand
    option(opt_int, &image.w, "width", "");
    option(opt_int, &image.h, "height", "");

    option(opt_int, &count, "count", "");

    option(opt_int, &min, "quality-min", "");
    option(opt_int, &max, "quality-max", "");

    option(opt_flag, &dont_decode, "dont-decode", "");
    option(opt_file, &dump, "dump", "");
    option(opt_int, &size, "size", "");

    option_run(argc, argv);

    tycho_set_quality(min, max);

    buffer_t buffer;
    buffer_setup(&buffer, safe_malloc(size), size);

    tycho_t tycho_encode;
    byte_set(&tycho_encode, 0, sizeof(tycho_t));

    tycho_t tycho_decode;
    byte_set(&tycho_decode, 0, sizeof(tycho_t));

    int dumpfd = safe_open(dump, O_CREAT | O_TRUNC | O_WRONLY, 0640);

    int progress = isatty(1) && !isatty(2);

    for (int i = 0; i < count; i++) {
        if (progress)
            print(" %i\r", i);

        int ret = filename ? image_tga(&image, filename, i)
                           : image_random(&image, i);

        if (ret)
            break;

        TINI(0);

        int update = tycho_set_image(&image);

        TINI(1);

        if (update) {
            tycho_setup_server(&tycho_encode);
            tycho_send(&tycho_encode, &buffer);
        }

        size_t size = buffer_read_size(&buffer);

        if (size && dumpfd != -1) {
            if (safe_write(dumpfd, buffer.read, size) != size)
                warning("dump write error\n");
        }

        TINI(2);

        if (update && !dont_decode) {
            tycho_setup(&tycho_decode, image.w, image.h);
            tycho_recv(&tycho_decode, &buffer, &image);
        }

        TINI(3);

        if (!dont_decode && buffer_read_size(&buffer))
            error("decode error\n");

        double time01 = TDIF(0, 1);
        double time12 = TDIF(1, 2);
        double time23 = TDIF(2, 3);

        info("%i %f %f %f %f %i\n", update, time01, time12, time01 + time12, time23, size);

        buffer_format(&buffer);
    }

    safe_close(dumpfd);

    return 0;
}
