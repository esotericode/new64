/*
 * new64 -- submerged group.
 *
 * Swimming is not implemented: the demo level has no water, and swimming is a
 * second complete movement model rather than a variation on this one.  What is
 * here is the minimum that keeps the state machine sound if a level *does*
 * define a water plane -- you plunge, sink to the floor, and walk out -- so the
 * player can never become stuck in an unhandled state.
 *
 * Slotting in a real submerged implementation is supported; see
 * docs/SLOTTING_IN_DECOMP.md.  perform_water_step() in mario_step.c is already
 * present and functional for that purpose.
 */
#include "mario_actions_submerged.h"

#include "engine/math_util.h"
#include "mario.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "sm64.h"
#include "types.h"

s32 mario_execute_submerged_action(struct MarioState *m) {
    /* Back on dry land: hand control to the walking code. */
    if (m->pos[1] > (f32) m->waterLevel - 80.0f && m->floor != NULL
        && m->pos[1] <= m->floorHeight + 10.0f) {
        return transition_submerged_to_walking(m);
    }

    m->vel[1] = -20.0f;
    set_vel_from_yaw(m);
    m->vel[1] = -20.0f;

    if (perform_water_step(m) == WATER_STEP_HIT_FLOOR) {
        return transition_submerged_to_walking(m);
    }

    set_mario_animation(m, MARIO_ANIM_WATER_IDLE);
    return FALSE;
}
