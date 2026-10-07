/* PatchScan: which functions of a library jump outside the ROM (someone's
 * patch), to see what an RTG system changes. Not shipped. MIT, Dalsin Limited. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void scan(const char *name)
{
    struct Library *lib = OpenLibrary((STRPTR)name, 0);
    if (!lib) return;
    Printf((STRPTR)"%s (%ld vectors):\n", (LONG)name, (LONG)(lib->lib_NegSize / 6));
    for (UWORD off = 30; off < lib->lib_NegSize; off += 6) {
        UBYTE *v = (UBYTE *)lib - off;
        ULONG target = *(UWORD *)v == 0x4EF9 ? *(ULONG *)(v + 2) : 0;
        if (target && (target < 0x00F80000UL || target >= 0x01000000UL))
            Printf((STRPTR)"  -%ld  -> %08lx\n", (LONG)off, target);
    }
    CloseLibrary(lib);
}

int main(void)
{
    scan("graphics.library");
    scan("intuition.library");
    scan("layers.library");
    return 0;
}
