/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A module's first code, and the heads of libnix's set lists
 * (include/opengpu/module.h). It must be the first object on the link line.
 *
 * Built -fbaserel32 (a shared module: code loaded once, data per program),
 * the first code is a BRA.W over the module's header (struct
 * OGPUModuleHeader). opengpu.library reads the header, makes the opening
 * program's copy of the data and calls the header's entry with A4 on that
 * copy. Anything that calls the first code itself (a stub on an
 * opengpu.library older than 0.5, or 0.5 itself) lands in module_self,
 * which sets A4 on the data LoadSeg loaded: that load is then the
 * program's own copy, as in step 1.
 *
 * Built without -fbaserel32 (a module for each program, step 1), the first
 * code jumps to the entry, and there is no header.
 *
 * The entry is plain C, the argument on the stack and the result in D0:
 *   struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
 * libnix's own startup (ncrt0) isn't linked: a module isn't a program, so
 * the list heads below stand in for its.
 * (C comments only here: the file goes through the C preprocessor.) */

	.text
	.globl	_ogpu_module_start
_ogpu_module_start:
#ifdef __baserel32__
	bra.w	module_self

/* struct OGPUModuleHeader, at the first code + 4. */
	.long	0x4F47534D		/* OGPU_MODULE_SHARED_MAGIC, "OGSM" */
	.long	_ogpu_module_entry	/* entry, called with A4 set */
	.long	___a4_init		/* A4 for the data as loaded: its start + 0x7ffe */
	.long	___data_size		/* data and BSS, bytes */
	.long	___datadata_relocs	/* a count, then offsets into the data */

/* The first code called directly: A4 on the loaded data, the caller's back after. */
module_self:
	move.l	%a4,-(%sp)
	lea	___a4_init,%a4
	move.l	8(%sp),-(%sp)
	jsr	_ogpu_module_entry
	addq.l	#4,%sp
	move.l	(%sp)+,%a4
	rts

/* long ogpu_module_callout_a4(APTR a4, APTR fn, long a, long b, long c, long d)
 * fn(a, b, c, d) with A4 set to a4, and the module's A4 back afterwards
 * (module_rt.h: a call into the program, or a new thread's start). Results
 * in D0 and D1 come back as they are. */
	.globl	_ogpu_module_callout_a4
_ogpu_module_callout_a4:
	move.l	%a4,-(%sp)		/* 0 a4 save, 4 return, 8 a4, 12 fn, 16 a, 20 b, 24 c, 28 d */
	move.l	12(%sp),%a1
	move.l	28(%sp),-(%sp)		/* d */
	move.l	28(%sp),-(%sp)		/* c */
	move.l	28(%sp),-(%sp)		/* b */
	move.l	28(%sp),-(%sp)		/* a */
	move.l	24(%sp),%a4
	jsr	(%a1)
	lea	16(%sp),%sp
	move.l	(%sp)+,%a4
	rts
#else
	jmp	_ogpu_module_entry
#endif

/* libnix's startup links two of its init list's members in by naming them:
 * __initlibraries opens the libraries libnix's own code calls (the maths
 * libraries, among others), and __initcpp runs the constructors (C++ statics,
 * __attribute__((constructor))), with __exitcpp the destructors on close.
 * Without them their bases stay unset and no constructor runs. */
	.data
	.long	___initlibraries
	.long	___initcpp

/* Each list is its head (a zero long), the elements the linker gathers from
 * libnix's objects (function, priority), and the next list's head (or
 * .end_of_lists) as its end. The linker puts the lists with the code, so C
 * reaches them as const (module_rt.c), never through A4. */
	.section .list___INIT_LIST__,"aw"
	.globl	___INIT_LIST__
___INIT_LIST__:
	.long	0
	.section .list___EXIT_LIST__,"aw"
	.globl	___EXIT_LIST__
___EXIT_LIST__:
	.long	0
	.section .list___CTOR_LIST__,"aw"
	.globl	___CTOR_LIST__
___CTOR_LIST__:
	.long	0
	.section .list___DTOR_LIST__,"aw"
	.globl	___DTOR_LIST__
___DTOR_LIST__:
	.long	0
	.section .dlist___LIB_LIST__,"aw"
	.globl	___LIB_LIST__
___LIB_LIST__:
	.long	0
	.section .end_of_lists,"aw"
	.long	0
	.section .end_of_dlists,"aw"
	.long	0
