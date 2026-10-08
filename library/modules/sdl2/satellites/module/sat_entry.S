/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * void ogpu_sat_callin3(struct SatCallin *c, long b, long d) (sat_rt.h):
 * SDL's call into a satellite module. SDL2.module calls it with the
 * program's A4; it sets the satellite copy's (c->a4), calls
 * c->fn(c->arg, b, d) and puts the caller's A4 back.
 * (C comments only: the file goes through the C preprocessor.) */
	.text
	.globl	_ogpu_sat_callin3
_ogpu_sat_callin3:
	move.l	%a4,-(%sp)		/* 0 a4 save, 4 return, 8 c, 12 b, 16 d */
	move.l	8(%sp),%a0
	move.l	16(%sp),-(%sp)		/* d */
	move.l	16(%sp),-(%sp)		/* b */
	move.l	8(%a0),-(%sp)		/* c->arg */
	move.l	4(%a0),%a1		/* c->fn */
	move.l	(%a0),%a4		/* c->a4 */
	jsr	(%a1)
	lea	12(%sp),%sp
	move.l	(%sp)+,%a4
	rts
