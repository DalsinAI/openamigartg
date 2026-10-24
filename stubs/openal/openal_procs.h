/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenAL.module's jump table: every AL and ALC call, in the order of the
 * table libopenal.a (stubs/openal) holds and the module fills
 * (openal_module.h). The order is the ABI: calls are only ever added at the
 * end, so a program built today runs on any later module.
 *
 * Include it with the macros defined:
 *   OPENAL_PROC(rc, fn, params, args)       a call that returns rc
 *   OPENAL_PROCV(fn, params, args)          a call that returns nothing
 *   OPENAL_PROC_OWN(rc, fn, params, args)   a call the stub answers itself
 *                                           (the two GetProcAddress: their
 *                                           answer is the stub's own entry);
 *                                           OPENAL_PROC when not defined
 * Version 1 (24 October 2026): OpenAL 1.1 and ALC 1.1, 93 calls. */
#ifndef OPENAL_PROC_OWN
#define OPENAL_PROC_OWN OPENAL_PROC
#define OPENAL_PROCS_OWN_DEFAULTED
#endif

OPENAL_PROCV(alEnable, (ALenum capability), (capability))
OPENAL_PROCV(alDisable, (ALenum capability), (capability))
OPENAL_PROC(ALboolean, alIsEnabled, (ALenum capability), (capability))
OPENAL_PROC(const ALchar *, alGetString, (ALenum param), (param))
OPENAL_PROCV(alGetBooleanv, (ALenum param, ALboolean *values), (param, values))
OPENAL_PROCV(alGetIntegerv, (ALenum param, ALint *values), (param, values))
OPENAL_PROCV(alGetFloatv, (ALenum param, ALfloat *values), (param, values))
OPENAL_PROCV(alGetDoublev, (ALenum param, ALdouble *values), (param, values))
OPENAL_PROC(ALboolean, alGetBoolean, (ALenum param), (param))
OPENAL_PROC(ALint, alGetInteger, (ALenum param), (param))
OPENAL_PROC(ALfloat, alGetFloat, (ALenum param), (param))
OPENAL_PROC(ALdouble, alGetDouble, (ALenum param), (param))
OPENAL_PROC(ALenum, alGetError, (void), ())
OPENAL_PROC(ALboolean, alIsExtensionPresent, (const ALchar *extname), (extname))
OPENAL_PROC_OWN(void *, alGetProcAddress, (const ALchar *fname), (fname))
OPENAL_PROC(ALenum, alGetEnumValue, (const ALchar *ename), (ename))
OPENAL_PROCV(alListenerf, (ALenum param, ALfloat value), (param, value))
OPENAL_PROCV(alListener3f, (ALenum param, ALfloat value1, ALfloat value2, ALfloat value3), (param, value1, value2, value3))
OPENAL_PROCV(alListenerfv, (ALenum param, const ALfloat *values), (param, values))
OPENAL_PROCV(alListeneri, (ALenum param, ALint value), (param, value))
OPENAL_PROCV(alListener3i, (ALenum param, ALint value1, ALint value2, ALint value3), (param, value1, value2, value3))
OPENAL_PROCV(alListeneriv, (ALenum param, const ALint *values), (param, values))
OPENAL_PROCV(alGetListenerf, (ALenum param, ALfloat *value), (param, value))
OPENAL_PROCV(alGetListener3f, (ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3), (param, value1, value2, value3))
OPENAL_PROCV(alGetListenerfv, (ALenum param, ALfloat *values), (param, values))
OPENAL_PROCV(alGetListeneri, (ALenum param, ALint *value), (param, value))
OPENAL_PROCV(alGetListener3i, (ALenum param, ALint *value1, ALint *value2, ALint *value3), (param, value1, value2, value3))
OPENAL_PROCV(alGetListeneriv, (ALenum param, ALint *values), (param, values))
OPENAL_PROCV(alGenSources, (ALsizei n, ALuint *sources), (n, sources))
OPENAL_PROCV(alDeleteSources, (ALsizei n, const ALuint *sources), (n, sources))
OPENAL_PROC(ALboolean, alIsSource, (ALuint source), (source))
OPENAL_PROCV(alSourcef, (ALuint source, ALenum param, ALfloat value), (source, param, value))
OPENAL_PROCV(alSource3f, (ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3), (source, param, value1, value2, value3))
OPENAL_PROCV(alSourcefv, (ALuint source, ALenum param, const ALfloat *values), (source, param, values))
OPENAL_PROCV(alSourcei, (ALuint source, ALenum param, ALint value), (source, param, value))
OPENAL_PROCV(alSource3i, (ALuint source, ALenum param, ALint value1, ALint value2, ALint value3), (source, param, value1, value2, value3))
OPENAL_PROCV(alSourceiv, (ALuint source, ALenum param, const ALint *values), (source, param, values))
OPENAL_PROCV(alGetSourcef, (ALuint source, ALenum param, ALfloat *value), (source, param, value))
OPENAL_PROCV(alGetSource3f, (ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3), (source, param, value1, value2, value3))
OPENAL_PROCV(alGetSourcefv, (ALuint source, ALenum param, ALfloat *values), (source, param, values))
OPENAL_PROCV(alGetSourcei, (ALuint source, ALenum param, ALint *value), (source, param, value))
OPENAL_PROCV(alGetSource3i, (ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3), (source, param, value1, value2, value3))
OPENAL_PROCV(alGetSourceiv, (ALuint source, ALenum param, ALint *values), (source, param, values))
OPENAL_PROCV(alSourcePlayv, (ALsizei n, const ALuint *sources), (n, sources))
OPENAL_PROCV(alSourceStopv, (ALsizei n, const ALuint *sources), (n, sources))
OPENAL_PROCV(alSourceRewindv, (ALsizei n, const ALuint *sources), (n, sources))
OPENAL_PROCV(alSourcePausev, (ALsizei n, const ALuint *sources), (n, sources))
OPENAL_PROCV(alSourcePlay, (ALuint source), (source))
OPENAL_PROCV(alSourceStop, (ALuint source), (source))
OPENAL_PROCV(alSourceRewind, (ALuint source), (source))
OPENAL_PROCV(alSourcePause, (ALuint source), (source))
OPENAL_PROCV(alSourceQueueBuffers, (ALuint source, ALsizei nb, const ALuint *buffers), (source, nb, buffers))
OPENAL_PROCV(alSourceUnqueueBuffers, (ALuint source, ALsizei nb, ALuint *buffers), (source, nb, buffers))
OPENAL_PROCV(alGenBuffers, (ALsizei n, ALuint *buffers), (n, buffers))
OPENAL_PROCV(alDeleteBuffers, (ALsizei n, const ALuint *buffers), (n, buffers))
OPENAL_PROC(ALboolean, alIsBuffer, (ALuint buffer), (buffer))
OPENAL_PROCV(alBufferData, (ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq), (buffer, format, data, size, freq))
OPENAL_PROCV(alBufferf, (ALuint buffer, ALenum param, ALfloat value), (buffer, param, value))
OPENAL_PROCV(alBuffer3f, (ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3), (buffer, param, value1, value2, value3))
OPENAL_PROCV(alBufferfv, (ALuint buffer, ALenum param, const ALfloat *values), (buffer, param, values))
OPENAL_PROCV(alBufferi, (ALuint buffer, ALenum param, ALint value), (buffer, param, value))
OPENAL_PROCV(alBuffer3i, (ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3), (buffer, param, value1, value2, value3))
OPENAL_PROCV(alBufferiv, (ALuint buffer, ALenum param, const ALint *values), (buffer, param, values))
OPENAL_PROCV(alGetBufferf, (ALuint buffer, ALenum param, ALfloat *value), (buffer, param, value))
OPENAL_PROCV(alGetBuffer3f, (ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3), (buffer, param, value1, value2, value3))
OPENAL_PROCV(alGetBufferfv, (ALuint buffer, ALenum param, ALfloat *values), (buffer, param, values))
OPENAL_PROCV(alGetBufferi, (ALuint buffer, ALenum param, ALint *value), (buffer, param, value))
OPENAL_PROCV(alGetBuffer3i, (ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3), (buffer, param, value1, value2, value3))
OPENAL_PROCV(alGetBufferiv, (ALuint buffer, ALenum param, ALint *values), (buffer, param, values))
OPENAL_PROCV(alDopplerFactor, (ALfloat value), (value))
OPENAL_PROCV(alDopplerVelocity, (ALfloat value), (value))
OPENAL_PROCV(alSpeedOfSound, (ALfloat value), (value))
OPENAL_PROCV(alDistanceModel, (ALenum distanceModel), (distanceModel))
OPENAL_PROC(ALCcontext *, alcCreateContext, (ALCdevice *device, const ALCint *attrlist), (device, attrlist))
OPENAL_PROC(ALCboolean, alcMakeContextCurrent, (ALCcontext *context), (context))
OPENAL_PROCV(alcProcessContext, (ALCcontext *context), (context))
OPENAL_PROCV(alcSuspendContext, (ALCcontext *context), (context))
OPENAL_PROCV(alcDestroyContext, (ALCcontext *context), (context))
OPENAL_PROC(ALCcontext *, alcGetCurrentContext, (void), ())
OPENAL_PROC(ALCdevice *, alcGetContextsDevice, (ALCcontext *context), (context))
OPENAL_PROC(ALCdevice *, alcOpenDevice, (const ALCchar *devicename), (devicename))
OPENAL_PROC(ALCboolean, alcCloseDevice, (ALCdevice *device), (device))
OPENAL_PROC(ALCenum, alcGetError, (ALCdevice *device), (device))
OPENAL_PROC(ALCboolean, alcIsExtensionPresent, (ALCdevice *device, const ALCchar *extname), (device, extname))
OPENAL_PROC_OWN(void *, alcGetProcAddress, (ALCdevice *device, const ALCchar *funcname), (device, funcname))
OPENAL_PROC(ALCenum, alcGetEnumValue, (ALCdevice *device, const ALCchar *enumname), (device, enumname))
OPENAL_PROC(const ALCchar *, alcGetString, (ALCdevice *device, ALCenum param), (device, param))
OPENAL_PROCV(alcGetIntegerv, (ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values), (device, param, size, values))
OPENAL_PROC(ALCdevice *, alcCaptureOpenDevice, (const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize), (devicename, frequency, format, buffersize))
OPENAL_PROC(ALCboolean, alcCaptureCloseDevice, (ALCdevice *device), (device))
OPENAL_PROCV(alcCaptureStart, (ALCdevice *device), (device))
OPENAL_PROCV(alcCaptureStop, (ALCdevice *device), (device))
OPENAL_PROCV(alcCaptureSamples, (ALCdevice *device, ALCvoid *buffer, ALCsizei samples), (device, buffer, samples))

#ifdef OPENAL_PROCS_OWN_DEFAULTED
#undef OPENAL_PROC_OWN
#undef OPENAL_PROCS_OWN_DEFAULTED
#endif
