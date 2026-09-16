/*
 * new64 -- stationary actions: idle, crouch, the land-stop states.
 *
 * These are the states where the player is *not* translating, and they exist
 * mainly to host transitions.  Two of them carry real mechanics:
 *
 *   ACT_CROUCHING     A while crouched is a backflip, and stick input is a
 *                     crawl.  Crouching is the gateway to half the moveset.
 *   the *_LAND_STOP   states keep the jump chain alive after the brief land
 *                     action has expired, so a slightly late A press still
 *                     escalates instead of restarting.
 *
 * Note that "stationary" still calls stationary_ground_step(), which zeroes
 * horizontal velocity but *does* re-snap you to the floor -- so standing on a
 * surface that moves under you still works.
 */
#include "mario_actions_stationary.h"

#include "area.h"
#include "audio/external.h"
#include "audio_defines.h"
#include "camera.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "mario_actions_moving.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "object_fields.h"
#include "rumble_init.h"
#include "sm64.h"
#include "surface_terrains.h"

#include <stddef.h>

/*
 * cos(73 degrees).  A floor shallower than this cannot be stood on at all, and
 * every stationary action bails out to a slide or a fall when it finds one.
 * This is a harder limit than mario_floor_is_steep(), which is about whether
 * you *keep your footing*.
 */
#define MIN_STANDABLE_NORMAL_Y 0.29237169f

static s32 check_common_idle_cancels(struct MarioState *m) {
    if (m->floor != NULL && m->floor->normal.y < MIN_STANDABLE_NORMAL_Y) {
        return mario_push_off_steep_floor(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 0);
    }
    if (m->input & INPUT_Z_DOWN) {
        return set_mario_action(m, ACT_START_CROUCHING, 0);
    }
    return FALSE;
}

static s32 check_common_hold_idle_cancels(struct MarioState *m) {
    if (m->floor != NULL && m->floor->normal.y < MIN_STANDABLE_NORMAL_Y) {
        return mario_push_off_steep_floor(m, ACT_HOLD_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_HOLD_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_HOLD_FREEFALL, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_HOLD_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
    return FALSE;
}

/* Like landing_step, but waits for the animation to run *past* its end, giving
 * a slightly longer recovery. Used where the state should feel committal. */
static s32 stopping_step(struct MarioState *m, s32 animation, u32 action) {
    stationary_ground_step(m);
    set_mario_animation(m, animation);
    if (is_anim_past_end(m)) {
        return set_mario_action(m, action, 0);
    }
    return FALSE;
}

static s32 act_idle(struct MarioState *m) {
    if (m->quicksandDepth > 30.0f) {
        return set_mario_action(m, ACT_IN_QUICKSAND, 0);
    }
    if (m->input & INPUT_IN_POISON_GAS) {
        return set_mario_action(m, ACT_COUGHING, 0);
    }
    if (check_common_idle_cancels(m)) {
        return TRUE;
    }

    stationary_ground_step(m);

    /* Idle cycles between three head positions so a standing character is not
     * perfectly static; harmless with a capsule, kept for parity. */
    switch (m->actionState) {
        case 0:  set_mario_animation(m, MARIO_ANIM_IDLE_HEAD_LEFT); break;
        case 1:  set_mario_animation(m, MARIO_ANIM_IDLE_HEAD_RIGHT); break;
        default: set_mario_animation(m, MARIO_ANIM_IDLE_HEAD_CENTER); break;
    }
    if (is_anim_at_end(m)) {
        if (++m->actionState > 2) {
            m->actionState = 0;
        }
    }
    return FALSE;
}

static s32 act_hold_idle(struct MarioState *m) {
    if (check_common_hold_idle_cancels(m)) {
        return TRUE;
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_IDLE_WITH_LIGHT_OBJ);
    return FALSE;
}

static s32 act_in_quicksand(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_IDLE_IN_QUICKSAND);
    return FALSE;
}

static s32 act_coughing(struct MarioState *m) {
    if (check_common_idle_cancels(m)) {
        return TRUE;
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_COUGHING);
    return FALSE;
}

static s32 act_panting(struct MarioState *m) {
    if (check_common_idle_cancels(m)) {
        return TRUE;
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_WALK_PANTING);
    play_sound_if_no_flag(m, SOUND_MARIO_PANTING, MARIO_MARIO_SOUND_PLAYED);
    return FALSE;
}

static s32 act_braking_stop(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 0);
    }
    /* The skid recovery plays out and then releases to idle. Without a
     * terminating step here the state never ends on its own. */
    return stopping_step(m, MARIO_ANIM_STOP_SKID, ACT_IDLE);
}

static s32 act_standing_against_wall(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 0);
    }
    if (m->input & INPUT_Z_DOWN) {
        return set_mario_action(m, ACT_START_CROUCHING, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_STAND_AGAINST_WALL);
    return FALSE;
}

/* --- Crouching ---------------------------------------------------------- */

static s32 act_crouching(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        /* Crouch + A is a backflip. One of the most important two-button
         * combinations in the moveset. */
        return set_mario_action(m, ACT_BACKFLIP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (!(m->input & INPUT_Z_DOWN)) {
        return set_mario_action(m, ACT_STOP_CROUCHING, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_START_CRAWLING, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 9);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_CROUCHING);
    return FALSE;
}

static s32 act_start_crouching(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_BACKFLIP, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 9);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_START_CROUCHING);
    if (is_anim_past_end(m)) {
        set_mario_action(m, ACT_CROUCHING, 0);
    }
    return FALSE;
}

static s32 act_stop_crouching(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_PUNCHING, 0);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_STOP_CROUCHING);
    if (is_anim_past_end(m)) {
        set_mario_action(m, ACT_IDLE, 0);
    }
    return FALSE;
}

static s32 act_start_crawling(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (!(m->input & INPUT_Z_DOWN)) {
        return set_mario_action(m, ACT_STOP_CRAWLING, 0);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_START_CRAWLING);
    if (is_anim_past_end(m)) {
        set_mario_action(m, ACT_CRAWLING, 0);
    }
    return FALSE;
}

static s32 act_stop_crawling(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    stationary_ground_step(m);
    set_mario_animation(m, MARIO_ANIM_STOP_CRAWLING);
    if (is_anim_past_end(m)) {
        set_mario_action(m, ACT_CROUCHING, 0);
    }
    return FALSE;
}

/* --- Land-stop states --------------------------------------------------- */

/*
 * The land-stop states are what keep a slightly-late A press meaningful.  The
 * `action` argument is the jump this state chains into; ACT_UNINITIALIZED means
 * "no chain, give a plain jump".
 */
static s32 check_common_landing_cancels(struct MarioState *m, u32 action) {
    if (m->waterLevel > FLOOR_LOWER_LIMIT_MISC && m->pos[1] < (f32) (m->waterLevel - 100)) {
        return set_water_plunge_action(m);
    }
    if (m->input & INPUT_A_PRESSED) {
        if (action == ACT_UNINITIALIZED) {
            return set_jumping_action(m, ACT_JUMP, 0);
        }
        return set_jumping_action(m, action, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    return FALSE;
}

static s32 landing_step(struct MarioState *m, s32 animation, u32 action) {
    stationary_ground_step(m);
    set_mario_animation(m, animation);
    if (is_anim_at_end(m)) {
        return set_mario_action(m, action, 0);
    }
    return FALSE;
}

static s32 act_jump_land_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, ACT_DOUBLE_JUMP)) {
        return TRUE;
    }
    return landing_step(m, MARIO_ANIM_LAND_FROM_SINGLE_JUMP, ACT_IDLE);
}

static s32 act_double_jump_land_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, ACT_JUMP)) {
        return TRUE;
    }
    return landing_step(m, MARIO_ANIM_LAND_FROM_DOUBLE_JUMP, ACT_IDLE);
}

static s32 act_side_flip_land_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, ACT_DOUBLE_JUMP)) {
        return TRUE;
    }
    return landing_step(m, MARIO_ANIM_SLIDEFLIP_LAND, ACT_IDLE);
}

static s32 act_freefall_land_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, ACT_DOUBLE_JUMP)) {
        return TRUE;
    }
    return landing_step(m, MARIO_ANIM_GENERAL_LAND, ACT_IDLE);
}

/* A triple jump and a backflip both end the chain: pressing A gives a plain
 * jump rather than escalating further. */
static s32 act_triple_jump_land_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, ACT_UNINITIALIZED)) {
        return TRUE;
    }
    return landing_step(m, MARIO_ANIM_TRIPLE_JUMP_LAND, ACT_IDLE);
}

static s32 act_backflip_land_stop(struct MarioState *m) {
    if (!(m->input & INPUT_Z_DOWN) || m->actionState >= 4) {
        m->input &= ~INPUT_A_PRESSED;
    }
    if (check_common_landing_cancels(m, ACT_BACKFLIP)) {
        return TRUE;
    }
    landing_step(m, MARIO_ANIM_TRIPLE_JUMP_LAND, ACT_IDLE);
    m->actionState++;
    return FALSE;
}

static s32 act_long_jump_land_stop(struct MarioState *m) {
    m->input &= ~INPUT_B_PRESSED;
    if (check_common_landing_cancels(m, ACT_LONG_JUMP)) {
        return TRUE;
    }
    return landing_step(m,
                        m->marioObj->oMarioLongJumpIsSlow
                            ? MARIO_ANIM_CROUCH_FROM_SLOW_LONGJUMP
                            : MARIO_ANIM_CROUCH_FROM_FAST_LONGJUMP,
                        ACT_CROUCHING);
}

/* Heavy landings: a ground pound and a butt slide both end in a crouch rather
 * than standing, which is what lets you chain straight into another move. */
static s32 act_ground_pound_land(struct MarioState *m) {
    m->actionState = 1;
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    landing_step(m, MARIO_ANIM_GROUND_POUND_LANDING, ACT_BUTT_SLIDE_STOP);
    return FALSE;
}

static s32 act_butt_slide_stop(struct MarioState *m) {
    if (check_common_landing_cancels(m, 0)) {
        return TRUE;
    }
    landing_step(m, MARIO_ANIM_STOP_SLIDE, ACT_IDLE);
    return FALSE;
}

static s32 act_stomach_slide_stop(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    landing_step(m, MARIO_ANIM_STOP_SLIDE, ACT_IDLE);
    return FALSE;
}

static s32 act_slide_kick_slide_stop(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    stopping_step(m, MARIO_ANIM_CROUCH_FROM_SLIDE_KICK, ACT_CROUCHING);
    return FALSE;
}

static s32 act_twirl_land(struct MarioState *m) {
    m->actionState = 1;
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    set_mario_animation(m, MARIO_ANIM_TWIRL_LAND);
    stationary_ground_step(m);
    if (is_anim_at_end(m)) {
        set_mario_action(m, ACT_IDLE, 0);
    }
    m->marioObj->header.gfx.angle[1] += m->twirlYaw;
    return FALSE;
}

/*
 * First person has no camera to enter in new64, so it resolves straight back to
 * idle.  Keeping the state reachable (rather than deleting the transition)
 * means hosted code that enters it still behaves predictably.
 */
static s32 act_first_person(struct MarioState *m) {
    return set_mario_action(m, ACT_IDLE, 0);
}

static s32 act_shockwave_bounce(struct MarioState *m) {
    if (m->actionTimer++ >= 12) {
        return set_mario_action(m, ACT_IDLE, 0);
    }
    stop_and_set_height_to_floor(m);
    set_mario_animation(m, MARIO_ANIM_A_POSE);
    return FALSE;
}

/* --- Group dispatch ----------------------------------------------------- */

static s32 check_common_stationary_cancels(struct MarioState *m) {
    if (m->waterLevel > FLOOR_LOWER_LIMIT_MISC && m->pos[1] < (f32) (m->waterLevel - 100)) {
        return set_water_plunge_action(m);
    }
    if (m->input & INPUT_SQUISHED) {
        return drop_and_set_mario_action(m, ACT_SQUISHED, 0);
    }
    return FALSE;
}

s32 mario_execute_stationary_action(struct MarioState *m) {
    s32 cancel = FALSE;

    if (check_common_stationary_cancels(m)) {
        return TRUE;
    }
    if (mario_update_quicksand(m, 0.5f)) {
        return TRUE;
    }

    switch (m->action) {
        case ACT_IDLE:                   cancel = act_idle(m); break;
        case ACT_HOLD_IDLE:              cancel = act_hold_idle(m); break;
        case ACT_IN_QUICKSAND:           cancel = act_in_quicksand(m); break;
        case ACT_COUGHING:               cancel = act_coughing(m); break;
        case ACT_PANTING:                cancel = act_panting(m); break;
        case ACT_BRAKING_STOP:           cancel = act_braking_stop(m); break;
        case ACT_STANDING_AGAINST_WALL:  cancel = act_standing_against_wall(m); break;
        case ACT_CROUCHING:              cancel = act_crouching(m); break;
        case ACT_START_CROUCHING:        cancel = act_start_crouching(m); break;
        case ACT_STOP_CROUCHING:         cancel = act_stop_crouching(m); break;
        case ACT_START_CRAWLING:         cancel = act_start_crawling(m); break;
        case ACT_STOP_CRAWLING:          cancel = act_stop_crawling(m); break;
        case ACT_JUMP_LAND_STOP:         cancel = act_jump_land_stop(m); break;
        case ACT_DOUBLE_JUMP_LAND_STOP:  cancel = act_double_jump_land_stop(m); break;
        case ACT_FREEFALL_LAND_STOP:     cancel = act_freefall_land_stop(m); break;
        case ACT_SIDE_FLIP_LAND_STOP:    cancel = act_side_flip_land_stop(m); break;
        case ACT_TRIPLE_JUMP_LAND_STOP:  cancel = act_triple_jump_land_stop(m); break;
        case ACT_BACKFLIP_LAND_STOP:     cancel = act_backflip_land_stop(m); break;
        case ACT_LONG_JUMP_LAND_STOP:    cancel = act_long_jump_land_stop(m); break;
        case ACT_GROUND_POUND_LAND:      cancel = act_ground_pound_land(m); break;
        case ACT_BUTT_SLIDE_STOP:        cancel = act_butt_slide_stop(m); break;
        case ACT_STOMACH_SLIDE_STOP:     cancel = act_stomach_slide_stop(m); break;
        case ACT_SLIDE_KICK_SLIDE_STOP:  cancel = act_slide_kick_slide_stop(m); break;
        case ACT_TWIRL_LAND:             cancel = act_twirl_land(m); break;
        case ACT_FIRST_PERSON:           cancel = act_first_person(m); break;
        case ACT_SHOCKWAVE_BOUNCE:       cancel = act_shockwave_bounce(m); break;
        case ACT_HOLD_JUMP_LAND_STOP:    cancel = act_jump_land_stop(m); break;
        case ACT_HOLD_FREEFALL_LAND_STOP: cancel = act_freefall_land_stop(m); break;
        default:
            cancel = set_mario_action(m, ACT_IDLE, 0);
            break;
    }
    return cancel;
}
