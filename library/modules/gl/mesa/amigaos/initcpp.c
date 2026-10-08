/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libnix's constructor runner (__initcpp, __exitcpp), done again for
 * programs built -fbaserel32 that link Mesa itself (test_gla,
 * OpenDemos.static: build.sh's PROGOBJS). The stove's libb32 one reaches
 * the end of its list through A4, but the linker puts the list with the
 * code, so in a -fbaserel32 program it starts from a wrong place: it runs
 * no constructor, or jumps into the code. Here the list is const, reached
 * by its address. (GL.module has the same in library/modules/common's
 * module_rt.c.) Linked ahead of libnix, it stands in for libnix's. */
typedef void (*initcpp_func)(void);
extern const initcpp_func __CTOR_LIST__[], __DTOR_LIST__[];

void __initcpp(void);
void __exitcpp(void);

/* Constructors last to first, destructors first to last, as libnix's. */
void __initcpp(void)
{
    const initcpp_func *p0 = __CTOR_LIST__ + 1, *p;
    for (p = p0; *p; p++) {
    }
    while (p > p0) (*--p)();
}

void __exitcpp(void)
{
    const initcpp_func *p = __DTOR_LIST__ + 1;
    while (*p) (*p++)();
}

/* libnix's ADD2INIT and ADD2EXIT (stabs.h), spelt with __asm__ as the
 * build's -std=c11 has no asm: the function, then its priority + 128. */
__asm__(".section .list___INIT_LIST__,\"aw\"\n\t.long ___initcpp\n\t.long 123\n\t.text");
__asm__(".section .list___EXIT_LIST__,\"aw\"\n\t.long ___exitcpp\n\t.long 123\n\t.text");
