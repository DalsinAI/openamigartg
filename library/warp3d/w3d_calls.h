/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's entry points, in their registers (from warp3d_lib.sfd).
 * Each takes the library base in a6, as every Amiga library call does. */
#ifndef W3D_CALLS_H
#define W3D_CALLS_H

#include "w3d_internal.h"

LIBCALL W3D_Context * LIB_W3D_CreateContext(REG(a0, ULONG * error), REG(a1, struct TagItem * CCTags), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_DestroyContext(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_GetState(REG(a0, W3D_Context * context), REG(d1, ULONG state), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetState(REG(a0, W3D_Context * context), REG(d0, ULONG state), REG(d1, ULONG action), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_CheckDriver(REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_LockHardware(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_UnLockHardware(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_WaitIdle(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_CheckIdle(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_Query(REG(a0, W3D_Context * context), REG(d0, ULONG query), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_GetTexFmtInfo(REG(a0, W3D_Context * context), REG(d0, ULONG format), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase));
LIBCALL W3D_Texture * LIB_W3D_AllocTexObj(REG(a0, W3D_Context * context), REG(a1, ULONG * error), REG(a2, struct TagItem * ATOTags), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_FreeTexObj(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_ReleaseTexture(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_FlushTextures(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetFilter(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(d0, ULONG min), REG(d1, ULONG mag), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetTexEnv(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(d1, ULONG envparam), REG(a2, W3D_Color * envcolor), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetWrapMode(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(d0, ULONG mode_s), REG(d1, ULONG mode_t), REG(a2, W3D_Color * bordercolor), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_UpdateTexImage(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a2, void * teximage), REG(d1, int level), REG(a3, ULONG * palette), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_UploadTexture(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawLine(REG(a0, W3D_Context * context), REG(a1, W3D_Line * line), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawPoint(REG(a0, W3D_Context * context), REG(a1, W3D_Point * point), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriangle(REG(a0, W3D_Context * context), REG(a1, W3D_Triangle * triangle), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriFan(REG(a0, W3D_Context * context), REG(a1, W3D_Triangles * triangles), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriStrip(REG(a0, W3D_Context * context), REG(a1, W3D_Triangles * triangles), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetAlphaMode(REG(a0, W3D_Context * context), REG(d1, ULONG mode), REG(a1, W3D_Float * refval), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetBlendMode(REG(a0, W3D_Context * context), REG(d0, ULONG srcfunc), REG(d1, ULONG dstfunc), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetDrawRegion(REG(a0, W3D_Context * context), REG(a1, struct BitMap * bm), REG(d1, int yoffset), REG(a2, W3D_Scissor * scissor), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetFogParams(REG(a0, W3D_Context * context), REG(a1, W3D_Fog * fogparams), REG(d1, ULONG fogmode), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetColorMask(REG(a0, W3D_Context * context), REG(d0, ULONG red), REG(d1, ULONG green), REG(d2, ULONG blue), REG(d3, ULONG alpha), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetStencilFunc(REG(a0, W3D_Context * context), REG(d0, ULONG func), REG(d1, ULONG refvalue), REG(d2, ULONG mask), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_AllocZBuffer(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_FreeZBuffer(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ClearZBuffer(REG(a0, W3D_Context * context), REG(a1, W3D_Double * clearvalue), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ReadZPixel(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(a1, W3D_Double * z), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ReadZSpan(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG n), REG(a1, W3D_Double * z), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetZCompareMode(REG(a0, W3D_Context * context), REG(d1, ULONG mode), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_AllocStencilBuffer(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ClearStencilBuffer(REG(a0, W3D_Context * context), REG(a1, ULONG * clearval), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_FillStencilBuffer(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG width), REG(d3, ULONG height), REG(d4, ULONG depth), REG(a1, void * data), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_FreeStencilBuffer(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ReadStencilPixel(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(a1, ULONG * st), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ReadStencilSpan(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG n), REG(a1, ULONG * st), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetLogicOp(REG(a0, W3D_Context * context), REG(d1, ULONG operation), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_Hint(REG(a0, W3D_Context * context), REG(d0, ULONG mode), REG(d1, ULONG quality), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetDrawRegionWBM(REG(a0, W3D_Context * context), REG(a1, W3D_Bitmap * bitmap), REG(a2, W3D_Scissor * scissor), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_GetDriverState(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_Flush(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetPenMask(REG(a0, W3D_Context * context), REG(d1, ULONG pen), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetStencilOp(REG(a0, W3D_Context * context), REG(d0, ULONG sfail), REG(d1, ULONG dpfail), REG(d2, ULONG dppass), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetWriteMask(REG(a0, W3D_Context * context), REG(d1, ULONG mask), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_WriteStencilPixel(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG st), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_WriteStencilSpan(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG n), REG(a1, ULONG * st), REG(a2, UBYTE * mask), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_WriteZPixel(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(a1, W3D_Double * z), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_WriteZSpan(REG(a0, W3D_Context * context), REG(d0, ULONG x), REG(d1, ULONG y), REG(d2, ULONG n), REG(a1, W3D_Double * z), REG(a2, UBYTE * mask), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetCurrentColor(REG(a0, W3D_Context * context), REG(a1, W3D_Color * color), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetCurrentPen(REG(a0, W3D_Context * context), REG(d1, ULONG pen), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_UpdateTexSubImage(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a2, void * teximage), REG(d1, ULONG level), REG(a3, ULONG * palette), REG(a4, W3D_Scissor * scissor), REG(d0, ULONG srcbpr), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_FreeAllTexObj(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_GetDestFmt(REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawLineStrip(REG(a0, W3D_Context * context), REG(a1, W3D_Lines * lines), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawLineLoop(REG(a0, W3D_Context * context), REG(a1, W3D_Lines * lines), REG(a6, struct W3DBase *libbase));
LIBCALL W3D_Driver ** LIB_W3D_GetDrivers(REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_QueryDriver(REG(a0, W3D_Driver * driver), REG(d0, ULONG query), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_GetDriverTexFmtInfo(REG(a0, W3D_Driver * driver), REG(d0, ULONG format), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_RequestMode(REG(a0, struct TagItem * taglist), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_SetScissor(REG(a0, W3D_Context * context), REG(a1, W3D_Scissor * scissor), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_FlushFrame(REG(a0, W3D_Context * context), REG(a6, struct W3DBase *libbase));
LIBCALL W3D_Driver * LIB_W3D_TestMode(REG(d0, ULONG ModeID), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetChromaTestBounds(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(d0, ULONG rgba_lower), REG(d1, ULONG rgba_upper), REG(d2, ULONG mode), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ClearDrawRegion(REG(a0, W3D_Context * context), REG(d0, ULONG color), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriangleV(REG(a0, W3D_Context * context), REG(a1, W3D_TriangleV * triangle), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriFanV(REG(a0, W3D_Context * context), REG(a1, W3D_TrianglesV * triangles), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawTriStripV(REG(a0, W3D_Context * context), REG(a1, W3D_TrianglesV * triangles), REG(a6, struct W3DBase *libbase));
LIBCALL W3D_ScreenMode * LIB_W3D_GetScreenmodeList(REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_FreeScreenmodeList(REG(a0, W3D_ScreenMode * list), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_BestModeID(REG(a0, struct TagItem * tags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_VertexPointer(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, ULONG mode), REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_TexCoordPointer(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, int unit), REG(d2, int off_v), REG(d3, int off_w), REG(d4, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ColorPointer(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, ULONG format), REG(d2, ULONG mode), REG(d3, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_BindTexture(REG(a0, W3D_Context * context), REG(d0, ULONG tmu), REG(a1, W3D_Texture * texture), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawArray(REG(a0, W3D_Context * context), REG(d0, ULONG primitive), REG(d1, ULONG base), REG(d2, ULONG count), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_DrawElements(REG(a0, W3D_Context * context), REG(d0, ULONG primitive), REG(d1, ULONG type), REG(d2, ULONG count), REG(a1, void * indices), REG(a6, struct W3DBase *libbase));
LIBCALL void LIB_W3D_SetFrontFace(REG(a0, W3D_Context * context), REG(d0, ULONG direction), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetTextureBlend(REG(a0, W3D_Context * context), REG(a1, struct TagItem * tagList), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SecondaryColorPointer(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, ULONG format), REG(d2, ULONG mode), REG(d3, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_FogCoordPointer(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, ULONG mode), REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_InterleavedArray(REG(a0, W3D_Context * context), REG(a1, void * pointer), REG(d0, int stride), REG(d1, ULONG format), REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_ClearBuffers(REG(a0, W3D_Context * context), REG(a1, W3D_Color * color), REG(a2, W3D_Double * depth), REG(a3, ULONG * stencil), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetParameter(REG(a0, W3D_Context * context), REG(d0, ULONG target), REG(a1, void * param), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_PinTexture(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(d0, ULONG pinning), REG(a6, struct W3DBase *libbase));
LIBCALL ULONG LIB_W3D_SetDrawRegionTexture(REG(a0, W3D_Context * context), REG(a1, W3D_Texture * texture), REG(a2, W3D_Scissor * scissor), REG(a6, struct W3DBase *libbase));

ULONG W3D_GetDestFmt_all(void);

#endif
