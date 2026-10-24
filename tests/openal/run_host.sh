#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# OpenAL.module's core on a Linux PC (library/modules/openal/src, AL_HOST),
# with Mix's output standing in as tests/openal/host_out.c, built with
# AddressSanitizer and UBSan (and every UBSan report fatal):
#   test_al_api      every AL and ALC call's error cases and state queries
#   test_al_3d       the distance models, cones, Doppler and the pan against
#                    values worked out in doubles, the gain steps, formats
#                    as mixed, and suspend
#   test_al_stream   queued streams with no gap and no repeat (at the
#                    device's rate and resampled), AL_BUFFERS_PROCESSED after
#                    each update, offsets, looping, underruns
#   test_al_thread   OpenAL called from two threads while a third mixes,
#                    as on the Amiga; once more under ThreadSanitizer when the
#                    compiler has it
#   test_al_voices   Voices on ACAHI's board (tests/openal/acahi, the
#                    runtime's own acahi.c, with host_voices.c as AHI): the
#                    devices, the board's settings, one-shot, looping, paused
#                    and streamed sources (no gap, AL_BUFFERS_PROCESSED right
#                    after each event), offsets, virtual sources, suspend,
#                    a random workout, and Mix's output against the same
#                    scenes through the board
#   stub_nomodule    libopenal.a (stubs/openal) with no OpenAL.module
# The Amiga's build (the module, the stub and its tests) is build.sh's.
#
#   tests/openal/run_host.sh [OUT_DIR]      (default build/tests/openal)
# Exit 0 when every test passes.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT=${1:-$ROOT/build/tests/openal}
MOD=$ROOT/library/modules/openal
CC=${CC:-cc}
mkdir -p "$OUT"

SAN="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
BOARD=$HERE/acahi/machines/motherboards/a1200-x86/native
FLAGS="-std=gnu11 -Wall -Wextra -Werror -DAL_HOST -I$MOD/include -I$MOD/src -I$ROOT/stubs/openal -I$HERE -I$BOARD"
CORE="$MOD/src/alc.c $MOD/src/al_state.c $MOD/src/al_buffer.c $MOD/src/al_source.c $MOD/src/al_spatial.c $MOD/src/al_mix.c $MOD/src/al_voices.c $HERE/host_out.c $HERE/host_voices.c $BOARD/acahi.c"

for t in test_al_api test_al_3d test_al_stream test_al_thread test_al_voices; do
    # shellcheck disable=SC2086
    $CC $FLAGS $SAN -o "$OUT/$t" "$HERE/$t.c" $CORE -lm -lpthread
    "$OUT/$t"
done

# The same threads under ThreadSanitizer (it can't share a build with
# AddressSanitizer), when the compiler and the machine have it (an empty
# program built and run with it says so).
if printf 'int main(void) { return 0; }\n' | $CC -x c -fsanitize=thread -o "$OUT/tsan_probe" - 2>/dev/null &&
    "$OUT/tsan_probe" 2>/dev/null; then
    # shellcheck disable=SC2086
    $CC $FLAGS -O1 -g -fsanitize=thread -o "$OUT/test_al_thread_tsan" "$HERE/test_al_thread.c" $CORE -lm -lpthread
    TSAN_OPTIONS=halt_on_error=1 "$OUT/test_al_thread_tsan"
else
    echo "test_al_thread under ThreadSanitizer: skipped (not available here)"
fi

# libopenal.a with no module: the stub's own file with exec, dos and
# opengpu.library answered by the test (tests/sdl3/host's stand-ins).
$CC -std=gnu11 -Wall -Wextra -Werror $SAN -I"$ROOT/tests/sdl3/host" -I"$ROOT/tests/host-include" -I"$ROOT/include" \
    -I"$ROOT/stubs/openal" -I"$MOD/include" -o "$OUT/stub_nomodule" "$HERE/stub_nomodule.c"
"$OUT/stub_nomodule"
echo "OpenAL host tests: all passed"
