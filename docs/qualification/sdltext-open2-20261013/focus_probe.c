#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <stdio.h>
struct IntuitionBase *IntuitionBase;
int main(void) {
 struct Window *w;
 IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",37);
 if(!IntuitionBase) return 20;
 Delay(400);
 w=OpenWindowTags(NULL,WA_Left,900,WA_Top,420,WA_Width,340,WA_Height,130,
 WA_Title,(ULONG)"SDL focus isolation probe",WA_Flags,WFLG_ACTIVATE|WFLG_DRAGBAR|WFLG_DEPTHGADGET,TAG_DONE);
 if(!w) {CloseLibrary((struct Library *)IntuitionBase);return 20;}
 puts("FOCUS_PROBE_OPEN");fflush(stdout);
 Delay(400);
 CloseWindow(w);CloseLibrary((struct Library *)IntuitionBase);
 puts("FOCUS_PROBE_CLOSED");fflush(stdout);return 0;
}
