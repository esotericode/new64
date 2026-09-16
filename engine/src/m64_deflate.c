/*
 * new64 -- self-contained deflate, adler32 and crc32.
 *
 * This exists so the engine has no external dependencies at all.  The only
 * thing zlib was used for was compressing PNG scanlines, and carrying a library
 * dependency for that means anyone rebuilding -- particularly on Windows with
 * MSVC, where there is no package manager to lean on -- has to go and find it
 * first, and any binary handed out risks needing a DLL alongside it.
 *
 * The implementation is LZ77 with fixed Huffman codes (RFC 1951 BTYPE=01).
 * Fixed rather than dynamic Huffman because a dynamic table costs a few hundred
 * lines of tree construction to save maybe 10% on images that are already flat
 * colour; the tradeoff is not worth it here.
 *
 * Bit order is the fiddly part of this format and worth stating plainly:
 * the stream is written least-significant-bit first, but Huffman codes are
 * defined most-significant-bit first, so codes have to be emitted MSB-to-LSB
 * into an LSB-first stream.  Everything else follows from that.
 */
#include "m64_deflate.h"

#include <stdlib.h>
#include <string.h>

/* --- Checksums ---------------------------------------------------------- */

u32 m64_adler32(const u8 *data, size_t len) {
    u32 a = 1, b = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        a += data[i];
        if (a >= 65521u) {
            a -= 65521u;
        }
        b += a;
        if (b >= 65521u) {
            b -= 65521u;
        }
    }
    return (b << 16) | a;
}

static u32 sCrcTable[256];
static s32 sCrcTableReady;

static void crc_table_init(void) {
    u32 n, k, c;

    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        sCrcTable[n] = c;
    }
    sCrcTableReady = TRUE;
}

u32 m64_crc32(u32 crc, const u8 *data, size_t len) {
    u32 c = crc ^ 0xFFFFFFFFu;
    size_t i;

    if (!sCrcTableReady) {
        crc_table_init();
    }
    for (i = 0; i < len; i++) {
        c = sCrcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* --- Bit output --------------------------------------------------------- */

struct BitWriter {
    u8 *buf;
    size_t cap;
    size_t len;
    u32 bitBuf;
    s32 bitCount;
    s32 overflow;
};

static void bw_byte(struct BitWriter *w, u8 value) {
    if (w->len >= w->cap) {
        w->overflow = TRUE;
        return;
    }
    w->buf[w->len++] = value;
}

/* Writes `count` bits of `value`, least significant bit first. */
static void bw_bits(struct BitWriter *w, u32 value, s32 count) {
    w->bitBuf |= (value & ((1u << count) - 1u)) << w->bitCount;
    w->bitCount += count;

    while (w->bitCount >= 8) {
        bw_byte(w, (u8) (w->bitBuf & 0xFF));
        w->bitBuf >>= 8;
        w->bitCount -= 8;
    }
}

/*
 * Writes a Huffman code, which is defined most significant bit first, into a
 * stream that is least significant bit first -- so the bits come out reversed
 * relative to bw_bits().
 */
static void bw_code(struct BitWriter *w, u32 code, s32 count) {
    s32 i;

    for (i = count - 1; i >= 0; i--) {
        bw_bits(w, (code >> i) & 1u, 1);
    }
}

static void bw_flush(struct BitWriter *w) {
    if (w->bitCount > 0) {
        bw_byte(w, (u8) (w->bitBuf & 0xFF));
        w->bitBuf = 0;
        w->bitCount = 0;
    }
}

/* --- Fixed Huffman tables ----------------------------------------------- */

/*
 * Literal/length alphabet, per RFC 1951 section 3.2.6:
 *   0..143   8 bits, starting at 0x30
 *   144..255 9 bits, starting at 0x190
 *   256..279 7 bits, starting at 0x000
 *   280..287 8 bits, starting at 0xC0
 */
static void emit_literal(struct BitWriter *w, s32 symbol) {
    if (symbol <= 143) {
        bw_code(w, (u32) (0x30 + symbol), 8);
    } else {
        bw_code(w, (u32) (0x190 + symbol - 144), 9);
    }
}

static void emit_length_symbol(struct BitWriter *w, s32 symbol) {
    if (symbol <= 279) {
        bw_code(w, (u32) (symbol - 256), 7);
    } else {
        bw_code(w, (u32) (0xC0 + symbol - 280), 8);
    }
}

/* Length codes 257..285 -> base length and extra bit count. */
static const u16 sLengthBase[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                     15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                     67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 sLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

/* Distance codes 0..29 -> base distance and extra bit count. */
static const u16 sDistBase[30] = { 1,    2,    3,    4,    5,    7,     9,    13,
                                   17,   25,   33,   49,   65,   97,    129,  193,
                                   257,  385,  513,  769,  1025, 1537,  2049, 3073,
                                   4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 sDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void emit_match(struct BitWriter *w, s32 length, s32 distance) {
    s32 i;

    for (i = 28; i >= 0; i--) {
        if (length >= sLengthBase[i]) {
            emit_length_symbol(w, 257 + i);
            if (sLengthExtra[i] > 0) {
                bw_bits(w, (u32) (length - sLengthBase[i]), sLengthExtra[i]);
            }
            break;
        }
    }
    for (i = 29; i >= 0; i--) {
        if (distance >= sDistBase[i]) {
            /* Distance codes use a fixed 5-bit encoding, MSB first. */
            bw_code(w, (u32) i, 5);
            if (sDistExtra[i] > 0) {
                bw_bits(w, (u32) (distance - sDistBase[i]), sDistExtra[i]);
            }
            break;
        }
    }
}

/* --- LZ77 --------------------------------------------------------------- */

#define WINDOW_SIZE  32768
#define MIN_MATCH    3
#define MAX_MATCH    258
#define HASH_BITS    15
#define HASH_SIZE    (1 << HASH_BITS)
#define MAX_CHAIN    128 /* cap on chain walking: bounds worst-case time */

static u32 hash3(const u8 *p) {
    return (((u32) p[0] << 10) ^ ((u32) p[1] << 5) ^ (u32) p[2]) & (HASH_SIZE - 1);
}

/*
 * Compress `src` into a zlib stream (RFC 1950 header, deflate body, adler32).
 * Returns the byte count written, or 0 on failure.
 */
size_t m64_zlib_compress(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap) {
    struct BitWriter w;
    s32 *head = NULL;
    s32 *prev = NULL;
    size_t pos = 0;
    u32 adler;
    s32 i;

    if (dstCap < 6) {
        return 0;
    }

    memset(&w, 0, sizeof(w));
    w.buf = dst;
    w.cap = dstCap;

    /* zlib header: deflate, 32K window, default compression, no preset dict.
     * The two bytes must satisfy (CMF<<8 | FLG) % 31 == 0; 0x78 0x9C does. */
    bw_byte(&w, 0x78);
    bw_byte(&w, 0x9C);

    head = (s32 *) malloc(sizeof(s32) * HASH_SIZE);
    prev = (s32 *) malloc(sizeof(s32) * WINDOW_SIZE);
    if (head == NULL || prev == NULL) {
        free(head);
        free(prev);
        return 0;
    }
    for (i = 0; i < HASH_SIZE; i++) {
        head[i] = -1;
    }
    for (i = 0; i < WINDOW_SIZE; i++) {
        prev[i] = -1;
    }

    /* A single final block with fixed Huffman codes. */
    bw_bits(&w, 1, 1); /* BFINAL */
    bw_bits(&w, 1, 2); /* BTYPE = 01, fixed Huffman */

    while (pos < srcLen && !w.overflow) {
        s32 bestLen = 0;
        s32 bestDist = 0;

        if (pos + MIN_MATCH <= srcLen) {
            u32 h = hash3(src + pos);
            s32 candidate = head[h];
            s32 chain = 0;

            while (candidate >= 0 && chain < MAX_CHAIN) {
                s32 dist = (s32) (pos - (size_t) candidate);
                s32 len = 0;
                s32 maxLen;

                if (dist <= 0 || dist > WINDOW_SIZE) {
                    break;
                }

                maxLen = (s32) (srcLen - pos);
                if (maxLen > MAX_MATCH) {
                    maxLen = MAX_MATCH;
                }
                while (len < maxLen && src[candidate + len] == src[pos + len]) {
                    len++;
                }

                if (len > bestLen) {
                    bestLen = len;
                    bestDist = dist;
                    /* A maximal match cannot be improved on. */
                    if (len >= MAX_MATCH) {
                        break;
                    }
                }
                candidate = prev[candidate & (WINDOW_SIZE - 1)];
                chain++;
            }
        }

        if (bestLen >= MIN_MATCH) {
            s32 k;

            emit_match(&w, bestLen, bestDist);
            /* Insert every position the match covers, so later matches can
             * start inside it. */
            for (k = 0; k < bestLen; k++) {
                if (pos + MIN_MATCH <= srcLen) {
                    u32 h = hash3(src + pos);

                    prev[pos & (WINDOW_SIZE - 1)] = head[h];
                    head[h] = (s32) pos;
                }
                pos++;
            }
        } else {
            emit_literal(&w, src[pos]);
            if (pos + MIN_MATCH <= srcLen) {
                u32 h = hash3(src + pos);

                prev[pos & (WINDOW_SIZE - 1)] = head[h];
                head[h] = (s32) pos;
            }
            pos++;
        }
    }

    emit_length_symbol(&w, 256); /* end of block */
    bw_flush(&w);

    free(head);
    free(prev);

    if (w.overflow) {
        return 0;
    }

    /* zlib trailer: adler32 of the *uncompressed* data, big endian. */
    adler = m64_adler32(src, srcLen);
    bw_byte(&w, (u8) (adler >> 24));
    bw_byte(&w, (u8) (adler >> 16));
    bw_byte(&w, (u8) (adler >> 8));
    bw_byte(&w, (u8) adler);

    return w.overflow ? 0 : w.len;
}

size_t m64_compress_bound(size_t srcLen) {
    /* Worst case is every byte emitted as a 9-bit literal, plus headers and a
     * little slack. */
    return srcLen + (srcLen / 8) + 128;
}
