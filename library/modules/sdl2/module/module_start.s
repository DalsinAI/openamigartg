| Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
| SPDX-License-Identifier: MIT
|
| SDL2.module's first code, and the heads of libnix's set lists.
| The module loader (OGPU_ModuleOpen, or the link stub until the library has
| it) LoadSegs the module and calls its first code as
|   struct SDL2ModuleTable *entry(struct SDL2ModuleArgs *args)
| with the C calling convention (the argument on the stack). It jumps to
| SDL2Module_Entry in sdl2_module.c. libnix's own startup (ncrt0) isn't
| linked: the module isn't a program, so these heads stand in for its.

	.text
	.globl	_SDL2Module_Start
_SDL2Module_Start:
	jmp	_SDL2Module_Entry

| libnix's startup links __initlibraries in by naming it: it opens the
| libraries libnix's own code calls (the maths libraries, among others) when
| the init list runs. Without it their bases stay unset.
	.data
	.long	___initlibraries

| Each list is its head (a zero long), the elements the linker gathers from
| libnix's objects (function, priority), and the next list's head (or
| .end_of_lists) as its end.
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
