#include "netio.h"
#include "buffer-static.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static char *
netio_get_name(struct sockaddr *addr, socklen_t addrlen)
{
    char ip[64] = {0};
    char port[32] = {0};

    int ret = getnameinfo(addr, addrlen,
                          ip, sizeof(ip),
                          port, sizeof(port),
                          NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret)
        return NULL;

    if (str_contains(ip, ':'))
        return STR_MAKE("[", ip, "]:", port);

    return STR_MAKE(ip, ":", port);
}

int
netio_create(netio_t *netio, const char *host, const char *port, int listener)
{
    if (!netio)
        return -1;

#ifdef __EMSCRIPTEN__
    netio->fd = 0;
    netio->name = STR_MAKE("client");
#else

    struct addrinfo hints, *res, *ai;

    byte_set(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE; // XXX hack

    netio->fd = -1;

    if (str_empty(port)) {
        warning("empty port is not valid\n");
        return -1;
    }

    int ret = getaddrinfo(host, port, &hints, &res);

    if (ret) {
        warning("host not found\n");
        return -1;
    }

    for (ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

        if (fd == -1)
            continue;

        char *name = netio_get_name(ai->ai_addr, ai->ai_addrlen);

        socket_setup(fd);

        if (listener) {
            socket_set_int(fd, SOL_SOCKET, SO_REUSEADDR, 1);
            ret = bind(fd, ai->ai_addr, ai->ai_addrlen);
            if (!ret)
                ret = listen(fd, 1);
        } else {
            ret = socket_error(connect(fd, ai->ai_addr, ai->ai_addrlen));
        }

        if (!ret) {
            netio->fd = fd;
            netio->name = name;
            break;
        }

        if (errno && name)
            warning("%s: %m\n", name);

        safe_free(name);
        socket_close(fd);
    }

    freeaddrinfo(res);
#endif

    return netio->fd;
}

int
netio_accept(netio_t *netio, netio_t *listener)
{
    if (!netio || !listener)
        return -1;

#ifndef __EMSCRIPTEN__
    struct sockaddr_storage addr_storage;
    struct sockaddr *addr = (struct sockaddr *)&addr_storage;
    socklen_t addrlen = sizeof(addr_storage);

    netio->fd = accept(listener->fd, addr, &addrlen);

    if (netio->fd == -1) {
        warning("%s: %m\n", "accept");
        return -1;
    }

    netio->state = NETIO_ACCEPT;
    netio->name = netio_get_name(addr, addrlen);

    socket_setup(netio->fd);
#endif

    return netio->fd;
}

void
netio_delete(netio_t *netio)
{
    if (!netio)
        return;

#ifndef NETIO_NO_SSL
    openssl_delete(netio->ssl);
#endif

#ifndef __EMSCRIPTEN__
    socket_close(netio->fd);
#endif

    safe_free(netio->name);
    safe_free(netio->proto);
    safe_free(netio->input.data);
    safe_free(netio->output.data);

    byte_set(netio, 0, sizeof(netio_t));
    netio->fd = -1;
}

int
netio_start(netio_t *netio)
{
    if (!netio)
        return 0;

    if (netio->state & NETIO_READY)
        return 1;

    if (!netio->state) {
#ifndef __EMSCRIPTEN__
        if (socket_wait(netio->fd, SOCKET_WAIT_W, 0) != 1)
            return -1;

        int optval = 0;
        socklen_t optlen = sizeof(optval);
        socket_get(netio->fd, SOL_SOCKET, SO_ERROR, &optval, &optlen);

        if (optval) {
            errno = optval;
            warning("%s: %m\n", netio->name);
            return 0;
        }
#endif

        netio->state |= NETIO_START;
    }

#ifndef NETIO_NO_SSL
    if (!netio->ssl)
        netio->ssl = openssl_create(netio->fd, netio->state & NETIO_ACCEPT);

    if (!netio->ssl)
        return 0;

    int ret = SSL_do_handshake(netio->ssl);

    if (ret <= 0) {
        switch (SSL_get_error(netio->ssl, ret)) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return -1;
        case SSL_ERROR_SYSCALL:
            if (ret == -1 && errno)
                warning("%s: %m\n", netio->name);
        }
        openssl_print_error(netio->name);
        return 0;
    }

    netio->proto = STR_MAKE(SSL_get_version(netio->ssl), " (",
                            SSL_CIPHER_get_name(SSL_get_current_cipher(netio->ssl)), ")");
#else

#ifndef __EMSCRIPTEN__
    netio->proto = STR_MAKE("TCP");
#else
    netio->proto = STR_MAKE("WebSocket");
#endif

#endif

    netio->state |= NETIO_READY;

    buffer_setup(&netio->input, NULL, 16384);
    buffer_setup(&netio->output, NULL, 16384);

    return 1;
}

int
netio_stop(netio_t *netio)
{
    if (!netio)
        return 0;

#ifndef NETIO_NO_SSL
    if (!netio->ssl)
        return 1;

    int ret = SSL_shutdown(netio->ssl);

    if (ret <= 0) {
        switch (SSL_get_error(netio->ssl, ret)) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return -1;
        case SSL_ERROR_SYSCALL:
            if (ret == -1 && errno)
                warning("%s: %m\n", netio->name);
        }
        openssl_print_error(netio->name);
        return 0;
    }
#endif

    return 1;
}

int
netio_read(netio_t *netio)
{
    if (!netio)
        return 0;

    if ((netio->state & (NETIO_READY | NETIO_WRITE_WANT_READ)) != NETIO_READY)
        return -1;

    buffer_t *buffer = &netio->input;

    buffer_shift(buffer);

    size_t size = buffer_write_size(buffer);

    if (!size)
        return -1;

    int ret = 0;

#ifdef __EMSCRIPTEN__
    ret = EM_ASM_INT({
        return do_netio_read($0, $1);
    }, buffer->write, size);

    if (ret < 0) {
        int closed = EM_ASM_INT_V({
            return is_netio_closed();
        });

        if (closed)
            return 0;

        return -1;
    }
#else
#ifndef NETIO_NO_SSL
    netio->state &= ~NETIO_READ_WANT_WRITE;

    ret = SSL_read(netio->ssl, buffer->write, size);

    if (ret <= 0) {
        switch (SSL_get_error(netio->ssl, ret)) {
        case SSL_ERROR_WANT_WRITE:
            netio->state |= NETIO_READ_WANT_WRITE;
        case SSL_ERROR_WANT_READ:
            return -1;
        case SSL_ERROR_SYSCALL:
            if (ret == -1 && errno)
                warning("%s: %m\n", netio->name);
        }
        openssl_print_error(netio->name);
        return 0;
    }
#else
    ret = read(netio->fd, buffer->write, size);

    if (ret == -1) {
        if (errno == EAGAIN || errno == EINTR) // TODO win
            return -1;
        if (errno)
            warning("%s: %m\n", netio->name);
        return 0;
    }
#endif
#endif

    buffer->write += ret;

    return 1;
}

int
netio_write(netio_t *netio)
{
    if (!netio)
        return 0;

    if ((netio->state & (NETIO_READY | NETIO_READ_WANT_WRITE)) != NETIO_READY)
        return -1;

    buffer_t *buffer = &netio->output;

    size_t size = buffer_read_size(buffer);

    if (!size)
        return -1;

    int ret = 0;

#ifdef __EMSCRIPTEN__
    ret = EM_ASM_INT({
        return do_netio_write($0, $1);
    }, buffer->read, size);

    if (ret < 0)
        return -1;
#else
#ifndef NETIO_NO_SSL
    netio->state &= ~NETIO_WRITE_WANT_READ;

    ret = SSL_write(netio->ssl, buffer->read, size);

    if (ret <= 0) {
        switch (SSL_get_error(netio->ssl, ret)) {
        case SSL_ERROR_WANT_READ:
            netio->state |= NETIO_WRITE_WANT_READ;
        case SSL_ERROR_WANT_WRITE:
            return -1;
        case SSL_ERROR_SYSCALL:
            if (ret == -1 && errno)
                warning("%s: %m\n", netio->name);
        }
        openssl_print_error(netio->name);
        return 0;
    }
#else
    ret = write(netio->fd, buffer->read, size); // TODO win

    if (ret == -1) {
        if (errno == EAGAIN || errno == EINTR)
            return -1;
        if (errno)
            warning("%s: %m\n", netio->name);
        return 0;
    }
#endif
#endif

    buffer->read += ret;

    buffer_shift(buffer);

    return 1;
}
