/* SPDX-License-Identifier: MS-PL
 *
 * plans/plan_xna_sample_xnb_sweep.md XNASWEEP-221: does the vendored stb_image decode a 24-bpp
 * BMP whose pixel data does not begin immediately after the DIB header?
 *
 * A BMP's file header carries `bfOffBits`, and a 24-bpp file is allowed to put bytes between the
 * DIB header and the pixels -- a colour table written for 256-colour displays is the usual one,
 * and SAMPLE-141's `riemerstexture.bmp` has 864 bytes of exactly that. CNA decodes such a file
 * wrongly; this says whether the decoder under it does.
 *
 *   cc -I../../third_party/stb -o bmp_offset_spike bmp_offset_spike.c -lm
 *   ./bmp_offset_spike <a 24-bpp bmp>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static unsigned int u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* The file re-headed with `gap` filler bytes before its pixels, which is the same image. */
static unsigned char *with_gap(const unsigned char *file, long size, unsigned int gap, long *out) {
    unsigned int offset = u32(file + 10);
    long pixels = size - (long)offset;
    long whole = 54 + (long)gap + pixels;
    unsigned char *made = (unsigned char *)malloc((size_t)whole);
    if (made == NULL) { return NULL; }
    memcpy(made, file, 54);
    made[10] = (unsigned char)((54u + gap) & 0xFFu);
    made[11] = (unsigned char)(((54u + gap) >> 8) & 0xFFu);
    made[12] = (unsigned char)(((54u + gap) >> 16) & 0xFFu);
    made[13] = (unsigned char)(((54u + gap) >> 24) & 0xFFu);
    made[2] = (unsigned char)(whole & 0xFF);
    made[3] = (unsigned char)((whole >> 8) & 0xFF);
    made[4] = (unsigned char)((whole >> 16) & 0xFF);
    made[5] = (unsigned char)((whole >> 24) & 0xFF);
    memset(made + 46, 0, 8);                    /* biClrUsed, biClrImportant */
    memset(made + 54, 0xAA, (size_t)gap);
    memcpy(made + 54 + gap, file + offset, (size_t)pixels);
    *out = whole;
    return made;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <bmp>\n", argv[0]); return 2; }
    FILE *handle = fopen(argv[1], "rb");
    if (handle == NULL) { perror(argv[1]); return 2; }
    fseek(handle, 0, SEEK_END);
    long size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    unsigned char *file = (unsigned char *)malloc((size_t)size);
    if (file == NULL || fread(file, 1, (size_t)size, handle) != (size_t)size) { return 2; }
    fclose(handle);

    long baseSize = 0;
    unsigned char *base = with_gap(file, size, 0u, &baseSize);
    int w = 0, h = 0, n = 0;
    unsigned char *truth = stbi_load_from_memory(base, (int)baseSize, &w, &h, &n, 4);
    if (truth == NULL) { fprintf(stderr, "gap 0 refused: %s\n", stbi_failure_reason()); return 1; }
    printf("gap 0 decodes %dx%d, taken as the truth\n", w, h);

    const unsigned int gaps[] = {4u, 16u, 64u, 256u, 768u, 864u, 1024u};
    for (size_t i = 0; i < sizeof gaps / sizeof gaps[0]; ++i) {
        long madeSize = 0;
        unsigned char *made = with_gap(file, size, gaps[i], &madeSize);
        int mw = 0, mh = 0, mn = 0;
        unsigned char *got = stbi_load_from_memory(made, (int)madeSize, &mw, &mh, &mn, 4);
        if (got == NULL) {
            printf("gap %-5u refused: %s\n", gaps[i], stbi_failure_reason());
        } else {
            long bad = 0;
            for (long p = 0; p < (long)w * h * 4; ++p) { if (got[p] != truth[p]) { ++bad; } }
            printf("gap %-5u decodes %dx%d, %ld of %ld bytes differ from the truth\n",
                   gaps[i], mw, mh, bad, (long)w * h * 4);
            stbi_image_free(got);
        }
        free(made);
    }
    stbi_image_free(truth);
    free(base);
    free(file);
    return 0;
}
