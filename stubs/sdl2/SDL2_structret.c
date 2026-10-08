/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL's six calls that return a struct, for programs built with GCC 6.
 * GCC 6 passes the address for a returned struct in A0; the GCC 16 that
 * builds SDL2.module and libSDL2.a takes it in A1, so a GCC 6 program
 * calling them straight would get garbage and have 12 or 16 bytes written
 * wherever A1 happened to point. SDL's headers (patches/sdl2/0008) send a
 * GCC 6 program's calls here instead, with the address as an argument.
 * Built with GCC 16 into libSDL2.a and libSDL2_static.a, where the calls
 * below are ordinary GCC 16 calls.
 */
#include "SDL.h"

void SDLCALL SDL_OS3_JoystickGetDeviceGUID_p(SDL_JoystickGUID *r, int device_index)
{
    *r = SDL_JoystickGetDeviceGUID(device_index);
}

void SDLCALL SDL_OS3_JoystickGetGUID_p(SDL_JoystickGUID *r, SDL_Joystick *joystick)
{
    *r = SDL_JoystickGetGUID(joystick);
}

void SDLCALL SDL_OS3_JoystickGetGUIDFromString_p(SDL_JoystickGUID *r, const char *pchGUID)
{
    *r = SDL_JoystickGetGUIDFromString(pchGUID);
}

void SDLCALL SDL_OS3_GUIDFromString_p(SDL_GUID *r, const char *pchGUID)
{
    *r = SDL_GUIDFromString(pchGUID);
}

void SDLCALL SDL_OS3_GameControllerGetBindForAxis_p(SDL_GameControllerButtonBind *r,
                                                    SDL_GameController *gamecontroller, SDL_GameControllerAxis axis)
{
    *r = SDL_GameControllerGetBindForAxis(gamecontroller, axis);
}

void SDLCALL SDL_OS3_GameControllerGetBindForButton_p(SDL_GameControllerButtonBind *r,
                                                      SDL_GameController *gamecontroller, SDL_GameControllerButton button)
{
    *r = SDL_GameControllerGetBindForButton(gamecontroller, button);
}
