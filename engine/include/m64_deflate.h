/*
 * new64 -- self-contained deflate and checksums.
 *
 * Replaces the engine's only external dependency (zlib), so that the project
 * builds anywhere a C compiler exists and the shipped executables need no DLLs.
 * See m64_deflate.c for the implementation notes.
 */
#ifndef M64_DEFLATE_H
#define M64_DEFLATE_H

#include "m64_types.h"

#include <stddef.h>

/* Compress into a zlib stream (RFC 1950). Returns bytes written, 0 on failure. */
size_t m64_zlib_compress(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap);

/* Safe output buffer size for the above. */
size_t m64_compress_bound(size_t srcLen);

u32 m64_adler32(const u8 *data, size_t len);
u32 m64_crc32(u32 crc, const u8 *data, size_t len);

#endif /* M64_DEFLATE_H */
