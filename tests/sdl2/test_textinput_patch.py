#!/usr/bin/env python3
"""Run the exact added conversion/mapping helpers from the backend patch.
Host mocks exercise helper contracts only; actual Amiga tests are separate.
"""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
PATCH=ROOT/'library/modules/sdl2/patches/amigaos3/0002-text-input.patch'

def added_source():
    return '\n'.join(x[1:] for x in PATCH.read_text().splitlines()
                     if x.startswith('+') and not x.startswith('+++'))

def function(name):
    text=added_source();start=text.index('static int '+name+'(')
    begin=text.index('{',start);depth=0
    for i in range(begin,len(text)):
        if text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if depth==0:return text[start:i+1]
    raise AssertionError('Incomplete function in patch: '+name)

class TextInputPatchTests(unittest.TestCase):
    def test_message_and_focus_ownership_contract(self):
        patch=PATCH.read_text();added=added_source()
        self.assertIn('dead_key = *((APTR *)imsg->IAddress);',added)
        self.assertLess(patch.index('dead_key = *((APTR *)imsg->IAddress);'),
                        patch.index('ReplyMsg((struct Message *)imsg);'))
        self.assertIn('SDL_GetKeyboardFocus() == sdlwin',added)
        self.assertIn('CloseLibrary(KeymapBase)',added)
        self.assertIn('KeymapBase = NULL',added)
        self.assertNotIn('SDL_SendQuit',added)

    @unittest.skipUnless(shutil.which('cc'),'Host C compiler unavailable')
    def test_actual_helper_bodies(self):
        support=r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint16_t UWORD;
typedef void *APTR;
typedef char *STRPTR;
typedef long LONG;
struct Library {int unused;};
struct InputEvent {int ie_Class,ie_SubClass;UWORD ie_Code,ie_Qualifier;APTR ie_EventAddress;};
#define AMIGA_KEY_UP 0x80
#define IECLASS_RAWKEY 1
#define IEQUALIFIER_CONTROL 8
#define IEQUALIFIER_LCOMMAND 64
#define IEQUALIFIER_RCOMMAND 128
#define SDL_zero(x) memset(&(x),0,sizeof(x))
static struct Library lib;
static struct Library *KeymapBase=&lib;
static int enabled=1,calls=0,result=1;
static struct InputEvent seen;
static int SDL_IsTextInputActive(void){return enabled;}
static LONG MapRawKey(struct InputEvent *event,STRPTR buffer,LONG cap,APTR keymap){
 (void)cap;(void)keymap;calls++;seen=*event;buffer[0]='a';return result;
}
'''
        main=r'''
static int checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 char out[80];unsigned char mapped[32];int previous;
 unsigned char accents[]={0xe9,0xa0,0xff};unsigned char expected[]={0xc3,0xa9,0xc2,0xa0,0xc3,0xbf,0};
 memset(out,0x55,sizeof(out));
 CHECK(OS3_AmigaTextToUTF8((const unsigned char *)"aA1 ",4,out,80)==4);
 CHECK(strcmp(out,"aA1 ")==0);
 CHECK(OS3_AmigaTextToUTF8(accents,3,out,80)==6);
 CHECK(memcmp(out,expected,7)==0);
 CHECK(OS3_AmigaTextToUTF8((const unsigned char *)"\033[A",3,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8((const unsigned char *)"\t",1,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8((const unsigned char *)"\177",1,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8((const unsigned char *)"\200",1,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8(NULL,1,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8(accents,0,out,80)==0);
 CHECK(OS3_AmigaTextToUTF8(accents,1,NULL,80)==0);
 memset(out,0x55,sizeof(out));
 CHECK(OS3_AmigaTextToUTF8(accents,1,out,2)==0);
 CHECK(out[2]==0x55);
 CHECK(OS3_AmigaTextToUTF8(accents,1,out,3)==2 && out[2]==0);
 CHECK(OS3_AmigaTextToUTF8(accents,1,out,1)==0);
 CHECK(OS3_MapRawText(0x20,0,(APTR)(uintptr_t)0x1234,mapped,32)==1);
 CHECK(calls==1 && seen.ie_Code==0x20 && seen.ie_Class==IECLASS_RAWKEY);
 CHECK(seen.ie_EventAddress==(APTR)(uintptr_t)0x1234 && seen.ie_Qualifier==0);
 previous=calls;
 CHECK(OS3_MapRawText(0xa0,0,NULL,mapped,32)==0);
 enabled=0;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==0);enabled=1;
 CHECK(OS3_MapRawText(0x20,IEQUALIFIER_CONTROL,NULL,mapped,32)==0);
 CHECK(OS3_MapRawText(0x20,IEQUALIFIER_LCOMMAND,NULL,mapped,32)==0);
 CHECK(OS3_MapRawText(0x20,IEQUALIFIER_RCOMMAND,NULL,mapped,32)==0);
 KeymapBase=NULL;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==0);KeymapBase=&lib;
 CHECK(calls==previous);
 CHECK(OS3_MapRawText(0x20,16,NULL,mapped,32)==1 && seen.ie_Qualifier==16);
 result=0;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==0);
 result=-1;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==0);
 result=33;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==0);
 result=32;CHECK(OS3_MapRawText(0x20,0,NULL,mapped,32)==32);
 printf("TEXTINPUT_HELPERS_PASS %d checks; host helper contract, not guest runtime\n",checks);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/'helpers.c';exe=Path(temp)/'helpers'
            src.write_text(support+'\n'+function('OS3_AmigaTextToUTF8')+'\n'+function('OS3_MapRawText')+'\n'+main)
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            completed=subprocess.run([str(exe)],check=True,text=True,capture_output=True)
            self.assertIn('TEXTINPUT_HELPERS_PASS',completed.stdout)
            print(completed.stdout,end='')

class ModifierFocusPatchTests(unittest.TestCase):
    def test_raw_modifier_mapping_matches_amiga_rkm(self):
        p = Path(__file__).resolve().parents[2] / 'library/modules/sdl2/patches/amigaos3/0003-modifier-focus.patch'
        added = '\n'.join(line[1:] for line in p.read_text().splitlines() if line.startswith('+') and not line.startswith('+++'))
        for raw, code in [('64','LALT'),('65','RALT'),('66','LGUI'),('67','RGUI')]:
            self.assertIn('/* 0x'+raw+' */ SDL_SCANCODE_'+code, added)
    def test_inactive_raw_messages_are_gated(self):
        p = Path(__file__).resolve().parents[2] / 'library/modules/sdl2/patches/amigaos3/0003-modifier-focus.patch'
        self.assertIn('+                if (SDL_GetKeyboardFocus() == sdlwin)', p.read_text())

if __name__=='__main__':unittest.main()
