#include "buffer-static.h"
#include "core-client.h"
#include "option.h"

#include "clipboard.h"
#include "display.h"
#include "image.h"
#include "pointer.h"

#include "terminal.h"

#include "auth-gss.h"
#include "auth-ssl.h"

#include <X11/extensions/Xrender.h>

static struct client_global {
    const char *host;

    core_client_t core;

    auth_gss_t auth_gss;

    struct {
        buffer_t send;
    } gss, control;

    struct {
        Window id;
        GC gc;
        int w, h;
        Atom delete;
        Picture picture;
    } window;

    image_t image;
    int fullscreen;

    struct {
        Pixmap id;
        int enabled;
    } pixmap;

    struct {
        Cursor id;
        Cursor remote;
        Picture picture;
        int x, y, w, h;
    } cursor;

    int grab;

    struct {
        unsigned key;
        int on;
        int action;
    } lock;

    struct {
        unsigned min;
        unsigned max;
    } quality;

    int display;
    int use_stdin;
    int trust;
    int direct_key;
} global;

static void
key_grab(int mode)
{
    if (mode == global.grab)
        return;

    global.grab = !((mode < 0) ? global.grab : !mode);

    if (global.grab) {
        XGrabKeyboard(display.id,
                      global.window.id, True,
                      GrabModeAsync, GrabModeAsync,
                      CurrentTime);
    } else {
        XUngrabKeyboard(display.id, CurrentTime);
    }

    XSync(display.id, False);
}

static void
cursor_change(Cursor id)
{
    if (global.cursor.id == id)
        return;

    XDefineCursor(display.id, global.window.id, id);
    global.cursor.id = id;

    XSync(display.id, False);
}

static void
cursor_exit()
{
    if (global.cursor.remote)
        XFreeCursor(display.id, global.cursor.remote);

    if (global.cursor.picture)
        XRenderFreePicture(display.id, global.cursor.picture);
}

static void
cursor_update(uint32_t *data)
{
    if (!data)
        return;

    global.cursor.w = global.core.cursor.w;
    global.cursor.h = global.core.cursor.h;
    global.cursor.x = global.core.cursor.x;
    global.cursor.y = global.core.cursor.y;

    XImage *image = XCreateImage(display.id, display.visual, 32, ZPixmap, 0,
                                 (char *)data, global.cursor.w, global.cursor.h, 32, 0);

    Pixmap pixmap = XCreatePixmap(display.id, display.root,
                                  global.cursor.w, global.cursor.h, 32);

    GC gc = XCreateGC(display.id, pixmap, 0, NULL);

    XPutImage(display.id, pixmap, gc, image, 0, 0, 0, 0,
              global.cursor.w, global.cursor.h);

    XFreeGC(display.id, gc);

    safe_free(data);

    image->data = NULL;
    XDestroyImage(image);

    XRenderPictFormat *format = XRenderFindStandardFormat(display.id,
                                                          PictStandardARGB32);

    if (global.cursor.picture)
        XRenderFreePicture(display.id, global.cursor.picture);

    global.cursor.picture = XRenderCreatePicture(display.id, pixmap, format, 0, NULL);

    XFreePixmap(display.id, pixmap);

    if (global.cursor.remote)
        XFreeCursor(display.id, global.cursor.remote);

    global.cursor.remote = XRenderCreateCursor(display.id,
                                               global.cursor.picture,
                                               global.cursor.x,
                                               global.cursor.y);

    cursor_change(global.cursor.remote);
}

static void
window_init(void)
{
    global.window.w = 800;
    global.window.h = 600;

    XSetWindowAttributes swa;

    swa.event_mask = StructureNotifyMask
                   | ExposureMask
                   | KeyPressMask
                   | KeyReleaseMask
                   | ButtonPressMask
                   | ButtonReleaseMask
                   | PointerMotionMask
                   | FocusChangeMask
                   | EnterWindowMask
                   | LeaveWindowMask
                   ;

    global.window.id = XCreateWindow(display.id, display.root, 100, 100,
                                     global.window.w, global.window.h, 0,
                                     display.depth, InputOutput, display.visual,
                                     CWEventMask, &swa);

    global.window.gc = XCreateGC(display.id, global.window.id, 0, NULL);

    global.window.delete = XInternAtom(display.id, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display.id, global.window.id, &global.window.delete, 1);

    int ev, er;
    XRenderQueryExtension(display.id, &ev, &er);

    int minor = RENDER_MINOR;
    int major = RENDER_MAJOR;
    XRenderQueryVersion(display.id, &major, &minor);

    XRenderPictFormat *format = XRenderFindStandardFormat(display.id, PictStandardRGB24);

    global.window.picture = XRenderCreatePicture(display.id,
                                                 global.window.id, format, 0, NULL);

    XClassHint ch;
    ch.res_name = PROG_SERVICE;
    ch.res_class = PROG_NAME;

    XSetClassHint(display.id, global.window.id, &ch);

    char *name = STR_MAKE(PROG_NAME, " - ", global.host);

    if (name) {
        XStoreName(display.id, global.window.id, name);
        safe_free(name);
    }
}

static void
window_fullscreen(int mode)
{
    XEvent event;
    byte_set(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.send_event = True;
    event.xclient.window = global.window.id;
    event.xclient.message_type = XInternAtom(display.id, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = mode;
    event.xclient.data.l[1] = XInternAtom(display.id, "_NET_WM_STATE_FULLSCREEN", False);

    XSendEvent(display.id, display.root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XSync(display.id, False);
}

static void
cursor_draw(void)
{
    if (global.core.master)
        return;

    XRenderComposite(display.id,
                     PictOpOver,
                     global.cursor.picture,
                     None,
                     global.window.picture,
                     0, 0, 0, 0,
                     global.core.pointer.x - global.cursor.x,
                     global.core.pointer.y - global.cursor.y,
                     global.cursor.w, global.cursor.h);
}

static void
window_update(void)
{
    unsigned w = global.window.w;
    unsigned h = global.window.h;

    unsigned iw = global.image.info.w;
    unsigned ih = global.image.info.h;

    XRectangle rect[2];
    int n = 0;

    if (ih < h) {
        rect[n++] = (XRectangle){0, ih, w, h - ih};
        h = ih;
    }

    if (iw < w) {
        rect[n++] = (XRectangle){iw, 0, w - iw, h};
        w = iw;
    }

    if (n)
        XFillRectangles(display.id, global.window.id, global.window.gc, rect, n);

    if (global.pixmap.id)
        XCopyArea(display.id, global.pixmap.id, global.window.id, global.window.gc, 0, 0, w, h, 0, 0);

    cursor_draw();
}

static void
image_update(void)
{
    image_put(&global.image, 0, 0, 0, 0,
              global.image.info.w, global.image.info.h);

    window_update();
}

static void
image_cleanup(void)
{
    image_delete(&global.image);

    if (global.pixmap.id) {
        XFreePixmap(display.id, global.pixmap.id);
        global.pixmap.id = 0;
    }
}

static void
image_resize(int w, int h)
{
    static int first = 1;

    if (!w || !h)
        return;

    if ((w == global.image.info.w) && // XXX
        (h == global.image.info.h) && // XXX
        (!first))                     // XXX
        return;                       // XXX

    XSync(display.id, False); // XXX

    image_cleanup();

    if (global.pixmap.enabled)
        global.pixmap.id = XCreatePixmap(display.id, global.window.id, w, h, display.depth);

    image_create(&global.image, global.pixmap.id ?: global.window.id, w, h);

    global.core.image = global.image.info;

    if (first) {
        image_update();
        XResizeWindow(display.id, global.window.id, w, h);
        XMapWindow(display.id, global.window.id);
        if (global.fullscreen)
            window_fullscreen(1);
        XSync(display.id, False);
        first = 0;
    }
}

static void
pretty_print(char *str)
{
    while (!str_empty(str)) {
        char *end = str_skip_char_space(str);
        info("%s: %.*s\n", global.core.netio.name, end - str, str);
        str = str_skip_line(end);
    }
}

#ifndef NETIO_NO_SSL
static void
send_auth_ssl(void)
{
    if (!global.trust && !openssl_verify(global.core.netio.ssl, 0)) {
        warning("WARNING: this server is not trusted!\n");

        if (terminal_get_yesno("continue", 0) == 0)
            exit(EXIT_SUCCESS);

        if (terminal_get_yesno("trust this server", 0) == 1) {
            openssl_verify(global.core.netio.ssl, 1);
            auth_ssl_save(0);
        }
    }

    core_send(&global.core, command_auth_ssl);
}
#endif

static int
send_auth_gss(void)
{
    if (str_empty(global.host))
        return -1;

    netio_t *const netio = &global.core.netio;

    int ret = auth_gss_create(&global.auth_gss, global.host, &global.gss.send, &global.core.gss.recv);

    if (ret == -1) {
        auth_gss_print_error(&global.auth_gss, netio->name);
        auth_gss_delete(&global.auth_gss);
    }

    uint8_t *data = global.gss.send.data;

    if (data) {
        if (ret != -1)
            core_send_data(&global.core, command_auth_gss, data, buffer_size(&global.gss.send));
        safe_free(data);
    }

    byte_set(&global.gss.send, 0, sizeof(buffer_t));

    return ret;
}

static void
send_auth(void)
{
    char *name = NULL;
    char *pass = NULL;

    while (!pass) {
        if (name = terminal_get_min("Login: ", 0, 1), !name)
            exit(EXIT_SUCCESS);
        if (pass = terminal_get_min("Password: ", 1, 1), !pass)
            safe_free(name);
    }

    core_send_auth(&global.core, name, pass);
    byte_set_safe(pass, 0, str_len(pass));

    safe_free(pass);
    safe_free(name);
}

static void
send_auth_all(void)
{
    static int state = 0;

    switch (state) {
    case 0:
        state++;
        if (send_auth_gss() != -1)
            break;
        /* FALLTHRU */
    case 1:
#ifndef NETIO_NO_SSL
        state++;
        send_auth_ssl();
        break;
    case 2:
        state++;
        send_auth();
        break;
    case 3:
#else
        state++;
        send_auth();
        break;
    case 2:
#endif
        if (global.use_stdin)
            exit(EXIT_FAILURE);
        pretty_print("authentication failed, please try again");
        send_auth();
    }
}

static void
display_event(void)
{
    while (XPending(display.id)) {
        XEvent event;
        XNextEvent(display.id, &event);

        /*
        if ((!global.grab) &&
            (XFilterEvent(&event, global.window.id)))
            continue;
        */

        if (clipboard_event(&event)) {
            if (!global.core.master)
                continue;
            char *data = (char *)clipboard_get();
            if (!data)
                continue;
            core_send_data(&global.core, command_clipboard, data, str_len(data));
            safe_free(data);
            continue;
        }

        switch (event.type) {

        case MotionNotify:
            core_send_pointer(&global.core, event.xmotion.x, event.xmotion.y, pointer_is_warp());
            break;

        case ButtonPress:
        case ButtonRelease:
            core_send_button(&global.core, event.xbutton.button, event.type == ButtonPress);
            break;

        case KeyRelease:
            if (event.xkey.keycode == global.lock.key) {
                global.lock.on = 0;
                if (global.lock.action) {
                    global.lock.action = 0;
                } else {
                    key_grab(-1);
                }
                break;
            }

            if (global.lock.on)
                break;

            core_send_key(&global.core, 0, 0, event.xkey.keycode, 0);
            break;

        case KeyPress:
            {
                char buf[32];
                KeySym keysym = 0;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);

                if (global.direct_key) {
                    if (keysym == XK_F12) {
                        running = 0;
                        break;
                    }
                }

                if (event.xkey.keycode == global.lock.key) {
                    global.lock.on = 1;
                    break;
                }

                if (global.lock.on) {
                    if (keysym == XK_F11 || keysym == XK_f) {
                        window_fullscreen(2);
                    } else if (keysym == XK_q) {
                        running = 0;
                    } else if (keysym == XK_r) {
                        core_send_resize(&global.core, global.window.w, global.window.h);
                    } else if (keysym == XK_F12) {
                        core_send_data(&global.core, command_control, "tcp", 3);
                    } else if (keysym == XK_KP_Add) {
                        if (global.quality.min < global.quality.max)
                            core_send_quality(&global.core,
                                    ++global.quality.min, global.quality.max);
                    } else if (keysym == XK_KP_Multiply) {
                        if (global.quality.max < 8)
                            core_send_quality(&global.core,
                                    global.quality.min, ++global.quality.max);
                    } else if (keysym == XK_KP_Subtract) {
                        if (global.quality.min)
                            core_send_quality(&global.core,
                                    --global.quality.min, global.quality.max);
                    } else if (keysym == XK_KP_Divide) {
                        if (global.quality.max > global.quality.min)
                            core_send_quality(&global.core,
                                    global.quality.min, --global.quality.max);
                    }
                    global.lock.action = 1;
                    break;
                }

                core_send_key(&global.core, 0, keysym, event.xkey.keycode, 1);
                break;
            }

        case ConfigureNotify:
            global.window.w = event.xconfigure.width;
            global.window.h = event.xconfigure.height;
            if ((global.window.w != global.image.info.w) ||
                (global.window.h != global.image.info.h))
                core_send_resize(&global.core, global.window.w, global.window.h);
            window_update();
            break;

        case FocusOut:
        case FocusIn:
            core_send_key(&global.core, 0, 0, 0, 0); // reset inputs
            break;

        case EnterNotify:
        case LeaveNotify:
            break;

        case Expose:
            if (event.xexpose.count == 0)
                image_update();
            break;

        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == global.window.delete)
                running = 0;
            pointer_event(&event);
            break;

        case MappingNotify:
            XRefreshKeyboardMapping(&(event.xmapping));
            break;
        }
    }
}

static void
main_init(int argc, char **argv)
{
    const char *port = CONFIG_PORT;
    option(opt_host, &global.host, NULL, NULL);
    option(opt_port, &port, NULL, NULL);
    option(opt_port, &port, "port", "port to connect on the remote host");

#ifndef NETIO_NO_SSL
    const char *ciphers = CONFIG_SSL_CIPHERS;
    int rsa_len = CONFIG_SSL_RSA_LEN;
    int rsa_exp = CONFIG_SSL_RSA_EXP;
    option(opt_list, &ciphers, "ciphers", "sets the list of ciphers");
    option(opt_int, &rsa_len, "rsa-length", "RSA modulus length (in bits)");
    option(opt_int, &rsa_exp, "rsa-exponent", "RSA public exponent");
#endif

    int delegate = 0;
    option(opt_flag, &delegate, "delegate", "delegate user credentials");
    option(opt_int, &global.lock.key, "lock-key", "keycode used to lock/unlock the keyboard and mouse");

    // hidden
    option(opt_flag, &global.use_stdin, "stdin", NULL);
    option(opt_flag, &global.fullscreen, "fullscreen", NULL);
    option(opt_flag, &global.pixmap.enabled, "enable-pixmap", NULL);
    option(opt_flag, &global.direct_key, "direct-key", NULL);

#ifndef NETIO_NO_SSL
    int print_cert = 0;
    option(opt_flag, &print_cert, "print-cert", NULL);
    option(opt_flag, &global.trust, "trust", NULL);
#endif

    option_run(argc, argv);

    global.display = 1;

    for (int i = 1; i < argc; i++) {
        if (!global.display) {
            size_t len = str_len(argv[i]);
            if (buffer_write_size(&global.control.send) < len + 2)
                break;
            if (buffer_read_size(&global.control.send))
                buffer_write(&global.control.send, ' ');
            buffer_write_data(&global.control.send, argv[i], len);
        } else if (!str_cmp(argv[i], "--")) {
            global.display = 0;
            buffer_setup(&global.control.send, safe_calloc(1, 4096), 4096);
        }
    }

    if (global.use_stdin)
        terminal_set_tty(NULL);

    common_init();
    socket_init();

#ifndef NETIO_NO_SSL
    openssl_init();

    auth_ssl_load(0);
    auth_ssl_setup_rsa(rsa_len, rsa_exp);

    openssl_use_ciphers(ciphers);
    openssl_print_error(NULL); // XXX

    if (print_cert) {
        char *cert = openssl_get_cert();
        if (cert)
            print("%s\n", cert);
        safe_free(cert);
        exit(0);
    }
#endif

    if (!core_create(&global.core, global.host, port))
        exit(2);

    if (delegate)
        auth_gss_delegate();

    global.quality.min = CONFIG_QUALITY_MIN;
    global.quality.max = CONFIG_QUALITY_MAX;

    if (!global.display)
        return;

    display_init();
    window_init();
    clipboard_init(global.window.id);

    if (!global.lock.key)
        global.lock.key = XKeysymToKeycode(display.id, 0xffe4);

    if (!global.lock.key)
        global.lock.key = XKeysymToKeycode(display.id, 0xfe11);

    if (!global.lock.key)
        warning("no key defined to lock/unlock your keyboard and mouse\n"
                "please use --lock-key to set a keycode\n");
}

static void
main_exit()
{
    image_cleanup();

    core_delete(&global.core);

    auth_gss_delete(&global.auth_gss);

    cursor_exit();
    display_exit();
#ifndef NETIO_NO_SSL
    openssl_exit();
#endif
    socket_exit();
}

int
main(int argc, char **argv)
{
    atexit(main_exit);
    main_init(argc, argv);

    while (running) {
        socket_wait(global.core.netio.fd, SOCKET_WAIT_R, 1);

        if (global.display)
            display_event();

        int ret = core_recv_all(&global.core);

        if (core_received(&global.core, command_start)) {
            info("%s: protocol %s\n", global.core.netio.name, global.core.netio.proto);
            send_auth_all();
        }

        if (global.core.gss.recv.data &&
            !buffer_write_size(&global.core.gss.recv))
            send_auth_gss();

        if (core_received(&global.core, command_access)) {
            if (!global.core.level) {
                send_auth_all();
            } else if (global.core.access <= 0) {
                pretty_print("access denied");
                break;
            } else {
                if (global.control.send.data) {
                    core_send_data(&global.core, command_control,
                                   global.control.send.data,
                                   buffer_read_size(&global.control.send));
                    core_send(&global.core, command_stop);
                } else {
                    core_send(&global.core, command_access);
                }
            }
        }

        if (global.display) {
            if (core_received(&global.core, command_master) && !global.core.master)
                key_grab(0);

            cursor_update(core_recv_cursor(&global.core));

            image_resize(global.core.size.w, global.core.size.h);

            if (core_received(&global.core, command_pointer_sync))
                pointer_warp(global.window.id, global.core.pointer.sx, global.core.pointer.sy);

            if ((core_received(&global.core, command_image_data)) ||
                (core_received(&global.core, command_pointer) &&
                 (!global.core.master)))
                image_update();
        }

        char *data;

        if (data = core_recv_clipboard(&global.core), data)
            clipboard_set((uint8_t *)data);

        if (data = core_recv_control(&global.core), data) {
            pretty_print(data);
            safe_free(data);
        }

        if (!ret)
            break;

        core_send_all(&global.core);
    }

    return 0;
}
