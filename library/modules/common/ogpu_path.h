/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * File names that mean the same wherever they are opened.
 *
 * "PROGDIR:data/a.ogg", "gfx/a.png" and "Games:Neverball/a.png" name a file
 * only for the process that asked: PROGDIR: is the home folder of that
 * process alone, a relative name is relative to its current directory, and
 * an assign can differ from process to process. A module that passes the
 * name on (to a thread it starts, to a decoder that reads a piece ahead, to
 * the x86 or ARM64 cores, to datatypes.library) hands it to a process where
 * it means something else, or nothing, and AmigaDOS answers with "Please
 * insert volume PROGDIR: in any drive".
 *
 * ogpu_abspath turns the name into the file's full path (Lock +
 * NameFromLock, "DH1:Games/Neverball/data/a.ogg") in the caller's own
 * context, before anything else sees it. A file that doesn't exist yet
 * (one to be written) gets its folder's full path and its own name. When
 * the name can't be resolved the name itself comes back, so the call the
 * name was for fails, or works, as it would have without this.
 *
 * ogpu_quiet_requesters / ogpu_restore_requesters set the process's
 * window pointer to -1 (no "insert volume" requesters) around an open that
 * may fail on purpose, and put it back.
 *
 * Header only: the module's code, built -fbaserel32, keeps no data of its
 * own here. */
#ifndef OGPU_PATH_H
#define OGPU_PATH_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* A process's requester window: -1 turns requesters off. A task that isn't
 * a process has none, and gets NULL. */
static inline APTR ogpu_quiet_requesters(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old;
    if (!me || me->pr_Task.tc_Node.ln_Type != NT_PROCESS) {
        return NULL;
    }
    old = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1L;
    return old;
}

static inline void ogpu_restore_requesters(APTR old)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    if (me && me->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        me->pr_WindowPtr = old;
    }
}

/* name is "VOL:..." for a volume or assign called vol (case ignored). */
static inline int ogpu_path_volume_is(const char *name, const char *vol)
{
    while (*vol) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (c != *vol++) {
            return 0;
        }
    }
    return *name == ':';
}

/* A handler's stream (a console, a pipe, the null device), not a file of a
 * volume: locking it means nothing, and some handlers would wait for it. */
static inline int ogpu_path_is_stream(const char *name)
{
    return name[0] == '*' ||
           ogpu_path_volume_is(name, "CON") || ogpu_path_volume_is(name, "RAW") ||
           ogpu_path_volume_is(name, "NIL") || ogpu_path_volume_is(name, "PIPE") ||
           ogpu_path_volume_is(name, "SPEAK") || ogpu_path_volume_is(name, "SER") ||
           ogpu_path_volume_is(name, "PAR") || ogpu_path_volume_is(name, "PRT") ||
           ogpu_path_volume_is(name, "AUX") || ogpu_path_volume_is(name, "KCON") ||
           ogpu_path_volume_is(name, "KRAW") || ogpu_path_volume_is(name, "CONSOLE");
}

/* name as a full path in buf (size bytes), or name itself. The result is
 * buf or name; it stays good as long as both do. */
static inline const char *ogpu_abspath(const char *name, char *buf, ULONG size)
{
    const char *result = name;
    const char *leaf;
    APTR oldwin;
    BPTR lock;
    ULONG len, cut;

    if (!name || !*name || !buf || size < 16 || ogpu_path_is_stream(name)) {
        return name;
    }
    oldwin = ogpu_quiet_requesters();
    lock = Lock((CONST_STRPTR)name, SHARED_LOCK);
    if (lock) {
        if (NameFromLock(lock, (STRPTR)buf, (LONG)size)) {
            result = buf;
        }
        UnLock(lock);
    } else {
        /* Not there (yet), or open for writing: its folder's path and its name. */
        for (len = 0; name[len]; len++) {
        }
        cut = len;
        while (cut > 0 && name[cut - 1] != '/' && name[cut - 1] != ':') {
            cut--;
        }
        leaf = name + cut;
        if (*leaf && cut < size) {
            ULONG i;
            for (i = 0; i < cut; i++) {
                buf[i] = name[i];
            }
            buf[cut] = '\0';
            if (cut > 0 && buf[cut - 1] == '/') {
                buf[--cut] = '\0';          /* "dir/" is the folder dir */
            }
            if (cut > 0 || name[0] != '/') {
                /* "" is the current directory; a lone "/" (the parent) is left as it is */
                lock = Lock((CONST_STRPTR)buf, SHARED_LOCK);
                if (lock) {
                    if (NameFromLock(lock, (STRPTR)buf, (LONG)size) && AddPart((STRPTR)buf, (CONST_STRPTR)leaf, size)) {
                        result = buf;
                    }
                    UnLock(lock);
                }
            }
        }
    }
    ogpu_restore_requesters(oldwin);
    return result;
}

#endif
