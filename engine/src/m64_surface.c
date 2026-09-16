/*
 * new64 -- surface storage and collision queries.
 *
 * See m64_surface.h for the list of intentional fidelity quirks. They are
 * annotated inline below as [QUIRK n].
 */
#include "m64_surface.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/*
 * Pool sizes.  The N64 titles used much smaller pools (a couple of thousand
 * triangles); the limits here are larger because new64 levels are authored for
 * this engine rather than squeezed into a cartridge.  Nothing about collision
 * behaviour depends on these numbers.
 */
#define M64_MAX_SURFACES      16384
#define M64_MAX_SURFACE_NODES 131072

static struct Surface sSurfacePool[M64_MAX_SURFACES];
static s32 sSurfaceCount;
static s32 sStaticSurfaceCount; /* everything below this index is static */

static struct SurfaceNode sNodePool[M64_MAX_SURFACE_NODES];
static s32 sNodeCount;
static s32 sStaticNodeCount;

/*
 * Partition heads.  Each cell holds a sentinel node whose ->next is the first
 * real entry, which keeps insertion branch-free at the head.
 */
static struct SurfaceNode sStaticPartition[NUM_CELLS][NUM_CELLS][SPATIAL_PARTITION_COUNT];
static struct SurfaceNode sDynamicPartition[NUM_CELLS][NUM_CELLS][SPATIAL_PARTITION_COUNT];

static f32 sWaterLevel = FLOOR_LOWER_LIMIT;

void m64_surface_pool_init(void) {
    memset(sSurfacePool, 0, sizeof(sSurfacePool));
    memset(sNodePool, 0, sizeof(sNodePool));
    memset(sStaticPartition, 0, sizeof(sStaticPartition));
    memset(sDynamicPartition, 0, sizeof(sDynamicPartition));
    sSurfaceCount = 0;
    sStaticSurfaceCount = 0;
    sNodeCount = 0;
    sStaticNodeCount = 0;
    sWaterLevel = FLOOR_LOWER_LIMIT;
}

void m64_set_water_level(f32 level) {
    sWaterLevel = level;
}

f32 m64_find_water_level(f32 x, f32 z) {
    (void) x;
    (void) z;
    return sWaterLevel;
}

s32 m64_surface_count(void) {
    return sSurfaceCount;
}

struct Surface *m64_surface_at(s32 index) {
    if (index < 0 || index >= sSurfaceCount) {
        return NULL;
    }
    return &sSurfacePool[index];
}

s32 m64_surface_class(const struct Surface *surface) {
    if (surface == NULL) {
        return SURFACE_CLASS_DEFAULT;
    }
    switch (surface->type) {
        case SURFACE_NOT_SLIPPERY:
        case SURFACE_HARD_NOT_SLIPPERY:
        case SURFACE_SWITCH:
            return SURFACE_CLASS_NOT_SLIPPERY;

        case SURFACE_SLIPPERY:
        case SURFACE_NOISE_SLIPPERY:
        case SURFACE_HARD_SLIPPERY:
        case SURFACE_NO_CAM_COL_SLIPPERY:
            return SURFACE_CLASS_SLIPPERY;

        case SURFACE_VERY_SLIPPERY:
        case SURFACE_ICE:
        case SURFACE_HARD_VERY_SLIPPERY:
        case SURFACE_NOISE_VERY_SLIPPERY_73:
        case SURFACE_NOISE_VERY_SLIPPERY_74:
        case SURFACE_NOISE_VERY_SLIPPERY:
        case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
            return SURFACE_CLASS_VERY_SLIPPERY;

        default:
            return SURFACE_CLASS_DEFAULT;
    }
}

/* --- Construction ------------------------------------------------------- */

static struct SurfaceNode *alloc_node(void) {
    if (sNodeCount >= M64_MAX_SURFACE_NODES) {
        return NULL;
    }
    return &sNodePool[sNodeCount++];
}

/*
 * Thread a surface into one partition cell.
 *
 * Floors are kept sorted by descending first-vertex Y and ceilings by
 * ascending first-vertex Y, so that a linear scan finds the topmost floor (or
 * lowest ceiling) first and can stop at the first containing triangle.
 *
 * [QUIRK 2] The sort key is literally vertex1's Y, *not* the triangle's height
 * at the query point.  For two overlapping sloped floors this can order them
 * against the intuitive answer, and the query then returns the "wrong" one.
 * That behaviour is load-bearing for a handful of original-game setups, so it
 * is preserved rather than fixed.
 */
static void add_surface_to_cell(s32 dynamic, s32 cellX, s32 cellZ,
                                struct Surface *surface) {
    struct SurfaceNode *list;
    struct SurfaceNode *newNode;
    s32 listIndex;
    s32 sortDir;
    s32 surfacePriority;
    s32 priority;

    if (surface->normal.y > 0.01f) {
        listIndex = SPATIAL_PARTITION_FLOORS;
        sortDir = 1; /* highest first */
    } else if (surface->normal.y < -0.01f) {
        listIndex = SPATIAL_PARTITION_CEILS;
        sortDir = -1; /* lowest first */
    } else {
        listIndex = SPATIAL_PARTITION_WALLS;
        sortDir = 0; /* preserve insertion order */
    }

    newNode = alloc_node();
    if (newNode == NULL) {
        return;
    }
    newNode->surface = surface;
    newNode->next = NULL;

    list = dynamic ? &sDynamicPartition[cellZ][cellX][listIndex]
                   : &sStaticPartition[cellZ][cellX][listIndex];

    surfacePriority = surface->vertex1[1] * sortDir;
    while (list->next != NULL) {
        priority = list->next->surface->vertex1[1] * sortDir;
        if (surfacePriority > priority) {
            break;
        }
        list = list->next;
    }
    newNode->next = list->next;
    list->next = newNode;
}

/*
 * Cell index for the *low* edge of a triangle's bounding box.
 *
 * [QUIRK 6] Note the 50-unit boundary buffer: a coordinate within 50 units of a
 * cell's low edge is also registered in the previous cell.  This exists because
 * queries are binned by a single point while wall checks have a radius, so
 * without it a wall sitting exactly on a cell border would be invisible to a
 * query standing just across that border.
 *
 * The buffer is 50, while the largest wall query radius is also 50 -- so the
 * compensation is only just adequate, and for radii at or above it walls near a
 * cell boundary *can* still be missed.  That is a real limitation of the
 * original design, reproduced rather than widened: enlarging the buffer would
 * silently fix wall clips that hosted movement code may rely on.
 */
#define CELL_BOUNDARY_BUFFER 50

static s32 lower_cell_index(s16 coord) {
    s32 shifted = (s32) coord + LEVEL_BOUNDARY_MAX;
    s32 index;

    if (shifted < 0) {
        shifted = 0;
    }
    index = shifted / CELL_SIZE;

    if (shifted % CELL_SIZE < CELL_BOUNDARY_BUFFER) {
        index -= 1;
    }
    if (index < 0) {
        index = 0;
    }
    if (index > NUM_CELLS_INDEX) {
        index = NUM_CELLS_INDEX;
    }
    return index;
}

/* Mirror of the above for the high edge of the bounding box. */
static s32 upper_cell_index(s16 coord) {
    s32 shifted = (s32) coord + LEVEL_BOUNDARY_MAX;
    s32 index;

    if (shifted < 0) {
        shifted = 0;
    }
    index = shifted / CELL_SIZE;

    if (shifted % CELL_SIZE > CELL_SIZE - CELL_BOUNDARY_BUFFER) {
        index += 1;
    }
    if (index > NUM_CELLS_INDEX) {
        index = NUM_CELLS_INDEX;
    }
    if (index < 0) {
        index = 0;
    }
    return index;
}

static void register_surface(struct Surface *surface, s32 dynamic) {
    s32 minX, maxX, minZ, maxZ;
    s32 cx, cz;
    s16 x1 = surface->vertex1[0], x2 = surface->vertex2[0], x3 = surface->vertex3[0];
    s16 z1 = surface->vertex1[2], z2 = surface->vertex2[2], z3 = surface->vertex3[2];

    minX = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
    maxX = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
    minZ = z1 < z2 ? (z1 < z3 ? z1 : z3) : (z2 < z3 ? z2 : z3);
    maxZ = z1 > z2 ? (z1 > z3 ? z1 : z3) : (z2 > z3 ? z2 : z3);

    /* Registered in every cell the bounding box touches, plus the boundary
     * buffer above. */
    for (cz = lower_cell_index((s16) minZ); cz <= upper_cell_index((s16) maxZ); cz++) {
        for (cx = lower_cell_index((s16) minX); cx <= upper_cell_index((s16) maxX); cx++) {
            add_surface_to_cell(dynamic, cx, cz, surface);
        }
    }
}

static struct Surface *build_surface(s16 type, const Vec3s v1, const Vec3s v2,
                                    const Vec3s v3) {
    struct Surface *surface;
    f32 nx, ny, nz, mag;
    s16 minY, maxY;

    if (sSurfaceCount >= M64_MAX_SURFACES) {
        return NULL;
    }

    /*
     * Normal from the edge cross product, computed in floats but from integer
     * vertices, so a given triangle always yields the same normal.
     */
    nx = (f32) (v2[1] - v1[1]) * (f32) (v3[2] - v2[2])
         - (f32) (v2[2] - v1[2]) * (f32) (v3[1] - v2[1]);
    ny = (f32) (v2[2] - v1[2]) * (f32) (v3[0] - v2[0])
         - (f32) (v2[0] - v1[0]) * (f32) (v3[2] - v2[2]);
    nz = (f32) (v2[0] - v1[0]) * (f32) (v3[1] - v2[1])
         - (f32) (v2[1] - v1[1]) * (f32) (v3[0] - v2[0]);
    mag = sqrtf(nx * nx + ny * ny + nz * nz);

    /* Degenerate (zero-area) triangles are dropped; they have no normal. */
    if (mag < 0.0001f) {
        return NULL;
    }
    mag = 1.0f / mag;
    nx *= mag;
    ny *= mag;
    nz *= mag;

    minY = v1[1] < v2[1] ? (v1[1] < v3[1] ? v1[1] : v3[1]) : (v2[1] < v3[1] ? v2[1] : v3[1]);
    maxY = v1[1] > v2[1] ? (v1[1] > v3[1] ? v1[1] : v3[1]) : (v2[1] > v3[1] ? v2[1] : v3[1]);

    surface = &sSurfacePool[sSurfaceCount++];
    memset(surface, 0, sizeof(*surface));
    surface->type = type;
    surface->force = 0;
    surface->flags = 0;
    surface->room = 0;
    surface->vertex1[0] = v1[0]; surface->vertex1[1] = v1[1]; surface->vertex1[2] = v1[2];
    surface->vertex2[0] = v2[0]; surface->vertex2[1] = v2[1]; surface->vertex2[2] = v2[2];
    surface->vertex3[0] = v3[0]; surface->vertex3[1] = v3[1]; surface->vertex3[2] = v3[2];
    surface->normal.x = nx;
    surface->normal.y = ny;
    surface->normal.z = nz;
    surface->originOffset = -(nx * v1[0] + ny * v1[1] + nz * v1[2]);

    /*
     * The Y slack is why you can be flush against a wall and still register it:
     * the cheap vertical reject in the wall query uses these bounds.
     */
    surface->lowerY = minY - 5;
    surface->upperY = maxY + 5;

    /*
     * [QUIRK 5] Walls are tested by projecting onto whichever vertical plane
     * their normal leans into.  The threshold is on the normal's X component
     * alone (0.707 == 45 degrees), which for a true vertical wall is the same
     * as asking whether |nx| exceeds |nz|.
     */
    if (surface->normal.x < -0.707f || surface->normal.x > 0.707f) {
        surface->flags |= SURFACE_FLAG_X_PROJECTION;
    }
    return surface;
}

struct Surface *m64_surface_add_static(s16 type, const Vec3s v1, const Vec3s v2,
                                      const Vec3s v3) {
    struct Surface *surface = build_surface(type, v1, v2, v3);

    if (surface == NULL) {
        return NULL;
    }
    register_surface(surface, FALSE);
    /* Static geometry is a prefix of the pool, so clearing dynamics is a
     * truncation rather than a list walk. */
    sStaticSurfaceCount = sSurfaceCount;
    sStaticNodeCount = sNodeCount;
    return surface;
}

struct Surface *m64_surface_add_dynamic(s16 type, const Vec3s v1, const Vec3s v2,
                                        const Vec3s v3, struct Object *owner) {
    struct Surface *surface = build_surface(type, v1, v2, v3);

    if (surface == NULL) {
        return NULL;
    }
    surface->flags |= SURFACE_FLAG_DYNAMIC;
    surface->object = owner;
    register_surface(surface, TRUE);
    return surface;
}

void m64_surface_clear_dynamic(void) {
    memset(sDynamicPartition, 0, sizeof(sDynamicPartition));
    sSurfaceCount = sStaticSurfaceCount;
    sNodeCount = sStaticNodeCount;
}

/* --- Floor queries ------------------------------------------------------ */

/*
 * Scan one cell list for the floor under (x, y, z).
 *
 * [QUIRK 2] Returns the first containing triangle, relying on the list being
 * sorted highest-first, and stops there.
 * [QUIRK 3] Skips any triangle whose surface sits more than 78 units above the
 * query point.  This is the step-up allowance.
 */
static struct Surface *find_floor_from_list(struct SurfaceNode *node, s32 x, s32 y,
                                            s32 z, f32 *pheight) {
    struct Surface *surface;
    struct Surface *found = NULL;
    s32 x1, z1, x2, z2, x3, z3;
    f32 height;

    while (node != NULL) {
        surface = node->surface;
        node = node->next;

        x1 = surface->vertex1[0]; z1 = surface->vertex1[2];
        x2 = surface->vertex2[0]; z2 = surface->vertex2[2];
        x3 = surface->vertex3[0]; z3 = surface->vertex3[2];

        /*
         * Containment by three edge cross products, in integer arithmetic.
         * Exactness here is what makes shared triangle edges seamless: a point
         * exactly on an edge yields 0 and is accepted by both triangles.
         */
        if ((z1 - z) * (x2 - x1) - (x1 - x) * (z2 - z1) < 0) {
            continue;
        }
        if ((z2 - z) * (x3 - x2) - (x2 - x) * (z3 - z2) < 0) {
            continue;
        }
        if ((z3 - z) * (x1 - x3) - (x3 - x) * (z1 - z3) < 0) {
            continue;
        }

        if (surface->type == SURFACE_INTANGIBLE) {
            continue;
        }

        /* Plane solve for the surface height at this XZ. */
        height = -((f32) x * surface->normal.x + surface->normal.z * (f32) z
                   + surface->originOffset)
                 / surface->normal.y;

        if ((f32) y - (height + FIND_FLOOR_BUFFER) < 0.0f) {
            continue;
        }

        *pheight = height;
        found = surface;
        break;
    }
    return found;
}

f32 m64_find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface **pfloor) {
    s32 cellX, cellZ;
    struct Surface *floor;
    struct Surface *dynamicFloor;
    f32 height = FLOOR_LOWER_LIMIT;
    f32 dynamicHeight = FLOOR_LOWER_LIMIT;

    /*
     * [QUIRK 1] Truncate to s16 before testing.  Collision therefore happens on
     * an integer lattice: two positions within the same unit cell resolve to
     * the same floor at the same height.  A great deal of SM64's
     * frame-perfect behaviour follows from this single cast.
     */
    s32 x = (s32) (s16) xPos;
    s32 y = (s32) (s16) yPos;
    s32 z = (s32) (s16) zPos;

    *pfloor = NULL;

    /* Outside the partition there is simply nothing to stand on. */
    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return height;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return height;
    }

    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    dynamicFloor = find_floor_from_list(
        sDynamicPartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, x, y, z,
        &dynamicHeight);
    floor = find_floor_from_list(
        sStaticPartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, x, y, z, &height);

    /* A dynamic floor only wins if it is strictly higher than the static one. */
    if (dynamicHeight > height) {
        floor = dynamicFloor;
        height = dynamicHeight;
    }

    *pfloor = floor;
    return height;
}

f32 m64_find_floor_height(f32 x, f32 y, f32 z) {
    struct Surface *floor;

    return m64_find_floor(x, y, z, &floor);
}

/* --- Ceiling queries ---------------------------------------------------- */

/* Mirror of the floor scan: opposite winding, and the buffer sits below. */
static struct Surface *find_ceil_from_list(struct SurfaceNode *node, s32 x, s32 y,
                                           s32 z, f32 *pheight) {
    struct Surface *surface;
    struct Surface *found = NULL;
    s32 x1, z1, x2, z2, x3, z3;
    f32 height;

    while (node != NULL) {
        surface = node->surface;
        node = node->next;

        x1 = surface->vertex1[0]; z1 = surface->vertex1[2];
        x2 = surface->vertex2[0]; z2 = surface->vertex2[2];
        x3 = surface->vertex3[0]; z3 = surface->vertex3[2];

        if ((z1 - z) * (x2 - x1) - (x1 - x) * (z2 - z1) > 0) {
            continue;
        }
        if ((z2 - z) * (x3 - x2) - (x2 - x) * (z3 - z2) > 0) {
            continue;
        }
        if ((z3 - z) * (x1 - x3) - (x3 - x) * (z1 - z3) > 0) {
            continue;
        }

        if (surface->type == SURFACE_INTANGIBLE) {
            continue;
        }

        height = -((f32) x * surface->normal.x + surface->normal.z * (f32) z
                   + surface->originOffset)
                 / surface->normal.y;

        if ((f32) y - (height - FIND_CEIL_BUFFER) > 0.0f) {
            continue;
        }

        *pheight = height;
        found = surface;
        break;
    }
    return found;
}

f32 m64_find_ceil(f32 xPos, f32 yPos, f32 zPos, struct Surface **pceil) {
    s32 cellX, cellZ;
    struct Surface *ceil;
    struct Surface *dynamicCeil;
    f32 height = CEIL_UPPER_LIMIT;
    f32 dynamicHeight = CEIL_UPPER_LIMIT;

    s32 x = (s32) (s16) xPos;
    s32 y = (s32) (s16) yPos;
    s32 z = (s32) (s16) zPos;

    *pceil = NULL;

    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return height;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return height;
    }

    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    dynamicCeil = find_ceil_from_list(
        sDynamicPartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, x, y, z,
        &dynamicHeight);
    ceil = find_ceil_from_list(
        sStaticPartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, x, y, z, &height);

    if (dynamicHeight < height) {
        ceil = dynamicCeil;
        height = dynamicHeight;
    }

    *pceil = ceil;
    return height;
}

/* --- Wall queries ------------------------------------------------------- */

/*
 * Scan one cell list for walls within `radius` of the probe point and push the
 * caller's position out of each.
 *
 * [QUIRK 4] Every test uses the *original* x/z held in locals, while the
 * pushout accumulates into data->x/z.  Two walls therefore each push out by
 * their full overlap rather than the second seeing the first's correction --
 * which is exactly how SM64 handles corners, and why deep corners can eject
 * you further than either wall alone would.
 */
static s32 find_wall_collisions_from_list(struct SurfaceNode *node,
                                          struct WallCollisionData *data) {
    struct Surface *surface;
    f32 offset;
    f32 radius = data->radius;
    f32 x = data->x;
    f32 y = data->y + data->offsetY;
    f32 z = data->z;
    f32 w1, w2, w3;
    f32 y1, y2, y3;
    s32 numCols = 0;

    /* A radius past 200 would reach outside the cell the query was binned to. */
    if (radius > 200.0f) {
        radius = 200.0f;
    }

    while (node != NULL) {
        surface = node->surface;
        node = node->next;

        /* Cheap vertical reject first: most walls in a cell fail here. */
        if (y < surface->lowerY || y > surface->upperY) {
            continue;
        }

        /* Signed distance from the wall plane. */
        offset = surface->normal.x * x + surface->normal.y * y + surface->normal.z * z
                 + surface->originOffset;
        if (offset < -radius || offset > radius) {
            continue;
        }

        if (surface->type == SURFACE_INTANGIBLE) {
            continue;
        }

        /*
         * [QUIRK 5] Project the triangle onto the vertical plane its normal
         * leans into and do a 2D containment test there.  The Z axis is negated
         * in the X-projection case so that both cases share a winding
         * convention.
         */
        if (surface->flags & SURFACE_FLAG_X_PROJECTION) {
            w1 = -surface->vertex1[2];
            w2 = -surface->vertex2[2];
            w3 = -surface->vertex3[2];
            y1 = surface->vertex1[1];
            y2 = surface->vertex2[1];
            y3 = surface->vertex3[1];

            if (surface->normal.x > 0.0f) {
                if ((y1 - y) * (w2 - w1) - (w1 - -z) * (y2 - y1) > 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - -z) * (y3 - y2) > 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - -z) * (y1 - y3) > 0.0f) continue;
            } else {
                if ((y1 - y) * (w2 - w1) - (w1 - -z) * (y2 - y1) < 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - -z) * (y3 - y2) < 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - -z) * (y1 - y3) < 0.0f) continue;
            }
        } else {
            w1 = surface->vertex1[0];
            w2 = surface->vertex2[0];
            w3 = surface->vertex3[0];
            y1 = surface->vertex1[1];
            y2 = surface->vertex2[1];
            y3 = surface->vertex3[1];

            if (surface->normal.z > 0.0f) {
                if ((y1 - y) * (w2 - w1) - (w1 - x) * (y2 - y1) > 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - x) * (y3 - y2) > 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - x) * (y1 - y3) > 0.0f) continue;
            } else {
                if ((y1 - y) * (w2 - w1) - (w1 - x) * (y2 - y1) < 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - x) * (y3 - y2) < 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - x) * (y1 - y3) < 0.0f) continue;
            }
        }

        /* Only the first four walls are remembered, but all of them push. */
        if (data->numWalls < 4) {
            data->walls[data->numWalls++] = surface;
        }

        data->x += surface->normal.x * (radius - offset);
        data->z += surface->normal.z * (radius - offset);
        numCols++;
    }
    return numCols;
}

s32 m64_find_wall_collisions(struct WallCollisionData *colData) {
    s32 cellX, cellZ;
    s32 numCollisions = 0;
    s32 x = (s32) (s16) colData->x;
    s32 z = (s32) (s16) colData->z;

    colData->numWalls = 0;

    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return numCollisions;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return numCollisions;
    }

    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    /* Dynamic walls are resolved before static ones. */
    numCollisions += find_wall_collisions_from_list(
        sDynamicPartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, colData);
    numCollisions += find_wall_collisions_from_list(
        sStaticPartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, colData);

    return numCollisions;
}
