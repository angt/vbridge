#pragma once

#include "common.h"
#include "netio.h"
#include "token.h"
#include "tycho-client.h"
#include "command.h"

typedef struct core_client core_client_t;
typedef struct buffer_list buffer_list_t;
typedef struct cursor cursor_t;

struct core_client {
    netio_t netio;
    tycho_t tycho;

    image_info_t image;

    struct {
        uint64_t time;
        uint64_t timeout;
    } send;

    uint64_t image_time;

    struct {
        int w, h;
    } size;

    struct {
        int x, y;
        int px, py;
        int sx, sy;
    } pointer;

    struct {
        int x, y;
        int w, h;
        int serial;
        cursor_t *cache;
    } cursor;

    int level;
    int access;
    int master;

    buffer_list_t *buffers;

    struct {
        buffer_t recv;
    } gss, control, clipboard;

    struct {
        command_t command;
        uint32_t mask;
    } recv;

    int error;
};

int       core_create            (core_client_t *, const char *, const char *);
void      core_delete            (core_client_t *);
void      core_send              (core_client_t *, command_t);
void      core_send_data         (core_client_t *, command_t, const void *, size_t);
void      core_send_quality      (core_client_t *, unsigned, unsigned);
void      core_send_resize       (core_client_t *, unsigned, unsigned);
void      core_send_pointer      (core_client_t *, int, int, int);
void      core_send_button       (core_client_t *, unsigned, int);
void      core_send_key          (core_client_t *, int, unsigned, int, int);
void      core_send_auth         (core_client_t *, const char *, const char *);
int       core_send_all          (core_client_t *);
uint32_t *core_recv_cursor       (core_client_t *);
void     *core_recv_control      (core_client_t *);
void     *core_recv_clipboard    (core_client_t *);
int       core_recv_all          (core_client_t *);
int       core_received          (core_client_t *, command_t);
