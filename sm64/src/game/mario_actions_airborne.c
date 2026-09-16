/*
 * new64 -- airborne movement: every jump, the dive, the ground pound, wall kicks.
 *
 * ========================= AIR CONTROL IS WEAK ============================
 *
 * The single most important thing about SM64's air movement is how little of it
 * there is.  Full stick gives 1.5 units/frame of forward acceleration and 10
 * units of sideways *drift* -- and in most jumps the sideways component does
 * not rotate you at all, it just slides you.  Your trajectory is therefore
 * decided almost entirely at takeoff.  That is what makes the ground game
 * matter: speed and direction are things you commit to before leaving the
 * floor, not things you fix in the air.
 *
 * Drag kicks in above 32 units/frame (48 during a long jump), at 1 per frame.
 * So a long jump keeps its speed while an ordinary jump bleeds back down
 * toward 32.
 *
 * ========================== THE WALL KICK WINDOW ==========================
 *
 * ACT_AIR_HIT_WALL accepts A for exactly *two frames* (see act_air_hit_wall).
 * Miss it and you get a bonk instead.  A successful kick sets vertical velocity
 * to 52 -- higher than a single jump's 42 -- and flips facing 180 degrees.
 * That two-frame window is why wall kicks feel like a skill rather than a
 * feature, and widening it is the most tempting and most flavour-destroying
 * change you could make here.
 */
#include "mario_actions_airborne.h"

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

#include <math.h>
#include <stddef.h>

void play_flip_sounds(struct MarioState *m, s16 frame1, s16 frame2, s16 frame3) {
    s32 animFrame = m->marioObj->header.gfx.animInfo.animFrame;

    if (animFrame == frame1 || animFrame == frame2 || animFrame == frame3) {
        play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
    }
}

/* The "falling a long way" yell, latched so it only fires once per fall. */
void play_far_fall_sound(struct MarioState *m) {
    u32 action = m->action;

    if (!(action & ACT_FLAG_INVULNERABLE) && action != ACT_TWIRLING && action != ACT_FLYING
        && !(m->flags & MARIO_UNKNOWN_18)) {
        if (m->peakHeight - m->pos[1] > 1150.0f) {
            play_sound(SOUND_MARIO_WAAAOOOW, m->marioObj->header.gfx.cameraToObject);
            m->flags |= MARIO_UNKNOWN_18;
        }
    }
}

/*
 * Fall damage needs *both* a high impact speed (below -55 vertical) and a large
 * drop from the arc's peak.  Requiring both is why a long fall onto a slope you
 * slide down does no damage, while a short drop at terminal velocity does.
 */
s32 check_fall_damage(struct MarioState *m, u32 hardFallAction) {
    f32 fallHeight = m->peakHeight - m->pos[1];
    f32 damageHeight = 1150.0f;

    if (m->action != ACT_TWIRLING && m->floor != NULL
        && m->floor->type != SURFACE_BURNING) {
        if (m->vel[1] < -55.0f) {
            if (fallHeight > 3000.0f) {
                m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 16 : 24;
                queue_rumble_data(5, 80);
                set_camera_shake_from_hit(SHAKE_FALL_DAMAGE);
                play_sound(SOUND_MARIO_ATTACKED, m->marioObj->header.gfx.cameraToObject);
                return drop_and_set_mario_action(m, hardFallAction, 4);
            } else if (fallHeight > damageHeight && !mario_floor_is_slippery(m)) {
                m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 8 : 12;
                m->squishTimer = 30;
                queue_rumble_data(5, 80);
                set_camera_shake_from_hit(SHAKE_FALL_DAMAGE);
                play_sound(SOUND_MARIO_ATTACKED, m->marioObj->header.gfx.cameraToObject);
            }
        }
    }
    return FALSE;
}

/*
 * Landing face-first in deep snow or sand sticks you in the ground.  The demo
 * level declares neither terrain, so this is always FALSE here -- but the
 * checks that call it are real, and a level that sets TERRAIN_SNOW would want
 * this implemented.
 */
s32 should_get_stuck_in_ground(struct MarioState *m) {
    (void) m;
    return FALSE;
}

s32 check_fall_damage_or_get_stuck(struct MarioState *m, u32 hardFallAction) {
    if (should_get_stuck_in_ground(m)) {
        return TRUE;
    }
    return check_fall_damage(m, hardFallAction);
}

/*
 * B in the air is a dive above 28 units/frame and a jump kick below it.  Note
 * the ground threshold is 29 with a stick-magnitude requirement; in the air
 * speed alone decides.
 */
s32 check_kick_or_dive_in_air(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, m->forwardVel > 28.0f ? ACT_DIVE : ACT_JUMP_KICK, 0);
    }
    return FALSE;
}

static s32 check_z_pressed_in_air(struct MarioState *m) {
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    return FALSE;
}

void update_air_without_turn(struct MarioState *m) {
    f32 sidewaysSpeed = 0.0f;
    f32 dragThreshold;
    s16 intendedDYaw;
    f32 intendedMag;

    if (!check_horizontal_wind(m)) {
        dragThreshold = (m->action == ACT_LONG_JUMP) ? 48.0f : 32.0f;

        /* A slow constant bleed toward zero, independent of drag. */
        m->forwardVel = approach_f32(m->forwardVel, 0.0f, 0.35f, 0.35f);

        if (m->input & INPUT_NONZERO_ANALOG) {
            intendedDYaw = (s16) (m->intendedYaw - m->faceAngle[1]);
            intendedMag = m->intendedMag / 32.0f;

            /* Forward component accelerates; sideways component becomes a
             * velocity directly, applied perpendicular to facing below.  It is
             * *not* integrated, so releasing the stick removes it instantly. */
            m->forwardVel += 1.5f * coss(intendedDYaw) * intendedMag;
            sidewaysSpeed = 10.0f * intendedMag * sins(intendedDYaw);
        }

        if (m->forwardVel > dragThreshold) {
            m->forwardVel -= 1.0f;
        }
        if (m->forwardVel < -16.0f) {
            m->forwardVel = -16.0f;
        }

        m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
        m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
        m->slideVelX += sidewaysSpeed * sins(m->faceAngle[1] + 0x4000);
        m->slideVelZ += sidewaysSpeed * coss(m->faceAngle[1] + 0x4000);

        m->vel[0] = m->slideVelX;
        m->vel[2] = m->slideVelZ;
    }
}

void update_air_with_turn(struct MarioState *m) {
    f32 dragThreshold;
    s16 intendedDYaw;
    f32 intendedMag;

    if (!check_horizontal_wind(m)) {
        dragThreshold = (m->action == ACT_LONG_JUMP) ? 48.0f : 32.0f;
        m->forwardVel = approach_f32(m->forwardVel, 0.0f, 0.35f, 0.35f);

        if (m->input & INPUT_NONZERO_ANALOG) {
            intendedDYaw = (s16) (m->intendedYaw - m->faceAngle[1]);
            intendedMag = m->intendedMag / 32.0f;

            m->forwardVel += 1.5f * coss(intendedDYaw) * intendedMag;
            /* 512 units/frame is 2.8 degrees at full stick -- a quarter of the
             * ground turn rate. Air steering is deliberately sluggish. */
            m->faceAngle[1] += (s16) (512.0f * sins(intendedDYaw) * intendedMag);
        }

        if (m->forwardVel > dragThreshold) {
            m->forwardVel -= 1.0f;
        }
        if (m->forwardVel < -16.0f) {
            m->forwardVel = -16.0f;
        }

        m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
        m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
        m->vel[0] = m->slideVelX;
        m->vel[2] = m->slideVelZ;
    }
}

/*
 * The shared body of nearly every airborne action: apply air control, step, and
 * translate the step result into a state change.
 *
 * The wall branch is where the speed thresholds live: above 16 you reflect and
 * enter the wall-kick window; above 38 with no wall reference you take real
 * knockback; below that you bonk softly.
 */
s32 common_air_action_step(struct MarioState *m, u32 landAction, s32 animation,
                           u32 stepArg) {
    s32 stepResult;

    update_air_without_turn(m);

    stepResult = perform_air_step(m, stepArg);
    switch (stepResult) {
        case AIR_STEP_NONE:
            set_mario_animation(m, animation);
            break;

        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                set_mario_action(m, landAction, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            set_mario_animation(m, animation);

            if (m->forwardVel > 16.0f) {
                queue_rumble_data(5, 40);
                mario_bonk_reflection(m, FALSE);
                m->faceAngle[1] += 0x8000;

                if (m->wall != NULL) {
                    /* Into the two-frame wall kick window. */
                    set_mario_action(m, ACT_AIR_HIT_WALL, 0);
                } else {
                    if (m->vel[1] > 0.0f) {
                        m->vel[1] = 0.0f;
                    }
                    if (m->forwardVel >= 38.0f) {
                        m->particleFlags |= PARTICLE_VERTICAL_STAR;
                        set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
                    } else {
                        if (m->forwardVel > 8.0f) {
                            mario_set_forward_vel(m, -8.0f);
                        }
                        return set_mario_action(m, ACT_SOFT_BONK, 0);
                    }
                }
            } else {
                mario_set_forward_vel(m, 0.0f);
            }
            break;

        case AIR_STEP_GRABBED_LEDGE:
            set_mario_animation(m, MARIO_ANIM_IDLE_ON_LEDGE);
            drop_and_set_mario_action(m, ACT_LEDGE_GRAB, 0);
            break;

        case AIR_STEP_GRABBED_CEILING:
            set_mario_action(m, ACT_START_HANGING, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;

        default:
            break;
    }
    return stepResult;
}

/* --- The jumps ---------------------------------------------------------- */

static s32 act_jump(struct MarioState *m) {
    if (check_kick_or_dive_in_air(m)) {
        return TRUE;
    }
    if (check_z_pressed_in_air(m)) {
        return TRUE;
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);
    common_air_action_step(m, ACT_JUMP_LAND, MARIO_ANIM_SINGLE_JUMP,
                           AIR_STEP_CHECK_LEDGE_GRAB | AIR_STEP_CHECK_HANG);
    return FALSE;
}

static s32 act_double_jump(struct MarioState *m) {
    s32 animation = (m->vel[1] >= 0.0f) ? MARIO_ANIM_DOUBLE_JUMP_RISE
                                        : MARIO_ANIM_DOUBLE_JUMP_FALL;

    if (check_kick_or_dive_in_air(m)) {
        return TRUE;
    }
    if (check_z_pressed_in_air(m)) {
        return TRUE;
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_HOOHOO);
    common_air_action_step(m, ACT_DOUBLE_JUMP_LAND, animation,
                           AIR_STEP_CHECK_LEDGE_GRAB | AIR_STEP_CHECK_HANG);
    return FALSE;
}

static s32 act_triple_jump(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    /* No ledge grab from a triple jump: the flip commits you to the landing. */
    common_air_action_step(m, ACT_TRIPLE_JUMP_LAND, MARIO_ANIM_TRIPLE_JUMP, 0);
    if (m->action == ACT_TRIPLE_JUMP_LAND) {
        queue_rumble_data(5, 40);
    }
    play_flip_sounds(m, 2, 8, 20);
    return FALSE;
}

static s32 act_backflip(struct MarioState *m) {
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);
    common_air_action_step(m, ACT_BACKFLIP_LAND, MARIO_ANIM_BACKFLIP, 0);
    if (m->action == ACT_BACKFLIP_LAND) {
        queue_rumble_data(5, 40);
    }
    play_flip_sounds(m, 2, 3, 17);
    return FALSE;
}

static s32 act_freefall(struct MarioState *m) {
    s32 animation;

    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    /* actionArg records how we got here, purely so the fall reads correctly. */
    switch (m->actionArg) {
        case 1:  animation = MARIO_ANIM_FALL_FROM_SLIDE; break;
        case 2:  animation = MARIO_ANIM_FALL_FROM_SLIDE_KICK; break;
        default: animation = MARIO_ANIM_GENERAL_FALL; break;
    }

    common_air_action_step(m, ACT_FREEFALL_LAND, animation, AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

static s32 act_side_flip(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);

    if (common_air_action_step(m, ACT_SIDE_FLIP_LAND, MARIO_ANIM_SLIDEFLIP,
                               AIR_STEP_CHECK_LEDGE_GRAB)
        != AIR_STEP_GRABBED_LEDGE) {
        /* The model faces backwards during a side flip; collision facing is
         * unchanged, only the displayed yaw is flipped. */
        m->marioObj->header.gfx.angle[1] += 0x8000;
    }

    if (m->marioObj->header.gfx.animInfo.animFrame == 6) {
        play_sound(SOUND_ACTION_SIDE_FLIP_UNK, m->marioObj->header.gfx.cameraToObject);
    }
    return FALSE;
}

static s32 act_long_jump(struct MarioState *m) {
    s32 animation = m->marioObj->oMarioLongJumpIsSlow ? MARIO_ANIM_SLOW_LONGJUMP
                                                      : MARIO_ANIM_FAST_LONGJUMP;

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);
    common_air_action_step(m, ACT_LONG_JUMP_LAND, animation, AIR_STEP_CHECK_LEDGE_GRAB);
    if (m->action == ACT_LONG_JUMP_LAND) {
        queue_rumble_data(5, 40);
    }
    return FALSE;
}

static s32 act_steep_jump(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);

    /* 2% speed loss per frame: a steep jump slowly gives up the speed the slope
     * gave it, rather than keeping it like a long jump. */
    mario_set_forward_vel(m, 0.98f * m->forwardVel);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                m->faceAngle[0] = 0;
                set_mario_action(m, m->forwardVel < 0.0f ? ACT_BEGIN_SLIDING
                                                        : ACT_JUMP_LAND,
                                 0);
            }
            break;
        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            break;
        default:
            break;
    }

    set_mario_animation(m, MARIO_ANIM_AIRBORNE_ON_STOMACH);
    m->marioObj->header.gfx.angle[2] = 0;
    return FALSE;
}

static s32 act_wall_kick_air(struct MarioState *m) {
    play_mario_jump_sound(m);
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    common_air_action_step(m, ACT_JUMP_LAND, MARIO_ANIM_SLIDEJUMP,
                           AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

/*
 * The wall kick window.
 *
 * actionTimer increments once per frame; A is only accepted while it is 1 or 2.
 * After that the outcome is forced: hard knockback above 38 units/frame, a soft
 * bonk below.  wallKickTimer is set on failure to suppress an immediate retry.
 */
static s32 act_air_hit_wall(struct MarioState *m) {
    if (++(m->actionTimer) <= 2) {
        if (m->input & INPUT_A_PRESSED) {
            m->vel[1] = 52.0f;
            m->faceAngle[1] += 0x8000;
            return set_mario_action(m, ACT_WALL_KICK_AIR, 0);
        }
    } else if (m->forwardVel >= 38.0f) {
        m->wallKickTimer = 5;
        if (m->vel[1] > 0.0f) {
            m->vel[1] = 0.0f;
        }
        m->particleFlags |= PARTICLE_VERTICAL_STAR;
        return set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
    } else {
        m->wallKickTimer = 5;
        if (m->vel[1] > 0.0f) {
            m->vel[1] = 0.0f;
        }
        if (m->forwardVel > 8.0f) {
            mario_set_forward_vel(m, -8.0f);
        }
        return set_mario_action(m, ACT_SOFT_BONK, 0);
    }

    set_mario_animation(m, MARIO_ANIM_START_WALLKICK);
    return FALSE;
}

/* --- Attacks ------------------------------------------------------------ */

static s32 act_dive(struct MarioState *m) {
    if (m->actionArg == 0) {
        play_mario_sound(m, SOUND_ACTION_THROW, SOUND_MARIO_HOOHOO);
    } else {
        play_mario_sound(m, SOUND_ACTION_THROW, 0);
    }

    set_mario_animation(m, MARIO_ANIM_DIVE);
    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            /* Pitch down as you fall, to a cap of -60 degrees. The dive rotates
             * to face its own trajectory rather than staying level. */
            if (m->vel[1] < 0.0f && m->faceAngle[0] > -0x2AAA) {
                m->faceAngle[0] -= 0x200;
                if (m->faceAngle[0] < -0x2AAA) {
                    m->faceAngle[0] = -0x2AAA;
                }
            }
            m->marioObj->header.gfx.angle[0] = -m->faceAngle[0];
            break;

        case AIR_STEP_LANDED:
            if (!check_fall_damage(m, ACT_HARD_FORWARD_GROUND_KB)) {
                /* A dive that reaches the ground becomes a slide, keeping its
                 * speed -- which is what makes dive-slide travel so far. */
                set_mario_action(m, ACT_DIVE_SLIDE, 0);
            }
            m->faceAngle[0] = 0;
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            m->faceAngle[0] = 0;
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            drop_and_set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;

        default:
            break;
    }
    return FALSE;
}

/*
 * Ground pound: a wind-up that *rises slightly*, then a fixed -50 plunge.
 *
 * The rise is 20 units on the first frame decreasing by 2 each frame, and it is
 * refused if it would put your head into a ceiling.  Forward speed is zeroed
 * throughout, which is why a ground pound cannot be used to cross a gap.
 */
static s32 act_ground_pound(struct MarioState *m) {
    s32 stepResult;
    f32 yOffset;

    play_sound_if_no_flag(m, SOUND_ACTION_THROW, MARIO_ACTION_SOUND_PLAYED);

    if (m->actionState == 0) {
        if (m->actionTimer < 10) {
            yOffset = 20.0f - 2.0f * m->actionTimer;
            if (m->pos[1] + yOffset + 160.0f < m->ceilHeight) {
                m->pos[1] += yOffset;
                m->peakHeight = m->pos[1];
                vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
            }
        }

        m->vel[1] = -50.0f;
        mario_set_forward_vel(m, 0.0f);

        set_mario_animation(m, m->actionArg == 0 ? MARIO_ANIM_START_GROUND_POUND
                                                : MARIO_ANIM_TRIPLE_JUMP_GROUND_POUND);
        if (m->actionTimer == 0) {
            play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
        }

        m->actionTimer++;
        if (m->actionTimer >= m->marioObj->header.gfx.animInfo.curAnim->loopEnd + 4) {
            play_sound(SOUND_MARIO_GROUND_POUND_WAH,
                       m->marioObj->header.gfx.cameraToObject);
            m->actionState = 1;
        }
    } else {
        set_mario_animation(m, MARIO_ANIM_GROUND_POUND);

        stepResult = perform_air_step(m, 0);
        if (stepResult == AIR_STEP_LANDED) {
            play_mario_heavy_landing_sound(m, SOUND_ACTION_TERRAIN_HEAVY_LANDING);
            m->particleFlags |= PARTICLE_MIST_CIRCLE | PARTICLE_HORIZONTAL_STAR;
            set_mario_action(m, ACT_GROUND_POUND_LAND, 0);
            set_camera_shake_from_hit(SHAKE_GROUND_POUND);
        } else if (stepResult == AIR_STEP_HIT_WALL) {
            mario_set_forward_vel(m, -16.0f);
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
        }
    }
    return FALSE;
}

static s32 act_slide_kick(struct MarioState *m) {
    if (m->actionState == 0 && m->actionTimer == 0) {
        play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_HOOHOO);
        set_mario_animation(m, MARIO_ANIM_SLIDE_KICK);
    }

    /* Airborne too long over a big drop: give up and just fall. */
    if (++(m->actionTimer) > 30 && m->pos[1] - m->floorHeight > 500.0f) {
        return set_mario_action(m, ACT_FREEFALL, 2);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == 0) {
                m->marioObj->header.gfx.angle[0] = atan2s(m->forwardVel, -m->vel[1]);
                if (m->marioObj->header.gfx.angle[0] > 0x1800) {
                    m->marioObj->header.gfx.angle[0] = 0x1800;
                }
            }
            break;

        case AIR_STEP_LANDED:
            /* First touch bounces at half speed; the second commits to sliding. */
            if (m->actionState == 0 && m->vel[1] < 0.0f) {
                m->vel[1] = -m->vel[1] / 2.0f;
                m->actionState = 1;
                m->actionTimer = 0;
            } else {
                set_mario_action(m, ACT_SLIDE_KICK_SLIDE, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        default:
            break;
    }
    return FALSE;
}

static s32 act_jump_kick(struct MarioState *m) {
    s32 animFrame;

    if (m->actionState == 0) {
        play_sound_if_no_flag(m, SOUND_MARIO_PUNCH_HOO, MARIO_ACTION_SOUND_PLAYED);
        m->marioObj->header.gfx.animInfo.animID = -1;
        set_mario_animation(m, MARIO_ANIM_AIR_KICK);
        m->actionState = 1;
    }

    animFrame = m->marioObj->header.gfx.animInfo.animFrame;
    if (animFrame >= 0 && animFrame < 8) {
        m->flags |= MARIO_KICKING;
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                set_mario_action(m, ACT_FREEFALL_LAND, 0);
            }
            break;
        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;
        default:
            break;
    }
    return FALSE;
}

/* --- Slides and recoveries ---------------------------------------------- */

static s32 act_butt_slide_air(struct MarioState *m) {
    if (++(m->actionTimer) > 30 && m->pos[1] - m->floorHeight > 500.0f) {
        return set_mario_action(m, ACT_FREEFALL, 1);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            /* Landing on near-flat ground bounces once; on a slope it commits
             * straight into the slide. */
            if (m->actionState == 0 && m->vel[1] < 0.0f && m->floor != NULL
                && m->floor->normal.y >= 0.9848077f) {
                m->vel[1] = -m->vel[1] / 2.0f;
                m->actionState = 1;
            } else {
                set_mario_action(m, ACT_BUTT_SLIDE, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;

        default:
            break;
    }

    set_mario_animation(m, MARIO_ANIM_SLIDE);
    return FALSE;
}

/* A rollout converts a slide or dive back into height: a fresh +30 upward. */
static s32 act_forward_rollout(struct MarioState *m) {
    if (m->actionState == 0) {
        m->vel[1] = 30.0f;
        m->actionState = 1;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == 1) {
                if (set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING) == 4) {
                    play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
                }
            }
            break;
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_FREEFALL_LAND_STOP, 0);
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;
        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;
        default:
            break;
    }

    if (m->actionState == 1 && is_anim_past_end(m)) {
        m->actionState = 2;
    }
    return FALSE;
}

static s32 act_backward_rollout(struct MarioState *m) {
    if (m->actionState == 0) {
        m->vel[1] = 30.0f;
        m->actionState = 1;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == 1) {
                set_mario_animation(m, MARIO_ANIM_BACKWARD_SPINNING);
            }
            break;
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_FREEFALL_LAND_STOP, 0);
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;
        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;
        default:
            break;
    }

    if (m->actionState == 1 && is_anim_past_end(m)) {
        m->actionState = 2;
    }
    return FALSE;
}

static s32 act_soft_bonk(struct MarioState *m) {
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    common_air_action_step(m, ACT_FREEFALL_LAND, MARIO_ANIM_GENERAL_FALL, 0);
    return FALSE;
}

/* Knockback: no air control at all, which is what makes it feel like a loss of
 * agency rather than a slower jump. */
static s32 common_air_knockback_step(struct MarioState *m, u32 landAction,
                                     u32 hardFallAction, s32 animation, f32 speed) {
    s32 stepResult;

    mario_set_forward_vel(m, speed);

    stepResult = perform_air_step(m, 0);
    switch (stepResult) {
        case AIR_STEP_NONE:
            set_mario_animation(m, animation);
            break;

        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, hardFallAction)) {
                set_mario_action(m, landAction, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            set_mario_animation(m, MARIO_ANIM_BACKWARD_AIR_KB);
            mario_bonk_reflection(m, TRUE);
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            mario_set_forward_vel(m, -speed);
            break;

        default:
            break;
    }
    return stepResult;
}

static s32 act_backward_air_kb(struct MarioState *m) {
    common_air_knockback_step(m, ACT_BACKWARD_GROUND_KB, ACT_HARD_BACKWARD_GROUND_KB,
                              MARIO_ANIM_BACKWARD_AIR_KB, -16.0f);
    return FALSE;
}

static s32 act_forward_air_kb(struct MarioState *m) {
    common_air_knockback_step(m, ACT_FORWARD_GROUND_KB, ACT_HARD_FORWARD_GROUND_KB,
                              MARIO_ANIM_AIR_FORWARD_KB, 16.0f);
    return FALSE;
}

static s32 act_twirling(struct MarioState *m) {
    s16 startTwirlYaw = m->twirlYaw;
    s16 yawVelTarget = (m->input & INPUT_A_DOWN) ? 0x2000 : 0x1800;

    m->angleVel[1] = (s16) approach_s32(m->angleVel[1], yawVelTarget, 0x200, 0x200);
    m->twirlYaw += m->angleVel[1];

    set_mario_animation(m, m->actionArg == 0 ? MARIO_ANIM_START_TWIRL : MARIO_ANIM_TWIRL);
    if (is_anim_past_end(m)) {
        m->actionArg = 1;
    }

    if (startTwirlYaw > m->twirlYaw) {
        play_sound(SOUND_ACTION_TWIRL, m->marioObj->header.gfx.cameraToObject);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_TWIRL_LAND, 0);
            break;
        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;
        default:
            break;
    }

    m->marioObj->header.gfx.angle[1] += m->twirlYaw;
    return FALSE;
}

/* --- Group dispatch ----------------------------------------------------- */

static s32 check_common_airborne_cancels(struct MarioState *m) {
    if (m->waterLevel > FLOOR_LOWER_LIMIT_MISC && m->pos[1] < (f32) (m->waterLevel - 100)) {
        return set_water_plunge_action(m);
    }
    if (m->input & INPUT_SQUISHED) {
        return drop_and_set_mario_action(m, ACT_SQUISHED, 0);
    }
    if (m->floor != NULL && m->floor->type == SURFACE_VERTICAL_WIND
        && (m->action & ACT_FLAG_ALLOW_VERTICAL_WIND_ACTION)) {
        return drop_and_set_mario_action(m, ACT_VERTICAL_WIND, 0);
    }
    m->quicksandDepth = 0.0f;
    return FALSE;
}

s32 mario_execute_airborne_action(struct MarioState *m) {
    s32 cancel = FALSE;

    if (check_common_airborne_cancels(m)) {
        return TRUE;
    }

    play_far_fall_sound(m);

    switch (m->action) {
        case ACT_JUMP:              cancel = act_jump(m); break;
        case ACT_DOUBLE_JUMP:       cancel = act_double_jump(m); break;
        case ACT_TRIPLE_JUMP:       cancel = act_triple_jump(m); break;
        case ACT_BACKFLIP:          cancel = act_backflip(m); break;
        case ACT_SIDE_FLIP:         cancel = act_side_flip(m); break;
        case ACT_FREEFALL:          cancel = act_freefall(m); break;
        case ACT_HOLD_JUMP:         cancel = act_jump(m); break;
        case ACT_HOLD_FREEFALL:     cancel = act_freefall(m); break;
        case ACT_STEEP_JUMP:        cancel = act_steep_jump(m); break;
        case ACT_WALL_KICK_AIR:     cancel = act_wall_kick_air(m); break;
        case ACT_AIR_HIT_WALL:      cancel = act_air_hit_wall(m); break;
        case ACT_LONG_JUMP:         cancel = act_long_jump(m); break;
        case ACT_DIVE:              cancel = act_dive(m); break;
        case ACT_GROUND_POUND:      cancel = act_ground_pound(m); break;
        case ACT_SLIDE_KICK:        cancel = act_slide_kick(m); break;
        case ACT_JUMP_KICK:         cancel = act_jump_kick(m); break;
        case ACT_BUTT_SLIDE_AIR:    cancel = act_butt_slide_air(m); break;
        case ACT_HOLD_BUTT_SLIDE_AIR: cancel = act_butt_slide_air(m); break;
        case ACT_FORWARD_ROLLOUT:   cancel = act_forward_rollout(m); break;
        case ACT_BACKWARD_ROLLOUT:  cancel = act_backward_rollout(m); break;
        case ACT_SOFT_BONK:         cancel = act_soft_bonk(m); break;
        case ACT_BACKWARD_AIR_KB:   cancel = act_backward_air_kb(m); break;
        case ACT_FORWARD_AIR_KB:    cancel = act_forward_air_kb(m); break;
        case ACT_TWIRLING:          cancel = act_twirling(m); break;
        default:
            /* Unimplemented airborne action: fall, which is always safe. */
            cancel = set_mario_action(m, ACT_FREEFALL, 0);
            break;
    }
    return cancel;
}
