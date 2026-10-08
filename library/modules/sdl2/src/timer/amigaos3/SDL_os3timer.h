/*
  SDL2 timed waits -- AmigaOS 3.x (timer.device UNIT_MICROHZ)
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).

  SDL_Delay and the timed waits of semaphores and condition variables
  use these. Each call opens its own timer request in the calling task,
  because a message port signals only the task that made it.
*/
#ifndef SDL_os3timer_h_
#define SDL_os3timer_h_

#include "../../SDL_internal.h"

/* Wait until one of the signals in sigmask arrives or ms milliseconds
   pass. Returns the signals from sigmask that arrived (0 on a timeout).
   ms == 0 only checks. If timer.device can't be opened, it waits for
   the signals without a timeout. */
extern Uint32 SDL_OS3_WaitSignals(Uint32 sigmask, Uint32 ms);

#endif
