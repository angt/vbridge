#include "input.h"
#include "common-static.h"

#include <X11/XKBlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XTest.h>

static struct input_global {
    int event;
    int device;
    unsigned button;
    unsigned mask;
    struct {
        uint8_t keycode;
    } keys[256];
} global;

static void
input_update_mods(unsigned mods)
{
    XkbLockModifiers(display.id, global.device, global.mask, mods);
    XFlush(display.id);
}

static void
input_update_map(XkbDescPtr desc)
{
    XkbSetMap(display.id, XkbAllMapComponentsMask, desc);
    XFlush(display.id);
}

static int
input_install_key(XkbDescPtr desc, int keycode, KeySym keysym)
{
    int type = XkbOneLevelIndex;

    if (XkbChangeTypesOfKey(desc, keycode, 1, XkbGroup1Mask, &type, NULL) != Success)
        return -1;

    KeySym *sym = XkbResizeKeySyms(desc, keycode, 1);

    if (!sym)
        return -1;

    *sym = keysym;

    /* FUTUR
    keyboard_update_map(desc);

    XkbAction *action = XkbResizeKeyActions(desc, keycode, 1);

    if (action)
        action->any.type = XkbSA_NoAction;
    else
        return -1;
    */

    desc->map->modmap[keycode] = 0;

    input_update_map(desc);

    return 0;
}

static int
input_search_key(KeySym keysym, unsigned mods, unsigned *change_mods)
{
    XkbDescPtr desc = XkbGetMap(display.id, XkbAllMapComponentsMask, global.device);

    if (!desc)
        return 0;

    const int group = 0; // XXX FUTUR
    const int kmin = desc->min_key_code;
    const int kmax = desc->max_key_code;

    XkbSymMapPtr smap = NULL;
    KeySym *syms = NULL;

    int keycode = 0;
    int keycode_empty = 0;
    int level_want = 0;
    int level_have = 0;

    for (int k = kmin; !keycode && k < kmax; k++) {
        smap = &desc->map->key_sym_map[k];
        syms = &desc->map->syms[smap->offset + group * smap->width];
        if (!smap->width) {
            keycode_empty = k;
            level_have = 0;
            continue;
        }
        for (int level = 0; level < smap->width; level++) {
            if (!syms[level]) {
             // keycode_empty = k;    // XXX
             // level_have = level;   // XXX
                continue;
            }
            if (keysym == syms[level]) {
                level_have = level;
                keycode = k;
                debug("keysym %i: found at keycode %i, level %i\n", keysym, keycode, level);
                break;
            }
        }
    }

    if (!syms)
        goto end;

    if (!keycode) {
        if (!keycode_empty) {
            // XXX
        }
        if (input_install_key(desc, keycode_empty, keysym) == -1) {
            warning("keyboard update failed\n");
            goto end;
        }
        keycode = keycode_empty;
    }

    const XkbKeyTypePtr kt = &desc->map->types[smap->kt_index[group]];

    const unsigned use = mods & kt->mods.mask;

    if (keysym == syms[0]) {
        if (!(use & ~global.mask)) {
            *change_mods = use;
            debug("keysym %i: ok for level 0\n", keysym);
            goto end;
        }
    }

    for (int i = 0; i < kt->map_count; i++) {
        XkbKTMapEntryPtr ktmap = &kt->map[i];

        if (!ktmap->active)
            continue;

        if (use == ktmap->mods.mask)
            level_want = ktmap->level;

        if (keysym != syms[ktmap->level])
            continue;

        if (!((use ^ ktmap->mods.mask) & ~global.mask)) {
            *change_mods = use ^ ktmap->mods.mask;
            goto end;
        }
    }

    if (syms[level_want] != syms[level_have]) {
        debug("keysym %i: level want: %i, have: %i\n", keysym, level_want, level_have);
        syms[level_have] = syms[level_want];
        syms[level_want] = keysym;
        input_update_map(desc);
    }

end:
    XkbFreeKeyboard(desc, 0, True);
    return keycode;
}

void
input_key(uint8_t key, KeySym keysym, int press)
{
    int keycode = 0;

    if (press) {
        if (!keysym ||
            keysym == XK_Caps_Lock ||
            keysym == XK_Shift_Lock ||
            keysym == XK_Num_Lock)
            return;

        XkbStateRec state;
        XkbGetState(display.id, global.device, &state);

        unsigned change_mods = 0;

        keycode = input_search_key(keysym, state.mods, &change_mods);
        global.keys[key].keycode = keycode;

        if (change_mods & global.mask)
            input_update_mods(change_mods ^ state.mods);

    } else {
        keycode = global.keys[key].keycode;
        global.keys[key].keycode = 0;
    }

    if (!keycode)
        return;

    XTestFakeKeyEvent(display.id, keycode, press, CurrentTime);
    XFlush(display.id);
}

void
input_button(uint8_t button, int press)
{
    if (button > 31)
        return;

    if (press) {
        global.button |= 1 << button;
    } else {
        global.button &= ~(1 << button);
    }

    XTestFakeButtonEvent(display.id, button, press, CurrentTime);
    XFlush(display.id);
}

void
input_pointer(int x, int y, int rel)
{
    Window w = rel ? None : display.root;
    XWarpPointer(display.id, None, w, 0, 0, 0, 0, x, y);
    XFlush(display.id);
}

void
input_release(void)
{
    for (int i = 0; i < 256; i++)
        input_key(i, 0, 0);

    for (int i = 0; i < 8; i++) {
        if ((global.button >> i) & 1)
            input_button(i, 0);
    }

    global.button = 0;
}

int
input_need_release(void)
{
    for (int i = 0; i < 256; i++) {
        if (global.keys[i].keycode)
            return 1;
    }

    for (int i = 0; i < 8; i++) {
        if ((global.button >> i) & 1)
            return 1;
    }

    return 0;
}

static int
input_get_keyboard(const char *name)
{
    int device = XkbUseCoreKbd;

    int n = 0;
    XDeviceInfo *devices = XListInputDevices(display.id, &n);

    if (!devices) {
        warning("couldn't list input devices\n");
        return device;
    }

    for (int i = 0; i < n; i++) {
        if (!str_cmp(devices[i].name, name))
            device = devices[i].id;
    }

    if (device == XkbUseCoreKbd)
        warning("device `%s' not found\n", name);

    XFreeDeviceList(devices);

    return device;
}

void
input_init(void)
{
    int xmajor, xminor;
    int xevent, xerror, xopcode;

    if (!XTestQueryExtension(display.id, &xevent, &xerror, &xmajor, &xminor))
        error("couldn't query XTest extension\n");

    xmajor = XkbMajorVersion;
    xminor = XkbMinorVersion;

    if (!XkbQueryExtension(display.id, &xopcode, &global.event, &xerror, &xmajor, &xminor))
        error("couldn't query XKB extension\n");

    global.device = input_get_keyboard("Virtual core XTEST keyboard"); // XXX

    XkbChangeEnabledControls(display.id, global.device, XkbRepeatKeysMask, 0);

    XkbSelectEvents(display.id, XkbUseCoreKbd,
                    XkbNewKeyboardNotifyMask | XkbMapNotifyMask,
                    XkbNewKeyboardNotifyMask | XkbMapNotifyMask);

    XkbSelectEvents(display.id, global.device,
                    XkbNewKeyboardNotifyMask | XkbMapNotifyMask,
                    XkbNewKeyboardNotifyMask | XkbMapNotifyMask);

    global.mask = XkbKeysymToModifiers(display.id, XK_Num_Lock) | XkbKeysymToModifiers(display.id, XK_Caps_Lock);

    XTestGrabControl(display.id, True);

    input_key(255, XK_VoidSymbol, 1); // ugly hack...
    input_release();

    global.device = XkbUseCoreKbd;
}

int
input_event(XEvent *event)
{
    XkbEvent *xkb_event = (XkbEvent *)event;

    if (xkb_event->type != global.event)
        return 0;

    switch (xkb_event->any.xkb_type) {
 // case XkbNewKeyboardNotify:            // XXX need some test
 //     keyboard_action(global.kmax, 1);  // XXX
 //     keyboard_action(global.kmax, 0);  // XXX
 //     break;                            // XXX
    case XkbMapNotify:
        XkbRefreshKeyboardMapping(&(xkb_event->map));
        break;
    }

    return 1;
}

void
input_exit(void)
{
    input_release();
}
