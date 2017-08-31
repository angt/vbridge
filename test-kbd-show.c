#include "common.h"
#include "display.h"
#include "option.h"

#include <X11/XKBlib.h>

static void
print_mods(unsigned mods)
{
    print("mods:");

    if (mods & ShiftMask)
        print(" Shift");

    if (mods & LockMask)
        print(" Lock");

    if (mods & ControlMask)
        print(" Control");

    if (mods & Mod1Mask)
        print(" Mod1");

    if (mods & Mod2Mask)
        print(" Mod2");

    if (mods & Mod3Mask)
        print(" Mod3");

    if (mods & Mod4Mask)
        print(" Mod4");

    if (mods & Mod5Mask)
        print(" Mod5");

    print("\n");
}

static void
print_action_mods(XkbModAction *mods)
{
    print("  flags:");

    if (mods->flags & XkbSA_UseModMapMods)
        print(" UseModMapMods");

    if (mods->flags & XkbSA_ClearLocks)
        print(" ClearLocks");

    if (mods->flags & XkbSA_LatchToLock)
        print(" LatchToLock");

    if (mods->flags & XkbSA_LockNoLock)
        print(" LockNoLock");

    if (mods->flags & XkbSA_LockNoUnlock)
        print(" LockNoUnlock");

    print("\n");
    print("  ");
    print_mods(mods->mask);
}

static void
print_key(XkbDescPtr desc, int k)
{
    print("key %i\n", k);

    const XkbSymMapPtr smap = &desc->map->key_sym_map[k];

    print("group_info: %i\n", smap->group_info);
    print("width: %i\n", smap->width);

    for (int g = 0; g < smap->group_info; g++) {

        print(" --- group %i --- \n", g);

        KeySym *syms = &desc->map->syms[smap->offset + g * smap->width];
        print("keysym: %s\n", XKeysymToString(syms[0]));

        print("type: ");
        switch (smap->kt_index[g]) {
        case XkbOneLevelIndex:
            print("XkbOneLevelIndex\n");
            break;
        case XkbTwoLevelIndex:
            print("XkbTwoLevelIndex\n");
            break;
        case XkbAlphabeticIndex:
            print("XkbAlphabeticIndex\n");
            break;
        case XkbKeypadIndex:
            print("XkbKeypadIndex\n");
            break;
        default:
            print("\n");
        }

        XkbKeyTypePtr type = &desc->map->types[smap->kt_index[g]];
        print("num_levels: %i\n", type->num_levels);

        for (int i = 0; i < type->map_count; i++) {
            XkbKTMapEntryPtr map = &type->map[i];
            if (!map->active)
                continue;
            print("keysym: %s (level %i)\n", XKeysymToString(syms[map->level]), map->level);
            print("  ");
            print_mods(map->mods.mask);
            if (type->preserve) {
                print("  preserve ");
                print_mods(type->preserve[i].mask);
            }
        }
    }

    if (desc->map->modmap[k])
        print_mods(desc->map->modmap[k]);

    if (desc->server->key_acts[k]) {
        XkbAction *action = &desc->server->acts[desc->server->key_acts[k]];

        for (int g = 0; g < smap->group_info; g++) {
            for (int i = 0; i < smap->width; i++) {
                print("action group %i level %i:\n", g, i);
                switch (action[i].any.type) {
                case XkbSA_NoAction:
                    print("  NoAction\n");
                    break;
                case XkbSA_SetMods:
                    print("  SetMods\n");
                    print_action_mods(&action[i].mods);
                    break;
                case XkbSA_LatchMods:
                    print("  LatchMods\n");
                    print_action_mods(&action[i].mods);
                    break;
                case XkbSA_LockMods:
                    print("  LockMods\n");
                    print_action_mods(&action[i].mods);
                    break;
                case XkbSA_SetGroup:
                    print("  SetGroup\n");
                    break;
                case XkbSA_LatchGroup:
                    print("  LatchGroup\n");
                    break;
                case XkbSA_LockGroup:
                    print("  LockGroup\n");
                    break;
                case XkbSA_MovePtr:
                    print("  MovePtr\n");
                    break;
                case XkbSA_PtrBtn:
                    print("  PtrBtn\n");
                    break;
                case XkbSA_LockPtrBtn:
                    print("  LockPtrBtn\n");
                    break;
                case XkbSA_SetPtrDflt:
                    print("  SetPtrDflt\n");
                    break;
                case XkbSA_ISOLock:
                    print("  ISOLock\n");
                    break;
                case XkbSA_SwitchScreen:
                    print("  SwitchScreen\n");
                    break;
                case XkbSA_SetControls:
                    print("  SetControls\n");
                    break;
                case XkbSA_LockControls:
                    print("  LockControls\n");
                    break;
                case XkbSA_ActionMessage:
                    print("  ActionMessage\n");
                    break;
                case XkbSA_RedirectKey:
                    print("  RedirectKey\n");
                    break;
                case XkbSA_DeviceBtn:
                    print("  DeviceBtn\n");
                    break;
                case XkbSA_LockDeviceBtn:
                    print("  LockDeviceBtn\n");
                    break;
                case XkbSA_DeviceValuator:
                    print("  DeviceValuator\n");
                    break;

                default:
                    print("%i\n", action[i].any.type);
                }
            }
        }
    }

    print("\n");
}

int
main(int argc, char **argv)
{
    int device = XkbUseCoreKbd;
    int keycode = -1;

    option(opt_int, &device, "device", "");
    option(opt_int, &keycode, "keycode", "");
    option_run(argc, argv);

    display_init();

    int xmajor = XkbMajorVersion;
    int xminor = XkbMinorVersion;
    int xopcode, xerror, xevent;

    if (!XkbQueryExtension(display.id, &xopcode, &xevent, &xerror, &xmajor, &xminor))
        error("couldn't query XKB extension\n");

    XkbDescPtr desc = XkbGetMap(display.id, XkbAllMapComponentsMask, device);

    int kmin = desc->min_key_code;
    int kmax = desc->max_key_code;

    if (keycode != -1) {
        print_key(desc, keycode);
    } else {
        for (int k = kmin; k <= kmax; k++)
            print_key(desc, k);
    }

    XkbFreeKeyboard(desc, 0, True);

    display_exit();

    return 0;
}
