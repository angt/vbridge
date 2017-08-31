#include "common-static.h"

volatile int running = 1;

static void
log_va(int fd, const char *format, va_list ap)
{
    if (fd < 0 || str_empty(format))
        return;

    const size_t size = 4096;
    static char *buffer = NULL;

    if (!buffer)
        buffer = safe_malloc(size);

    int len = vsnprintf(buffer, size, format, ap);

    if (len > 0 && size > (size_t)len)
        safe_write(fd, buffer, len);
}

_noreturn_ void
error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_va(STDERR_FILENO, format, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

void
warning(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_va(STDERR_FILENO, format, ap);
    va_end(ap);
}

void
info(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_va(STDERR_FILENO, format, ap);
    va_end(ap);
}

void
print(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_va(STDOUT_FILENO, format, ap);
    va_end(ap);
}

void
debug(const char *format, ...)
{
#ifdef NDEBUG
    (void)format;
#else
    va_list ap;
    va_start(ap, format);
    log_va(STDOUT_FILENO, format, ap);
    va_end(ap);
#endif
}

int
safe_open(const char *filename, int flags, ...)
{
    if (str_empty(filename))
        return -1;

    va_list ap;
    int mode = 0;

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    int ret = -1;

    do {
        ret = open(filename, flags | O_CLOEXEC, mode);
    } while (ret == -1 && errno == EINTR);

    if (ret == -1 && errno != ENOENT)
        warning("%s(%s): %m\n", "open", filename);

    return ret;
}

int
safe_close(int fd)
{
    if (fd >= 0)
        close(fd);

    return -1;
}

size_t
safe_write(int fd, const void *data, size_t size)
{
    if (fd < 0)
        return 0;

    size_t total = 0;

    while (total < size) {
        ssize_t ret = write(fd, (char *)data + total, size - total);

        if (ret <= 0) {
            if (ret == -1 && (errno == EAGAIN || errno == EINTR))
                continue;
            break;
        }

        total += ret;
    }

    return total;
}

size_t
safe_read(int fd, void *data, size_t size)
{
    if (fd < 0)
        return 0;

    size_t total = 0;

    while (total < size) {
        ssize_t ret = read(fd, (char *)data + total, size - total);

        if (ret <= 0) {
            if (ret == -1 && (errno == EAGAIN || errno == EINTR))
                continue;
            break;
        }

        total += ret;
    }

    return total;
}

void
safe_delete_file(const char *filename)
{
    if (str_empty(filename))
        return;

    if (unlink(filename) == -1 && errno != ENOENT)
        warning("%s(%s): %m\n", "unlink", filename);
}

void *
safe_free(void *data)
{
    if (data) // useless
        free(data);

    return NULL;
}

void *
safe_malloc(size_t size)
{
    if (!size)
        return NULL;

    void *ret = malloc(size);

    if (!ret)
        abort();

    return ret;
}

void *
safe_realloc(void *ptr, size_t size)
{
    void *ret = realloc(ptr, size);

    if (!ret && size)
        abort();

    return ret;
}

void *
safe_calloc(size_t n, size_t size)
{
    if (!size || !n)
        return NULL;

    if (n > SIZE_MAX / size)
        abort();

    void *ret = calloc(n, size);

    if (!ret)
        abort();

    return ret;
}

static void
stop_running();

static void
signal_init(void)
{
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
#ifdef SIGHUP
    signal(SIGHUP, SIG_IGN);
#endif
    signal(SIGTERM, stop_running);
    signal(SIGINT, stop_running);
}

static void
stop_running()
{
    running = 0;
    signal_init();
}

void
common_init(void)
{
#ifndef _WIN32
    umask(0077);
    signal_init();
#endif
}

uint64_t
time_now(void)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000ULL;
#else

#ifdef __linux__
    static int ret;
    if (!ret) {
        struct timespec ts;
        ret = clock_gettime(6, &ts);
        return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
    }
#endif

    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
#endif
}

uint64_t
time_dt(uint64_t a, uint64_t b)
{
    return (a > b) ? ULLONG_MAX : (b - a);
}

int
time_diff(uint64_t *time, uint64_t period)
{
    if (!time)
        return 0;

    uint64_t now = time_now();

    if (time_dt(*time, now) < period)
        return 0;

    *time = now;

    return 1;
}

void
safe_free_strs(char **str)
{
    if (!str)
        return;

    for (int i = 0; str[i]; i++)
        free(str[i]);

    free(str);
}

char *
str_ll(long long n)
{
    char data[32];
    size_t k = sizeof(data);

    data[--k] = 0;

    int neg = 0;

    if (n < 0ll) {
        neg = 1;
        n = -n;
    }

    do {
        data[--k] = '0' + (n % 10ll);
        n /= 10ll;
    } while (n);

    if (neg)
        data[--k] = '-';

    return byte_dup(&data[k], sizeof(data) - k);
}

char *
str_ull(unsigned long long n)
{
    char data[32];
    size_t k = sizeof(data);

    data[--k] = 0;

    do {
        data[--k] = '0' + (n % 10ull);
        n /= 10ull;
    } while (n);

    return byte_dup(&data[k], sizeof(data) - k);
}

const char str_free = 0;

char *
str_make(const char **str)
{
    size_t size = 1;

    for (int i = 0; str[i]; i++)
        size += str_len(str[i]);

    char *data = safe_malloc(size);
    char *ret = data;

    for (int i = 0; str[i]; i++) {
        if (&str_free == str[i]) {
            if (i)
                safe_free((char *)str[i - 1]);
        } else {
            size_t len = str_len(str[i]);
            byte_copy(data, str[i], len);
            data += len;
        }
    }

    data[0] = 0;

    return ret;
}
