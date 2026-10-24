/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's core: the objects, the locks and the back-end interface.
 * Written by the Team from the OpenAL 1.1 specification and the extension
 * texts (README.md). The same files build for the Amiga (the module, and
 * libopenal_static.a) and, with AL_HOST defined, for a PC, where
 * tests/openal checks them.
 *
 * The core holds every object and the timeline of every source (which queue
 * entry plays, and where in it). A back end makes the sound:
 *   Mix (al_mix.c)  the module mixes its sources into one 16-bit stereo
 *                   stream, which an output plays: al_ahi.c through
 *                   ahi.device on the Amiga, tests/openal/host_out.c on a PC;
 *   Voices          the board mixes (ACAHI's Host mix): each playing source
 *                   on one of its voices (al_voices.c), through AHI on the
 *                   Amiga (al_ahi_voices.c) and a model of the board on a
 *                   PC (tests/openal/host_voices.c).
 * Both take a source's gains and pitch from al_source_params (al_spatial.c)
 * and move its timeline with al_source_next. */
#ifndef AL_INTERNAL_H
#define AL_INTERNAL_H

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#ifdef AL_HOST
#include <pthread.h>
typedef pthread_mutex_t al_mutex;
#else
#include <exec/semaphores.h>
typedef struct SignalSemaphore al_mutex;
#endif

/* Recursive locks: a pthread mutex on a PC, a SignalSemaphore on the Amiga. */
void al_mutex_init(al_mutex *m);
void al_mutex_lock(al_mutex *m);
void al_mutex_unlock(al_mutex *m);

/* Two locks, always taken in this order:
 *   al_device_lock  devices and contexts made and ended (alcOpenDevice,
 *                   alcCloseDevice, alcCreateContext, alcDestroyContext);
 *                   a back end's start and stop run under it alone, so they
 *                   can wait for an output thread that takes al_state_lock;
 *   al_state_lock   every object's state: each AL call, the other ALC
 *                   calls, and the mixer while it renders. */
extern al_mutex al_device_lock;
extern al_mutex al_state_lock;
#define AL_LOCK() al_mutex_lock(&al_state_lock)
#define AL_UNLOCK() al_mutex_unlock(&al_state_lock)

/* ---- Objects ------------------------------------------------------------------- */

typedef struct ALbuffer ALbuffer;
typedef struct ALsource ALsource;

/* A buffer (5): the data as the module keeps it. 8-bit data is stored
 * signed, float data as 16 bit, and multichannel data folded to stereo
 * (al_buffer.c); AL_BITS, AL_CHANNELS and AL_SIZE give this stored form
 * (5.3.3: "the internal representation of the buffer data"). */
struct ALbuffer {
    ALuint name;
    ALCdevice *device;
    ALenum format;              /* as alBufferData was given it: 0 before any data */
    ALint frequency;            /* 0 before any data (table 5-1) */
    ALint channels;             /* stored: 1 or 2 */
    ALint bits;                 /* stored: 8 (signed) or 16 */
    ALuint frames;
    void *data;                 /* frames * channels * bits / 8 bytes, native order, at a multiple of 4 */
    void *alloc;                /* what was allocated for data */
    ALuint refs;                /* queue entries naming it, on every source */
};

/* What al_source_params works out from a source, its buffers and the
 * listener: the gains the back end applies, and the pitch. */
struct al_params {
    ALfloat left, right;        /* the gains into each channel */
    ALfloat gain;               /* the effective gain, times the listener's (3.5 steps 1 to 5) */
    ALfloat lateral;            /* -1 left, 0 centre, 1 right: the pan (0 for stereo sources) */
    ALfloat distance_gain;      /* step 1 alone */
    ALfloat cone_gain;          /* step 3 alone */
    ALfloat doppler;            /* the Doppler ratio f'/f (3.5.2) */
    ALfloat pitch;              /* AL_PITCH times doppler */
};

/* What changed in a source since its back end last looked (Voices sends
 * each kind to the board; Mix reads the state as it renders). */
#define AL_DIRTY_PARAMS 1       /* a property its gains or pitch come from */
#define AL_DIRTY_STATE 2        /* played, paused, stopped, rewound or moved (offset) */
#define AL_DIRTY_QUEUE 4        /* buffers attached, queued or unqueued */
#define AL_DIRTY_SEEK 8         /* the timeline jumped: played from the start or an offset, rewound */
#define AL_DIRTY_LOOP 16        /* AL_LOOPING changed */

/* A source (4.3): its properties, its queue and its timeline. */
struct ALsource {
    ALuint name;
    ALCcontext *context;
    ALsource *next, *prev;      /* the context's sources */

    ALfloat position[3], velocity[3], direction[3];
    ALfloat gain, min_gain, max_gain, pitch;
    ALfloat ref_distance, rolloff, max_distance;
    ALfloat cone_inner, cone_outer, cone_outer_gain;
    ALboolean relative, looping;
    ALenum type;                /* AL_UNDETERMINED, AL_STATIC or AL_STREAMING */
    ALenum state;               /* AL_INITIAL, AL_PLAYING, AL_PAUSED or AL_STOPPED */

    /* The queue. An entry may be the NULL buffer (0), which plays nothing. */
    ALbuffer **queue;
    ALuint queued, room;
    /* The timeline: the entry playing (queued once stopped: all processed)
     * and where in it, in frames and a 16-bit fraction. */
    ALuint current, pos, frac;
    ALuint dequeued;            /* entries ever unqueued from the front: entry k is dequeued + k from the first ever queued */
    /* An offset set while inactive, for the next alSourcePlay: its token
     * (AL_SEC_OFFSET, ...; 0 for none) and value. */
    ALenum pending_type;
    ALdouble pending_value;

    unsigned dirty;             /* AL_DIRTY_* */
    struct al_params params;    /* worked out by al_context_update */

    /* Mix's own (al_mix.c). */
    ALint mix_cur[2];           /* the gains it applies, Q22 */
    ALint mix_inc[2];           /* their change each frame while ramping */
    ALint mix_target[2];        /* the gains it ramps to, Q14 */
    ALuint mix_ramp;            /* frames of the ramp left */
    ALuint mix_pitch;           /* params.pitch, 16.16 */
    ALboolean mix_fresh;        /* just played: start at the target, no ramp */

    /* Voices' own (al_voices.c). */
    ALint voice;                /* the board's voice it plays on, or -1 (none: virtual while it plays) */
    ALuint vclock;              /* the output's clock when its virtual time last moved */
};

/* A context (6.2): the listener, the global state and the sources. */
struct ALCcontext_struct {
    ALCdevice *device;
    ALCcontext *next;           /* the device's contexts */
    ALenum error;
    ALboolean suspended;        /* alcSuspendContext: changes wait for alcProcessContext */
    ALboolean dirty;            /* the listener or the global state changed: every source's parameters */
    ALboolean changed;          /* this call changed something (al_leave tells the back end) */

    ALfloat listener_position[3], listener_velocity[3];
    ALfloat listener_at[3], listener_up[3];
    ALfloat listener_gain;
    ALenum distance_model;
    ALfloat doppler_factor, doppler_velocity, speed_of_sound;

    ALsource *sources;
    ALuint nsources, max_sources;
    ALint mono_sources, stereo_sources;     /* the hints granted (ALC_MONO_SOURCES, ALC_STEREO_SOURCES) */
};

struct al_backend;

/* A device (6.1): one output. */
struct ALCdevice_struct {
    ALCdevice *next;            /* every open device */
    ALCenum error;
    const struct al_backend *backend;
    ALCcontext *contexts;
    ALuint frequency;           /* the mixing rate, Hz */
    ALuint refresh;             /* updates a second */
    ALuint update_frames;       /* frames in each update: frequency / refresh */
    ALCboolean connected;       /* ALC_CONNECTED (ALC_EXT_disconnect) */
    ALCboolean running;         /* the back end plays: from the first context to the last */
    ALuint buffers;             /* buffers made on it and not yet deleted */
    void *out;                  /* the back end's output's own */
    void *voices;               /* Voices' own (al_voices.c) */
    ALint *mix;                 /* Mix's accumulator: update_frames stereo frames */
};

/* ---- Back ends ----------------------------------------------------------------- */

struct al_backend {
    const char *name;           /* the device's specifier */
    /* Whether this back end has an output now, for the default device
     * (ALC_DEFAULT_DEVICE_SPECIFIER); NULL: always. */
    ALCboolean (*available)(void);
    /* alcOpenDevice: ALC_FALSE when there is no output. Sets the device's
     * frequency to the output's own rate when it knows it (else 0). Runs
     * in the caller's task. */
    ALCboolean (*open)(ALCdevice *device);
    void (*close)(ALCdevice *device);
    /* The first context: frequency and update_frames are set. ALC_FALSE
     * when it can't start (memory); an output that starts but has no sound
     * calls al_device_disconnect. Under al_device_lock alone. */
    ALCboolean (*start)(ALCdevice *device);
    void (*stop)(ALCdevice *device);
    /* A context's sources or listener changed (also while it is
     * suspended: context->suspended says so), or alcProcessContext ended a
     * suspend. Under al_state_lock. NULL: nothing to tell. */
    void (*update)(ALCcontext *context);
    /* The start of each AL call on a context of the device, under
     * al_state_lock: what the output played since the last call. NULL:
     * nothing. */
    void (*sync)(ALCcontext *context);
    /* A source of the device is about to be freed, under al_state_lock.
     * NULL: nothing. */
    void (*forget)(ALsource *source);
    unsigned flags;             /* AL_BACKEND_* */
};

/* The output's rate and update rate are its own: a context's
 * ALC_FREQUENCY and ALC_REFRESH are hints it doesn't take. */
#define AL_BACKEND_FIXED_RATE 1

extern const struct al_backend al_backend_mix;
extern const struct al_backend al_backend_voices;

/* Mix's output (al_ahi.c on the Amiga, tests/openal/host_out.c on a PC):
 * the same calls as a back end's. It plays what al_mix_render gives it. */
ALCboolean al_out_open(ALCdevice *device);
void al_out_close(ALCdevice *device);
ALCboolean al_out_start(ALCdevice *device);
void al_out_stop(ALCdevice *device);

/* Mix: frames stereo frames of 16-bit native samples into out, from every
 * context of the device. Takes al_state_lock. */
void al_mix_render(ALCdevice *device, ALshort *out, ALuint frames);

/* ALC_EXT_disconnect: the output has gone. Every source of the device stops,
 * with all its buffers processed, and none plays again. Takes al_state_lock. */
void al_device_disconnect(ALCdevice *device);

/* ---- Voices' output -------------------------------------------------------------- */

/* Voices (al_voices.c) plays each source on one of the board's voices
 * through these: AHI on the Amiga (al_ahi_voices.c: AHI_AllocAudio on
 * ACAHI's Host mix mode), a model of the board on a PC
 * (tests/openal/host_voices.c). A voice plays a segment of a sound (AHI's
 * SetSound): with imm at once, else when the one playing ends; that end is
 * an event (AHI's SoundFunc), which the output passes to al_voices_event.
 * The sounds are AHI's "all of memory" sounds, one for each stored format:
 * a segment's offset is where its frames are. */
#define AL_VOICES_MAX 32
#define AL_VOUT_NOSOUND 0xFFFFu

/* Whether the board has a Host mix mode, for the default device. */
ALCboolean al_vout_available(void);
/* Take n of the board's voices, with every voice silent; ALC_FALSE when
 * it can't give them. Sets the device's frequency to the board's rate. */
ALCboolean al_vout_open(ALCdevice *device, ALuint n);
void al_vout_close(ALCdevice *device);
/* The task that does al_voices_service when the events wake it. */
ALCboolean al_vout_start(ALCdevice *device);
void al_vout_stop(ALCdevice *device);
/* The sound for data of bits and channels, and where data is in it (its
 * frame number). */
unsigned al_vout_sound(ALCdevice *device, ALint bits, ALint channels);
ALuint al_vout_frame(ALCdevice *device, const void *data, ALuint frames, ALint bits, ALint channels);
/* A voice's segment: frames of sound from frame offset; AL_VOUT_NOSOUND
 * for silence. imm: now (and an end not yet told is forgotten); else when
 * the segment playing ends. */
void al_vout_play(ALCdevice *device, ALuint voice, unsigned sound, ALuint offset, ALuint frames, int imm);
/* Now: frames a second (0 holds it where it is), and the volume and pan
 * (16.16: the left channel gets volume * (1 - pan), the right volume * pan;
 * a stereo sound's pan is a balance). */
void al_vout_freq(ALCdevice *device, ALuint voice, ALuint freq);
void al_vout_vol(ALCdevice *device, ALuint voice, ALint vol, ALint pan);
/* Keep the events out (al_voices_event, al_voices_info) meanwhile. */
void al_vout_lock(ALCdevice *device);
void al_vout_unlock(ALCdevice *device);
/* Output frames played, as far as the module knows (the board's timer). */
ALuint al_vout_clock(ALCdevice *device);
/* From an event: al_voices_service should run soon. */
void al_vout_wake(ALCdevice *device);

/* The output's calls into Voices. The first two come at interrupt time
 * (AHI's SoundFunc and channel info), take no lock and use no FPU. */
void al_voices_event(ALCdevice *device, ALuint voice);              /* its segment ended */
void al_voices_info(ALCdevice *device, ALuint voice, ALuint frame); /* the frame it plays now */
void al_voices_service(ALCdevice *device);                          /* the task: takes al_state_lock */
/* A voice's volume and pan for a source's params (16.16). */
void al_voices_settings(const struct al_params *params, ALint channels, ALint *vol, ALint *pan);

/* ---- The core's own calls ------------------------------------------------------- */

/* Set up the locks and tables; end every device and free everything (the
 * module's open and close). */
void al_core_init(void);
void al_core_shutdown(void);

extern ALCcontext *al_current;

/* An AL call's start and end: the current context with al_state_lock held,
 * or NULL (nothing held) when there is none. al_leave tells the back end
 * what changed and lets go. */
ALCcontext *al_enter(void);
void al_leave(ALCcontext *context);

/* The first error stays until alGetError or alcGetError (2.7, 6.3.6). */
void al_set_error(ALCcontext *context, ALenum error);
void alc_set_error(ALCdevice *device, ALCenum error);
ALCboolean alc_device_valid(const ALCdevice *device);

/* Names (2.11): a growing table, a name being an index + 1. */
struct al_names {
    void **items;
    ALuint room;
    ALuint hint;                /* where the next free name is looked for */
};
extern struct al_names al_buffer_names, al_source_names;
ALuint al_names_add(struct al_names *names, void *item);    /* 0: no memory */
void *al_names_get(const struct al_names *names, ALuint name);
void al_names_remove(struct al_names *names, ALuint name);
void al_names_free(struct al_names *names);

ALbuffer *al_buffer_get(ALCcontext *context, ALuint name);  /* NULL unless on the context's device */
void al_buffer_free(ALbuffer *buffer);
ALsource *al_source_get(ALCcontext *context, ALuint name);  /* NULL unless the context's */
void al_source_free(ALsource *source);                      /* unlinked, its buffers let go */

/* The source changed in a way the back end may need to see. */
void al_source_touch(ALsource *source, unsigned dirty);

/* The timeline. */
ALuint al_source_processed(const ALsource *source);         /* AL_BUFFERS_PROCESSED */
void al_source_stop_all(ALsource *source);                  /* AL_STOPPED, every buffer processed */
/* The current entry has played to its end: move to the next, wrapping
 * round the queue when looping. 0 when the source stopped. */
int al_source_next(ALsource *source);
/* The channels of the source's buffers (1 or 2; 1 with none). */
ALint al_source_channels(const ALsource *source);
/* The first buffer of its queue with data, which gives the queue's format
 * and frequency (4.3.5); NULL with none. */
const ALbuffer *al_source_format(const ALsource *source);
/* Time going on for a source no back end plays (Voices' virtual sources):
 * its timeline moves out frames of an output at out_rate, at its rate times
 * params.pitch, as Mix's silent sources move. */
void al_source_advance(ALsource *source, ALuint out, ALuint out_rate);

/* Work out a source's params (al_spatial.c), and every changed source's of
 * a context (al_spatial.c: al_context_update), unless it is suspended. */
void al_source_params(const ALCcontext *context, const ALsource *source, ALint channels, struct al_params *params);
void al_context_update(ALCcontext *context);
/* A distance model's gain (3.4): 1 where its formula can't be evaluated. */
double al_distance_gain(ALenum model, double distance, double ref, double rolloff, double max);

/* Look-ups by name, for alGetProcAddress, alcGetProcAddress and the two
 * GetEnumValue calls (alc.c). */
void *al_proc_address(const char *name);
ALboolean al_enum_value(const char *name, ALenum *value);
/* Whether name is in a space-separated list, ignoring case (6.3.3, 7.1). */
ALboolean al_list_has(const char *list, const char *name);
extern const char al_extensions[];
extern const char alc_extensions[];

/* A finite float, as every float property must be. */
#define AL_FINITE(x) ((x) == (x) && (x) - (x) == 0)

#endif
