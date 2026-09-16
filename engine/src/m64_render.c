/*
 * new64 -- software renderer implementation.
 *
 * Pipeline: world -> view (look-at) -> near-plane clip -> perspective divide ->
 * screen, then a barycentric scanline fill with a reciprocal-depth buffer.
 *
 * Clipping happens in view space against the near plane only.  That is the one
 * clip that is *required* (a vertex at or behind the eye has no valid
 * projection); left/right/top/bottom are handled by clamping the raster bounds,
 * which is simpler and costs nothing here.
 */
#include "m64_render.h"

#include "m64_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const u8 gM64Font8x8[64][8];

#define M64_NEAR_PLANE 10.0f

/* Current view state, set by m64_render_set_camera. */
static Mat4 sViewMatrix;
static f32 sFocalLength; /* 1 / tan(fov/2) */
static f32 sAspect;
static Vec3f sLightDir = { 0.45f, 0.80f, 0.40f };

s32 m64_fb_create(struct M64Framebuffer *fb, s32 width, s32 height) {
    fb->width = width;
    fb->height = height;
    fb->color = (u32 *) malloc((size_t) width * (size_t) height * sizeof(u32));
    fb->depth = (f32 *) malloc((size_t) width * (size_t) height * sizeof(f32));

    if (fb->color == NULL || fb->depth == NULL) {
        m64_fb_destroy(fb);
        return -1;
    }
    return 0;
}

void m64_fb_destroy(struct M64Framebuffer *fb) {
    free(fb->color);
    free(fb->depth);
    fb->color = NULL;
    fb->depth = NULL;
}

void m64_fb_clear(struct M64Framebuffer *fb, u32 color) {
    s32 i;
    s32 count = fb->width * fb->height;

    for (i = 0; i < count; i++) {
        fb->color[i] = color;
        fb->depth[i] = 0.0f; /* reciprocal depth: 0 is infinitely far */
    }
}

void m64_fb_clear_sky(struct M64Framebuffer *fb) {
    s32 x, y;

    for (y = 0; y < fb->height; y++) {
        /* Vertical gradient from a deeper blue at the top to haze at the horizon. */
        f32 t = (f32) y / (f32) (fb->height - 1);
        s32 r = (s32) (90.0f + 110.0f * t);
        s32 g = (s32) (140.0f + 90.0f * t);
        s32 b = (s32) (215.0f + 30.0f * t);
        u32 c = 0xFF000000u | ((u32) r << 16) | ((u32) g << 8) | (u32) b;

        for (x = 0; x < fb->width; x++) {
            fb->color[y * fb->width + x] = c;
            fb->depth[y * fb->width + x] = 0.0f;
        }
    }
}

void m64_render_set_camera(struct M64Framebuffer *fb, const Vec3f eye, const Vec3f focus,
                           f32 fovDeg) {
    m64_mtxf_look_at(sViewMatrix, eye, focus, 0.0f);
    sFocalLength = 1.0f / tanf(fovDeg * 0.5f * 3.14159265358979323846f / 180.0f);
    sAspect = (f32) fb->width / (f32) fb->height;
    m64_vec3f_normalize(sLightDir);
}

/* --- Triangle rasterisation --------------------------------------------- */

struct ScreenVertex {
    f32 x, y;    /* pixel coordinates */
    f32 invDepth; /* 1 / view depth */
};

static void to_view(const Vec3f world, Vec3f view) {
    Vec4f v;

    m64_mtxf_transform_point(v, sViewMatrix, world);
    view[0] = v[0];
    view[1] = v[1];
    view[2] = v[2];
}

static void project(const Vec3f view, struct ScreenVertex *out, const struct M64Framebuffer *fb) {
    /* View space looks down -Z, so depth is -view[2] and is positive in front. */
    f32 depth = -view[2];
    f32 invDepth = 1.0f / depth;

    out->x = ((view[0] * sFocalLength / sAspect) * invDepth * 0.5f + 0.5f) * (f32) fb->width;
    out->y = (0.5f - (view[1] * sFocalLength) * invDepth * 0.5f) * (f32) fb->height;
    out->invDepth = invDepth;
}

static void raster_triangle(struct M64Framebuffer *fb, const struct ScreenVertex *a,
                            const struct ScreenVertex *b, const struct ScreenVertex *c,
                            u32 color) {
    f32 area;
    s32 minX, maxX, minY, maxY;
    s32 px, py;

    /* Signed area; zero means degenerate after projection. */
    area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (area > -0.0001f && area < 0.0001f) {
        return;
    }

    minX = (s32) floorf(m64_minf(a->x, m64_minf(b->x, c->x)));
    maxX = (s32) ceilf(m64_maxf(a->x, m64_maxf(b->x, c->x)));
    minY = (s32) floorf(m64_minf(a->y, m64_minf(b->y, c->y)));
    maxY = (s32) ceilf(m64_maxf(a->y, m64_maxf(b->y, c->y)));

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > fb->width - 1) maxX = fb->width - 1;
    if (maxY > fb->height - 1) maxY = fb->height - 1;

    for (py = minY; py <= maxY; py++) {
        for (px = minX; px <= maxX; px++) {
            f32 sx = (f32) px + 0.5f;
            f32 sy = (f32) py + 0.5f;

            /* Barycentric weights, normalised by the signed area so the sign of
             * `area` (i.e. winding) does not matter. */
            f32 w0 = ((b->x - sx) * (c->y - sy) - (c->x - sx) * (b->y - sy)) / area;
            f32 w1 = ((c->x - sx) * (a->y - sy) - (a->x - sx) * (c->y - sy)) / area;
            f32 w2 = 1.0f - w0 - w1;
            f32 invDepth;
            s32 index;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }

            /* Reciprocal depth interpolates linearly in screen space. */
            invDepth = w0 * a->invDepth + w1 * b->invDepth + w2 * c->invDepth;
            index = py * fb->width + px;

            if (invDepth > fb->depth[index]) {
                fb->depth[index] = invDepth;
                fb->color[index] = color;
            }
        }
    }
}

/*
 * Clip a view-space triangle against the near plane and rasterise the result.
 *
 * Clipping a triangle against one plane yields either nothing, one triangle, or
 * a quad (which is emitted as two triangles).
 */
static void draw_view_triangle(struct M64Framebuffer *fb, const Vec3f v0, const Vec3f v1,
                               const Vec3f v2, u32 color) {
    Vec3f poly[4];
    Vec3f clipped[4];
    s32 polyCount = 3;
    s32 clippedCount = 0;
    s32 i;
    struct ScreenVertex sv[4];

    m64_vec3f_copy(poly[0], v0);
    m64_vec3f_copy(poly[1], v1);
    m64_vec3f_copy(poly[2], v2);

    for (i = 0; i < polyCount; i++) {
        const f32 *cur = poly[i];
        const f32 *next = poly[(i + 1) % polyCount];
        /* Inside means "in front of the near plane": -z >= near. */
        s32 curIn = (-cur[2] >= M64_NEAR_PLANE);
        s32 nextIn = (-next[2] >= M64_NEAR_PLANE);

        if (curIn) {
            m64_vec3f_copy(clipped[clippedCount++], cur);
        }
        if (curIn != nextIn) {
            /* Crossing the plane: interpolate the intersection point. */
            f32 t = (-cur[2] - M64_NEAR_PLANE) / ((-cur[2]) - (-next[2]));

            clipped[clippedCount][0] = cur[0] + t * (next[0] - cur[0]);
            clipped[clippedCount][1] = cur[1] + t * (next[1] - cur[1]);
            clipped[clippedCount][2] = cur[2] + t * (next[2] - cur[2]);
            clippedCount++;
        }
        if (clippedCount >= 4) {
            break;
        }
    }

    if (clippedCount < 3) {
        return;
    }

    for (i = 0; i < clippedCount; i++) {
        project(clipped[i], &sv[i], fb);
    }

    raster_triangle(fb, &sv[0], &sv[1], &sv[2], color);
    if (clippedCount == 4) {
        raster_triangle(fb, &sv[0], &sv[2], &sv[3], color);
    }
}

static void draw_world_triangle(struct M64Framebuffer *fb, const Vec3f a, const Vec3f b,
                                const Vec3f c, u32 color) {
    Vec3f va, vb, vc;

    to_view(a, va);
    to_view(b, vb);
    to_view(c, vc);
    draw_view_triangle(fb, va, vb, vc, color);
}

/* --- Shading ------------------------------------------------------------ */

static u32 shade(u32 baseColor, f32 ndotl) {
    /* Half-Lambert style wrap so back faces are dim rather than black; a fully
     * unlit face makes geometry impossible to read. */
    f32 lit = 0.42f + 0.58f * m64_clampf(ndotl, 0.0f, 1.0f);
    s32 r = (s32) (((baseColor >> 16) & 0xFF) * lit);
    s32 g = (s32) (((baseColor >> 8) & 0xFF) * lit);
    s32 b = (s32) ((baseColor & 0xFF) * lit);

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | ((u32) r << 16) | ((u32) g << 8) | (u32) b;
}

/*
 * Surface colours are a legend, not decoration: the slipperiness class of a
 * surface is invisible geometrically, so it is encoded as hue.  Being able to
 * see at a glance that a ramp is ice is worth more here than realism.
 */
static u32 surface_base_color(const struct Surface *s) {
    switch (s->type) {
        case SURFACE_VERY_SLIPPERY: return 0x9FE8FF; /* pale cyan: ice */
        case SURFACE_SLIPPERY:      return 0x7FC8E8; /* blue: slippery */
        case SURFACE_NOT_SLIPPERY:  return 0xC8A070; /* brown: grippy */
        case SURFACE_SLOW:          return 0xE8E0C0; /* sand: speed-capped */
        case SURFACE_HANGABLE:      return 0xC090D0; /* purple: hangable ceiling */
        case SURFACE_BURNING:       return 0xE05020;
        case SURFACE_DEATH_PLANE:   return 0x402040;
        default:                    break;
    }

    /* Untyped geometry is coloured by orientation so floors, walls and ceilings
     * are distinguishable at a glance. */
    if (s->normal.y > 0.7f) {
        return 0x7FB85A; /* grass green: walkable floor */
    }
    if (s->normal.y < -0.7f) {
        return 0x6A6A78; /* grey: ceiling */
    }
    return 0xB8A88F; /* stone: wall */
}

void m64_render_level(struct M64Framebuffer *fb) {
    s32 count = m64_surface_count();
    s32 i;

    for (i = 0; i < count; i++) {
        struct Surface *s = m64_surface_at(i);
        Vec3f a, b, c;
        Vec3f normal;
        f32 ndotl;
        u32 color;

        if (s == NULL) {
            continue;
        }

        a[0] = (f32) s->vertex1[0]; a[1] = (f32) s->vertex1[1]; a[2] = (f32) s->vertex1[2];
        b[0] = (f32) s->vertex2[0]; b[1] = (f32) s->vertex2[1]; b[2] = (f32) s->vertex2[2];
        c[0] = (f32) s->vertex3[0]; c[1] = (f32) s->vertex3[1]; c[2] = (f32) s->vertex3[2];

        normal[0] = s->normal.x;
        normal[1] = s->normal.y;
        normal[2] = s->normal.z;
        ndotl = m64_vec3f_dot(normal, sLightDir);

        color = shade(surface_base_color(s), ndotl);
        draw_world_triangle(fb, a, b, c, color);
    }
}

/* --- Capsule ------------------------------------------------------------ */

#define CAPSULE_SEGMENTS 16
#define CAPSULE_RINGS 6

/*
 * The capsule stands in for the player.  Its dimensions are the collision
 * dimensions, not an arbitrary size: the body is 160 units tall, which is
 * exactly the headroom the step code demands, so if the capsule fits through a
 * gap on screen then the physics agrees it fits.
 */
void m64_render_capsule(struct M64Framebuffer *fb, const Vec3f pos, s16 yaw, f32 radius,
                        f32 height, u32 color) {
    f32 cylBottom = radius;
    f32 cylTop = height - radius;
    s32 i, j;

    if (cylTop < cylBottom) {
        cylTop = cylBottom;
    }

    /* Barrel. */
    for (i = 0; i < CAPSULE_SEGMENTS; i++) {
        f32 a0 = 2.0f * 3.14159265f * (f32) i / (f32) CAPSULE_SEGMENTS;
        f32 a1 = 2.0f * 3.14159265f * (f32) (i + 1) / (f32) CAPSULE_SEGMENTS;
        f32 c0 = cosf(a0), s0 = sinf(a0);
        f32 c1 = cosf(a1), s1 = sinf(a1);
        Vec3f p0 = { pos[0] + c0 * radius, pos[1] + cylBottom, pos[2] + s0 * radius };
        Vec3f p1 = { pos[0] + c1 * radius, pos[1] + cylBottom, pos[2] + s1 * radius };
        Vec3f p2 = { pos[0] + c1 * radius, pos[1] + cylTop, pos[2] + s1 * radius };
        Vec3f p3 = { pos[0] + c0 * radius, pos[1] + cylTop, pos[2] + s0 * radius };
        Vec3f normal = { (c0 + c1) * 0.5f, 0.0f, (s0 + s1) * 0.5f };
        u32 shaded;

        m64_vec3f_normalize(normal);
        shaded = shade(color, m64_vec3f_dot(normal, sLightDir));

        draw_world_triangle(fb, p0, p1, p2, shaded);
        draw_world_triangle(fb, p0, p2, p3, shaded);
    }

    /* Hemispherical caps. */
    for (j = 0; j < CAPSULE_RINGS; j++) {
        f32 t0 = (f32) j / (f32) CAPSULE_RINGS;
        f32 t1 = (f32) (j + 1) / (f32) CAPSULE_RINGS;
        f32 e0 = t0 * 3.14159265f * 0.5f;
        f32 e1 = t1 * 3.14159265f * 0.5f;

        for (i = 0; i < CAPSULE_SEGMENTS; i++) {
            f32 a0 = 2.0f * 3.14159265f * (f32) i / (f32) CAPSULE_SEGMENTS;
            f32 a1 = 2.0f * 3.14159265f * (f32) (i + 1) / (f32) CAPSULE_SEGMENTS;
            s32 cap;

            for (cap = 0; cap < 2; cap++) {
                f32 baseY = cap == 0 ? cylTop : cylBottom;
                f32 dir = cap == 0 ? 1.0f : -1.0f;
                f32 r0 = cosf(e0) * radius, y0 = sinf(e0) * radius * dir;
                f32 r1 = cosf(e1) * radius, y1 = sinf(e1) * radius * dir;
                Vec3f p0 = { pos[0] + cosf(a0) * r0, pos[1] + baseY + y0,
                             pos[2] + sinf(a0) * r0 };
                Vec3f p1 = { pos[0] + cosf(a1) * r0, pos[1] + baseY + y0,
                             pos[2] + sinf(a1) * r0 };
                Vec3f p2 = { pos[0] + cosf(a1) * r1, pos[1] + baseY + y1,
                             pos[2] + sinf(a1) * r1 };
                Vec3f p3 = { pos[0] + cosf(a0) * r1, pos[1] + baseY + y1,
                             pos[2] + sinf(a0) * r1 };
                Vec3f normal = { (p0[0] + p2[0]) * 0.5f - pos[0],
                                 (p0[1] + p2[1]) * 0.5f - (pos[1] + baseY),
                                 (p0[2] + p2[2]) * 0.5f - pos[2] };
                u32 shaded;

                m64_vec3f_normalize(normal);
                shaded = shade(color, m64_vec3f_dot(normal, sLightDir));

                draw_world_triangle(fb, p0, p1, p2, shaded);
                draw_world_triangle(fb, p0, p2, p3, shaded);
            }
        }
    }

    /*
     * Facing wedge.  A capsule is rotationally symmetric, so without this the
     * rendered yaw -- a real physics quantity that drives all ground movement --
     * would be invisible.
     */
    {
        f32 s = m64_sins(yaw), c = m64_coss(yaw);
        f32 noseY = pos[1] + height * 0.62f;
        Vec3f tip = { pos[0] + s * (radius + 38.0f), noseY, pos[2] + c * (radius + 38.0f) };
        Vec3f l = { pos[0] + m64_sins((s16) (yaw + 0x3000)) * radius * 0.9f,
                    noseY + 16.0f,
                    pos[2] + m64_coss((s16) (yaw + 0x3000)) * radius * 0.9f };
        Vec3f r = { pos[0] + m64_sins((s16) (yaw - 0x3000)) * radius * 0.9f,
                    noseY + 16.0f,
                    pos[2] + m64_coss((s16) (yaw - 0x3000)) * radius * 0.9f };
        Vec3f b = { pos[0] + m64_sins((s16) (yaw + 0x8000)) * radius * 0.2f,
                    noseY - 16.0f,
                    pos[2] + m64_coss((s16) (yaw + 0x8000)) * radius * 0.2f };

        draw_world_triangle(fb, tip, l, r, 0xFFFFE8B0);
        draw_world_triangle(fb, tip, r, b, 0xFFE8D090);
        draw_world_triangle(fb, tip, b, l, 0xFFE8D090);
        (void) s;
        (void) c;
    }
}

/* --- Debug primitives --------------------------------------------------- */

void m64_render_line(struct M64Framebuffer *fb, const Vec3f a, const Vec3f b, u32 color) {
    Vec3f va, vb;
    struct ScreenVertex sa, sb;
    f32 dx, dy, steps;
    s32 i;

    to_view(a, va);
    to_view(b, vb);

    /* Reject rather than clip: debug lines are short and near the subject. */
    if (-va[2] < M64_NEAR_PLANE || -vb[2] < M64_NEAR_PLANE) {
        return;
    }
    project(va, &sa, fb);
    project(vb, &sb, fb);

    dx = sb.x - sa.x;
    dy = sb.y - sa.y;
    steps = m64_maxf(fabsf(dx), fabsf(dy));
    if (steps < 1.0f) {
        steps = 1.0f;
    }

    for (i = 0; i <= (s32) steps; i++) {
        f32 t = (f32) i / steps;
        s32 px = (s32) (sa.x + dx * t);
        s32 py = (s32) (sa.y + dy * t);
        f32 invDepth = sa.invDepth + (sb.invDepth - sa.invDepth) * t;

        if (px < 0 || py < 0 || px >= fb->width || py >= fb->height) {
            continue;
        }
        /* Bias toward the camera so a line lying on a surface is still visible. */
        if (invDepth * 1.02f > fb->depth[py * fb->width + px]) {
            fb->color[py * fb->width + px] = color;
        }
    }
}

void m64_render_marker(struct M64Framebuffer *fb, const Vec3f pos, u32 color) {
    Vec3f a, b;

    m64_vec3f_copy(a, pos);
    m64_vec3f_copy(b, pos);
    a[1] += 10.0f;
    b[1] += 120.0f;
    m64_render_line(fb, a, b, color);
}

/* --- Overlay ------------------------------------------------------------ */

void m64_render_rect(struct M64Framebuffer *fb, s32 x, s32 y, s32 w, s32 h, u32 color) {
    s32 px, py;

    for (py = y; py < y + h; py++) {
        if (py < 0 || py >= fb->height) {
            continue;
        }
        for (px = x; px < x + w; px++) {
            if (px < 0 || px >= fb->width) {
                continue;
            }
            fb->color[py * fb->width + px] = color;
        }
    }
}

void m64_render_rect_blend(struct M64Framebuffer *fb, s32 x, s32 y, s32 w, s32 h,
                           u32 color, s32 alpha) {
    s32 px, py;

    for (py = y; py < y + h; py++) {
        if (py < 0 || py >= fb->height) {
            continue;
        }
        for (px = x; px < x + w; px++) {
            u32 dst;
            s32 r, g, b;

            if (px < 0 || px >= fb->width) {
                continue;
            }
            dst = fb->color[py * fb->width + px];
            r = (s32) (((color >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * (255 - alpha)) / 255;
            g = (s32) (((color >> 8) & 0xFF) * alpha + ((dst >> 8) & 0xFF) * (255 - alpha)) / 255;
            b = (s32) ((color & 0xFF) * alpha + (dst & 0xFF) * (255 - alpha)) / 255;
            fb->color[py * fb->width + px] =
                0xFF000000u | ((u32) r << 16) | ((u32) g << 8) | (u32) b;
        }
    }
}

void m64_render_text(struct M64Framebuffer *fb, s32 x, s32 y, s32 scale, u32 color,
                     const char *text) {
    s32 cursorX = x;
    const char *p;

    if (scale < 1) {
        scale = 1;
    }

    for (p = text; *p != '\0'; p++) {
        s32 ch = (u8) *p;
        s32 row;

        if (ch == '\n') {
            cursorX = x;
            y += 9 * scale;
            continue;
        }

        /* Lowercase folds to uppercase: the font only carries 32..95. */
        if (ch >= 'a' && ch <= 'z') {
            ch -= 32;
        }
        if (ch < 32 || ch > 95) {
            ch = '?';
        }

        for (row = 0; row < 8; row++) {
            u8 bits = gM64Font8x8[ch - 32][row];
            s32 col;

            for (col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    m64_render_rect(fb, cursorX + col * scale, y + row * scale, scale,
                                    scale, color);
                }
            }
        }
        cursorX += 6 * scale;
    }
}
