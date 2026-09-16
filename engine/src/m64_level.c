/*
 * new64 -- level construction and the demo level.
 */
#include "m64_level.h"

#include <string.h>

/* Rounds toward nearest, which keeps a symmetric extent symmetric. */
static s16 to_s16(f32 v) {
    return (s16) (v < 0.0f ? (v - 0.5f) : (v + 0.5f));
}

void m64_level_add_floor(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y) {
    Vec3s a = { to_s16(x0), to_s16(y), to_s16(z0) };
    Vec3s b = { to_s16(x0), to_s16(y), to_s16(z1) };
    Vec3s c = { to_s16(x1), to_s16(y), to_s16(z1) };
    Vec3s d = { to_s16(x1), to_s16(y), to_s16(z0) };

    /* Winding chosen so the normal points up (+Y). */
    m64_surface_add_static(type, a, b, c);
    m64_surface_add_static(type, a, c, d);
}

void m64_level_add_ceiling(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y) {
    Vec3s a = { to_s16(x0), to_s16(y), to_s16(z0) };
    Vec3s b = { to_s16(x1), to_s16(y), to_s16(z0) };
    Vec3s c = { to_s16(x1), to_s16(y), to_s16(z1) };
    Vec3s d = { to_s16(x0), to_s16(y), to_s16(z1) };

    /* Reverse winding: normal points down (-Y). */
    m64_surface_add_static(type, a, b, c);
    m64_surface_add_static(type, a, c, d);
}

/*
 * Vertical wall from (x0,z0) to (x1,z1).
 *
 * Winding rule, worth stating exactly because getting it backwards produces a
 * wall the player walks straight through with no other symptom:
 *
 *   the normal is the travel direction (dx,dz) rotated a quarter turn, coming
 *   out as (-dz, 0, dx).
 *
 * So travelling +X yields a normal facing +Z, and travelling +Z yields a normal
 * facing -X.  The normal must face the side the player is on, i.e. walk the
 * wall so that the open space is on your left.  For an enclosing boundary that
 * means going counter-clockwise viewed from above; for a solid box, clockwise.
 */
void m64_level_add_wall(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBottom, f32 yTop) {
    Vec3s a = { to_s16(x0), to_s16(yBottom), to_s16(z0) };
    Vec3s b = { to_s16(x1), to_s16(yBottom), to_s16(z1) };
    Vec3s c = { to_s16(x1), to_s16(yTop), to_s16(z1) };
    Vec3s d = { to_s16(x0), to_s16(yTop), to_s16(z0) };

    m64_surface_add_static(type, a, b, c);
    m64_surface_add_static(type, a, c, d);
}

void m64_level_add_ramp(s16 type, s32 axis, f32 x0, f32 z0, f32 x1, f32 z1, f32 yLow,
                        f32 yHigh) {
    if (axis == M64_X) {
        /* Height varies with X: low at x0, high at x1. */
        Vec3s a = { to_s16(x0), to_s16(yLow), to_s16(z0) };
        Vec3s b = { to_s16(x0), to_s16(yLow), to_s16(z1) };
        Vec3s c = { to_s16(x1), to_s16(yHigh), to_s16(z1) };
        Vec3s d = { to_s16(x1), to_s16(yHigh), to_s16(z0) };

        m64_surface_add_static(type, a, b, c);
        m64_surface_add_static(type, a, c, d);
    } else {
        /* Height varies with Z: low at z0, high at z1. */
        Vec3s a = { to_s16(x0), to_s16(yLow), to_s16(z0) };
        Vec3s b = { to_s16(x0), to_s16(yHigh), to_s16(z1) };
        Vec3s c = { to_s16(x1), to_s16(yHigh), to_s16(z1) };
        Vec3s d = { to_s16(x1), to_s16(yLow), to_s16(z0) };

        m64_surface_add_static(type, a, b, c);
        m64_surface_add_static(type, a, c, d);
    }
}

void m64_level_add_box(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBottom, f32 yTop) {
    m64_level_add_floor(type, x0, z0, x1, z1, yTop);
    m64_level_add_ceiling(type, x0, z0, x1, z1, yBottom);

    /*
     * Walls wound so their normals face outward.  Going around the footprint
     * counter-clockwise when viewed from above puts the solid side inside.
     */
    m64_level_add_wall(type, x0, z0, x0, z1, yBottom, yTop); /* -X face */
    m64_level_add_wall(type, x0, z1, x1, z1, yBottom, yTop); /* +Z face */
    m64_level_add_wall(type, x1, z1, x1, z0, yBottom, yTop); /* +X face */
    m64_level_add_wall(type, x1, z0, x0, z0, yBottom, yTop); /* -Z face */
}

/* --- Landmarks ---------------------------------------------------------- */

#define M64_MAX_LANDMARKS 32

static struct M64Landmark sLandmarks[M64_MAX_LANDMARKS];
static s32 sLandmarkCount;

static void add_landmark(const char *name, f32 x, f32 y, f32 z) {
    if (sLandmarkCount >= M64_MAX_LANDMARKS) {
        return;
    }
    sLandmarks[sLandmarkCount].name = name;
    sLandmarks[sLandmarkCount].pos[0] = x;
    sLandmarks[sLandmarkCount].pos[1] = y;
    sLandmarks[sLandmarkCount].pos[2] = z;
    sLandmarkCount++;
}

s32 m64_level_landmark_count(void) {
    return sLandmarkCount;
}

const struct M64Landmark *m64_level_landmark(s32 index) {
    if (index < 0 || index >= sLandmarkCount) {
        return NULL;
    }
    return &sLandmarks[index];
}

const struct M64Landmark *m64_level_landmark_by_name(const char *name) {
    s32 i;

    for (i = 0; i < sLandmarkCount; i++) {
        if (strcmp(sLandmarks[i].name, name) == 0) {
            return &sLandmarks[i];
        }
    }
    return NULL;
}

/* --- The demo level ----------------------------------------------------- */

/*
 * Map (viewed from above, +X right, +Z down, origin at spawn):
 *
 *        -X                    0                    +X
 *   -Z   +--------------------------------------------------+
 *        |  ice ramp    |  slope ladder: 10/20/30/45 deg    |
 *        |  (very slip) |                                   |
 *        |--------------+-----------------------------------|
 *    0   |  wall kick   |   open runway (flat)  | stair     |
 *        |  corridor    |   SPAWN is at its south end     |
 *        |--------------+-----------------------+-----------|
 *        |  ledge-grab tower  |  long-jump gap  | low tunnel|
 *   +Z   +--------------------------------------------------+
 *
 * Every feature is separated by enough flat ground to reach top speed (which
 * takes roughly 40 frames from a standstill) before hitting it.
 */
void m64_level_build_demo(Vec3f spawnPos) {
    const f32 groundY = 0.0f;

    m64_surface_pool_init();
    sLandmarkCount = 0;

    /* --- The ground -----------------------------------------------------
     * One large plane. Split into a few pieces with different surface types so
     * the slipperiness classes can be compared on flat ground too. */
    m64_level_add_floor(SURFACE_DEFAULT, -3000.0f, -3000.0f, 3000.0f, 3000.0f, groundY);

    /* An ice patch on the flat, to feel the class difference without a slope. */
    m64_level_add_floor(SURFACE_VERY_SLIPPERY, 900.0f, 1400.0f, 1900.0f, 2400.0f,
                        groundY + 1.0f);
    add_landmark("ice_patch", 1400.0f, groundY, 1900.0f);

    /* A slow (deep-snow style) patch: caps walking speed at 24 instead of 32. */
    m64_level_add_floor(SURFACE_SLOW, -1900.0f, 1400.0f, -900.0f, 2400.0f, groundY + 1.0f);
    add_landmark("slow_patch", -1400.0f, groundY, 1900.0f);

    /*
     * --- Slope ladder ---------------------------------------------------
     * Four ramps spanning the thresholds that matter on default terrain:
     *   10 deg : below the ~15 deg slope threshold -- walk up freely
     *   20 deg : a slope; gravity pulls, still climbable
     *   30 deg : at the steep threshold -- you lose your footing
     *   45 deg : far past it -- slides you off immediately
     * Each is 600 deep and rises over 800 of run, so the angle is set by height.
     */
    {
        const f32 run = 800.0f;
        const f32 z0 = -2600.0f;
        const f32 depth = 500.0f;
        /* tan(angle) * run */
        const f32 heights[4] = { 141.0f, 291.0f, 462.0f, 800.0f };
        const f32 xs[4] = { -400.0f, 400.0f, 1200.0f, 2000.0f };
        const char *names[4] = { "ramp_10deg", "ramp_20deg", "ramp_30deg", "ramp_45deg" };
        s32 i;

        for (i = 0; i < 4; i++) {
            f32 x0 = xs[i];
            f32 x1 = xs[i] + 600.0f;

            m64_level_add_ramp(SURFACE_DEFAULT, M64_Z, x0, z0, x1, z0 + run, heights[i],
                               groundY);
            /* Side walls so you cannot walk off the edge mid-slope. */
            m64_level_add_wall(SURFACE_DEFAULT, x0, z0 + run, x0, z0, groundY,
                               groundY + heights[i] + 50.0f);
            m64_level_add_wall(SURFACE_DEFAULT, x1, z0, x1, z0 + run, groundY,
                               groundY + heights[i] + 50.0f);
            /* Landing platform at the top of each ramp. */
            m64_level_add_box(SURFACE_DEFAULT, x0, z0 - depth, x1, z0, groundY - 100.0f,
                              heights[i]);
            add_landmark(names[i], x0 + 300.0f, groundY, z0 + run - 100.0f);
        }
    }

    /*
     * --- Slippery slope pair --------------------------------------------
     * The same 20 degree pitch in SLIPPERY and VERY_SLIPPERY. On the ice one,
     * 20 degrees is past even the steep threshold (15 deg), so it cannot be
     * stood on at all -- a direct demonstration that "steepness" is a property
     * of the surface type, not the geometry.
     */
    {
        const f32 run = 800.0f;
        const f32 rise = 291.0f;
        const f32 z0 = -2600.0f;

        m64_level_add_ramp(SURFACE_SLIPPERY, M64_Z, -1300.0f, z0, -700.0f, z0 + run, rise,
                           groundY);
        m64_level_add_box(SURFACE_SLIPPERY, -1300.0f, z0 - 500.0f, -700.0f, z0,
                          groundY - 100.0f, rise);
        add_landmark("ramp_slippery", -1000.0f, groundY, z0 + run - 100.0f);

        m64_level_add_ramp(SURFACE_VERY_SLIPPERY, M64_Z, -2200.0f, z0, -1600.0f,
                           z0 + run, rise, groundY);
        m64_level_add_box(SURFACE_VERY_SLIPPERY, -2200.0f, z0 - 500.0f, -1600.0f, z0,
                          groundY - 100.0f, rise);
        add_landmark("ramp_ice", -1900.0f, groundY, z0 + run - 100.0f);
    }

    /*
     * --- Wall kick corridor ---------------------------------------------
     * Two parallel walls 300 apart and 1200 tall.  300 is wide enough to run
     * into one, wall kick, cross, and kick again -- the standard way to climb a
     * shaft.  Narrower and the two-frame window becomes nearly impossible.
     */
    {
        const f32 x0 = -2400.0f;
        const f32 x1 = -2100.0f;
        const f32 zA = -300.0f;
        const f32 zB = 900.0f;

        /* Wound so each normal faces into the shaft, where the player is. */
        m64_level_add_wall(SURFACE_DEFAULT, x0, zB, x0, zA, groundY, groundY + 1200.0f);
        m64_level_add_wall(SURFACE_DEFAULT, x1, zA, x1, zB, groundY, groundY + 1200.0f);
        /* Back wall, closing the shaft so you kick between two faces. */
        m64_level_add_wall(SURFACE_DEFAULT, x1, zB, x0, zB, groundY, groundY + 1200.0f);
        /* A ledge at the top as the reward for climbing it. */
        m64_level_add_box(SURFACE_DEFAULT, x0 - 400.0f, zA, x0, zB, groundY,
                          groundY + 1200.0f);
        /*
         * The approach landmark sits 1000 units *south* of the shaft mouth, not
         * inside it.  A wall kick needs more than 16 forward speed at contact,
         * and that takes roughly 20 frames of run-up -- starting inside the
         * shaft leaves no room to build it, and the wall hit degrades to a
         * plain stop instead of offering a kick.
         */
        add_landmark("wallkick_corridor", (x0 + x1) * 0.5f, groundY, zA - 1000.0f);
    }

    /*
     * --- Stair steps -----------------------------------------------------
     * Rises of 60, 120, 180, 240.  The ground step allows stepping up onto
     * anything within the floor buffer without leaving the ground, so the first
     * couple are walked over seamlessly while the taller ones need a jump.
     */
    {
        f32 x = 1400.0f;
        s32 i;

        for (i = 0; i < 4; i++) {
            f32 h = 60.0f * (f32) (i + 1);

            m64_level_add_box(SURFACE_DEFAULT, x, -200.0f, x + 300.0f, 600.0f,
                              groundY - 50.0f, groundY + h);
            x += 300.0f;
        }
        add_landmark("stairs", 1300.0f, groundY, 200.0f);
    }

    /*
     * --- Ledge grab tower ------------------------------------------------
     * A platform whose top is 300 above the ground.  Jumping at its face from
     * the ground peaks around 220 with a single jump, so you catch the lip
     * rather than clearing it -- which is exactly the ledge grab case.
     */
    {
        m64_level_add_box(SURFACE_DEFAULT, -1200.0f, 1600.0f, -600.0f, 2200.0f,
                          groundY - 50.0f, groundY + 300.0f);
        add_landmark("ledge_tower", -900.0f, groundY, 1500.0f);
    }

    /*
     * --- Long jump gap ---------------------------------------------------
     * An 800-unit gap between two platforms, with nothing below.  A running
     * jump covers roughly 500; a long jump clears 800 comfortably thanks to its
     * halved gravity and 1.5x speed multiplier.  So this gap separates the two.
     */
    {
        /*
         * 150 tall, not higher: the near platform has to be reachable with an
         * ordinary running jump so the gap itself is what the demo tests, not
         * getting onto the approach.
         */
        const f32 y = groundY + 150.0f;

        m64_level_add_box(SURFACE_DEFAULT, -300.0f, 1600.0f, 500.0f, 2400.0f,
                          groundY - 50.0f, y);
        m64_level_add_box(SURFACE_DEFAULT, 1300.0f, 1600.0f, 2100.0f, 2400.0f,
                          groundY - 50.0f, y);
        /* At the -X end of the near platform: the gap runs along X, so the
         * approach must too. */
        add_landmark("gap_near", -200.0f, y, 2000.0f);
        add_landmark("gap_far", 1700.0f, y, 2000.0f);
    }

    /*
     * --- Low tunnel -------------------------------------------------------
     * A ceiling 130 above the floor.  The body needs 160 of headroom to stand,
     * so this can only be crossed crawling -- and the ground step's headroom
     * check is what refuses to let you walk in.
     */
    {
        const f32 x0 = 2200.0f;
        const f32 x1 = 2800.0f;
        const f32 z0 = 600.0f;
        const f32 z1 = 1600.0f;

        m64_level_add_ceiling(SURFACE_DEFAULT, x0, z0, x1, z1, groundY + 130.0f);
        m64_level_add_wall(SURFACE_DEFAULT, x0, z1, x0, z0, groundY, groundY + 400.0f);
        m64_level_add_wall(SURFACE_DEFAULT, x1, z0, x1, z1, groundY, groundY + 400.0f);
        add_landmark("low_tunnel", (x0 + x1) * 0.5f, groundY, z0 - 200.0f);
    }

    /*
     * --- Outer boundary ---------------------------------------------------
     * A wall around the whole arena. Without it, running off the edge leaves the
     * collision partition, which is a legitimate state but an unhelpful demo.
     */
    {
        const f32 e = 3000.0f;
        const f32 h = 800.0f;

        /* Counter-clockwise from above, so all four normals face inward. Wound
         * the other way these are invisible to the player and they fall out of
         * the world. */
        m64_level_add_wall(SURFACE_DEFAULT, -e, e, -e, -e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, -e, -e, e, -e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, e, -e, e, e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, e, e, -e, e, groundY, groundY + h);
    }

    /*
     * Spawn at the south end of the central corridor rather than at the origin.
     * Running north from here gives ~3100 units of clear ground before the
     * long-jump platforms -- enough to reach top speed (about 40 frames), run a
     * full jump chain, and still have room to land.  Starting at the origin left
     * only 1600 units, which a jump chain overruns.
     */
    spawnPos[0] = 0.0f;
    spawnPos[1] = groundY;
    spawnPos[2] = -1500.0f;
    add_landmark("spawn", 0.0f, groundY, -1500.0f);
    add_landmark("runway", 0.0f, groundY, -1500.0f);
}
