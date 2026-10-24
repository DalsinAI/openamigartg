/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The lab's scenes (Design-OpenAL section 6, "In a lab"), shared by ALLab
 * on the Amiga (allab.c) and the PC's references (allab_ref.c), so both
 * play the same samples:
 *   1. a tone: 440 Hz, 1 second at 44,100 Hz, gain 0.8, in the centre;
 *   2. a sweep: 441 Hz at 22,050 Hz, looping, round the listener at 2 units
 *      in 4 seconds (from the left, behind, right, ahead), its position
 *      moved every 441 frames of the buffer (882 of the board's);
 *   3. a stream: alstream.raw (16-bit big-endian mono at 22,050 Hz, 5
 *      seconds, written by allab_ref), in buffers of 4,096 frames.
 * Half a second of silence goes between them. */
#ifndef ALLAB_SCENES_H
#define ALLAB_SCENES_H

#include <math.h>

#define AS_PI 3.14159265358979323846
#define AS_TONE_RATE 44100
#define AS_TONE_FRAMES 44100
#define AS_TONE_GAIN 0.8f
#define AS_SWEEP_RATE 22050
#define AS_SWEEP_FRAMES 22050
#define AS_SWEEP_STEPS 200
#define AS_SWEEP_STEP 441               /* buffer frames a step */
#define AS_STREAM_RATE 22050
#define AS_STREAM_FRAMES (5 * 22050)
#define AS_STREAM_BUFFER 4096
#define AS_GAP_SECONDS 0.5

static void as_tone(short *pcm)
{
    int i;
    for (i = 0; i < AS_TONE_FRAMES; i++) {
        pcm[i] = (short)(12000.0 * sin(2.0 * AS_PI * 440.0 * i / AS_TONE_RATE));
    }
}

static void as_sweep_tone(short *pcm)
{
    int i;
    for (i = 0; i < AS_SWEEP_FRAMES; i++) {
        pcm[i] = (short)(12000.0 * sin(2.0 * AS_PI * 441.0 * i / AS_SWEEP_RATE));
    }
}

/* Step t's position: from the left (-x), behind (+z), right (+x), ahead (-z). */
static void as_sweep_position(int t, float *x, float *z)
{
    double a = 2.0 * AS_PI * t / AS_SWEEP_STEPS;
    *x = (float)(-2.0 * cos(a));
    *z = (float)(2.0 * sin(a));
}

/* Frame i of the stream: a chord of three notes and a slow chirp, rising
 * and falling, so a gap, a repeat or a misplaced buffer shows. */
static short as_stream_frame(long i)
{
    double t = (double)i / AS_STREAM_RATE;
    double env = 0.5 - 0.5 * cos(2.0 * AS_PI * t / 5.0);
    double v = 0.30 * sin(2.0 * AS_PI * 220.0 * t) + 0.20 * sin(2.0 * AS_PI * 277.18 * t) +
               0.15 * sin(2.0 * AS_PI * 329.63 * t) + 0.25 * sin(2.0 * AS_PI * (300.0 + 60.0 * t) * t);
    return (short)(26000.0 * env * v);
}

#endif
