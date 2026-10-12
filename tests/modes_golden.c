/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The golden comparison for the mode table (0.17, the native screen mode's
 * step 3): with no size asked for (no ACRTG v5 board, or WANT_W 0) the
 * table must be exactly 0.16's.
 *
 * It writes every table as text: for the boards OpenRTG knows (and a few
 * more), every monitor, Standard and All, every listed mode and every mode
 * the board shows, then BestModeIDA's answers to a grid of requests. A
 * ModeID that doesn't look up to its own mode is written too.
 * tests/golden/modes-table.txt holds the FNV-1a of that text as
 * openrtg.library 0.16.0's modes.c (openamigartg f47794c) wrote it, and its
 * line count.
 *   modes_golden GOLDEN_FILE    compares; 0 when the same
 *   modes_golden --dump         writes the text, to compare by eye with an
 *                               older modes.c built the same way
 * It uses only what 0.16's modes.h has, so it builds against either. */
#include <stdio.h>
#include <string.h>
#include "../library/modes.h"

static unsigned long hash = 2166136261UL, lines;
static FILE *dump;

static void emit(const char *line) {
    for (const char *p = line; *p; p++) { hash ^= (unsigned char)*p; hash = (hash * 16777619UL) & 0xFFFFFFFFUL; }
    lines++;
    if (dump) fputs(line, dump);
}

static void emit_mode(const char *what, const struct ortg_mode *m) {
    char buf[160];
    snprintf(buf, sizeof buf, "  %s %08lx %ux%u d%u f%u s%u %s\n", what, (unsigned long)m->mode_id, (unsigned)m->width,
             (unsigned)m->height, (unsigned)m->depth, (unsigned)m->format, (unsigned)m->standard, m->name);
    emit(buf);
}

struct board { const char *name; struct ortg_caps caps; int monitors; };

int main(int argc, char **argv) {
    /* ACRTG's 16 and 64 MiB boards as openrtg_lib.c sets them up (video RAM less its two 64 KiB pages,
     * 1920 x 1200, three formats), the host tests' larger ones, and boards with fewer formats or less RAM */
    static const struct board boards[] = {
        { "acrtg16", { .vram_bytes = 0x01000000UL - 0x20000UL, .max_width = 1920, .max_height = 1200, .formats = 7 }, 4 },
        { "acrtg64", { .vram_bytes = 0x04000000UL - 0x20000UL, .max_width = 1920, .max_height = 1200, .formats = 7 }, 4 },
        { "big16", { .vram_bytes = 16UL << 20, .max_width = 4096, .max_height = 4096, .formats = 7 }, 1 },
        { "big64", { .vram_bytes = 64UL << 20, .max_width = 4096, .max_height = 4096, .formats = 7 }, 1 },
        { "no32", { .vram_bytes = 16UL << 20, .max_width = 4096, .max_height = 4096, .formats = 3 }, 1 },
        { "tiny", { .vram_bytes = 2UL << 20, .max_width = 4096, .max_height = 4096, .formats = 7 }, 1 },
        { "clut", { .vram_bytes = 1UL << 20, .max_width = 1024, .max_height = 768, .formats = 1 }, 1 },
    };
    static const int req[][2] = { { 0, 0 }, { 1, 1 }, { 320, 200 }, { 320, 256 }, { 640, 480 }, { 800, 600 },
                                  { 1000, 700 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 1920, 1200 },
                                  { 2560, 1440 }, { 4000, 3000 } };
    static const int depths[] = { 1, 8, 15, 16, 24, 32 };
    static struct ortg_mode_table t;
    char buf[160];
    if (argc == 2 && !strcmp(argv[1], "--dump")) dump = stdout;
    else if (argc != 2) { fprintf(stderr, "usage: modes_golden GOLDEN_FILE | --dump\n"); return 2; }
    for (unsigned b = 0; b < sizeof boards / sizeof boards[0]; b++)
        for (int mon = 1; mon <= boards[b].monitors; mon++)
            for (int all = 0; all <= 1; all++) {
                int n = ortg_build_modes(&t, mon, &boards[b].caps, all);
                snprintf(buf, sizeof buf, "board %s monitor %d %s: %d listed, %d shown\n", boards[b].name, mon,
                         all ? "All" : "Standard", n, t.full_count);
                emit(buf);
                for (int i = 0; i < t.count; i++) emit_mode("listed", &t.modes[i]);
                for (int i = 0; i < t.full_count; i++) emit_mode("shown", &t.full[i]);
                for (int i = 0; i < t.full_count; i++)
                    if (ortg_find_mode(&t, t.full[i].mode_id) != &t.full[i]) emit_mode("LOOKUP FAILS", &t.full[i]);
                if (mon != 1) continue;
                for (unsigned r = 0; r < sizeof req / sizeof req[0]; r++)
                    for (unsigned d = 0; d < sizeof depths / sizeof depths[0]; d++) {
                        snprintf(buf, sizeof buf, "  best %dx%d d%d: %08lx\n", req[r][0], req[r][1], depths[d],
                                 (unsigned long)ortg_best_mode(&t, req[r][0], req[r][1], depths[d]));
                        emit(buf);
                    }
            }
    if (dump) return 0;
    {
        FILE *f = fopen(argv[1], "r");
        unsigned long want = 0, want_lines = 0;
        int ok = f && fscanf(f, "%lx %lu", &want, &want_lines) == 2;
        if (f) fclose(f);
        if (!ok) { printf("FAIL modes_golden: can't read %s\n", argv[1]); return 1; }
        if (want != hash || want_lines != lines) {
            printf("FAIL modes_golden: the mode table differs from 0.16's: %08lx in %lu lines, golden %08lx in %lu\n",
                   hash, lines, want, want_lines);
            return 1;
        }
        printf("modes_golden: the mode table is 0.16's (%08lx, %lu lines)\n", hash, lines);
    }
    return 0;
}
