/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What SDL2_net needs that libnix gets wrong or lacks.
 *
 * random() and srandom(): SDL2_net picks UDP ports and sequence numbers
 * with them, and libnix doesn't have them; its rand() and srand() stand in.
 *
 * select(): SDL2_net asks select() whether a socket has data
 * (SDLNet_UDP_Recv, SDLNet_CheckSockets). libnix's select() for its
 * sockets (libnix sources/socket/socket/__initsock.c, _sock_poll and
 * _sock_select) gives bsdsocket.library's WaitSelect the socket's own
 * number as nfds, not one more, so WaitSelect never looks at the socket
 * and a zero-timeout select() never finds data: SDLNet_UDP_Recv got
 * nothing, even from a datagram the socket had just sent itself over
 * 127.0.0.1. With a timeout, libnix's select() sends a timer request it
 * never opened timer.device for, and the program fails (#80000004). Both
 * are libnix's (bebbo's amiga-gcc), in the GCC 6.5 and GCC 16 stoves alike.
 * Until the stoves carry a fixed libnix, SDL2_net's sources are built with
 * select() renamed to ogpu_net_select (satellites/Makefile), which asks
 * WaitSelect itself, through libnix's own bsdsocket.library base, with the
 * sockets' numbers and nfds one past the highest. A set that holds anything
 * but sockets goes to libnix's select() as before. */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>              /* libnix's: StdFileDes, _lx_fhfromfd, LX_SOCKET */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <proto/bsdsocket.h>    /* SocketBase: libnix's, opened by its socket startup */

long random(void)
{
    return (long)rand();
}

void srandom(unsigned int seed)
{
    srand(seed);
}

#undef select
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

int ogpu_net_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    fd_set sr, sw, se;
    int fd, top = 0, count = 0;
    LONG rc;
    if (n > FD_SETSIZE)
        n = FD_SETSIZE;
    FD_ZERO(&sr);
    FD_ZERO(&sw);
    FD_ZERO(&se);
    for (fd = 0; fd < n; fd++) {
        int ir = r && FD_ISSET(fd, r), iw = w && FD_ISSET(fd, w), ie = e && FD_ISSET(fd, e);
        StdFileDes *f;
        if (!ir && !iw && !ie)
            continue;
        f = _lx_fhfromfd(fd);
        if (!f || !(f->lx_flags & LX_SOCKET) || !SocketBase || f->lx_sock < 0 || f->lx_sock >= FD_SETSIZE)
            return select(n, r, w, e, tv);
        if (ir) FD_SET(f->lx_sock, &sr);
        if (iw) FD_SET(f->lx_sock, &sw);
        if (ie) FD_SET(f->lx_sock, &se);
        if (f->lx_sock + 1 > top)
            top = f->lx_sock + 1;
    }
    rc = WaitSelect(top, r ? &sr : NULL, w ? &sw : NULL, e ? &se : NULL, (APTR)tv, NULL);
    if (rc < 0) {
        errno = Errno();
        return -1;
    }
    /* Back to libnix's numbers, in the caller's sets (only their first n
     * bits: a caller may pass a set as short as n needs). */
    for (fd = 0; fd < n; fd++) {
        int ir = r && FD_ISSET(fd, r), iw = w && FD_ISSET(fd, w), ie = e && FD_ISSET(fd, e);
        StdFileDes *f;
        if (!ir && !iw && !ie)
            continue;
        f = _lx_fhfromfd(fd);
        if (ir) { if (FD_ISSET(f->lx_sock, &sr)) count++; else FD_CLR(fd, r); }
        if (iw) { if (FD_ISSET(f->lx_sock, &sw)) count++; else FD_CLR(fd, w); }
        if (ie) { if (FD_ISSET(f->lx_sock, &se)) count++; else FD_CLR(fd, e); }
    }
    return count;
}
