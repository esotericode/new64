/*
 * new64 -- sine table generator.
 *
 * Emits engine/src/m64_sintable.c.  The table is committed to the repo rather
 * than built at startup so that a given new64 binary is bit-for-bit
 * deterministic regardless of the host libm.  4096 entries cover a full turn,
 * which is the resolution the N64 titles use: an s16 angle is shifted right by
 * 4 to index it, so the low nibble of an angle is discarded.
 *
 *   cc -O2 -o gen_sintable tools/gen_sintable.c -lm && ./gen_sintable > engine/src/m64_sintable.c
 */
#include <math.h>
#include <stdio.h>

#define LEN 4096

int main(void) {
    printf("/*\n");
    printf(" * new64 -- GENERATED FILE, do not edit by hand.\n");
    printf(" * Regenerate with: tools/gen_sintable.c (see the comment at its head).\n");
    printf(" *\n");
    printf(" * sin(2*pi*i/%d) for i in [0, %d).  Indexed by an s16 angle >> 4.\n", LEN, LEN);
    printf(" */\n");
    printf("#include \"m64_math.h\"\n\n");
    printf("const f32 gM64SineTable[M64_SINE_TABLE_LEN] = {\n");
    for (int i = 0; i < LEN; i++) {
        double a = 2.0 * M_PI * (double) i / (double) LEN;
        double s = sin(a);
        /* Snap the exact quadrant values so they are clean, not 1e-16 off. */
        if (i % (LEN / 4) == 0) {
            s = (i == 0) ? 0.0 : (i == LEN / 4 ? 1.0 : (i == LEN / 2 ? 0.0 : -1.0));
        }
        if (i % 4 == 0) {
            printf("    ");
        }
        printf("%14.9ff,", (float) s);
        printf((i % 4 == 3) ? "\n" : " ");
    }
    printf("};\n");
    return 0;
}
