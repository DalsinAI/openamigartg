/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
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

static const struct ortg_mode *native_mode(const struct ortg_mode_table *t, int depth) {
    for (int i = 0; i < t->count; i++)
        if (t->modes[i].native && t->modes[i].depth == depth) return &t->modes[i];
    return 0;
}

/* ACRTG v6: the board's display names each monitor's preferred mode. */
static void preferred_checks(void) {
    static struct ortg_mode_table t, all, none, odd, nat;
    struct ortg_caps c = { .vram_bytes = 0x3FE0000u, .max_width = 1920, .max_height = 1200, .formats = 7, .native = 1,
                           .display_w = 1920, .display_h = 1080 };
    struct ortg_caps unknown = c, odd_c = c, nat_c = c;
    const struct ortg_mode *m16, *m32, *m8;
    unknown.display_w = unknown.display_h = 0;
    odd_c.display_w = 1000; odd_c.display_h = 700;               /* no mode has that size */
    nat_c.native_w = 1920; nat_c.native_h = 1080;                /* a Native size asked for too: never the preferred one */
    ortg_build_modes(&t, 1, &c, 0);
    ortg_build_modes(&all, 1, &c, 1);
    ortg_build_modes(&none, 1, &unknown, 0);
    ortg_build_modes(&odd, 1, &odd_c, 0);
    ortg_build_modes(&nat, 1, &nat_c, 0);
    m8 = by_size(&t, 1920, 1080, 8); m16 = by_size(&t, 1920, 1080, 16); m32 = by_size(&t, 1920, 1080, 32);
    CHECK(m8 && m16 && m32);
    CHECK(m16 && ortg_preferred_mode(&t, ORTG_RGB16) == m16->mode_id);
    CHECK(m32 && ortg_preferred_mode(&t, ORTG_ARGB32) == m32->mode_id && m8 && ortg_preferred_mode(&t, ORTG_CLUT8) == m8->mode_id);
    CHECK(m16 && ortg_preferred_mode(&all, ORTG_RGB16) == m16->mode_id);   /* the same ModeID under All */
    CHECK(ortg_preferred_mode(&none, ORTG_RGB16) == 0 && ortg_preferred_mode(&odd, ORTG_RGB16) == 0);
    CHECK(m16 && ortg_preferred_mode(&nat, ORTG_RGB16) == m16->mode_id && !ORTG_IS_NATIVE(ortg_preferred_mode(&nat, ORTG_RGB16)));
    CHECK(t.display_w == 1920 && t.display_h == 1080 && none.display_w == 0);
}

/* 0.17, the native screen mode's step 3: the Native place. */
static void native_checks(void) {
    static struct ortg_mode_table plain, plain_all, nat, nat_all, again, mon3, tiny_t, over;
    /* ACRTG's 16 MiB board as openrtg_lib.c sets it up, without and with acrtg-v5 */
    struct ortg_caps v4 = { .vram_bytes = 0xFE0000u, .max_width = 1920, .max_height = 1200, .formats = 7 };
    struct ortg_caps v5 = v4, v5none = v4, v4asked = v4;
    v5.native = 1; v5.native_w = 1600; v5.native_h = 900;
    v5none.native = 1;                                         /* a v5 board nobody has asked yet: WANT_W 0 */
    v4asked.native_w = 1600; v4asked.native_h = 900;           /* a size, but not a board that can be asked */

    /* The place and its ModeIDs never move: 0x6n22, 8, 16 and 32-bit. */
    CHECK(ORTG_NATIVE_PLACE == 0x22);
    CHECK(ortg_native_id(1, ORTG_CLUT8) == 0x61221000u && ortg_native_id(1, ORTG_RGB16) == 0x61221100u &&
          ortg_native_id(1, ORTG_ARGB32) == 0x61221200u && ortg_native_id(4, ORTG_ARGB32) == 0x64221200u);
    CHECK(ORTG_IS_NATIVE(0x61221200u) && ORTG_IS_NATIVE(0x62221000u) && !ORTG_IS_NATIVE(0x610A1200u) && !ORTG_IS_NATIVE(0x00220000u));

    /* Listed only on a board that can be asked (v5) and while a size is asked for. */
    CHECK(ortg_build_modes(&plain, 1, &v4, 0) == 45 && ortg_build_modes(&plain_all, 1, &v4, 1) == 96);
    CHECK(ortg_build_modes(&again, 1, &v5none, 0) == 45 && !native_mode(&again, 32) && again.native == 1);
    CHECK(ortg_build_modes(&again, 1, &v4asked, 0) == 45 && !native_mode(&again, 32) && again.native == 0);
    CHECK(!ortg_find_mode(&again, ortg_native_id(1, ORTG_ARGB32)));
    CHECK(ortg_build_modes(&nat, 1, &v5, 0) == 48 && ortg_build_modes(&nat_all, 1, &v5, 1) == 99);
    CHECK(nat.full_count == plain.full_count + 3 && nat_all.full_count == plain_all.full_count + 3);

    /* In Standard and All, last, after every other mode; the others keep their places and ModeIDs. */
    for (int d = 0; d < 3; d++) {
        const struct ortg_mode *m = &nat.modes[45 + d], *a = &nat_all.modes[96 + d];
        static const int depth[3] = { 8, 16, 32 };
        static const char *const name[3] = { "OpenRTG.1: Native 8-bit", "OpenRTG.1: Native 16-bit", "OpenRTG.1: Native 32-bit" };
        CHECK(m->native && m->width == 1600 && m->height == 900 && m->depth == depth[d] && m->standard);
        CHECK(m->mode_id == ortg_native_id(1, d) && a->mode_id == m->mode_id && !strcmp(m->name, name[d]));
        CHECK(ortg_find_mode(&nat, m->mode_id) == &nat.full[nat.full_count - 3 + d]);
        CHECK(ortg_record_slot(&nat, ortg_find_mode(&nat, m->mode_id)) == ORTG_MAX_MODES - 3 + d);
    }
    for (int i = 0; i < 45; i++)
        CHECK(nat.modes[i].mode_id == plain.modes[i].mode_id && !nat.modes[i].native &&
              ortg_record_slot(&nat, &nat.full[i]) == i);
    for (int i = 0; i < 96; i++) CHECK(nat_all.modes[i].mode_id == plain_all.modes[i].mode_id);
    CHECK(ortg_record_slot(&nat, &plain.full[0]) == -1);       /* not this table's */

    /* Its size follows the board: the same ModeIDs at the new size. */
    struct ortg_caps v5b = v5;
    v5b.native_w = 1280; v5b.native_h = 720;
    CHECK(ortg_build_modes(&again, 1, &v5b, 0) == 48);
    CHECK(native_mode(&again, 32)->width == 1280 && native_mode(&again, 32)->height == 720 &&
          native_mode(&again, 32)->mode_id == native_mode(&nat, 32)->mode_id);
    CHECK(ortg_build_modes(&mon3, 3, &v5b, 0) == 48 && native_mode(&mon3, 8)->mode_id == 0x63221000u &&
          !strcmp(native_mode(&mon3, 8)->name, "OpenRTG.3: Native 8-bit"));
    /* The board's largest picture still holds: a size beyond it gives no Native modes. */
    struct ortg_caps big = v5;
    big.native_w = 2560; big.native_h = 1440;
    CHECK(ortg_build_modes(&over, 1, &big, 0) == 45 && !native_mode(&over, 8));
    /* Video RAM still holds: a 2 MiB board shows 1920 x 1080 in 8-bit only. */
    struct ortg_caps small = { .vram_bytes = 2u << 20, .max_width = 1920, .max_height = 1200, .formats = 7,
                               .native = 1, .native_w = 1920, .native_h = 1080 };
    ortg_build_modes(&tiny_t, 1, &small, 0);
    CHECK(native_mode(&tiny_t, 8) && !native_mode(&tiny_t, 16) && !native_mode(&tiny_t, 32));
    CHECK(ortg_record_slot(&tiny_t, ortg_find_mode(&tiny_t, ortg_native_id(1, ORTG_CLUT8))) == ORTG_MAX_MODES - 3);

    /* BestModeIDA never returns it, even when it is the exact size asked for or the only one that big. */
    for (int d = 8; d <= 32; d *= 2) {
        CHECK(!ORTG_IS_NATIVE(ortg_best_mode(&nat, 1600, 900, d)));
        CHECK(ortg_best_mode(&nat, 1600, 900, d) == ortg_best_mode(&plain, 1600, 900, d));
        CHECK(ortg_best_mode(&nat_all, 640, 480, d) == ortg_best_mode(&plain_all, 640, 480, d));
    }
    struct ortg_caps odd = v5;
    odd.native_w = 1912; odd.native_h = 1200;                 /* no listed mode is 1912 x 1200 */
    ortg_build_modes(&again, 1, &odd, 0);
    CHECK(ortg_best_mode(&again, 1912, 1200, 32) == by_size(&plain, 1920, 1200, 32)->mode_id);
    CHECK(ortg_best_mode(&again, 5000, 5000, 32) == by_size(&plain, 1920, 1200, 32)->mode_id);
    CHECK(ortg_best_mode(&again, 1, 1, 1) == ortg_best_mode(&plain, 1, 1, 1));
}

int main(void) {
    static struct ortg_mode_table std16, all16, std64, mon2, small, nodeep;
    struct ortg_caps b16 = { .vram_bytes = 16u << 20, .max_width = 4096, .max_height = 4096, .formats = 7 },
                     b64 = { .vram_bytes = 64u << 20, .max_width = 4096, .max_height = 4096, .formats = 7 };

    /* Standard: twelve PC sizes and three game sizes (320x200, 320x240,
     * 640x400) in three depths; 64 MiB boards add 2560x1440 and 3840x2160. */
    CHECK(ortg_build_modes(&std16, 1, &b16, 0) == 45);
    CHECK(ortg_build_modes(&std64, 1, &b64, 0) == 51);
    CHECK(by_size(&std16, 320, 200, 8) != 0 && by_size(&std16, 320, 240, 8) != 0 && by_size(&std16, 640, 400, 8) != 0);
    CHECK(by_size(&std64, 3840, 2160, 32) != 0);
    CHECK(by_size(&std16, 2560, 1440, 8) == 0);
    /* All adds the other seventeen Amiga-shaped sizes. */
    CHECK(ortg_build_modes(&all16, 1, &b16, 1) == 96);
    CHECK(by_size(&all16, 320, 256, 8) != 0 && by_size(&std16, 320, 256, 8) == 0);

    /* Every ModeID is unique and carries the monitor's part. */
    for (int i = 0; i < all16.count; i++) {
        CHECK((all16.modes[i].mode_id & 0xFF000000u) == ortg_monitor_id(1));
        CHECK((all16.modes[i].mode_id & 0x0CFFu) == 0);          /* no chipset mode flags */
        for (int j = i + 1; j < all16.count; j++) CHECK(all16.modes[i].mode_id != all16.modes[j].mode_id);
    }
    CHECK(ortg_monitor_id(1) == 0x61000000u && ortg_monitor_id(2) == 0x62000000u);

    /* A ModeID means the same mode under Standard and All. */
    CHECK(by_size(&std16, 1920, 1080, 32)->mode_id == by_size(&all16, 1920, 1080, 32)->mode_id);
    /* A saved All-only ModeID still resolves while Standard is chosen. */
    uint32_t pal = by_size(&all16, 320, 256, 8)->mode_id;
    CHECK(ortg_find_mode(&std16, pal) != 0 && ortg_find_mode(&std16, pal)->height == 256);
    /* Another monitor's ModeID is not this monitor's. */
    CHECK(ortg_build_modes(&mon2, 2, &b16, 0) == 45);
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
    CHECK(ortg_best_mode(&std16, 320, 200, 8) == by_size(&std16, 320, 200, 8)->mode_id);
    CHECK(ortg_best_mode(&std16, 300, 220, 8) == by_size(&std16, 320, 240, 8)->mode_id);

    /* A board without 32-bit: the deepest it has. A small board: what fits. */
    struct ortg_caps no32 = { .vram_bytes = 16u << 20, .max_width = 4096, .max_height = 4096, .formats = 3 },
                     tiny = { .vram_bytes = 2u << 20, .max_width = 4096, .max_height = 4096, .formats = 7 };
    CHECK(ortg_build_modes(&nodeep, 1, &no32, 0) == 30);
    CHECK(ortg_best_mode(&nodeep, 640, 480, 32) == by_size(&nodeep, 1920, 1200, 16)->mode_id);
    ortg_build_modes(&small, 1, &tiny, 0);
    CHECK(by_size(&small, 1024, 768, 16) != 0 && by_size(&small, 1024, 768, 32) == 0);
    CHECK(ortg_build_modes(&small, 5, &tiny, 0) == -1 && ortg_build_modes(&small, 0, &tiny, 0) == -1);

    native_checks();
    preferred_checks();

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
