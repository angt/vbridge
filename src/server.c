#include "buffer-static.h"
#include "netio.h"
#include "option.h"
#include "token.h"
#include "tycho-server.h"

#include "command.h"

#include "clipboard.h"
#include "display.h"
#include "image.h"

#include "input.h"
#include "xrandr.h"

#include "acl.h"
#include "ucs_to_keysym.h"
#include "user.h"

#include "auth-gss-server.h"
#include "auth-pam.h"

#ifndef NETIO_NO_SSL
#include "auth-ssl.h"
#endif

typedef struct client client_t;

struct client {
    netio_t netio;
    tycho_t tycho;

    char *name;
    int level;
    int access;

    auth_pam_t auth_pam;
    auth_gss_t auth_gss;

    struct {
        command_t command;
        uint32_t mask;
    } send, recv;

    uint32_t to_send; // XXX

    struct {
        int x, y;
        int sx, sy;
    } pointer;

    struct {
        buffer_t send;
        buffer_t recv;
    } clipboard, control, gss;

    unsigned image_count;

    struct {
        buffer_t send;
        uint32_t hash[256];
    } cursor;

    struct {
        uint64_t accept;
    } time;

    int close;

    client_t *prev, *next;
};

static struct server_global {
    netio_t netio;

    struct {
        image_t image;
        struct {
            int x, y;
        } pointer;
        struct {
            XFixesCursorImage *image;
            uint32_t hash;
        } cursor;
        uint64_t time;
    } grab;

    struct {
        int x, y;
    } pointer;

    struct {
        int w, h;
    } resize;

    struct {
        client_t *client;
        uint64_t time;
    } master;

    struct {
        uint64_t time;
        uint64_t timeout;
    } activity;

    client_t *clients;
    acl_t *acl;

    const char *congestion;
    int pam_reinit;
} global;

static int
set_congestion(int fd, const char *name)
{
    if (str_empty(name) || fd == -1)
        return 0;

    size_t size = str_len(name) + 1;

    return socket_set(fd, IPPROTO_TCP, TCP_CONGESTION, name, size);
}

static client_t *
client_accept(void)
{
    client_t *c = safe_calloc(1, sizeof(client_t));

    if (netio_accept(&c->netio, &global.netio) == -1) {
        safe_free(c);
        return NULL;
    }

    c->next = global.clients;

    if (global.clients)
        global.clients->prev = c;

    global.clients = c;

    set_congestion(c->netio.fd, global.congestion);

    info("%s: accepted\n", c->netio.name);

    c->time.accept = time_now();

    return c;
}

static void
client_master_stop(client_t *c)
{
    if (global.master.client != c)
        return;

    input_release();

    c->to_send |= (1 << command_master);
    global.master.client = NULL;
}

static int
client_master(client_t *c)
{
    if (c->access != 2)
        return 0;

    if (global.master.client != c) {
        if (global.master.client) {
            if (time_dt(global.master.time, time_now()) < CONFIG_MASTER_TIMEOUT)
                return 0;
            if (input_need_release())
                return 0;
            global.master.client->to_send |= (1 << command_master);
        }
        c->to_send |= (1 << command_master);
        global.master.client = c;
    }

    global.master.time = time_now();

    return 1;
}

static client_t *
client_close(client_t *c)
{
    client_t *n = c->next;
    client_t *p = c->prev;

    info("%s: closed\n", c->netio.name);

    if (n) n->prev = p;
    if (p) p->next = n;
    else global.clients = n;

    client_master_stop(c);

    netio_delete(&c->netio);

    auth_pam_delete(&c->auth_pam);
    auth_gss_delete(&c->auth_gss);

    tycho_delete(&c->tycho);

    safe_free(c->name);
    safe_free(c->clipboard.send.data);
    safe_free(c->clipboard.recv.data);
    safe_free(c->control.send.data);
    safe_free(c->control.recv.data);
    safe_free(c->gss.send.data);
    safe_free(c->gss.recv.data);
    safe_free(c);

    return n;
}

static uint32_t
grab_cursor(void)
{
    uint32_t ret = 0;

    XFixesCursorImage *image = XFixesGetCursorImage(display.id);

    if (!image)
        return 0;

    if ((global.grab.pointer.x != image->x) ||
        (global.grab.pointer.y != image->y)) {
        global.grab.pointer.x = image->x;
        global.grab.pointer.y = image->y;
        ret |= (1 << command_pointer);
    }

    uint32_t hash = 0;

    for (int i = 0; i < image->height * image->width; i++)
        hash = (hash << 5) - hash + (uint32_t)image->pixels[i];

    if ((!global.grab.cursor.image) ||
        (hash != global.grab.cursor.hash))
        ret |= (1 << command_cursor);

    if (global.grab.cursor.image)
        XFree(global.grab.cursor.image);

    global.grab.cursor.image = image;
    global.grab.cursor.hash = hash;

    return ret;
}

static uint32_t
grab_image(void)
{
    if (xrandr_resize(global.resize.w, global.resize.h)) {
        XSync(display.id, False);
        return 0;
    }

    int screen = DefaultScreen(display.id);          // XXX
    const int w = DisplayWidth(display.id, screen);  // XXX
    const int h = DisplayHeight(display.id, screen); // XXX

    if ((global.grab.image.info.w != w) ||
        (global.grab.image.info.h != h)) {
        image_delete(&global.grab.image);
        image_create(&global.grab.image, display.root, w, h);
    }

    display.error = 0;

    image_get(&global.grab.image, 0, 0);

    XSync(display.id, False);

    if (display.error)
        return 0;

    if (tycho_set_image(&global.grab.image.info))
        return (1 << command_image);

    return 0;
}

static uint32_t
grab(void)
{
    if (!time_diff(&global.grab.time, CONFIG_GRAB_TIMEOUT))
        return 0;

    uint32_t to_send = grab_image() | grab_cursor();

    for (client_t *c = global.clients; c; c = c->next) {
        if (c->close)
            continue;

        if ((c == global.master.client) &&
            (!c->pointer.sx) &&
            (!c->pointer.sy) &&
            (global.pointer.x == c->pointer.x) &&
            (global.pointer.y == c->pointer.y)) {
            c->pointer.sx = global.grab.pointer.x - c->pointer.x;
            c->pointer.sy = global.grab.pointer.y - c->pointer.y;
            if (c->pointer.sx || c->pointer.sy)
                c->to_send |= (1 << command_pointer_sync);
        }

        c->to_send |= to_send;
    }

    return to_send;
}

static void
client_access(client_t *c, int access, const char *name)
{
    c->to_send |= (1 << command_access);

    if (c->access == access)
        return;

    c->access = access;

    info("%s: access %sed (%s)\n", c->netio.name, access > 0 ? "grant" : "deni", name);

    for (client_t *l = global.clients; l; l = l->next) {
        if (l != c && !str_cmp(l->name, c->name)) {
            l->to_send |= (1 << command_access);
            l->access = 0;
        }
    }

    global.activity.time = time_now();

    if (access != 2)
        client_master_stop(c);
}

static void
client_auth(client_t *c, const char *auth)
{
    if (str_empty(c->name))
        c->level = 0;

    if (c->level) {
        info("%s: authenticated as `%s'\n", c->netio.name, c->name);
        c->recv.mask = (1 << command_access)
                     | (1 << command_control)
                     ;
    } else {
        info("%s: authentication with %s failed\n", c->netio.name, auth);
    }

    int access = c->level;

    if (access == 1)
        access = acl_get(&global.acl, c->name);

    client_access(c, access, "acl");
}

static char *
client_control_extract_word(client_t *c)
{
    char *word = str_skip_space((char *)c->control.recv.read);
    char *word_end = str_skip_char(word);
    word_end[0] = '\0';

    c->control.recv.read += word_end - word;

    if (buffer_read_size(&c->control.recv))
        c->control.recv.read++;

    return word;
}

static void
client_control_acl_list(client_t *c)
{
    if (!global.acl) {
        buffer_from_string(&c->control.send, "empty");
        return;
    }

    size_t size_max = 0;
    size_t count = 0;

    for (acl_t *acl = global.acl; acl; acl = acl->next) {
        size_max = MAX(size_max, str_len(acl->name));
        count++;
    }

    buffer_setup(&c->control.send, NULL, count * (size_max + 3));

    for (acl_t *acl = global.acl; acl; acl = acl->next) {
        size_t size = str_len(acl->name);
        buffer_write_data(&c->control.send, acl->name, size);

        for (; size <= size_max; size++)
            buffer_write(&c->control.send, ' ');

        buffer_write(&c->control.send, acl->level + '0');
        buffer_write(&c->control.send, '\n');
    }
}

static void
client_control_acl(client_t *c)
{
    char *name = client_control_extract_word(c);

    if (str_empty(name)) {
        client_control_acl_list(c);
        return;
    }

    char *level_str = client_control_extract_word(c);

    if (str_len(level_str) != 1) {
        buffer_setup(&c->control.send, NULL, 1);
        buffer_write(&c->control.send, acl_get(&global.acl, name) + '0');
        return;
    }

    int level = level_str[0] - '0';

    if (level < 0 || level > 2) {
        buffer_from_string(&c->control.send, "invalid level");
        return;
    }

    acl_put(&global.acl, name, level);
}

static void
client_control_log(client_t *c)
{
    char *data = (char *)c->control.recv.read;

    while (!str_empty(data)) {
        char *end = str_skip_char_space(data);
        info("%s: %.*s\n", c->netio.name, end - data, data);
        data = str_skip_line(end);
    }
}

static void
client_control_user_list(client_t *c)
{
    size_t size_max_1 = 0;
    size_t size_max_2 = 0;
    size_t count = 0;

    for (client_t *k = global.clients; k; k = k->next) {
        size_max_1 = MAX(size_max_1, str_len(k->netio.name));
        size_max_2 = MAX(size_max_2, str_len(k->name));
        count++;
    }

    buffer_setup(&c->control.send, NULL, count * (size_max_1 + size_max_2 + 4));

    for (client_t *k = global.clients; k; k = k->next) {
        size_t size = str_len(k->netio.name);
        buffer_write_data(&c->control.send, k->netio.name, size);

        for (; size <= size_max_1; size++)
            buffer_write(&c->control.send, ' ');

        size = str_len(k->name);
        buffer_write_data(&c->control.send, k->name, size);

        for (; size <= size_max_2; size++)
            buffer_write(&c->control.send, ' ');

        buffer_write(&c->control.send, k->access + '0');
        buffer_write(&c->control.send, '\n');
    }
}

static void
client_control_user(client_t *c)
{
    char *name = client_control_extract_word(c);

    if (str_empty(name)) {
        client_control_user_list(c);
        return;
    }

    client_t *k = NULL;

    for (client_t *l = global.clients; l; l = l->next) {
        if (!str_cmp(l->name, name)) {
            k = l;
            break;
        }
    }

    if (!k) {
        buffer_from_string(&c->control.send, "invalid user");
        return;
    }

    char *access_str = client_control_extract_word(c);

    if (str_len(access_str) != 1) {
        buffer_setup(&c->control.send, NULL, 1);
        buffer_write(&c->control.send, k->access + '0');
        return;
    }

    int access = access_str[0] - '0';

    if (access < 0 || access > 2) {
        buffer_from_string(&c->control.send, "invalid access");
        return;
    }

    for (client_t *l = global.clients; l; l = l->next) {
        if (!str_cmp(l->name, name))
            client_access(l, access, c->netio.name);
    }
}

static void
client_control_version(client_t *c)
{
    buffer_from_string(&c->control.send, PROG_NAME " " PROG_VERSION);
}

static void
client_control_stat(client_t *c)
{
    char *name = client_control_extract_word(c);

    client_t *client = c;

    if (!str_cmp(name, "master")) {
        if (!global.master.client) {
            buffer_from_string(&c->control.send, "no master");
            return;
        }
        client = global.master.client;
    }

    struct tcp_info tcpi;
    socklen_t len = sizeof(tcpi);

    if (socket_get(client->netio.fd, SOL_TCP, TCP_INFO, &tcpi, &len) == -1 || !len) {
        buffer_from_string(&c->control.send, "not available");
        return;
    }

    char *data = STR_MAKE(
        "client: ", client->netio.name, "\n",
        "rto: ", STR_ULL(tcpi.tcpi_rto), "\n",
        "ato: ", STR_ULL(tcpi.tcpi_ato), "\n",
        "snd_mss: ", STR_ULL(tcpi.tcpi_snd_mss), "\n",
        "rcv_mss: ", STR_ULL(tcpi.tcpi_rcv_mss), "\n",
        "unacked: ", STR_ULL(tcpi.tcpi_unacked), "\n",
        "sacked: ", STR_ULL(tcpi.tcpi_sacked), "\n",
        "lost: ", STR_ULL(tcpi.tcpi_lost), "\n",
        "retrans: ", STR_ULL(tcpi.tcpi_retrans), "\n",
        "fackets: ", STR_ULL(tcpi.tcpi_fackets), "\n",
        "last_data_sent: ", STR_ULL(tcpi.tcpi_last_data_sent), "\n",
        "last_data_recv: ", STR_ULL(tcpi.tcpi_last_data_recv), "\n",
        "pmtu: ", STR_ULL(tcpi.tcpi_pmtu), "\n",
        "rcv_ssthresh: ", STR_ULL(tcpi.tcpi_rcv_ssthresh), "\n",
        "rtt: ", STR_ULL(tcpi.tcpi_rtt), "\n",
        "rttvar: ", STR_ULL(tcpi.tcpi_rttvar), "\n",
        "snd_ssthresh: ", STR_ULL(tcpi.tcpi_snd_ssthresh), "\n",
        "snd_cwnd: ", STR_ULL(tcpi.tcpi_snd_cwnd), "\n",
        "advmss: ", STR_ULL(tcpi.tcpi_advmss), "\n",
        "reordering: ", STR_ULL(tcpi.tcpi_reordering), "\n");

    size_t size = str_len(data) + 1;
    buffer_setup(&c->control.send, data, size);
    c->control.send.write += size;
}

#ifndef NETIO_NO_SSL
static void
client_control_key_load(_unused_ client_t *c)
{
    auth_ssl_load(1);
}

static void
client_control_key_save(_unused_ client_t *c)
{
    auth_ssl_save(1);
}

static void
client_control_key_add(client_t *c)
{
    char *key = client_control_extract_word(c);

    if (str_empty(key)) {
        openssl_verify(c->netio.ssl, 1);
        return;
    }

    openssl_add_cert(key);
}

static void
client_control_key_delete(client_t *c)
{
    char *key = client_control_extract_word(c);

    if (str_empty(key))
        return;

    openssl_delete_cert(key);
}

static void
client_control_key_reset(_unused_ client_t *c)
{
    openssl_set_certs(NULL);
}

static void
client_control_key_list(client_t *c)
{
    char **certs = openssl_get_certs();

    if (!certs)
        return;

    size_t size = 0;

    for (int i = 0; certs[i]; i++)
        size += str_len(certs[i]) + 1;

    if (!size) {
        safe_free(certs);
        return;
    }

    buffer_setup(&c->control.send, NULL, size);

    for (int i = 0; certs[i]; i++) {
        buffer_write_data(&c->control.send, certs[i], str_len(certs[i]));
        buffer_write(&c->control.send, '\n');
        safe_free(certs[i]);
    }

    safe_free(certs);
}

static void
client_control_key(client_t *c)
{
    char *name = client_control_extract_word(c);

    if (str_empty(name)) {
        client_control_key_list(c);
        return;
    }

    struct {
        const char *name;
        void (*func)(client_t *);
    } control[] = {
        {"load", client_control_key_load},
        {"save", client_control_key_save},
        {"add", client_control_key_add},
        {"delete", client_control_key_delete},
        {"reset", client_control_key_reset},
        {NULL, NULL},
    };

    for (int i = 0; control[i].name; i++) {
        if (!str_cmp(name, control[i].name)) {
            control[i].func(c);
            break;
        }
    }
}
#endif

static void
client_control_close(client_t *c)
{
    char *name = client_control_extract_word(c);

    for (client_t *l = global.clients; l; l = l->next) {
        if (l == c)
            continue;
        if (str_empty(name) || !str_cmp(l->name, name))
            l->close = 1;
    }
}

static void
client_control(client_t *c)
{
    char *name = client_control_extract_word(c);

    struct {
        const char *name;
        void (*func)(client_t *);
    } control[] = {
        {"acl", client_control_acl},
        {"log", client_control_log},
        {"user", client_control_user},
        {"info", client_control_version},
        {"version", client_control_version},
        {"tcp", client_control_stat},
        {"stat", client_control_stat},
#ifndef NETIO_NO_SSL
        {"key", client_control_key},
#endif
        {"close", client_control_close},
        {NULL, NULL},
    };

    for (int i = 0; control[i].name; i++) {
        if (!str_cmp(name, control[i].name)) {
            control[i].func(c);
            break;
        }
    }
}

static void
display_event(void)
{
    while (XPending(display.id)) {
        XEvent event;
        XNextEvent(display.id, &event);

        if (input_event(&event))
            continue;

        if (clipboard_event(&event)) {
            client_t *master = global.master.client;
            if (master && !master->clipboard.send.data) {
                uint8_t *data = clipboard_get();
                if (!data)
                    continue;
                size_t size = str_len((char *)data);
                buffer_setup(&master->clipboard.send, data, size);
                master->clipboard.send.write += size;
                master->to_send |= (1 << command_clipboard);
            }
            continue;
        }

        if (xrandr_event(&event))
            continue;
    }
}

static void
main_init(int argc, char **argv)
{
    const char *host = NULL;
    const char *port = CONFIG_PORT;
    option(opt_host, &host, NULL, NULL);
    option(opt_port, &port, NULL, NULL);
    option(opt_port, &port, "port", "port to listen");

#ifndef NETIO_NO_SSL
    const char *ciphers = CONFIG_SSL_CIPHERS;
    int rsa_len = CONFIG_SSL_RSA_LEN;
    int rsa_exp = CONFIG_SSL_RSA_EXP;
    const char *ecdh_curve = CONFIG_SSL_ECDH_CURVE;
    option(opt_list, &ciphers, "ciphers", "sets the list of ciphers");
    option(opt_int, &rsa_len, "rsa-length", "RSA modulus length (in bits)");
    option(opt_int, &rsa_exp, "rsa-exponent", "RSA public exponent");
    option(opt_name, &ecdh_curve, "ecdh-curve", "ECDH curve name");
#endif

    int background = 0;
    option(opt_flag, &background, "background", "run in background");
    option(opt_flag, &global.pam_reinit, "reinit-cred", "reinitialize the user's pam credentials");

    option(opt_int, &global.activity.timeout, "timeout", "inactivity timeout");

    // hidden
    int lock_user = 0;
    unsigned quality_min = CONFIG_QUALITY_MIN;
    unsigned quality_max = CONFIG_QUALITY_MAX;

    option(opt_flag, &lock_user, "lock-user", NULL);
    option(opt_int, &quality_min, "quality-min", NULL);
    option(opt_int, &quality_max, "quality-max", NULL);
    option(opt_name, &global.congestion, "congestion", NULL);

    option_run(argc, argv);

    user_init();
    common_init();
    socket_init();

#ifndef NETIO_NO_SSL
    openssl_init();

    auth_ssl_load(1);
    auth_ssl_setup_rsa(rsa_len, rsa_exp);

    openssl_use_dh();
    openssl_use_ecdh(ecdh_curve);
    openssl_use_ciphers(ciphers);
    openssl_print_error(NULL); // XXX
#endif

    display_init();
    input_init();
    xrandr_init();
    clipboard_init(0);

    XSelectInput(display.id, display.root, StructureNotifyMask);

    if (netio_create(&global.netio, host, port, 1) < 0)
        exit(2);

    tycho_set_quality(quality_min, quality_max);

    if (background) {
        switch (fork()) {
        case -1:
            error("%s: %m\n", "fork");
        case 0:
            if (setsid() == -1)
                warning("%s: %m\n", "setsid");
            break;
        default:
            _exit(0);
        }
    }

    if (lock_user) {
        user_lock();
        if (!user_locked())
            warning("couldn't lock user!\n");
    }

    global.pointer.x = ~0;
    global.pointer.y = ~0;

    info("listening on %s\n", global.netio.name);
}

static void
main_exit()
{
    while (global.clients)
        client_close(global.clients);

    netio_delete(&global.netio);
    image_delete(&global.grab.image);

    input_exit();
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

    int timeout = 1;

    while (running) {
        if (socket_wait(global.netio.fd, SOCKET_WAIT_R, timeout) > 0)
            client_accept();

        timeout = 1;

        display_event();

        if (global.activity.timeout && global.activity.time &&
            time_dt(global.activity.time, time_now()) > global.activity.timeout) {
            for (client_t *l = global.clients; l; l = l->next) {
                if (l->access) {
                    l->to_send |= (1 << command_access);
                    l->access = 0;
                }
            }
            global.activity.time = 0;
        }

        client_t *c = global.clients;

        while (c) {
            buffer_t *const output = &c->netio.output;
            buffer_t *const input = &c->netio.input;

            switch (netio_read(&c->netio)) {
            case 0: goto client_end;
            case -1: break;
            default: timeout = 0;
            }

            while (1) {
                switch (c->recv.command) {

                case command_start:
                    {
                        switch (netio_start(&c->netio)) {
                            case 0: goto client_end;
                            case -1: goto read_end;
                        }

                        info("%s: protocol %s\n", c->netio.name, c->netio.proto);

                        c->recv.mask = (1 << command_auth_ssl)
                                     | (1 << command_auth_pam)
                                     | (1 << command_auth_gss)
                                     ;

                        break;
                    }

                case command_next:
                    {
                        if (c->close) {
                            c->recv.command = command_stop;
                        } else {
                            if (buffer_read_size(input) < 1)
                                goto read_end;

                            c->recv.command = buffer_read(input);
                        }

                        if (c->recv.mask & (1 << c->recv.command))
                            continue;

                        if (c->recv.command != command_stop) {
                            warning("%s: client is not compatible\n", c->netio.name);
                            goto client_end;
                        }

                        continue;
                    }

                case command_auth_ssl:
                    {
                        #ifndef NETIO_NO_SSL
                        if (openssl_verify(c->netio.ssl, 0)) {
                            c->name = STR_MAKE(user_name());
                            c->level = 2;
                        }
                        #endif

                        c->recv.mask &= ~(1 << command_auth_ssl);

                        client_auth(c, "ssl");

                        break;
                    }

                case command_auth_pam:
                    {
                        if (token_recv(&c->auth_pam.name, input))
                            goto read_end;

                        if (token_recv(&c->auth_pam.pass, input))
                            goto read_end;

                        if (!c->auth_pam.name.data || !c->auth_pam.pass.data)
                            goto client_end;

                        if (!auth_pam_ready())
                            goto read_end;

                        if (buffer_read_size(&c->auth_pam.name))
                            c->name = auth_pam_create(&c->auth_pam, global.pam_reinit);

                        if (c->name)
                            c->level = 1 + !str_cmp(user_name(), c->name);

                        auth_pam_print_error(&c->auth_pam, c->netio.name);

                        if (!c->level)
                            auth_pam_delete(&c->auth_pam);

                        client_auth(c, "pam");

                        break;
                    }

                case command_auth_gss:
                    {
                        if (token_recv(&c->gss.recv, input))
                            goto read_end;

                        if (!c->gss.recv.data)
                            goto client_end;

                        const int ret = auth_gss_create(&c->auth_gss, NULL, &c->gss.send, &c->gss.recv);

                        if (buffer_read_size(&c->gss.send)) {
                            c->send.mask |= 1 << command_auth_gss;
                            c->to_send |= 1 << command_auth_gss;
                        }

                        if (!ret)
                            break;

                        if (ret == 1) {
                            c->name = auth_gss_get_name(&c->auth_gss);
                            c->level = auth_gss_get_level(&c->auth_gss, c->name);
                            if (c->auth_gss.cred_file)
                                info("%s: credential cache: %s\n", c->netio.name, c->auth_gss.cred_file);
                        }

                        auth_gss_print_error(&c->auth_gss, c->netio.name);

                        if (ret == -1)
                            auth_gss_delete(&c->auth_gss);

                        client_auth(c, "gss");

                        break;
                    }

                case command_access:
                    {
                        if (!c->access)
                            break;

                        c->recv.mask = (1 << command_pointer)
                                     | (1 << command_pointer_sync)
                                     | (1 << command_button)
                                     | (1 << command_key)
                                     | (1 << command_image)
                                     | (1 << command_quality)
                                     | (1 << command_resize)
                                     | (1 << command_clipboard)
                                     | (1 << command_access)
                                     | (1 << command_control)
                                     ;

                        uint32_t to_send = (1 << command_image)
                                         | (1 << command_pointer)
                                         | (1 << command_cursor)
                                         ;

                        c->send.mask |= to_send;
                        c->to_send |= to_send;

                        client_master(c);

                        break;
                    }

                case command_quality:
                    {
                        if (buffer_read_size(input) < 2)
                            goto read_end;

                        const unsigned min = buffer_read(input);
                        const unsigned max = buffer_read(input);

                        if (global.master.client != c)
                            break;

                        global.activity.time = time_now();

                        debug("%s: set quality to min=%u max=%u\n", c->netio.name, min, max);
                        tycho_set_quality(min, max);

                        break;
                    }

                case command_image:
                    {
                        if (c->image_count)
                            c->image_count--;

                        if (c->access)
                            c->send.mask |= (1 << command_image);

                        break;
                    }

                case command_control:
                    {
                        if (token_recv(&c->control.recv, input))
                            goto read_end;

                        if (!c->control.recv.data)
                            goto client_end;

                        if (c->control.send.data)
                            goto read_end;

                        if (c->level == 2)
                            client_control(c);

                        safe_free(c->control.recv.data);

                        if (c->control.send.data)
                            c->to_send |= (1 << command_control);

                        byte_set(&c->control.recv, 0, sizeof(buffer_t));

                        break;
                    }

                case command_pointer:
                    {
                        if (buffer_read_size(input) < 4)
                            goto read_end;

                        const int16_t x_old = c->pointer.x;
                        const int16_t y_old = c->pointer.y;

                        c->pointer.x = buffer_read_16(input);
                        c->pointer.y = buffer_read_16(input);

                        if (!client_master(c))
                            break;

                        global.activity.time = time_now();

                        if ((global.pointer.x == x_old) && (global.pointer.y == y_old)) {
                            input_pointer(c->pointer.x - x_old, c->pointer.y - y_old, 1);
                        } else {
                            input_pointer(c->pointer.x, c->pointer.y, 0);
                            c->pointer.sx = 0;
                            c->pointer.sy = 0;
                        }

                        global.pointer.x = c->pointer.x;
                        global.pointer.y = c->pointer.y;

                        break;
                    }

                case command_pointer_sync:
                    {
                        if (buffer_read_size(input) < 4)
                            goto read_end;

                        c->pointer.x += (int16_t)buffer_read_16(input);
                        c->pointer.y += (int16_t)buffer_read_16(input);

                        c->pointer.sx = 0;
                        c->pointer.sy = 0;

                        break;
                    }

                case command_button:
                    {
                        if (buffer_read_size(input) < 2)
                            goto read_end;

                        const uint8_t button = buffer_read(input);
                        const int press = buffer_read(input);

                        if (!client_master(c))
                            break;

                        global.activity.time = time_now();

                        input_button(button, press);

                        break;
                    }

                case command_key:
                    {
                        if (buffer_read_size(input) < 7)
                            goto read_end;

                        const int ucs = buffer_read(input);
                        const uint32_t symbol = buffer_read_32(input);
                        const uint8_t key = buffer_read(input);
                        const int press = buffer_read(input);

                        if (!client_master(c))
                            break;

                        global.activity.time = time_now();

                        if (key) {
                            input_key(key, ucs ? ucs_to_keysym(symbol) : symbol, press);
                        } else {
                            input_release();
                        }

                        break;
                    }

                case command_resize:
                    {
                        if (buffer_read_size(input) < 4)
                            goto read_end;

                        const unsigned w = buffer_read_16(input);
                        const unsigned h = buffer_read_16(input);

                        if (global.master.client != c)
                            break;

                        global.activity.time = time_now();

                        debug("%s: set resize to %ux%u\n", c->netio.name, w, h);

                        global.resize.w = w;
                        global.resize.h = h;

                        break;
                    }

                case command_clipboard:
                    {
                        if (token_recv(&c->clipboard.recv, input))
                            goto read_end;

                        if (!c->clipboard.recv.data)
                            goto client_end;

                        if (global.master.client == c) {
                            clipboard_set(c->clipboard.recv.data);
                        } else {
                            safe_free(c->clipboard.recv.data);
                        }

                        byte_set(&c->clipboard.recv, 0, sizeof(buffer_t));

                        break;
                    }

                default:
                case command_stop:
                    buffer_format(input);
                    goto read_end;
                }

                c->recv.command = command_next;
            }

        read_end:
            if (global.master.client == c)
                grab();

            while (1) {
                switch (c->send.command) {

                case command_start:
                    c->send.mask = (1 << command_master)
                                 | (1 << command_pointer_sync)
                                 | (1 << command_clipboard)
                                 | (1 << command_access)
                                 | (1 << command_control)
                                 ;
                    break;

                case command_next:
                    {
                        if (buffer_write_size(output) < 1)
                            goto write_end;

                        const uint32_t send = c->to_send & c->send.mask;

                        if (send) {
                            c->send.command = CTZ(send);
                            buffer_write(output, c->send.command);
                            c->to_send &= ~(1 << c->send.command);
                        } else {
                            if (c->recv.command != command_stop)
                                goto write_end;
                            c->send.command = command_stop;
                        }

                        continue;
                    }

                case command_auth_gss:
                    {
                        if (token_send(output, &c->gss.send))
                            goto write_end;

                        c->send.mask &= ~(1 << command_auth_gss);

                        break;
                    }

                case command_access:
                    {
                        if (buffer_write_size(output) < 2)
                            goto write_end;

                        buffer_write(output, c->level);
                        buffer_write(output, c->access);

                        break;
                    }

                case command_master:
                    {
                        if (buffer_write_size(output) < 1)
                            goto write_end;

                        buffer_write(output, global.master.client == c);

                        break;
                    }

                case command_pointer:
                    {
                        if (buffer_write_size(output) < 4)
                            goto write_end;

                        buffer_write_16(output, global.grab.pointer.x);
                        buffer_write_16(output, global.grab.pointer.y);

                        break;
                    }

                case command_pointer_sync:
                    {
                        if (buffer_write_size(output) < 4)
                            goto write_end;

                        buffer_write_16(output, c->pointer.sx);
                        buffer_write_16(output, c->pointer.sy);

                        break;
                    }

                case command_cursor:
                    {
                        if (buffer_write_size(output) < 12)
                            goto write_end;

                        if (global.grab.cursor.image) {
                            uint32_t hash = global.grab.cursor.hash;

                            buffer_write_32(output, hash);

                            const unsigned w = global.grab.cursor.image->width;
                            const unsigned h = global.grab.cursor.image->height;

                            buffer_write_16(output, w);
                            buffer_write_16(output, h);

                            buffer_write_16(output, global.grab.cursor.image->xhot);
                            buffer_write_16(output, global.grab.cursor.image->yhot);

                            if (!hash)
                                break;

                            int i = 0;

                            for (; i < 256; i++) {
                                if (hash == c->cursor.hash[i])
                                    break;
                            }

                            if (i < 256)
                                break;

                            for (int k = 255; k; k--)
                                c->cursor.hash[k] = c->cursor.hash[k - 1];

                            c->cursor.hash[0] = hash;

                            buffer_setup(&c->cursor.send, NULL, w * h * 4);

                            for (unsigned k = 0; k < w * h; k++)
                                buffer_write_32(&c->cursor.send, global.grab.cursor.image->pixels[k]);

                        } else { // XXX
                            buffer_write_32(output, 0);
                            buffer_write_32(output, 0);
                            buffer_write_32(output, 0);
                            break;
                        }

                        c->send.command++;
                    }
                    /* FALLTHRU */

                case command_cursor_data:
                    {
                        buffer_copy(output, &c->cursor.send);

                        if (buffer_read_size(&c->cursor.send))
                            goto write_end;

                        safe_free(c->cursor.send.data);

                        byte_set(&c->cursor.send, 0, sizeof(buffer_t));

                        break;
                    }

                case command_image:
                    {
                        if (buffer_write_size(output) < 4)
                            goto write_end;

                        buffer_write_16(output, global.grab.image.info.w);
                        buffer_write_16(output, global.grab.image.info.h);

                        tycho_setup_server(&c->tycho);

                        c->image_count++;
                        c->send.command++;
                    }
                    /* FALLTHRU */

                case command_image_data:
                    {
                        if (tycho_send(&c->tycho, output))
                            goto write_end;

                        if (output->read != output->write)
                            goto write_end;

                        if (c->image_count >= 2)
                            c->send.mask &= ~(1 << command_image);

                        break;
                    }

                case command_control:
                    {
                        if (token_send(output, &c->control.send))
                            goto write_end;

                        break;
                    }

                case command_clipboard:
                    {
                        if (token_send(output, &c->clipboard.send))
                            goto write_end;

                        break;
                    }

                default:
                case command_stop:
                    {
                        if (buffer_read_size(output))
                            goto write_end;

                        if (netio_stop(&c->netio) == -1)
                            goto write_end;

                        goto client_end;
                    }
                }

                c->send.command = command_next;
            }

        write_end:
            if (!netio_write(&c->netio))
                goto client_end;

            c = c->next;
            continue;

        client_end:
            c = client_close(c);
        }

        if (global.clients && grab())
            timeout = 0;
    }

    return 0;
}
