/* OnionStation5: PS5 workarounds linked around Tor, libevent and OpenSSL.
 *
 * TokenStation5's WireGuard work found that fcntl(F_SETFL, O_NONBLOCK) doesn't
 * make a PS5 socket non-blocking, while ioctl(FIONBIO) does. Rather than patch
 * every library, the link wraps fcntl, socket and socketpair
 * (-Wl,--wrap=..., payload/build.sh) and these wrappers apply the
 * non-blocking flag both ways. On a console where fcntl works, the extra
 * ioctl changes nothing.
 *
 * Copyright (c) 2026, ThatRetiredDude. BSD-3-Clause, see LICENSE. */

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "start.h"

#ifndef FIONBIO
#include <sys/filio.h>
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

int __real_fcntl(int fd, int cmd, ...);
int __real_socket(int domain, int type, int protocol);
int __real_socketpair(int domain, int type, int protocol, int sv[2]);

static void set_fionbio(int fd, int on)
{
    ioctl(fd, FIONBIO, &on);
}

int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    int rc;
    switch (cmd) {
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW: {
        void *lock = va_arg(ap, void *);
        rc = __real_fcntl(fd, cmd, lock);
        break;
    }
    case F_GETFD:
    case F_GETFL:
#ifdef F_GETOWN
    case F_GETOWN:
#endif
        rc = __real_fcntl(fd, cmd);
        break;
    default: {
        int arg = va_arg(ap, int);
        if (cmd == F_SETFL)
            set_fionbio(fd, (arg & O_NONBLOCK) != 0);
        rc = __real_fcntl(fd, cmd, arg);
        break;
    }
    }
    va_end(ap);
    return rc;
}

/* SOCK_NONBLOCK and SOCK_CLOEXEC in the type are applied after the socket is
 * made, the way that works on the PS5. */
static void split_flags(int *type, int *nonblock, int *cloexec)
{
    *nonblock = (*type & SOCK_NONBLOCK) != 0;
    *cloexec = (*type & SOCK_CLOEXEC) != 0;
    *type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
}

static void apply_flags(int fd, int nonblock, int cloexec)
{
    if (nonblock) {
        set_fionbio(fd, 1);
        int fl = __real_fcntl(fd, F_GETFL);
        if (fl >= 0)
            __real_fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    if (cloexec)
        __real_fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int __wrap_socket(int domain, int type, int protocol)
{
    int nonblock, cloexec;
    split_flags(&type, &nonblock, &cloexec);
    int fd = __real_socket(domain, type, protocol);
    if (fd >= 0)
        apply_flags(fd, nonblock, cloexec);
    return fd;
}

int __wrap_socketpair(int domain, int type, int protocol, int sv[2])
{
    int nonblock, cloexec;
    split_flags(&type, &nonblock, &cloexec);
    int rc = __real_socketpair(domain, type, protocol, sv);
    if (rc == 0) {
        apply_flags(sv[0], nonblock, cloexec);
        apply_flags(sv[1], nonblock, cloexec);
    }
    return rc;
}

/* The system notification the payload SDK's hello_world sample uses. */
typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int blocking);

void os_notify(const char *message)
{
    notify_request_t req;
    memset(&req, 0, sizeof req);
    strncpy(req.message, message, sizeof req.message - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

void os_platform_init(void)
{
}
