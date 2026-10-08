/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * C:Warp3DPrefs: OpenRTG Warp3D's settings, the same ENV:Warp3D/ variables
 * OpenPrefs' Acceleration page writes, and Wazp3D's carried over.
 *   Warp3DPrefs                         shows the settings
 *   Warp3DPrefs FOG=OFF SAVE            sets one (SAVE: in ENVARC: too)
 *   Warp3DPrefs FROMWAZP3D SAVE         takes Wazp3D's settings (ENVARC:Wazp3D.cfg)
 * The installer runs the last one when it finds Wazp3D; Wazp3D's own files
 * stay where they are. Settings:
 *   DRIVER      AUTO (the host's GPU when there is one), CPU or OPENGPU
 *   ZBUFFER     16 or 32
 *   FOG, PERSPECTIVE, FILTERING, LIGHTING   ON or OFF: OFF draws faster and
 *               plainer, as Wazp3D's options of the same names did. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static const char version[] __attribute__((used)) = "$VER: Warp3DPrefs 1.0 (8.10.2026) OpenRTG Warp3D, Dalsin Limited";

static const char *names[] = { "Driver", "ZBuffer", "Fog", "Perspective", "Filtering", "Lighting" };
static const char *dflt[] = { "Auto", "16", "1", "1", "1", "1" };

static void set(const char *name, const char *value, int save)
{
    char var[40];
    strcpy(var, "Warp3D/");
    strcat(var, name);
    SetVar((STRPTR)var, (STRPTR)value, -1, GVF_GLOBAL_ONLY | (save ? GVF_SAVE_VAR : 0));
}

static const char *onoff(const char *a)
{
    if (!a) return 0;
    if (!stricmp(a, "ON") || !strcmp(a, "1") || !stricmp(a, "YES")) return "1";
    if (!stricmp(a, "OFF") || !strcmp(a, "0") || !stricmp(a, "NO")) return "0";
    return 0;
}

/* Wazp3D's settings file: "WAZP3D", 0, its version (52, 1), its build, then
 * one digit a setting in the order of Wazp3D's settings window. Those that
 * have a match here: PerspMode (15), TexMode (16), UseFog (17),
 * SmoothTextures (6), UseFiltering (19). */
static int from_wazp3d(int save)
{
    UBYTE cfg[100];
    LONG n = 0;
    BPTR f = Open((STRPTR)"ENVARC:Wazp3D.cfg", MODE_OLDFILE);
    if (!f) f = Open((STRPTR)"ENV:Wazp3D.cfg", MODE_OLDFILE);
    if (!f) { printf("Warp3DPrefs: no Wazp3D settings (ENVARC:Wazp3D.cfg); Wazp3D's defaults are taken\n"); }
    else { n = Read(f, cfg, sizeof cfg); Close(f); }
    if (n < 10 + 20 || memcmp(cfg, "WAZP3D", 7)) {
        /* Wazp3D's 68k defaults: no fog, no lighting of textures, perspective on edges, no filtering */
        memset(cfg, '0', sizeof cfg);
        cfg[10 + 15] = '1';
    } else if (cfg[7] != 52) printf("Warp3DPrefs: Wazp3D settings version %d (52 expected); read as 52\n", cfg[7]);
    set("Fog", cfg[10 + 17] != '0' ? "1" : "0", save);
    set("Lighting", cfg[10 + 16] == '1' ? "1" : "0", save);       /* TexMode 1: "GL Coloring" */
    set("Perspective", cfg[10 + 15] != '0' ? "1" : "0", save);
    set("Filtering", cfg[10 + 19] != '0' || cfg[10 + 6] != '0' ? "1" : "0", save);
    printf("Warp3DPrefs: Wazp3D's settings taken; Wazp3D's own files are left as they are\n");
    return 0;
}

int main(void)
{
    LONG args[8] = { 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"DRIVER/K,ZBUFFER/K,FOG/K,PERSPECTIVE/K,FILTERING/K,LIGHTING/K,FROMWAZP3D/S,SAVE/S", args, 0);
    int i, save;
    if (!rd) { PrintFault(IoErr(), (STRPTR)"Warp3DPrefs"); return 10; }
    save = args[7] != 0;
    if (args[6]) from_wazp3d(save);
    if (args[0]) {
        const char *d = (const char *)args[0];
        if (!stricmp(d, "AUTO") || !stricmp(d, "CPU") || !stricmp(d, "OPENGPU")) set("Driver", !stricmp(d, "AUTO") ? "Auto" : !stricmp(d, "CPU") ? "CPU" : "OpenGPU", save);
        else printf("Warp3DPrefs: DRIVER is AUTO, CPU or OPENGPU\n");
    }
    if (args[1]) {
        if (!strcmp((char *)args[1], "16") || !strcmp((char *)args[1], "32")) set("ZBuffer", (char *)args[1], save);
        else printf("Warp3DPrefs: ZBUFFER is 16 or 32\n");
    }
    for (i = 2; i < 6; i++)
        if (args[i]) {
            const char *v = onoff((const char *)args[i]);
            if (v) set(names[i], v, save);
            else printf("Warp3DPrefs: %s is ON or OFF\n", names[i]);
        }
    for (i = 0; i < 6; i++) {
        char var[40], v[16];
        strcpy(var, "Warp3D/");
        strcat(var, names[i]);
        if (GetVar((STRPTR)var, (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) <= 0) strcpy(v, dflt[i]);
        if (i >= 2) strcpy(v, v[0] == '0' ? "Off" : "On");
        printf("%-12s %s\n", names[i], v);
    }
    FreeArgs(rd);
    return 0;
}
