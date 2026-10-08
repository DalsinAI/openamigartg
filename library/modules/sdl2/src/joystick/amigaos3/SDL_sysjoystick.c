/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL 2 joysticks and game controllers for AmigaOS 3.x. Replaces
 * libSDL2-amigaos3's stub. Two back ends, chosen when SDL's joystick
 * subsystem starts:
 *
 * 1. OpenInput (openinput.library 1.2 or later, Design-OpenInput.md in
 *    openamigainput): every controller the library lists, with SDL's GUID,
 *    name, vendor and product from the library, hot-plug from its
 *    notification (SDL_JOYDEVICEADDED/REMOVED and, for game controllers,
 *    SDL_CONTROLLERDEVICEADDED/REMOVED), and rumble through OIN_Rumble.
 *    A pad's SDL joystick is OpenInput's standard layout, which is SDL's
 *    GameController layout: buttons 0-20 are SDL_GameControllerButton's,
 *    axes 0-5 SDL_GameControllerAxis's (triggers, as everywhere in SDL,
 *    run -32768 released to 32767 pulled), and one hat repeats the d-pad.
 *    The driver hands SDL the mapping that says so, so SDL_GameController
 *    works with no database. The mapping that makes a pad right is
 *    OpenInput's (its built-in lines, ENVARC:OpenInput/mappings.txt,
 *    OpenPrefs' Gamepads page).
 *    A pad SDL has a mapping of its own for (SDL_GameControllerAddMapping,
 *    SDL_GameControllerAddMappingsFromFile, SDL_GAMECONTROLLERCONFIG), and
 *    wheels and flight sticks, are opened in the raw layout instead: the
 *    buttons, axes and hats as the device reports them, which is what
 *    SDL's mapping lines and OpenInput's lines both count in.
 *    SDL opens pads with OIT_Exclusive, so a pad that also drives an Amiga
 *    port doesn't move the game's port joystick as well; the hint
 *    SDL_JOYSTICK_OPENINPUT_EXCLUSIVE=0 opens them shared.
 *
 * 2. lowlevel.library (OS 3.1 and later), when openinput.library isn't
 *    there, is the skeleton (1.1), or the hint SDL_JOYSTICK_OPENINPUT=0
 *    asks for it: the joystick or CD32 pad in each game port. Port 2
 *    (lowlevel unit 1) is SDL joystick 0, since that is where Amiga games
 *    expect the stick; port 1 (unit 0) is SDL joystick 1 when lowlevel
 *    reports a joystick or game controller there. Each has 2 axes
 *    (digital, so -32768, 0 or 32767), 1 hat and 7 buttons: red, blue,
 *    then on a CD32 pad green, yellow, play, forward and reverse.
 *    Without lowlevel.library there are no joysticks.
 */

#include "../../SDL_internal.h"

#if SDL_JOYSTICK_AMIGAOS3

#include "SDL_events.h"
#include "SDL_hints.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>

/* OpenInput's frozen header (openamigainput, include/openinput here). The
 * base has a name of SDL's own, so a program that opens openinput.library
 * itself and links SDL statically has no clash over OpenInputBase. */
#include <libraries/openinput.h>
#define OPENINPUT_BASE_NAME SDL_OS3_OpenInputBase
#define NO_INLINE_STDARG
#include <inline/openinput.h>

/* Opened here, not by the startup code, so a machine without them still runs. */
struct Library *LowLevelBase = NULL;
struct Library *SDL_OS3_OpenInputBase = NULL;

/* In SDL_gamecontroller.c (patches/sdl2/0007): whether SDL has a mapping
 * of the program's or the user's for this GUID. */
extern SDL_bool SDL_PrivateGameControllerHasOwnMapping(SDL_JoystickGUID guid);

#define SDL_HINT_JOYSTICK_OPENINPUT           "SDL_JOYSTICK_OPENINPUT"
#define SDL_HINT_JOYSTICK_OPENINPUT_EXCLUSIVE "SDL_JOYSTICK_OPENINPUT_EXCLUSIVE"

static SDL_bool os3_openinput = SDL_FALSE;    /* which back end runs */

/* ======================================================================
 * 1. OpenInput
 * ====================================================================== */

#define OI_STD_HATS 1

typedef struct {
    ULONG id;                       /* OpenInput's ID */
    SDL_JoystickID instance_id;
    int player;                     /* -1, or the player SDL was asked for */
    struct OIControllerInfo info;
} OI_Device;

/* The SDL joystick's view of an open controller. */
struct joystick_hwdata {
    APTR handle;                    /* OIN_OpenControllerA's */
    ULONG id;
    SDL_bool raw;                   /* the raw layout, not the standard one */
    ULONG sequence;                 /* the last state reported */
    SDL_bool have_state;
    ULONG flags;                    /* OICF_* when opened */
};

static OI_Device *oi_devs = NULL;
static int oi_ndevs = 0;
static int oi_maxdevs = 0;
static struct MsgPort *oi_port = NULL;
static APTR oi_notify = NULL;
static SDL_bool oi_relist = SDL_FALSE;

static SDL_bool OI_Usable(struct Library *lib)
{
    return lib->lib_Version > OPENINPUT_VERSION ||
           (lib->lib_Version == OPENINPUT_VERSION && lib->lib_Revision >= OPENINPUT_REVISION_CORE);
}

/* A port that needs no signal: the library may send to it from any task,
 * and the driver only ever looks at it (Detect), never waits on it, so it
 * doesn't matter which of the program's tasks starts or stops SDL. */
static struct MsgPort *OI_CreatePort(void)
{
    struct MsgPort *port = (struct MsgPort *)AllocMem(sizeof(*port), MEMF_PUBLIC | MEMF_CLEAR);
    if (port) {
        struct List *l = &port->mp_MsgList;
        port->mp_Node.ln_Type = NT_MSGPORT;
        port->mp_Flags = PA_IGNORE;
        l->lh_Head = (struct Node *)&l->lh_Tail;
        l->lh_Tail = NULL;
        l->lh_TailPred = (struct Node *)&l->lh_Head;
    }
    return port;
}

static int OI_FindIndex(ULONG id)
{
    int i;
    for (i = 0; i < oi_ndevs; ++i) {
        if (oi_devs[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void OI_AddDevice(const struct OIControllerInfo *info)
{
    OI_Device *dev;

    if (info->oci_ID == 0 || OI_FindIndex(info->oci_ID) >= 0) {
        return;
    }
    if (oi_ndevs == oi_maxdevs) {
        int max = oi_maxdevs ? oi_maxdevs * 2 : 4;
        OI_Device *devs = (OI_Device *)SDL_realloc(oi_devs, max * sizeof(*devs));
        if (!devs) {
            SDL_OutOfMemory();
            return;
        }
        oi_devs = devs;
        oi_maxdevs = max;
    }
    dev = &oi_devs[oi_ndevs];
    SDL_zerop(dev);
    SDL_memcpy(&dev->info, info, sizeof(*info));
    dev->info.oci_Name[sizeof(dev->info.oci_Name) - 1] = '\0';
    dev->id = info->oci_ID;
    dev->player = info->oci_Player;
    dev->instance_id = SDL_GetNextJoystickInstanceID();
    oi_ndevs++;
    SDL_PrivateJoystickAdded(dev->instance_id);
}

static void OI_RemoveIndex(int index)
{
    SDL_JoystickID instance_id = oi_devs[index].instance_id;

    /* Down by one: a copy forwards, so SDL_memmove keeps it right either way. */
    if (index + 1 < oi_ndevs) {
        SDL_memmove(&oi_devs[index], &oi_devs[index + 1], (oi_ndevs - index - 1) * sizeof(*oi_devs));
    }
    oi_ndevs--;
    SDL_PrivateJoystickRemoved(instance_id);
}

static void OI_AddByID(ULONG id)
{
    struct OIControllerInfo info;

    SDL_zero(info);
    if (OIN_GetControllerInfo(id, &info, sizeof(info)) == OIERR_OK) {
        OI_AddDevice(&info);
    }
}

/* Every controller the library has now: the gone ones out, the new ones in. */
static void OI_Relist(void)
{
    struct OIControllerInfo *list = NULL;
    ULONG n, got = 0, i;
    int j;

    n = OIN_ListControllers(NULL, 0, sizeof(*list));
    if (n) {
        list = (struct OIControllerInfo *)SDL_calloc(n, sizeof(*list));
        if (!list) {
            SDL_OutOfMemory();
            return;
        }
        got = OIN_ListControllers(list, n, sizeof(*list));
        if (got > n) {
            got = n;    /* more came meanwhile: the next relist gets them */
        }
    }
    for (j = oi_ndevs - 1; j >= 0; --j) {
        SDL_bool present = SDL_FALSE;
        for (i = 0; i < got; ++i) {
            if (list[i].oci_ID == oi_devs[j].id) {
                present = SDL_TRUE;
                break;
            }
        }
        if (!present) {
            OI_RemoveIndex(j);
        }
    }
    for (i = 0; i < got; ++i) {
        OI_AddDevice(&list[i]);
    }
    SDL_free(list);
}

static int OI_JoystickInit(void)
{
    ULONG tags[3];

    oi_ndevs = 0;
    oi_relist = SDL_FALSE;
    oi_port = OI_CreatePort();
    if (oi_port) {
        tags[0] = OIT_Events;
        tags[1] = OIMC_ADDED | OIMC_REMOVED;
        tags[2] = TAG_DONE;
        oi_notify = OIN_AddNotifyA(oi_port, (struct TagItem *)tags);
    }
    /* Listed after the notification is in, so nothing that comes between
     * the two is missed (a controller seen twice is only added once). */
    OI_Relist();
    return 0;
}

static int OI_JoystickGetCount(void) { return oi_ndevs; }

static void OI_JoystickDetect(void)
{
    struct OIMessage *msg;

    if (!oi_port) {
        return;
    }
    while ((msg = (struct OIMessage *)GetMsg(oi_port)) != NULL) {
        ULONG cls = msg->oim_Class;
        ULONG id = msg->oim_ID;
        ReplyMsg(&msg->oim_Message);

        if (id == 0) {
            oi_relist = SDL_TRUE;      /* several changed: list them again */
        } else if (cls & OIMC_ADDED) {
            OI_AddByID(id);
        } else if (cls & OIMC_REMOVED) {
            int index = OI_FindIndex(id);
            if (index >= 0) {
                OI_RemoveIndex(index);
            }
        }
    }
    if (oi_relist) {
        oi_relist = SDL_FALSE;
        OI_Relist();
    }
}

static const char *OI_JoystickGetDeviceName(int index)
{
    return (index >= 0 && index < oi_ndevs) ? oi_devs[index].info.oci_Name : NULL;
}

static int OI_JoystickGetDevicePlayerIndex(int index)
{
    return (index >= 0 && index < oi_ndevs) ? oi_devs[index].player : -1;
}

static void OI_JoystickSetDevicePlayerIndex(int index, int player)
{
    /* Given to the library when SDL opens the pad (OIT_Player). */
    if (index >= 0 && index < oi_ndevs) {
        oi_devs[index].player = player;
    }
}

static SDL_JoystickGUID OI_JoystickGetDeviceGUID(int index)
{
    SDL_JoystickGUID guid;

    SDL_zero(guid);
    if (index >= 0 && index < oi_ndevs) {
        SDL_memcpy(guid.data, oi_devs[index].info.oci_GUID, sizeof(guid.data));
    }
    return guid;
}

static SDL_JoystickID OI_JoystickGetDeviceInstanceID(int index)
{
    return (index >= 0 && index < oi_ndevs) ? oi_devs[index].instance_id : -1;
}

/* Wheels and flight sticks don't fit the pad layout: they are their raw selves. */
static SDL_bool OI_RawOnly(const struct OIControllerInfo *info)
{
    return info->oci_Type == OITYPE_WHEEL || info->oci_Type == OITYPE_FLIGHTSTICK;
}

static int OI_JoystickOpen(SDL_Joystick *joy, int index)
{
    const OI_Device *dev;
    struct joystick_hwdata *hw;
    ULONG tags[7];
    int t = 0;

    if (index < 0 || index >= oi_ndevs) {
        return SDL_SetError("No joystick at index %d", index);
    }
    dev = &oi_devs[index];
    hw = (struct joystick_hwdata *)SDL_calloc(1, sizeof(*hw));
    if (!hw) {
        return SDL_OutOfMemory();
    }

    if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_OPENINPUT_EXCLUSIVE, SDL_TRUE)) {
        tags[t++] = OIT_Exclusive;
        tags[t++] = TRUE;
    }
    if (dev->player >= 0) {
        tags[t++] = OIT_Player;
        tags[t++] = (ULONG)dev->player;
    }
    tags[t] = TAG_DONE;
    hw->handle = OIN_OpenControllerA(dev->id, (struct TagItem *)tags);
    if (!hw->handle && t >= 2 && tags[0] == OIT_Exclusive) {
        /* Another program has it to itself: share it rather than fail. */
        tags[1] = FALSE;
        hw->handle = OIN_OpenControllerA(dev->id, (struct TagItem *)tags);
    }
    if (!hw->handle) {
        SDL_free(hw);
        return SDL_SetError("OpenInput couldn't open \"%s\"", dev->info.oci_Name);
    }

    hw->id = dev->id;
    hw->flags = dev->info.oci_Flags;
    hw->raw = OI_RawOnly(&dev->info) ||
              SDL_PrivateGameControllerHasOwnMapping(OI_JoystickGetDeviceGUID(index));
    joy->hwdata = hw;
    joy->instance_id = dev->instance_id;
    if (hw->raw) {
        joy->nbuttons = SDL_min(dev->info.oci_NumButtons, OIRAW_BUTTONS);
        joy->naxes = SDL_min(dev->info.oci_NumAxes, OIRAW_AXES);
        joy->nhats = SDL_min(dev->info.oci_NumHats, OIRAW_HATS);
    } else {
        joy->nbuttons = OIB_COUNT;
        joy->naxes = OIAXIS_COUNT;
        joy->nhats = OI_STD_HATS;
    }
    return 0;
}

static int OI_JoystickRumble(SDL_Joystick *joy, Uint16 lo, Uint16 hi)
{
    struct joystick_hwdata *hw = joy->hwdata;
    LONG rc;

    if (!hw || !(hw->flags & OICF_RUMBLE)) {
        return SDL_Unsupported();
    }
    /* SDL ends the rumble itself (a call with 0, 0) when its time is up. */
    rc = OIN_Rumble(hw->handle, lo, hi, (lo || hi) ? SDL_MAX_RUMBLE_DURATION_MS : 0);
    if (rc == OIERR_UNSUPP) {
        return SDL_Unsupported();
    }
    return rc == OIERR_OK ? 0 : SDL_SetError("OpenInput rumble failed (%ld)", (long)rc);
}

static Uint32 OI_JoystickGetCapabilities(SDL_Joystick *joy)
{
    struct joystick_hwdata *hw = joy->hwdata;
    return (hw && (hw->flags & OICF_RUMBLE)) ? SDL_JOYCAP_RUMBLE : 0;
}

/* A trigger, 0 (released) to 32767 (pulled), as an SDL axis: -32768 to 32767. */
static Sint16 OI_TriggerAxis(WORD v)
{
    if (v <= 0) {
        return -32768;
    }
    return (Sint16)(2 * (LONG)v - 32767);
}

static void OI_UpdateStandard(SDL_Joystick *joy, struct joystick_hwdata *hw)
{
    struct OIState st;
    Uint8 hat = SDL_HAT_CENTERED;
    LONG rc;
    int i;

    SDL_zero(st);
    rc = OIN_ReadState(hw->handle, &st, sizeof(st));
    if (rc != OIERR_OK && rc != OIERR_GONE) {
        return;
    }
    if (hw->have_state && st.ois_Sequence == hw->sequence) {
        return;
    }
    hw->have_state = SDL_TRUE;
    hw->sequence = st.ois_Sequence;

    for (i = 0; i < OIB_COUNT; ++i) {
        SDL_PrivateJoystickButton(joy, (Uint8)i, (st.ois_Buttons & OIBF(i)) ? SDL_PRESSED : SDL_RELEASED);
    }
    SDL_PrivateJoystickAxis(joy, OIAXIS_LEFTX, st.ois_Axes[OIAXIS_LEFTX]);
    SDL_PrivateJoystickAxis(joy, OIAXIS_LEFTY, st.ois_Axes[OIAXIS_LEFTY]);
    SDL_PrivateJoystickAxis(joy, OIAXIS_RIGHTX, st.ois_Axes[OIAXIS_RIGHTX]);
    SDL_PrivateJoystickAxis(joy, OIAXIS_RIGHTY, st.ois_Axes[OIAXIS_RIGHTY]);
    SDL_PrivateJoystickAxis(joy, OIAXIS_TRIGGERLEFT, OI_TriggerAxis(st.ois_Axes[OIAXIS_TRIGGERLEFT]));
    SDL_PrivateJoystickAxis(joy, OIAXIS_TRIGGERRIGHT, OI_TriggerAxis(st.ois_Axes[OIAXIS_TRIGGERRIGHT]));

    if (st.ois_Buttons & OIBF(OIB_DPAD_UP))    hat |= SDL_HAT_UP;
    if (st.ois_Buttons & OIBF(OIB_DPAD_DOWN))  hat |= SDL_HAT_DOWN;
    if (st.ois_Buttons & OIBF(OIB_DPAD_LEFT))  hat |= SDL_HAT_LEFT;
    if (st.ois_Buttons & OIBF(OIB_DPAD_RIGHT)) hat |= SDL_HAT_RIGHT;
    SDL_PrivateJoystickHat(joy, 0, hat);
}

static void OI_UpdateRaw(SDL_Joystick *joy, struct joystick_hwdata *hw)
{
    struct OIRawState raw;
    LONG rc;
    int i;

    SDL_zero(raw);
    rc = OIN_ReadRaw(hw->handle, &raw, sizeof(raw));
    if (rc != OIERR_OK && rc != OIERR_GONE) {
        return;
    }
    if (hw->have_state && raw.oir_Sequence == hw->sequence) {
        return;
    }
    hw->have_state = SDL_TRUE;
    hw->sequence = raw.oir_Sequence;

    for (i = 0; i < joy->nbuttons; ++i) {
        SDL_PrivateJoystickButton(joy, (Uint8)i,
                                  (raw.oir_Buttons[i / 32] & (1UL << (i % 32))) ? SDL_PRESSED : SDL_RELEASED);
    }
    for (i = 0; i < joy->naxes; ++i) {
        SDL_PrivateJoystickAxis(joy, (Uint8)i, raw.oir_Axes[i]);
    }
    /* OIHAT_* are SDL's hat bits. */
    for (i = 0; i < joy->nhats; ++i) {
        SDL_PrivateJoystickHat(joy, (Uint8)i, raw.oir_Hats[i] & 0x0F);
    }
}

static void OI_JoystickUpdate(SDL_Joystick *joy)
{
    struct joystick_hwdata *hw = joy->hwdata;

    if (!hw || !hw->handle) {
        return;
    }
    if (hw->raw) {
        OI_UpdateRaw(joy, hw);
    } else {
        OI_UpdateStandard(joy, hw);
    }
}

static void OI_JoystickClose(SDL_Joystick *joy)
{
    struct joystick_hwdata *hw = joy->hwdata;

    if (hw) {
        if (hw->handle) {
            if (hw->flags & OICF_RUMBLE) {
                OIN_Rumble(hw->handle, 0, 0, 0);
            }
            OIN_CloseController(hw->handle);    /* gives back an exclusive pad's legacy feed */
        }
        SDL_free(hw);
        joy->hwdata = NULL;
    }
}

static void OI_JoystickQuit(void)
{
    if (oi_notify) {
        OIN_RemNotify(oi_notify);   /* takes back its messages still in the port */
        oi_notify = NULL;
    }
    if (oi_port) {
        FreeMem(oi_port, sizeof(*oi_port));
        oi_port = NULL;
    }
    SDL_free(oi_devs);
    oi_devs = NULL;
    oi_ndevs = oi_maxdevs = 0;
}

static void OI_MapButton(SDL_InputMapping *m, Uint8 button)
{
    m->kind = EMappingKind_Button;
    m->target = button;
}

static void OI_MapAxis(SDL_InputMapping *m, Uint8 axis)
{
    m->kind = EMappingKind_Axis;
    m->target = axis;
}

/* The standard layout is SDL's own: each control maps to itself. */
static SDL_bool OI_JoystickGetGamepadMapping(int index, SDL_GamepadMapping *out)
{
    if (index < 0 || index >= oi_ndevs || OI_RawOnly(&oi_devs[index].info)) {
        return SDL_FALSE;
    }
    OI_MapButton(&out->a, OIB_A);
    OI_MapButton(&out->b, OIB_B);
    OI_MapButton(&out->x, OIB_X);
    OI_MapButton(&out->y, OIB_Y);
    OI_MapButton(&out->back, OIB_BACK);
    OI_MapButton(&out->guide, OIB_GUIDE);
    OI_MapButton(&out->start, OIB_START);
    OI_MapButton(&out->leftstick, OIB_LEFTSTICK);
    OI_MapButton(&out->rightstick, OIB_RIGHTSTICK);
    OI_MapButton(&out->leftshoulder, OIB_LEFTSHOULDER);
    OI_MapButton(&out->rightshoulder, OIB_RIGHTSHOULDER);
    OI_MapButton(&out->dpup, OIB_DPAD_UP);
    OI_MapButton(&out->dpdown, OIB_DPAD_DOWN);
    OI_MapButton(&out->dpleft, OIB_DPAD_LEFT);
    OI_MapButton(&out->dpright, OIB_DPAD_RIGHT);
    OI_MapButton(&out->misc1, OIB_MISC1);
    OI_MapButton(&out->paddle1, OIB_PADDLE1);
    OI_MapButton(&out->paddle2, OIB_PADDLE2);
    OI_MapButton(&out->paddle3, OIB_PADDLE3);
    OI_MapButton(&out->paddle4, OIB_PADDLE4);
    OI_MapButton(&out->touchpad, OIB_TOUCHPAD);
    OI_MapAxis(&out->leftx, OIAXIS_LEFTX);
    OI_MapAxis(&out->lefty, OIAXIS_LEFTY);
    OI_MapAxis(&out->rightx, OIAXIS_RIGHTX);
    OI_MapAxis(&out->righty, OIAXIS_RIGHTY);
    OI_MapAxis(&out->lefttrigger, OIAXIS_TRIGGERLEFT);
    OI_MapAxis(&out->righttrigger, OIAXIS_TRIGGERRIGHT);
    return SDL_TRUE;
}

/* ======================================================================
 * 2. lowlevel.library
 * ====================================================================== */

#define OS3_MAX_PORTS 2
#define OS3_NAXES 2
#define OS3_NHATS 1
#define OS3_NBUTTONS 7

typedef struct {
    ULONG unit;                 /* lowlevel unit: 1 = port 2, 0 = port 1 */
    SDL_JoystickID instance_id;
    const char *name;
} OS3_JoyPort;

static OS3_JoyPort os3_ports[OS3_MAX_PORTS];
static int os3_nports = 0;

static const ULONG os3_button_bits[OS3_NBUTTONS] = {
    JPF_BUTTON_RED, JPF_BUTTON_BLUE, JPF_BUTTON_GREEN, JPF_BUTTON_YELLOW,
    JPF_BUTTON_PLAY, JPF_BUTTON_FORWARD, JPF_BUTTON_REVERSE
};

/* Only a stick or pad lowlevel has seen: UNKNOWN is what a port with a
 * mouse in it can read as, and a mouse-only machine has one joystick. */
static int OS3_PortHasStick(ULONG unit)
{
    ULONG type = ReadJoyPort(unit) & JP_TYPE_MASK;
    return type == JP_TYPE_JOYSTK || type == JP_TYPE_GAMECTLR;
}

static int LL_JoystickInit(void)
{
    int i;

    os3_nports = 0;
    LowLevelBase = OpenLibrary((CONST_STRPTR)"lowlevel.library", 40);
    if (!LowLevelBase) {
        return 0;
    }

    /* Port 2 is always offered: a stick plugged in later still works, as
     * lowlevel reads the port afresh each time. */
    os3_ports[os3_nports].unit = 1;
    os3_ports[os3_nports].name = "Amiga joystick port 2";
    os3_nports++;
    if (OS3_PortHasStick(0)) {
        os3_ports[os3_nports].unit = 0;
        os3_ports[os3_nports].name = "Amiga joystick port 1";
        os3_nports++;
    }

    for (i = 0; i < os3_nports; ++i) {
        os3_ports[i].instance_id = SDL_GetNextJoystickInstanceID();
        SDL_PrivateJoystickAdded(os3_ports[i].instance_id);
    }
    return 0;
}

static const char *LL_JoystickGetDeviceName(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].name : NULL;
}

static int LL_JoystickOpen(SDL_Joystick *joy, int index)
{
    if (index < 0 || index >= os3_nports) {
        return SDL_SetError("No joystick at index %d", index);
    }
    joy->instance_id = os3_ports[index].instance_id;
    joy->hwdata = (struct joystick_hwdata *)&os3_ports[index];
    joy->naxes = OS3_NAXES;
    joy->nhats = OS3_NHATS;
    joy->nbuttons = OS3_NBUTTONS;
    return 0;
}

static void LL_JoystickUpdate(SDL_Joystick *joy)
{
    const OS3_JoyPort *port = (const OS3_JoyPort *)joy->hwdata;
    ULONG state;
    Sint16 x = 0, y = 0;
    Uint8 hat = SDL_HAT_CENTERED;
    int i;

    if (!port || !LowLevelBase) {
        return;
    }
    state = ReadJoyPort(port->unit);
    if ((state & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL ||
        (state & JP_TYPE_MASK) == JP_TYPE_MOUSE) {
        state = 0;
    }

    if (state & JPF_JOY_LEFT)  { x = -32768; hat |= SDL_HAT_LEFT; }
    if (state & JPF_JOY_RIGHT) { x = 32767;  hat |= SDL_HAT_RIGHT; }
    if (state & JPF_JOY_UP)    { y = -32768; hat |= SDL_HAT_UP; }
    if (state & JPF_JOY_DOWN)  { y = 32767;  hat |= SDL_HAT_DOWN; }

    /* The core drops repeats, so every axis, hat and button is reported. */
    SDL_PrivateJoystickAxis(joy, 0, x);
    SDL_PrivateJoystickAxis(joy, 1, y);
    SDL_PrivateJoystickHat(joy, 0, hat);
    for (i = 0; i < OS3_NBUTTONS; ++i) {
        SDL_PrivateJoystickButton(joy, (Uint8)i,
                                  (state & os3_button_bits[i]) ? SDL_PRESSED : SDL_RELEASED);
    }
}

static void LL_JoystickQuit(void)
{
    os3_nports = 0;
}

/* ======================================================================
 * The driver: OpenInput when it is there, lowlevel.library otherwise.
 * ====================================================================== */

static int OS3_JoystickInit(void)
{
    struct Library *lib;

    os3_openinput = SDL_FALSE;
    if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_OPENINPUT, SDL_TRUE)) {
        lib = OpenLibrary((CONST_STRPTR)OPENINPUT_NAME, OPENINPUT_VERSION);
        if (lib && OI_Usable(lib)) {
            SDL_OS3_OpenInputBase = lib;
            os3_openinput = SDL_TRUE;
            return OI_JoystickInit();
        }
        if (lib) {
            CloseLibrary(lib);      /* the skeleton (1.1): it lists nothing */
        }
    }
    return LL_JoystickInit();
}

static int OS3_JoystickGetCount(void)
{
    return os3_openinput ? OI_JoystickGetCount() : os3_nports;
}

static void OS3_JoystickDetect(void)
{
    if (os3_openinput) {
        OI_JoystickDetect();
    }
}

static const char *OS3_JoystickGetDeviceName(int index)
{
    return os3_openinput ? OI_JoystickGetDeviceName(index) : LL_JoystickGetDeviceName(index);
}

static const char *OS3_JoystickGetDevicePath(int index) { return NULL; }
static int OS3_JoystickGetDeviceSteamVirtualGamepadSlot(int index) { return -1; }

static int OS3_JoystickGetDevicePlayerIndex(int index)
{
    return os3_openinput ? OI_JoystickGetDevicePlayerIndex(index) : index;
}

static void OS3_JoystickSetDevicePlayerIndex(int index, int player)
{
    if (os3_openinput) {
        OI_JoystickSetDevicePlayerIndex(index, player);
    }
}

static SDL_JoystickGUID OS3_JoystickGetDeviceGUID(int index)
{
    if (os3_openinput) {
        return OI_JoystickGetDeviceGUID(index);
    }
    return SDL_CreateJoystickGUIDForName(LL_JoystickGetDeviceName(index));
}

static SDL_JoystickID OS3_JoystickGetDeviceInstanceID(int index)
{
    if (os3_openinput) {
        return OI_JoystickGetDeviceInstanceID(index);
    }
    return (index >= 0 && index < os3_nports) ? os3_ports[index].instance_id : -1;
}

static int OS3_JoystickOpen(SDL_Joystick *joy, int index)
{
    return os3_openinput ? OI_JoystickOpen(joy, index) : LL_JoystickOpen(joy, index);
}

static int OS3_JoystickRumble(SDL_Joystick *joy, Uint16 lo, Uint16 hi)
{
    return os3_openinput ? OI_JoystickRumble(joy, lo, hi) : SDL_Unsupported();
}

static int OS3_JoystickRumbleTriggers(SDL_Joystick *joy, Uint16 lo, Uint16 hi) { return SDL_Unsupported(); }

static Uint32 OS3_JoystickGetCapabilities(SDL_Joystick *joy)
{
    return os3_openinput ? OI_JoystickGetCapabilities(joy) : 0;
}

static int OS3_JoystickSetLED(SDL_Joystick *joy, Uint8 r, Uint8 g, Uint8 b) { return SDL_Unsupported(); }
static int OS3_JoystickSendEffect(SDL_Joystick *joy, const void *data, int size) { return SDL_Unsupported(); }
static int OS3_JoystickSetSensorsEnabled(SDL_Joystick *joy, SDL_bool enabled) { return SDL_Unsupported(); }

static void OS3_JoystickUpdate(SDL_Joystick *joy)
{
    if (os3_openinput) {
        OI_JoystickUpdate(joy);
    } else {
        LL_JoystickUpdate(joy);
    }
}

static void OS3_JoystickClose(SDL_Joystick *joy)
{
    if (os3_openinput) {
        OI_JoystickClose(joy);
    } else {
        joy->hwdata = NULL;
    }
}

static void OS3_JoystickQuit(void)
{
    if (os3_openinput) {
        OI_JoystickQuit();
    } else {
        LL_JoystickQuit();
    }
    os3_openinput = SDL_FALSE;
    if (SDL_OS3_OpenInputBase) {
        CloseLibrary(SDL_OS3_OpenInputBase);
        SDL_OS3_OpenInputBase = NULL;
    }
    if (LowLevelBase) {
        CloseLibrary(LowLevelBase);
        LowLevelBase = NULL;
    }
}

static SDL_bool OS3_JoystickGetGamepadMapping(int index, SDL_GamepadMapping *out)
{
    return os3_openinput ? OI_JoystickGetGamepadMapping(index, out) : SDL_FALSE;
}

SDL_JoystickDriver SDL_AMIGAOS3_JoystickDriver = {
    OS3_JoystickInit,
    OS3_JoystickGetCount,
    OS3_JoystickDetect,
    OS3_JoystickGetDeviceName,
    OS3_JoystickGetDevicePath,
    OS3_JoystickGetDeviceSteamVirtualGamepadSlot,
    OS3_JoystickGetDevicePlayerIndex,
    OS3_JoystickSetDevicePlayerIndex,
    OS3_JoystickGetDeviceGUID,
    OS3_JoystickGetDeviceInstanceID,
    OS3_JoystickOpen,
    OS3_JoystickRumble,
    OS3_JoystickRumbleTriggers,
    OS3_JoystickGetCapabilities,
    OS3_JoystickSetLED,
    OS3_JoystickSendEffect,
    OS3_JoystickSetSensorsEnabled,
    OS3_JoystickUpdate,
    OS3_JoystickClose,
    OS3_JoystickQuit,
    OS3_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_AMIGAOS3 */
