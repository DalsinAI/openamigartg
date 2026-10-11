# OpenGPU SDL Keyboard Text Input Fix

**Description:** `opengpu-sdl-keyboard-fix`  
**Repository:** `DalsinAI/openamigartg`  
**Status:** Ready for implementation  
**Scope:** OpenGPU SDL2 AmigaOS 3.x keyboard backend  
**Primary proving application:** AstroMenace profile naming

## 1. Purpose

The OpenGPU SDL2 AmigaOS 3.x backend currently delivers physical keyboard events (`SDL_KEYDOWN` and `SDL_KEYUP`) but does not deliver translated text events (`SDL_TEXTINPUT`).

This is visible in AstroMenace. The game runs and responds to control keys, but the pilot-profile naming page does not accept typed letters. AstroMenace handles Backspace and Return through key events, while printable characters are appended only when it receives `SDL_TEXTINPUT`.

The fix belongs in the shared OpenGPU SDL2 backend, not in AstroMenace. Once corrected, the same text-input path will benefit every SDL2 application running on OpenUp, including games, editors, launchers and future OpenBench applications.

## 2. Current Behaviour

The present input route is:

```text
PC keyboard
    -> AmigaChrome raw Amiga key record
    -> Amiga input event
    -> Intuition IDCMP_RAWKEY
    -> OpenGPU SDL2 AmigaOS backend
    -> SDL_SendKeyboardKey()
    -> SDL_KEYDOWN / SDL_KEYUP
```

This is sufficient for gameplay controls such as arrows, Escape, Return and function keys. It is not sufficient for text fields.

SDL deliberately separates a physical key from the text produced by that key:

```text
Physical event:  the key at position A went down
Text event:      the active keymap translated it to "a" or "A"
```

The distinction is required for Shift, Caps Lock, punctuation, national keyboard layouts, AltGr-style characters and dead-key composition.

## 3. Required Behaviour

The corrected route is:

```text
PC keyboard
    -> AmigaChrome raw Amiga key record
    -> Amiga input event
    -> Intuition IDCMP_RAWKEY
       |-> SDL_SendKeyboardKey()
       |      -> SDL_KEYDOWN / SDL_KEYUP
       |
       `-> keymap.library MapRawKey()
              -> Amiga character bytes
              -> bounded Amiga ANSI/Latin-1 to UTF-8 conversion
              -> SDL_SendKeyboardText()
              -> SDL_TEXTINPUT
```

The backend must continue to generate key events exactly as it does now. Text generation is an additional path for printable key-down events.

## 4. Design Principles

1. **Fix the platform, not the game.** AstroMenace must not receive a private key-to-character table.
2. **Use the active Amiga keymap.** Do not assume US QWERTY.
3. **Preserve SDL semantics.** Physical keys remain key events; translated printable text becomes text events.
4. **Keep Intuition ownership safe.** No field or pointer from an `IntuiMessage` may be used after `ReplyMsg()`.
5. **Never turn control sequences into visible text.** Cursor keys, function keys and command shortcuts must not emit escape-sequence fragments.
6. **Do not make graphics startup depend on text input.** A missing `keymap.library` disables translated text but does not prevent SDL from opening.
7. **Keep the SDL ABI unchanged.** Existing applications linked to `libSDL2.a` should use the corrected `SDL2.module` without relinking.

## 5. Root Cause

The AmigaOS event backend currently translates an Amiga raw-key code into an SDL scancode and calls:

```c
SDL_SendKeyboardKey(state, scancode);
```

It does not retain the Intuition qualifier field, does not call `MapRawKey()`, and never calls:

```c
SDL_SendKeyboardText(text);
```

AstroMenace therefore receives `SDL_KEYDOWN` and `SDL_KEYUP`, but no `SDL_TEXTINPUT`. Its profile input routine waits for a Unicode character supplied by the text event path, so the visible cursor remains active while ordinary letters appear to do nothing.

## 6. Repository Changes

The implementation should remain a focused SDL2 backend change.

```text
library/modules/sdl2/
    patches/amigaos3/
        0002-text-input.patch
    Makefile
    README.md

tests/sdl2/
    textinput.c

measurements/
    YYYYMMDD-sdl2-text-input.txt
```

### 6.1 `0002-text-input.patch`

This patch updates the prepared AmigaOS SDL2 source after `0001-video.patch` has been applied. It should modify:

```text
src/video/amigaos3/SDL_os3video.c
src/video/amigaos3/SDL_os3events.c
```

A separate patch is preferable to enlarging `0001-video.patch`: it gives the text-input repair its own reviewable unit and makes rollback straightforward.

### 6.2 Test utility

`tests/sdl2/textinput.c` should be a small diagnostic program that shows, separately:

- `SDL_KEYDOWN`
- `SDL_KEYUP`
- `SDL_TEXTINPUT`
- scancode
- keycode
- modifier state
- UTF-8 text bytes

It should build against both the shared module stub and the static SDL library when practical.

## 7. Opening `keymap.library`

The backend should open `keymap.library` during video initialisation and close it during video shutdown.

In the AmigaOS video implementation:

```c
#include <proto/keymap.h>

struct Library *KeymapBase = NULL;
```

During `OS3_VideoInit()`:

```c
KeymapBase = OpenLibrary((CONST_STRPTR) "keymap.library", 36UL);
if (!KeymapBase)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                "OS3: keymap.library unavailable; SDL text input disabled");
}
```

During `OS3_VideoQuit()`:

```c
if (KeymapBase)
{
    CloseLibrary(KeymapBase);
    KeymapBase = NULL;
}
```

`keymap.library` should be closed before the supporting Intuition and graphics libraries are closed. Failure to open it is non-fatal.

The event implementation can use:

```c
extern struct Library *KeymapBase;
```

The build preparation must stage the required keymap headers if they are not already supplied by the GCC 16 stove:

```text
proto/keymap.h
inline/keymap.h
clib/keymap_protos.h
libraries/keymap.h, if required by the SDK
```

`devices/inputevent.h` is also required for `struct InputEvent` and qualifier definitions.

## 8. Intuition Message Handling

The current event pump copies fields from the `IntuiMessage`, replies to it promptly, and then performs SDL processing. That safety rule must remain.

For `IDCMP_RAWKEY`, translation through `MapRawKey()` must occur while the message and its `IAddress` information are still valid. The translated result is copied into a local bounded buffer. Only then is the message replied to and the SDL events posted.

Conceptually:

```c
while ((imsg = (struct IntuiMessage *) GetMsg(intuiwin->UserPort)) != NULL)
{
    ULONG class = imsg->Class;
    UWORD code = imsg->Code;
    UWORD qualifier = imsg->Qualifier;
    unsigned char mapped[32];
    int mapped_len = 0;

    if (class == IDCMP_RAWKEY)
    {
        mapped_len = OS3_MapRawText(imsg, mapped, sizeof(mapped));
    }

    ReplyMsg((struct Message *) imsg);

    if (class == IDCMP_RAWKEY)
    {
        OS3_HandleRawKey(sdlwin, code, qualifier, mapped, mapped_len);
    }
    else
    {
        /* Existing event handling. */
    }
}
```

This structure prevents a use-after-reply while preserving the prompt `ReplyMsg()` discipline.

## 9. Mapping Raw Keys To Text

The mapping helper builds a local `InputEvent` and uses `MapRawKey()` with the active system keymap.

```c
static int OS3_MapRawText(const struct IntuiMessage *imsg,
                          unsigned char *buffer,
                          int capacity)
{
    struct InputEvent event;
    LONG result;

    if (!KeymapBase || !imsg || !buffer || capacity < 2)
        return 0;

    if (imsg->Code & IECODE_UP_PREFIX)
        return 0;

    if (!SDL_IsTextInputActive())
        return 0;

    if (imsg->Qualifier & (IEQUALIFIER_CONTROL |
                           IEQUALIFIER_LCOMMAND |
                           IEQUALIFIER_RCOMMAND))
        return 0;

    SDL_zero(event);
    event.ie_Class = IECLASS_RAWKEY;
    event.ie_SubClass = 0;
    event.ie_Code = imsg->Code & ~IECODE_UP_PREFIX;
    event.ie_Qualifier = imsg->Qualifier;

    /*
     * Populate ie_EventAddress according to the MapRawKey contract while
     * imsg and imsg->IAddress are still owned by this task.
     */
    event.ie_EventAddress = imsg->IAddress;

    result = MapRawKey(&event, (STRPTR) buffer, capacity, NULL);

    if (result <= 0 || result > capacity)
        return 0;

    return (int) result;
}
```

The exact `ie_EventAddress` treatment must follow the AmigaOS 3.x keymap autodoc. The implementation must not keep the pointer after `ReplyMsg()`. Calling `MapRawKey()` before replying avoids that hazard and supports dead-key state correctly.

## 10. SDL Key Events Remain Unchanged

The current scancode table and key-state path remain responsible for physical input:

```c
static void OS3_HandleRawKey(SDL_Window *sdlwin,
                             UWORD code,
                             UWORD qualifier,
                             const unsigned char *mapped,
                             int mapped_len)
{
    SDL_Scancode scancode;
    Uint8 state;
    UWORD rawcode;

    if (code & IECODE_UP_PREFIX)
    {
        state = SDL_RELEASED;
        rawcode = code & ~IECODE_UP_PREFIX;
    }
    else
    {
        state = SDL_PRESSED;
        rawcode = code;
    }

    if (rawcode >= SDL_arraysize(os3_scancode_table))
        return;

    scancode = os3_scancode_table[rawcode];

    if (scancode != SDL_SCANCODE_UNKNOWN)
        SDL_SendKeyboardKey(state, scancode);

    if (state == SDL_PRESSED && mapped_len > 0)
    {
        char utf8[64];

        if (OS3_AmigaTextToUTF8(mapped, mapped_len,
                                utf8, sizeof(utf8)) > 0)
            SDL_SendKeyboardText(utf8);
    }
}
```

This preserves:

- key-down and key-up state;
- SDL repeat detection;
- Backspace and Return handling;
- cursor and function keys;
- gameplay bindings;
- modifier state.

Text is an additional result of printable key-down events, not a replacement for the key event.

## 11. Amiga Text To UTF-8

`MapRawKey()` returns Amiga character bytes. SDL requires UTF-8 in `SDL_TEXTINPUT`.

The initial implementation should support the AmigaOS 3.x Western character set safely:

- ASCII `0x20` to `0x7E` passes through unchanged.
- Latin-1 `0xA0` to `0xFF` is encoded as two-byte UTF-8.
- C0 and C1 control bytes are rejected.
- Results beginning with Escape (`0x1B`) are rejected so ANSI cursor or function-key sequences never become visible text.
- Output is always bounded and NUL-terminated.

```c
static int OS3_AmigaTextToUTF8(const unsigned char *src,
                               int src_len,
                               char *dst,
                               int dst_len)
{
    int i;
    int out = 0;

    if (!src || src_len <= 0 || !dst || dst_len <= 1)
        return 0;

    if (src[0] == 0x1B)
        return 0;

    for (i = 0; i < src_len; ++i)
    {
        unsigned int c = src[i];

        if (c < 0x20 || (c >= 0x7F && c < 0xA0))
            return 0;

        if (c < 0x80)
        {
            if (out + 1 >= dst_len)
                return 0;

            dst[out++] = (char) c;
        }
        else
        {
            if (out + 2 >= dst_len)
                return 0;

            dst[out++] = (char) (0xC0 | (c >> 6));
            dst[out++] = (char) (0x80 | (c & 0x3F));
        }
    }

    dst[out] = '\0';
    return out;
}
```

Expected examples:

```text
a           -> SDL_KEYDOWN(a), SDL_TEXTINPUT("a")
Shift+a     -> SDL_KEYDOWN(a), SDL_TEXTINPUT("A")
Shift+1     -> SDL_KEYDOWN(1), SDL_TEXTINPUT("!")
Space       -> SDL_KEYDOWN(space), SDL_TEXTINPUT(" ")
GBP sign    -> SDL_KEYDOWN(...), SDL_TEXTINPUT(UTF-8 GBP)
Backspace   -> SDL_KEYDOWN(backspace), no text event
Return      -> SDL_KEYDOWN(return), no text event
Cursor Up   -> SDL_KEYDOWN(up), no text event
F1          -> SDL_KEYDOWN(F1), no text event
Ctrl+V      -> key events only; no inserted control character
```

## 12. Modifier And Repeat Rules

### 12.1 Allowed for text

- Shift
- Caps Lock
- Alt or right-Alt when the active keymap uses it to produce a printable character
- key repeat

### 12.2 Suppressed for text

- Control combinations
- Left Amiga command combinations
- Right Amiga command combinations
- key-up events
- non-printable mappings
- ANSI escape sequences

Repeated printable key-down messages should generate repeated `SDL_TEXTINPUT`, matching normal desktop SDL behaviour.

## 13. Focus Behaviour

Text events must be posted only for the SDL window that currently owns keyboard focus. The existing focus fix that calls `SDL_SetKeyboardFocus()` on `IDCMP_ACTIVEWINDOW` must remain.

The test must cover:

1. activate the SDL window;
2. type text successfully;
3. switch to another Workbench/OpenBench window;
4. verify the SDL program receives nothing;
5. reactivate it;
6. verify text input resumes without stuck modifiers or duplicate characters.

## 14. SDLTextCheck Diagnostic

The diagnostic should open a normal SDL window and display or print records such as:

```text
KEYDOWN   scancode=7 key=d mods=0 repeat=0
TEXTINPUT bytes=44 text="D"
KEYUP     scancode=7 key=d mods=0
```

Controls:

```text
Escape      exit
F1          clear log
F2          toggle SDL_StartTextInput / SDL_StopTextInput
```

The F2 test proves that the backend honours SDL text-input state rather than emitting text unconditionally.

The program should make duplicate delivery obvious. One physical printable press should normally produce:

```text
one KEYDOWN
one TEXTINPUT
one KEYUP
```

## 15. Automated AmigaChrome Input Test

A disposable clone of Instance-32 should be used. The runtime input endpoint can inject raw Amiga key records without touching the live instance.

The automated sequence should cover at least:

```text
lowercase letter
uppercase letter using Shift
number
shifted punctuation
space
backspace
return
cursor key
function key
```

Where the clone uses a UK keymap, include a UK-specific printable character such as the pound sign. The expected text must be based on the guest's active keymap, not the host browser layout alone.

The test should retain:

- SDLTextCheck log;
- runtime log;
- current-screen capture;
- module checksum;
- exact key records injected;
- active guest keymap identity if available.

## 16. AstroMenace Qualification

The platform test is not complete until the original failure is resolved in the real application.

In a disposable Instance-32 clone:

1. Install the corrected `SDL2.module`.
2. Launch the current AstroMenace diagnostic build.
3. Navigate to pilot-profile creation.
4. Type `DALE`.
5. Confirm all four letters appear once and in order.
6. Press Backspace and confirm `DAL` remains.
7. Type `E` again.
8. Press Return.
9. Confirm the profile is created and selected.
10. Start the first mission and verify gameplay controls still work.
11. Exit AstroMenace cleanly.
12. Relaunch it and confirm the `DALE` profile persists.

The test should also try Shift, punctuation and Caps Lock on the profile page.

## 17. Deployment

The runtime file is:

```text
LIBS:OpenGPU/SDL2.module
```

The shared module ABI is unchanged, so an existing application linked through the OpenGPU `libSDL2.a` stub should gain the corrected behaviour when the module is replaced. An AstroMenace relink is not technically required for the fix, but a clean rebuild is recommended for release qualification and provenance.

Deployment order:

1. Build the shared module, static library and test binaries.
2. Run host-side checks.
3. Install in a disposable instance clone.
4. Run SDLTextCheck.
5. Run AstroMenace profile qualification.
6. Verify Neverball/BoingBall and another SDL2 game still receive gameplay keys normally.
7. Stop the live Instance-32 with Dale's agreement.
8. Back up the previous module.
9. Install the corrected module atomically.
10. Restart and repeat the profile test on the live instance.

Do not replace the live module while SDL applications are running.

## 18. Compatibility And ABI

This change adds no public SDL symbol and changes no SDL structure visible to applications.

It changes only event production inside the AmigaOS video backend. Therefore:

- `libSDL2.a` stub ABI remains unchanged;
- `SDL2.module` ABI version should remain unchanged;
- existing binaries continue to load;
- static SDL applications must be rebuilt to receive the fix;
- module-linked applications receive it from the updated runtime module.

The OpenRTG/OpenGPU package patch version should be incremented and the module build provenance recorded.

## 19. Risks And Mitigations

### Duplicate text

**Risk:** A future backend or application also synthesises text from key events.

**Mitigation:** SDLTextCheck explicitly counts key and text events. AstroMenace must show one character per press.

### Escape sequences displayed as text

**Risk:** `MapRawKey()` can return sequences for non-printable keys.

**Mitigation:** Reject results beginning with Escape and reject control bytes.

### Incorrect national layout

**Risk:** A hard-coded key table produces US characters on a UK or European keymap.

**Mitigation:** Use `MapRawKey()` with the active guest keymap and qualifiers.

### Use after `ReplyMsg()`

**Risk:** `IAddress` is read after Intuition owns the message again.

**Mitigation:** Perform `MapRawKey()` before replying and retain only copied bytes.

### Stuck modifiers

**Risk:** focus changes lose key-up events.

**Mitigation:** retain SDL's keyboard-focus reset path and test focus loss/re-entry.

### Text emitted during shortcuts

**Risk:** Ctrl or Amiga command combinations insert characters.

**Mitigation:** suppress text mapping for Control and command qualifiers while retaining key events.

### Missing keymap library

**Risk:** SDL video refuses to start.

**Mitigation:** make `keymap.library` optional for video startup and log a clear warning.

## 20. Non-Goals

This change does not:

- redesign the AmigaChrome browser-to-Amiga raw-key protocol;
- alter mouse or joystick input;
- change AstroMenace's profile UI;
- add an on-screen keyboard;
- implement full Unicode input beyond the AmigaOS keymap's character output;
- remap SDL physical keycodes to every national layout;
- change clipboard support;
- change the OpenInput ABI.

A later enhancement may build SDL's keycode map from the active Amiga keymap so key-binding screens display national punctuation more accurately. That is separate from the immediate `SDL_TEXTINPUT` repair.

## 21. Acceptance Criteria

The fix is complete when all of the following are true:

- [ ] `keymap.library` is opened and closed safely.
- [ ] `IDCMP_RAWKEY` retains code and qualifier information.
- [ ] printable key-down events generate valid UTF-8 `SDL_TEXTINPUT`.
- [ ] key-up events never generate text.
- [ ] Backspace, Return, cursor keys and function keys remain key events only.
- [ ] Control and Amiga command combinations do not insert text.
- [ ] Shift, Caps Lock and the active guest keymap affect the resulting text.
- [ ] dead-key handling follows the `MapRawKey()` contract without using message memory after `ReplyMsg()`.
- [ ] SDLTextCheck passes against the shared module.
- [ ] SDLTextCheck passes against the static library.
- [ ] focus loss and regain do not duplicate or stick keys.
- [ ] AstroMenace accepts `DALE` on the profile page.
- [ ] Backspace edits the name.
- [ ] Return creates the profile.
- [ ] the profile survives an AstroMenace restart.
- [ ] gameplay keyboard controls remain functional.
- [ ] another SDL2 title passes a keyboard regression check.
- [ ] the live module is deployed only after the disposable-instance gate is green.

## 22. Expected Result

After deployment, AstroMenace's profile page should behave like a normal SDL2 text field:

```text
Typed key       Visible result
---------       --------------
d               d
Shift+a         A
l               l
e               e
Backspace       removes the final character
Return          accepts the completed profile name
```

The wider result is more important than AstroMenace alone: OpenGPU SDL2 gains a proper native Amiga text-input path, allowing future OpenUp and OpenBench software to use SDL text entry without application-specific workarounds.
