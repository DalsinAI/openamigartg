/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Voices' output for the tests (host_voices.c): ACAHI's board itself
 * (tests/openal/acahi, the runtime's acahi.c), with this file in the part of
 * AHI and acaudio.audio's Host mix. */
#ifndef HOST_VOICES_H
#define HOST_VOICES_H

#include "al_internal.h"

struct acahi;

extern int host_voices_fitted;          /* the board and its Host mix mode are there */
extern ALuint host_voices_free;         /* voices the board can give (32 less what others hold) */
extern ALuint host_voices_chunk;        /* frames the board plays between two interrupts */
extern int host_voices_service_auto;    /* the task runs after an interrupt that wakes it */
extern unsigned long host_voices_sounds;    /* SetSound calls made */
extern unsigned long host_voices_vols;      /* SetVol calls made */
extern unsigned long host_voices_freqs;     /* SetFreq calls made */
extern unsigned long host_voices_refused;   /* SetSound calls the board refused */
extern int host_voices_lock_depth;          /* al_vout_lock taken and not let go */

/* The next frames of the device's board into out (16-bit stereo, clamped
 * as the instance's ring has it), its interrupts served every
 * host_voices_chunk frames. */
void host_voices_render(ALCdevice *d, ALshort *out, ALuint frames);
/* The device's board, to look at its registers. */
struct acahi *host_voices_board(ALCdevice *d);
/* Board voice k's frame now (V_POS), and whether it plays (V_CMD bit 0). */
ALuint host_voices_pos(ALCdevice *d, ALuint k);
/* The frame of its buffer board voice k plays now (V_POS less where the
 * buffer starts in the sound). */
ALuint host_voices_buffer_frame(ALCdevice *d, ALuint k);
int host_voices_on(ALCdevice *d, ALuint k);
/* The voices the device holds. */
ALuint host_voices_held(ALCdevice *d);

#endif
