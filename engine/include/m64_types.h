/*
 * new64 -- core scalar / vector types.
 *
 * The engine deliberately speaks the same scalar vocabulary as the N64 titles
 * it hosts: 32-bit floats for world space, and *signed 16-bit angles* for all
 * rotation.  Every angle in the engine is an s16 where 0x10000 == one full
 * turn, so 0x4000 == 90 degrees.  Angles wrap for free on overflow, which is
 * what makes "shortest turn" arithmetic a plain subtraction.
 */
#ifndef M64_TYPES_H
#define M64_TYPES_H

#include <PR/ultratypes.h>

typedef f32 Vec3f[3];
typedef s16 Vec3s[3];
typedef s32 Vec3i[3];
typedef f32 Vec4f[4];
typedef f32 Mat4[4][4];

/* Index names for the Vec3* arrays.  Y is up, as on the N64. */
enum { M64_X = 0, M64_Y = 1, M64_Z = 2 };

#endif /* M64_TYPES_H */
