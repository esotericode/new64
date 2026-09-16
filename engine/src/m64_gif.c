/*
 * new64 -- animated GIF writer, including its LZW coder.
 *
 * Why GIF at all, when PNG is already here: an animation is the only honest way
 * to review movement.  A still frame cannot show you that a jump arc is right,
 * that a slide decelerates plausibly, or that a wall kick fired on the frame you
 * expected.  GIF is chosen over a video container because it needs no codec and
 * plays anywhere.
 *
 * A single fixed palette is shared by every frame -- a 6x6x6 colour cube plus a
 * grey ramp.  Per-frame palettes would quantise slightly differently each frame
 * and make flat surfaces shimmer, which reads as a rendering bug.
 */
#include "m64_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIF_PALETTE_SIZE 256
#define GIF_CUBE_LEVELS  6
#define GIF_CUBE_COLORS  (GIF_CUBE_LEVELS * GIF_CUBE_LEVELS * GIF_CUBE_LEVELS) /* 216 */
#define GIF_GRAY_COLORS  (GIF_PALETTE_SIZE - GIF_CUBE_COLORS)                  /* 40 */

#define LZW_MIN_CODE_SIZE 8
#define LZW_CLEAR_CODE    256
#define LZW_EOI_CODE      257
#define LZW_FIRST_CODE    258
#define LZW_MAX_CODE      4095
#define LZW_HASH_SIZE     8192

struct M64Gif {
    FILE *f;
    s32 width;
    s32 height;
    s32 delayCs;
    u8 *indices;

    /* Output bit packing, buffered into 255-byte sub-blocks. */
    u8 block[255];
    s32 blockLen;
    u32 bitBuffer;
    s32 bitCount;

    /* LZW dictionary: open-addressed map from (prefix, suffix) to code. */
    s32 hashKey[LZW_HASH_SIZE];
    s16 hashCode[LZW_HASH_SIZE];
};

/* --- Palette ------------------------------------------------------------ */

static void gif_palette_entry(s32 index, u8 *r, u8 *g, u8 *b) {
    if (index < GIF_CUBE_COLORS) {
        s32 ri = index / (GIF_CUBE_LEVELS * GIF_CUBE_LEVELS);
        s32 gi = (index / GIF_CUBE_LEVELS) % GIF_CUBE_LEVELS;
        s32 bi = index % GIF_CUBE_LEVELS;

        *r = (u8) (ri * 255 / (GIF_CUBE_LEVELS - 1));
        *g = (u8) (gi * 255 / (GIF_CUBE_LEVELS - 1));
        *b = (u8) (bi * 255 / (GIF_CUBE_LEVELS - 1));
    } else {
        /* Grey ramp, offset off the cube's own greys to add intermediate steps. */
        s32 level = index - GIF_CUBE_COLORS;
        u8 v = (u8) (level * 255 / (GIF_GRAY_COLORS - 1));

        *r = v;
        *g = v;
        *b = v;
    }
}

static u8 gif_quantize(u32 pixel) {
    s32 r = (s32) ((pixel >> 16) & 0xFF);
    s32 g = (s32) ((pixel >> 8) & 0xFF);
    s32 b = (s32) (pixel & 0xFF);

    /* Nearest cube cell. */
    s32 ri = (r * (GIF_CUBE_LEVELS - 1) + 127) / 255;
    s32 gi = (g * (GIF_CUBE_LEVELS - 1) + 127) / 255;
    s32 bi = (b * (GIF_CUBE_LEVELS - 1) + 127) / 255;
    s32 cubeIndex = ri * GIF_CUBE_LEVELS * GIF_CUBE_LEVELS + gi * GIF_CUBE_LEVELS + bi;

    u8 cr, cg, cb;
    s32 cubeErr, grayErr;
    s32 grayLevel, grayIndex;
    s32 dr, dg, db;

    gif_palette_entry(cubeIndex, &cr, &cg, &cb);
    dr = r - cr; dg = g - cg; db = b - cb;
    cubeErr = dr * dr + dg * dg + db * db;

    /* The grey ramp is finer than the cube's diagonal, so near-grey pixels
     * (which flat shading produces a lot of) quantise noticeably better there. */
    grayLevel = (r * 299 + g * 587 + b * 114) / 1000;
    grayIndex = GIF_CUBE_COLORS + (grayLevel * (GIF_GRAY_COLORS - 1) + 127) / 255;
    gif_palette_entry(grayIndex, &cr, &cg, &cb);
    dr = r - cr; dg = g - cg; db = b - cb;
    grayErr = dr * dr + dg * dg + db * db;

    return (u8) (grayErr < cubeErr ? grayIndex : cubeIndex);
}

/* --- Bit / block output ------------------------------------------------- */

static void gif_flush_block(struct M64Gif *gif) {
    if (gif->blockLen > 0) {
        u8 len = (u8) gif->blockLen;

        fwrite(&len, 1, 1, gif->f);
        fwrite(gif->block, 1, (size_t) gif->blockLen, gif->f);
        gif->blockLen = 0;
    }
}

static void gif_emit_byte(struct M64Gif *gif, u8 value) {
    gif->block[gif->blockLen++] = value;
    if (gif->blockLen == 255) {
        gif_flush_block(gif);
    }
}

static void gif_emit_code(struct M64Gif *gif, s32 code, s32 codeSize) {
    /* GIF packs codes least-significant-bit first across byte boundaries. */
    gif->bitBuffer |= ((u32) code) << gif->bitCount;
    gif->bitCount += codeSize;

    while (gif->bitCount >= 8) {
        gif_emit_byte(gif, (u8) (gif->bitBuffer & 0xFF));
        gif->bitBuffer >>= 8;
        gif->bitCount -= 8;
    }
}

static void gif_flush_bits(struct M64Gif *gif) {
    if (gif->bitCount > 0) {
        gif_emit_byte(gif, (u8) (gif->bitBuffer & 0xFF));
        gif->bitBuffer = 0;
        gif->bitCount = 0;
    }
}

/* --- LZW --------------------------------------------------------------- */

static void lzw_reset_dict(struct M64Gif *gif) {
    memset(gif->hashKey, -1, sizeof(gif->hashKey));
}

static s32 lzw_lookup(struct M64Gif *gif, s32 prefix, s32 suffix, s16 *outCode) {
    s32 key = (prefix << 8) | suffix;
    s32 slot = ((key >> 5) ^ (key << 3)) & (LZW_HASH_SIZE - 1);

    for (;;) {
        if (gif->hashKey[slot] == -1) {
            *outCode = (s16) slot; /* free slot, returned for insertion */
            return 0;
        }
        if (gif->hashKey[slot] == key) {
            *outCode = gif->hashCode[slot];
            return 1;
        }
        slot = (slot + 1) & (LZW_HASH_SIZE - 1);
    }
}

static void lzw_insert(struct M64Gif *gif, s32 slot, s32 prefix, s32 suffix, s32 code) {
    s32 key = (prefix << 8) | suffix;
    s32 s = slot;

    /* Re-probe: the slot handed back by lookup is the first free one. */
    while (gif->hashKey[s] != -1) {
        s = (s + 1) & (LZW_HASH_SIZE - 1);
    }
    gif->hashKey[s] = key;
    gif->hashCode[s] = (s16) code;
    (void) prefix;
    (void) suffix;
}

static void gif_encode_frame(struct M64Gif *gif) {
    s32 pixelCount = gif->width * gif->height;
    s32 codeSize = LZW_MIN_CODE_SIZE + 1;
    s32 nextCode = LZW_FIRST_CODE;
    s32 prefix;
    s32 i;

    u8 minCodeSize = LZW_MIN_CODE_SIZE;

    fwrite(&minCodeSize, 1, 1, gif->f);

    gif->blockLen = 0;
    gif->bitBuffer = 0;
    gif->bitCount = 0;

    lzw_reset_dict(gif);
    gif_emit_code(gif, LZW_CLEAR_CODE, codeSize);

    if (pixelCount == 0) {
        gif_emit_code(gif, LZW_EOI_CODE, codeSize);
        gif_flush_bits(gif);
        gif_flush_block(gif);
        fputc(0, gif->f);
        return;
    }

    prefix = gif->indices[0];

    for (i = 1; i < pixelCount; i++) {
        s32 suffix = gif->indices[i];
        s16 found;

        if (lzw_lookup(gif, prefix, suffix, &found)) {
            prefix = found;
            continue;
        }

        /* Not in the dictionary: emit the prefix and add the new pair. */
        gif_emit_code(gif, prefix, codeSize);

        if (nextCode <= LZW_MAX_CODE) {
            lzw_insert(gif, found, prefix, suffix, nextCode);
            if (nextCode == (1 << codeSize) && codeSize < 12) {
                codeSize++;
            }
            nextCode++;
        } else {
            /* Dictionary full: reset, as the format requires. */
            gif_emit_code(gif, LZW_CLEAR_CODE, codeSize);
            lzw_reset_dict(gif);
            codeSize = LZW_MIN_CODE_SIZE + 1;
            nextCode = LZW_FIRST_CODE;
        }
        prefix = suffix;
    }

    gif_emit_code(gif, prefix, codeSize);
    gif_emit_code(gif, LZW_EOI_CODE, codeSize);
    gif_flush_bits(gif);
    gif_flush_block(gif);
    fputc(0, gif->f); /* block terminator */
}

/* --- Public API --------------------------------------------------------- */

static void put_u16le(FILE *f, u32 v) {
    fputc((s32) (v & 0xFF), f);
    fputc((s32) ((v >> 8) & 0xFF), f);
}

struct M64Gif *m64_gif_begin(const char *path, s32 width, s32 height, s32 delayCs) {
    struct M64Gif *gif;
    s32 i;

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    gif = (struct M64Gif *) calloc(1, sizeof(*gif));
    if (gif == NULL) {
        return NULL;
    }

    gif->indices = (u8 *) malloc((size_t) width * (size_t) height);
    if (gif->indices == NULL) {
        free(gif);
        return NULL;
    }

    gif->f = fopen(path, "wb");
    if (gif->f == NULL) {
        free(gif->indices);
        free(gif);
        return NULL;
    }

    gif->width = width;
    gif->height = height;
    gif->delayCs = delayCs;

    fwrite("GIF89a", 1, 6, gif->f);
    put_u16le(gif->f, (u32) width);
    put_u16le(gif->f, (u32) height);
    fputc(0xF7, gif->f); /* global colour table, 256 entries */
    fputc(0, gif->f);    /* background colour index */
    fputc(0, gif->f);    /* pixel aspect ratio: unspecified */

    for (i = 0; i < GIF_PALETTE_SIZE; i++) {
        u8 r, g, b;

        gif_palette_entry(i, &r, &g, &b);
        fputc(r, gif->f);
        fputc(g, gif->f);
        fputc(b, gif->f);
    }

    /* Netscape extension: loop forever. */
    fputc(0x21, gif->f);
    fputc(0xFF, gif->f);
    fputc(0x0B, gif->f);
    fwrite("NETSCAPE2.0", 1, 11, gif->f);
    fputc(0x03, gif->f);
    fputc(0x01, gif->f);
    put_u16le(gif->f, 0); /* 0 == infinite */
    fputc(0x00, gif->f);

    return gif;
}

s32 m64_gif_add_frame(struct M64Gif *gif, const u32 *pixels) {
    s32 i;
    s32 pixelCount;

    if (gif == NULL || pixels == NULL) {
        return -1;
    }
    pixelCount = gif->width * gif->height;

    for (i = 0; i < pixelCount; i++) {
        gif->indices[i] = gif_quantize(pixels[i]);
    }

    /* Graphic control extension: per-frame delay. */
    fputc(0x21, gif->f);
    fputc(0xF9, gif->f);
    fputc(0x04, gif->f);
    fputc(0x04, gif->f); /* disposal: leave in place, no transparency */
    put_u16le(gif->f, (u32) gif->delayCs);
    fputc(0x00, gif->f); /* transparent colour index (unused) */
    fputc(0x00, gif->f);

    /* Image descriptor: full frame, no local colour table, not interlaced. */
    fputc(0x2C, gif->f);
    put_u16le(gif->f, 0);
    put_u16le(gif->f, 0);
    put_u16le(gif->f, (u32) gif->width);
    put_u16le(gif->f, (u32) gif->height);
    fputc(0x00, gif->f);

    gif_encode_frame(gif);
    return 0;
}

s32 m64_gif_end(struct M64Gif *gif) {
    if (gif == NULL) {
        return -1;
    }
    fputc(0x3B, gif->f); /* trailer */
    fclose(gif->f);
    free(gif->indices);
    free(gif);
    return 0;
}
