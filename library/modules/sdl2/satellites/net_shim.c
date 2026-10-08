/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_net picks UDP ports and sequence numbers with random() and
 * srandom(), which libnix doesn't have: its rand() and srand() stand in. */
#include <stdlib.h>

long random(void)
{
    return (long)rand();
}

void srandom(unsigned int seed)
{
    srand(seed);
}
