/*
 * new64 -- level construction and the demo level.
 */
#include "m64_level.h"

#include <string.h>

/* Rounds toward nearest, which keeps a symmetric extent symmetric. */
static s16 to_s16(f32 v) {
    return (s16) (v < 0.0f ? (v - 0.5f) : (v + 0.5f));
}

/*
 * Add a triangle, guaranteeing which way it faces.
 *
 * Winding decides a surface's normal, and a surface wound the wrong way is not
 * merely mirrored -- it is the wrong KIND of surface. A floor wound backwards
 * becomes a ceiling: not standable, and invisible from above. That failure is
 * silent, and from inside the level it looks like geometry that is
 * inexplicably missing or that you fall straight through.
 *
 * Rather than require every caller to derive vertex order correctly for every
 * combination of coordinate signs, this computes the normal and flips the
 * winding when it disagrees with the caller's stated intent. Callers can then
 * pass corners in whatever order is natural and say what they meant.
 */
static void add_tri_facing(s16 type, const Vec3s a, const Vec3s b, const Vec3s c,
                           f32 wantX, f32 wantY, f32 wantZ) {
    f32 ux = (f32) (b[0] - a[0]), uy = (f32) (b[1] - a[1]), uz = (f32) (b[2] - a[2]);
    f32 vx = (f32) (c[0] - b[0]), vy = (f32) (c[1] - b[1]), vz = (f32) (c[2] - b[2]);
    f32 nx = uy * vz - uz * vy;
    f32 ny = uz * vx - ux * vz;
    f32 nz = ux * vy - uy * vx;

    if (nx * wantX + ny * wantY + nz * wantZ < 0.0f) {
        m64_surface_add_static(type, a, c, b);
    } else {
        m64_surface_add_static(type, a, b, c);
    }
}

/* Axis-aligned quad with a stated facing. Corners are given in ring order. */
static void add_quad_facing(s16 type, const Vec3s a, const Vec3s b, const Vec3s c,
                            const Vec3s d, f32 wantX, f32 wantY, f32 wantZ) {
    add_tri_facing(type, a, b, c, wantX, wantY, wantZ);
    add_tri_facing(type, a, c, d, wantX, wantY, wantZ);
}

void m64_level_add_floor(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y) {
    Vec3s a = { to_s16(x0), to_s16(y), to_s16(z0) };
    Vec3s b = { to_s16(x0), to_s16(y), to_s16(z1) };
    Vec3s c = { to_s16(x1), to_s16(y), to_s16(z1) };
    Vec3s d = { to_s16(x1), to_s16(y), to_s16(z0) };

    /* Faces up regardless of the order the extents were given in. */
    add_quad_facing(type, a, b, c, d, 0.0f, 1.0f, 0.0f);
}

void m64_level_add_ceiling(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 y) {
    Vec3s a = { to_s16(x0), to_s16(y), to_s16(z0) };
    Vec3s b = { to_s16(x1), to_s16(y), to_s16(z0) };
    Vec3s c = { to_s16(x1), to_s16(y), to_s16(z1) };
    Vec3s d = { to_s16(x0), to_s16(y), to_s16(z1) };

    add_quad_facing(type, a, b, c, d, 0.0f, -1.0f, 0.0f);
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
    Vec3s a, b, c, d;

    if (axis == M64_X) {
        /* Height varies with X: yLow at x0, yHigh at x1. */
        a[0] = to_s16(x0); a[1] = to_s16(yLow);  a[2] = to_s16(z0);
        b[0] = to_s16(x0); b[1] = to_s16(yLow);  b[2] = to_s16(z1);
        c[0] = to_s16(x1); c[1] = to_s16(yHigh); c[2] = to_s16(z1);
        d[0] = to_s16(x1); d[1] = to_s16(yHigh); d[2] = to_s16(z0);
    } else {
        /* Height varies with Z: yLow at z0, yHigh at z1. */
        a[0] = to_s16(x0); a[1] = to_s16(yLow);  a[2] = to_s16(z0);
        b[0] = to_s16(x0); b[1] = to_s16(yHigh); b[2] = to_s16(z1);
        c[0] = to_s16(x1); c[1] = to_s16(yHigh); c[2] = to_s16(z1);
        d[0] = to_s16(x1); d[1] = to_s16(yLow);  d[2] = to_s16(z0);
    }

    /*
     * Faces up whichever way the ramp ascends.  This matters: a ramp that
     * descends along its axis (z1 < z0) reverses the sign of the cross product,
     * and a fixed winding would build it as a ceiling -- non-standable and
     * invisible from above.
     */
    add_quad_facing(type, a, b, c, d, 0.0f, 1.0f, 0.0f);
}

void m64_level_add_box(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBottom, f32 yTop) {
    f32 t;

    /*
     * Normalise the extents first.  The wall windings below encode which face
     * is which, so a box handed reversed extents would be built inside out:
     * every normal pointing inward, making it a room rather than a solid.  From
     * inside the level that looks like a block you walk straight through.
     */
    if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
    if (z1 < z0) { t = z0; z0 = z1; z1 = t; }
    if (yTop < yBottom) { t = yBottom; yBottom = yTop; yTop = t; }

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

void m64_level_add_wedge(s16 type, f32 x0, f32 z0, f32 x1, f32 z1, f32 yBase,
                         f32 yTop) {
    /*
     * The high end is wherever the caller put z1, which may be either side of
     * z0. Resolve that to concrete low/high edges once, then build every face
     * from those with an explicit facing.
     */
    f32 zLow = z0 < z1 ? z0 : z1;
    f32 zHigh = z0 < z1 ? z1 : z0;
    s32 ascendsTowardPositiveZ = (z1 > z0);
    f32 yAtLow = ascendsTowardPositiveZ ? yBase : yTop;
    f32 yAtHigh = ascendsTowardPositiveZ ? yTop : yBase;
    /* The vertical face sits at whichever end is high. */
    f32 zBack = ascendsTowardPositiveZ ? zHigh : zLow;
    f32 backFacing = ascendsTowardPositiveZ ? 1.0f : -1.0f;
    Vec3s a, b, c, d;

    /* Sloped top: the surface actually walked on. */
    m64_level_add_ramp(type, M64_Z, x0, zLow, x1, zHigh, yAtLow, yAtHigh);

    /* Underside, so the wedge is solid rather than a sheet. */
    m64_level_add_ceiling(type, x0, zLow, x1, zHigh, yBase);

    /* Vertical face at the high end. */
    a[0] = to_s16(x0); a[1] = to_s16(yBase); a[2] = to_s16(zBack);
    b[0] = to_s16(x1); b[1] = to_s16(yBase); b[2] = to_s16(zBack);
    c[0] = to_s16(x1); c[1] = to_s16(yTop);  c[2] = to_s16(zBack);
    d[0] = to_s16(x0); d[1] = to_s16(yTop);  d[2] = to_s16(zBack);
    add_quad_facing(type, a, b, c, d, 0.0f, 0.0f, backFacing);

    /* Triangular sides, one facing -X and one +X. */
    a[0] = to_s16(x0); a[1] = to_s16(yBase); a[2] = to_s16(zLow);
    b[0] = to_s16(x0); b[1] = to_s16(yBase); b[2] = to_s16(zHigh);
    c[0] = to_s16(x0); c[1] = to_s16(yTop);  c[2] = to_s16(zBack);
    add_tri_facing(type, a, b, c, -1.0f, 0.0f, 0.0f);

    a[0] = to_s16(x1); a[1] = to_s16(yBase); a[2] = to_s16(zLow);
    b[0] = to_s16(x1); b[1] = to_s16(yBase); b[2] = to_s16(zHigh);
    c[0] = to_s16(x1); c[1] = to_s16(yTop);  c[2] = to_s16(zBack);
    add_tri_facing(type, a, b, c, 1.0f, 0.0f, 0.0f);
}

void m64_level_add_wall_kick_shaft(s16 type, f32 centerX, f32 z0, f32 z1, f32 width,
                                   f32 height, f32 thickness) {
    f32 innerLeft = centerX - width * 0.5f;
    f32 innerRight = centerX + width * 0.5f;

    /*
     * Each side is a full box rather than a bare wall.  A single wall quad is
     * solid from one side and invisible from the other, which is exactly the
     * "is that a wall or not?" ambiguity worth avoiding; a box is legible from
     * anywhere and cannot be entered from behind.
     */
    m64_level_add_box(type, innerLeft - thickness, z0, innerLeft, z1, 0.0f, height);
    m64_level_add_box(type, innerRight, z0, innerRight + thickness, z1, 0.0f, height);
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
 * The demo level is a test rig, not a scene.  Every feature exists to exercise
 * one mechanic, and they are spread far enough apart that each can be reached
 * at full running speed -- which takes about 40 frames from a standstill, or
 * roughly 1000 units of run-up.
 *
 * Layout, viewed from above (+X east, +Z north). Arena is 10400 units across.
 *
 *      z=+4600  +--------------------------------------------------------+
 *               |  platform cluster   |      long jump gap run           |
 *               |  (free-form,        |  gaps 500 / 700 / 900 / 1100     |
 *      z=+1800  |   heights 150-1000) |                                  |
 *               |---------------------+------------------+---------------|
 *               |  WALL JUMP ZONE     |                  |  crawl tunnel |
 *               |  shafts 300/450/600 |  central runway  +---------------|
 *      z=0      |  + offset wall pair |   SPAWN is at    |  stairs and   |
 *               |                     |   its south end  |  step tower   |
 *      z=-1800  |---------------------+------------------+---------------|
 *               |            slope ladder: 10/20/30/45 degrees,          |
 *      z=-4400  |            plus slippery and icy variants              |
 *               +--------------------------------------------------------+
 *
 * Surface colour is a legend rather than decoration, since slipperiness is
 * invisible geometrically: green is normal ground, pale blue is ice, blue is
 * slippery, sand is speed-capped, brown is stone.
 */

#define ARENA_EXTENT 5200.0f

/*
 * A ramp plus the platform it climbs to, and an approach landmark.
 *
 * All three placements depend on which way the ramp climbs, so the direction is
 * resolved once here.  Getting it wrong does not look like a misplaced
 * platform: the landing lands ON the ramp, burying the walkable surface under a
 * flat box, and the ramp becomes unreachable while still being visible.
 */
static void add_ramp_with_landing(s16 type, f32 x0, f32 x1, f32 zBottom, f32 zTop,
                                  f32 height, const char *name) {
    const f32 landingDepth = 700.0f;
    const f32 approachOffset = 350.0f;
    f32 dir = (zTop > zBottom) ? 1.0f : -1.0f;

    m64_level_add_wedge(type, x0, zBottom, x1, zTop, 0.0f, height);

    /* The landing sits beyond the high end, continuing in the climb direction. */
    m64_level_add_box(type, x0, zTop, x1, zTop + dir * landingDepth, -100.0f, height);

    /* The approach sits before the low end, on the downhill side. */
    add_landmark(name, (x0 + x1) * 0.5f, 0.0f, zBottom - dir * approachOffset);
}

void m64_level_build_demo(Vec3f spawnPos) {
    const f32 groundY = 0.0f;

    m64_surface_pool_init();
    sLandmarkCount = 0;

    /* --- Ground --------------------------------------------------------- */
    m64_level_add_floor(SURFACE_DEFAULT, -ARENA_EXTENT, -ARENA_EXTENT, ARENA_EXTENT,
                        ARENA_EXTENT, groundY);

    /*
     * Two terrain patches on the flat, so surface class can be felt without a
     * slope confusing the picture: ice barely decelerates, and SURFACE_SLOW
     * caps walking speed at 24 instead of 32.
     */
    m64_level_add_floor(SURFACE_VERY_SLIPPERY, 1100.0f, -600.0f, 2300.0f, 600.0f,
                        groundY + 1.0f);
    add_landmark("ice_patch", 1700.0f, groundY, -900.0f);

    m64_level_add_floor(SURFACE_SLOW, -2300.0f, -600.0f, -1100.0f, 600.0f, groundY + 1.0f);
    add_landmark("slow_patch", -1700.0f, groundY, -900.0f);

    /*
     * --- Slope ladder (south) -------------------------------------------
     * Four pitches spanning the thresholds that matter on default terrain:
     *   10 deg  below the ~15 deg slope threshold -- walk up freely
     *   20 deg  a slope; gravity pulls, still climbable
     *   30 deg  at the steep threshold -- footing is lost
     *   45 deg  far past it -- slides you straight off
     * Then the same 20 deg pitch in slippery and icy variants, where the
     * thresholds move and 20 deg becomes unclimbable.
     */
    {
        const f32 zBottom = -3400.0f;
        const f32 zTop = -4200.0f;
        const f32 width = 700.0f;
        /* tan(angle) * 800 of run */
        const f32 heights[4] = { 141.0f, 291.0f, 462.0f, 800.0f };
        const char *names[4] = { "ramp_10deg", "ramp_20deg", "ramp_30deg", "ramp_45deg" };
        f32 x = -700.0f;
        s32 i;

        for (i = 0; i < 4; i++) {
            add_ramp_with_landing(SURFACE_DEFAULT, x, x + width, zBottom, zTop, heights[i],
                                  names[i]);
            x += width + 250.0f;
        }

        add_ramp_with_landing(SURFACE_SLIPPERY, -2400.0f, -1700.0f, zBottom, zTop, 291.0f,
                              "ramp_slippery");
        add_ramp_with_landing(SURFACE_VERY_SLIPPERY, -3350.0f, -2650.0f, zBottom, zTop,
                              291.0f, "ramp_ice");
    }

    /*
     * --- Wall jump zone (west) -------------------------------------------
     * Three shafts at increasing width.  The width is the difficulty: after a
     * kick you cross at roughly 25 units/frame while the kick gives about 13
     * frames of useful rise, so 300 is comfortable, 450 needs decent speed, and
     * 600 is near the limit of what a single kick can cross before falling.
     *
     * Each shaft's walls are solid boxes rather than bare quads, so they are
     * legible from outside as well as in.
     */
    {
        const f32 shaftZ0 = -900.0f;
        const f32 shaftZ1 = 900.0f;
        const f32 shaftHeight = 1800.0f;
        const f32 thickness = 300.0f;
        const f32 widths[3] = { 300.0f, 450.0f, 600.0f };
        const char *names[3] = { "shaft_narrow", "shaft_medium", "shaft_wide" };
        f32 centers[3] = { -2200.0f, -3200.0f, -4400.0f };
        s32 i;

        static const char *const insideNames[3] = { "shaft_narrow_in",
                                                   "shaft_medium_in",
                                                   "shaft_wide_in" };

        for (i = 0; i < 3; i++) {
            m64_level_add_wall_kick_shaft(SURFACE_DEFAULT, centers[i], shaftZ0, shaftZ1,
                                          widths[i], shaftHeight, thickness);

            /* Outside approach, 1100 units south of the mouth. */
            add_landmark(names[i], centers[i], groundY, shaftZ0 - 1100.0f);

            /*
             * A second landmark just inside the west wall.
             *
             * Climbing a shaft means bouncing between its two facing walls,
             * which are perpendicular to X -- so the travel that matters is
             * across the gap, not along the shaft.  Running in along the shaft
             * axis passes straight down the middle and touches nothing.  Even
             * the narrowest gap here is enough run-up: acceleration reaches the
             * 16 units/frame a kick requires within about 170 units.
             */
            add_landmark(insideNames[i], centers[i] - widths[i] * 0.5f + 30.0f, groundY,
                         (shaftZ0 + shaftZ1) * 0.5f);
        }

        /*
         * An offset pair: two walls that do not face each other squarely, set
         * further apart. Climbing this needs kicks that carry sideways as well
         * as up, which is a different skill from a straight shaft.
         */
        m64_level_add_box(SURFACE_DEFAULT, -4700.0f, 1400.0f, -4400.0f, 2400.0f, 0.0f,
                          1600.0f);
        m64_level_add_box(SURFACE_DEFAULT, -3700.0f, 1900.0f, -3400.0f, 2900.0f, 0.0f,
                          1600.0f);
        add_landmark("offset_walls", -4050.0f, groundY, 1000.0f);
    }

    /*
     * --- Stairs and step tower (east) -------------------------------------
     * Rises of 60, 120, 180, 240, 300, 360.  The ground step's 78-unit step-up
     * allowance means the first couple are walked over without leaving the
     * ground, while the taller ones need a jump -- a direct demonstration of
     * where that threshold sits.
     */
    {
        f32 x = 1400.0f;
        s32 i;

        for (i = 0; i < 6; i++) {
            f32 h = 60.0f * (f32) (i + 1);

            m64_level_add_box(SURFACE_DEFAULT, x, -1200.0f, x + 420.0f, -300.0f,
                              groundY - 60.0f, groundY + h);
            x += 420.0f;
        }
        add_landmark("stairs", 1200.0f, groundY, -1800.0f);

        /* A tower of decreasing platforms, reachable by chaining jumps. */
        m64_level_add_box(SURFACE_DEFAULT, 3400.0f, 200.0f, 4300.0f, 1100.0f,
                          groundY - 60.0f, groundY + 300.0f);
        m64_level_add_box(SURFACE_DEFAULT, 3600.0f, 1300.0f, 4300.0f, 2000.0f,
                          groundY - 60.0f, groundY + 650.0f);
        m64_level_add_box(SURFACE_DEFAULT, 3900.0f, 2200.0f, 4500.0f, 2800.0f,
                          groundY - 60.0f, groundY + 1000.0f);
        add_landmark("tower", 3850.0f, groundY, -300.0f);

        /*
         * A ledge-grab face: 300 tall, which a running single jump peaks just
         * short of, so the lip is caught rather than cleared.
         */
        m64_level_add_box(SURFACE_DEFAULT, 1600.0f, 900.0f, 2400.0f, 1700.0f,
                          groundY - 60.0f, groundY + 300.0f);
        add_landmark("ledge_tower", 2000.0f, groundY, 300.0f);
    }

    /*
     * --- Crawl tunnel ------------------------------------------------------
     * A slab raised on two legs, with 130 units of clearance underneath. The
     * body needs 160 to stand, so this can only be crossed crawling (Z), and
     * the ground step's headroom check is what refuses to let you walk in.
     */
    {
        const f32 x0 = 2700.0f;
        const f32 x1 = 3700.0f;
        const f32 z0 = 2200.0f;
        const f32 z1 = 3000.0f;
        const f32 clearance = 130.0f;

        /* The slab itself, solid so it reads as an object from every side. */
        m64_level_add_box(SURFACE_DEFAULT, x0, z0, x1, z1, clearance, clearance + 220.0f);
        /* Legs at each end, leaving the crawl axis open. */
        m64_level_add_box(SURFACE_DEFAULT, x0, z0, x0 + 150.0f, z1, groundY, clearance);
        m64_level_add_box(SURFACE_DEFAULT, x1 - 150.0f, z0, x1, z1, groundY, clearance);
        add_landmark("crawl_tunnel", (x0 + x1) * 0.5f, groundY, z0 - 900.0f);
    }

    /*
     * --- Long jump gap run (north) ----------------------------------------
     * Platforms separated by 500, 700, 900 and 1100.  A running jump covers
     * roughly 500; a long jump clears well past 1000 thanks to halved gravity
     * and the 1.5x speed multiplier. So the run sorts the two apart: the first
     * gap is jumpable, the last is long-jump-only.
     */
    {
        const f32 y = groundY + 250.0f;
        const f32 depth = 900.0f;
        const f32 z0 = 2600.0f;
        const f32 gaps[4] = { 500.0f, 700.0f, 900.0f, 1100.0f };
        f32 x = -2100.0f;
        const f32 platformWidth = 800.0f;
        /* The approach platform is twice as long as the rest: reaching top
         * speed takes about 1000 units, and a long jump demo that starts from
         * a short run is measuring the run-up, not the jump. */
        const f32 startWidth = 2000.0f;
        s32 i;

        m64_level_add_box(SURFACE_DEFAULT, x, z0, x + startWidth, z0 + depth,
                          groundY - 60.0f, y);
        add_landmark("gap_start", x + 150.0f, y, z0 + depth * 0.5f);
        x += startWidth - platformWidth;

        for (i = 0; i < 4; i++) {
            x += platformWidth + gaps[i];
            m64_level_add_box(SURFACE_DEFAULT, x, z0, x + platformWidth, z0 + depth,
                              groundY - 60.0f, y);
        }
        add_landmark("gap_end", x + platformWidth * 0.5f, y, z0 + depth * 0.5f);
    }

    /*
     * --- Platform cluster (northwest) -------------------------------------
     * Free-form jumping practice: eight platforms at assorted heights and
     * spacings, with no single intended route. Useful for feeling out jump
     * distances rather than testing one specific mechanic.
     */
    {
        static const f32 cluster[8][4] = {
            /* x0,      z0,      size,   height */
            { -3900.0f, 3000.0f, 700.0f, 150.0f },
            { -2900.0f, 3400.0f, 600.0f, 350.0f },
            { -3600.0f, 4000.0f, 500.0f, 600.0f },
            { -2400.0f, 4200.0f, 700.0f, 250.0f },
            { -4600.0f, 3600.0f, 600.0f, 800.0f },
            { -1800.0f, 3000.0f, 500.0f, 450.0f },
            { -1500.0f, 3900.0f, 600.0f, 700.0f },
            { -4400.0f, 2500.0f, 500.0f, 1000.0f },
        };
        s32 i;

        for (i = 0; i < 8; i++) {
            m64_level_add_box(SURFACE_DEFAULT, cluster[i][0], cluster[i][1],
                              cluster[i][0] + cluster[i][2], cluster[i][1] + cluster[i][2],
                              groundY - 60.0f, groundY + cluster[i][3]);
        }
        add_landmark("cluster", -3000.0f, groundY, 2400.0f);
    }

    /*
     * --- Arena boundary ---------------------------------------------------
     * Counter-clockwise from above so all four normals face inward. Wound the
     * other way these are invisible to the player and they fall out of the
     * world.
     */
    {
        const f32 e = ARENA_EXTENT;
        const f32 h = 900.0f;

        m64_level_add_wall(SURFACE_DEFAULT, -e, e, -e, -e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, -e, -e, e, -e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, e, -e, e, e, groundY, groundY + h);
        m64_level_add_wall(SURFACE_DEFAULT, e, e, -e, e, groundY, groundY + h);
    }

    /*
     * Spawn at the south end of the central runway: running north gives about
     * 3600 units of clear ground, enough to reach top speed and run a full jump
     * chain before reaching anything.
     */
    spawnPos[0] = 0.0f;
    spawnPos[1] = groundY;
    spawnPos[2] = -2200.0f;
    add_landmark("spawn", 0.0f, groundY, -2200.0f);
    add_landmark("runway", 0.0f, groundY, -2200.0f);
}
