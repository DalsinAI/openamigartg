| Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
| SPDX-License-Identifier: MIT
|
| A module's first code, and the heads of libnix's set lists
| (include/opengpu/module.h). It must be the first object on the link line.
| OGPU_ModuleOpen (or a stub, on an older opengpu.library) LoadSegs the
| module and calls its first code as
|   struct OGPUModuleTable *entry(struct OGPUModuleArgs *args)
| with the C calling convention (the argument on the stack). It jumps to the
| module's ogpu_module_entry. libnix's own startup (ncrt0) isn't linked: a
| module isn't a program, so these heads stand in for its.
| (SDL2.module's module_start.s is the same, with its own entry's name;
| it doesn't name __initcpp yet.)

	.text
	.globl	_ogpu_module_start
_ogpu_module_start:
	jmp	_ogpu_module_entry

| libnix's startup links two of its init list's members in by naming them:
| __initlibraries opens the libraries libnix's own code calls (the maths
| libraries, among others), and __initcpp runs the constructors (C++ statics,
| __attribute__((constructor))), with __exitcpp the destructors on close.
| Without them their bases stay unset and no constructor runs.
	.data
	.long	___initlibraries
	.long	___initcpp

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
