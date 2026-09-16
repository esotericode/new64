/*
 * new64 -- collision entry points under the names hosted code calls them.
 * Thin forwards to the engine; see engine/include/m64_surface.h for the
 * fidelity-critical details of what these actually do.
 */
#ifndef NEW64_SURFACE_COLLISION_H
#define NEW64_SURFACE_COLLISION_H

#include "types.h"
#include "m64_surface.h"

static inline f32 find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor) {
    return m64_find_floor(x, y, z, pfloor);
}
static inline f32 find_ceil(f32 x, f32 y, f32 z, struct Surface **pceil) {
    return m64_find_ceil(x, y, z, pceil);
}
static inline f32 find_floor_height(f32 x, f32 y, f32 z) {
    return m64_find_floor_height(x, y, z);
}
static inline s32 find_wall_collisions(struct WallCollisionData *colData) {
    return m64_find_wall_collisions(colData);
}
static inline f32 find_water_level(f32 x, f32 z) { return m64_find_water_level(x, z); }

/* No poison gas in new64 levels; returning the void floor means "never". */
static inline f32 find_poison_gas_level(f32 x, f32 z) {
    (void) x;
    (void) z;
    return FLOOR_LOWER_LIMIT;
}

#endif /* NEW64_SURFACE_COLLISION_H */
