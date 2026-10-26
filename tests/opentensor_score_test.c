/* Host DSP validation; no AHI or target timing claims. Dalsin Limited, MIT. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../demos/opentensor/src/opentensor_score.h"

int main(int argc, char **argv)
{
    static struct OtScore a, b;
    static int16_t whole[16384], split[16384], block[8192];
    unsigned i, frames, peak = 0, clipped = 0;
    uint64_t stereo = 0, energy = 0;
    FILE *preview = argc > 1 ? fopen(argv[1], "wb") : NULL;
    if (argc > 1 && !preview) return 2;
    ot_score_init(&a, 24 * 44100);
    ot_score_init(&b, 24 * 44100);
    ot_score_render(&a, whole, 8192);
    ot_score_render(&b, split, 123);
    ot_score_render(&b, split + 246, 8069);
    assert(memcmp(whole, split, sizeof(whole)) == 0);
    assert(a.cursor == b.cursor && a.echo_pos == b.echo_pos);
    ot_score_init(&a, 0);
    for (frames = 0; frames < 164 * 44100; frames += 4096) {
        unsigned n = 164 * 44100 - frames;
        if (n > 4096) n = 4096;
        ot_score_render(&a, block, n);
        for (i = 0; i < n * 2; ++i) {
            unsigned magnitude = (unsigned)abs(block[i]);
            if (magnitude > peak) peak = magnitude;
            if (magnitude >= 32767) ++clipped;
            energy += (uint64_t)((int32_t)block[i] * block[i]);
        }
        for (i = 0; i < n; ++i) stereo += block[i * 2] != block[i * 2 + 1];
        /* Raw output is native-endian signed 16-bit stereo for host preview. */
        if (preview && frames < 24 * 44100) {
            unsigned take = 24 * 44100 - frames;
            if (take > n) take = n;
            assert(fwrite(block, 4, take, preview) == take);
        }
    }
    if (preview) assert(fclose(preview) == 0);
    assert(a.cursor == 164 * 44100);
    assert(peak > 8000 && peak < 32767 && clipped == 0);
    assert(stereo > 44100 && energy > 0);
    printf("164 seconds: peak=%u clipped=%u stereo_frames=%llu; chunk invariance passed\n",
           peak, clipped, (unsigned long long)stereo);
    return 0;
}
