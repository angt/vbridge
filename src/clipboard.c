#include "clipboard.h"
#include "common-static.h"

static struct clipboard_global {
    Window window;

    struct {
        Atom clipboard;
        Atom targets;
        Atom utf8_string;
    } atom;

    struct {
        uint8_t *data;
    } set, get;

} global;

static uint8_t *
utf8_to_latin(uint8_t *src)
{
    size_t len = str_len((char *)src);
    uint8_t *dst = safe_calloc(1, len + 1);

    for (size_t i = 0, j = 0; src[j]; j++) {
        if (!(src[j] & 0x80)) {
            dst[i++] = src[j];
        } else if ((src[j] & 0xFC) == 0xC0) {
            dst[i] = src[j++] << 6;
            dst[i++] |= src[j] & 0x3F;
        }
    }

    return dst;
}

static uint8_t *
latin_to_utf8(uint8_t *src)
{
    size_t len = str_len((char *)src) * 2;
    uint8_t *dst = safe_calloc(1, len + 1);

    for (size_t i = 0, j = 0; src[j]; j++) {
        if (src[j] & 0x80) {
            dst[i++] = 0xC0 | (src[j] >> 6);
            dst[i++] = 0x80 | (src[j] & 0x3F);
        } else {
            dst[i++] = src[j];
        }
    }

    return dst;
}

void
clipboard_init(Window window)
{
    if (!display.id)
        return;

    if (!window) {
        XSetWindowAttributes swa;
        swa.event_mask = 0;

        window = XCreateWindow(display.id,
                               display.root,
                               0, 0, 16, 16, 0,
                               display.depth, InputOutput, display.visual,
                               CWEventMask, &swa);
    }

    if (!window)
        return;

    global.atom.clipboard = XInternAtom(display.id, "CLIPBOARD", False);
    global.atom.targets = XInternAtom(display.id, "TARGETS", False);
    global.atom.utf8_string = XInternAtom(display.id, "UTF8_STRING", False);

    XFixesSelectSelectionInput(display.id, window, XA_PRIMARY, 0
                                  | XFixesSetSelectionOwnerNotifyMask
                                  | XFixesSelectionWindowDestroyNotifyMask
                                  | XFixesSelectionClientCloseNotifyMask);

    XFixesSelectSelectionInput(display.id, window, global.atom.clipboard, 0
                                  | XFixesSetSelectionOwnerNotifyMask
                                  | XFixesSelectionWindowDestroyNotifyMask
                                  | XFixesSelectionClientCloseNotifyMask);

    global.window = window;
}

static void
set_selection_owner(Atom atom)
{
    if (!global.window)
        return;

    XSetSelectionOwner(display.id, atom, global.window, CurrentTime);

    if (XGetSelectionOwner(display.id, atom) != global.window)
        warning("couldn't set clipboard\n");
}

static void
clipboard_notify_other(XFixesSelectionNotifyEvent *event)
{
    if (!event)
        return;

    if ((event->owner == None) ||
        (event->owner == global.window))
        return;

    if ((event->selection != XA_PRIMARY) &&
        (event->selection != global.atom.clipboard))
        return;

    if (event->subtype != XFixesSetSelectionOwnerNotify)
        return;

    XConvertSelection(display.id, event->selection,
                      global.atom.utf8_string, event->selection,
                      global.window, event->timestamp);
}

static void
clipboard_request(XSelectionRequestEvent *event)
{
    if (!event)
        return;

    if (event->requestor == global.window)
        return;

    if (!global.set.data)
        return;

    XEvent revent;
    revent.xselection.property = event->property;

    if (event->target == global.atom.targets) {
        const Atom targets[] = {
            global.atom.utf8_string,
            XA_STRING,
        };
        XChangeProperty(display.id, event->requestor, event->property, XA_ATOM,
                        32, PropModeReplace, (unsigned char *)targets, COUNT(targets));
    } else if (event->target == global.atom.utf8_string) {
        size_t len = str_len((char *)global.set.data);
        XChangeProperty(display.id, event->requestor, event->property, event->target,
                        8, PropModeReplace, global.set.data, len);
    } else if (event->target == XA_STRING) {
        uint8_t *data = utf8_to_latin(global.set.data);
        size_t len = str_len((char *)data);
        XChangeProperty(display.id, event->requestor, event->property, event->target,
                        8, PropModeReplace, data, len);
        safe_free(data);
    } else {
        revent.xselection.property = None;
    }

    revent.xselection.type = SelectionNotify;
    revent.xselection.display = event->display;
    revent.xselection.requestor = event->requestor;
    revent.xselection.selection = event->selection;
    revent.xselection.target = event->target;
    revent.xselection.time = event->time;

    XSendEvent(display.id, revent.xselection.requestor, False, 0, &revent);
    XFlush(display.id);
}

static void
clipboard_notify(XSelectionEvent *event)
{
    if (!event)
        return;

    if (event->requestor != global.window)
        return;

    if ((event->property == None) &&
        (event->target != XA_STRING)) {
        XConvertSelection(display.id, event->selection, XA_STRING,
                          event->selection, global.window, event->time);
        return;
    }

    Atom type;
    int format;
    unsigned char *data = NULL;
    unsigned long len = 0;
    unsigned long left = 0;

    XGetWindowProperty(display.id, global.window,
                       event->property, 0, 4096, 0, AnyPropertyType,
                       &type, &format, &len, &left, &data);

    XDeleteProperty(display.id, global.window, type);
    XFlush(display.id);

    if (!data)
        return;

    if (!len) {
        XFree(data);
        return;
    }

    data[len] = '\0';

    safe_free(global.get.data);

    if (type == XA_STRING) {
        global.get.data = latin_to_utf8(data);
    } else {
        global.get.data = (uint8_t *)STR_MAKE((char *)data);
    }

    XFree(data);
}

uint8_t *
clipboard_get(void)
{
    uint8_t *data = global.get.data;
    global.get.data = NULL;
    return data;
}

void
clipboard_set(uint8_t *data)
{
    if (!data)
        return;

    int new_data = str_cmp((char *)global.set.data, (char *)data);

    safe_free(global.set.data);

    global.set.data = data;

    if (new_data) {
        set_selection_owner(XA_PRIMARY);            // XXX
        set_selection_owner(global.atom.clipboard); // XXX
    }
}

int
clipboard_event(XEvent *event)
{
    if (!event)
        return 0;

    if (event->type == display.xfixes_event + XFixesSelectionNotify) {
        clipboard_notify_other((XFixesSelectionNotifyEvent *)event);
    } else
        switch (event->type) {
        case SelectionClear:
            break;
        case SelectionRequest:
            clipboard_request(&event->xselectionrequest);
            break;
        case SelectionNotify:
            clipboard_notify(&event->xselection);
            break;
        default:
            return 0;
        }

    return 1;
}
