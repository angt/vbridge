#include "display.h"
#include "color-static.h"
#include "common-static.h"

struct display_extern display;

static int
display_error()
{
    display.error = 1;
    return 0;
}

int
display_init(void)
{
    byte_set(&display, 0, sizeof(struct display_extern));

    display.id = XOpenDisplay(NULL);

    if (!display.id)
        error("couldn't init display\n");

    XSetErrorHandler(display_error);
    XSetIOErrorHandler(display_error);

    int screen = DefaultScreen(display.id);
    int depth = DefaultDepth(display.id, screen);

    if (depth < 24)
        error("default depth is not supported\n");

    Visual *visual = DefaultVisual(display.id, screen);

    if ((visual->red_mask != color_rgb(255, 0, 0)) ||
        (visual->green_mask != color_rgb(0, 255, 0)) ||
        (visual->blue_mask != color_rgb(0, 0, 255)))
        error("default visual is not supported\n");

    int xerror;

    if (!XFixesQueryExtension(display.id, &display.xfixes_event, &xerror))
        error("couldn't query XFixes extension\n");

    display.visual = visual;
    display.depth = depth;
    display.root = RootWindow(display.id, screen);

    return ConnectionNumber(display.id);
}

void
display_exit()
{
    if (display.id)
        XCloseDisplay(display.id);
}
