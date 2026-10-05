/* MouseProbe: intuition's idea of the mouse and the front screen's view, once. Not shipped. MIT, Dalsin Limited. */
#include <exec/types.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/monitor.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
int main(void)
{
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    struct Screen *s = ib->FirstScreen;
    struct View *v = GfxBase->ActiView;
    struct ViewExtra *ve = v ? (struct ViewExtra *)GfxLookUp(v) : NULL;
    struct ViewPortExtra *vpe = s && s->ViewPort.ColorMap ? s->ViewPort.ColorMap->cm_vpe : NULL;
    Printf((STRPTR)"IBase mouse %ld,%ld; screen mouse %ld,%ld; vp %ld,%ld %ldx%ld modes %04lx\n",
           (LONG)ib->MouseX, (LONG)ib->MouseY, s ? (LONG)s->MouseX : 0, s ? (LONG)s->MouseY : 0,
           s ? (LONG)s->ViewPort.DxOffset : 0, s ? (LONG)s->ViewPort.DyOffset : 0, s ? (LONG)s->ViewPort.DWidth : 0, s ? (LONG)s->ViewPort.DHeight : 0,
           s ? (LONG)s->ViewPort.Modes : 0);
    Printf((STRPTR)"  ActiView %lx (ViewLord %lx) dx %ld dy %ld modes %04lx; ViewExtra %lx monitor %lx %s ratio %ld/%ld\n",
           (ULONG)v, (ULONG)&ib->ViewLord, v ? (LONG)v->DxOffset : 0, v ? (LONG)v->DyOffset : 0, v ? (LONG)v->Modes : 0,
           (ULONG)ve, ve ? (ULONG)ve->Monitor : 0, ve && ve->Monitor && ve->Monitor->ms_Node.xln_Name ? (LONG)ve->Monitor->ms_Node.xln_Name : (LONG)"-",
           ve && ve->Monitor ? ve->Monitor->ratioh : 0, ve && ve->Monitor ? ve->Monitor->ratiov : 0);
    if (vpe) Printf((STRPTR)"  vp extra: DisplayClip %ld,%ld-%ld,%ld, flags %lx\n",
                    (LONG)vpe->DisplayClip.MinX, (LONG)vpe->DisplayClip.MinY, (LONG)vpe->DisplayClip.MaxX, (LONG)vpe->DisplayClip.MaxY, (ULONG)vpe->Flags);
    else Printf((STRPTR)"  no vp extra\n");
    if (s) {
        ULONG id = GetVPModeID(&s->ViewPort);
        struct Rectangle r;
        for (int k = 1; k <= 4; k++) {
            LONG ok = QueryOverscan(id, &r, k);
            Printf((STRPTR)"  QueryOverscan(%lx, %ld): %ld -> %ld,%ld-%ld,%ld\n", id, (LONG)k, ok, (LONG)r.MinX, (LONG)r.MinY, (LONG)r.MaxX, (LONG)r.MaxY);
        }
        Printf((STRPTR)"  screen mode %08lx, view's first vp %lx (screen's %lx)\n", id, v ? (ULONG)v->ViewPort : 0, (ULONG)&s->ViewPort);
    }
    for (struct Screen *t = ib->FirstScreen; t; t = t->NextScreen)
        Printf((STRPTR)"  screen %lx '%s' %ldx%ld; vp %ld,%ld %ldx%ld modes %04lx id %08lx; bitmap %lx\n", (ULONG)t,
               t->DefaultTitle ? (LONG)t->DefaultTitle : (LONG)"-", (LONG)t->Width, (LONG)t->Height,
               (LONG)t->ViewPort.DxOffset, (LONG)t->ViewPort.DyOffset, (LONG)t->ViewPort.DWidth, (LONG)t->ViewPort.DHeight,
               (LONG)t->ViewPort.Modes, GetVPModeID(&t->ViewPort), t->ViewPort.RasInfo ? (ULONG)t->ViewPort.RasInfo->BitMap : 0);
    return 0;
}
