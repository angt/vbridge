#include "xrandr.h"

#include <X11/extensions/Xrandr.h>

static struct xrandr_global {
    int use;
    int event;
    struct {
        int w, h;
    } min, max, last;
} global;

void
xrandr_init(void)
{
    int xerror;

    if (!XRRQueryExtension(display.id, &global.event, &xerror)) {
        warning("couldn't query XRandR extension\n");
        return;
    }

    global.use = 1;

#if RANDR_MINOR < 2
    XRRSelectInput(display.id, display.root, RRScreenChangeNotifyMask);
#else
    XRRGetScreenSizeRange(display.id, display.root,
                          &global.min.w, &global.min.h,
                          &global.max.w, &global.max.h);

    XRRSelectInput(display.id, display.root,
                   RRScreenChangeNotifyMask | RROutputChangeNotifyMask);
#endif
}

static int
ptm(int pixels)
{
    const double ret = (pixels * 25.4) / 96.0 + 0.5;
    return (int)ret;
}

int
xrandr_event(XEvent *event)
{
    if (!global.use)
        return 0;

    int ret = XRRUpdateConfiguration(event);

    if (event->type - global.event == RRScreenChangeNotify) {
        _unused_ XRRScreenChangeNotifyEvent *ev = (XRRScreenChangeNotifyEvent *)event;
        debug("XRandR event window=%d root=%d size_index=%d rotation=%d width=%d height=%d\n",
              ev->window, ev->root, ev->size_index, ev->rotation, ev->width, ev->height);
    }

    return ret;
}

int
xrandr_resize(int w, int h)
{
#if RANDR_MINOR < 2
    return 0;
#else
    if (!global.use || !w || !h)
        return 0;

    w = CLAMP(w, global.min.w, global.max.w);
    h = CLAMP(h, global.min.h, global.max.h);

    if (w == global.last.w && h == global.last.h)
        return 0;

    debug("do resize %dx%d\n", w, h);

    global.last.w = w;
    global.last.h = h;

    int area = 0;
    int mw = w;
    int mh = h;

    RRMode mode;
    RRCrtc crtc;

    XRRScreenResources *sr = XRRGetScreenResources(display.id, display.root);

    for (int i = 0; sr && i < sr->noutput; i++) {
        XRROutputInfo *oi = XRRGetOutputInfo(display.id, sr, sr->outputs[i]);
        if (!oi)
            continue;
        if (oi->connection != RR_Connected) {
            XRRFreeOutputInfo(oi);
            continue;
        }
        for (int k = 0; k < oi->nmode; k++) {
            for (int m = 0; m < sr->nmode; m++) {
                XRRModeInfo *mi = &sr->modes[m];
                if (oi->modes[k] != mi->id)
                    continue;
                const int miw = mi->width;
                const int mih = mi->height;
                if (miw > w || mih > h || miw * mih <= area)
                    continue;
                area = miw * mih;
                mode = mi->id;
                crtc = oi->crtc;
                mw = miw;
                mh = mih;
            }
        }
        XRRFreeOutputInfo(oi);
    }

    const int screen = DefaultScreen(display.id);     // XXX
    const int dw = DisplayWidth(display.id, screen);  // XXX
    const int dh = DisplayHeight(display.id, screen); // XXX

    if (mw != dw || mh != dh) {
        XRRSetScreenSize(display.id, display.root, mw, mh, ptm(mw), ptm(mh));
        if (area)
            XRRSetCrtcConfig(display.id, sr, crtc, CurrentTime, 0, 0, mode,
                             RR_Rotate_0, sr->outputs, sr->noutput);
    }

    if (sr)
        XRRFreeScreenResources(sr);

    return 1;
#endif
}
