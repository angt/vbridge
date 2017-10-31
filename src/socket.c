#include "socket.h"

void
socket_init(void)
{
#ifdef _WIN32
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data))
        warning("WSAStartup() failed\n");
#endif
}

void
socket_exit()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int
socket_wait(int fd, int events, int ms)
{
#ifdef _WIN32
    struct timeval tv;
    struct timeval *ptv = NULL;

    fd_set fds[2];
    FD_ZERO(&fds[0]);
    FD_ZERO(&fds[1]);

    if (events & SOCKET_WAIT_R)
        FD_SET(fd, &fds[0]);

    if (events & SOCKET_WAIT_W)
        FD_SET(fd, &fds[1]);

    if (ms >= 0) {
        ptv = &tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }

    int ret = select(fd + 1, &fds[0], &fds[1], NULL, ptv);
#else
    const short tr[] = {
            [SOCKET_WAIT_R] = POLLIN,
            [SOCKET_WAIT_W] = POLLOUT,
            [SOCKET_WAIT_RW] = POLLIN | POLLOUT,
    };

    struct pollfd pollfd = {
        .fd = fd,
        .events = tr[events],
    };

    int ret = poll(&pollfd, 1, ms);
#endif

    return ret;
}

int
socket_set(int fd, int level, int name, const void *val, socklen_t len)
{
    int ret = setsockopt(fd, level, name, val, len);

    if (ret == -1)
        warning("%s: %m\n", "setsockopt");

    return ret;
}

int
socket_get(int fd, int level, int name, void *val, socklen_t *len)
{
    int ret = getsockopt(fd, level, name, val, len);

    if (ret == -1)
        warning("%s: %m\n", "getsockopt");

    return ret;
}

int
socket_set_int(int fd, int level, int optname, int opt)
{
    return socket_set(fd, level, optname, &opt, sizeof(opt));
}

static void
socket_set_nonblock(int fd)
{
    int ret = 0;

#ifdef _WIN32
    unsigned long opt = 1;
    ret = ioctlsocket(fd, FIONBIO, &opt);
#else
    int opt = 1;
    do {
        ret = ioctl(fd, FIONBIO, &opt);
    } while (ret == -1 && errno == EINTR);
#endif

    if (ret)
        warning("FIONBIO failed\n");
}

static void
socket_set_cloexec(int fd)
{
#ifdef _WIN32
// XXX TODO
#else
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        warning("%s: %m\n", "fcntl");
#endif
}

void
socket_setup(int fd)
{
    socket_set_nonblock(fd);
    socket_set_cloexec(fd);
    socket_set_int(fd, IPPROTO_TCP, TCP_NODELAY, 1);
}

int
socket_error(int ret)
{
    if (!ret)
        return 0;

#ifdef _WIN32
    if (WSAGetLastError() != WSAEWOULDBLOCK)
        return -1;
#else
    if ((errno != EINPROGRESS) && (errno != EINTR))
        return -1;
#endif

    errno = 0;
    return 0;
}

void
socket_close(int fd)
{
    if (fd < 0)
        return;

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}
