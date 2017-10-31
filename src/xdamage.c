#include "xdamage.h"

#include <X11/extensions/Xdamage.h>

static struct xdamage_global {
    int use;
    int event;
    Damage damage;
} global;

void
xdamage_init(void)
{
    int xerror;

    if (!XDamageQueryExtension(display.id, &global.event, &xerror)) {
        warning("couldn't query Xdamage extension\n");
        return;
    }

    global.use = 1;

    int major = 1;
    int minor = 1;
    XDamageQueryVersion(display.id, &major, &minor);

    global.damage = XDamageCreate(display.id, display.root,
                                  XDamageReportNonEmpty);
}

int
xdamage_event(XEvent *event)
{
    if (!global.use)
        return 0;

    if (event->type != global.event + XDamageNotify)
        return 0;

    _unused_
    XDamageNotifyEvent *ev = (XDamageNotifyEvent *)event;

    XserverRegion region = XFixesCreateRegion(display.id, 0, 0);
    XDamageSubtract(display.id, global.damage, None, region);

    XFixesDestroyRegion(display.id, region);

    return 1;
}
