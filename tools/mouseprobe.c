/* MouseProbe: intuition's idea of the mouse, once. Not shipped. MIT, Dalsin Limited. */
#include <exec/types.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
int main(void)
{
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    struct Screen *s = ib->FirstScreen;
    Printf((STRPTR)"IBase mouse %ld,%ld; front screen %s mouse %ld,%ld, vp offset %ld,%ld, %ld x %ld; ViewLord dx %ld dy %ld\n",
           (LONG)ib->MouseX, (LONG)ib->MouseY, s ? (LONG)s->Title : (LONG)"-", s ? (LONG)s->MouseX : 0, s ? (LONG)s->MouseY : 0,
           s ? (LONG)s->ViewPort.DxOffset : 0, s ? (LONG)s->ViewPort.DyOffset : 0, s ? (LONG)s->ViewPort.DWidth : 0, s ? (LONG)s->ViewPort.DHeight : 0,
           (LONG)ib->ViewLord.DxOffset, (LONG)ib->ViewLord.DyOffset);
    return 0;
}
