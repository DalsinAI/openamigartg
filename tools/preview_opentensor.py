#!/usr/bin/env python3
"""Render Act I from the actual OpenTensor sources with host SDL/GL.

Requires cc, SDL2/GL development files and Pillow. This is offline image
validation; it does not test the Amiga display path or AHI. Run from any cwd.
"""
from pathlib import Path
import subprocess,os
root=Path(__file__).resolve().parents[1]
p=root/'demos/opentensor/src';out=root/'build/polish-preview'
out.mkdir(parents=True,exist_ok=True)
a=(p/'opentensor.c.part1').read_text();b=(p/'opentensor.c.part2').read_text();c=(p/'opentensor.c.part3').read_text()
header='''#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef unsigned char UBYTE;
typedef uint32_t ULONG;
#define MEMF_ANY 0
#define MEMF_CLEAR 0
#define AllocVec(n,f) calloc(1,n)
#define FreeVec(p) free(p)
#define OT_PI 3.14159265358979323846f
#define OT_TAU 6.28318530717958647692f
#define OT_MAX(a,b) ((a)>(b)?(a):(b))
#define OT_MIN(a,b) ((a)<(b)?(a):(b))
struct Mesh {GLfloat *v,*c; GLsizei count; GLenum mode;};
struct Demo {int width,height,current_scene,breakout_active,windowed; float fps; char driver_name[16]; struct {ULONG underruns;} audio; struct Mesh ground,ball,stars,tunnel;};
'''
source=header+a[a.index('static float ot_absf'):a.index('static double ot_now')]+b[b.index('static int ot_mesh_alloc'):]+c
source+='''\nstatic const char *ot_scene_name(int s){return "PREVIEW";}
int main(int argc,char **argv){
 struct Demo d={0}; unsigned char pixels[960*540*3]; FILE *f; int y; GLenum err;
 SDL_SetHint(SDL_HINT_VIDEODRIVER,"offscreen");
 if(SDL_Init(SDL_INIT_VIDEO)) {fprintf(stderr,"%s",SDL_GetError());return 1;}
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
 SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
 SDL_Window *w=SDL_CreateWindow("offline preview",0,0,960,540,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 if(!w){fprintf(stderr,"%s",SDL_GetError());return 2;}
 SDL_GLContext ctx=SDL_GL_CreateContext(w); if(!ctx){fprintf(stderr,"%s",SDL_GetError());return 3;}
 d.width=960;d.height=540;
 if(!ot_build_ground(&d.ground)||!ot_build_ball(&d.ball)||!ot_build_stars(&d.stars)||!ot_build_tunnel(&d.tunnel))return 4;
 glEnable(GL_DEPTH_TEST);glClearColor(0.008,0.012,0.028,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 ot_set_perspective(&d);ot_studio_lights();ot_draw_sky();
 switch(atoi(argv[1])){case 0:ot_scene_intro(&d,6);break;case 1:ot_scene_field(&d,5);break;case 2:ot_scene_boing(&d,2.3);break;default:ot_scene_final(&d,5);break;}
 glFinish();glReadPixels(0,0,960,540,GL_RGB,GL_UNSIGNED_BYTE,pixels);
 err=glGetError();if(err){fprintf(stderr,"GL error %x",err);return 5;}
 f=fopen(argv[2],"wb");fprintf(f,"P6\\n960 540\\n255\\n");for(y=539;y>=0;--y)fwrite(pixels+y*960*3,1,960*3,f);fclose(f);
 printf("%s\\n",glGetString(GL_RENDERER));
 SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
'''
(out/'preview.c').write_text(source)
subprocess.run(['cc','-O2',str(out/'preview.c'),'-o',str(out/'preview'),'-lSDL2','-lGL','-lm'],check=True)
for scene in range(4):
 subprocess.run([str(out/'preview'),str(scene),str(out/f'scene-{scene}.ppm')],check=True)
 from PIL import Image
 Image.open(out/f'scene-{scene}.ppm').save(out/f'scene-{scene}.png')
