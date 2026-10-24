/* ACAHI, AmigaChrome's sound board for AHI: the stream and the host-mixed
 * voices. See acahi.c and vendor/amigachrome-guest/common/protocol/acahi.h. */
#ifndef A1200_ACAHI_H
#define A1200_ACAHI_H
#include <stdint.h>
#include "../../../../vendor/amigachrome-guest/common/protocol/acahi.h"

#define ACAHI_FIFO (1u << 18)          /* stream frames held, stereo: about 6 s at 44.1 kHz */
#define ACAHI_LEVELS_SET (1u << 31)    /* in the ring's word only: the levels are set (an older ring has 0 there) */

/* Guest RAM for [addr, addr + len): a host pointer, or NULL when any of it
 * is not RAM. The board copies the stream through it and reads the voices'
 * sounds through it as they play. */
typedef uint8_t *(*acahi_ram_fn)(void *opaque, uint32_t addr, uint32_t len);

typedef struct {
    uint32_t snd, start, frames;       /* a sound slot, its first frame, how many */
    int dir;                           /* 1 forwards, -1 backwards */
    uint32_t freq;                     /* frames a second; 0 holds */
    int32_t vol, pan;                  /* 16.16 */
} acahi_seg;

typedef struct {
    int on;                            /* a segment is playing */
    acahi_seg cur;
    uint64_t pos, step;                /* 32.32 frames into cur, and a step an output frame */
    const uint8_t *data;               /* cur's frames in host memory, at cur.start */
    int next_on;                       /* what plays when cur ends: AHI plays a sound again until told otherwise */
    acahi_seg next;
    uint32_t s_sound, s_offset, s_freq; int32_t s_length, s_vol, s_pan;   /* staged */
} acahi_voice;

typedef struct {
    uint32_t addr, frames, type;       /* type: AHI's number; frames 0 is an empty slot */
} acahi_sound;

typedef struct acahi {
    uint32_t base;                     /* where autoconfig placed it; 0 not placed */
    int host;                          /* the output reaches the host (STATUS bit 0) */
    acahi_ram_fn ram; void *ram_opaque;

    uint32_t control, rate, format;
    uint32_t pb_addr, pb_frames, pb_seq, pb_result, pb_done;
    uint32_t intreq, intena, underruns, errors;
    uint64_t frame;                    /* output frames rendered since reset: the audio clock */

    /* the stream: frames copied in, at its rate, in a ring */
    int16_t *fifo;                     /* ACAHI_FIFO stereo frames */
    uint64_t fifo_w, fifo_r;           /* frames written, frames played (whole frames) */
    uint64_t frac;                     /* 32.32: between frame fifo_r and fifo_r + 1 */
    struct { uint32_t seq; uint64_t end; } q[ACAHI_QUEUE];
    uint32_t q_n;                      /* buffers queued, oldest first */
    int dry;                           /* the running stream had nothing at the last frame */

    /* the timer */
    uint32_t timer_freq, timer_ticks;
    uint64_t timer_acc;                /* 32.32 output frames since the last tick */

    /* the voices */
    uint32_t master, voice_events;
    uint32_t snd_id, snd_type, snd_addr, snd_len, snd_result;
    acahi_sound sounds[ACAHI_SOUNDS];
    acahi_voice v[ACAHI_VOICES];

    /* the levels: two words the PC's mixer shares (Paula's ring header,
     * bytes 24-31: LEVELS | ACAHI_LEVELS_SET, then LEVEL_SEQ), or the
     * board's own pair when there is no ring. They outlast a reset, as a
     * volume knob does. */
    volatile uint32_t *levels;
    uint32_t levels_own[2];
} acahi;

/* The levels words in a ring header (bytes 24-31), shared with the PC's
 * mixer; set to the defaults unless the ring already holds levels. */
void acahi_share_levels(acahi *a, uint8_t *ring_header);

/* Allocates the stream's ring; reset values. */
int acahi_init(acahi *a, acahi_ram_fn ram, void *opaque);
void acahi_free(acahi *a);
/* /RST, or CONTROL's RESET: everything stops and empties. base is kept. */
void acahi_reset(acahi *a);
/* Registers: off from the board's base; size 1, 2 or 4. */
uint32_t acahi_read(acahi *a, uint32_t off, int size);
void acahi_write(acahi *a, uint32_t off, uint32_t v, int size);
/* The next n output frames, stereo, added into mix (2n ints). Moves the
 * audio clock: buffers finish, voices start their next sounds, the timer
 * ticks. */
void acahi_render(acahi *a, int32_t *mix, uint32_t n);
/* Something to play or time: the machine renders it at each line. */
int acahi_busy(const acahi *a);
/* INT2 asked for. */
static inline int acahi_irq(const acahi *a) {   /* atomic: a host thread may render the board (a1200 ahi_host_clock) */
    return (__atomic_load_n(&a->intreq, __ATOMIC_RELAXED) & __atomic_load_n(&a->intena, __ATOMIC_RELAXED)) != 0;
}

#endif
