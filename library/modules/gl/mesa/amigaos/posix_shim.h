/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The POSIX calls Mesa's util and c11 code uses that libnix and the stove's
 * libpthread don't fully give it. The cross file includes this in every file
 * Mesa compiles, before any system header.
 *
 *  - nanosleep, mkstemp, strndup and vasprintf are in libnix, but its
 *    headers don't declare them, so they are declared here; alloca is
 *    GCC's builtin. The stove's pthread.h later wraps nanosleep in a
 *    cancellation-point macro; that still calls the real one.
 *  - ftruncate and pthread_condattr_setclock are nowhere in the stove;
 *    posix_shim.c defines them (gla_ names, so they never meet a library
 *    one) and is linked with the GLA core. It also defines what only the
 *    link misses: mkstemps, pthread_sigmask, log2f, exp2f and the
 *    __sync compare-and-swap calls GCC makes on AmigaOS.
 *
 * It includes no system header: meson's has_function checks see it too, and
 * a declaration from time.h made them fail (clock_gettime, then librt). */
#ifndef GLA_POSIX_SHIM_H
#define GLA_POSIX_SHIM_H
#if (defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA)) && !defined(__ASSEMBLER__)
#ifdef __cplusplus
extern "C" {
#endif
struct timespec;
int nanosleep(const struct timespec *req, struct timespec *rem);
int mkstemp(char *tmpl);
char *strndup(const char *s, __SIZE_TYPE__ n);
int vasprintf(char **strp, const char *fmt, __builtin_va_list ap);
#define alloca(size) __builtin_alloca(size)
/* libnix's fcntl.h has no O_CLOEXEC; there is no exec to close across. */
#define O_CLOEXEC 0
int gla_ftruncate(int fd, long length);
int gla_pthread_condattr_setclock(void *attr, int clock);
#ifdef __cplusplus
}
#endif
#define ftruncate gla_ftruncate
#define pthread_condattr_setclock gla_pthread_condattr_setclock
#endif
#endif
