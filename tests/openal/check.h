/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The PC tests' checks (tests/openal): each counts, and a failure prints
 * its line. A test ends with check_done, which says how many ran and exits
 * 1 if any failed. */
#ifndef OPENAL_CHECK_H
#define OPENAL_CHECK_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

static int check_count, check_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        check_count++;                                                       \
        if (!(cond)) {                                                       \
            check_failed++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

/* alGetError is the expected error (and the flag is clear after). */
#define CHECK_AL(expected)                                                   \
    do {                                                                     \
        ALenum e_ = alGetError();                                            \
        check_count++;                                                       \
        if (e_ != (ALenum)(expected)) {                                      \
            check_failed++;                                                  \
            printf("FAIL %s:%d: alGetError 0x%x, expected %s\n", __FILE__,   \
                   __LINE__, (unsigned)e_, #expected);                       \
        }                                                                    \
    } while (0)

#define CHECK_ALC(device, expected)                                          \
    do {                                                                     \
        ALCenum e_ = alcGetError(device);                                    \
        check_count++;                                                       \
        if (e_ != (ALCenum)(expected)) {                                     \
            check_failed++;                                                  \
            printf("FAIL %s:%d: alcGetError 0x%x, expected %s\n", __FILE__,  \
                   __LINE__, (unsigned)e_, #expected);                       \
        }                                                                    \
    } while (0)

/* Two numbers within a relative tolerance (and an absolute one near 0). */
#define CHECK_NEAR(got, want, rel)                                           \
    do {                                                                     \
        double g_ = (got), w_ = (want);                                      \
        check_count++;                                                       \
        if (!(fabs(g_ - w_) <= (rel) * fabs(w_) + 1e-7) && !(g_ == w_)) {    \
            check_failed++;                                                  \
            printf("FAIL %s:%d: %s = %.9g, expected %.9g\n", __FILE__,       \
                   __LINE__, #got, g_, w_);                                  \
        }                                                                    \
    } while (0)

static int check_done(const char *name)
{
    printf("%s: %d checks, %d failed\n", name, check_count, check_failed);
    return check_failed ? 1 : 0;
}

#endif
