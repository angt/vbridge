#include "common-static.h"
#include "core-client.h"
#include "utf8-static.h"
#include "utf8.h"

#include "keysym.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static struct client_global {
    const char *host;
    core_client_t core;
    char *user;
    char *pass;
    image_info_t image;
} global;

static void
cursor_update(uint32_t *data)
{
    if (!data)
        return;

    int w = global.core.cursor.w;
    int h = global.core.cursor.h;

    int x = global.core.cursor.x;
    int y = global.core.cursor.y;

    EM_ASM_ARGS({
        do_cursor_update($0, $1, $2, $3, $4);
    }, data, x, y, w, h);

    safe_free(data);
}

static void
image_update(void)
{
    uint32_t *data = global.core.image.data;

    int w = global.core.image.w;
    int h = global.core.image.h;

    if (!data)
        return;

    EM_ASM_ARGS({
        do_image_update($0, $1, $2);
    }, data, w, h);
}

static void
image_resize(int w, int h)
{
    static int first = 1;

    if (!w || !h)
        return;

    if ((w == global.image.w) &&
        (h == global.image.h) &&
        (!first))
        return;

    safe_free(global.image.data);

    global.image.w = w;
    global.image.h = h;
    global.image.stride = w;
    global.image.data = safe_calloc(h, 4 * w);

    global.core.image = global.image;

    uint32_t *data = global.core.image.data;

    EM_ASM_ARGS({
        do_image_resize($0, $1, $2);
    }, data, w, h);

    first = 0;
}

void
client_send_quality(unsigned min, unsigned max)
{
    core_send_quality(&global.core, min, max);
}

void
client_send_resize(unsigned w, unsigned h)
{
    core_send_resize(&global.core, w, h);
}

void
client_send_pointer(int x, int y, int sync)
{
    core_send_pointer(&global.core, x, y, sync);
}

void
client_send_button(unsigned button, int press)
{
    core_send_button(&global.core, button, press);
}

void
client_send_key(int ucs, unsigned symbol, int key, int press)
{
    core_send_key(&global.core, ucs, symbol, key, press);
}

int
client_loop(void)
{
    int ret = core_recv_all(&global.core);

    if (core_received(&global.core, command_start))
        core_send_auth(&global.core, global.user, global.pass);

    if (core_received(&global.core, command_access)) {
        if (global.core.access <= 0) {
            EM_ASM(do_access(0););
            return 0;
        } else {
            core_send(&global.core, command_access);
            EM_ASM(do_access(1););
        }
    }

    if (core_received(&global.core, command_master)) {
        EM_ASM_ARGS({
            do_master($0);
        }, global.core.master);
    }

    cursor_update(core_recv_cursor(&global.core));
    image_resize(global.core.size.w, global.core.size.h);

    if (core_received(&global.core, command_pointer_sync)) {
        EM_ASM_ARGS({
            do_pointer_sync($0, $1);
        }, global.core.pointer.sx, global.core.pointer.sy);
    }

    if (core_received(&global.core, command_pointer)) {
        EM_ASM_ARGS({
            do_pointer($0, $1);
        }, global.core.pointer.sx, global.core.pointer.sy);
    }

    if (core_received(&global.core, command_image_data))
        image_update();

    char *data;

    if (data = core_recv_clipboard(&global.core), data) {
        //  clipboard_set((uint8_t *)data);
        safe_free(data);
    }

    if (data = core_recv_control(&global.core), data) {
        safe_free(data);
    }

    if (ret)
        core_send_all(&global.core);

    return ret;
}

void
client_init(char *host, char *port, char *user, char *pass)
{
    global.host = host;
    global.user = user;
    global.pass = pass;

    core_create(&global.core, host, port);
}

#else
#endif
