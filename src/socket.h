#pragma once

#include "common.h"

#define SOCKET_WAIT_R  (1<<0)
#define SOCKET_WAIT_W  (1<<1)
#define SOCKET_WAIT_RW (SOCKET_WAIT_R|SOCKET_WAIT_W)

void socket_init    (void);
void socket_exit    ();
int  socket_wait    (int, int, int);
int  socket_set     (int, int, int, const void *, socklen_t);
int  socket_get     (int, int, int, void *, socklen_t *);
int  socket_set_int (int, int, int, int);
void socket_setup   (int);
int  socket_error   (int);
void socket_close   (int);
