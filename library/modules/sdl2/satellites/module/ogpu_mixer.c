/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2_mixer's decoders on the x86 or ARM64 cores: Ogg Vorbis, MP3, FLAC
 * and tracker modules (MOD, XM, S3M, IT...) decoded by media.decode/1
 * (DalsinAI/openamigaservice docs/MEDIA_DECODE.md: FFmpeg, and libopenmpt
 * for modules) on the services card or a paired Cradle, and mixed into
 * SDL's audio device here like any other music. patches/sdl2_mixer puts
 * these four interfaces before SDL2_mixer's own, so a music, and a chunk
 * (Mix_LoadWAV of an Ogg file decodes through the music interfaces), goes
 * to the cores first; when no card or Cradle offers the service,
 * CreateFromRW says no and SDL2_mixer's own decoder (stb_vorbis, minimp3,
 * dr_flac, libxmp) plays it on this CPU.
 *
 * The service answers 16-bit big-endian PCM at the file's rate halved until
 * it is at most the mixer's, so the 68k only converts the rate and format
 * (SDL_AudioStream). A music is decoded two seconds at a time: as each
 * piece arrives the next is asked for, so the next is decoding while this
 * one plays. The piece is asked for from SDL's audio thread and waited for
 * there, through sat_service.c's port, which any task may wait on.
 *
 * SDL_MIXER_DECODER (a hint, or SetEnv): "auto" (the default) or "cpu"
 * (SDL2_mixer's own decoders only). */
#include "SDL.h"
#include "music.h"

#include <exec/memory.h>
#include <proto/exec.h>

#include "sat_service.h"

#define MEDIA_DECODE "media.decode/1"
#define MD_PROBE 1
#define MD_DECODE 2
#define KIND_SOUND 3
#define PIECE_SECONDS 2
#define MAX_FILE (64UL << 20)

typedef struct {
    struct sat_svc *svc;
    Uint8 *file;
    ULONG len;
    ULONG frames, rate, channels;   /* the service's answer: the whole sound */
    Sint16 *piece;
    ULONG piece_frames;
    ULONG next;                     /* the first frame of the next piece */
    ULONG put;                      /* frames given to the stream since the start */
    int pending, eos;
    int volume, play_count;
    ULONG extra[4];
    SDL_AudioStream *stream;
    char format[5];
} SVC_Music;

static int svc_missing = 0;         /* no card or Cradle had it; asked again after Mix_CloseAudio */

static Uint32 be32(const Uint8 *p)
{
    return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3];
}

static void SVC_Ask(SVC_Music *m)
{
    struct OSBuffer buf[4];
    SDL_zeroa(buf);
    buf[0].ob_Data = m->file;
    buf[0].ob_Length = m->len;
    buf[1].ob_Data = m->piece;
    buf[1].ob_Length = m->piece_frames * m->channels * 2;
    sat_service_start(m->svc, MD_DECODE, m->next, 2, buf, m->extra);
    m->pending = 1;
}

static void SVC_Cancel(SVC_Music *m)
{
    if (m->pending) {
        sat_service_abort(m->svc);
        m->pending = 0;
    }
}

static void SVC_Delete(void *context)
{
    SVC_Music *m = (SVC_Music *)context;
    if (m->svc) {
        sat_service_close(m->svc);
    }
    if (m->file) {
        FreeVec(m->file);
    }
    if (m->piece) {
        FreeVec(m->piece);
    }
    if (m->stream) {
        SDL_FreeAudioStream(m->stream);
    }
    SDL_free(m);
}

static void *SVC_CreateFromRW(SDL_RWops *src, int freesrc)
{
    const char *mode = SDL_GetHint("SDL_MIXER_DECODER");
    SVC_Music *m;
    Sint64 start, size;
    struct OSBuffer buf[4];
    Uint8 info[24];
    ULONG frames = 0, rate = 0;

    if ((mode && SDL_strcasecmp(mode, "cpu") == 0) || svc_missing) {
        return NULL;
    }
    start = SDL_RWtell(src);
    size = SDL_RWsize(src);
    if (start < 0 || size <= start || size - start > (Sint64)MAX_FILE) {
        return NULL;
    }
    m = (SVC_Music *)SDL_calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->svc = sat_service_open(MEDIA_DECODE);
    if (!m->svc) {
        svc_missing = 1;
        SDL_free(m);
        return NULL;
    }
    m->len = (ULONG)(size - start);
    m->file = AllocVec(m->len, MEMF_ANY);
    if (!m->file || SDL_RWread(src, m->file, 1, m->len) != m->len) {
        SVC_Delete(m);
        return NULL;
    }
    m->extra[0] = music_spec.channels > 2 ? 2 : music_spec.channels;
    m->extra[1] = (ULONG)music_spec.freq;
    SDL_zeroa(buf);
    buf[0].ob_Data = m->file;
    buf[0].ob_Length = m->len;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof(info);
    if (sat_service_call(m->svc, MD_PROBE, 0, 2, buf, m->extra, &frames, &rate) != OSERR_OK ||
        be32(info) != KIND_SOUND || !frames || !rate) {
        SVC_Delete(m);
        return NULL;
    }
    m->frames = frames;
    m->rate = rate;
    m->channels = be32(info + 20);       /* kind, format, flags, frames, rate, channels */
    if (m->channels < 1 || m->channels > 2) {
        SVC_Delete(m);
        return NULL;
    }
    SDL_memcpy(m->format, info + 4, 4);
    m->piece_frames = rate * PIECE_SECONDS;
    m->piece = AllocVec(m->piece_frames * m->channels * 2, MEMF_ANY);
    m->stream = SDL_NewAudioStream(AUDIO_S16MSB, (Uint8)m->channels, (int)rate,
                                   music_spec.format, music_spec.channels, music_spec.freq);
    if (!m->piece || !m->stream) {
        SVC_Delete(m);
        return NULL;
    }
    m->volume = MIX_MAX_VOLUME;
    if (freesrc) {
        SDL_RWclose(src);
    }
    return m;
}

static void SVC_SetVolume(void *context, int volume)
{
    ((SVC_Music *)context)->volume = volume;
}

static int SVC_GetVolume(void *context)
{
    return ((SVC_Music *)context)->volume;
}

static int SVC_Seek(void *context, double position)
{
    SVC_Music *m = (SVC_Music *)context;
    Sint64 f = (Sint64)(position * (double)m->rate);
    SVC_Cancel(m);
    SDL_AudioStreamClear(m->stream);
    m->next = f < 0 ? 0 : (f > (Sint64)m->frames ? m->frames : (ULONG)f);
    m->put = m->next;
    m->eos = m->next >= m->frames;
    if (!m->eos) {
        SVC_Ask(m);                 /* the first piece decodes before it is wanted */
    }
    return 0;
}

static int SVC_Play(void *context, int play_count)
{
    SVC_Music *m = (SVC_Music *)context;
    m->play_count = play_count;
    return SVC_Seek(m, 0.0);
}

static void SVC_Stop(void *context)
{
    SVC_Music *m = (SVC_Music *)context;
    SVC_Cancel(m);
    SDL_AudioStreamClear(m->stream);
}

static int SVC_GetSome(void *context, void *data, int bytes, SDL_bool *done)
{
    SVC_Music *m = (SVC_Music *)context;
    ULONG got = 0, rate = 0;
    int filled = SDL_AudioStreamGet(m->stream, data, bytes);

    if (filled != 0) {
        return filled;
    }
    if (!m->play_count) {
        *done = SDL_TRUE;
        return 0;
    }
    if (!m->pending && !m->eos) {
        SVC_Ask(m);
    }
    if (m->pending) {
        LONG st = sat_service_wait(m->svc, &got, &rate);
        m->pending = 0;
        if (st != OSERR_OK) {
            if (st == OSERR_LOST || st == OSERR_CANCELLED) {
                svc_missing = 1;    /* new musics go to this CPU's decoders */
            }
            Mix_SetError("media.decode/1: error %ld", (long)st);
            return -1;
        }
        if (got) {
            if (SDL_AudioStreamPut(m->stream, m->piece, (int)(got * m->channels * 2)) < 0) {
                return -1;
            }
            m->next += got;
            m->put += got;
        }
        if (!got || m->next >= m->frames) {
            m->eos = 1;
        } else {
            SVC_Ask(m);             /* the next piece decodes while this one plays */
        }
        if (got) {
            return 0;
        }
    }
    /* The end of the sound: again, or done. */
    if (m->play_count == 1) {
        m->play_count = 0;
        SDL_AudioStreamFlush(m->stream);
    } else {
        if (m->play_count > 0) {
            m->play_count--;
        }
        SVC_Seek(m, 0.0);
    }
    return 0;
}

static int SVC_GetAudio(void *context, void *data, int bytes)
{
    SVC_Music *m = (SVC_Music *)context;
    return music_pcm_getaudio(context, data, bytes, m->volume, SVC_GetSome);
}

static double SVC_Tell(void *context)
{
    SVC_Music *m = (SVC_Music *)context;
    int per = music_spec.channels * (SDL_AUDIO_BITSIZE(music_spec.format) / 8);
    double queued = per ? (double)SDL_AudioStreamAvailable(m->stream) / per * m->rate / music_spec.freq : 0.0;
    double played = (double)m->put - queued;
    return played < 0 ? 0.0 : played / m->rate;
}

static double SVC_Duration(void *context)
{
    SVC_Music *m = (SVC_Music *)context;
    return (double)m->frames / m->rate;
}

static void SVC_Close(void)
{
    svc_missing = 0;
}

#define SVC_INTERFACE(name, tag, api, type)                                     \
    Mix_MusicInterface name = {                                                 \
        tag, api, type, SDL_FALSE, SDL_FALSE,                                   \
        NULL,   /* Load */                                                      \
        NULL,   /* Open */                                                      \
        SVC_CreateFromRW,                                                       \
        NULL,   /* CreateFromFile */                                            \
        SVC_SetVolume, SVC_GetVolume, SVC_Play,                                 \
        NULL,   /* IsPlaying */                                                 \
        SVC_GetAudio,                                                           \
        NULL,   /* Jump */                                                      \
        SVC_Seek, SVC_Tell, SVC_Duration,                                       \
        NULL, NULL, NULL,   /* LoopStart, LoopEnd, LoopLength */                \
        NULL,   /* GetMetaTag */                                                \
        NULL, NULL,   /* GetNumTracks, StartTrack */                            \
        NULL, NULL,   /* Pause, Resume */                                       \
        SVC_Stop, SVC_Delete, SVC_Close,                                        \
        NULL    /* Unload */                                                    \
    };

SVC_INTERFACE(Mix_MusicInterface_OGPU_OGG, "OGG (media.decode)", MIX_MUSIC_OGG, MUS_OGG)
SVC_INTERFACE(Mix_MusicInterface_OGPU_MP3, "MP3 (media.decode)", MIX_MUSIC_MINIMP3, MUS_MP3)
SVC_INTERFACE(Mix_MusicInterface_OGPU_FLAC, "FLAC (media.decode)", MIX_MUSIC_DRFLAC, MUS_FLAC)
SVC_INTERFACE(Mix_MusicInterface_OGPU_MOD, "MOD (media.decode)", MIX_MUSIC_LIBXMP, MUS_MOD)
