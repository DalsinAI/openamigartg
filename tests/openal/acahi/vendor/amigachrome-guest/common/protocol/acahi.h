/* ACAHI: AmigaChrome's sound board for AHI, protocol acahi-v1.
 *
 * Design: docs/architecture/ACAHI_MODERN_AUDIO_SUBSYSTEM_SPEC.md (which
 * supersedes ACAHI_AHI_GUEST_INTERFACE_DESIGN.md where they differ) and the
 * AUD-4 review (docs/reviews/2026-10-02_GFX-10_GFX-11_AUD-4.md). The
 * register reference is docs/autodocs/acahi.doc.
 *
 * Zorro II, manufacturer AmigaChrome Team (0xDA15, amigachrome_zorro.h), product 4, 64 KiB of
 * registers, no ROM. The guest's driver is DEVS:AHI/acaudio.audio.
 *
 * The board plays two things into the instance's second audio ring
 * (PORT.ahi, beside Paula's PORT.aud), both timed by the machine's audio
 * clock, never the CPU's:
 *
 *   - the STREAM: buffers AHI has already mixed (16 or 32 bit, mono or
 *     stereo, any rate from 4 to 96 kHz), copied when they are submitted
 *     and resampled to the ring's 44.1 kHz. A buffer is DONE when its last
 *     frame has played.
 *   - the VOICES: 32 channels the host mixes itself, reading AHI's sounds
 *     straight from guest RAM, with AHI's volume, panning, frequency and
 *     "play this next" semantics. The 68k mixes nothing.
 *
 * Registers are 32 bits, big-endian, at the offsets below from the board's
 * base. Unknown registers read 0 and ignore writes. The board raises INT2
 * (PORTS) while INTREQ & INTENA is not 0.
 */
#ifndef ACAHI_PROTOCOL_H
#define ACAHI_PROTOCOL_H

#include "amigachrome_zorro.h"
#define ACAHI_MANUFACTURER      AMIGACHROME_MANUFACTURER
#define ACAHI_MANUFACTURER_OLD  AMIGACHROME_MANUFACTURER_OLD   /* looked for too */
#define ACAHI_PRODUCT           4
#define ACAHI_BOARD_SIZE        0x00010000UL

#define ACAHI_SIGNATURE         0x41434148UL      /* "ACAH" */
#define ACAHI_VERSION           1UL

#define ACAHI_OUT_RATE          44100UL           /* the ring's rate: the voices' mix rate */
#define ACAHI_MIN_RATE          4000UL            /* the stream's */
#define ACAHI_MAX_RATE          96000UL
#define ACAHI_QUEUE             8UL               /* stream buffers in flight */
#define ACAHI_MAX_FRAMES        65536UL           /* frames in one stream buffer */
#define ACAHI_VOICES            32UL
#define ACAHI_SOUNDS            1024UL            /* sound slots (AHI's sound numbers 0-1023) */

/* ---- identity and control ---- */
#define ACAHI_REG_ID            0x00   /* R  "ACAH" */
#define ACAHI_REG_VERSION       0x04   /* R  1 */
#define ACAHI_REG_CAPS          0x08   /* R  ACAHI_CAP_* */
#define ACAHI_REG_STATUS        0x0C   /* R  ACAHI_ST_* */
#define ACAHI_REG_CONTROL       0x10   /* RW ACAHI_CTL_*; ACAHI_CTL_RESET is a command, never read back */
#define ACAHI_REG_RATE          0x14   /* RW the stream's frames a second, ACAHI_MIN_RATE..ACAHI_MAX_RATE */
#define ACAHI_REG_FORMAT        0x18   /* RW the stream's sample type, AHI's numbers: 1 M16S, 3 S16S, 8 M32S, 10 S32S */
#define ACAHI_REG_OUTRATE       0x1C   /* R  ACAHI_OUT_RATE */

/* ---- the stream ---- */
#define ACAHI_REG_PB_ADDR       0x20   /* RW a buffer in guest RAM (Chip or Fast) */
#define ACAHI_REG_PB_FRAMES     0x24   /* RW its frames, 1..ACAHI_MAX_FRAMES */
#define ACAHI_REG_PB_SEQ        0x28   /* RW its sequence number: DONE reports it */
#define ACAHI_REG_PB_SUBMIT     0x2C   /* W  any value: copy the buffer in and queue it. R: ACAHI_PB_* for the last submit */
#define ACAHI_REG_PB_DONE       0x30   /* R  the sequence number of the last buffer played to its end */
#define ACAHI_REG_PB_QUEUED     0x34   /* R  frames queued, not yet played (stream rate) */
#define ACAHI_REG_PB_FREE       0x38   /* R  buffers that can still be submitted (0..ACAHI_QUEUE) */
#define ACAHI_REG_LATENCY       0x3C   /* R  the queue as output frames (44.1 kHz) */

/* ---- interrupts and telemetry ---- */
#define ACAHI_REG_INTREQ        0x40   /* R  ACAHI_INT_* raised */
#define ACAHI_REG_INTENA        0x44   /* RW ACAHI_INT_* that raise INT2 */
#define ACAHI_REG_INTACK        0x48   /* W  1 bits clear INTREQ's */
#define ACAHI_REG_UNDERRUNS     0x4C   /* R  times the running stream ran dry */
#define ACAHI_REG_ERRORS        0x50   /* R  refused submits, sounds and voice commands */
#define ACAHI_REG_FRAME         0x54   /* R  output frames played since reset (low 32 bits): the audio clock */

/* ---- the timer: AHI's PlayerFunc for the voices ---- */
#define ACAHI_REG_TIMER_FREQ    0x60   /* RW ticks a second, 16.16 fixed point (AHI's Fixed); 0 off */
#define ACAHI_REG_TIMER_TICKS   0x64   /* R  ticks since it was set */

/* ---- the voices ---- */
#define ACAHI_REG_VOICES        0x70   /* R  ACAHI_VOICES */
#define ACAHI_REG_VOICE_EVENTS  0x74   /* R  a bit a voice: its next sound started (AHI's SoundFunc). W: 1 bits clear */
#define ACAHI_REG_MASTER        0x78   /* RW the voices' gain, 16.16 (0x10000 unity; reset value) */
#define ACAHI_REG_SND_ID        0x80   /* RW the sound slot the next SND_CMD acts on */
#define ACAHI_REG_SND_TYPE      0x84   /* RW AHI's sample type: 0 M8S, 1 M16S, 2 S8S, 3 S16S, 8 M32S, 10 S32S */
#define ACAHI_REG_SND_ADDR      0x88   /* RW where its frames are in guest RAM */
#define ACAHI_REG_SND_LEN       0x8C   /* RW how many frames */
#define ACAHI_REG_SND_CMD       0x90   /* W  ACAHI_SND_LOAD or ACAHI_SND_UNLOAD. R: ACAHI_RES_* for the last one */

/* ---- the machine's levels, as the PC's mixer applies them ---- */
#define ACAHI_REG_LEVELS        0xA0   /* RW ACAHI_LEVEL_*: the volume, Paula's and AHI's levels, mute */
#define ACAHI_REG_LEVEL_SEQ     0xA4   /* R  counts changes to LEVELS, from either side */

#define ACAHI_REG_VOICE0        0x100  /* voice n's block at ACAHI_REG_VOICE0 + n * ACAHI_VOICE_STRIDE */
#define ACAHI_VOICE_STRIDE      0x20
#define ACAHI_V_SOUND           0x00   /* RW staged: a sound slot, or 0xFFFF (AHI_NOSOUND) for silence */
#define ACAHI_V_OFFSET          0x04   /* RW staged: the first frame */
#define ACAHI_V_LENGTH          0x08   /* RW staged: frames, signed; 0 the rest of the sound, below 0 played backwards from OFFSET */
#define ACAHI_V_FREQ            0x0C   /* RW staged: frames a second; 0xFFFFFFFF the output rate */
#define ACAHI_V_VOL             0x10   /* RW staged: 16.16, signed (below 0 inverts) */
#define ACAHI_V_PAN             0x14   /* RW staged: 16.16, 0 left, 0x8000 centre, 0x10000 right */
#define ACAHI_V_CMD             0x18   /* W  ACAHI_VC_* : apply what is staged. R: bit 0 playing */
#define ACAHI_V_POS             0x1C   /* R  the frame of its sound playing now */

/* CAPS */
#define ACAHI_CAP_PLAYBACK      (1UL << 0)
#define ACAHI_CAP_S16           (1UL << 3)
#define ACAHI_CAP_STEREO        (1UL << 4)
#define ACAHI_CAP_IRQ           (1UL << 5)
#define ACAHI_CAP_QUEUE         (1UL << 6)
#define ACAHI_CAP_LATENCY       (1UL << 7)
#define ACAHI_CAP_S32           (1UL << 8)
#define ACAHI_CAP_VOICES        (1UL << 16)       /* host-mixed channels */
#define ACAHI_CAP_TIMER         (1UL << 17)
#define ACAHI_CAP_LEVELS        (1UL << 18)       /* LEVELS and LEVEL_SEQ */

/* STATUS */
#define ACAHI_ST_HOST           (1UL << 0)        /* the ring reaches the host: there is sound out */
#define ACAHI_ST_STREAM         (1UL << 1)
#define ACAHI_ST_VOICES         (1UL << 2)

/* CONTROL */
#define ACAHI_CTL_STREAM        (1UL << 0)        /* play the stream's queue */
#define ACAHI_CTL_VOICES        (1UL << 1)        /* play the voices */
#define ACAHI_CTL_RESET         (1UL << 31)       /* stop both, drop the queue and every sound, timer off, INTREQ clear */

/* PB_SUBMIT results */
#define ACAHI_PB_OK             0
#define ACAHI_PB_FULL           1                 /* ACAHI_QUEUE buffers queued already */
#define ACAHI_PB_BADADDR        2                 /* not all in guest RAM */
#define ACAHI_PB_BADLEN         3                 /* 0, or more than ACAHI_MAX_FRAMES */
#define ACAHI_PB_BADFMT         4                 /* RATE or FORMAT not one the board plays */

/* INTREQ / INTENA / INTACK */
#define ACAHI_INT_DONE          (1UL << 0)        /* a stream buffer played to its end */
#define ACAHI_INT_VOICE         (1UL << 1)        /* VOICE_EVENTS has a bit */
#define ACAHI_INT_TIMER         (1UL << 2)        /* the timer ticked */
#define ACAHI_INT_UNDERRUN      (1UL << 3)        /* the running stream ran dry */

/* SND_CMD */
#define ACAHI_SND_LOAD          1
#define ACAHI_SND_UNLOAD        2

/* SND_CMD and V_CMD results (SND_CMD reads the last) */
#define ACAHI_RES_OK            0
#define ACAHI_RES_BADSLOT       1
#define ACAHI_RES_BADTYPE       2                 /* AHI's AHIE_BADSAMPLETYPE */
#define ACAHI_RES_BADADDR       3
#define ACAHI_RES_BADRANGE      4                 /* OFFSET/LENGTH outside the sound */

/* V_CMD */
#define ACAHI_VC_SOUND          (1UL << 0)        /* SOUND, OFFSET, LENGTH */
#define ACAHI_VC_FREQ           (1UL << 1)        /* FREQ */
#define ACAHI_VC_VOL            (1UL << 2)        /* VOL and PAN */
#define ACAHI_VC_IMM            (1UL << 8)        /* now (AHISF_IMM); without it, when the sound playing ends */

#define ACAHI_NOSOUND           0xFFFFUL

/* LEVELS: three percentages (0-100) and a mute bit. Writing it sets all
 * four; the PC's mixer page sets them too, and LEVEL_SEQ counts both. */
#define ACAHI_LEVEL_VOLUME(l)   ((l) & 0xFFUL)            /* everything the machine plays */
#define ACAHI_LEVEL_PAULA(l)    (((l) >> 8) & 0xFFUL)
#define ACAHI_LEVEL_AHI(l)      (((l) >> 16) & 0xFFUL)    /* this board */
#define ACAHI_LEVEL_MUTE        (1UL << 24)
#define ACAHI_LEVELS_MAKE(vol, paula, ahi, mute) \
    ((unsigned long)(vol) | (unsigned long)(paula) << 8 | (unsigned long)(ahi) << 16 | ((mute) ? ACAHI_LEVEL_MUTE : 0UL))
#define ACAHI_LEVELS_DEFAULT    0x00646464UL              /* 100%, 100%, 100%, not muted */

#endif
