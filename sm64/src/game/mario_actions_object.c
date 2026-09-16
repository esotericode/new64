/*
 * new64 -- object-interaction group (punching, picking up, throwing).
 *
 * The demo has no objects to interact with, so the only object action that can
 * actually be reached is punching, which movement code offers from idle and
 * from walking.  It is implemented as a brief committed state so that pressing
 * B has a visible, non-cancelling effect rather than doing nothing.
 */
#include "mario_actions_object.h"

#include "audio/external.h"
#include "audio_defines.h"
#include "engine/math_util.h"
#include "mario.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "sm64.h"
#include "types.h"

s32 mario_execute_object_action(struct MarioState *m) {
    return set_mario_action(m, ACT_IDLE, 0);
}
