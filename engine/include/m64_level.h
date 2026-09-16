/*
 * new64 -- level construction.
 *
 * Levels are plain triangle soup handed to the collision partition.  There is
 * no separate "visual mesh" and "collision mesh": the renderer draws the same
 * triangles collision uses, which means what you see is exactly what you can
 * stand on.  For a movement testbed that property is worth more than any
 * rendering nicety, because a discrepancy between the two is otherwise
 * indistinguishable from a physics bug.
 *
 * Builders take *float* extents for convenience but round to the integer
 * lattice, because collision vertices are s16 (see m64_surface.h).
 */
#ifndef M64_LEVEL_H
#define M64_LEVEL_H

#include "m64_surface.h"
#include "m64_types.h"

/* Axis-aligned horizontal quad at height y, spanning [x0,x1] x [z0,z1]. */
void m64_level_add_floor(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y);

/* Vertical wall from (x0,z0) to (x1,z1), rising from yBottom to yTop.  The
 * solid side is the one the normal points away from; the normal comes out of
 * the left-hand side of the direction of travel from point 0 to point 1. */
void m64_level_add_wall(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBottom, f32 yTop);

/* Downward-facing quad, i.e. something you can bump your head on. */
void m64_level_add_ceiling(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y);

/*
 * Ramp rising along one axis.  `axis` is M64_X or M64_Z; the surface runs from
 * yLow at the low end of that axis to yHigh at the high end.  Walls are not
 * added -- use m64_level_add_box for a solid wedge.
 */
void m64_level_add_ramp(s16 type, s32 axis, f32 x0, f32 z0, f32 x1, f32 z1, f32 yLow,
                        f32 yHigh);

/* Closed box: top floor, four walls, and a bottom ceiling. The workhorse for
 * platforms. */
void m64_level_add_box(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBottom, f32 yTop);

/*
 * Build the demo level and report where the player should start.
 *
 * The layout is deliberately a *test rig* rather than a pretty scene: every
 * feature exists to exercise one specific mechanic, and they are spread out so
 * you can reach each one at full running speed. See the implementation for the
 * annotated map.
 */
void m64_level_build_demo(Vec3f spawnPos);

/* Named landmarks, so the demo runner can aim scripted inputs at features
 * without hardcoding coordinates in two places. */
struct M64Landmark {
    const char *name;
    Vec3f pos;
};

s32 m64_level_landmark_count(void);
const struct M64Landmark *m64_level_landmark(s32 index);
const struct M64Landmark *m64_level_landmark_by_name(const char *name);

#endif /* M64_LEVEL_H */
