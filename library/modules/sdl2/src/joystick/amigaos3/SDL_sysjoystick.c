/* Copyright (c) 2026 Dalsin Limited. OpenGPU, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2 joystick backend for AmigaOS 3.x through openinput.library.
 * OpenInput owns controller discovery/mapping; SDL is only a front end.
 */
#include "../../SDL_internal.h"

#if SDL_JOYSTICK_AMIGAOS3

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/openinput.h>
#include <libraries/openinput.h>
#include <string.h>

#define OS3_MAX_PORTS 8

typedef struct {
    ULONG id;
    APTR handle;
    SDL_JoystickID instance_id;
    struct OIControllerInfo info;
} OS3_JoyPort;

struct Library *OpenInputBase = NULL;
static OS3_JoyPort os3_ports[OS3_MAX_PORTS];
static int os3_nports = 0;

static int OS3_JoystickInit(void)
{
    struct OIControllerInfo infos[OS3_MAX_PORTS];
    ULONG count, i;

    os3_nports = 0;
    OpenInputBase = OpenLibrary((CONST_STRPTR)OPENINPUT_NAME, OPENINPUT_VERSION);
    if (!OpenInputBase)
        return 0;

    count = OIN_ListControllers(infos, OS3_MAX_PORTS, sizeof(infos[0]));
    if (count > OS3_MAX_PORTS)
        count = OS3_MAX_PORTS;

    for (i = 0; i < count; ++i) {
        OS3_JoyPort *p = &os3_ports[os3_nports];
        p->id = infos[i].oci_ID;
        p->info = infos[i];
        p->handle = NULL;
        p->instance_id = SDL_GetNextJoystickInstanceID();
        SDL_PrivateJoystickAdded(p->instance_id);
        os3_nports++;
    }
    return 0;
}

static int OS3_JoystickGetCount(void) { return os3_nports; }
static void OS3_JoystickDetect(void) { }

static const char *OS3_JoystickGetDeviceName(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].info.oci_Name : NULL;
}

static const char *OS3_JoystickGetDevicePath(int index) { return NULL; }
static int OS3_JoystickGetDeviceSteamVirtualGamepadSlot(int index) { return -1; }
static int OS3_JoystickGetDevicePlayerIndex(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].info.oci_Player : -1;
}
static void OS3_JoystickSetDevicePlayerIndex(int index, int player) { (void)index; (void)player; }

static SDL_JoystickGUID OS3_JoystickGetDeviceGUID(int index)
{
    SDL_JoystickGUID guid;
    SDL_zero(guid);
    if (index >= 0 && index < os3_nports)
        memcpy(guid.data, os3_ports[index].info.oci_GUID, sizeof(guid.data));
    return guid;
}

static SDL_JoystickID OS3_JoystickGetDeviceInstanceID(int index)
{
    return (index >= 0 && index < os3_nports) ? os3_ports[index].instance_id : -1;
}

static int OS3_JoystickOpen(SDL_Joystick *joy, int index)
{
    OS3_JoyPort *p;
    if (index < 0 || index >= os3_nports)
        return SDL_SetError("No joystick at index %d", index);

    p = &os3_ports[index];
    p->handle = OIN_OpenControllerA(p->id, NULL);
    if (!p->handle)
        return SDL_SetError("OpenInput could not open controller %lu", p->id);

    joy->instance_id = p->instance_id;
    joy->hwdata = (struct joystick_hwdata *)p;
    joy->naxes = OIAXIS_COUNT;
    joy->nhats = 1;
    joy->nbuttons = OIB_COUNT;
    return 0;
}

static int OS3_JoystickRumble(SDL_Joystick *joy, Uint16 lo, Uint16 hi)
{
    OS3_JoyPort *p = (OS3_JoyPort *)joy->hwdata;
    if (!p || !p->handle) return SDL_SetError("Controller is not open");
    return OIN_Rumble(p->handle, lo, hi, 250) == OIERR_OK ? 0 : SDL_Unsupported();
}
static int OS3_JoystickRumbleTriggers(SDL_Joystick *joy, Uint16 lo, Uint16 hi) { (void)joy; (void)lo; (void)hi; return SDL_Unsupported(); }
static Uint32 OS3_JoystickGetCapabilities(SDL_Joystick *joy)
{
    OS3_JoyPort *p = (OS3_JoyPort *)joy->hwdata;
    return (p && (p->info.oci_Flags & OICF_RUMBLE)) ? SDL_JOYCAP_RUMBLE : 0;
}
static int OS3_JoystickSetLED(SDL_Joystick *joy, Uint8 r, Uint8 g, Uint8 b) { (void)joy; (void)r; (void)g; (void)b; return SDL_Unsupported(); }
static int OS3_JoystickSendEffect(SDL_Joystick *joy, const void *data, int size) { (void)joy; (void)data; (void)size; return SDL_Unsupported(); }
static int OS3_JoystickSetSensorsEnabled(SDL_Joystick *joy, SDL_bool enabled) { (void)joy; (void)enabled; return SDL_Unsupported(); }

static void OS3_JoystickUpdate(SDL_Joystick *joy)
{
    OS3_JoyPort *p = (OS3_JoyPort *)joy->hwdata;
    struct OIState st;
    int i;
    Uint8 hat = SDL_HAT_CENTERED;

    if (!p || !p->handle)
        return;
    if (OIN_ReadState(p->handle, &st, sizeof(st)) != OIERR_OK)
        return;

    for (i = 0; i < OIAXIS_COUNT; ++i)
        SDL_PrivateJoystickAxis(joy, i, st.ois_Axes[i]);

    if (st.ois_Buttons & OIBF(OIB_DPAD_UP)) hat |= SDL_HAT_UP;
    if (st.ois_Buttons & OIBF(OIB_DPAD_RIGHT)) hat |= SDL_HAT_RIGHT;
    if (st.ois_Buttons & OIBF(OIB_DPAD_DOWN)) hat |= SDL_HAT_DOWN;
    if (st.ois_Buttons & OIBF(OIB_DPAD_LEFT)) hat |= SDL_HAT_LEFT;
    SDL_PrivateJoystickHat(joy, 0, hat);

    for (i = 0; i < OIB_COUNT; ++i)
        SDL_PrivateJoystickButton(joy, (Uint8)i,
            (st.ois_Buttons & OIBF(i)) ? SDL_PRESSED : SDL_RELEASED);
}

static void OS3_JoystickClose(SDL_Joystick *joy)
{
    OS3_JoyPort *p = (OS3_JoyPort *)joy->hwdata;
    if (p && p->handle) {
        OIN_CloseController(p->handle);
        p->handle = NULL;
    }
    joy->hwdata = NULL;
}

static void OS3_JoystickQuit(void)
{
    int i;
    for (i = 0; i < os3_nports; ++i) {
        if (os3_ports[i].handle) {
            OIN_CloseController(os3_ports[i].handle);
            os3_ports[i].handle = NULL;
        }
    }
    os3_nports = 0;
    if (OpenInputBase) {
        CloseLibrary(OpenInputBase);
        OpenInputBase = NULL;
    }
}

static SDL_bool OS3_JoystickGetGamepadMapping(int index, SDL_GamepadMapping *out)
{
    (void)index; (void)out;
    return SDL_FALSE;
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
