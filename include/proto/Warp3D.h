/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Made by sfdc from library/warp3d/warp3d_lib.sfd (library/warp3d/build.sh headers). */
/* Automatically generated header (sfdc 1.12)! Do not edit! */

#ifndef PROTO_WARP3D_H
#define PROTO_WARP3D_H

#include <clib/Warp3D_protos.h>

#if defined(_CONST_BASES)
# ifndef __CONSTLIBBASEDECL__
# define __CONSTLIBBASEDECL__ const
# endif /* __CONSTLIBBASEDECL__ */
# ifndef __SEGMENTLIBBASEDECL__
# define __SEGMENTLIBBASEDECL__  __attribute__((__section__(".data")))
# endif /* __SEGMENTLIBBASEDECL__ */
#endif /* _CONST_BASES */
#ifdef __amigaos4__
# include <interfaces/Warp3D.h>
# ifndef __NOGLOBALIFACE__
   extern struct Warp3DIFace *IWarp3D;
# endif /* __NOGLOBALIFACE__*/
#endif /* !__amigaos4__ */
#ifndef __NOLIBBASE__
  extern struct Library *
# ifdef __CONSTLIBBASEDECL__
   __CONSTLIBBASEDECL__
# endif /* __CONSTLIBBASEDECL__ */
  Warp3DBase
# ifdef __SEGMENTLIBBASEDECL__
 __SEGMENTLIBBASEDECL__
# endif /* __SEGMENTLIBBASEDECL__ */
;
#endif /* !__NOLIBBASE__ */

#ifndef _NO_INLINE
# if defined(__GNUC__)
#  ifdef __AROS__
#   include <defines/Warp3D.h>
#  else
#   include <inline/Warp3D.h>
#  endif
# else
#  include <pragmas/Warp3D_pragmas.h>
# endif
#endif /* _NO_INLINE */

#endif /* !PROTO_WARP3D_H */
