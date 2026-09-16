/*
 * new64 -- software renderer.
 *
 * A z-buffered flat-shaded triangle rasteriser with no external dependencies.
 * It exists to make the physics *inspectable*, so it favours legibility over
 * looks: collision triangles are drawn directly, surface types are colour-coded,
 * and there is a text overlay for the live state readout.  A frame that looks
 * wrong should mean the physics is wrong, not that the renderer took a liberty.
 */
#ifndef M64_RENDER_H
#define M64_RENDER_H

#include "m64_surface.h"
#include "m64_types.h"

struct M64Framebuffer {
    s32 width;
    s32 height;
    u32 *color; /* 0xAARRGGBB */
    f32 *depth; /* reciprocal view depth; larger is nearer */
};

s32 m64_fb_create(struct M64Framebuffer *fb, s32 width, s32 height);
void m64_fb_destroy(struct M64Framebuffer *fb);
void m64_fb_clear(struct M64Framebuffer *fb, u32 color);

/* Sky gradient, so the horizon reads without needing a skybox. */
void m64_fb_clear_sky(struct M64Framebuffer *fb);

/*
 * Set the view for subsequent draws.  fovDeg is the vertical field of view.
 * Everything downstream is in world space.
 */
void m64_render_set_camera(struct M64Framebuffer *fb, const Vec3f eye, const Vec3f focus,
                           f32 fovDeg);

/* Draw every collision triangle in the world, coloured by surface type. */
void m64_render_level(struct M64Framebuffer *fb);

/*
 * The player: a capsule of the given radius and height, plus a facing wedge so
 * the rendered yaw is readable (a capsule alone cannot show which way it faces,
 * and facing is a physics quantity worth seeing).
 */
void m64_render_capsule(struct M64Framebuffer *fb, const Vec3f pos, s16 yaw, f32 radius,
                        f32 height, u32 color);

/* World-space line, depth tested. Used for velocity and normal debug vectors. */
void m64_render_line(struct M64Framebuffer *fb, const Vec3f a, const Vec3f b, u32 color);

/* Marks a world position with a small cross that ignores depth, for landmarks. */
void m64_render_marker(struct M64Framebuffer *fb, const Vec3f pos, u32 color);

/* --- Overlay (screen space, no depth) ----------------------------------- */

void m64_render_text(struct M64Framebuffer *fb, s32 x, s32 y, s32 scale, u32 color,
                     const char *text);
void m64_render_rect(struct M64Framebuffer *fb, s32 x, s32 y, s32 w, s32 h, u32 color);
/* Semi-transparent fill, for making overlay text readable over the scene. */
void m64_render_rect_blend(struct M64Framebuffer *fb, s32 x, s32 y, s32 w, s32 h,
                           u32 color, s32 alpha);

#endif /* M64_RENDER_H */
