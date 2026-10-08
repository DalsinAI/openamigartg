/*
  SDL 2 clipboard for AmigaOS 3.x.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
*/
#ifndef SDL_os3clipboard_h_
#define SDL_os3clipboard_h_

#include "../SDL_sysvideo.h"

extern int OS3_SetClipboardText(_THIS, const char *text);
extern char *OS3_GetClipboardText(_THIS);
extern SDL_bool OS3_HasClipboardText(_THIS);

#endif
