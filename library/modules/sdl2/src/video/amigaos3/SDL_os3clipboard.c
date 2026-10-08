/*
  SDL 2 clipboard for AmigaOS 3.x: clipboard.device unit 0, IFF FTXT.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).

  SDL's clipboard text is UTF-8; the Amiga clipboard's FTXT CHRS chunks are
  ISO-8859-1 (the Amiga's own character set). Text is converted both ways;
  a character Latin-1 hasn't got is written as '?'. On AmigaChrome, ACClip
  carries unit 0 to and from the PC's clipboard, so SDL programs share it.

  iffparse.library is opened for each call and closed after, so a program
  that never touches the clipboard never opens it.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_AMIGAOS3

/* SDL's own base, so a program that opens iffparse.library itself keeps its own. */
#define IFFParseBase SDL_OS3_IFFParseBase
#include <proto/exec.h>
#include <proto/iffparse.h>
#include <libraries/iffparse.h>
#include <devices/clipboard.h>

#include "SDL_os3clipboard.h"

#define ID_FTXT MAKE_ID('F', 'T', 'X', 'T')
#define ID_CHRS MAKE_ID('C', 'H', 'R', 'S')

struct Library *IFFParseBase = NULL;

/* UTF-8 to Latin-1. Returns a new string, or NULL. */
static char *OS3_UTF8ToLatin1(const char *text, LONG *outlen)
{
    size_t len = SDL_strlen(text);
    char *out = (char *)SDL_malloc(len + 1);
    const Uint8 *s = (const Uint8 *)text;
    char *d = out;

    if (!out) {
        return NULL;
    }
    while (*s) {
        Uint32 c = *s++;
        if (c >= 0x80) {
            int more = 0;
            if ((c & 0xE0) == 0xC0) { c &= 0x1F; more = 1; }
            else if ((c & 0xF0) == 0xE0) { c &= 0x0F; more = 2; }
            else if ((c & 0xF8) == 0xF0) { c &= 0x07; more = 3; }
            else { c = '?'; }
            while (more-- > 0 && (*s & 0xC0) == 0x80) {
                c = (c << 6) | (*s++ & 0x3F);
            }
            if (c > 0xFF) {
                c = '?';
            }
        }
        if (c == '\r') {
            continue; /* Amiga lines end in LF alone */
        }
        *d++ = (char)c;
    }
    *d = '\0';
    *outlen = (LONG)(d - out);
    return out;
}

/* Latin-1 to UTF-8, appending to a growing buffer. */
static int OS3_AppendLatin1(char **buf, size_t *len, size_t *cap, const Uint8 *s, LONG n)
{
    LONG i;
    if (*len + (size_t)n * 2 + 1 > *cap) {
        size_t ncap = (*len + (size_t)n * 2 + 1) * 2;
        char *nb = (char *)SDL_realloc(*buf, ncap);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = ncap;
    }
    for (i = 0; i < n; i++) {
        Uint8 c = s[i];
        if (c < 0x80) {
            (*buf)[(*len)++] = (char)c;
        } else {
            (*buf)[(*len)++] = (char)(0xC0 | (c >> 6));
            (*buf)[(*len)++] = (char)(0x80 | (c & 0x3F));
        }
    }
    (*buf)[*len] = '\0';
    return 0;
}

/* Open the IFF handle on clipboard unit 0, for reading or writing. */
static struct IFFHandle *OS3_OpenClip(LONG mode)
{
    struct IFFHandle *iff;

    IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 39);
    if (!IFFParseBase) {
        SDL_SetError("Can't open iffparse.library");
        return NULL;
    }
    iff = AllocIFF();
    if (!iff) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        SDL_OutOfMemory();
        return NULL;
    }
    iff->iff_Stream = (ULONG)OpenClipboard(PRIMARY_CLIP);
    if (!iff->iff_Stream) {
        FreeIFF(iff);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        SDL_SetError("Can't open clipboard.device");
        return NULL;
    }
    InitIFFasClip(iff);
    if (OpenIFF(iff, mode) != 0) {
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
        FreeIFF(iff);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        SDL_SetError("Can't start the clipboard");
        return NULL;
    }
    return iff;
}

static void OS3_CloseClip(struct IFFHandle *iff)
{
    CloseIFF(iff);
    CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    FreeIFF(iff);
    CloseLibrary(IFFParseBase);
    IFFParseBase = NULL;
}

int OS3_SetClipboardText(_THIS, const char *text)
{
    struct IFFHandle *iff;
    LONG len = 0;
    char *latin1;
    int ok = 0;

    latin1 = OS3_UTF8ToLatin1(text, &len);
    if (!latin1) {
        return SDL_OutOfMemory();
    }
    iff = OS3_OpenClip(IFFF_WRITE);
    if (!iff) {
        SDL_free(latin1);
        return -1;
    }
    if (PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) == 0) {
        if (PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN) == 0) {
            if (WriteChunkBytes(iff, latin1, len) == len) {
                ok = 1;
            }
            PopChunk(iff);
        }
        PopChunk(iff);
    }
    OS3_CloseClip(iff);
    SDL_free(latin1);
    return ok ? 0 : SDL_SetError("Can't write the clipboard");
}

char *OS3_GetClipboardText(_THIS)
{
    struct IFFHandle *iff;
    char *buf = NULL;
    size_t len = 0, cap = 0;

    iff = OS3_OpenClip(IFFF_READ);
    if (iff) {
        if (StopChunk(iff, ID_FTXT, ID_CHRS) == 0) {
            /* Every CHRS of every FTXT in the clip, in order. */
            for (;;) {
                LONG err = ParseIFF(iff, IFFPARSE_SCAN);
                struct ContextNode *cn;
                if (err == IFFERR_EOC) {
                    continue;
                }
                if (err != 0) {
                    break;
                }
                cn = CurrentChunk(iff);
                if (cn && cn->cn_Type == ID_FTXT && cn->cn_ID == ID_CHRS) {
                    Uint8 tmp[512];
                    LONG got;
                    while ((got = ReadChunkBytes(iff, tmp, sizeof(tmp))) > 0) {
                        if (OS3_AppendLatin1(&buf, &len, &cap, tmp, got) < 0) {
                            break;
                        }
                    }
                }
            }
        }
        OS3_CloseClip(iff);
    }
    if (!buf) {
        buf = SDL_strdup("");
    }
    return buf;
}

SDL_bool OS3_HasClipboardText(_THIS)
{
    char *text = OS3_GetClipboardText(_this);
    SDL_bool has = (text && *text) ? SDL_TRUE : SDL_FALSE;
    SDL_free(text);
    return has;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */
