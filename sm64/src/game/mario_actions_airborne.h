/* new64 -- airborne action group. */
#ifndef NEW64_MARIO_ACTIONS_AIRBORNE_H
#define NEW64_MARIO_ACTIONS_AIRBORNE_H

#include "types.h"

/*
 * Air control, in two flavours.  Both give the same modest forward authority
 * (1.5 per frame) and the same drag; they differ in what the stick's sideways
 * component does:
 *
 *   without_turn : adds a *sideways velocity* without changing facing, so you
 *                  drift.  Used by most jumps -- it is why you cannot rotate
 *                  mid-jump but can still adjust where you land.
 *   with_turn    : rotates facing instead (up to 512 units/frame scaled by
 *                  stick magnitude).  Used where a jump is allowed to steer.
 */
void update_air_without_turn(struct MarioState *m);
void update_air_with_turn(struct MarioState *m);

s32 common_air_action_step(struct MarioState *m, u32 landAction, s32 animation,
                           u32 stepArg);

void play_flip_sounds(struct MarioState *m, s16 frame1, s16 frame2, s16 frame3);
void play_far_fall_sound(struct MarioState *m);

s32 mario_execute_airborne_action(struct MarioState *m);

#endif /* NEW64_MARIO_ACTIONS_AIRBORNE_H */
