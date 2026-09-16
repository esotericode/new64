/*
 * new64 -- image output.
 *
 * PNG for stills and GIF for animation.  Both are written by hand (PNG over
 * zlib, GIF including its LZW coder) so the engine needs no image library: a
 * movement testbed that can only be verified by a human watching a window is
 * much less useful than one that can emit frames from a headless build.
 */
#ifndef M64_IMAGE_H
#define M64_IMAGE_H

#include "m64_types.h"

/* Pixels are 0xAARRGGBB in host order. */
s32 m64_png_write(const char *path, const u32 *pixels, s32 width, s32 height);

/*
 * Animated GIF.  Frames are quantised to a fixed 6x6x6 colour cube plus a grey
 * ramp, which is plenty for flat-shaded geometry and avoids needing a per-frame
 * palette pass (which would make the animation shimmer).
 */
struct M64Gif;

struct M64Gif *m64_gif_begin(const char *path, s32 width, s32 height, s32 delayCs);
s32 m64_gif_add_frame(struct M64Gif *gif, const u32 *pixels);
s32 m64_gif_end(struct M64Gif *gif);

#endif /* M64_IMAGE_H */
