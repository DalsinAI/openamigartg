| Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
| SPDX-License-Identifier: MIT
| libminigl.a: calls minigl.library's function at -30, which answers its table.
	.text
	.even
	.globl	_MiniGLGetDispatchTableLVO
_MiniGLGetDispatchTableLVO:
	move.l	_MiniGLBase,d0
	beq.s	1f
	move.l	a6,-(sp)
	move.l	d0,a6
	jsr	-30(a6)
	move.l	(sp)+,a6
1:	rts
