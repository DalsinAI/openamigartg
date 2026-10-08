/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2 joystick for AmigaOS 3.x through lowlevel.library (OS 3.1 and later):
 * the joystick or CD32 pad in each game port. Replaces libSDL2-amigaos3's
 * stub. Port 2 (lowlevel unit 1) is SDL joystick 0, since that is where
 * Amiga games expect the stick; port 1 (unit 0) is SDL joystick 1 when
 * lowlevel reports a joystick or game controller there.
 *
 * Each joystick has 2 axes (digital, so -32768, 0 or 32767), 1 hat and
 * 7 buttons: red, blue, then on a CD32 pad green, yellow, play, forward
 * and reverse. Without lowlevel.library there are no joysticks.
 */

#include "../../SDL_internal.h"

#if SDL_JOYSTICK_AMIGAOS3

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <exec/types.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>

/* Opened here, not by the startup code, so a machine without it still runs. */
struct Library *LowLevelBase = NULL;

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

static int OS3_JoystickInit(void)
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

static int OS3_JoystickGetCount(void) { return os3_nports; }
static void OS3_JoystickDetect(void) { }

static const char *OS3_JoystickGetDeviceName(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].name : NULL;
}

static const char *OS3_JoystickGetDevicePath(int index) { return NULL; }
static int OS3_JoystickGetDeviceSteamVirtualGamepadSlot(int index) { return -1; }
static int OS3_JoystickGetDevicePlayerIndex(int index) { return index; }
static void OS3_JoystickSetDevicePlayerIndex(int index, int player) { }

static SDL_JoystickGUID OS3_JoystickGetDeviceGUID(int index)
{
    return SDL_CreateJoystickGUIDForName(OS3_JoystickGetDeviceName(index));
}

static SDL_JoystickID OS3_JoystickGetDeviceInstanceID(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].instance_id : -1;
}

static int OS3_JoystickOpen(SDL_Joystick *joy, int index)
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

static int OS3_JoystickRumble(SDL_Joystick *joy, Uint16 lo, Uint16 hi) { return SDL_Unsupported(); }
static int OS3_JoystickRumbleTriggers(SDL_Joystick *joy, Uint16 lo, Uint16 hi) { return SDL_Unsupported(); }
static Uint32 OS3_JoystickGetCapabilities(SDL_Joystick *joy) { return 0; }
static int OS3_JoystickSetLED(SDL_Joystick *joy, Uint8 r, Uint8 g, Uint8 b) { return SDL_Unsupported(); }
static int OS3_JoystickSendEffect(SDL_Joystick *joy, const void *data, int size) { return SDL_Unsupported(); }
static int OS3_JoystickSetSensorsEnabled(SDL_Joystick *joy, SDL_bool enabled) { return SDL_Unsupported(); }

static void OS3_JoystickUpdate(SDL_Joystick *joy)
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

static void OS3_JoystickClose(SDL_Joystick *joy)
{
    joy->hwdata = NULL;
}

static void OS3_JoystickQuit(void)
{
    os3_nports = 0;
    if (LowLevelBase) {
        CloseLibrary(LowLevelBase);
        LowLevelBase = NULL;
    }
}

static SDL_bool OS3_JoystickGetGamepadMapping(int index, SDL_GamepadMapping *out) { return SDL_FALSE; }

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
