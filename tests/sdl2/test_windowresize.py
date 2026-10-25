"""Host contract for the exact resize callback; native geometry needs a lab."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'library/modules/sdl2/patches/amigaos3/0010-window-resize.patch'

class ResizeContract(unittest.TestCase):
    def test_callback(self):
        added = '\n'.join(s[1:] for s in PATCH.read_text().splitlines()
                          if s.startswith('+') and not s.startswith('+++'))
        start = added.index('void OS3_SetWindowSize(')
        end = added.index('\n}', start) + 2
        callback = added[start:end]
        support = r'''
#include <assert.h>
#include <stddef.h>
typedef unsigned short UWORD;
#define _THIS void *unused
struct Window { int GZZWidth, GZZHeight; };
typedef struct { struct Window *window; int is_fullscreen; } OS3_WindowData;
typedef struct { void *driverdata; int w, h; } SDL_Window;
static int limits, resize, dx, dy;
static void WindowLimits(struct Window *w,int a,int b,UWORD c,UWORD d) {
 assert(w && !a && !b && c==(UWORD)-1 && d==(UWORD)-1); ++limits;
}
static void SizeWindow(struct Window *w,int x,int y) {
 assert(w); ++resize; dx=x; dy=y;
}
'''
        main = r'''
int main(void) {
 struct Window native={320,200}; OS3_WindowData data={&native,0};
 SDL_Window win={&data,640,400};
 OS3_SetWindowSize(NULL,&win);
 assert(limits==1 && resize==1 && dx==320 && dy==200);
 native.GZZWidth=640; native.GZZHeight=400; win.w=320; win.h=200;
 OS3_SetWindowSize(NULL,&win);
 assert(limits==2 && resize==2 && dx==-320 && dy==-200);
 data.is_fullscreen=1; win.w=800; win.h=600;
 OS3_SetWindowSize(NULL,&win);
 assert(limits==2 && resize==2 && win.w==640 && win.h==400);
 data.window=NULL; OS3_SetWindowSize(NULL,&win);
 win.driverdata=NULL; OS3_SetWindowSize(NULL,&win);
 assert(limits==2 && resize==2);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as t:
            src=Path(t)/'resize.c'; exe=Path(t)/'resize'
            src.write_text(support + callback + main)
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                            '-Wno-unused-parameter',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == '__main__': unittest.main()
