/* Native SDL logical/Intuition resize control. MIT, AmigaChrome Team 2026. */
#include <SDL.h>
#include <proto/intuition.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
static int native_size(int *w,int *h) {
 struct Screen*s=LockPubScreen(NULL);if(!s)return 0;int found=0;
 Forbid();for(struct Window*i=s->FirstWindow;i;i=i->NextWindow)if(i->Title&&!strcmp((const char*)i->Title,"SDL resize probe")){*w=i->GZZWidth;*h=i->GZZHeight;found=1;break;}Permit();UnlockPubScreen(NULL,s);return found;
}
int main(void){int w=0,h=0,nw=0,nh=0,rc=20;SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");if(SDL_Init(SDL_INIT_VIDEO))goto done;
 SDL_Window*win=SDL_CreateWindow("SDL resize probe",100,100,320,200,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);if(!win)goto done;
 SDL_SetWindowSize(win,640,400);SDL_ShowWindow(win);SDL_Delay(300);SDL_PumpEvents();SDL_GetWindowSize(win,&w,&h);int found=native_size(&nw,&nh);printf("logical %dx%d native %dx%d found %d\n",w,h,nw,nh,found);
 SDL_Renderer*r=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);if(r){SDL_SetRenderDrawColor(r,255,0,0,255);SDL_RenderClear(r);SDL_SetRenderDrawColor(r,0,255,0,255);SDL_Rect edge={620,380,20,20};SDL_RenderFillRect(r,&edge);SDL_RenderPresent(r);SDL_Delay(500);SDL_DestroyRenderer(r);}
 rc=found&&w==640&&h==400&&nw==640&&nh==400?0:20;SDL_DestroyWindow(win);
 done:printf("resize probe RC %d SDL %s\n",rc,SDL_GetError());SDL_Quit();return rc;
}
