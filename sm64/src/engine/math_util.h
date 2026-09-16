/*
 * new64 -- math entry points under the names hosted code calls them.
 *
 * Every one of these forwards to the engine's m64_* implementation.  The
 * indirection is deliberate: it keeps the engine's own headers free of the
 * hosting vocabulary, while guaranteeing that hosted code and engine code do
 * the arithmetic *identically* rather than through two similar-looking copies.
 */
#ifndef NEW64_MATH_UTIL_H
#define NEW64_MATH_UTIL_H

#include "types.h"
#include "m64_math.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(x)  ((x) > 0 ? (x) : -(x))
#define ABSI(x) ((x) > 0 ? (x) : -(x))
#define SQ(x)   ((x) * (x))

/* Trig over s16 angles.  Table lookups, not libm -- see m64_math.h. */
static inline f32 sins(s16 angle) { return m64_sins(angle); }
static inline f32 coss(s16 angle) { return m64_coss(angle); }

/* Note the (z, x) argument order; 0 is +Z and 0x4000 is +X. */
static inline s16 atan2s(f32 z, f32 x) { return m64_atan2s(z, x); }
static inline f32 atan2f(f32 z, f32 x) { return m64_atan2f(z, x); }

static inline s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    return m64_approach_s32(current, target, inc, dec);
}
static inline f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec) {
    return m64_approach_f32(current, target, inc, dec);
}

static inline void vec3f_copy(Vec3f dest, const Vec3f src) { m64_vec3f_copy(dest, src); }
static inline void vec3f_set(Vec3f dest, f32 x, f32 y, f32 z) { m64_vec3f_set(dest, x, y, z); }
static inline void vec3f_add(Vec3f dest, const Vec3f a) { m64_vec3f_add(dest, a); }
static inline void vec3f_sum(Vec3f dest, const Vec3f a, const Vec3f b) {
    dest[0] = a[0] + b[0];
    dest[1] = a[1] + b[1];
    dest[2] = a[2] + b[2];
}
static inline f32 vec3f_dot(const Vec3f a, const Vec3f b) { return m64_vec3f_dot(a, b); }
static inline f32 vec3f_length(const Vec3f a) { return m64_vec3f_length(a); }
static inline void vec3f_normalize(Vec3f v) { m64_vec3f_normalize(v); }

static inline void vec3s_copy(Vec3s dest, const Vec3s src) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
}
static inline void vec3s_set(Vec3s dest, s16 x, s16 y, s16 z) { m64_vec3s_set(dest, x, y, z); }
static inline void vec3s_add(Vec3s dest, const Vec3s a) {
    dest[0] += a[0];
    dest[1] += a[1];
    dest[2] += a[2];
}

#endif /* NEW64_MATH_UTIL_H */
