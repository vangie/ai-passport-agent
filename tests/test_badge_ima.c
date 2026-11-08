#include "badge_ima.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "encode")) {
            int16_t pcm[480];
            uint8_t block[246];
            int index = 0;
            size_t count;
            while ((count = fread(pcm, sizeof(*pcm), 480, stdin))) {
                size_t bytes = badge_ima_encode(pcm, count, &index, block, sizeof(block));
                assert(bytes && fwrite(block, 1, bytes, stdout) == bytes);
            }
        } else {
            uint8_t block[246];
            int16_t pcm[480];
            while (fread(block, 1, 6, stdin) == 6) {
                size_t count = (unsigned)block[4] | (unsigned)block[5] << 8;
                assert(count && count <= 480);
                assert(fread(block + 6, 1, count / 2, stdin) == count / 2);
                assert(badge_ima_decode(block, 6 + count / 2, pcm, 480) == count);
                assert(fwrite(pcm, sizeof(*pcm), count, stdout) == count);
            }
        }
        return 0;
    }
    int16_t pcm[480], decoded[480];
    uint8_t block[246];
    for (unsigned count = 1; count <= 480; count++) {
        int index = 0;
        for (unsigned sample = 0; sample < count; sample++) pcm[sample] = (int16_t)(sample * 101 - 24000);
        size_t bytes = badge_ima_encode(pcm, count, &index, block, sizeof(block));
        assert(bytes == 6 + count / 2 && badge_ima_samples(block, bytes) == count);
        assert(badge_ima_decode(block, bytes, decoded, 480) == count && decoded[0] == pcm[0]);
        assert(!badge_ima_decode(block, bytes, decoded, count - 1));
        assert(!badge_ima_samples(block, bytes - 1));
        block[2] = 89; assert(!badge_ima_samples(block, bytes)); block[2] = 0;
        block[3] = 1; assert(!badge_ima_samples(block, bytes)); block[3] = 0;
        if (!(count & 1)) { block[bytes - 1] |= 0xf0; assert(!badge_ima_samples(block, bytes)); }
    }
    for (unsigned sample = 0; sample < 480; sample++) pcm[sample] = sample % 2 ? -32768 : 32767;
    int index = 0;
    assert(badge_ima_encode(pcm, 480, &index, block, sizeof(block)) == 246);
    assert(badge_ima_decode(block, 246, decoded, 480) == 480);
    assert(!badge_ima_encode(pcm, 481, &index, block, sizeof(block)));
    puts("Passport IMA: bounded blocks, all lengths, clipping and malformed blocks PASS");
}
