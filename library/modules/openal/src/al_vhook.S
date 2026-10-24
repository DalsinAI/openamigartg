/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The entry of Voices' AHI hooks (al_ahi_voices.c). AHI calls a hook at
 * interrupt time, through utility.library's CallHookPkt: A0 the hook, A2
 * the object, A1 the message. In OpenAL.module (-fbaserel32) the module's
 * data is the program's copy, reached through A4, which an interrupt
 * doesn't have: the hook's h_Data holds it. This sets A4 from h_Data, calls
 * h_SubEntry(hook, object, message) as a C function, and gives A4 back.
 * Built into libopenal_static.a too, where h_Data is 0 and A4 is just a
 * register kept. */
	.text
	.even
	.globl	_al_vhook_stub
_al_vhook_stub:
	move.l	%a4,-(%sp)
	move.l	%a1,-(%sp)		/* the message */
	move.l	%a2,-(%sp)		/* the object */
	move.l	%a0,-(%sp)		/* the hook */
	move.l	16(%a0),%a4		/* h_Data */
	move.l	12(%a0),%a0		/* h_SubEntry */
	jsr	(%a0)
	lea	12(%sp),%sp
	move.l	(%sp)+,%a4
	rts
