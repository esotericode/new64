/*
 * new64 -- the movement substrate: turning a desired velocity into a position.
 *
 * ============================ WHY QUARTER STEPS ============================
 *
 * Motion is applied in four sub-steps per frame, each doing a full collision
 * query.  This is not an optimisation or a modern "substepping for accuracy"
 * scheme -- it is observable behaviour.  Because each quarter step re-queries
 * the floor and can bail out early, a frame that would tunnel through a thin
 * platform at full velocity instead lands on it, and a frame that clips a wall
 * corner resolves differently depending on which quarter step touched it.  Any
 * implementation that moves the full velocity in one go, or that substeps a
 * different number of times, diverges from the original within a few frames of
 * fast movement.
 *
 * ========================== WHAT ACTIONS MAY DO ===========================
 *
 * Action code sets velocity and calls a step function.  It must not write
 * m->pos.  The step functions own position, the floor/ceil/wall pointers, and
 * the graphics-node copy of the transform; keeping that ownership in one place
 * is what stops collision state and render state from drifting apart.
 *
 * Probe heights and radii below (30/24, 60/50, 150/50) are the collision
 * "body": a lower hip check and an upper chest check.  They are why you can
 * walk up to a waist-high ledge but not a chest-high one, and why an airborne
 * wall hit registers higher up than a grounded one.
 */
#include "mario_step.h"

#include "audio/external.h"
#include "audio_defines.h"
#include "camera.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "sm64.h"
#include "surface_terrains.h"
#include "types.h"

#include <stddef.h>

/* Half the body height. A ceiling closer than this above the feet is a
 * no-go: it is what makes low gaps impassable. */
#define MARIO_BODY_HEIGHT 160.0f

/*
 * Push `pos` out of every wall within `radius` at `offset` above pos[1], and
 * report the wall that mattered.
 *
 * The *last* wall in the query's list is returned, not the nearest or the most
 * opposed.  In a corner this means the reported wall is whichever the partition
 * happened to visit last, which is load order -- and action code reacts to that
 * one (wall kicks take its normal).  Returning the "best" wall here would be an
 * improvement that changes corner behaviour, so it is left alone.
 */
struct Surface *resolve_and_return_wall_collisions(Vec3f pos, f32 offset, f32 radius) {
    struct WallCollisionData collisionData;
    struct Surface *wall = NULL;

    collisionData.x = pos[0];
    collisionData.y = pos[1];
    collisionData.z = pos[2];
    collisionData.radius = radius;
    collisionData.offsetY = offset;

    if (find_wall_collisions(&collisionData)) {
        pos[0] = collisionData.x;
        pos[1] = collisionData.y;
        pos[2] = collisionData.z;
        wall = collisionData.walls[collisionData.numWalls - 1];
    }
    return wall;
}

/*
 * Ceiling above `pos`, but queried from `height` rather than from pos[1].
 * Anchoring the query to the floor keeps the answer stable while the body moves
 * within a room, instead of a ceiling flickering in and out of range.
 */
f32 vec3f_find_ceil(Vec3f pos, f32 height, struct Surface **ceil) {
    return find_ceil(pos[0], height + 80.0f, pos[2], ceil);
}

void stop_and_set_height_to_floor(struct MarioState *m) {
    struct Object *marioObj = m->marioObj;

    mario_set_forward_vel(m, 0.0f);
    m->vel[1] = 0.0f;

    /* Snap the display transform to the collision position so that a stopped
     * player is never rendered a frame behind where they actually are. */
    m->pos[1] = m->floorHeight;
    vec3f_copy(marioObj->header.gfx.pos, m->pos);
    vec3s_set(marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);
}

void set_vel_from_pitch_and_yaw(struct MarioState *m) {
    m->vel[0] = m->forwardVel * coss(m->faceAngle[0]) * sins(m->faceAngle[1]);
    m->vel[1] = m->forwardVel * sins(m->faceAngle[0]);
    m->vel[2] = m->forwardVel * coss(m->faceAngle[0]) * coss(m->faceAngle[1]);
}

void set_vel_from_yaw(struct MarioState *m) {
    m->vel[0] = m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
    m->vel[2] = m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
}

f32 get_additive_y_vel_for_jumps(void) {
    return 0.0f;
}

/*
 * Reflect facing off the wall just hit.
 *
 * With negateSpeed the reflection is a true mirror about the wall normal and
 * speed is reversed; without it, facing is flipped 180 degrees instead.  The
 * first is a bonk that sends you back along your approach, the second is used
 * where the action wants to keep moving but face away.
 */
void mario_bonk_reflection(struct MarioState *m, u32 negateSpeed) {
    if (m->wall != NULL) {
        s16 wallAngle = atan2s(m->wall->normal.z, m->wall->normal.x);
        m->faceAngle[1] = wallAngle - (m->faceAngle[1] - wallAngle);
        play_sound((m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_BONK
                                                : SOUND_ACTION_BONK,
                   m->marioObj->header.gfx.cameraToObject);
    } else {
        play_sound(SOUND_ACTION_HIT, m->marioObj->header.gfx.cameraToObject);
    }

    if (negateSpeed) {
        mario_set_forward_vel(m, -m->forwardVel);
    } else {
        m->faceAngle[1] += 0x8000;
    }
}

/*
 * Eject off a floor too steep to stand on, pointing downhill if already facing
 * that way and uphill otherwise, at a fixed 16 units/frame.  The fixed speed is
 * why sliding off a steep slope always starts the same way regardless of how
 * fast you arrived.
 */
u32 mario_push_off_steep_floor(struct MarioState *m, u32 action, u32 actionArg) {
    s16 floorDYaw = m->floorAngle - m->faceAngle[1];

    if (floorDYaw > -0x4000 && floorDYaw < 0x4000) {
        m->forwardVel = 16.0f;
        m->faceAngle[1] = m->floorAngle;
    } else {
        m->forwardVel = -16.0f;
        m->faceAngle[1] = m->floorAngle + 0x8000;
    }
    return set_mario_action(m, action, actionArg);
}

/*
 * Gravity.
 *
 * Baseline is -4.0 per frame with terminal velocity -75.0.  The interesting
 * clause is the jump-ascent one: releasing the jump button while still rising
 * faster than 20 divides upward velocity by *four*, immediately.  That single
 * line is the entire variable-jump-height mechanic -- there is no gradual
 * "reduced upward thrust" model.  It only applies to actions carrying
 * ACT_FLAG_CONTROL_JUMP_HEIGHT, which is why a backflip or long jump always
 * reaches full height no matter how briefly the button was held.
 */
static u32 should_strengthen_gravity_for_jump_ascent(struct MarioState *m) {
    if (!(m->flags & MARIO_UNKNOWN_08)) {
        return FALSE;
    }
    if (m->action & (ACT_FLAG_INTANGIBLE | ACT_FLAG_INVULNERABLE)) {
        return FALSE;
    }
    if (!(m->input & INPUT_A_DOWN) && m->vel[1] > 20.0f) {
        return (m->action & ACT_FLAG_CONTROL_JUMP_HEIGHT) != 0;
    }
    return FALSE;
}

void apply_gravity(struct MarioState *m) {
    if (m->action == ACT_TWIRLING && m->vel[1] < 0.0f) {
        /* Twirling falls slowly, and slower still while actively spinning. */
        f32 terminalVelocity;
        f32 heaviness = 1.0f;

        if (m->angleVel[1] > 1024) {
            heaviness = 1024.0f / m->angleVel[1];
        }
        terminalVelocity = -75.0f * heaviness;
        m->vel[1] -= 4.0f * heaviness;
        if (m->vel[1] < terminalVelocity) {
            m->vel[1] = terminalVelocity;
        }
    } else if (m->action == ACT_SHOT_FROM_CANNON) {
        m->vel[1] -= 1.0f;
        if (m->vel[1] < -75.0f) {
            m->vel[1] = -75.0f;
        }
    } else if (m->action == ACT_LONG_JUMP || m->action == ACT_SLIDE_KICK) {
        /* Half gravity: this is what makes a long jump travel so far, as much
         * as its higher forward speed does. */
        m->vel[1] -= 2.0f;
        if (m->vel[1] < -75.0f) {
            m->vel[1] = -75.0f;
        }
    } else if (m->action == ACT_LAVA_BOOST || m->action == ACT_FALL_AFTER_STAR_GRAB) {
        m->vel[1] -= 3.2f;
        if (m->vel[1] < -65.0f) {
            m->vel[1] = -65.0f;
        }
    } else if (m->action == ACT_GETTING_BLOWN) {
        m->vel[1] -= m->unkC4;
        if (m->vel[1] < -75.0f) {
            m->vel[1] = -75.0f;
        }
    } else if (should_strengthen_gravity_for_jump_ascent(m)) {
        m->vel[1] /= 4.0f;
    } else if (m->action & ACT_FLAG_METAL_WATER) {
        m->vel[1] -= 1.6f;
        if (m->vel[1] < -16.0f) {
            m->vel[1] = -16.0f;
        }
    } else if ((m->flags & MARIO_WING_CAP_ON_HEAD) == MARIO_WING_CAP_ON_HEAD
               && m->vel[1] < 0.0f
               && (m->input & INPUT_A_DOWN)) {
        /* Wing cap flutter: half gravity and a gentler floor while falling. */
        m->marioBodyState->wingFlutter = TRUE;
        m->vel[1] -= 2.0f;
        if (m->vel[1] < -37.5f) {
            if ((m->vel[1] += 4.0f) > -37.5f) {
                m->vel[1] = -37.5f;
            }
        }
    } else {
        m->vel[1] -= 4.0f;
        if (m->vel[1] < -75.0f) {
            m->vel[1] = -75.0f;
        }
    }
}

/* Updraft volumes. The demo has none, but the surface type is honoured so a
 * level can add one without touching movement code. */
static void apply_vertical_wind(struct MarioState *m) {
    f32 maxVelY;
    f32 offsetY;

    if (m->action != ACT_GROUND_POUND) {
        offsetY = m->pos[1] - -1500.0f;

        if (m->floor != NULL && m->floor->type == SURFACE_VERTICAL_WIND
            && -3000.0f < offsetY && offsetY < 2000.0f) {
            if (offsetY >= 0.0f) {
                maxVelY = 10000.0f / (offsetY + 200.0f);
            } else {
                maxVelY = 50.0f;
            }

            if (m->vel[1] < maxVelY) {
                if ((m->vel[1] += maxVelY / 8.0f) > maxVelY) {
                    m->vel[1] = maxVelY;
                }
            }
            play_sound(SOUND_ENV_WIND2, m->marioObj->header.gfx.cameraToObject);
        }
    }
}

/* --- Quicksand / moving ground ------------------------------------------ */

u32 mario_update_quicksand(struct MarioState *m, f32 sinkingSpeed) {
    if (m->action & ACT_FLAG_RIDING_SHELL) {
        m->quicksandDepth = 0.0f;
    } else {
        if (m->quicksandDepth < 1.1f) {
            m->quicksandDepth = 1.1f;
        }

        switch (m->floor != NULL ? m->floor->type : SURFACE_DEFAULT) {
            case SURFACE_SHALLOW_QUICKSAND:
                if ((m->quicksandDepth += sinkingSpeed) >= 10.0f) {
                    m->quicksandDepth = 10.0f;
                }
                break;

            case SURFACE_DEEP_QUICKSAND:
                if ((m->quicksandDepth += sinkingSpeed) >= 160.0f) {
                    m->quicksandDepth = 160.0f;
                }
                break;

            case SURFACE_INSTANT_QUICKSAND:
                /* No escape: this is a death plane wearing a texture. */
                update_mario_sound_and_camera(m);
                drop_and_set_mario_action(m, ACT_DISAPPEARED, 0);
                return TRUE;

            default:
                m->quicksandDepth = 0.0f;
                break;
        }
    }
    return FALSE;
}

/* Conveyor-style ground force. No such surfaces in the demo level. */
u32 mario_update_moving_sand(struct MarioState *m) {
    (void) m;
    return FALSE;
}

u32 mario_update_windy_ground(struct MarioState *m) {
    (void) m;
    return FALSE;
}

/* --- Ground stepping ---------------------------------------------------- */

static u32 perform_ground_quarter_step(struct MarioState *m, Vec3f nextPos) {
    struct Surface *lowerWall;
    struct Surface *upperWall;
    struct Surface *ceil;
    struct Surface *floor;
    f32 ceilHeight;
    f32 floorHeight;
    s16 wallDYaw;

    /*
     * Both probes run, and both displace nextPos.  The lower one's *result* is
     * then discarded -- only the upper wall is reported to the caller.  The
     * displacement it performed still stands, so a low wall pushes you out
     * without ever being reported as "the wall you hit".  Dropping the lower
     * probe entirely would change where you end up; reporting it would change
     * what actions do about it.  Both halves of that are deliberate.
     */
    lowerWall = resolve_and_return_wall_collisions(nextPos, 30.0f, 24.0f);
    upperWall = resolve_and_return_wall_collisions(nextPos, 60.0f, 50.0f);
    (void) lowerWall;

    floorHeight = find_floor(nextPos[0], nextPos[1], nextPos[2], &floor);
    ceilHeight = vec3f_find_ceil(nextPos, floorHeight, &ceil);

    m->wall = NULL;

    /* Nothing to stand on at the destination: refuse the move outright and
     * abandon the remaining quarter steps. */
    if (floor == NULL) {
        return GROUND_STEP_HIT_WALL_STOP_QSTEP;
    }

    /* Destination floor is far below: we have walked off an edge. */
    if (nextPos[1] > floorHeight + 100.0f) {
        if (nextPos[1] + MARIO_BODY_HEIGHT >= ceilHeight) {
            return GROUND_STEP_HIT_WALL_STOP_QSTEP;
        }
        vec3f_copy(m->pos, nextPos);
        m->floor = floor;
        m->floorHeight = floorHeight;
        return GROUND_STEP_LEFT_GROUND;
    }

    /* Not enough headroom at the destination to stand there. */
    if (floorHeight + MARIO_BODY_HEIGHT >= ceilHeight) {
        return GROUND_STEP_HIT_WALL_STOP_QSTEP;
    }

    /* Accept the move, snapping to the floor. Walking is always floor-locked;
     * that is the difference between a ground step and an air step. */
    vec3f_set(m->pos, nextPos[0], floorHeight, nextPos[2]);
    m->floor = floor;
    m->floorHeight = floorHeight;

    if (upperWall != NULL) {
        wallDYaw = atan2s(upperWall->normal.z, upperWall->normal.x) - m->faceAngle[1];

        /*
         * A wall between 60 and 120 degrees off our facing is a glancing
         * contact: we slide along it without the action ever learning there was
         * a wall.  This is why you can run along a wall at an angle smoothly
         * instead of stuttering to a stop.
         */
        if (wallDYaw >= 0x2AAA && wallDYaw <= 0x5555) {
            return GROUND_STEP_NONE;
        }
        if (wallDYaw <= -0x2AAA && wallDYaw >= -0x5555) {
            return GROUND_STEP_NONE;
        }

        m->wall = upperWall;
        return GROUND_STEP_HIT_WALL_CONTINUE_QSTEP;
    }

    return GROUND_STEP_NONE;
}

s32 perform_ground_step(struct MarioState *m) {
    s32 i;
    u32 stepResult = GROUND_STEP_NONE;
    Vec3f intendedPos;

    for (i = 0; i < 4; i++) {
        /*
         * Horizontal speed is scaled by the floor's normal.y.  On a slope this
         * makes you cover less ground per frame than on the flat for the same
         * forwardVel -- the speed is along the *slope*, and this projects it.
         * It also means a steeper floor slows you even while running uphill
         * under full control.
         */
        intendedPos[0] = m->pos[0] + m->floor->normal.y * (m->vel[0] / 4.0f);
        intendedPos[2] = m->pos[2] + m->floor->normal.y * (m->vel[2] / 4.0f);
        intendedPos[1] = m->pos[1];

        stepResult = perform_ground_quarter_step(m, intendedPos);
        if (stepResult == GROUND_STEP_LEFT_GROUND
            || stepResult == GROUND_STEP_HIT_WALL_STOP_QSTEP) {
            break;
        }
    }

    m->terrainSoundAddend = mario_get_terrain_sound_addend(m);
    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    vec3s_set(m->marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);

    /* Collapse the internal "keep stepping" code before handing it to actions:
     * they only need to know that a wall was hit. */
    if (stepResult == GROUND_STEP_HIT_WALL_CONTINUE_QSTEP) {
        stepResult = GROUND_STEP_HIT_WALL;
    }
    return stepResult;
}

s32 stationary_ground_step(struct MarioState *m) {
    u32 takeStep;
    struct Object *marioObj = m->marioObj;
    u32 stepResult = GROUND_STEP_NONE;

    m->slideVelX = 0.0f;
    m->slideVelZ = 0.0f;
    m->vel[0] = 0.0f;
    m->vel[2] = 0.0f;

    /* Standing still still means stepping if the ground itself is moving. */
    takeStep = mario_update_moving_sand(m);
    takeStep |= mario_update_windy_ground(m);

    if (takeStep) {
        stepResult = perform_ground_step(m);
    } else {
        m->pos[1] = m->floorHeight;
        vec3f_copy(marioObj->header.gfx.pos, m->pos);
        vec3s_set(marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);
    }
    return stepResult;
}

/* --- Air stepping ------------------------------------------------------- */

/*
 * Decide whether a descending brush against a wall is a ledge grab.
 *
 * Three conditions, each ruling out a false positive:
 *   - must be falling, so you cannot grab on the way up;
 *   - the wall's pushout must *oppose* horizontal velocity, so you cannot grab
 *     a wall you are moving away from;
 *   - the floor 60 units past the wall must be more than 100 units above where
 *     you are, so a low kerb is not a ledge.
 */
static s32 check_ledge_grab(struct MarioState *m, struct Surface *wall,
                            Vec3f intendedPos, Vec3f nextPos) {
    struct Surface *ledgeFloor;
    Vec3f ledgePos;
    f32 displacementX = nextPos[0] - intendedPos[0];
    f32 displacementZ = nextPos[2] - intendedPos[2];

    if (m->vel[1] > 0.0f) {
        return FALSE;
    }
    if (displacementX * m->vel[0] + displacementZ * m->vel[2] > 0.0f) {
        return FALSE;
    }

    /* Probe the ground on the far side of the wall, starting the query high so
     * the lip itself is found rather than the ground you are falling past. */
    ledgePos[0] = nextPos[0] - wall->normal.x * 60.0f;
    ledgePos[2] = nextPos[2] - wall->normal.z * 60.0f;
    ledgePos[1] = find_floor(ledgePos[0], nextPos[1] + 160.0f, ledgePos[2], &ledgeFloor);

    if (ledgeFloor == NULL || ledgePos[1] - nextPos[1] <= 100.0f) {
        return FALSE;
    }

    vec3f_copy(m->pos, ledgePos);
    m->floor = ledgeFloor;
    m->floorHeight = ledgePos[1];
    m->faceAngle[0] = 0;
    /* Face into the wall: the grab orients you regardless of approach angle. */
    m->faceAngle[1] = atan2s(wall->normal.z, wall->normal.x) + 0x8000;
    return TRUE;
}

static s32 perform_air_quarter_step(struct MarioState *m, Vec3f intendedPos, u32 stepArg) {
    s16 wallDYaw;
    Vec3f nextPos;
    struct Surface *upperWall;
    struct Surface *lowerWall;
    struct Surface *ceil;
    struct Surface *floor;
    f32 ceilHeight;
    f32 floorHeight;

    vec3f_copy(nextPos, intendedPos);

    /*
     * Airborne wall probes sit higher and wider than grounded ones (150/50 and
     * 30/50 versus 60/50 and 30/24).  Both results are kept here, because the
     * *combination* is what distinguishes a ledge grab from a wall hit: a wall
     * at hip height but not at head height means a lip you can catch.
     */
    upperWall = resolve_and_return_wall_collisions(nextPos, 150.0f, 50.0f);
    lowerWall = resolve_and_return_wall_collisions(nextPos, 30.0f, 50.0f);

    floorHeight = find_floor(nextPos[0], nextPos[1], nextPos[2], &floor);
    ceilHeight = vec3f_find_ceil(nextPos, floorHeight, &ceil);

    m->wall = NULL;

    /* Off the edge of the world: keep the vertical motion, refuse the rest. */
    if (floor == NULL) {
        if (nextPos[1] <= m->floorHeight) {
            m->pos[1] = m->floorHeight;
            return AIR_STEP_LANDED;
        }
        m->pos[1] = nextPos[1];
        return AIR_STEP_HIT_WALL;
    }

    /* Descended to or below the floor: landed. */
    if (nextPos[1] <= floorHeight) {
        /*
         * Horizontal movement is only accepted if the landing spot has
         * headroom.  Landing in a gap shorter than the body keeps you where you
         * were horizontally *and leaves m->floor stale*, which is a real
         * behavioural wart rather than an oversight to tidy up: actions that
         * read m->floor immediately after such a landing see the old surface.
         */
        if (ceilHeight - floorHeight > MARIO_BODY_HEIGHT) {
            m->pos[0] = nextPos[0];
            m->pos[2] = nextPos[2];
            m->floor = floor;
            m->floorHeight = floorHeight;
        }
        m->pos[1] = floorHeight;
        return AIR_STEP_LANDED;
    }

    /* Head would go into a ceiling. */
    if (nextPos[1] + MARIO_BODY_HEIGHT > ceilHeight) {
        if (m->vel[1] >= 0.0f) {
            /* Rising into a ceiling kills upward velocity dead -- no bounce. */
            m->vel[1] = 0.0f;

            /*
             * Note this tests m->ceil (the ceiling from the *previous* frame),
             * not the freshly found `ceil`.  Hanging therefore engages based on
             * slightly stale information, which is exactly how the original
             * behaves and is observable when jumping along a hangable ceiling's
             * edge.
             */
            if ((stepArg & AIR_STEP_CHECK_HANG) && m->ceil != NULL
                && m->ceil->type == SURFACE_HANGABLE) {
                return AIR_STEP_GRABBED_CEILING;
            }
            return AIR_STEP_NONE;
        }

        /* Falling with a ceiling overhead: still check for landing first. */
        if (nextPos[1] <= m->floorHeight) {
            m->pos[1] = m->floorHeight;
            return AIR_STEP_LANDED;
        }
        m->pos[1] = nextPos[1];
        return AIR_STEP_HIT_WALL;
    }

    /* A wall at hip height but clear overhead: candidate ledge grab. */
    if ((stepArg & AIR_STEP_CHECK_LEDGE_GRAB) && upperWall == NULL && lowerWall != NULL) {
        if (check_ledge_grab(m, lowerWall, intendedPos, nextPos)) {
            return AIR_STEP_GRABBED_LEDGE;
        }
        vec3f_copy(m->pos, nextPos);
        m->floor = floor;
        m->floorHeight = floorHeight;
        return AIR_STEP_NONE;
    }

    vec3f_copy(m->pos, nextPos);
    m->floor = floor;
    m->floorHeight = floorHeight;

    if (upperWall != NULL || lowerWall != NULL) {
        struct Surface *wall = upperWall != NULL ? upperWall : lowerWall;

        wallDYaw = atan2s(wall->normal.z, wall->normal.x) - m->faceAngle[1];
        m->wall = wall;

        if (wall->type == SURFACE_BURNING) {
            return AIR_STEP_HIT_LAVA_WALL;
        }

        /*
         * Only a wall more than 135 degrees off our facing -- that is, one we
         * are genuinely moving into rather than past -- counts as a hit.  This
         * threshold is the wall-kick window: too shallow an approach and the
         * kick simply is not offered.
         */
        if (wallDYaw < -0x6000 || wallDYaw > 0x6000) {
            m->flags |= MARIO_UNKNOWN_30;
            return AIR_STEP_HIT_WALL;
        }
    }
    return AIR_STEP_NONE;
}

s32 perform_air_step(struct MarioState *m, u32 stepArg) {
    Vec3f intendedPos;
    s32 i;
    s32 quarterStepResult;
    s32 stepResult = AIR_STEP_NONE;

    m->wall = NULL;

    for (i = 0; i < 4; i++) {
        intendedPos[0] = m->pos[0] + m->vel[0] / 4.0f;
        intendedPos[1] = m->pos[1] + m->vel[1] / 4.0f;
        intendedPos[2] = m->pos[2] + m->vel[2] / 4.0f;

        quarterStepResult = perform_air_quarter_step(m, intendedPos, stepArg);

        /* A later quarter step's result wins, except that these four end the
         * frame immediately -- there is nothing sensible to do after them. */
        if (quarterStepResult != AIR_STEP_NONE) {
            stepResult = quarterStepResult;
        }
        if (quarterStepResult == AIR_STEP_LANDED
            || quarterStepResult == AIR_STEP_GRABBED_LEDGE
            || quarterStepResult == AIR_STEP_GRABBED_CEILING
            || quarterStepResult == AIR_STEP_HIT_LAVA_WALL) {
            break;
        }
    }

    /* peakHeight tracks the apex of the current airborne stretch; fall damage
     * is computed from the drop below it. Updating it only while rising is what
     * makes that measurement "height fallen from the top of the arc". */
    if (m->vel[1] >= 0.0f) {
        m->peakHeight = m->pos[1];
    }

    m->terrainSoundAddend = mario_get_terrain_sound_addend(m);

    /* Gravity is applied *after* the move, so the velocity a frame moves with
     * is the velocity it entered with. Flying manages its own vertical motion. */
    if (m->action != ACT_FLYING) {
        apply_gravity(m);
    }
    apply_vertical_wind(m);

    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    vec3s_set(m->marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);

    return stepResult;
}

/* --- Hanging ------------------------------------------------------------ */

s32 perform_hanging_step(struct MarioState *m, Vec3f nextPos) {
    struct Surface *ceil;
    struct Surface *floor;
    f32 ceilHeight;
    f32 floorHeight;
    f32 ceilOffset;

    m->wall = NULL;
    resolve_and_return_wall_collisions(nextPos, 50.0f, 50.0f);

    ceilHeight = find_ceil(nextPos[0], nextPos[1], nextPos[2], &ceil);
    if (ceil == NULL) {
        return HANG_HIT_CEIL_OR_OOB;
    }
    if (ceil->type != SURFACE_HANGABLE) {
        return HANG_LEFT_CEIL;
    }

    floorHeight = find_floor(nextPos[0], nextPos[1], nextPos[2], &floor);
    if (floor == NULL) {
        return HANG_HIT_CEIL_OR_OOB;
    }
    if (ceilHeight - floorHeight <= MARIO_BODY_HEIGHT) {
        return HANG_HIT_CEIL_OR_OOB;
    }

    /* Hanging keeps the body a fixed distance below the ceiling; drifting more
     * than 30 units out of that band lets go. */
    ceilOffset = ceilHeight - (nextPos[1] + MARIO_BODY_HEIGHT);
    if (ceilOffset < -30.0f) {
        return HANG_HIT_CEIL_OR_OOB;
    }
    if (ceilOffset > 30.0f) {
        return HANG_LEFT_CEIL;
    }

    nextPos[1] += ceilOffset;
    vec3f_copy(m->pos, nextPos);
    m->floor = floor;
    m->floorHeight = floorHeight;
    m->ceil = ceil;
    m->ceilHeight = ceilHeight;

    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    vec3s_set(m->marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);
    return HANG_NONE;
}

/* --- Water -------------------------------------------------------------- */
/*
 * The demo level has no water, so swimming is not implemented.  The step
 * function exists and behaves sanely (it moves and collides) so that hosted
 * submerged actions link and run rather than failing at build time; see
 * docs/SLOTTING_IN_DECOMP.md for what is missing.
 */
s32 perform_water_step(struct MarioState *m) {
    s32 i;
    Vec3f nextPos;
    Vec3f step;
    struct Surface *floor;
    struct Surface *ceil;
    f32 floorHeight;
    f32 ceilHeight;

    vec3f_copy(step, m->vel);

    for (i = 0; i < 4; i++) {
        nextPos[0] = m->pos[0] + step[0] / 4.0f;
        nextPos[1] = m->pos[1] + step[1] / 4.0f;
        nextPos[2] = m->pos[2] + step[2] / 4.0f;

        resolve_and_return_wall_collisions(nextPos, 10.0f, 110.0f);
        floorHeight = find_floor(nextPos[0], nextPos[1], nextPos[2], &floor);
        ceilHeight = vec3f_find_ceil(nextPos, floorHeight, &ceil);

        if (floor == NULL) {
            return WATER_STEP_CANCELLED;
        }
        if (nextPos[1] < floorHeight) {
            nextPos[1] = floorHeight;
            vec3f_copy(m->pos, nextPos);
            m->floor = floor;
            m->floorHeight = floorHeight;
            return WATER_STEP_HIT_FLOOR;
        }
        if (nextPos[1] + 160.0f > ceilHeight) {
            return WATER_STEP_HIT_CEILING;
        }

        vec3f_copy(m->pos, nextPos);
        m->floor = floor;
        m->floorHeight = floorHeight;
    }

    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    vec3s_set(m->marioObj->header.gfx.angle, m->faceAngle[0], m->faceAngle[1], 0);
    return WATER_STEP_NONE;
}
