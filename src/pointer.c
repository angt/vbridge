#include "pointer.h"
#include "common-static.h"

static struct pointer_global {
    Atom atom;
    int warp;
} global;

int
pointer_event(XEvent *event)
{
    if ((global.atom) &&
        (event->type == ClientMessage) &&
        (event->xclient.message_type == global.atom)) {
        global.warp = 1;
        return 1;
    }

    return 0;
}

int
pointer_is_warp(void)
{
    int ret = global.warp;
    global.warp = 0;

    return ret;
}

void
pointer_warp(Window window, int x, int y)
{
    if (!x && !y)
        return;

    if (!global.atom)
        global.atom = XInternAtom(display.id, "_POINTER_WARP_", False);

    XEvent event;
    byte_set(&event, 0, sizeof(event));

    event.type = ClientMessage;
    event.xclient.send_event = True;
    event.xclient.window = window;
    event.xclient.message_type = global.atom;
    event.xclient.format = 32;

    XSendEvent(display.id, window, False, 0, &event);
    XWarpPointer(display.id, None, None, 0, 0, 0, 0, x, y);
    XSync(display.id, False);
}
