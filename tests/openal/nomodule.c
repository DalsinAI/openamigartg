/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * nomodule.m: a program on libopenal.a still runs when OpenAL.module isn't
 * there, with no sound. With LIBS:OpenGPU/OpenAL.module renamed away, run
 * it from a Shell: one line, "OpenAL: this program's sound needs
 * LIBS:OpenGPU/OpenAL.module (OpenGPU), which isn't installed.", then
 * "nomodule: no sound device", and return code 0. With the module in place
 * it prints "nomodule: device AHI Mix" and returns 0. The stub's host test
 * is tests/openal/stub_nomodule.c. */
#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>

int main(void)
{
    ALCdevice *d = alcOpenDevice(NULL);
    if (!d) {
        printf("nomodule: no sound device\n");
        return 0;
    }
    printf("nomodule: device %s\n", alcGetString(d, ALC_DEVICE_SPECIFIER));
    alcCloseDevice(d);
    return 0;
}
