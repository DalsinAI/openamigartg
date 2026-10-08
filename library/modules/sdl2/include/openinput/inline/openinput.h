/* Copyright (c) 2026 Dalsin Limited. OpenInput, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Inline calls for bebbo's m68k-amigaos-gcc, from library/openinput_lib.sfd.
 * The tag-list helpers build their list in an array (never by taking the
 * address of the first variable argument, which an inlined helper can lose). */
#ifndef _INLINE_OPENINPUT_H
#define _INLINE_OPENINPUT_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif
#ifndef OPENINPUT_BASE_NAME
#define OPENINPUT_BASE_NAME OpenInputBase
#endif

#define OIN_ListControllers(buffer, max, infoSize) \
    LP3(0x1e, ULONG, OIN_ListControllers, struct OIControllerInfo *, buffer, a0, ULONG, max, d0, ULONG, infoSize, d1, , OPENINPUT_BASE_NAME)
#define OIN_GetControllerInfo(id, info, infoSize) \
    LP3(0x24, LONG, OIN_GetControllerInfo, ULONG, id, d0, struct OIControllerInfo *, info, a0, ULONG, infoSize, d1, , OPENINPUT_BASE_NAME)
#define OIN_OpenControllerA(id, tags) \
    LP2(0x2a, APTR, OIN_OpenControllerA, ULONG, id, d0, struct TagItem *, tags, a0, , OPENINPUT_BASE_NAME)
#define OIN_CloseController(controller) \
    LP1NR(0x30, OIN_CloseController, APTR, controller, a0, , OPENINPUT_BASE_NAME)
#define OIN_ReadState(controller, state, stateSize) \
    LP3(0x36, LONG, OIN_ReadState, APTR, controller, a0, struct OIState *, state, a1, ULONG, stateSize, d0, , OPENINPUT_BASE_NAME)
#define OIN_ReadRaw(controller, raw, rawSize) \
    LP3(0x3c, LONG, OIN_ReadRaw, APTR, controller, a0, struct OIRawState *, raw, a1, ULONG, rawSize, d0, , OPENINPUT_BASE_NAME)
#define OIN_Rumble(controller, low, high, millis) \
    LP4(0x42, LONG, OIN_Rumble, APTR, controller, a0, ULONG, low, d0, ULONG, high, d1, ULONG, millis, d2, , OPENINPUT_BASE_NAME)
#define OIN_AddNotifyA(port, tags) \
    LP2(0x48, APTR, OIN_AddNotifyA, struct MsgPort *, port, a0, struct TagItem *, tags, a1, , OPENINPUT_BASE_NAME)
#define OIN_RemNotify(notify) \
    LP1NR(0x4e, OIN_RemNotify, APTR, notify, a0, , OPENINPUT_BASE_NAME)
#define OIN_GetMapping(guid, buffer, size) \
    LP3(0x54, LONG, OIN_GetMapping, UBYTE *, guid, a0, STRPTR, buffer, a1, ULONG, size, d0, , OPENINPUT_BASE_NAME)
#define OIN_SetMapping(line, flags) \
    LP2(0x5a, LONG, OIN_SetMapping, STRPTR, line, a0, ULONG, flags, d0, , OPENINPUT_BASE_NAME)
#define OIN_ButtonLabel(id, button) \
    LP2(0x60, STRPTR, OIN_ButtonLabel, ULONG, id, d0, ULONG, button, d1, , OPENINPUT_BASE_NAME)
#define OIN_GetLegacyPort(port, legacy, size) \
    LP3(0x66, LONG, OIN_GetLegacyPort, ULONG, port, d0, struct OILegacyPort *, legacy, a0, ULONG, size, d1, , OPENINPUT_BASE_NAME)
#define OIN_SetLegacyPortA(port, id, tags) \
    LP3(0x6c, LONG, OIN_SetLegacyPortA, ULONG, port, d0, ULONG, id, d1, struct TagItem *, tags, a0, , OPENINPUT_BASE_NAME)

#ifndef NO_INLINE_STDARG
#define OIN_OpenController(id, ...) \
    ({ ULONG _oin_tags[] = { __VA_ARGS__ }; OIN_OpenControllerA((id), (struct TagItem *)_oin_tags); })
#define OIN_AddNotify(port, ...) \
    ({ ULONG _oin_tags[] = { __VA_ARGS__ }; OIN_AddNotifyA((port), (struct TagItem *)_oin_tags); })
#define OIN_SetLegacyPort(port, id, ...) \
    ({ ULONG _oin_tags[] = { __VA_ARGS__ }; OIN_SetLegacyPortA((port), (id), (struct TagItem *)_oin_tags); })
#endif

#endif /* _INLINE_OPENINPUT_H */
