/*
 * new64 -- cutscene group.
 *
 * new64 has no cutscenes.  The states that matter here are the *failure*
 * states -- squished, dead, disappeared -- which ordinary movement code can
 * enter.  They hold you still for a moment and then return control, so the demo
 * stays playable instead of locking up.
 */
#include "mario_actions_cutscene.h"

#include "mario.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "sm64.h"
#include "types.h"

s32 mario_execute_cutscene_action(struct MarioState *m) {
    switch (m->action) {
        case ACT_SQUISHED:
            if (m->actionTimer++ >= 30) {
                m->squishTimer = 0;
                return set_mario_action(m, ACT_IDLE, 0);
            }
            stop_and_set_height_to_floor(m);
            set_mario_animation(m, MARIO_ANIM_A_POSE);
            return FALSE;

        case ACT_DISAPPEARED:
            if (m->actionTimer++ >= 16) {
                return set_mario_action(m, ACT_IDLE, 0);
            }
            stop_and_set_height_to_floor(m);
            return FALSE;

        default:
            return set_mario_action(m, ACT_IDLE, 0);
    }
}
