/*
 * new64 -- PNG writer.
 *
 * Minimal but valid: one IHDR, one IDAT holding a zlib stream of the filtered
 * scanlines, one IEND.  Filter type 0 (none) is used for every row; the images
 * here are flat-shaded polygons, so the per-row filters buy little and cost
 * clarity.
 */
#include "m64_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put_u32be(u8 *p, u32 v) {
    p[0] = (u8) (v >> 24);
    p[1] = (u8) (v >> 16);
    p[2] = (u8) (v >> 8);
    p[3] = (u8) v;
}

static s32 write_chunk(FILE *f, const char *type, const u8 *data, u32 len) {
    u8 header[8];
    u8 crcBuf[4];
    uLong crc;

    put_u32be(header, len);
    memcpy(header + 4, type, 4);
    if (fwrite(header, 1, 8, f) != 8) {
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        return -1;
    }

    /* CRC covers the type and the data, but not the length. */
    crc = crc32(0L, (const Bytef *) type, 4);
    if (len > 0) {
        crc = crc32(crc, (const Bytef *) data, len);
    }
    put_u32be(crcBuf, (u32) crc);
    if (fwrite(crcBuf, 1, 4, f) != 4) {
        return -1;
    }
    return 0;
}

s32 m64_png_write(const char *path, const u32 *pixels, s32 width, s32 height) {
    static const u8 signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    FILE *f;
    u8 ihdr[13];
    u8 *raw;
    u8 *compressed;
    uLongf compressedLen;
    size_t rawLen;
    s32 x, y;
    s32 result = -1;

    if (width <= 0 || height <= 0) {
        return -1;
    }

    /* One filter byte per row, then 3 bytes per pixel. */
    rawLen = (size_t) height * (1 + (size_t) width * 3);
    raw = (u8 *) malloc(rawLen);
    if (raw == NULL) {
        return -1;
    }

    for (y = 0; y < height; y++) {
        u8 *row = raw + (size_t) y * (1 + (size_t) width * 3);

        row[0] = 0; /* filter: none */
        for (x = 0; x < width; x++) {
            u32 c = pixels[(size_t) y * width + x];

            row[1 + x * 3 + 0] = (u8) (c >> 16);
            row[1 + x * 3 + 1] = (u8) (c >> 8);
            row[1 + x * 3 + 2] = (u8) c;
        }
    }

    compressedLen = compressBound((uLong) rawLen);
    compressed = (u8 *) malloc(compressedLen);
    if (compressed == NULL) {
        free(raw);
        return -1;
    }
    if (compress2(compressed, &compressedLen, raw, (uLong) rawLen, 6) != Z_OK) {
        free(raw);
        free(compressed);
        return -1;
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(compressed);
        return -1;
    }

    if (fwrite(signature, 1, 8, f) != 8) {
        goto done;
    }

    put_u32be(ihdr + 0, (u32) width);
    put_u32be(ihdr + 4, (u32) height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* colour type: truecolour */
    ihdr[10] = 0; /* compression: deflate */
    ihdr[11] = 0; /* filter method: adaptive */
    ihdr[12] = 0; /* interlace: none */

    if (write_chunk(f, "IHDR", ihdr, 13) != 0) {
        goto done;
    }
    if (write_chunk(f, "IDAT", compressed, (u32) compressedLen) != 0) {
        goto done;
    }
    if (write_chunk(f, "IEND", NULL, 0) != 0) {
        goto done;
    }
    result = 0;

done:
    fclose(f);
    free(raw);
    free(compressed);
    return result;
}
