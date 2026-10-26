#!/usr/bin/env python3
"""Run the patched Amiga system-cursor factory with SDL allocation/list shims."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
patch = (ROOT / 'library/modules/sdl3/patches/amigaos3/0006-system-cursor-ownership.patch').read_text()
added = '\n'.join(line[1:] for line in patch.splitlines()
                  if line.startswith((' ', '+')) and not line.startswith('+++'))
factory = re.search(r'static SDL_Cursor \*OS3_CreateSystemCursor\(SDL_SystemCursor id\)\n\{.*?\n\}', added, re.S).group(0)
shim = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef int SDL_SystemCursor;
typedef struct { void *pixels; int pitch; } SDL_Surface;
typedef struct SDL_Cursor { struct SDL_Cursor *next; int id; } SDL_Cursor;
#define DEFAULT_CWIDTH 8
#define DEFAULT_CHEIGHT 2
#define DEFAULT_CHOTX 1
#define DEFAULT_CHOTY 1
#define SDL_PIXELFORMAT_ARGB8888 1
static const Uint8 default_cdata[]={0xa0,0x50}, default_cmask[]={0xc0,0xc0};
static int fail_surface, fail_cursor, surfaces, destroyed, calls;
static SDL_Cursor *list;
static SDL_Surface *SDL_CreateSurface(int w,int h,int format) {
    assert(w==8 && h==2 && format==1);
    if (fail_surface) return NULL;
    SDL_Surface *s=calloc(1,sizeof(*s));
    s->pitch=40; s->pixels=calloc(2,40); surfaces++; return s;
}
static void SDL_DestroySurface(SDL_Surface *s) { destroyed++; free(s->pixels); free(s); }
static SDL_Cursor *OS3_CreateCursor(SDL_Surface *s,int x,int y) {
    assert(x==1 && y==1);
    Uint32 *a=s->pixels, *b=(Uint32 *)((Uint8 *)s->pixels+s->pitch);
    assert(a[0]==0xff000000 && a[1]==0xffffffff && a[2]==0xff000000 && a[3]==0);
    assert(b[0]==0xffffffff && b[1]==0xff000000 && b[2]==0 && b[3]==0xff000000);
    calls++;
    if (fail_cursor) return NULL;
    SDL_Cursor *c=calloc(1,sizeof(*c)); c->id=calls; return c;
}
'''
checks = r'''
/* The public SDL entry owns list insertion; the backend must not insert. */
static SDL_Cursor *public_system_cursor(void) {
    SDL_Cursor *c=OS3_CreateSystemCursor(0);
    if (c) { c->next=list; list=c; }
    return c;
}
int main(void) {
    SDL_Cursor *old=calloc(1,sizeof(*old)); list=old;
    SDL_Cursor *a=public_system_cursor(), *b=public_system_cursor(), *c=public_system_cursor();
    assert(c && b && a && list==c && c->next==b && b->next==a && a->next==old && old->next==NULL);
    /* Selecting an older cursor terminates rather than circling on the head. */
    SDL_Cursor *p=list; int steps=0;
    while (p && p!=old && steps<10) { p=p->next; steps++; }
    assert(p==old && steps==3);
    fail_surface=1; assert(public_system_cursor()==NULL && list==c); fail_surface=0;
    fail_cursor=1; assert(public_system_cursor()==NULL && list==c);
    assert(surfaces==destroyed && calls==4);
    while (list) { p=list->next; free(list); list=p; }
    puts("PASS system cursor: single insertion, older cursor reachable, bitmap/pitch/hotspot, allocation failure cleanup");
}
'''
with tempfile.TemporaryDirectory() as d:
    src=Path(d)/'cursor.c'; exe=Path(d)/'cursor'
    src.write_text(shim + factory + checks)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)], check=True)
    subprocess.run([str(exe)],check=True)
