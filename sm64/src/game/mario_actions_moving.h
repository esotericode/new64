/*
 * new64 -- ground movement group.
 *
 * The helpers below are shared with the other action groups: landing code in
 * the airborne group needs the same friction and slope handling as walking, so
 * those routines live here and are declared for cross-group use.
 */
#ifndef NEW64_MARIO_ACTIONS_MOVING_H
#define NEW64_MARIO_ACTIONS_MOVING_H

#include "types.h"

/*
 * Add gravity's pull along the current floor to forwardVel, then rebuild the
 * derived slide velocity.  Call this at the end of any routine that changed
 * forwardVel on the ground, or the slope will be ignored for a frame.
 */
void apply_slope_accel(struct MarioState *m);

/* Landing friction. Returns TRUE once speed has reached zero. */
s32 apply_landing_accel(struct MarioState *m, f32 frictionFactor);

/* Class-dependent deceleration. Returns TRUE once stopped. */
s32 apply_slope_decel(struct MarioState *m, f32 decelCoef);

s32 update_decelerating_speed(struct MarioState *m);
void update_walking_speed(struct MarioState *m);
void update_sliding_angle(struct MarioState *m, f32 accel, f32 lossFactor);
s32 update_sliding(struct MarioState *m, f32 stopSpeed);

s32 begin_braking_action(struct MarioState *m);
void anim_and_audio_for_walk(struct MarioState *m);

/* TRUE when the stick points more than ~100 degrees away from facing, which is
 * the threshold for "the player asked to turn around" rather than to steer. */
s32 analog_stick_held_back(struct MarioState *m);

s32 should_begin_sliding(struct MarioState *m);
s32 check_ground_dive_or_punch(struct MarioState *m);
void align_with_floor(struct MarioState *m);

s32 mario_execute_moving_action(struct MarioState *m);

#endif /* NEW64_MARIO_ACTIONS_MOVING_H */
