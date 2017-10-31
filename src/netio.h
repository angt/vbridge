#pragma once

#include "socket.h"

#ifndef NETIO_NO_SSL
#include "openssl.h"
#endif

#define NETIO_START            (1<<0)
#define NETIO_READY            (1<<1)
#define NETIO_ACCEPT           (1<<2)
#define NETIO_READ_WANT_WRITE  (1<<3)
#define NETIO_WRITE_WANT_READ  (1<<4)

typedef struct netio netio_t;

struct netio {
    int fd;
    int state;
    char *name;
    char *proto;
#ifndef NETIO_NO_SSL
    SSL *ssl;
#endif
    buffer_t input;
    buffer_t output;
};

int  netio_create (netio_t *, const char *, const char *, int);
int  netio_accept (netio_t *, netio_t *);
void netio_delete (netio_t *);
int  netio_start  (netio_t *);
int  netio_stop   (netio_t *);
int  netio_read   (netio_t *);
int  netio_write  (netio_t *);
