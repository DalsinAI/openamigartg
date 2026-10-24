# ACAHI's board, for the OpenAL tests

These four files are the AmigaChrome runtime's own model of ACAHI, its sound
board for AHI, copied byte for byte from the amigachrome repository at
e7bb202c5bf19eed0f0427763006dd9feb3c16da, at the same paths:

| File | What it is |
| --- | --- |
| `machines/motherboards/a1200-x86/native/acahi.c`, `acahi.h` | The board: the stream, the 32 host-mixed voices, the timer |
| `vendor/amigachrome-guest/common/protocol/acahi.h` | Its registers (protocol acahi-v1) |
| `vendor/amigachrome-guest/common/protocol/amigachrome_zorro.h` | The Team's Zorro numbers, which it includes |

`tests/openal/host_voices.c` drives it the way AHI and acaudio.audio's Host
mix drive the real one, so `test_al_voices` plays OpenAL's Voices back end
on the same code the instance runs. Both repositories are the Team's, MIT.
Copy them again when the board changes.
