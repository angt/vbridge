#include "option.h"

#include "display.h"
#include "image.h"

#include "tga.h"

int
main(int argc, char **argv)
{
    char *filename = "img";
    int x = 0;
    int y = 0;
    int width = -1;
    int height = -1;
    int count = 0;

    option(opt_file, &filename, NULL, NULL);
    option(opt_int, &x, "x", "");
    option(opt_int, &y, "y", "");
    option(opt_int, &width, "width", "");
    option(opt_int, &height, "height", "");
    option(opt_int, &count, "count", "");
    option_run(argc, argv);

    display_init();

    int screen = XDefaultScreen(display.id);

    if (width == -1)
        width = XDisplayWidth(display.id, screen);

    if (height == -1)
        height = XDisplayHeight(display.id, screen);

    image_t image;
    image_create(&image, display.root, width, height);

    for (int i = 0; i < count; i++) {
        char name[256];
        snprintf(name, sizeof(name), "%s-%d.tga", filename, i);

        TINI(0);
        image_get(&image, x, y);
        TINI(1);
        XSync(display.id, False);
        TINI(2);

        if (tga_write(name, &image.info))
            error("write error\n");

        print("%s %f %f\n", name, TDIF(0, 1), TDIF(1, 2));

        usleep(20000);
    }

    display_exit();

    return 0;
}
