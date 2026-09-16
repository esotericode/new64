/*
 * new64 -- surface (collision triangle) storage and queries.
 *
 * This is the single most fidelity-critical file in the engine.  The movement
 * state machine is only as faithful as the ground under it, and SM64's
 * collision has a number of behaviours that are *not* what a modern engine
 * would do by accident.  They are reproduced deliberately here; each one is
 * called out at its implementation site in m64_surface.c.  The short list:
 *
 *   1. Query positions are truncated to s16 before any triangle test.  Sub-unit
 *      horizontal motion therefore does not change which triangle you are on,
 *      and floor height is sampled on integer coordinates only.
 *   2. Floor queries return the *first* triangle in a cell's list that contains
 *      the point and is not too far above, not the closest one.  Overlapping
 *      geometry resolves by load order.
 *   3. Floors are ignored if they are more than 78 units above the query point;
 *      ceilings if more than 78 below.  This is what lets you step up onto a
 *      surface slightly above your feet.
 *   4. Wall pushout accumulates across every wall hit in one query, but each
 *      wall is tested against the *original* position, not the pushed one.
 *   5. A wall triangle is tested in either the XY or the ZY plane depending on
 *      whether its normal leans more along X or Z.  Seams between two walls
 *      that disagree are the origin of SM64's wall-clip quirks.
 *   6. Triangles are binned into partition cells with a 50-unit slack at cell
 *      borders, because queries are a point but wall checks have a radius. The
 *      slack equals the largest wall radius, so it only just suffices -- walls
 *      right on a cell border can still be missed.
 *
 * Deviating from any of these makes slotted-in movement code behave subtly
 * differently from the original, usually in ways that only show up in
 * frame-perfect tricks.
 */
#ifndef M64_SURFACE_H
#define M64_SURFACE_H

#include "m64_types.h"

/* --- World partition geometry ------------------------------------------- */

#define CELL_SIZE          0x400  /* 1024 units on a side */
#define LEVEL_BOUNDARY_MAX 0x2000 /* world spans [-8192, 8192] on X and Z */
#define NUM_CELLS          (2 * LEVEL_BOUNDARY_MAX / CELL_SIZE) /* 16 */
#define NUM_CELLS_INDEX    (NUM_CELLS - 1)

#define SPATIAL_PARTITION_FLOORS 0
#define SPATIAL_PARTITION_CEILS  1
#define SPATIAL_PARTITION_WALLS  2
#define SPATIAL_PARTITION_COUNT  3

/* Returned when no floor exists below a point: the "you are over the void" Y. */
#define FLOOR_LOWER_LIMIT      -11000.0f
#define FLOOR_LOWER_LIMIT_MISC (FLOOR_LOWER_LIMIT + 1000.0f)
#define CEIL_UPPER_LIMIT       20000.0f

/*
 * How far above the query point a floor may still count, and how far below a
 * ceiling may still count.  Sign convention matches the comparisons in the
 * implementation, where the floor buffer is added as a negative number.
 */
#define FIND_FLOOR_BUFFER (-78.0f)
#define FIND_CEIL_BUFFER  (78.0f)

/* --- Surface terrain types ---------------------------------------------- */
/*
 * Only the types the engine actually reacts to are given names.  The numeric
 * values follow the N64 titles' terrain encoding so that level data authored
 * against that encoding keeps its meaning.  Unlisted values behave as
 * SURFACE_DEFAULT.
 */
#define SURFACE_DEFAULT            0x0000
#define SURFACE_BURNING            0x0001
#define SURFACE_HANGABLE           0x0005
#define SURFACE_SLOW               0x0009
#define SURFACE_DEATH_PLANE        0x000A
#define SURFACE_CLOSE_CAMERA       0x000B
#define SURFACE_WATER              0x000D
#define SURFACE_FLOWING_WATER      0x000E
#define SURFACE_INTANGIBLE         0x0012
#define SURFACE_VERY_SLIPPERY      0x0013
#define SURFACE_SLIPPERY           0x0014
#define SURFACE_NOT_SLIPPERY       0x0015
#define SURFACE_SHALLOW_QUICKSAND  0x0021
#define SURFACE_DEEP_QUICKSAND     0x0022
#define SURFACE_INSTANT_QUICKSAND  0x0023
#define SURFACE_HARD               0x0030
#define SURFACE_WARP               0x0032
#define SURFACE_HARD_SLIPPERY      0x0035
#define SURFACE_HARD_VERY_SLIPPERY 0x0036
#define SURFACE_HARD_NOT_SLIPPERY  0x0037
#define SURFACE_VERTICAL_WIND      0x0038
#define SURFACE_WALL_MISC          0x0028
#define SURFACE_SWITCH             0x003A
#define SURFACE_ICE                0x002E
#define SURFACE_NOISE_DEFAULT      0x00A0
#define SURFACE_NOISE_SLIPPERY     0x00A1
#define SURFACE_HORIZONTAL_WIND    0x00A7
#define SURFACE_NOISE_VERY_SLIPPERY_73 0x0073
#define SURFACE_NOISE_VERY_SLIPPERY_74 0x0074
#define SURFACE_NOISE_VERY_SLIPPERY 0x0075
#define SURFACE_NO_CAM_COL_SLIPPERY 0x0079
#define SURFACE_NO_CAM_COL_VERY_SLIPPERY 0x007A

/* Slipperiness classes.  These drive every slope decision in the movement
 * code: what counts as a slope, what counts as too steep to stand on, and how
 * hard gravity drags you along it. */
/*
 * These deliberately share numeric values with the corresponding SURFACE_*
 * terrain types above.  Hosted code switches on a *class* using terrain-type
 * names (`case SURFACE_VERY_SLIPPERY:` applied to the result of
 * mario_get_floor_class()), so the two spellings have to be interchangeable.
 * Renumbering them independently would silently misclassify every slope.
 */
#define SURFACE_CLASS_DEFAULT       0x0000
#define SURFACE_CLASS_VERY_SLIPPERY SURFACE_VERY_SLIPPERY /* 0x0013 */
#define SURFACE_CLASS_SLIPPERY      SURFACE_SLIPPERY      /* 0x0014 */
#define SURFACE_CLASS_NOT_SLIPPERY  SURFACE_NOT_SLIPPERY  /* 0x0015 */

/* --- Surface flags ------------------------------------------------------ */

#define SURFACE_FLAG_DYNAMIC          (1 << 0)
#define SURFACE_FLAG_NO_CAM_COLLISION (1 << 1)
#define SURFACE_FLAG_X_PROJECTION     (1 << 3)

/* --- Types -------------------------------------------------------------- */

struct Object; /* forward declaration; dynamic surfaces carry their owner */

/*
 * A single collision triangle.  Field names and order match what hosted
 * movement code expects to be able to read directly.
 *
 * Vertices are s16: SM64 collision geometry is on an integer lattice, and the
 * triangle-containment tests below depend on that (they run in integer
 * arithmetic, which is how they stay exact on triangle seams).
 */
struct Surface {
    s16 type;
    s16 force;
    s8 flags;
    s8 room;
    s16 lowerY; /* min vertex Y, minus a small slack */
    s16 upperY; /* max vertex Y, plus a small slack */
    Vec3s vertex1;
    Vec3s vertex2;
    Vec3s vertex3;
    struct {
        f32 x, y, z;
    } normal;
    f32 originOffset; /* plane equation constant: dot(normal, vertex1) negated */
    struct Object *object;
};

/* Intrusive singly-linked list threading surfaces through partition cells. */
struct SurfaceNode {
    struct SurfaceNode *next;
    struct Surface *surface;
};

/*
 * In/out parameter block for wall queries.  x/y/z go in as the position to
 * test and come out *displaced* by the accumulated pushout of every wall hit.
 */
struct WallCollisionData {
    f32 x, y, z;
    f32 offsetY; /* probe height above the given y */
    f32 radius;
    s16 numWalls;
    struct Surface *walls[4];
};

/* --- Lifecycle ---------------------------------------------------------- */

void m64_surface_pool_init(void);

/*
 * Append a triangle to the world.  Vertices are given in the winding order
 * that makes the normal point away from the solid side (counter-clockwise seen
 * from outside).  Returns the surface, or NULL if it was degenerate or the pool
 * is full.
 */
struct Surface *m64_surface_add_static(s16 type, const Vec3s v1, const Vec3s v2,
                                       const Vec3s v3);

/* Same, but placed in the dynamic partition, which is rebuilt every frame. */
struct Surface *m64_surface_add_dynamic(s16 type, const Vec3s v1, const Vec3s v2,
                                        const Vec3s v3, struct Object *owner);

/* Drops every dynamic surface; call once per frame before re-adding them. */
void m64_surface_clear_dynamic(void);

/* --- Queries ------------------------------------------------------------ */

f32 m64_find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor);
f32 m64_find_ceil(f32 x, f32 y, f32 z, struct Surface **pceil);
s32 m64_find_wall_collisions(struct WallCollisionData *colData);

/* Floor height only, ignoring which triangle it was. */
f32 m64_find_floor_height(f32 x, f32 y, f32 z);

/* Water is a flat plane per region in this engine; -11000 means "no water". */
f32 m64_find_water_level(f32 x, f32 z);
void m64_set_water_level(f32 level);

/* Iteration for the renderer and for tests. */
s32 m64_surface_count(void);
struct Surface *m64_surface_at(s32 index);

/* Classify a surface's slipperiness.  Hosted code reaches this through
 * mario_get_floor_class(). */
s32 m64_surface_class(const struct Surface *surface);

#endif /* M64_SURFACE_H */
