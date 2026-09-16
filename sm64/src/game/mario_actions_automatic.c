/*
 * new64 -- automatic actions: ledge grabbing and climbing, ceiling hanging.
 *
 * "Automatic" means the engine took over: these states are entered by the step
 * code reporting something (AIR_STEP_GRABBED_LEDGE, AIR_STEP_GRABBED_CEILING)
 * rather than by the player asking for them.
 *
 * The ledge grab is the interesting one.  It is offered only when an air step
 * finds a wall at hip height but *not* at head height, while descending, moving
 * into the wall -- see check_ledge_grab() in mario_step.c.  That combination is
 * why you catch a ledge when you just barely miss a jump, but pass cleanly by a
 * wall you are merely brushing.
 */
#include "mario_actions_automatic.h"

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
 * Release the ledge: shove 60 units back out from the wall and drop.
 *
 * The backward shove is essential -- without it you would still be inside the
 * wall's collision radius next frame and would immediately re-grab.
 */
static s32 let_go_of_ledge(struct MarioState *m) {
    f32 floorHeight;
    struct Surface *floor;

    m->vel[1] = 0.0f;
    m->forwardVel = -8.0f;
    m->pos[0] -= 60.0f * sins(m->faceAngle[1]);
    m->pos[2] -= 60.0f * coss(m->faceAngle[1]);

    floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &floor);
    if (floorHeight < m->pos[1] - 100.0f) {
        m->pos[1] -= 100.0f;
    } else {
        m->pos[1] = floorHeight;
    }
    return set_mario_action(m, ACT_SOFT_BONK, 0);
}

/* Step up onto the ledge, nudging forward so you end up on the surface rather
 * than balanced on its lip. */
static void climb_up_ledge(struct MarioState *m) {
    set_mario_animation(m, MARIO_ANIM_IDLE_HEAD_LEFT);
    m->pos[0] += 14.0f * sins(m->faceAngle[1]);
    m->pos[2] += 14.0f * coss(m->faceAngle[1]);
    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
}

static void update_ledge_climb_camera(struct MarioState *m) {
    (void) m;
}

static s32 act_ledge_grab(struct MarioState *m) {
    f32 heightAboveFloor;
    s16 intendedDYaw = (s16) (m->intendedYaw - m->faceAngle[1]);
    s32 hasSpaceForMario = (m->ceilHeight - m->floorHeight >= 160.0f);

    if (m->actionTimer < 10) {
        m->actionTimer++;
    }

    /* The lip stopped being a lip (geometry moved, or we mis-grabbed). */
    if (m->floor != NULL && m->floor->normal.y < 0.9063078f) {
        return let_go_of_ledge(m);
    }
    if (m->input & (INPUT_Z_PRESSED | INPUT_OFF_FLOOR)) {
        return let_go_of_ledge(m);
    }
    if ((m->input & INPUT_A_PRESSED) && hasSpaceForMario) {
        return set_mario_action(m, ACT_LEDGE_CLIMB_FAST, 0);
    }

    /* Pulling back off the ledge drops you. Pushing forward climbs, but only
     * after a short settle so that the grab reads before the climb starts. */
    if (m->actionTimer == 10 && (m->input & INPUT_NONZERO_ANALOG)) {
        if (intendedDYaw >= -0x4000 && intendedDYaw <= 0x4000) {
            if (hasSpaceForMario) {
                return set_mario_action(m, ACT_LEDGE_CLIMB_SLOW_1, 0);
            }
        } else {
            return let_go_of_ledge(m);
        }
    }

    /* Already almost level with the ledge: just stand up. */
    heightAboveFloor = m->pos[1] - find_floor_height_relative_polar(m, -0x8000, 30.0f);
    if (hasSpaceForMario && heightAboveFloor < 100.0f) {
        return set_mario_action(m, ACT_LEDGE_CLIMB_FAST, 0);
    }

    if (m->actionArg == 0) {
        play_sound_if_no_flag(m, SOUND_MARIO_WHOA, MARIO_MARIO_SOUND_PLAYED);
    }

    stop_and_set_height_to_floor(m);
    set_mario_animation(m, MARIO_ANIM_IDLE_ON_LEDGE);
    return FALSE;
}

static s32 act_ledge_climb_slow(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return let_go_of_ledge(m);
    }
    if (m->actionTimer >= 28 && (m->input & (INPUT_NONZERO_ANALOG | INPUT_A_PRESSED
                                             | INPUT_B_PRESSED | INPUT_Z_PRESSED))) {
        climb_up_ledge(m);
        return check_common_action_exits(m);
    }

    if (m->actionTimer == 10) {
        play_sound_if_no_flag(m, SOUND_MARIO_EEUH, MARIO_MARIO_SOUND_PLAYED);
    }

    set_mario_animation(m, MARIO_ANIM_SLOW_LEDGE_GRAB);
    update_ledge_climb_camera(m);
    stop_and_set_height_to_floor(m);

    if (++m->actionTimer >= 34) {
        climb_up_ledge(m);
        set_mario_action(m, ACT_IDLE, 0);
    }
    return FALSE;
}

static s32 act_ledge_climb_fast(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return let_go_of_ledge(m);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_STEP, 0);
    set_mario_animation(m, MARIO_ANIM_FAST_LEDGE_GRAB);
    stop_and_set_height_to_floor(m);

    if (m->marioObj->header.gfx.animInfo.animFrame == 8) {
        play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
    }
    if (++m->actionTimer >= 16 || is_anim_past_end(m)) {
        climb_up_ledge(m);
        set_mario_action(m, ACT_IDLE, 0);
    }
    return FALSE;
}

static s32 act_ledge_climb_down(struct MarioState *m) {
    if (m->input & INPUT_OFF_FLOOR) {
        return let_go_of_ledge(m);
    }
    play_sound_if_no_flag(m, SOUND_MARIO_WHOA, MARIO_MARIO_SOUND_PLAYED);
    set_mario_animation(m, MARIO_ANIM_CLIMB_DOWN_LEDGE);
    stop_and_set_height_to_floor(m);

    if (is_anim_at_end(m)) {
        set_mario_action(m, ACT_LEDGE_GRAB, 1);
        m->actionArg = 1;
    }
    return FALSE;
}

/* --- Ceiling hanging ---------------------------------------------------- */

static s32 act_start_hanging(struct MarioState *m) {
    if (m->actionTimer++ > 0 && (m->input & INPUT_A_PRESSED)) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (!(m->input & INPUT_A_DOWN)) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    if (m->ceil == NULL || m->ceil->type != SURFACE_HANGABLE) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }

    set_mario_animation(m, MARIO_ANIM_HANG_ON_CEILING);
    m->vel[1] = 0.0f;
    m->pos[1] = m->ceilHeight - 160.0f;
    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);

    if (m->actionTimer >= 5) {
        set_mario_action(m, ACT_HANGING, 0);
    }
    return FALSE;
}

static s32 act_hanging(struct MarioState *m) {
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_HANG_MOVING, m->actionArg);
    }
    if (!(m->input & INPUT_A_DOWN)) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    if (m->ceil == NULL || m->ceil->type != SURFACE_HANGABLE) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }

    set_mario_animation(m, MARIO_ANIM_HANDSTAND_IDLE);
    m->vel[1] = 0.0f;
    m->pos[1] = m->ceilHeight - 160.0f;
    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    return FALSE;
}

static s32 act_hang_moving(struct MarioState *m) {
    Vec3f nextPos;

    if (!(m->input & INPUT_A_DOWN)) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }
    if (!(m->input & INPUT_NONZERO_ANALOG)) {
        return set_mario_action(m, ACT_HANGING, m->actionArg);
    }

    /* Hand-over-hand movement is slow and steers fully, unlike air control. */
    m->faceAngle[1] = m->intendedYaw;
    m->forwardVel = 12.0f;
    m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
    m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
    m->vel[0] = m->slideVelX;
    m->vel[2] = m->slideVelZ;

    nextPos[0] = m->pos[0] + m->vel[0] / 4.0f;
    nextPos[1] = m->pos[1];
    nextPos[2] = m->pos[2] + m->vel[2] / 4.0f;

    switch (perform_hanging_step(m, nextPos)) {
        case HANG_NONE:
            set_mario_animation(m, MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT);
            break;
        case HANG_LEFT_CEIL:
        case HANG_HIT_CEIL_OR_OOB:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        default:
            break;
    }
    return FALSE;
}

s32 mario_execute_automatic_action(struct MarioState *m) {
    s32 cancel = FALSE;

    if (m->waterLevel > FLOOR_LOWER_LIMIT_MISC && m->pos[1] < (f32) (m->waterLevel - 100)) {
        return set_water_plunge_action(m);
    }

    switch (m->action) {
        case ACT_LEDGE_GRAB:        cancel = act_ledge_grab(m); break;
        case ACT_LEDGE_CLIMB_SLOW_1: cancel = act_ledge_climb_slow(m); break;
        case ACT_LEDGE_CLIMB_SLOW_2: cancel = act_ledge_climb_slow(m); break;
        case ACT_LEDGE_CLIMB_FAST:  cancel = act_ledge_climb_fast(m); break;
        case ACT_LEDGE_CLIMB_DOWN:  cancel = act_ledge_climb_down(m); break;
        case ACT_START_HANGING:     cancel = act_start_hanging(m); break;
        case ACT_HANGING:           cancel = act_hanging(m); break;
        case ACT_HANG_MOVING:       cancel = act_hang_moving(m); break;
        default:
            cancel = set_mario_action(m, ACT_FREEFALL, 0);
            break;
    }
    return cancel;
}
