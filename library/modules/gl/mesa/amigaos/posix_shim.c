/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What posix_shim.h promises. */
#include <errno.h>
#include "posix_shim.h"

/* Anonymous files (os_create_anonymous_file) are for drivers that share
 * buffers with another process; softpipe and the GLA core never ask. */
int gla_ftruncate(int fd, long length)
{
    (void)fd, (void)length;
    errno = ENOSYS;
    return -1;
}

/* The stove's condition variables keep one clock; accept the monotonic one
 * so u_cnd_monotonic_init succeeds. Timed waits are only used by fences with
 * a timeout, which GLA doesn't set. */
int gla_pthread_condattr_setclock(void *attr, int clock)
{
    (void)attr, (void)clock;
    return 0;
}

/* Mesa's log file name; logging goes to stderr instead. */
int mkstemps(char *tmpl, int suffixlen)
{
    (void)tmpl, (void)suffixlen;
    errno = ENOSYS;
    return -1;
}

/* No signals to block on AmigaOS; Mesa's threads only ask so that the
 * process's signals keep going to its main thread. */
#include <signal.h>
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
    (void)how, (void)set;
    if (old)
        sigemptyset(old);
    return 0;
}

/* libm has log2 and pow, but no log2f, exp2 or exp2f. */
#include <math.h>
float log2f(float x) { return (float)log2(x); }
float exp2f(float x) { return (float)pow(2.0, x); }

/* GCC calls these for the __sync builtins on AmigaOS; the 68040's CAS is
 * atomic on one CPU. */
unsigned int __sync_val_compare_and_swap_4(volatile void *p, unsigned int expected, unsigned int desired)
{
    __asm__ volatile("cas.l %0,%2,%1" : "+d"(expected), "+m"(*(volatile unsigned int *)p) : "d"(desired) : "cc", "memory");
    return expected;
}

unsigned char __sync_val_compare_and_swap_1(volatile void *p, unsigned char expected, unsigned char desired)
{
    __asm__ volatile("cas.b %0,%2,%1" : "+d"(expected), "+m"(*(volatile unsigned char *)p) : "d"(desired) : "cc", "memory");
    return expected;
}

/* libnix qsort misorders arrays and hands the compare function pointers
 * outside the array, which crashed nir_opt_vectorize and
 * nir_opt_vectorize_io in the es2-shaders scene. Mesa calls qsort from many
 * places, so this merge sort replaces it for the whole program.
 */
#include <stdlib.h>
#include <string.h>

static void gla_insertion_sort(unsigned char *base, unsigned char *hold,
                               size_t nmemb, size_t size,
                               int (*compar)(const void *, const void *))
{
   for (size_t i = 1; i < nmemb; i++) {
      size_t j = i;
      memcpy(hold, base + i * size, size);
      while (j > 0 && compar(base + (j - 1) * size, hold) > 0) {
         memcpy(base + j * size, base + (j - 1) * size, size);
         j--;
      }
      memcpy(base + j * size, hold, size);
   }
}

static void gla_merge_sort(unsigned char *base, unsigned char *tmp,
                           size_t nmemb, size_t size,
                           int (*compar)(const void *, const void *))
{
   if (nmemb <= 8) {
      gla_insertion_sort(base, tmp, nmemb, size, compar);
      return;
   }
   size_t half = nmemb / 2;
   gla_merge_sort(base, tmp, half, size, compar);
   gla_merge_sort(base + half * size, tmp, nmemb - half, size, compar);
   unsigned char *l = base, *le = base + half * size;
   unsigned char *r = le, *re = base + nmemb * size;
   unsigned char *o = tmp;
   while (l < le && r < re) {
      if (compar(r, l) < 0) {
         memcpy(o, r, size);
         r += size;
      } else {
         memcpy(o, l, size);
         l += size;
      }
      o += size;
   }
   memcpy(o, l, le - l);
   o += le - l;
   memcpy(base, tmp, o - tmp);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
   if (nmemb < 2 || size == 0)
      return;
   unsigned char *tmp = malloc(nmemb * size);
   if (tmp) {
      gla_merge_sort(base, tmp, nmemb, size, compar);
      free(tmp);
      return;
   }
   /* No memory: sort in place by swapping neighbours. */
   unsigned char *b = base;
   for (size_t i = 1; i < nmemb; i++) {
      for (size_t j = i; j > 0 && compar(b + (j - 1) * size, b + j * size) > 0; j--) {
         unsigned char *x = b + (j - 1) * size, *y = b + j * size;
         for (size_t k = 0; k < size; k++) {
            unsigned char t = x[k];
            x[k] = y[k];
            y[k] = t;
         }
      }
   }
}

/* POSIX regular expressions: the header is there, the library isn't. Mesa
 * uses them only to match driconf entries to a program's name, and there
 * is no driconf on the Amiga, so nothing ever matches (as Mesa's own
 * NO_REGEX does). Needed since virgl brought xmlconfig into the link. */
#include <regex.h>

int regcomp(regex_t *__restrict r, const char *__restrict pattern, int flags)
{
   (void)r; (void)pattern; (void)flags;
   return 0;
}

int regexec(const regex_t *__restrict r, const char *__restrict s, size_t n, regmatch_t m[__restrict], int flags)
{
   (void)r; (void)s; (void)n; (void)m; (void)flags;
   return REG_NOMATCH;
}

void regfree(regex_t *r)
{
   (void)r;
}
