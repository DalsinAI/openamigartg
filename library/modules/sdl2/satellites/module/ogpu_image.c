/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_image's decoding ladder on OpenGPU (ogpu_image.h). Built into
 * SDL2_image.module and libSDL2_image_static.a alike. */
#include "SDL.h"
#include "SDL_image.h"

#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/datatypes.h>
#include <datatypes/pictureclass.h>
#include <clib/alib_protos.h>

#include "ogpu_image.h"
#include "sat_path.h"
#include "sat_service.h"

#define MEDIA_DECODE "media.decode/1"
#define MD_PROBE 1
#define MD_DECODE 2
#define MAX_FILE (64UL << 20)

static struct sat_svc *svc = NULL;
static int svc_tried = 0;
static struct SignalSemaphore svc_lock;
static int lock_ready = 0;

enum { MODE_AUTO, MODE_CPU, MODE_SERVICE };

static int mode(void)
{
    const char *m = SDL_GetHint("SDL_IMAGE_DECODER");
    if (m && SDL_strcasecmp(m, "cpu") == 0) {
        return MODE_CPU;
    }
    if (m && SDL_strcasecmp(m, "service") == 0) {
        return MODE_SERVICE;
    }
    return MODE_AUTO;
}

static void lock(void)
{
    Forbid();
    if (!lock_ready) {
        InitSemaphore(&svc_lock);
        lock_ready = 1;
    }
    Permit();
    ObtainSemaphore(&svc_lock);
}

/* The service, opened once; NULL when there is none. Under the lock. */
static struct sat_svc *service(void)
{
    if (!svc && !svc_tried) {
        svc_tried = 1;
        svc = sat_service_open(MEDIA_DECODE);
    }
    return svc;
}

static Uint32 be32(const Uint8 *p)
{
    return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3];
}

/* Rung 1's formats, from the first bytes. */
static int heavy(const Uint8 *h, size_t n)
{
    if (n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) {
        return 1;                                               /* JPEG */
    }
    if (n >= 26 && SDL_memcmp(h, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return h[25] != 3;                                      /* PNG, not paletted */
    }
    if (n >= 12 && SDL_memcmp(h, "RIFF", 4) == 0 && SDL_memcmp(h + 8, "WEBP", 4) == 0) {
        return 1;
    }
    if (n >= 12 && SDL_memcmp(h + 4, "ftyp", 4) == 0 &&
        (SDL_memcmp(h + 8, "avif", 4) == 0 || SDL_memcmp(h + 8, "avis", 4) == 0 ||
         SDL_memcmp(h + 8, "heic", 4) == 0 || SDL_memcmp(h + 8, "heix", 4) == 0 ||
         SDL_memcmp(h + 8, "mif1", 4) == 0)) {
        return 1;
    }
    if ((n >= 2 && h[0] == 0xFF && h[1] == 0x0A) ||
        (n >= 12 && SDL_memcmp(h, "\0\0\0\x0cJXL \r\n\x87\n", 12) == 0)) {
        return 1;                                               /* JPEG XL */
    }
    if (n >= 4 && (SDL_memcmp(h, "II*\0", 4) == 0 || SDL_memcmp(h, "MM\0*", 4) == 0)) {
        return 1;                                               /* TIFF */
    }
    return 0;
}

/* The rest of src, in memory of its own (AllocVec, so the card reads it as
   it is). NULL when it can't be read. */
static Uint8 *read_all(SDL_RWops *src, ULONG *len)
{
    Sint64 start = SDL_RWtell(src), size = SDL_RWsize(src);
    Uint8 *buf;
    if (start < 0 || size <= start || size - start > (Sint64)MAX_FILE) {
        return NULL;
    }
    *len = (ULONG)(size - start);
    buf = AllocVec(*len, MEMF_ANY);
    if (buf && SDL_RWread(src, buf, 1, *len) != *len) {
        FreeVec(buf);
        buf = NULL;
    }
    return buf;
}

SDL_Surface *ogpu_image_load(SDL_RWops *src, const char *type, int pass)
{
    int m = mode();
    Uint8 head[32], info[24];
    Sint64 start;
    size_t n;
    Uint8 *file = NULL;
    ULONG len = 0, w = 0, h = 0, w2 = 0, h2 = 0;
    SDL_Surface *s = NULL;
    struct sat_svc *sv;

    if (m == MODE_CPU || !src) {
        return NULL;
    }
    start = SDL_RWtell(src);
    if (start < 0) {
        return NULL;
    }
    if (pass == 0 && m == MODE_AUTO) {
        n = SDL_RWread(src, head, 1, sizeof(head));
        SDL_RWseek(src, start, RW_SEEK_SET);
        if (!heavy(head, n)) {
            return NULL;
        }
    }
    lock();
    sv = service();
    if (sv) {
        file = read_all(src, &len);
    }
    if (file) {
        struct OSBuffer buf[4];
        ULONG extra[4];
        SDL_zeroa(buf);
        extra[0] = extra[1] = 0;                        /* full size */
        extra[2] = sat_service_hint(type);
        extra[3] = 0;
        buf[0].ob_Data = file;
        buf[0].ob_Length = len;
        buf[1].ob_Data = info;
        buf[1].ob_Length = sizeof(info);
        if (sat_service_call(sv, MD_PROBE, 0, 2, buf, extra, &w, &h) == OSERR_OK && be32(info) == 1) {
            w = be32(info + 16);
            h = be32(info + 20);
            if (w && h && w <= 16384 && h <= 16384) {
                s = SDL_CreateRGBSurfaceWithFormat(0, (int)w, (int)h, 32,
                                                   (be32(info + 8) & 1) ? SDL_PIXELFORMAT_ARGB8888
                                                                        : SDL_PIXELFORMAT_RGB888);
            }
        }
        if (s && s->pitch == (int)(w * 4)) {
            buf[1].ob_Data = s->pixels;
            buf[1].ob_Length = w * h * 4;
            if (sat_service_call(sv, MD_DECODE, 0, 2, buf, extra, &w2, &h2) != OSERR_OK || w2 != w || h2 != h) {
                SDL_FreeSurface(s);
                s = NULL;
            }
        } else if (s) {
            SDL_FreeSurface(s);
            s = NULL;
        }
        FreeVec(file);
        if (sv->lost) {
            /* It stopped answering: this program decodes on the CPU from now on. */
            sat_service_close(sv);
            svc = NULL;
        }
    }
    ReleaseSemaphore(&svc_lock);
    SDL_RWseek(src, s ? start + (Sint64)len : start, RW_SEEK_SET);
    return s;
}

int ogpu_image_init_flags(int want)
{
    int have = 0;
    if (mode() == MODE_CPU) {
        return 0;
    }
    lock();
    if (service()) {
        have = want & (IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP | IMG_INIT_JXL | IMG_INIT_AVIF);
    }
    ReleaseSemaphore(&svc_lock);
    return have;
}

void ogpu_image_close(void)
{
    if (!lock_ready) {
        return;
    }
    lock();
    if (svc) {
        sat_service_close(svc);
        svc = NULL;
    }
    svc_tried = 0;
    ReleaseSemaphore(&svc_lock);
}

SDL_Surface *ogpu_image_load_datatype(const char *file)
{
    struct Library *DataTypesBase;
    Object *o;
    struct BitMapHeader *bmh = NULL;
    SDL_Surface *s = NULL;
    char path[512];
    void *window;

    if (!file || mode() == MODE_CPU) {
        return NULL;
    }
    DataTypesBase = OpenLibrary((CONST_STRPTR) "datatypes.library", 43);
    if (!DataTypesBase) {
        return NULL;
    }
    /* The datatype class opens the file in processes of its own: it is given the
       file's full path (PROGDIR: and a relative name mean nothing there), and
       a path that fails fails without a requester. */
    file = sat_abspath(file, path, sizeof(path));
    window = sat_requesters_off();
    o = NewDTObject((APTR)file, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
                    PDTA_Remap, FALSE, TAG_DONE);
    sat_requesters_restore(window);
    if (o) {
        struct gpLayout layout;
        layout.MethodID = DTM_PROCLAYOUT;
        layout.gpl_GInfo = NULL;
        layout.gpl_Initial = 1;
        DoDTMethodA(o, NULL, NULL, (Msg)&layout);
        if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) == 1 && bmh &&
            bmh->bmh_Width && bmh->bmh_Height) {
            s = SDL_CreateRGBSurfaceWithFormat(0, bmh->bmh_Width, bmh->bmh_Height, 32,
                                               bmh->bmh_Masking == mskHasAlpha ? SDL_PIXELFORMAT_ARGB8888
                                                                               : SDL_PIXELFORMAT_RGB888);
        }
        if (s) {
            struct pdtBlitPixelArray pa;
            pa.MethodID = PDTM_READPIXELARRAY;
            pa.pbpa_PixelData = s->pixels;
            pa.pbpa_PixelFormat = PBPAFMT_ARGB;
            pa.pbpa_PixelArrayMod = s->pitch;
            pa.pbpa_Left = 0;
            pa.pbpa_Top = 0;
            pa.pbpa_Width = s->w;
            pa.pbpa_Height = s->h;
            if (!DoMethodA(o, (Msg)&pa)) {
                SDL_FreeSurface(s);
                s = NULL;
            }
        }
        DisposeDTObject(o);
    }
    CloseLibrary(DataTypesBase);
    return s;
}
