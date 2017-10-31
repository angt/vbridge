#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef SSIZE_T_DEFINED
#define SSIZE_T_DEFINED 1
#endif

#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <stdarg.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/time.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#define _1_(x) (__builtin_expect((x), 1))
#define _0_(x) (__builtin_expect((x), 0))
#define CLZ(x) (__builtin_clz(x))
#define CTZ(x) (__builtin_ctz(x))

#define _pure_       __attribute__((pure))
#define _const_      __attribute__((const))
#define _unused_     __attribute__((unused))
#define _noreturn_   __attribute__((noreturn))
#define _malloc_     __attribute__((malloc))
#define _align_(...) __attribute__((aligned(__VA_ARGS__)))

#ifdef __clang__
#define _alloc_(...)
#else
#define _alloc_(...) __attribute__((alloc_size(__VA_ARGS__)))
#endif

#define COUNT(x) (sizeof(x)/sizeof(x[0]))

#define FLAG(x,y) ((x)&(y)?#y:"")

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#define DIV(x,y) (((x)+(y)-1)/(y))

#define CLAMP(x,m,M) (MIN(MAX(x,m),M))

#ifdef ALIGN
#undef ALIGN
#endif

#define ALIGN_SIZE    16
#define ALIGN_MASK    (ALIGN_SIZE-1)

#define ALIGN(x)      (((x)+ALIGN_MASK)&~ALIGN_MASK)
#define ALIGN_DOWN(x) ((x)&~ALIGN_MASK)

#define PALIGN(x)      ((void *)ALIGN((size_t)(x)))
#define PALIGN_DOWN(x) ((void *)ALIGN_DOWN((size_t)(x)))

#define TSET(T) gettimeofday(&_timer##T, NULL);
#define TINI(T) struct timeval _timer##T; TSET(T)
#define TSEC(T) ((double)(_timer##T.tv_sec+_timer##T.tv_usec*1e-6))

#define SIZE_OK(S) ((S)&&((S)!=SIZE_MAX))

/*
#define TSET(T) clock_gettime(CLOCK_MONOTONIC, &_timer##T);
#define TINI(T) struct timespec _timer##T; TSET(T)
#define TSEC(T) ((double)(_timer##T.tv_sec+_timer##T.tv_nsec*1e-9))
*/

#define TDIF(T,U) (TSEC(U)-TSEC(T))

#define STR_FREE &str_free
#define STR_ULL(x) str_ull((unsigned long long)(x)), STR_FREE
#define STR_LL(x) str_ll((long long)(x)), STR_FREE
#define STR_MAKE(...) str_make((const char *[]){__VA_ARGS__, NULL})

typedef struct image_info image_info_t;

struct image_info {
    uint32_t *data;
    int stride;
    int w, h;
};

typedef struct buffer buffer_t;

struct buffer {
    uint8_t *data;
    uint8_t *end;
    uint8_t *write;
    uint8_t *read;
};

extern volatile int running;

void error   (const char *, ...) _noreturn_;
void warning (const char *, ...);
void info    (const char *, ...);
void print   (const char *, ...);
void debug   (const char *, ...);

int safe_open  (const char *, int, ...);
int safe_close (int);

size_t safe_write (int, const void *, size_t);
size_t safe_read  (int, void *, size_t);

void safe_delete_file (const char *);

void *safe_malloc  (size_t)         _alloc_(1)   _malloc_;
void *safe_calloc  (size_t, size_t) _alloc_(1,2) _malloc_;
void *safe_realloc (void *, size_t) _alloc_(2);
void *safe_free    (void *);

void common_init (void);

uint64_t time_now  (void);
uint64_t time_dt   (uint64_t, uint64_t);
int      time_diff (uint64_t *, uint64_t);

void safe_free_strs (char **);

extern const char str_free;

char *str_make (const char **)      _malloc_;
char *str_ull  (unsigned long long) _malloc_;
char *str_ll   (long long)          _malloc_;
