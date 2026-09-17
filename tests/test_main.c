/*
 * new64 -- test suite.
 *
 * Two kinds of test here, and the distinction matters:
 *
 *   * Substrate tests pin down behaviour that *must* be exact for hosted
 *     movement code to behave identically to the original -- the angle table's
 *     quantisation, the s16 truncation in collision queries, the floor buffer,
 *     wall pushout accumulation.  If one of these fails, slotted-in code will
 *     misbehave in ways that look like bugs in the slotted code.
 *
 *   * Behavioural tests assert the documented constants of the movement model:
 *     jump launch velocities, the walk speed cap, terminal velocity, slope
 *     class thresholds, the jump chain, the wall kick window.  These are the
 *     numbers that define the feel, and they are asserted as numbers so that a
 *     refactor cannot quietly change them.
 */
#include "demo_app.h"

#include "area.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "mario_actions_airborne.h"
#include "mario_actions_moving.h"
#include "m64_camera.h"
#include "m64_level.h"
#include "m64_render.h"
#include "m64_surface.h"
#include "mario.h"
#include "mario_step.h"
#include "sm64.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static s32 sPassed;
static s32 sFailed;
static const char *sSection = "";

static void section(const char *name) {
    sSection = name;
    printf("\n-- %s\n", name);
}

static void check(s32 condition, const char *what) {
    if (condition) {
        sPassed++;
        printf("  ok    %s\n", what);
    } else {
        sFailed++;
        printf("  FAIL  %s  [%s]\n", what, sSection);
    }
}

static void check_near(f32 actual, f32 expected, f32 tolerance, const char *what) {
    f32 diff = actual - expected;

    if (diff < 0.0f) {
        diff = -diff;
    }
    if (diff <= tolerance) {
        sPassed++;
        printf("  ok    %s (%.4f)\n", what, actual);
    } else {
        sFailed++;
        printf("  FAIL  %s: got %.4f, want %.4f +/- %.4f  [%s]\n", what, actual, expected,
               tolerance, sSection);
    }
}

/* --- Test fixtures ------------------------------------------------------ */

static struct M64World sWorld;

/* A bare flat world, so collision tests are not perturbed by demo geometry. */
static void setup_flat_world(void) {
    Vec3f spawn = { 0.0f, 0.0f, 0.0f };

    m64_surface_pool_init();
    m64_level_add_floor(SURFACE_DEFAULT, -4000.0f, -4000.0f, 4000.0f, 4000.0f, 0.0f);

    memset(&sWorld, 0, sizeof(sWorld));
    gMarioState = &sWorld.marioState;
    gPlayer1Controller = &sWorld.controller;
    gCurrentArea = &sWorld.area;

    sWorld.area.camera = &sWorld.camera;
    sWorld.area.terrainType = TERRAIN_GRASS;
    sWorld.marioState.marioObj = &sWorld.marioObj;
    sWorld.marioState.area = &sWorld.area;
    sWorld.marioState.statusForCamera = &sWorld.camState;
    sWorld.marioState.marioBodyState = &sWorld.bodyState;
    sWorld.marioState.controller = &sWorld.controller;
    sWorld.marioState.animation = &sWorld.animation;

    m64_vec3f_copy(sWorld.marioState.pos, spawn);
    m64_vec3f_copy(sWorld.marioObj.header.gfx.pos, spawn);
    sWorld.marioState.faceAngle[1] = 0;

    /* Camera at yaw 0x8000 means "camera behind a player facing +Z", so a stick
     * reading of full-up maps to intendedYaw 0 and the tests can reason in
     * world space directly. */
    init_mario();
    sWorld.camera.yaw = 0x8000;
}

/* Steps with the camera left free to follow, for tests about how steering and
 * the follow camera interact. */
static void step_world_free_camera(s16 stickX, s16 stickY, u16 buttons, s32 frames) {
    s32 i;

    for (i = 0; i < frames; i++) {
        m64_world_step(&sWorld, stickX, stickY, buttons, 0);
    }
}

static void step_world(s16 stickX, s16 stickY, u16 buttons, s32 frames) {
    s32 i;

    for (i = 0; i < frames; i++) {
        m64_world_step(&sWorld, stickX, stickY, buttons, 0);
        /* Hold the camera still: a moving camera would rotate the meaning of
         * the stick mid-test. */
        sWorld.camera.yaw = 0x8000;
        sWorld.viewCam.yaw = 0x8000;
    }
}

/* --- Math --------------------------------------------------------------- */

static void test_angle_math(void) {
    section("angle math");

    check_near(sins(0), 0.0f, 1e-6f, "sins(0) == 0");
    check_near(sins(0x4000), 1.0f, 1e-6f, "sins(90deg) == 1");
    check_near(sins((s16) 0x8000), 0.0f, 1e-3f, "sins(180deg) == 0");
    check_near(sins((s16) 0xC000), -1.0f, 1e-6f, "sins(270deg) == -1");
    check_near(coss(0), 1.0f, 1e-6f, "coss(0) == 1");
    check_near(coss(0x4000), 0.0f, 1e-3f, "coss(90deg) == 0");

    /*
     * The table is indexed by the angle's top 12 bits, so the low nibble is
     * discarded.  This quantisation is observable: movement code accumulates
     * small yaw deltas, and an implementation using sinf() would drift away
     * over a long run.
     */
    check(sins(0x0001) == sins(0x000F), "sine table quantises the low 4 angle bits");
    check(sins(0x0010) != sins(0x0000), "sine table resolves the 5th angle bit");

    /* atan2s takes (z, x) and measures from +Z toward +X. */
    check(atan2s(1.0f, 0.0f) == 0, "atan2s(+Z) == 0");
    check(atan2s(0.0f, 1.0f) == 0x4000, "atan2s(+X) == 90deg");
    check(atan2s(0.0f, -1.0f) == (s16) 0xC000, "atan2s(-X) == 270deg");
    check(atan2s(-1.0f, 0.0f) == (s16) 0x8000, "atan2s(-Z) == 180deg");

    /* Asymmetric approach rates. */
    check(approach_s32(0, 100, 10, 50) == 10, "approach_s32 uses inc when climbing");
    check(approach_s32(0, -100, 10, 50) == -50, "approach_s32 uses dec when falling");
    check(approach_s32(95, 100, 10, 10) == 100, "approach_s32 does not overshoot");
    check_near(approach_f32(0.0f, 1.0f, 10.0f, 10.0f), 1.0f, 1e-6f,
               "approach_f32 does not overshoot");
}

/* --- Collision ---------------------------------------------------------- */

static void test_collision(void) {
    struct Surface *floor;
    struct Surface *ceil;
    f32 height;

    section("collision substrate");

    m64_surface_pool_init();
    m64_level_add_floor(SURFACE_DEFAULT, -1000.0f, -1000.0f, 1000.0f, 1000.0f, 0.0f);

    height = find_floor(0.0f, 100.0f, 0.0f, &floor);
    check(floor != NULL, "floor found under a point above it");
    check_near(height, 0.0f, 0.001f, "floor height is the surface height");

    /* Outside the triangle footprint there is no floor at all. */
    height = find_floor(5000.0f, 100.0f, 0.0f, &floor);
    check(floor == NULL, "no floor outside the geometry");
    check_near(height, FLOOR_LOWER_LIMIT, 1.0f, "missing floor reports the void height");

    /*
     * The floor buffer: a surface up to 78 units *above* the query point still
     * counts.  This is what lets a ground step climb a small lip without
     * leaving the ground.
     */
    height = find_floor(0.0f, -70.0f, 0.0f, &floor);
    check(floor != NULL, "floor 70 units above the query point is still found");
    height = find_floor(0.0f, -90.0f, 0.0f, &floor);
    check(floor == NULL, "floor 90 units above the query point is rejected");

    /*
     * Query positions are truncated to s16 before the triangle test, so
     * sub-unit horizontal motion cannot change the result.  Verified with a
     * sloped floor, where a real sub-unit change *would* alter the height.
     */
    m64_surface_pool_init();
    m64_level_add_ramp(SURFACE_DEFAULT, M64_X, 0.0f, -500.0f, 1000.0f, 500.0f, 0.0f,
                       1000.0f);
    {
        f32 h1 = find_floor(500.0f, 2000.0f, 0.0f, &floor);
        f32 h2 = find_floor(500.9f, 2000.0f, 0.0f, &floor);
        f32 h3 = find_floor(501.0f, 2000.0f, 0.0f, &floor);

        check(h1 == h2, "sub-unit X motion does not change floor height (s16 truncation)");
        check(h3 != h1, "a whole-unit X step does change floor height");
    }

    /* Ceilings mirror floors, with the buffer below instead of above. */
    m64_surface_pool_init();
    m64_level_add_floor(SURFACE_DEFAULT, -1000.0f, -1000.0f, 1000.0f, 1000.0f, 0.0f);
    m64_level_add_ceiling(SURFACE_DEFAULT, -1000.0f, -1000.0f, 1000.0f, 1000.0f, 500.0f);
    height = find_ceil(0.0f, 100.0f, 0.0f, &ceil);
    check(ceil != NULL, "ceiling found above a point below it");
    check_near(height, 500.0f, 0.001f, "ceiling height is the surface height");

    /*
     * Wall pushout.
     *
     * Sign convention: a wall's normal faces the *walkable* side, so this wall
     * spanning X at z = 0 is solid toward -Z and stood on from +Z.  A probe 20
     * units in front of it is inside the 50 radius and gets ejected to exactly
     * 50 units in front -- pushout places you at the radius, it does not merely
     * separate you.
     */
    m64_surface_pool_init();
    m64_level_add_floor(SURFACE_DEFAULT, -1000.0f, -1000.0f, 1000.0f, 1000.0f, 0.0f);
    m64_level_add_wall(SURFACE_DEFAULT, -500.0f, 0.0f, 500.0f, 0.0f, 0.0f, 500.0f);
    {
        struct WallCollisionData col;
        s32 hits;

        col.x = 0.0f;
        col.y = 50.0f;
        col.z = 20.0f; /* 20 units in front of the wall, inside a 50 radius */
        col.radius = 50.0f;
        col.offsetY = 0.0f;
        hits = find_wall_collisions(&col);

        check(hits > 0, "wall within the radius is detected");
        check(col.z > 20.0f, "wall pushes the position out along its normal");
        check_near(col.z, 50.0f, 1.0f, "pushout leaves the position exactly at the radius");

        /* Beyond the radius the wall is ignored entirely. */
        col.x = 0.0f;
        col.y = 50.0f;
        col.z = 80.0f;
        col.radius = 50.0f;
        col.offsetY = 0.0f;
        check(find_wall_collisions(&col) == 0, "wall beyond the radius is ignored");
        check_near(col.z, 80.0f, 0.001f, "an ignored wall does not move the position");

        /* Above the wall's top there is nothing to hit, via the lowerY/upperY
         * reject rather than the plane test. */
        col.x = 0.0f;
        col.y = 900.0f;
        col.z = 20.0f;
        col.radius = 50.0f;
        col.offsetY = 0.0f;
        check(find_wall_collisions(&col) == 0, "a probe above the wall top misses it");
    }
}

/* --- Jump launch velocities -------------------------------------------- */

/*
 * These are the numbers that define how the character feels.  They are asserted
 * immediately after the action is entered, before gravity has been applied, so
 * they are the launch values rather than post-frame values.
 */
static void test_jump_velocities(void) {
    section("jump launch velocities");

    setup_flat_world();
    sWorld.marioState.forwardVel = 0.0f;
    set_mario_action(&sWorld.marioState, ACT_JUMP, 0);
    check_near(sWorld.marioState.vel[1], 42.0f, 0.001f, "standing jump launches at 42");

    /* A running jump is higher: 42 + 0.25 * forwardVel. */
    setup_flat_world();
    sWorld.marioState.forwardVel = 32.0f;
    set_mario_action(&sWorld.marioState, ACT_JUMP, 0);
    check_near(sWorld.marioState.vel[1], 42.0f + 8.0f, 0.001f,
               "running jump adds a quarter of forward speed");

    setup_flat_world();
    sWorld.marioState.forwardVel = 32.0f;
    set_mario_action(&sWorld.marioState, ACT_DOUBLE_JUMP, 0);
    check_near(sWorld.marioState.vel[1], 52.0f + 8.0f, 0.001f, "double jump launches at 52");
    /*
     * This assertion previously demanded forwardVel == 0, which encoded a bug
     * as if it were the specification: it made a running chain stop dead on the
     * second jump. The suite passed precisely because it was asserting the
     * wrong thing, which is the failure mode worth remembering here -- a test
     * only protects behaviour you have actually confirmed.
     * See test_double_jump_keeps_speed() for the behavioural check.
     */
    check_near(sWorld.marioState.forwardVel, 32.0f * 0.8f, 0.001f,
               "double jump sheds a fifth of forward speed but keeps moving");

    setup_flat_world();
    sWorld.marioState.forwardVel = 30.0f;
    set_mario_action(&sWorld.marioState, ACT_TRIPLE_JUMP, 0);
    check_near(sWorld.marioState.vel[1], 69.0f, 0.001f,
               "triple jump launches at 69, independent of speed");
    check_near(sWorld.marioState.forwardVel, 24.0f, 0.001f,
               "triple jump scales forward speed by 0.8");

    setup_flat_world();
    sWorld.marioState.forwardVel = 20.0f;
    set_mario_action(&sWorld.marioState, ACT_BACKFLIP, 0);
    check_near(sWorld.marioState.vel[1], 62.0f, 0.001f, "backflip launches at 62");
    check_near(sWorld.marioState.forwardVel, -16.0f, 0.001f,
               "backflip forces forward speed to -16");

    /* The long jump trades height for distance: low launch, 1.5x speed. */
    setup_flat_world();
    sWorld.marioState.forwardVel = 30.0f;
    set_mario_action(&sWorld.marioState, ACT_LONG_JUMP, 0);
    check_near(sWorld.marioState.vel[1], 30.0f, 0.001f, "long jump launches at only 30");
    check_near(sWorld.marioState.forwardVel, 45.0f, 0.001f,
               "long jump multiplies forward speed by 1.5");

    setup_flat_world();
    sWorld.marioState.forwardVel = 40.0f;
    set_mario_action(&sWorld.marioState, ACT_LONG_JUMP, 0);
    check_near(sWorld.marioState.forwardVel, 48.0f, 0.001f,
               "long jump speed is capped at 48");

    /* A dive converts a standstill into real speed. */
    setup_flat_world();
    sWorld.marioState.forwardVel = 10.0f;
    set_mario_action(&sWorld.marioState, ACT_DIVE, 0);
    check_near(sWorld.marioState.forwardVel, 25.0f, 0.001f, "dive adds 15 to forward speed");

    setup_flat_world();
    sWorld.marioState.forwardVel = 40.0f;
    set_mario_action(&sWorld.marioState, ACT_DIVE, 0);
    check_near(sWorld.marioState.forwardVel, 48.0f, 0.001f, "dive speed is capped at 48");

    setup_flat_world();
    set_mario_action(&sWorld.marioState, ACT_JUMP_KICK, 0);
    check_near(sWorld.marioState.vel[1], 20.0f, 0.001f, "jump kick launches at 20");

    setup_flat_world();
    sWorld.marioState.forwardVel = 10.0f;
    set_mario_action(&sWorld.marioState, ACT_SLIDE_KICK, 0);
    check_near(sWorld.marioState.vel[1], 12.0f, 0.001f, "slide kick launches at 12");
    check_near(sWorld.marioState.forwardVel, 32.0f, 0.001f,
               "slide kick floors forward speed at 32");
}

/* --- Gravity ----------------------------------------------------------- */

static void test_gravity(void) {
    section("gravity");

    setup_flat_world();
    sWorld.marioState.action = ACT_FREEFALL;
    sWorld.marioState.vel[1] = 0.0f;
    apply_gravity(&sWorld.marioState);
    check_near(sWorld.marioState.vel[1], -4.0f, 0.001f, "gravity is -4 per frame");

    /* Terminal velocity, reached by iterating rather than asserted directly. */
    setup_flat_world();
    sWorld.marioState.action = ACT_FREEFALL;
    sWorld.marioState.vel[1] = 0.0f;
    {
        s32 i;

        for (i = 0; i < 200; i++) {
            apply_gravity(&sWorld.marioState);
        }
    }
    check_near(sWorld.marioState.vel[1], -75.0f, 0.001f, "terminal velocity is -75");

    /* A long jump falls at half gravity, which is most of why it travels so far. */
    setup_flat_world();
    sWorld.marioState.action = ACT_LONG_JUMP;
    sWorld.marioState.vel[1] = 0.0f;
    apply_gravity(&sWorld.marioState);
    check_near(sWorld.marioState.vel[1], -2.0f, 0.001f, "long jump gravity is halved");

    /*
     * Variable jump height: with A released and upward velocity above 20, the
     * velocity is divided by four in a single frame.  This is the whole
     * mechanic -- there is no gradual thrust model.
     */
    setup_flat_world();
    sWorld.marioState.action = ACT_JUMP;
    sWorld.marioState.flags |= MARIO_UNKNOWN_08;
    sWorld.marioState.input = 0; /* A not held */
    sWorld.marioState.vel[1] = 40.0f;
    apply_gravity(&sWorld.marioState);
    check_near(sWorld.marioState.vel[1], 10.0f, 0.001f,
               "releasing A above 20 vertical divides velocity by 4");

    /* Holding A keeps the full arc. */
    setup_flat_world();
    sWorld.marioState.action = ACT_JUMP;
    sWorld.marioState.flags |= MARIO_UNKNOWN_08;
    sWorld.marioState.input = INPUT_A_DOWN;
    sWorld.marioState.vel[1] = 40.0f;
    apply_gravity(&sWorld.marioState);
    check_near(sWorld.marioState.vel[1], 36.0f, 0.001f,
               "holding A keeps the full jump arc");

    /* A backflip lacks ACT_FLAG_CONTROL_JUMP_HEIGHT, so it cannot be shortened. */
    setup_flat_world();
    sWorld.marioState.action = ACT_BACKFLIP;
    sWorld.marioState.flags |= MARIO_UNKNOWN_08;
    sWorld.marioState.input = 0;
    sWorld.marioState.vel[1] = 40.0f;
    apply_gravity(&sWorld.marioState);
    check_near(sWorld.marioState.vel[1], 36.0f, 0.001f,
               "a backflip cannot be shortened by releasing A");
}

/* --- Ground movement --------------------------------------------------- */

static void test_ground_movement(void) {
    section("ground movement");

    /* Full stick on flat ground settles at the walk cap of 32, not the hard
     * cap of 48: acceleration fades to zero as 1.1 - fwd/43. */
    setup_flat_world();
    step_world(0, 80, 0, 90);
    check(sWorld.marioState.action == ACT_WALKING, "full stick reaches ACT_WALKING");
    /*
     * Top walking speed *oscillates* rather than settling flat, and that is
     * correct behaviour rather than noise: below 32 the acceleration term
     * (1.1 - fwd/43) is still positive and pushes past the cap, then the
     * over-cap branch subtracts a full 1.0.  The result cycles in roughly
     * [31.2, 32.4], so the assertion is a band, not a point.
     */
    check(sWorld.marioState.forwardVel > 30.5f && sWorld.marioState.forwardVel < 32.5f,
          "walking settles into the 32-speed cap band");

    /* The approach is asymptotic, so it is still short of the cap early on. */
    setup_flat_world();
    step_world(0, 80, 0, 10);
    check(sWorld.marioState.forwardVel > 8.0f && sWorld.marioState.forwardVel < 26.0f,
          "speed ramps in gradually rather than snapping to the cap");

    /* Releasing the stick at speed goes through braking, then stops. */
    setup_flat_world();
    step_world(0, 80, 0, 60);
    step_world(0, 0, 0, 60);
    check_near(sWorld.marioState.forwardVel, 0.0f, 0.001f,
               "releasing the stick brings the player to a full stop");
    check(sWorld.marioState.action == ACT_IDLE, "stopping ends in ACT_IDLE");

    /* Turn rate is capped at 0x800 per frame. */
    setup_flat_world();
    sWorld.marioState.action = ACT_WALKING;
    sWorld.marioState.faceAngle[1] = 0;
    sWorld.marioState.forwardVel = 10.0f;
    sWorld.marioState.intendedYaw = 0x4000; /* ask for a 90 degree turn at once */
    sWorld.marioState.intendedMag = 32.0f;
    update_walking_speed(&sWorld.marioState);
    check(sWorld.marioState.faceAngle[1] == 0x800,
          "ground turn rate is capped at 0x800 per frame");

    /* Speed-capped terrain: SURFACE_SLOW lowers the cap to 24. */
    m64_surface_pool_init();
    m64_level_add_floor(SURFACE_SLOW, -4000.0f, -4000.0f, 4000.0f, 4000.0f, 0.0f);
    {
        memset(&sWorld, 0, sizeof(sWorld));
        gMarioState = &sWorld.marioState;
        gPlayer1Controller = &sWorld.controller;
        gCurrentArea = &sWorld.area;
        sWorld.area.camera = &sWorld.camera;
        sWorld.area.terrainType = TERRAIN_GRASS;
        sWorld.marioState.marioObj = &sWorld.marioObj;
        sWorld.marioState.area = &sWorld.area;
        sWorld.marioState.statusForCamera = &sWorld.camState;
        sWorld.marioState.marioBodyState = &sWorld.bodyState;
        sWorld.marioState.controller = &sWorld.controller;
        sWorld.marioState.animation = &sWorld.animation;
        m64_vec3f_set(sWorld.marioState.pos, 0.0f, 0.0f, 0.0f);
        m64_vec3f_set(sWorld.marioObj.header.gfx.pos, 0.0f, 0.0f, 0.0f);
        init_mario();
        sWorld.camera.yaw = 0x8000;
        step_world(0, 80, 0, 90);
        check_near(sWorld.marioState.forwardVel, 24.0f, 0.6f,
                   "SURFACE_SLOW caps walking speed at 24");
    }
}

/* --- Slope classification ---------------------------------------------- */

/*
 * The same physical angle is classified differently per surface type.  This is
 * the mechanism behind ice feeling like ice, so each threshold is pinned.
 */
static void test_slope_classes(void) {
    section("slope classification");

    setup_flat_world();
    check(!mario_floor_is_slope(&sWorld.marioState), "flat ground is not a slope");
    check(!mario_floor_is_steep(&sWorld.marioState), "flat ground is not steep");
    check(!mario_floor_is_slippery(&sWorld.marioState), "flat default ground is not slippery");

    /* A 20 degree ramp: a slope on default terrain, but not yet too steep. */
    {
        struct Surface *floor;

        m64_surface_pool_init();
        /* rise/run = tan(20deg) -> 364 over 1000 */
        m64_level_add_ramp(SURFACE_DEFAULT, M64_X, -500.0f, -500.0f, 500.0f, 500.0f, 0.0f,
                           364.0f);
        memset(&sWorld, 0, sizeof(sWorld));
        gMarioState = &sWorld.marioState;
        gCurrentArea = &sWorld.area;
        sWorld.area.camera = &sWorld.camera;
        sWorld.area.terrainType = TERRAIN_GRASS;
        sWorld.marioState.marioObj = &sWorld.marioObj;
        sWorld.marioState.area = &sWorld.area;
        sWorld.marioState.controller = &sWorld.controller;
        sWorld.marioState.floorHeight =
            find_floor(0.0f, 500.0f, 0.0f, &sWorld.marioState.floor);
        floor = sWorld.marioState.floor;
        check(floor != NULL, "ramp floor is found");
        sWorld.marioState.floorAngle =
            atan2s(floor->normal.z, floor->normal.x);

        /* Face uphill so the steep test is not exempted by facing downhill. */
        sWorld.marioState.faceAngle[1] = (s16) (sWorld.marioState.floorAngle + 0x8000);

        check(mario_floor_is_slope(&sWorld.marioState),
              "20deg is a slope on default terrain (threshold ~15deg)");
        check(!mario_floor_is_steep(&sWorld.marioState),
              "20deg is not steep on default terrain (threshold ~30deg)");

        /* The same geometry as ice: now past even the steep threshold. */
        floor->type = SURFACE_VERY_SLIPPERY;
        check(mario_floor_is_slope(&sWorld.marioState),
              "20deg is a slope on ice (threshold ~5deg)");
        check(mario_floor_is_steep(&sWorld.marioState),
              "20deg IS steep on ice (threshold ~15deg)");
        check(mario_floor_is_slippery(&sWorld.marioState), "ice at 20deg is slippery");

        /* And as grippy ground: not steep at all. */
        floor->type = SURFACE_NOT_SLIPPERY;
        check(!mario_floor_is_slippery(&sWorld.marioState),
              "not-slippery ground is never slippery");
    }
}

/* --- Air control ------------------------------------------------------- */

static void test_air_control(void) {
    section("air control");

    /* Air drag above 32 (48 for a long jump), at 1 per frame. */
    setup_flat_world();
    sWorld.marioState.action = ACT_FREEFALL;
    sWorld.marioState.forwardVel = 40.0f;
    sWorld.marioState.input = 0;
    update_air_without_turn(&sWorld.marioState);
    /* -0.35 constant bleed, then -1.0 drag for being above the threshold. */
    check_near(sWorld.marioState.forwardVel, 40.0f - 0.35f - 1.0f, 0.001f,
               "air drag applies above 32 forward speed");

    setup_flat_world();
    sWorld.marioState.action = ACT_LONG_JUMP;
    sWorld.marioState.forwardVel = 40.0f;
    sWorld.marioState.input = 0;
    update_air_without_turn(&sWorld.marioState);
    check_near(sWorld.marioState.forwardVel, 40.0f - 0.35f, 0.001f,
               "a long jump's drag threshold is 48, so 40 is undragged");

    /* Backwards air speed is floored at -16. */
    setup_flat_world();
    sWorld.marioState.action = ACT_FREEFALL;
    sWorld.marioState.forwardVel = -30.0f;
    sWorld.marioState.input = 0;
    update_air_without_turn(&sWorld.marioState);
    check_near(sWorld.marioState.forwardVel, -16.0f, 0.001f,
               "backwards air speed is clamped to -16");

    /* Forward air acceleration is only 1.5 at full stick -- air control is weak. */
    setup_flat_world();
    sWorld.marioState.action = ACT_FREEFALL;
    sWorld.marioState.forwardVel = 0.0f;
    sWorld.marioState.faceAngle[1] = 0;
    sWorld.marioState.intendedYaw = 0;
    sWorld.marioState.intendedMag = 32.0f;
    sWorld.marioState.input = INPUT_NONZERO_ANALOG;
    update_air_without_turn(&sWorld.marioState);
    /*
     * Exactly 1.5, not 1.5 minus the 0.35 bleed: the bleed is an approach
     * toward zero, and starting *at* zero it does nothing. The bleed only costs
     * you speed you already had.
     */
    check_near(sWorld.marioState.forwardVel, 1.5f, 0.01f,
               "full stick gives only 1.5 forward acceleration in the air");
}

/* --- Sequencing -------------------------------------------------------- */

static void test_jump_chain(void) {
    section("jump chain");

    setup_flat_world();

    /* Run, then jump. */
    step_world(0, 80, 0, 30);
    step_world(0, 80, A_BUTTON, 2);
    check(sWorld.marioState.action == ACT_JUMP, "A while running gives ACT_JUMP");

    /* Fall to the ground, then press A again within the landing window. */
    {
        s32 i;
        s32 landed = FALSE;

        for (i = 0; i < 60; i++) {
            step_world(0, 80, 0, 1);
            if ((sWorld.marioState.action & ACT_GROUP_MASK) != ACT_GROUP_AIRBORNE) {
                landed = TRUE;
                break;
            }
        }
        check(landed, "the jump lands within 60 frames");

        step_world(0, 80, A_BUTTON, 2);
        check(sWorld.marioState.action == ACT_DOUBLE_JUMP,
              "A on landing escalates to ACT_DOUBLE_JUMP");

        landed = FALSE;
        for (i = 0; i < 60; i++) {
            step_world(0, 80, 0, 1);
            if ((sWorld.marioState.action & ACT_GROUP_MASK) != ACT_GROUP_AIRBORNE) {
                landed = TRUE;
                break;
            }
        }
        check(landed, "the double jump lands");

        step_world(0, 80, A_BUTTON, 2);
        check(sWorld.marioState.action == ACT_TRIPLE_JUMP,
              "A again escalates to ACT_TRIPLE_JUMP");
    }
}

static void test_crouch_moves(void) {
    section("crouch-derived moves");

    /* Crouch, then A: a backflip, not a jump. */
    setup_flat_world();
    step_world(0, 0, Z_TRIG, 12);
    check(sWorld.marioState.action == ACT_CROUCHING
              || sWorld.marioState.action == ACT_START_CROUCHING,
          "Z from idle crouches");
    step_world(0, 0, Z_TRIG | A_BUTTON, 2);
    check(sWorld.marioState.action == ACT_BACKFLIP, "crouch + A gives a backflip");

    /* Running + Z is a crouch slide; + A within 30 frames is a long jump. */
    setup_flat_world();
    step_world(0, 80, 0, 45);
    step_world(0, 80, Z_TRIG, 1);
    check(sWorld.marioState.action == ACT_CROUCH_SLIDE,
          "Z while running gives a crouch slide");
    step_world(0, 80, Z_TRIG | A_BUTTON, 2);
    check(sWorld.marioState.action == ACT_LONG_JUMP,
          "crouch slide + A gives a long jump");
}

static void test_dive_threshold(void) {
    section("dive vs punch threshold");

    /* Below 29 forward speed, B on the ground punches. */
    setup_flat_world();
    step_world(0, 80, 0, 8);
    check(sWorld.marioState.forwardVel < 29.0f, "still below the dive speed threshold");
    step_world(0, 80, B_BUTTON, 1);
    check(sWorld.marioState.action == ACT_MOVE_PUNCHING,
          "B below 29 forward speed punches");

    /* At full running speed it dives instead. */
    setup_flat_world();
    step_world(0, 80, 0, 60);
    check(sWorld.marioState.forwardVel >= 29.0f, "at running speed above the threshold");
    step_world(0, 80, B_BUTTON, 1);
    check(sWorld.marioState.action == ACT_DIVE, "B at speed dives");
}

static void test_wall_kick_window(void) {
    section("wall kick window");

    /*
     * ACT_AIR_HIT_WALL accepts A for exactly two frames.  Tested directly on the
     * action rather than by flying into a wall, so the window itself is measured
     * rather than the approach.
     */
    setup_flat_world();
    sWorld.marioState.action = ACT_AIR_HIT_WALL;
    sWorld.marioState.actionTimer = 0;
    sWorld.marioState.forwardVel = 20.0f;
    sWorld.marioState.faceAngle[1] = 0;
    sWorld.marioState.input = INPUT_A_PRESSED;
    mario_execute_airborne_action(&sWorld.marioState);
    check(sWorld.marioState.action == ACT_WALL_KICK_AIR,
          "A on the first frame of a wall hit kicks");
    check_near(sWorld.marioState.vel[1], 52.0f, 0.001f,
               "a wall kick launches at 52, higher than a jump");
    check(sWorld.marioState.faceAngle[1] == (s16) 0x8000,
          "a wall kick flips facing 180 degrees");

    /* Three frames in, the window has closed. */
    setup_flat_world();
    sWorld.marioState.action = ACT_AIR_HIT_WALL;
    sWorld.marioState.actionTimer = 3;
    sWorld.marioState.forwardVel = 10.0f;
    sWorld.marioState.input = INPUT_A_PRESSED;
    mario_execute_airborne_action(&sWorld.marioState);
    check(sWorld.marioState.action != ACT_WALL_KICK_AIR,
          "A after two frames is too late to wall kick");
}

/* --- Regressions ------------------------------------------------------- */

/*
 * Stick direction must map to the screen the way a player expects: push right,
 * go right ON SCREEN.
 *
 * This is checked by rendering a frame and looking at where the player actually
 * ended up in the image, rather than by asserting a world axis. That matters
 * because the obvious world-space assertion ("stick right increases X") is only
 * correct if you already know the renderer's handedness -- and if you get that
 * wrong, the test and the code agree with each other while the controls are
 * inverted for the person holding the pad. Pixels cannot be argued with.
 */
static s32 rendered_player_column(struct M64World *w, struct M64Framebuffer *fb) {
    long sum = 0;
    long count = 0;
    s32 x, y;

    /*
     * Render from a FIXED viewpoint south of the origin, not from the follow
     * camera. The follow camera re-centres on the player every frame, so with
     * it the player never moves on screen at all and there is nothing to
     * measure. The fixed eye turns world displacement into screen displacement.
     */
    Vec3f eye = { 0.0f, 350.0f, -900.0f };
    Vec3f focus = { 0.0f, 120.0f, 0.0f };

    m64_fb_clear_sky(fb);
    m64_render_set_camera(fb, eye, focus, 55.0f);
    m64_render_capsule(fb, w->marioState.pos, w->marioState.faceAngle[1], 45.0f, 160.0f,
                       0xE04040);

    /* The capsule is the only strongly red thing in frame. */
    for (y = 0; y < fb->height; y++) {
        for (x = 0; x < fb->width; x++) {
            u32 c = fb->color[y * fb->width + x];
            s32 r = (s32) ((c >> 16) & 0xFF);
            s32 g = (s32) ((c >> 8) & 0xFF);
            s32 b = (s32) (c & 0xFF);

            if (r > 120 && g < 80 && b < 80) {
                sum += x;
                count++;
            }
        }
    }
    return count > 0 ? (s32) (sum / count) : -1;
}

static void test_stick_directions(void) {
    struct M64Framebuffer fb;
    s32 centre;

    section("stick maps to the screen correctly");

    if (m64_fb_create(&fb, 640, 360) != 0) {
        check(FALSE, "framebuffer for the render check");
        return;
    }
    centre = fb.width / 2;

    /* Camera yaw 0x8000 puts the viewpoint due south of the player. */
    setup_flat_world();
    sWorld.camera.yaw = (s16) 0x8000;
    step_world(80, 0, 0, 22);
    check(rendered_player_column(&sWorld, &fb) > centre + 40,
          "stick RIGHT moves the player right on screen");

    setup_flat_world();
    sWorld.camera.yaw = (s16) 0x8000;
    step_world(-80, 0, 0, 22);
    {
        s32 col = rendered_player_column(&sWorld, &fb);

        check(col > 0 && col < centre - 40, "stick LEFT moves the player left on screen");
    }

    m64_fb_destroy(&fb);

    /*
     * Forward and back need no rendering: the viewpoint sits at negative Z, so
     * "away from the camera" is simply increasing Z. No handedness involved.
     */
    setup_flat_world();
    sWorld.camera.yaw = (s16) 0x8000;
    step_world(0, 80, 0, 22);
    check(sWorld.marioState.pos[2] > 100.0f,
          "stick UP moves the player away from the camera");

    setup_flat_world();
    sWorld.camera.yaw = (s16) 0x8000;
    step_world(0, -80, 0, 22);
    check(sWorld.marioState.pos[2] < -100.0f,
          "stick DOWN moves the player toward the camera");
}

/*
 * Holding the stick straight ahead must run straight. The camera auto-follows
 * the player's facing, so any error between "what the stick asked for" and
 * "where the camera thinks behind is" gets fed back every frame -- if it does
 * not cancel exactly, the player curves away on their own and the controls feel
 * like they are drifting.
 */
static void test_straight_running_does_not_drift(void) {
    s16 startYaw;
    s16 drift;

    section("straight running stability");

    setup_flat_world();
    startYaw = sWorld.marioState.faceAngle[1];

    /* Camera left free to follow, as in normal play. */
    step_world_free_camera(0, 80, 0, 120);

    drift = (s16) (sWorld.marioState.faceAngle[1] - startYaw);
    if (drift < 0) {
        drift = (s16) -drift;
    }

    /* Under an eighth of a turn over four seconds of running. */
    check(drift < 0x800, "holding forward for 120 frames does not curve away");
    check(sWorld.marioState.pos[0] > -250.0f && sWorld.marioState.pos[0] < 250.0f,
          "holding forward stays on the lane it started in");
}

static void test_double_jump_keeps_speed(void) {
    section("double jump preserves momentum");

    /*
     * The double jump used to set forwardVel to zero, which stopped a running
     * player dead in mid-air on the second jump of a chain.  It sheds a fifth
     * of the speed; it does not kill it.
     */
    setup_flat_world();
    sWorld.marioState.forwardVel = 30.0f;
    set_mario_action(&sWorld.marioState, ACT_DOUBLE_JUMP, 0);
    check(sWorld.marioState.forwardVel > 0.0f,
          "a double jump does not zero forward speed");
    check_near(sWorld.marioState.forwardVel, 24.0f, 0.001f,
               "a double jump keeps 80 percent of forward speed");

    /* And end to end: a chain run at speed must still be travelling at the
     * end of the second jump. */
    setup_flat_world();
    step_world(0, 80, 0, 40);
    {
        f32 speedBefore = sWorld.marioState.forwardVel;
        s32 i;

        step_world(0, 80, A_BUTTON, 2);
        for (i = 0; i < 60 && (sWorld.marioState.action & ACT_GROUP_MASK)
                                  == ACT_GROUP_AIRBORNE; i++) {
            step_world(0, 80, 0, 1);
        }
        step_world(0, 80, A_BUTTON, 2);
        check(sWorld.marioState.action == ACT_DOUBLE_JUMP, "chain reached the double jump");
        check(sWorld.marioState.forwardVel > speedBefore * 0.5f,
              "the double jump in a running chain retains most of its speed");
    }
}

/*
 * A full 180 reversal at speed. This is the bug that made the character stand
 * still: act_turning_around discarded the signal that deceleration had
 * finished, so it never handed off to ACT_FINISH_TURNING_AROUND and fell
 * through to braking instead.
 */
static void test_turnaround_completes(void) {
    s32 i;
    s32 sawTurning = FALSE;
    s32 sawFinish = FALSE;
    f32 speedBefore;

    section("180 degree turnaround");

    setup_flat_world();
    sWorld.marioState.faceAngle[1] = 0; /* facing +Z */
    step_world(0, 80, 0, 45);
    speedBefore = sWorld.marioState.forwardVel;
    check(speedBefore > 25.0f, "reached running speed before reversing");
    check(sWorld.marioState.vel[2] > 0.0f, "moving along +Z to begin with");

    /* Slam the stick the other way. */
    for (i = 0; i < 110; i++) {
        step_world(0, -80, 0, 1);
        if (sWorld.marioState.action == ACT_TURNING_AROUND) {
            sawTurning = TRUE;
        }
        if (sWorld.marioState.action == ACT_FINISH_TURNING_AROUND) {
            sawFinish = TRUE;
        }
    }

    check(sawTurning, "reversing the stick at speed enters ACT_TURNING_AROUND");
    check(sawFinish, "the turnaround reaches ACT_FINISH_TURNING_AROUND");
    check(sWorld.marioState.action == ACT_WALKING,
          "the turnaround ends in ACT_WALKING, not standing still");
    check(sWorld.marioState.action != ACT_IDLE && sWorld.marioState.action != ACT_BRAKING_STOP,
          "the player does not end the reversal stopped");
    check(sWorld.marioState.vel[2] < 0.0f, "now travelling along -Z: the reversal happened");
    check(sWorld.marioState.forwardVel > 20.0f,
          "speed is rebuilt in the new direction rather than lost");
}

/*
 * The collision facing must not be flipped per frame during the turn -- only
 * the displayed yaw is.  Flipping the real one made the velocity direction
 * alternate every frame and the player go nowhere.
 */
static void test_turnaround_facing_is_stable(void) {
    s32 i;
    s32 flips = 0;
    s16 previous;

    section("turnaround facing stability");

    setup_flat_world();
    sWorld.marioState.faceAngle[1] = 0;
    step_world(0, 80, 0, 45);

    previous = sWorld.marioState.faceAngle[1];
    for (i = 0; i < 80; i++) {
        s16 delta;

        step_world(0, -80, 0, 1);
        delta = (s16) (sWorld.marioState.faceAngle[1] - previous);

        /* Ground turning is capped at 0x800 per frame. Anything near a half
         * turn in one frame is the facing being flipped outright. */
        if (delta > 0x4000 || delta < -0x4000) {
            flips++;
        }
        previous = sWorld.marioState.faceAngle[1];
    }
    check(flips == 0, "collision facing never jumps by half a turn in one frame");
}

/* A side flip had no launch velocity at all, so pressing A mid-turn did
 * nothing visible. */
static void test_side_flip(void) {
    s32 i;

    section("side flip out of a turnaround");

    setup_flat_world();
    sWorld.marioState.forwardVel = 20.0f;
    set_mario_action(&sWorld.marioState, ACT_SIDE_FLIP, 0);
    check_near(sWorld.marioState.vel[1], 62.0f, 0.001f,
               "a side flip launches at 62, matching a backflip");

    /* And by the real route: reverse at speed, then press A mid-turn. */
    setup_flat_world();
    sWorld.marioState.faceAngle[1] = 0;
    step_world(0, 80, 0, 45);
    for (i = 0; i < 40; i++) {
        step_world(0, -80, 0, 1);
        if (sWorld.marioState.action == ACT_TURNING_AROUND) {
            break;
        }
    }
    check(sWorld.marioState.action == ACT_TURNING_AROUND, "mid-turnaround");
    step_world(0, -80, A_BUTTON, 1);
    check(sWorld.marioState.action == ACT_SIDE_FLIP,
          "A during a turnaround gives a side flip, not a plain jump");
    check(sWorld.marioState.vel[1] > 50.0f, "the side flip actually leaves the ground");
}

/* --- Level integrity ---------------------------------------------------- */

/*
 * Geometry built with the wrong winding is not visibly wrong -- it is the wrong
 * KIND of surface. A floor wound backwards becomes a ceiling: not standable and
 * invisible from above, which from inside the level reads as geometry you fall
 * straight through. The whole slope ladder shipped that way once, so the level
 * now gets asserted rather than eyeballed.
 */
static void test_level_geometry(void) {
    struct Surface *floor;
    s32 i;
    s32 floors = 0;
    s32 ceils = 0;
    s32 walls = 0;

    section("demo level geometry");

    m64_world_init(&sWorld);

    /* Every landmark must have standable ground at it, or the demo scripts
     * that warp there start by falling out of the world. */
    for (i = 0; i < m64_level_landmark_count(); i++) {
        const struct M64Landmark *lm = m64_level_landmark(i);
        char label[96];
        f32 height = find_floor(lm->pos[0], lm->pos[1] + 200.0f, lm->pos[2], &floor);

        snprintf(label, sizeof(label), "landmark '%s' has ground under it", lm->name);
        check(floor != NULL && height > FLOOR_LOWER_LIMIT_MISC, label);
    }

    /*
     * Each ramp must be climbable: sample the middle of the slope and confirm
     * there is a floor there, above the surrounding ground. A ramp built as a
     * ceiling reports the ground plane instead.
     */
    {
        static const char *const rampNames[6] = { "ramp_10deg",   "ramp_20deg",
                                                  "ramp_30deg",   "ramp_45deg",
                                                  "ramp_slippery", "ramp_ice" };

        for (i = 0; i < 6; i++) {
            const struct M64Landmark *lm = m64_level_landmark_by_name(rampNames[i]);
            char label[96];
            f32 height;

            snprintf(label, sizeof(label), "%s exists", rampNames[i]);
            check(lm != NULL, label);
            if (lm == NULL) {
                continue;
            }

            /* The approach landmark sits 350 short of the ramp foot, which
             * climbs away along -Z; sample 750 further along, mid-slope. */
            height = find_floor(lm->pos[0], 2000.0f, lm->pos[2] - 750.0f, &floor);

            snprintf(label, sizeof(label), "%s is a standable slope, not a ceiling",
                     rampNames[i]);
            check(floor != NULL && height > 20.0f && floor->normal.y > 0.0f, label);
        }
    }

    /* The wall-kick shafts must have walls facing into the gap, or there is
     * nothing to kick off. */
    {
        static const char *const shaftNames[3] = { "shaft_narrow_in", "shaft_medium_in",
                                                   "shaft_wide_in" };

        for (i = 0; i < 3; i++) {
            const struct M64Landmark *lm = m64_level_landmark_by_name(shaftNames[i]);
            struct WallCollisionData col;
            char label[96];

            snprintf(label, sizeof(label), "%s exists", shaftNames[i]);
            check(lm != NULL, label);
            if (lm == NULL) {
                continue;
            }

            col.x = lm->pos[0];
            col.y = lm->pos[1] + 60.0f;
            col.z = lm->pos[2];
            col.radius = 50.0f;
            col.offsetY = 0.0f;

            snprintf(label, sizeof(label), "%s is beside a kickable wall", shaftNames[i]);
            check(find_wall_collisions(&col) > 0, label);
        }
    }

    /* A sanity census: the level should be a mix of all three surface kinds.
     * An all-floors or all-ceilings level means the classifier or the winding
     * has gone wrong wholesale. */
    for (i = 0; i < m64_surface_count(); i++) {
        struct Surface *surf = m64_surface_at(i);

        if (surf->normal.y > 0.01f) {
            floors++;
        } else if (surf->normal.y < -0.01f) {
            ceils++;
        } else {
            walls++;
        }
    }
    check(floors > 50, "level has a substantial number of floor surfaces");
    check(ceils > 20, "level has ceiling surfaces (solids are closed underneath)");
    check(walls > 50, "level has wall surfaces");
}

/* --- Integration ------------------------------------------------------- */

static void test_demo_level_runs(void) {
    s32 i;
    s32 sane = TRUE;

    section("demo level integration");

    m64_world_init(&sWorld);
    check(m64_surface_count() > 100, "the demo level built a nontrivial triangle soup");
    check(m64_level_landmark_count() > 5, "the demo level registered landmarks");

    /* Run forward for a while and confirm nothing diverges or leaves the world. */
    for (i = 0; i < 300; i++) {
        m64_world_step(&sWorld, 0, 80, (i % 40 == 0) ? A_BUTTON : 0, 0);

        if (!(sWorld.marioState.pos[0] > -20000.0f && sWorld.marioState.pos[0] < 20000.0f)
            || !(sWorld.marioState.pos[1] > -20000.0f && sWorld.marioState.pos[1] < 20000.0f)
            || sWorld.marioState.pos[1] != sWorld.marioState.pos[1] /* NaN */) {
            sane = FALSE;
            break;
        }
    }
    check(sane, "300 frames of scripted movement stays finite and in bounds");
    check(sWorld.marioState.floor != NULL, "the player still has a floor at the end");
}

int main(void) {
    printf("new64 test suite\n");

    test_angle_math();
    test_collision();
    test_jump_velocities();
    test_gravity();
    test_ground_movement();
    test_slope_classes();
    test_air_control();
    test_jump_chain();
    test_crouch_moves();
    test_dive_threshold();
    test_wall_kick_window();
    test_stick_directions();
    test_straight_running_does_not_drift();
    test_double_jump_keeps_speed();
    test_turnaround_completes();
    test_turnaround_facing_is_stable();
    test_side_flip();
    test_level_geometry();
    test_demo_level_runs();

    printf("\n==================================\n");
    printf("passed: %d   failed: %d\n", sPassed, sFailed);
    printf("==================================\n");

    return sFailed == 0 ? 0 : 1;
}
