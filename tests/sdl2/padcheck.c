/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * padcheck: SDL 2's joysticks and game controllers on the Amiga, with no
 * window. Lists every joystick (name, GUID, whether SDL sees a game
 * controller, the mapping), then watches events for SECONDS: hot-plug,
 * every controller button and axis, and a short rumble on each controller
 * that has it. At the end, one line per check, as sdlcheck prints them.
 *
 *   padcheck [SECONDS] [MAPPINGS]
 *       SECONDS   how long to watch (default 40)
 *       MAPPINGS  a file of SDL mapping lines, loaded first with
 *                 SDL_GameControllerAddMappingsFromFile (lines need
 *                 platform:AmigaOS 3)
 *
 * Exit 0 when every check passes. With OpenInput's test pad (vendor
 * 0xDA15: it presses each button in turn, A first, and sweeps the sticks
 * and triggers) every check runs; with another game controller, all but
 * the A-before-B order; without one, only the joystick check.
 */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_PADS 8

static int failures = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("%-12s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

static Uint32 buttons_seen = 0;               /* SDL_GameControllerButton bits */
static Sint16 axis_min[SDL_CONTROLLER_AXIS_MAX], axis_max[SDL_CONTROLLER_AXIS_MAX];
static int axis_moved[SDL_CONTROLLER_AXIS_MAX];
static int joy_added = 0, joy_removed = 0, gc_added = 0, gc_removed = 0;
static int max_joysticks = 0;
static int rumble_tried = 0, rumble_ok = 0;
static int a_then_b = 0, b_then_a = 0;        /* which of A and B went down first, per cycle */
static int last_ab = -1;
static int testpad = 0;                      /* OpenInput's test pad (vendor 0xDA15) was seen */

static SDL_GameController *pads[MAX_PADS];

static void describe(int index)
{
    char guid[33];
    char *mapping;

    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
    printf("joystick %d: \"%s\" guid %s vendor %04x product %04x player %d%s\n",
           index, SDL_JoystickNameForIndex(index), guid,
           SDL_JoystickGetDeviceVendor(index), SDL_JoystickGetDeviceProduct(index),
           SDL_JoystickGetDevicePlayerIndex(index),
           SDL_IsGameController(index) ? ", game controller" : "");
    if (SDL_JoystickGetDeviceVendor(index) == 0xDA15) {
        testpad = 1;
    }
    mapping = SDL_GameControllerMappingForDeviceIndex(index);
    if (mapping) {
        printf("  mapping: %s\n", mapping);
        SDL_free(mapping);
    }
    fflush(stdout);
}

static void open_pad(int index)
{
    SDL_GameController *gc;
    SDL_Joystick *joy;
    int i;

    if (!SDL_IsGameController(index)) {
        joy = SDL_JoystickOpen(index);
        if (joy) {
            printf("  opened as a joystick: %d axes, %d hats, %d buttons\n",
                   SDL_JoystickNumAxes(joy), SDL_JoystickNumHats(joy), SDL_JoystickNumButtons(joy));
        }
        return;
    }
    gc = SDL_GameControllerOpen(index);
    if (!gc) {
        printf("  couldn't open: %s\n", SDL_GetError());
        return;
    }
    joy = SDL_GameControllerGetJoystick(gc);
    printf("  opened as a game controller (instance %d): %d axes, %d hats, %d buttons, rumble %s\n",
           (int)SDL_JoystickInstanceID(joy), SDL_JoystickNumAxes(joy), SDL_JoystickNumHats(joy),
           SDL_JoystickNumButtons(joy), SDL_GameControllerHasRumble(gc) ? "yes" : "no");
    if (SDL_GameControllerHasRumble(gc)) {
        rumble_tried++;
        if (SDL_GameControllerRumble(gc, 0x4000, 0xC000, 300) == 0) {
            rumble_ok++;
        } else {
            printf("  rumble: %s\n", SDL_GetError());
        }
    }
    fflush(stdout);
    for (i = 0; i < MAX_PADS; ++i) {
        if (!pads[i]) {
            pads[i] = gc;
            return;
        }
    }
    SDL_GameControllerClose(gc);
}

static void close_pad(SDL_JoystickID which)
{
    int i;
    for (i = 0; i < MAX_PADS; ++i) {
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == which) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
    }
}

int main(int argc, char *argv[])
{
    int seconds = argc > 1 ? atoi(argv[1]) : 40;
    Uint32 end;
    SDL_Event ev;
    char line[160];
    int i, n, gc_ever = 0, nbuttons, naxes;

    if (seconds <= 0) {
        seconds = 40;
    }
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    printf("padcheck: SDL %d.%d.%d on \"%s\"\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
           SDL_GetPlatform());
    if (argc > 2) {
        n = SDL_GameControllerAddMappingsFromFile(argv[2]);
        printf("mappings from %s: %d%s%s\n", argv[2], n, n < 0 ? " " : "", n < 0 ? SDL_GetError() : "");
        /* 0 is right too: a line for a pad SDL already had a mapping for replaces it. */
        report("mapfile", n >= 0, "SDL_GameControllerAddMappingsFromFile read the file");
    }
    for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        axis_min[i] = 32767;
        axis_max[i] = -32768;
    }

    n = SDL_NumJoysticks();
    max_joysticks = n;
    printf("%d joystick(s) at the start\n", n);
    /* The ones there at the start come as SDL_JOYDEVICEADDED too, and are opened then. */
    end = SDL_GetTicks() + (Uint32)seconds * 1000;
    while (!SDL_TICKS_PASSED(SDL_GetTicks(), end)) {
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_JOYDEVICEADDED:
                joy_added++;
                last_ab = -1;   /* a pad that just came may have missed its A */
                printf("[%6u] joystick added: device %d\n", (unsigned)SDL_GetTicks(), (int)ev.jdevice.which);
                describe(ev.jdevice.which);
                if (SDL_IsGameController(ev.jdevice.which)) {
                    gc_ever = 1;
                }
                open_pad(ev.jdevice.which);
                if (SDL_NumJoysticks() > max_joysticks) {
                    max_joysticks = SDL_NumJoysticks();
                }
                break;
            case SDL_JOYDEVICEREMOVED:
                joy_removed++;
                printf("[%6u] joystick removed: instance %d\n", (unsigned)SDL_GetTicks(), (int)ev.jdevice.which);
                close_pad(ev.jdevice.which);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                gc_added++;
                printf("[%6u] controller added: device %d\n", (unsigned)SDL_GetTicks(), (int)ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                gc_removed++;
                printf("[%6u] controller removed: instance %d\n", (unsigned)SDL_GetTicks(), (int)ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (!(buttons_seen & (1UL << ev.cbutton.button))) {
                    printf("[%6u] button %s\n", (unsigned)SDL_GetTicks(),
                           SDL_GameControllerGetStringForButton((SDL_GameControllerButton)ev.cbutton.button));
                }
                buttons_seen |= 1UL << ev.cbutton.button;
                if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    last_ab = 0;
                } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (last_ab == 0) {
                        a_then_b++;
                    } else if (last_ab == 1) {
                        b_then_a++;     /* B twice with no A between */
                    }
                    last_ab = 1;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (ev.caxis.axis < SDL_CONTROLLER_AXIS_MAX) {
                    axis_moved[ev.caxis.axis] = 1;
                    if (ev.caxis.value < axis_min[ev.caxis.axis]) axis_min[ev.caxis.axis] = ev.caxis.value;
                    if (ev.caxis.value > axis_max[ev.caxis.axis]) axis_max[ev.caxis.axis] = ev.caxis.value;
                }
                break;
            default:
                break;
            }
        }
        SDL_Delay(10);
    }

    printf("--- after %d s\n", seconds);
    SDL_snprintf(line, sizeof(line), "%d at most; %d added, %d removed", max_joysticks, joy_added, joy_removed);
    report("joysticks", max_joysticks > 0, line);
    if (gc_ever) {
        for (nbuttons = 0, i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
            nbuttons += (buttons_seen >> i) & 1;
        }
        SDL_snprintf(line, sizeof(line), "%d added, %d removed", gc_added, gc_removed);
        report("controllers", gc_added > 0, line);
        SDL_snprintf(line, sizeof(line), "%d of %d buttons seen (bits %06lx)", nbuttons,
                     (int)SDL_CONTROLLER_BUTTON_MAX, (unsigned long)buttons_seen);
        report("buttons", nbuttons == SDL_CONTROLLER_BUTTON_MAX, line);
        for (naxes = 0, i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
            printf("  axis %-12s %6d .. %6d\n",
                   SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)i), axis_min[i], axis_max[i]);
            if (i >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                naxes += axis_moved[i] && axis_min[i] >= 0 && axis_min[i] < 2000 && axis_max[i] > 30000;
            } else {
                naxes += axis_moved[i] && axis_min[i] < -30000 && axis_max[i] > 30000;
            }
        }
        SDL_snprintf(line, sizeof(line), "%d of %d axes swept their whole range (triggers 0 up)", naxes,
                     (int)SDL_CONTROLLER_AXIS_MAX);
        report("axes", naxes == SDL_CONTROLLER_AXIS_MAX, line);
        SDL_snprintf(line, sizeof(line), "A before B %d times, B before A %d times", a_then_b, b_then_a);
        if (testpad) {      /* the test pad presses A, then B, ... */
            report("a-b order", a_then_b > 0 && b_then_a == 0, line);
        }
        SDL_snprintf(line, sizeof(line), "%d of %d controllers with rumble took it", rumble_ok, rumble_tried);
        report("rumble", rumble_ok == rumble_tried, line);
        if (joy_removed) {
            SDL_snprintf(line, sizeof(line), "%d joystick and %d controller removals, %d and %d arrivals",
                         joy_removed, gc_removed, joy_added, gc_added);
            report("hot-plug", gc_removed == joy_removed && gc_added == joy_added, line);
        }
    }
    for (i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            SDL_GameControllerClose(pads[i]);
        }
    }
    SDL_Quit();
    printf("padcheck: %d failure(s)\n", failures);
    return failures ? 5 : 0;
}
