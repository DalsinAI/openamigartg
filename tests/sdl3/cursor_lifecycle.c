/* System/custom cursor alternation, including cleanup. MIT, AmigaChrome Team. */
#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    static const Uint8 data[8]={0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff};
    static const Uint8 mask[8]={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
    SDL_Window *window;
    SDL_Cursor *custom, *hand, *arrow;
    int round;
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) goto fail;
    window=SDL_CreateWindow("SDL3 cursor lifecycle",320,200,0);
    if (!window) goto fail;
    printf("KEYCODES return=%u backspace=%u s=%u\n",
           (unsigned)SDL_GetKeyFromScancode(SDL_SCANCODE_RETURN,0,true),
           (unsigned)SDL_GetKeyFromScancode(SDL_SCANCODE_BACKSPACE,0,true),
           (unsigned)SDL_GetKeyFromScancode(SDL_SCANCODE_S,0,true));
    fflush(stdout);
    for (round=0;round<30;round++) {
        custom=SDL_CreateCursor(data,mask,8,8,0,0);
        hand=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        arrow=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        if (!custom || !hand || !arrow) goto fail;
        /* Old backend: hand->next and arrow->next each point to themselves.
           Selecting custom then spins forever in SDL_SetCursor's search. */
        printf("ROUND %d selecting older custom cursor\n",round); fflush(stdout);
        if (!SDL_SetCursor(custom) || !SDL_SetCursor(hand) || !SDL_SetCursor(arrow) || !SDL_SetCursor(custom)) goto fail;
        SDL_DestroyCursor(hand);
        SDL_DestroyCursor(arrow);
        SDL_DestroyCursor(custom);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("PASS 30 system/custom cursor create/select/destroy cycles and SDL_Quit");
    return 0;
fail:
    fprintf(stderr,"FAIL cursor lifecycle: %s\n",SDL_GetError());
    return 20;
}
