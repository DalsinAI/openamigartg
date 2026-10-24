/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The OpenAL extensions OpenAL.module (library/modules/openal) has, written
 * by the Team from the extension texts. Each block's macro says the header
 * knows the extension; whether the module has it is what
 * alIsExtensionPresent and alcIsExtensionPresent answer at run time.
 *
 * In the module:
 *   AL_EXT_FLOAT32       float buffers, converted to 16 bit when loaded;
 *   AL_EXT_MCFORMATS     quad, rear, 5.1, 6.1 and 7.1 buffers, folded down
 *                        to stereo when loaded;
 *   ALC_ENUMERATION_EXT  the list of devices (no tokens of its own);
 *   ALC_EXT_disconnect   ALC_CONNECTED, so a program learns when its sound
 *                        has gone.
 * OpenAL 1.1 has the 1.0-compatible ones as its own (AL_EXT_OFFSET,
 * AL_EXT_LINEAR_DISTANCE, AL_EXT_EXPONENT_DISTANCE, ALC_EXT_CAPTURE): their
 * tokens are in al.h and alc.h. */
#ifndef AL_ALEXT_H
#define AL_ALEXT_H

#include "al.h"
#include "alc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AL_EXT_FLOAT32
#define AL_EXT_FLOAT32 1
#define AL_FORMAT_MONO_FLOAT32 0x10010
#define AL_FORMAT_STEREO_FLOAT32 0x10011
#endif

/* The 8 formats are unsigned like AL_FORMAT_MONO8, the 16 formats signed
 * like AL_FORMAT_MONO16, and the 32 formats float like AL_EXT_FLOAT32's.
 * Each is interleaved; the module reads the channels in this order (the
 * usual order of these layouts):
 *   QUAD   front left, front right, rear left, rear right
 *   REAR   rear left, rear right
 *   51CHN  front left, front right, front centre, LFE, rear left, rear right
 *   61CHN  front left, front right, front centre, LFE, rear centre, side left, side right
 *   71CHN  front left, front right, front centre, LFE, rear left, rear right, side left, side right */
#ifndef AL_EXT_MCFORMATS
#define AL_EXT_MCFORMATS 1
#define AL_FORMAT_QUAD8 0x1204
#define AL_FORMAT_QUAD16 0x1205
#define AL_FORMAT_QUAD32 0x1206
#define AL_FORMAT_REAR8 0x1207
#define AL_FORMAT_REAR16 0x1208
#define AL_FORMAT_REAR32 0x1209
#define AL_FORMAT_51CHN8 0x120A
#define AL_FORMAT_51CHN16 0x120B
#define AL_FORMAT_51CHN32 0x120C
#define AL_FORMAT_61CHN8 0x120D
#define AL_FORMAT_61CHN16 0x120E
#define AL_FORMAT_61CHN32 0x120F
#define AL_FORMAT_71CHN8 0x1210
#define AL_FORMAT_71CHN16 0x1211
#define AL_FORMAT_71CHN32 0x1212
#endif

#ifndef ALC_ENUMERATION_EXT
#define ALC_ENUMERATION_EXT 1
#endif

#ifndef ALC_EXT_disconnect
#define ALC_EXT_disconnect 1
#define ALC_CONNECTED 0x313
#endif

/* Known to the header so programs that ask for it build; the module
 * doesn't have it yet, and alcIsExtensionPresent says so. */
#ifndef ALC_ENUMERATE_ALL_EXT
#define ALC_ENUMERATE_ALL_EXT 1
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#endif

#ifdef __cplusplus
}
#endif

#endif
