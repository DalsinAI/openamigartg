/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The display database's mode table (modes.h).
 *
 * Standard and All (We, 4 October 2026: "display modes should be reported
 * in our next prefs as standard and all, standard being the popular PC
 * modes"). Every size has one place in SIZES for good, and a ModeID is built
 * from that place and the depth, so a ModeID means the same mode under
 * Standard and All and saved prefs stay valid when the choice changes. New
 * sizes are only ever added at the end.
 *
 * The last place is Native (0.17): its size is the one the host's window
 * asks for (ACRTG v5's WANT_W and WANT_H, in caps). It is in the table only
 * while a size is asked for, after every other mode, and BestModeIDA never
 * picks it. Without a size the table is exactly 0.16's (tests/modes_golden.c).
 */
#include "modes.h"

struct size { uint16_t w, h; uint8_t standard, large; };

static const struct size SIZES[] = {
    /* Standard: the popular PC resolutions (and three game sizes, below). */
    {  640,  480, 1, 0 }, {  800,  600, 1, 0 }, { 1024,  768, 1, 0 }, { 1280,  720, 1, 0 },
    { 1280,  800, 1, 0 }, { 1280, 1024, 1, 0 }, { 1366,  768, 1, 0 }, { 1440,  900, 1, 0 },
    { 1600,  900, 1, 0 }, { 1680, 1050, 1, 0 }, { 1920, 1080, 1, 0 }, { 1920, 1200, 1, 0 },
    /* Standard on 64 MiB boards (protocol v3). */
    { 2560, 1440, 1, 1 }, { 3840, 2160, 1, 1 },
    /* All adds the rest: Amiga-shaped sizes that programs and games ask for.
     * 320x200, 320x240 and 640x400 are in Standard too (0.8): games and SDL
     * ask for them (6 Oct 2026). */
    {  320,  200, 1, 0 }, {  320,  240, 1, 0 }, {  320,  256, 0, 0 }, {  320,  400, 0, 0 },
    {  320,  480, 0, 0 }, {  320,  512, 0, 0 }, {  400,  300, 0, 0 }, {  512,  384, 0, 0 },
    {  640,  200, 0, 0 }, {  640,  256, 0, 0 }, {  640,  400, 1, 0 }, {  640,  512, 0, 0 },
    {  720,  480, 0, 0 }, {  720,  576, 0, 0 }, {  800,  480, 0, 0 }, { 1024,  384, 0, 0 },
    { 1024,  600, 0, 0 }, { 1152,  864, 0, 0 }, { 1280,  960, 0, 0 }, { 1600, 1200, 0, 0 },
    /* Native (0.17): its size is the host's (caps); it must stay place 0x22. */
    {    0,    0, 1, 0 },
};
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))
typedef char native_place_is_last[(NSIZES - 1 == ORTG_NATIVE_PLACE) ? 1 : -1];

static const uint8_t DEPTH[ORTG_FORMATS] = { 8, 16, 32 };
#define LARGE_VRAM (64u << 20)

uint32_t ortg_monitor_id(int monitor) {
    return 0x60000000u | ((uint32_t)monitor << 24);
}

/* A ModeID: 0x6n (monitor n) in the top byte, the size's place in the next,
 * then 0x1000 and the format in bits 8-9 (0x000 8-bit, 0x100 16-bit, 0x200
 * 32-bit), as Picasso96 lays out its own. The low twelve bits are the
 * chipset's mode flags to graphics and intuition (LACE, HAM, SUPERHIRES and
 * the rest), so OpenRTG keeps them clear but for bits 8 and 9, which mean
 * nothing for a board's mode (5 Oct 2026: a LACE bit there broke the mouse). */
static uint32_t mode_id(int monitor, int size, int format) {
    return ortg_monitor_id(monitor) | ((uint32_t)size << 16) | 0x1000u | ((uint32_t)format << 8);
}

static void put_num(char **p, char *end, unsigned v) {
    char tmp[10]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n && *p < end) *(*p)++ = tmp[--n];
}

static void put_str(char **p, char *end, const char *s) {
    while (*s && *p < end) *(*p)++ = *s++;
}

/* "OpenRTG.1: 1920x1080 32-bit"; the Native place "OpenRTG.1: Native 32-bit", whatever its size. */
static void name_mode(char *out, int monitor, const struct size *s, int depth, int native) {
    char *p = out, *end = out + ORTG_NAME_LEN - 1;
    put_str(&p, end, "OpenRTG."); put_num(&p, end, (unsigned)monitor);
    put_str(&p, end, ": ");
    if (native) put_str(&p, end, "Native");
    else { put_num(&p, end, s->w); put_str(&p, end, "x"); put_num(&p, end, s->h); }
    put_str(&p, end, " "); put_num(&p, end, (unsigned)depth); put_str(&p, end, "-bit");
    *p = 0;
}

static int fits(const struct ortg_caps *c, const struct size *s, int format) {
    if (!(c->formats >> format & 1)) return 0;
    if (!s->w || !s->h) return 0;
    if (s->w > c->max_width || s->h > c->max_height) return 0;
    if (s->large && c->vram_bytes < LARGE_VRAM) return 0;
    return (uint64_t)s->w * s->h * (DEPTH[format] / 8) <= c->vram_bytes;
}

static void put_mode(struct ortg_mode *m, int monitor, int place, int f, const struct size *s, int native) {
    m->mode_id = mode_id(monitor, place, f);
    m->width = s->w; m->height = s->h;
    m->depth = DEPTH[f]; m->format = (uint8_t)f; m->standard = s->standard; m->native = (uint8_t)native;
    name_mode(m->name, monitor, s, DEPTH[f], native);
}

/* List order: by width, then height, then depth, as ScreenMode shows them. */
static int before(const struct ortg_mode *a, const struct ortg_mode *b) {
    if (a->width != b->width) return a->width < b->width;
    if (a->height != b->height) return a->height < b->height;
    return a->depth < b->depth;
}

static void sort_modes(struct ortg_mode *m, int n) {
    for (int i = 1; i < n; i++) {
        struct ortg_mode x = m[i]; int j = i - 1;
        while (j >= 0 && before(&x, &m[j])) { m[j + 1] = m[j]; j--; }
        m[j + 1] = x;
    }
}

int ortg_build_modes(struct ortg_mode_table *t, int monitor, const struct ortg_caps *caps, int all) {
    if (monitor < 1 || monitor > ORTG_MAX_MONITORS) return -1;
    t->monitor = monitor; t->all = all ? 1 : 0; t->native = caps->native ? 1 : 0; t->count = t->full_count = 0;
    t->max_width = caps->max_width; t->max_height = caps->max_height;
    for (int s = 0; s < NSIZES; s++)
        for (int f = 0; f < ORTG_FORMATS; f++) {
            if (s == ORTG_NATIVE_PLACE) continue;
            if (!fits(caps, &SIZES[s], f) || t->full_count >= ORTG_MAX_MODES - ORTG_NATIVE_MODES) continue;
            put_mode(&t->full[t->full_count++], monitor, s, f, &SIZES[s], 0);
        }
    sort_modes(t->full, t->full_count);
    /* the Native modes after the rest, at the size the host asks for: only on a board that can be asked,
     * and only while it is (the same rules as any size, the board's largest picture among them) */
    if (caps->native && caps->native_w && caps->native_h) {
        struct size n = SIZES[ORTG_NATIVE_PLACE];
        n.w = caps->native_w; n.h = caps->native_h;
        for (int f = 0; f < ORTG_FORMATS; f++)
            if (fits(caps, &n, f)) put_mode(&t->full[t->full_count++], monitor, ORTG_NATIVE_PLACE, f, &n, 1);
    }
    for (int i = 0; i < t->full_count; i++)
        if (t->all || t->full[i].standard) t->modes[t->count++] = t->full[i];
    return t->count;
}

uint32_t ortg_native_id(int monitor, int format) {
    return mode_id(monitor, ORTG_NATIVE_PLACE, format);
}

int ortg_record_slot(const struct ortg_mode_table *t, const struct ortg_mode *m) {
    if (m < t->full || m >= t->full + t->full_count) return -1;
    if (m->native) return ORTG_MAX_MODES - ORTG_NATIVE_MODES + m->format;
    return (int)(m - t->full);
}

const struct ortg_mode *ortg_find_mode(const struct ortg_mode_table *t, uint32_t mode_id) {
    if ((mode_id & 0xFF000000u) != ortg_monitor_id(t->monitor) || !(mode_id & 0x1000u)) return 0;
    for (int i = 0; i < t->full_count; i++)
        if (t->full[i].mode_id == mode_id) return &t->full[i];
    return 0;
}

uint32_t ortg_best_mode(const struct ortg_mode_table *t, int width, int height, int depth) {
    const struct ortg_mode *best = 0, *largest = 0, *deepest = 0;
    for (int i = 0; i < t->count; i++) {
        const struct ortg_mode *m = &t->modes[i];
        if (m->native) continue;                    /* never chosen for a program (0.17) */
        if (!deepest || m->depth > deepest->depth ||
            (m->depth == deepest->depth && (uint32_t)m->width * m->height > (uint32_t)deepest->width * deepest->height))
            deepest = m;
        if (m->depth < depth) continue;
        uint32_t area = (uint32_t)m->width * m->height;
        if (!largest || area > (uint32_t)largest->width * largest->height ||
            (area == (uint32_t)largest->width * largest->height && m->depth < largest->depth))
            largest = m;
        if (m->width < width || m->height < height) continue;
        if (!best) { best = m; continue; }
        uint32_t barea = (uint32_t)best->width * best->height;
        if (area < barea || (area == barea && m->depth < best->depth)) best = m;
    }
    if (best) return best->mode_id;
    if (largest) return largest->mode_id;
    return deepest ? deepest->mode_id : 0;
}
