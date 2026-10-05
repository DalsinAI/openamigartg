/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Inline calls for bebbo's m68k-amigaos-gcc, from library/openrtg_lib.sfd. */
#ifndef _INLINE_OPENRTG_H
#define _INLINE_OPENRTG_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#define ORTG_DisplayDatabase(on) \
    LP1(0x42, LONG, ORTG_DisplayDatabase, ULONG, on, d0, , OPENRTG_BASE_NAME)
#endif
#ifndef OPENRTG_BASE_NAME
#define OPENRTG_BASE_NAME OpenRTGBase
#define ORTG_DisplayDatabase(on) \
    LP1(0x42, LONG, ORTG_DisplayDatabase, ULONG, on, d0, , OPENRTG_BASE_NAME)
#endif
#define ORTG_MonitorCount() \
    LP0(0x1e, ULONG, ORTG_MonitorCount, , OPENRTG_BASE_NAME)
#define ORTG_NextMode(monitor, previous) \
    LP2(0x24, ULONG, ORTG_NextMode, ULONG, monitor, d0, ULONG, previous, d1, , OPENRTG_BASE_NAME)
#define ORTG_GetMode(modeID, mode) \
    LP2(0x2a, BOOL, ORTG_GetMode, ULONG, modeID, d0, struct OpenRTGMode *, mode, a0, , OPENRTG_BASE_NAME)
#define ORTG_BestMode(monitor, width, height, depth) \
    LP4(0x30, ULONG, ORTG_BestMode, ULONG, monitor, d0, ULONG, width, d1, ULONG, height, d2, ULONG, depth, d3, , OPENRTG_BASE_NAME)
#define ORTG_SetModeList(monitor, all) \
    LP2(0x36, LONG, ORTG_SetModeList, ULONG, monitor, d0, ULONG, all, d1, , OPENRTG_BASE_NAME)
#define ORTG_BoardAddress(monitor) \
    LP1(0x3c, APTR, ORTG_BoardAddress, ULONG, monitor, d0, , OPENRTG_BASE_NAME)
#define ORTG_DisplayDatabase(on) \
    LP1(0x42, LONG, ORTG_DisplayDatabase, ULONG, on, d0, , OPENRTG_BASE_NAME)
#define ORTG_Screens(on) \
    LP1(0x48, LONG, ORTG_Screens, ULONG, on, d0, , OPENRTG_BASE_NAME)
#define ORTG_WritePixels(rp, x, y, pixels) \
    LP4(0x4e, LONG, ORTG_WritePixels, struct RastPort *, rp, a1, LONG, x, d0, LONG, y, d1, const struct OpenRTGPixels *, pixels, a0, , OPENRTG_BASE_NAME)
#define ORTG_ReadPixels(rp, x, y, pixels) \
    LP4(0x54, LONG, ORTG_ReadPixels, struct RastPort *, rp, a1, LONG, x, d0, LONG, y, d1, struct OpenRTGPixels *, pixels, a0, , OPENRTG_BASE_NAME)
#define ORTG_FillPixels(rp, x, y, width, height, argb) \
    LP6(0x5a, LONG, ORTG_FillPixels, struct RastPort *, rp, a1, LONG, x, d0, LONG, y, d1, LONG, width, d2, LONG, height, d3, ULONG, argb, d4, , OPENRTG_BASE_NAME)
#define ORTG_InvertPixels(rp, x, y, width, height) \
    LP5(0x60, LONG, ORTG_InvertPixels, struct RastPort *, rp, a1, LONG, x, d0, LONG, y, d1, LONG, width, d2, LONG, height, d3, , OPENRTG_BASE_NAME)
#define ORTG_BitMapInfo(bitmap, info) \
    LP2(0x66, BOOL, ORTG_BitMapInfo, struct BitMap *, bitmap, a0, struct OpenRTGBitMapInfo *, info, a1, , OPENRTG_BASE_NAME)
#endif
