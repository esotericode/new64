/*
 * new64 -- the movement substrate: how a position request becomes a position.
 *
 * Action code never writes m->pos directly.  It sets a velocity and calls one
 * of the step functions, which does collision, clamps, and reports what
 * happened.  The return codes below are the entire language in which collision
 * talks back to the state machine, so their meanings matter:
 *
 *   GROUND_STEP_LEFT_GROUND            walked off an edge; go airborne
 *   GROUND_STEP_NONE                   moved cleanly (or glanced a wall)
 *   GROUND_STEP_HIT_WALL               blocked by a wall
 *   *_STOP_QSTEP / *_CONTINUE_QSTEP    internal: whether the remaining quarter
 *                                      steps should still run this frame
 *
 *   AIR_STEP_NONE           still airborne
 *   AIR_STEP_LANDED         touched a floor
 *   AIR_STEP_HIT_WALL       hit a wall steeply enough to matter (wall kicks)
 *   AIR_STEP_GRABBED_LEDGE  caught a lip on the way down
 *   AIR_STEP_GRABBED_CEILING hung from a hangable ceiling
 */
#ifndef NEW64_MARIO_STEP_H
#define NEW64_MARIO_STEP_H

#include "types.h"

/* stepArg bits: opt-in checks the caller wants performed during an air step. */
#define AIR_STEP_CHECK_LEDGE_GRAB 0x00000001
#define AIR_STEP_CHECK_HANG       0x00000002

enum GroundStep {
    GROUND_STEP_LEFT_GROUND,
    GROUND_STEP_NONE,
    GROUND_STEP_HIT_WALL,
    GROUND_STEP_HIT_WALL_STOP_QSTEP,
    GROUND_STEP_HIT_WALL_CONTINUE_QSTEP
};

enum AirStep {
    AIR_STEP_NONE,
    AIR_STEP_LANDED,
    AIR_STEP_HIT_WALL,
    AIR_STEP_GRABBED_LEDGE,
    AIR_STEP_GRABBED_CEILING,
    AIR_STEP_HIT_LAVA_WALL = 6
};

enum WaterStep {
    WATER_STEP_NONE,
    WATER_STEP_HIT_FLOOR,
    WATER_STEP_HIT_CEILING,
    WATER_STEP_CANCELLED,
    WATER_STEP_HIT_WALL
};

enum HangStep { HANG_NONE, HANG_HIT_CEIL_OR_OOB, HANG_LEFT_CEIL };

void stop_and_set_height_to_floor(struct MarioState *m);
void set_vel_from_pitch_and_yaw(struct MarioState *m);
void set_vel_from_yaw(struct MarioState *m);
void apply_gravity(struct MarioState *m);
void mario_bonk_reflection(struct MarioState *m, u32 negateSpeed);
u32 mario_push_off_steep_floor(struct MarioState *m, u32 action, u32 actionArg);
u32 mario_update_quicksand(struct MarioState *m, f32 sinkingSpeed);
u32 mario_update_moving_sand(struct MarioState *m);
u32 mario_update_windy_ground(struct MarioState *m);

f32 get_additive_y_vel_for_jumps(void);

s32 stationary_ground_step(struct MarioState *m);
s32 perform_ground_step(struct MarioState *m);
s32 perform_air_step(struct MarioState *m, u32 stepArg);
s32 perform_hanging_step(struct MarioState *m, Vec3f nextPos);
s32 perform_water_step(struct MarioState *m);

/* Ceiling above `pos` but never below `height`; used to keep a ceiling query
 * anchored to the floor rather than to the body's current Y. */
f32 vec3f_find_ceil(Vec3f pos, f32 height, struct Surface **ceil);

/* Push `pos` out of any walls within `radius` at `offset` above pos[1], and
 * report the last wall that pushed (which is the one actions react to). */
struct Surface *resolve_and_return_wall_collisions(Vec3f pos, f32 offset, f32 radius);

#endif /* NEW64_MARIO_STEP_H */
