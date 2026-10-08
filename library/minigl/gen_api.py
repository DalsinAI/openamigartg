#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# Makes minigl.library's interface files from library/minigl/minigl_api.txt
# (run by library/minigl/build.sh api; the results are kept in the tree):
#   include/libraries/minigl_dispatch.h  the table and the calls through it
#   include/mgl/minigl_calls.h           the calls' prototypes
#   library/minigl/mgl_slots.h           the library's entry points
#   library/minigl/mgl_table.c           the table the library hands out
#   library/minigl/mgl_static.c          the calls as functions (libmgl.a)
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
HEAD = ('/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).\n'
        ' * SPDX-License-Identifier: MIT\n'
        ' * Made by library/minigl/gen_api.py from library/minigl/minigl_api.txt. */\n')

slots, calls = [], []
for line in open(os.path.join(HERE, 'minigl_api.txt')):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    kind, rest = line.split(' ', 1)
    parts = [p.strip() for p in rest.split('|')]
    if kind == 'slot':
        slots.append(parts)
    elif kind == 'call':
        calls.append(parts)
    else:
        sys.exit('minigl_api.txt: ' + line)

if len(slots) != 205:
    sys.exit('minigl_api.txt: %d slots, 205 expected' % len(slots))
names = [s[1] for s in slots]
callnames = set(c[1] for c in calls)
def impl(n):
    """The library's function for a slot: LIB_ in front where a call has the slot's name."""
    return 'LIB_' + n if n in callnames else n

def ptr_ret(r):
    return r.replace(' ', '') in ('void*', 'constGLubyte*', 'GLUquadricObj*')

def decl(ret, name, params, fnptr=False):
    """A declaration; ret may end in '*'."""
    r = ret.rstrip()
    if fnptr:
        return '%s (*%s)(%s)' % (r, name, params)
    sep = '' if r.endswith('*') else ' '
    return '%s%s%s(%s)' % (r, sep, name, params)

def param_names(params):
    if params == 'void':
        return []
    out = []
    depth = 0
    cur = ''
    for ch in params:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    res = []
    for p in out:
        p = p.strip()
        m = re.search(r'\(\*\s*(\w+)\)', p)       # a function pointer parameter
        if m:
            res.append(m.group(1))
        else:
            res.append(re.findall(r'\w+', p)[-1])
    return res

def body(c, cur, disp):
    ret, name, params, how = c
    rv = '' if ret == 'void' else 'return '
    if how == 'delete':
        return ('    if (%s) {\n        GLcontext context = %s;\n        %s = (GLcontext)0;\n'
                '        %s->MGLDeleteContext(context);\n    }\n' % (cur, cur, cur, disp))
    if how.startswith('glutinit '):
        call = how.split(' ', 1)[1]
        return '    atexit(mgl_glut_release);\n    %s->%s;\n' % (disp, call)
    if how.startswith('guard '):
        call = how.split(' ', 1)[1].replace('CTX', cur)
        return '    if (%s) %s->%s;\n' % (cur, disp, call)
    if how.startswith('create '):
        call = how.split(' ', 1)[1]
        return ('    GLcontext context = (GLcontext)%s->%s;\n    if (context) %s = context;\n'
                '    return (void *)context;\n' % (disp, call, cur))
    call = how.replace('CTX', cur)
    slot = call.split('(')[0]
    if slot not in names:
        sys.exit('minigl_api.txt: %s calls %s, which is not a slot' % (name, slot))
    return '    %s%s->%s;\n' % (rv, disp, call)

GLUT_RELEASE = '''/* glutInit registers this with atexit: a GLUT program usually leaves by
 * exit() from its keyboard function, and its context is let go here. */
%sextern int atexit(void (*func)(void));
%svoid mgl_glut_release(void)
{
    if (%s && %s) {
        GLcontext context = %s;
        %s = (GLcontext)0;
        %s->MGLDeleteContext(context);
    }
}
'''

# ---- include/libraries/minigl_dispatch.h --------------------------------------------
cur = '(*MiniGLDispatch->currentContext)'
o = [HEAD, '''/*
 * minigl.library's table. A program opens the library (version 29 or later)
 * and calls its one function, at -30, which answers this table; every gl*,
 * glu*, glut* and mgl* call goes through it. MiniGLOpen() in libminigl.a does
 * the opening and checks the table: abiVersion must equal
 * MINIGL_DISPATCH_ABI_VERSION, and structSize must be at least the size this
 * header knows.
 *
 * Including <proto/minigl.h> turns every call name into an inline that goes
 * through the table. A program built with libmgl.a instead calls functions
 * of the same names, which do the same.
 */
#ifndef LIBRARIES_MINIGL_DISPATCH_H
#define LIBRARIES_MINIGL_DISPATCH_H

#include <exec/types.h>
#include <mgl/gl.h>
#include <mgl/glut.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINIGL_DISPATCH_ABI_VERSION   5UL
#define MINIGL_BACKEND_FLAG_STUB      (1UL << 0)
#define MINIGL_BACKEND_FLAG_CLASSIC   (1UL << 1)
#define MINIGL_BACKEND_FLAG_PISTORM3D (1UL << 2)
#define MINIGL_BACKEND_FLAG_OPENRTG   (1UL << 16)   /* OpenRTG's minigl.library: OpenGPU or the 68k */

typedef struct MGLDispatchTable {
    ULONG abiVersion;               /* MINIGL_DISPATCH_ABI_VERSION */
    ULONG structSize;               /* sizeof (MGLDispatchTable) as the library has it */
    ULONG backendFlags;             /* MINIGL_BACKEND_FLAG_ */
    ULONG reserved;
    GLcontext *currentContext;      /* the current context, which the calls below pass */
''']
for i, (r, n, p) in enumerate(slots):
    o.append('    %s;%s\n' % (decl(r, n, p, True), '' if i % 10 else '      /* %d */' % (i + 1)))
o.append('''} MGLDispatchTable;

extern const MGLDispatchTable *MiniGLDispatch;

#ifndef MINIGL_LIBRARY_BUILD

''')
o.append(GLUT_RELEASE % ('', 'static ', 'MiniGLDispatch', cur, cur, cur, 'MiniGLDispatch'))
for c in calls:
    ret, name, params, how = c
    o.append('\nstatic __inline__ %s\n{\n%s}\n#undef %s\n#define %s MGLD_%s\n'
             % (decl(ret, 'MGLD_' + name, params), body(c, cur, 'MiniGLDispatch'), name, name, name))
o.append('''
#endif /* MINIGL_LIBRARY_BUILD */

#ifdef __cplusplus
}
#endif

#endif
''')
open(os.path.join(ROOT, 'include/libraries/minigl_dispatch.h'), 'w').write(''.join(o))

# ---- include/mgl/minigl_calls.h -----------------------------------------------------
o = [HEAD, '''/* The calls programs make, as functions: libmgl.a has them. With
 * <proto/minigl.h> they are inlines through the table instead
 * (libraries/minigl_dispatch.h). Included by <mgl/gl.h>. */
#ifndef MGL_MINIGL_CALLS_H
#define MGL_MINIGL_CALLS_H

#ifdef __cplusplus
extern "C" {
#endif

GLboolean MGLInit(void);            /* libmgl.a: opens minigl.library (MiniGLOpen) */
void MGLTerm(void);                 /* libmgl.a: closes it */

''']
for ret, name, params, how in sorted(calls, key=lambda c: c[1].lower()):
    o.append('%s;\n' % decl(ret, name, params))
o.append('''
#ifdef __cplusplus
}
#endif

#endif
''')
open(os.path.join(ROOT, 'include/mgl/minigl_calls.h'), 'w').write(''.join(o))

# ---- library/minigl/mgl_slots.h ------------------------------------------------------
o = [HEAD, '/* The library\'s entry points, the table\'s order. */\n#ifndef MGL_SLOTS_H\n#define MGL_SLOTS_H\n\n']
for r, n, p in slots:
    o.append('%s;\n' % decl(r, impl(n), p))
o.append('\n#endif\n')
open(os.path.join(HERE, 'mgl_slots.h'), 'w').write(''.join(o))

# ---- library/minigl/mgl_table.c ------------------------------------------------------
o = [HEAD, '''/* The table minigl.library hands out (dispatch ABI 5). Positional: the
 * order is minigl_api.txt's. */
#include "mgl_internal.h"

const MGLDispatchTable mgl_table = {
    MINIGL_DISPATCH_ABI_VERSION,
    sizeof (MGLDispatchTable),
    MINIGL_BACKEND_FLAG_OPENRTG,
    0,
    &mgl_current,
''']
for i, (r, n, p) in enumerate(slots):
    o.append('    %s%s\n' % (impl(n), ',' if i < len(slots) - 1 else ''))
o.append('};\n\nconst char mgl_table_check[sizeof (MGLDispatchTable) == 840 ? 1 : -1] = { 0 };\n')
open(os.path.join(HERE, 'mgl_table.c'), 'w').write(''.join(o))

# ---- library/minigl/mgl_static.c -----------------------------------------------------
o = [HEAD, '''/* libmgl.a: every call as a function, for programs written for the
 * linked-in MiniGL. They open minigl.library with MGLInit() and close it with
 * MGLTerm(), and are otherwise unchanged. */
#define MINIGL_LIBRARY_BUILD
#include <proto/minigl.h>

GLboolean MGLInit(void) { return MiniGLOpen() ? GL_TRUE : GL_FALSE; }
void MGLTerm(void) { MiniGLClose(); }

''']
o.append(GLUT_RELEASE % ('', 'static ', 'MiniGLDispatch', cur, cur, cur, 'MiniGLDispatch'))
for c in calls:
    ret, name, params, how = c
    o.append('\n%s\n{\n%s}\n' % (decl(ret, name, params), body(c, cur, 'MiniGLDispatch')))
open(os.path.join(HERE, 'mgl_static.c'), 'w').write(''.join(o))
print('minigl_api.txt: %d slots, %d calls' % (len(slots), len(calls)))
