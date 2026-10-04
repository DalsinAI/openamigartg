/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Host tests for the display database's mode table: tests/run.sh */
#include <stdio.h>
#include <string.h>
#include "../library/modes.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const struct ortg_mode *by_size(const struct ortg_mode_table *t, int w, int h, int d) {
    for (int i = 0; i < t->count; i++)
        if (t->modes[i].width == w && t->modes[i].height == h && t->modes[i].depth == d) return &t->modes[i];
    return 0;
}

int main(void) {
    static struct ortg_mode_table std16, all16, std64, mon2, small, nodeep;
    struct ortg_caps b16 = { 16u << 20, 4096, 4096, 7 }, b64 = { 64u << 20, 4096, 4096, 7 };

    /* Standard: twelve sizes in three depths; 64 MiB boards add 2560x1440 and 3840x2160. */
    CHECK(ortg_build_modes(&std16, 1, &b16, 0) == 36);
    CHECK(ortg_build_modes(&std64, 1, &b64, 0) == 42);
    CHECK(by_size(&std64, 3840, 2160, 32) != 0);
    CHECK(by_size(&std16, 2560, 1440, 8) == 0);
    /* All adds twenty Amiga-shaped sizes. */
    CHECK(ortg_build_modes(&all16, 1, &b16, 1) == 96);
    CHECK(by_size(&all16, 320, 256, 8) != 0 && by_size(&std16, 320, 256, 8) == 0);

    /* Every ModeID is unique and carries the monitor's part. */
    for (int i = 0; i < all16.count; i++) {
        CHECK((all16.modes[i].mode_id & 0xFFFF1000u) == ortg_monitor_id(1));
        for (int j = i + 1; j < all16.count; j++) CHECK(all16.modes[i].mode_id != all16.modes[j].mode_id);
    }
    CHECK(ortg_monitor_id(1) == 0x50011000u && ortg_monitor_id(2) == 0x50021000u);

    /* A ModeID means the same mode under Standard and All. */
    CHECK(by_size(&std16, 1920, 1080, 32)->mode_id == by_size(&all16, 1920, 1080, 32)->mode_id);
    /* A saved All-only ModeID still resolves while Standard is chosen. */
    uint32_t pal = by_size(&all16, 320, 256, 8)->mode_id;
    CHECK(ortg_find_mode(&std16, pal) != 0 && ortg_find_mode(&std16, pal)->height == 256);
    /* Another monitor's ModeID is not this monitor's. */
    CHECK(ortg_build_modes(&mon2, 2, &b16, 0) == 36);
    CHECK(ortg_find_mode(&std16, by_size(&mon2, 640, 480, 8)->mode_id) == 0);
    CHECK(by_size(&mon2, 640, 480, 8)->mode_id != by_size(&std16, 640, 480, 8)->mode_id);

    /* Names and order. */
    CHECK(strcmp(by_size(&std16, 1920, 1080, 32)->name, "OpenRTG.1: 1920x1080 32-bit") == 0);
    CHECK(strcmp(by_size(&mon2, 640, 480, 8)->name, "OpenRTG.2: 640x480 8-bit") == 0);
    for (int i = 1; i < all16.count; i++) {
        const struct ortg_mode *a = &all16.modes[i - 1], *b = &all16.modes[i];
        CHECK(a->width < b->width || (a->width == b->width && (a->height < b->height || (a->height == b->height && a->depth < b->depth))));
    }

    /* BestModeIDA: the smallest big enough, else the largest at that depth, else the deepest. */
    CHECK(ortg_best_mode(&std16, 1000, 700, 16) == by_size(&std16, 1024, 768, 16)->mode_id);
    CHECK(ortg_best_mode(&std16, 1920, 1080, 32) == by_size(&std16, 1920, 1080, 32)->mode_id);
    CHECK(ortg_best_mode(&std16, 800, 600, 24) == by_size(&std16, 800, 600, 32)->mode_id);
    CHECK(ortg_best_mode(&std16, 5000, 5000, 8) == by_size(&std16, 1920, 1200, 8)->mode_id);
    CHECK(ortg_best_mode(&all16, 320, 200, 8) == by_size(&all16, 320, 200, 8)->mode_id);
    CHECK(ortg_best_mode(&std16, 320, 200, 8) == by_size(&std16, 640, 480, 8)->mode_id);

    /* A board without 32-bit: the deepest it has. A small board: what fits. */
    struct ortg_caps no32 = { 16u << 20, 4096, 4096, 3 }, tiny = { 2u << 20, 4096, 4096, 7 };
    CHECK(ortg_build_modes(&nodeep, 1, &no32, 0) == 24);
    CHECK(ortg_best_mode(&nodeep, 640, 480, 32) == by_size(&nodeep, 1920, 1200, 16)->mode_id);
    ortg_build_modes(&small, 1, &tiny, 0);
    CHECK(by_size(&small, 1024, 768, 16) != 0 && by_size(&small, 1024, 768, 32) == 0);
    CHECK(ortg_build_modes(&small, 5, &tiny, 0) == -1 && ortg_build_modes(&small, 0, &tiny, 0) == -1);

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
