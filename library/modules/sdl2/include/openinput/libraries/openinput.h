/* Copyright (c) 2026 Dalsin Limited. OpenInput, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * openinput.library: one way for Amiga programs to get game controllers
 * (Design-OpenInput.md). Version 1.
 *
 * FROZEN for 0.1 on 8 October 2026: from here on this file changes only
 * by adding (new names, new flags, new tags, new functions at the end of
 * the SFD, structures growing only at their end behind a size argument).
 * Nothing here is renumbered or removed.
 *
 * Every controller, whatever it is and wherever it comes from (an Amiga
 * joystick port, a CD32 pad, a USB pad through Poseidon, a pad on
 * AmigaChrome's x86 or ARM64 cores, the A1200's own ports under a PiStorm),
 * is offered in one standard layout, the one SDL 2's GameController API
 * uses: A, B, X, Y, Back, Guide, Start, the stick clicks, the shoulders,
 * the d-pad, two sticks and two triggers. The raw buttons, axes and hats
 * are there too, for programs and mappers that want them.
 */
#ifndef LIBRARIES_OPENINPUT_H
#define LIBRARIES_OPENINPUT_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_PORTS_H
#include <exec/ports.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define OPENINPUT_NAME    "openinput.library"
#define OPENINPUT_VERSION 1
/* The first working library is version 1 revision 2 (1.2). Revision 1
 * (1.1) was the skeleton: it lists nothing and opens nothing. A client
 * that needs a working library checks lib_Version > 1, or lib_Version 1
 * and lib_Revision >= OPENINPUT_REVISION_CORE. */
#define OPENINPUT_REVISION_CORE 2

/* ---- The standard layout ----------------------------------------------------
 * Button numbers are SDL_GameControllerButton's, axis numbers
 * SDL_GameControllerAxis's, so SDL 2's GameController API maps one to one.
 * A, B, X and Y are places on the pad, not letters: A is the bottom face
 * button (A on an Xbox pad, cross on a PlayStation pad, B on a Switch
 * pad, red on a CD32 pad). OIN_ButtonLabel() gives the pad's own name. */
#define OIB_A              0
#define OIB_B              1    /* right face button */
#define OIB_X              2    /* left face button */
#define OIB_Y              3    /* top face button */
#define OIB_BACK           4    /* Select, Share, Minus */
#define OIB_GUIDE          5    /* Home, the logo button */
#define OIB_START          6    /* Start, Options, Plus; a CD32 pad's Play */
#define OIB_LEFTSTICK      7    /* the left stick's click */
#define OIB_RIGHTSTICK     8
#define OIB_LEFTSHOULDER   9    /* a CD32 pad's Reverse */
#define OIB_RIGHTSHOULDER  10   /* a CD32 pad's Forward */
#define OIB_DPAD_UP        11
#define OIB_DPAD_DOWN      12
#define OIB_DPAD_LEFT      13
#define OIB_DPAD_RIGHT     14
#define OIB_MISC1          15   /* Xbox Share, PS5 microphone, Switch Capture */
#define OIB_PADDLE1        16
#define OIB_PADDLE2        17
#define OIB_PADDLE3        18
#define OIB_PADDLE4        19
#define OIB_TOUCHPAD       20
#define OIB_COUNT          21

#define OIBF(b)            (1UL << (b))   /* a button's bit in ois_Buttons */

#define OIAXIS_LEFTX        0   /* -32768 (left) to 32767 (right) */
#define OIAXIS_LEFTY        1   /* -32768 (up) to 32767 (down) */
#define OIAXIS_RIGHTX       2
#define OIAXIS_RIGHTY       3
#define OIAXIS_TRIGGERLEFT  4   /* 0 (released) to 32767 (fully pulled) */
#define OIAXIS_TRIGGERRIGHT 5
#define OIAXIS_COUNT        6

/* ---- Where a controller comes from (oci_Source) ---------------------------- */
#define OISRC_AMIGAPORT    1    /* an Amiga joystick port, read as lowlevel.library does */
#define OISRC_POSEIDON     2    /* a USB HID pad through Poseidon (OpenUSB) */
#define OISRC_AMIGACHROME  3    /* a pad on AmigaChrome's x86 or ARM64 cores, mapped there */
#define OISRC_PISTORMPORT  4    /* the A1200's own port, read by the core under a PiStorm */

/* ---- What it is (oci_Type) -------------------------------------------------- */
#define OITYPE_UNKNOWN     0
#define OITYPE_GAMEPAD     1    /* a modern pad: the whole standard layout, or most of it */
#define OITYPE_JOYSTICK    2    /* an Amiga or Atari-style stick: directions and one to three buttons */
#define OITYPE_CD32PAD     3    /* seven buttons and a d-pad */
#define OITYPE_ARCADESTICK 4
#define OITYPE_WHEEL       5
#define OITYPE_FLIGHTSTICK 6

/* ---- The maker's family, for button names (oci_Family) ---------------------- */
#define OIFAM_GENERIC      0
#define OIFAM_XBOX         1
#define OIFAM_PLAYSTATION  2
#define OIFAM_SWITCH       3    /* Switch Pro Controller, Joy-Cons */
#define OIFAM_8BITDO       4
#define OIFAM_CD32         5    /* a CD32 pad, or a CD32-style USB pad */
#define OIFAM_AMIGA        6    /* a plain Amiga joystick */

/* ---- What it can do (oci_Flags) --------------------------------------------- */
#define OICF_MAPPED         (1UL << 0)  /* the standard layout is known (a mapping, or the source maps it) */
#define OICF_RUMBLE         (1UL << 1)  /* OIN_Rumble works */
#define OICF_ANALOGTRIGGERS (1UL << 2)  /* triggers report 0-32767, not only 0 or 32767 */
#define OICF_ANALOGSTICKS   (1UL << 3)  /* sticks are analogue; an Amiga stick reports -32768, 0 or 32767 */
#define OICF_REMOVABLE      (1UL << 4)  /* it can go away (hot-plug); an Amiga port never does */
#define OICF_LEGACYFEED     (1UL << 5)  /* it also drives an Amiga port (oci_LegacyPort) for older games */
#define OICF_INUSE          (1UL << 6)  /* a program has it open with OIT_Exclusive (its legacy feed is paused) */

/* ---- How a pad drives an Amiga port for older games ------------------------- */
#define OILEG_NONE         0
#define OILEG_JOYSTICK     1    /* a two-button joystick */
#define OILEG_CD32         2    /* a CD32 pad */
#define OILEG_MOUSE        3    /* the mouse */
#define OILEG_KEYS         4    /* Amiga keys */
#define OILEG_NOPORT       0xFF /* oci_LegacyPort: no port */

/* ---- A controller, as listed ------------------------------------------------
 * Callers pass sizeof(struct OIControllerInfo) so later versions can grow
 * it; the library fills no more than the size it is given. */
struct OIControllerInfo {
    ULONG oci_ID;           /* never reused while the machine runs; 0 is never an ID */
    ULONG oci_Source;       /* OISRC_* */
    ULONG oci_Type;         /* OITYPE_* */
    ULONG oci_Flags;        /* OICF_* */
    UBYTE oci_GUID[16];     /* SDL 2's joystick GUID: bus, CRC, vendor, product, version, driver (little-endian words) */
    UWORD oci_Vendor;       /* USB vendor and product, 0 when there are none */
    UWORD oci_Product;
    UWORD oci_Version;
    WORD  oci_Player;       /* player number from 0, or -1 */
    UBYTE oci_NumButtons;   /* raw counts, as the device reports them */
    UBYTE oci_NumAxes;
    UBYTE oci_NumHats;
    UBYTE oci_Family;       /* OIFAM_* */
    UBYTE oci_LegacyPort;   /* 0 or 1 (2 and 3 the four-player adapter's), or OILEG_NOPORT */
    UBYTE oci_LegacyMode;   /* OILEG_* */
    UBYTE oci_Reserved0[2];
    char  oci_Name[64];     /* "Xbox Wireless Controller", "Amiga joystick port 2" */
    ULONG oci_Reserved[4];
};

/* ---- A controller's state in the standard layout ---------------------------- */
struct OIState {
    ULONG ois_Buttons;              /* OIBF(OIB_*) bits */
    WORD  ois_Axes[OIAXIS_COUNT];   /* OIAXIS_*: sticks -32768..32767, triggers 0..32767 */
    ULONG ois_Sequence;             /* goes up by one or more each time the state changes */
    ULONG ois_Micros;               /* when it changed, in microseconds; it wraps, so compare differences only */
    UWORD ois_Flags;                /* OISF_* */
    UWORD ois_Reserved;
};
#define OISF_CONNECTED   (1 << 0)   /* clear once the controller has gone */
#define OISF_MAPPED      (1 << 1)   /* the standard layout is real, not a guess */

/* ---- A controller's raw state, as the device reports it --------------------- */
#define OIRAW_BUTTONS 64
#define OIRAW_AXES    16
#define OIRAW_HATS    4
struct OIRawState {
    ULONG oir_Buttons[OIRAW_BUTTONS / 32];  /* button n is bit (n % 32) of oir_Buttons[n / 32] */
    WORD  oir_Axes[OIRAW_AXES];             /* -32768..32767 */
    UBYTE oir_Hats[OIRAW_HATS];             /* OIHAT_* bits */
    ULONG oir_Sequence;
    ULONG oir_Micros;
};
#define OIHAT_UP    1   /* SDL 2's hat bits */
#define OIHAT_RIGHT 2
#define OIHAT_DOWN  4
#define OIHAT_LEFT  8

/* ---- A legacy port's feed, for preferences (OIN_GetLegacyPort) -------------- */
struct OILegacyPort {
    ULONG olp_Port;         /* 0 (the mouse port), 1 (the joystick port), 2, 3 */
    ULONG olp_ControllerID; /* the pad that feeds it, or 0: the port's own hardware */
    ULONG olp_Mode;         /* OILEG_* */
    ULONG olp_Where;        /* OILPW_*: where the feed is made */
    ULONG olp_Reserved[4];
};
#define OILPW_HARDWARE  0   /* only the port's own hardware */
#define OILPW_CORE      1   /* on AmigaChrome's cores, at the chip registers: every game sees it */
#define OILPW_LOWLEVEL  2   /* on a real Amiga, through lowlevel.library's ReadJoyPort: opt-in, see OPENINPUT_LOWLEVELPATCH */
#define OILPW_GAMEPORT  3   /* on a real Amiga, through gameport.device too */

/* ---- Notification (OIN_AddNotifyA) -------------------------------------------
 * The library sends an OIMessage to the program's port; the program
 * replies to it with ReplyMsg(), soon, as to an IntuiMessage. The messages
 * belong to the library (they have no reply port; the library reuses them
 * once replied). Each notification has a few messages:
 * - ADDED and REMOVED: one per change. If the program falls so far behind
 *   that none is free, the next message has oim_ID 0, meaning "several
 *   controllers changed: list them again".
 * - STATE: at most one unreplied at a time per notification, so they never
 *   stack up; oim_ID says which controller changed (others may have too).
 * OIN_RemNotify takes back any of its messages still waiting in the port,
 * so the program may call it at any time, then delete its port. */
struct OIMessage {
    struct Message oim_Message;
    ULONG oim_Class;        /* OIMC_* */
    ULONG oim_ID;           /* the controller */
    ULONG oim_Micros;
    ULONG oim_Reserved[2];
};
#define OIMC_ADDED     (1UL << 0)   /* a controller arrived */
#define OIMC_REMOVED   (1UL << 1)   /* a controller went */
#define OIMC_STATE     (1UL << 2)   /* an opened controller's state changed */
#define OIMC_MAPPING   (1UL << 3)   /* a controller's mapping or legacy feed changed */

/* ---- The functions, in short (library/openinput_lib.sfd has the registers)
 * OIN_ListControllers(buf, max, size)  copies up to max entries, returns how many there are
 * OIN_GetControllerInfo(id, info, size) OIERR_OK or OIERR_NOSUCH
 * OIN_OpenControllerA(id, tags)        a handle, or NULL (no such controller, or
 *                                      OIT_Exclusive asked while another program has it)
 * OIN_CloseController(handle)          NULL is allowed
 * OIN_ReadState(handle, state, size)   OIERR_OK; OIERR_GONE once it has gone (state
 *                                      zeroed, OISF_CONNECTED clear). Never waits.
 * OIN_ReadRaw(handle, raw, size)       the same, for the raw state
 * OIN_Rumble(handle, low, high, ms)    motors 0..65535 for ms milliseconds; ms 0 stops;
 *                                      OIERR_UNSUPP without OICF_RUMBLE
 * OIN_AddNotifyA(port, tags)           a notification, or NULL; OIN_RemNotify(it)
 * OIN_GetMapping(guid, buf, size)      the mapping line in use for that GUID, in SDL's
 *                                      text format; OIERR_NOSUCH when there is none
 * OIN_SetMapping(line, OISMF_*)        OIERR_OK or OIERR_BADMAP; applies at once to
 *                                      pads with that GUID (OIMC_MAPPING)
 * OIN_ButtonLabel(id, OIB_*)           the pad's own name for a button ("Cross"), never NULL
 *                                      for a button below OIB_COUNT; id 0: generic names
 * OIN_GetLegacyPort(port, lp, size)    what feeds Amiga port 0..3
 * OIN_SetLegacyPortA(port, id, tags)   make pad id (0: none) feed the port; OIERR_OFF on a
 *                                      real Amiga until the ReadJoyPort feed is switched on
 * Every function may be called from any task, never from an interrupt. */

/* ---- Tags ------------------------------------------------------------------- */
#define OIT_Dummy       (TAG_USER + 0x4F490000)   /* 'OI' */
#define OIT_Events      (OIT_Dummy + 1)   /* OIN_AddNotifyA: OIMC_* bits (default ADDED|REMOVED) */
#define OIT_ID          (OIT_Dummy + 2)   /* OIN_AddNotifyA: only this controller */
#define OIT_Exclusive   (OIT_Dummy + 3)   /* OIN_OpenControllerA: BOOL, pause its legacy feed while open (default FALSE) */
#define OIT_Player      (OIT_Dummy + 4)   /* OIN_OpenControllerA: set its player number */
#define OIT_LegacyMode  (OIT_Dummy + 5)   /* OIN_SetLegacyPortA: OILEG_* */
#define OIT_LegacyPreset (OIT_Dummy + 6)  /* OIN_SetLegacyPortA: STRPTR, a preset by name: "joystick", "cd32", "mouse", "keys" */
#define OIT_Save        (OIT_Dummy + 7)   /* OIN_SetLegacyPortA: BOOL, keep it in ENVARC: too */
#define OIT_DeadZone    (OIT_Dummy + 8)   /* OIN_OpenControllerA: stick dead zone, 0..32767 (default the user's) */

/* ---- OIN_SetMapping flags --------------------------------------------------- */
#define OISMF_USE   0           /* for now (ENV:) */
#define OISMF_SAVE  (1UL << 0)  /* and keep it (ENVARC:) */
#define OISMF_TEST  (1UL << 1)  /* only check the line */

/* ---- Errors (LONG results; 0 is success) ------------------------------------ */
#define OIERR_OK         0
#define OIERR_NOTIMPL   -1      /* not built yet */
#define OIERR_GONE      -2      /* the controller has gone */
#define OIERR_NOSUCH    -3      /* no controller with that ID */
#define OIERR_UNSUPP    -4      /* this controller can't (rumble on an Amiga stick) */
#define OIERR_BADMAP    -5      /* a mapping line the library can't read */
#define OIERR_NOMEM     -6
#define OIERR_BUSY      -7      /* another program has it exclusively */
#define OIERR_TOOSMALL  -8      /* the buffer is too small */
#define OIERR_OFF       -9      /* the ReadJoyPort feed is switched off (the default on a real Amiga) */

/* ---- Settings ----------------------------------------------------------------- */
#define OPENINPUT_ENVDIR     "OpenInput"             /* in ENV: and ENVARC: */
#define OPENINPUT_MAPPINGS   "OpenInput/mappings.txt" /* the user's own lines, in SDL's GameControllerDB format,
                                                        over the built-in ones (Xbox, PlayStation, Switch,
                                                        8BitDo, CD32-style) */
#define OPENINPUT_PORTS      "OpenInput/ports.prefs"  /* which pad feeds which port */
/* The ReadJoyPort feed on a real Amiga is opt-in: openinput.library patches
 * lowlevel.library only while this variable is "1", which OpenPrefs'
 * Gamepads page sets. Nothing is patched by default. */
#define OPENINPUT_LOWLEVELPATCH "OpenInput/LowLevelPatch"

#endif /* LIBRARIES_OPENINPUT_H */
