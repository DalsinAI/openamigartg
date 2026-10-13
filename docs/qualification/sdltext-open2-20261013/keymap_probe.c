#include <exec/types.h>
#include <proto/exec.h>
#include <proto/keymap.h>
#include <stdio.h>
struct Library *KeymapBase;
int main(void) {
 const unsigned char chars[]={0xa3,0xe9,0xc4}; unsigned char keys[24]; unsigned i,k; LONG n;
 KeymapBase=OpenLibrary("keymap.library",36); if(!KeymapBase)return 20;
 for(i=0;i<sizeof(chars);i++) { n=MapANSI((STRPTR)&chars[i],1,(STRPTR)keys,12,NULL);
  printf("LATIN1 %02X COUNT %ld",chars[i],n);for(k=0;n>0 && k<(unsigned)n*2;k++)printf(" %02X",keys[k]);puts("");
 }
 CloseLibrary(KeymapBase);return 0;
}
