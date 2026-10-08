/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Writes one patch entry (library/ogfx/ogfx_tramp.h), for state 0x12345678
 * and entry 0x00ABCDEF, as big-endian bytes on stdout; tests/ogfx/run.sh
 * disassembles them. */
#include <stdio.h>
#include <stdint.h>
typedef uint16_t UWORD;
typedef uint32_t ULONG;
#include "../../library/ogfx/ogfx_tramp.h"
int main(void)
{
    UWORD t[OGFX_TRAMP_WORDS];
    int i;
    tramp_write(t, 0x12345678UL, 0x00ABCDEFUL);
    for (i = 0; i < OGFX_TRAMP_WORDS; i++) { putchar(t[i] >> 8); putchar(t[i] & 0xFF); }
    return 0;
}
