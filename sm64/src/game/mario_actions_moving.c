/*
 * new64 -- ground movement: walking, running, turning, sliding, crawling.
 *
 * ===================== HOW GROUND SPEED ACTUALLY WORKS ====================
 *
 * There is one authoritative horizontal speed, m->forwardVel, always along
 * m->faceAngle[1].  Steering does not decompose speed into components; it
 * rotates the facing and the speed follows.  That is why SM64 movement feels
 * like driving rather than like a twin-stick: you cannot strafe on the ground,
 * and a hard turn costs you nothing in speed except what friction takes.
 *
 * Acceleration in update_walking_speed() is  1.1 - forwardVel/43, which fades
 * to zero near 47 -- so the run-up curve is naturally ease-out, with no
 * separate "max speed" clamp doing the shaping.  The cap that *does* exist
 * (48.0) is only a safety net for speed gained by other means.
 *
 * Turning is capped at 0x800 per frame (11.25 degrees).  At 30fps that is a
 * little over a full turn per second, and it is the single most recognisable
 * number in the whole feel of the character.
 *
 * ========================= SLIDING IS A DIFFERENT MODEL ===================
 *
 * Slides do *not* use forwardVel as the source of truth -- they integrate
 * slideVelX/slideVelZ as a real 2D vector and derive forwardVel from it.  That
 * is why a slide can carry you sideways relative to your facing while walking
 * never can, and why steering during a slide feels like leaning rather than
 * turning.
 */
#include "mario_actions_moving.h"

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
#include "mario_actions_airborne.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "object_fields.h"
#include "rumble_init.h"
#include "sm64.h"
#include "surface_terrains.h"

#include <math.h>
#include <stddef.h>

/*
 * Landing behaviour table.
 *
 * Every "land" action shares one routine and differs only by this data.  The
 * jump chain lives in aPressedAction: land from a single jump and press A and
 * you get a double jump; land from a double and you get a triple.  Encoding it
 * as data rather than as a switch is what keeps the chain consistent no matter
 * which route you took into the landing.
 */
struct LandingAction {
    s16 numFrames;      /* frames before falling through to endAction */
    s16 unused;
    u32 verySteepAction;
    u32 endAction;
    u32 aPressedAction; /* the jump chain */
    u32 offFloorAction;
    u32 slideAction;
    u32 endJumpAction;
};

static struct LandingAction sJumpLandAction = {
    4, 5, ACT_FREEFALL, ACT_JUMP_LAND_STOP, ACT_DOUBLE_JUMP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_DOUBLE_JUMP,
};
static struct LandingAction sFreefallLandAction = {
    4, 5, ACT_FREEFALL, ACT_FREEFALL_LAND_STOP, ACT_DOUBLE_JUMP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_DOUBLE_JUMP,
};
static struct LandingAction sDoubleJumpLandAction = {
    4, 5, ACT_FREEFALL, ACT_DOUBLE_JUMP_LAND_STOP, ACT_TRIPLE_JUMP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_JUMP,
};
static struct LandingAction sSideFlipLandAction = {
    4, 5, ACT_FREEFALL, ACT_SIDE_FLIP_LAND_STOP, ACT_DOUBLE_JUMP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_DOUBLE_JUMP,
};
/* A long jump landing is longer (6 frames) and chains back into another long
 * jump, which is what makes consecutive long jumps possible at all. */
static struct LandingAction sLongJumpLandAction = {
    6, 5, ACT_FREEFALL, ACT_LONG_JUMP_LAND_STOP, ACT_LONG_JUMP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_LONG_JUMP,
};
/* A triple jump landing offers no chained jump of its own (ACT_UNINITIALIZED),
 * so pressing A gives a plain jump and the chain restarts. */
static struct LandingAction sTripleJumpLandAction = {
    4, 0, ACT_FREEFALL, ACT_TRIPLE_JUMP_LAND_STOP, ACT_UNINITIALIZED, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_JUMP,
};
static struct LandingAction sBackflipLandAction = {
    4, 0, ACT_FREEFALL, ACT_BACKFLIP_LAND_STOP, ACT_BACKFLIP, ACT_FREEFALL,
    ACT_BEGIN_SLIDING, ACT_BACKFLIP,
};

/* --- Shared helpers ----------------------------------------------------- */

s32 analog_stick_held_back(struct MarioState *m) {
    s16 intendedDYaw = m->intendedYaw - m->faceAngle[1];

    /* ~100 degrees, deliberately wider than 90 so that steering hard around a
     * corner does not accidentally trigger a turnaround. */
    return intendedDYaw < -0x471C || intendedDYaw > 0x471C;
}

s32 should_begin_sliding(struct MarioState *m) {
    if (m->input & INPUT_ABOVE_SLIDE) {
        s32 slideLevel = (m->area != NULL
                          && (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE);
        s32 movingBackward = m->forwardVel <= -1.0f;

        /* On a slippery slope you only lose your footing if you are facing
         * downhill (or already moving backwards). Facing uphill, you hold. */
        if (slideLevel || movingBackward || mario_facing_downhill(m, FALSE)) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * B on the ground is either a dive or a punch, and the difference is a speed
 * test: at 29 units/frame or more, with the stick pushed past 48, you dive.
 * Below either threshold you punch.  This is the entire "run and dive" input.
 */
s32 check_ground_dive_or_punch(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        if (m->forwardVel >= 29.0f && m->controller->stickMag > 48.0f) {
            m->vel[1] = 20.0f;
            return set_mario_action(m, ACT_DIVE, 1);
        }
        return set_mario_action(m, ACT_MOVE_PUNCHING, 0);
    }
    return FALSE;
}

s32 begin_braking_action(struct MarioState *m) {
    if (m->actionState == 1) {
        m->faceAngle[1] = (s16) m->actionArg;
        return set_mario_action(m, ACT_STANDING_AGAINST_WALL, 0);
    }
    /* Only a real skid above 16 units/frame, and only on ground shallow enough
     * to brake on (normal.y >= sin(10 deg)). */
    if (m->forwardVel >= 16.0f && m->floor->normal.y >= 0.17364818f) {
        return set_mario_action(m, ACT_BRAKING, 0);
    }
    return set_mario_action(m, ACT_DECELERATING, 0);
}

void align_with_floor(struct MarioState *m) {
    m->pos[1] = m->floorHeight;
}

void apply_slope_accel(struct MarioState *m) {
    f32 slopeAccel;
    struct Surface *floor = m->floor;
    f32 steepness = sqrtf(floor->normal.x * floor->normal.x
                          + floor->normal.z * floor->normal.z);
    u16 floorDYaw = (u16) (m->floorAngle - m->faceAngle[1]);

    if (mario_floor_is_slope(m)) {
        s16 slopeClass = 0;

        /* Soft knockback ignores surface class, sliding as if on default
         * ground regardless of what it landed on. */
        if (m->action != ACT_SOFT_BACKWARD_GROUND_KB
            && m->action != ACT_SOFT_FORWARD_GROUND_KB) {
            slopeClass = (s16) mario_get_floor_class(m);
        }

        /*
         * Slope pull, per surface class.  The ratio between these is what makes
         * ice feel like ice: at 5.3 versus 1.7, a slope you can walk up on
         * grass is unclimbable on ice.
         */
        switch (slopeClass) {
            case SURFACE_CLASS_VERY_SLIPPERY: slopeAccel = 5.3f; break;
            case SURFACE_CLASS_SLIPPERY:      slopeAccel = 2.7f; break;
            case SURFACE_CLASS_NOT_SLIPPERY:  slopeAccel = 0.0f; break;
            default:                          slopeAccel = 1.7f; break;
        }

        /* Facing downhill: gravity adds to speed. Uphill: it subtracts. */
        if (floorDYaw > 0xC000 || floorDYaw < 0x4000) {
            m->forwardVel += slopeAccel * steepness;
        } else {
            m->forwardVel -= slopeAccel * steepness;
        }
    }

    m->slideYaw = m->faceAngle[1];
    m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
    m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
    m->vel[0] = m->slideVelX;
    m->vel[1] = 0.0f;
    m->vel[2] = m->slideVelZ;
}

s32 apply_landing_accel(struct MarioState *m, f32 frictionFactor) {
    s32 stopped = FALSE;

    apply_slope_accel(m);

    /* On a slope, landing friction is skipped entirely -- you keep your speed
     * and gravity keeps working on it. */
    if (!mario_floor_is_slope(m)) {
        m->forwardVel *= frictionFactor;
        if (m->forwardVel * m->forwardVel < 1.0f) {
            mario_set_forward_vel(m, 0.0f);
            stopped = TRUE;
        }
    }
    return stopped;
}

s32 apply_slope_decel(struct MarioState *m, f32 decelCoef) {
    f32 decel;
    s32 stopped = FALSE;

    switch (mario_get_floor_class(m)) {
        case SURFACE_CLASS_VERY_SLIPPERY: decel = decelCoef * 0.2f; break;
        case SURFACE_CLASS_SLIPPERY:      decel = decelCoef * 0.7f; break;
        case SURFACE_CLASS_NOT_SLIPPERY:  decel = decelCoef * 3.0f; break;
        default:                          decel = decelCoef * 2.0f; break;
    }

    m->forwardVel = approach_f32(m->forwardVel, 0.0f, decel, decel);
    if (m->forwardVel == 0.0f) {
        stopped = TRUE;
    }
    apply_slope_accel(m);
    return stopped;
}

s32 update_decelerating_speed(struct MarioState *m) {
    s32 stopped = FALSE;

    m->forwardVel = approach_f32(m->forwardVel, 0.0f, 1.0f, 1.0f);
    if (m->forwardVel == 0.0f) {
        stopped = TRUE;
    }
    apply_slope_accel(m);
    return stopped;
}

void update_walking_speed(struct MarioState *m) {
    f32 maxTargetSpeed;
    f32 targetSpeed;

    /* Deep-snow-style surfaces cap you lower than normal ground. */
    if (m->floor != NULL && m->floor->type == SURFACE_SLOW) {
        maxTargetSpeed = 24.0f;
    } else {
        maxTargetSpeed = 32.0f;
    }

    targetSpeed = m->intendedMag < maxTargetSpeed ? m->intendedMag : maxTargetSpeed;

    if (m->quicksandDepth > 10.0f) {
        targetSpeed *= 6.25f / m->quicksandDepth;
    }

    if (m->forwardVel <= 0.0f) {
        /* Coming out of backwards motion: flat acceleration. */
        m->forwardVel += 1.1f;
    } else if (m->forwardVel <= targetSpeed) {
        /* The ease-out curve. Acceleration hits zero near forwardVel 47.3, so
         * the approach to top speed is asymptotic rather than clamped. */
        m->forwardVel += 1.1f - m->forwardVel / 43.0f;
    } else if (m->floor->normal.y >= 0.95f) {
        /* Over target on near-flat ground: bleed off at 1 per frame. Note this
         * does *not* apply on slopes, so downhill speed is kept. */
        m->forwardVel -= 1.0f;
    }

    if (m->forwardVel > 48.0f) {
        m->forwardVel = 48.0f;
    }

    /* Steering: ease facing toward the stick direction, 0x800 per frame max. */
    m->faceAngle[1] =
        m->intendedYaw
        - (s16) approach_s32((s16) (m->intendedYaw - m->faceAngle[1]), 0, 0x800, 0x800);

    apply_slope_accel(m);
}

/*
 * Rotate the slide velocity vector downhill and bleed it by lossFactor.
 *
 * The facing angle is eased toward the slide direction by 0x200 per frame, but
 * only within a quadrant -- a slide that is exactly perpendicular to your
 * facing is left alone, which is why you can slide sideways indefinitely.
 */
void update_sliding_angle(struct MarioState *m, f32 accel, f32 lossFactor) {
    s32 newFacingDYaw;
    s16 facingDYaw;
    struct Surface *floor = m->floor;
    s16 slopeAngle = atan2s(floor->normal.z, floor->normal.x);
    f32 steepness = sqrtf(floor->normal.x * floor->normal.x
                          + floor->normal.z * floor->normal.z);

    m->slideVelX += accel * steepness * sins(slopeAngle);
    m->slideVelZ += accel * steepness * coss(slopeAngle);

    m->slideYaw = atan2s(m->slideVelZ, m->slideVelX);

    facingDYaw = (s16) (m->faceAngle[1] - m->slideYaw);
    newFacingDYaw = facingDYaw;

    if (newFacingDYaw > 0 && newFacingDYaw <= 0x4000) {
        if ((newFacingDYaw -= 0x200) < 0) {
            newFacingDYaw = 0;
        }
    } else if (newFacingDYaw > -0x4000 && newFacingDYaw < 0) {
        if ((newFacingDYaw += 0x200) > 0) {
            newFacingDYaw = 0;
        }
    } else if (newFacingDYaw > 0x4000 && newFacingDYaw < 0x8000) {
        if ((newFacingDYaw += 0x200) > 0x8000) {
            newFacingDYaw = 0x8000;
        }
    } else if (newFacingDYaw > -0x8000 && newFacingDYaw < -0x4000) {
        if ((newFacingDYaw -= 0x200) < -0x8000) {
            newFacingDYaw = -0x8000;
        }
    }

    m->faceAngle[1] = (s16) (m->slideYaw + newFacingDYaw);

    m->vel[0] = m->slideVelX = m->slideVelX * lossFactor;
    m->vel[2] = m->slideVelZ = m->slideVelZ * lossFactor;

    /*
     * forwardVel is *derived* here, and signed by whether the slide runs with
     * or against our facing.  Note it is computed from the post-loss velocity
     * while the sign uses the pre-rotation facing delta, so it lags the true
     * state by a frame.  That lag is observable and is left in place.
     */
    m->forwardVel = sqrtf(m->slideVelX * m->slideVelX + m->slideVelZ * m->slideVelZ)
                    * ((facingDYaw > -0x4000 && facingDYaw < 0x4000) ? 1.0f : -1.0f);
}

s32 update_sliding(struct MarioState *m, f32 stopSpeed) {
    f32 lossFactor;
    f32 accel;
    f32 oldSpeed, newSpeed;
    s32 stopped = FALSE;

    s16 intendedDYaw = (s16) (m->intendedYaw - m->slideYaw);
    f32 forward = coss(intendedDYaw);
    f32 sideward = sins(intendedDYaw);

    /*
     * Pushing *against* the slide is weakened, and weakened less the faster you
     * are going -- at high speed, braking input barely registers.  This is what
     * makes fast slides feel committed rather than cancellable.
     */
    if (forward < 0.0f && m->forwardVel >= 0.0f) {
        forward *= 0.5f + 0.5f * m->forwardVel / 100.0f;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_CLASS_VERY_SLIPPERY:
            accel = 10.0f;
            lossFactor = m->intendedMag / 32.0f * forward * 0.02f + 0.98f;
            break;
        case SURFACE_CLASS_SLIPPERY:
            accel = 8.0f;
            lossFactor = m->intendedMag / 32.0f * forward * 0.02f + 0.96f;
            break;
        case SURFACE_CLASS_NOT_SLIPPERY:
            accel = 5.0f;
            lossFactor = m->intendedMag / 32.0f * forward * 0.02f + 0.92f;
            break;
        default:
            accel = 7.0f;
            lossFactor = m->intendedMag / 32.0f * forward * 0.02f + 0.92f;
            break;
    }

    oldSpeed = sqrtf(m->slideVelX * m->slideVelX + m->slideVelZ * m->slideVelZ);

    /*
     * Steering a slide rotates the velocity vector using a small-angle
     * approximation.  The second line uses the *already updated* X component
     * against the old Z, so the rotation is slightly asymmetric and not quite
     * norm-preserving -- which is why the magnitude is explicitly restored just
     * below rather than trusted.
     */
    m->slideVelX += m->slideVelZ * (m->intendedMag / 32.0f) * sideward * 0.05f;
    m->slideVelZ -= m->slideVelX * (m->intendedMag / 32.0f) * sideward * 0.05f;

    newSpeed = sqrtf(m->slideVelX * m->slideVelX + m->slideVelZ * m->slideVelZ);

    if (oldSpeed > 0.0f && newSpeed > 0.0f) {
        m->slideVelX = m->slideVelX * oldSpeed / newSpeed;
        m->slideVelZ = m->slideVelZ * oldSpeed / newSpeed;
    }

    update_sliding_angle(m, accel, lossFactor);

    /* A slide only ends on ground flat enough to stop on. */
    if (!mario_floor_is_slope(m) && m->forwardVel * m->forwardVel < stopSpeed * stopSpeed) {
        mario_set_forward_vel(m, 0.0f);
        stopped = TRUE;
    }
    return stopped;
}

/*
 * Pick a locomotion animation and emit footstep sounds.
 *
 * The speed bands (tiptoe / walk / run) are what make the character read as
 * having weight.  new64 renders a capsule, so the animation choice is cosmetic
 * here, but the *sound* timing is not: it is recorded in the trace and is a
 * useful check that speed is behaving.
 */
void anim_and_audio_for_walk(struct MarioState *m) {
    s32 targetPitch = 0;
    f32 speed = m->forwardVel < 0.0f ? -m->forwardVel : m->forwardVel;
    struct Object *marioObj = m->marioObj;

    if (speed < 1.0f) {
        set_mario_animation(m, MARIO_ANIM_IDLE_HEAD_CENTER);
    } else if (speed < 5.0f) {
        /* Tiptoe: a fixed-rate animation, since at this speed the stride length
         * would otherwise collapse. */
        set_mario_animation(m, MARIO_ANIM_TIPTOE);
        play_sound_if_no_flag(m, SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend,
                             MARIO_ACTION_SOUND_PLAYED);
    } else if (speed < 22.0f) {
        /* Walk: animation rate scales with speed so feet do not skate. */
        s32 accel = (s32) (speed / 4.0f * 0x10000);

        if (accel < 0x1000) {
            accel = 0x1000;
        }
        set_mario_anim_with_accel(m, MARIO_ANIM_WALKING, accel);
        if ((m->marioObj->header.gfx.animInfo.animFrame & 0x0F) == 0) {
            play_sound(SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend,
                       marioObj->header.gfx.cameraToObject);
        }
    } else {
        s32 accel = (s32) (speed / 4.0f * 0x10000);

        if (accel < 0x1000) {
            accel = 0x1000;
        }
        set_mario_anim_with_accel(m, MARIO_ANIM_RUNNING, accel);
        if ((m->marioObj->header.gfx.animInfo.animFrame & 0x07) == 0) {
            play_sound(SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend,
                       marioObj->header.gfx.cameraToObject);
        }
    }

    /* Lean into a slope: purely visual, driven off the floor pitch. */
    if (m->floor != NULL) {
        targetPitch = -(s32) (find_floor_slope(m, 0) / 2);
    }
    marioObj->oMarioWalkingPitch =
        (s32) approach_s32((s16) marioObj->oMarioWalkingPitch, targetPitch, 0x800, 0x800);
    m->marioObj->header.gfx.angle[0] = (s16) marioObj->oMarioWalkingPitch;
}

/*
 * Footstep sound on either of two animation frames.
 *
 * Clearing the latch afterwards is what allows the *next* footstep in the same
 * action to sound; without it only the first step of a run would be audible.
 */
static void play_step_sound(struct MarioState *m, s16 frame1, s16 frame2) {
    if (is_anim_past_frame(m, frame1) || is_anim_past_frame(m, frame2)) {
        play_sound_and_spawn_particles(m, SOUND_ACTION_TERRAIN_STEP, 0);
        m->flags &= ~MARIO_ACTION_SOUND_PLAYED;
    }
}

/* --- Walking ------------------------------------------------------------ */

/*
 * Running into a wall while walking: either push against it (losing speed) or
 * sidle along it.  Which one depends on how square the wall is to our facing.
 */
static void push_or_sidle_wall(struct MarioState *m, Vec3f startPos) {
    s16 wallAngle;
    s16 dWallAngle;
    f32 dx = m->pos[0] - startPos[0];
    f32 dz = m->pos[2] - startPos[2];
    f32 movedDistance = sqrtf(dx * dx + dz * dz);
    s32 slowDownVel = FALSE;

    if (m->forwardVel > 16.0f) {
        m->forwardVel = 16.0f;
    }

    if (m->wall != NULL) {
        wallAngle = atan2s(m->wall->normal.z, m->wall->normal.x);
        dWallAngle = (s16) (wallAngle - m->faceAngle[1]);
        slowDownVel = (dWallAngle <= -0x4000 || dWallAngle >= 0x4000);
    }

    if (slowDownVel && movedDistance < 4.0f) {
        /* Squarely into a wall and going nowhere: stand against it. */
        m->actionState = 1;
        m->actionArg = (u32) (u16) m->faceAngle[1];
        set_mario_animation(m, MARIO_ANIM_PUSHING);
        play_sound(SOUND_MARIO_UH, m->marioObj->header.gfx.cameraToObject);
    } else {
        anim_and_audio_for_walk(m);
    }
}

/* Dropping off a ledge you are standing on the edge of, by holding back. */
static s32 check_ledge_climb_down(struct MarioState *m) {
    struct WallCollisionData wallCols;
    struct Surface *floor;
    f32 floorHeight;
    s16 wallAngle;
    s16 wallDYaw;

    if (m->forwardVel < 10.0f) {
        wallCols.x = m->pos[0];
        wallCols.y = m->pos[1];
        wallCols.z = m->pos[2];
        wallCols.radius = 10.0f;
        wallCols.offsetY = -10.0f;

        if (find_wall_collisions(&wallCols) != 0) {
            floorHeight = find_floor(wallCols.x, wallCols.y, wallCols.z, &floor);
            if (floor != NULL && wallCols.y - floorHeight > 160.0f) {
                wallAngle = atan2s(wallCols.walls[0]->normal.z, wallCols.walls[0]->normal.x);
                wallDYaw = (s16) (wallAngle - m->faceAngle[1]);

                if (wallDYaw > 0x4000 - 0x2000 && wallDYaw < 0x4000 + 0x2000) {
                    m->pos[0] = wallCols.x - 20.0f * wallCols.walls[0]->normal.x;
                    m->pos[2] = wallCols.z - 20.0f * wallCols.walls[0]->normal.z;
                    m->faceAngle[1] = (s16) wallAngle;
                    set_mario_action(m, ACT_LEDGE_CLIMB_DOWN, 0);
                    set_mario_animation(m, MARIO_ANIM_CLIMB_DOWN_LEDGE);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static s32 act_walking(struct MarioState *m) {
    Vec3f startPos;

    if (should_begin_sliding(m)) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_JUMP, 0);
    }
    if (check_ground_dive_or_punch(m)) {
        return TRUE;
    }
    /* Stick released and A not pressed: begin stopping. */
    if (m->input & INPUT_UNKNOWN_5) {
        return begin_braking_action(m);
    }
    /* Hard reversal at speed is a distinct turnaround state, not just steering,
     * because it has to bleed the old speed off first. */
    if (analog_stick_held_back(m) && m->forwardVel >= 16.0f) {
        return set_mario_action(m, ACT_TURNING_AROUND, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_CROUCH_SLIDE, 0);
    }

    m->actionState = 0;
    vec3f_copy(startPos, m->pos);
    update_walking_speed(m);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            set_mario_animation(m, MARIO_ANIM_GENERAL_FALL);
            break;

        case GROUND_STEP_NONE:
            anim_and_audio_for_walk(m);
            /* Kicking up dust when the stick asks for far more speed than we
             * have -- i.e. during the initial acceleration burst. */
            if (m->intendedMag - m->forwardVel > 16.0f) {
                m->particleFlags |= PARTICLE_DUST;
            }
            break;

        case GROUND_STEP_HIT_WALL:
            push_or_sidle_wall(m, startPos);
            m->actionTimer = 0;
            break;

        default:
            break;
    }

    check_ledge_climb_down(m);
    return FALSE;
}

static s32 act_turning_around(struct MarioState *m) {
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    /* Jumping out of a turnaround with the stick still held back is a side
     * flip -- one of the two ways to get one. */
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_SIDE_FLIP, 0);
    }
    if (!analog_stick_held_back(m)) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (check_ground_dive_or_punch(m)) {
        return TRUE;
    }

    apply_slope_decel(m, 2.0f);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;
        default:
            break;
    }

    /* Once the old momentum is spent, commit to the new direction. */
    if (m->forwardVel >= 18.0f) {
        set_mario_animation(m, MARIO_ANIM_TURNING_PART1);
    } else {
        set_mario_animation(m, MARIO_ANIM_TURNING_PART2);
        if (is_anim_at_end(m)) {
            if (m->forwardVel > 0.0f) {
                begin_braking_action(m);
            } else {
                set_mario_action(m, ACT_FINISH_TURNING_AROUND, 0);
            }
        }
    }
    return FALSE;
}

static s32 act_finish_turning_around(struct MarioState *m) {
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_SIDE_FLIP, 0);
    }

    update_walking_speed(m);
    set_mario_animation(m, MARIO_ANIM_TURNING_PART2);

    if (perform_ground_step(m) == GROUND_STEP_LEFT_GROUND) {
        set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (is_anim_at_end(m)) {
        set_mario_action(m, ACT_WALKING, 0);
    }

    m->faceAngle[1] += 0x8000;
    return FALSE;
}

static s32 act_braking(struct MarioState *m) {
    if (!(m->input & INPUT_UNKNOWN_5)) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_MOVE_PUNCHING, 0);
    }

    /* A skid decelerates hard (coefficient 2.0 on top of class scaling). */
    if (apply_slope_decel(m, 2.0f)) {
        return set_mario_action(m, ACT_BRAKING_STOP, 0);
    }

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            break;
        default:
            break;
    }

    play_sound_if_no_flag(m, SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend,
                          MARIO_ACTION_SOUND_PLAYED);
    set_mario_animation(m, MARIO_ANIM_SKID_ON_GROUND);
    m->particleFlags |= PARTICLE_DUST;
    return FALSE;
}

static s32 act_decelerating(struct MarioState *m) {
    if (!(m->input & INPUT_UNKNOWN_5)) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_MOVE_PUNCHING, 0);
    }
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_CROUCH_SLIDE, 0);
    }

    if (update_decelerating_speed(m)) {
        return set_mario_action(m, ACT_IDLE, 0);
    }

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            if (m->forwardVel > 16.0f) {
                mario_bonk_reflection(m, TRUE);
            }
            break;
        default:
            break;
    }

    {
        s32 accel = (s32) (m->forwardVel / 4.0f * 0x10000);

        if (accel < 0x1000) {
            accel = 0x1000;
        }
        set_mario_anim_with_accel(m, MARIO_ANIM_WALKING, accel);
        play_sound_if_no_flag(m, SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend,
                              MARIO_ACTION_SOUND_PLAYED);
    }
    return FALSE;
}

static s32 act_crawling(struct MarioState *m) {
    if (should_begin_sliding(m)) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_JUMP, 0);
    }
    if (check_ground_dive_or_punch(m)) {
        return TRUE;
    }
    if (m->input & INPUT_UNKNOWN_5) {
        return set_mario_action(m, ACT_STOP_CRAWLING, 0);
    }
    if (!(m->input & INPUT_Z_DOWN)) {
        return set_mario_action(m, ACT_STOP_CRAWLING, 0);
    }

    /* Crawling is slow and, via mario_get_floor_class(), makes the floor count
     * as not-slippery -- so you can crawl up slopes you would slide down. */
    if (m->intendedMag > 10.0f) {
        m->intendedMag = 10.0f;
    }
    update_walking_speed(m);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            if (m->forwardVel > 10.0f) {
                mario_set_forward_vel(m, 10.0f);
            }
            align_with_floor(m);
            break;
        case GROUND_STEP_NONE:
            align_with_floor(m);
            break;
        default:
            break;
    }

    {
        s32 accel = (s32) (m->forwardVel / 4.0f * 0x10000);

        if (accel < 0x1000) {
            accel = 0x1000;
        }
        set_mario_anim_with_accel(m, MARIO_ANIM_CRAWLING, accel);
        play_step_sound(m, 26, 79);
    }
    return FALSE;
}

/* --- Slides ------------------------------------------------------------- */

static s32 common_slide_action(struct MarioState *m, u32 endAction, u32 airAction,
                               s32 animation) {
    Vec3f pos;

    vec3f_copy(pos, m->pos);
    play_sound(SOUND_MOVING_TERRAIN_SLIDE + m->terrainSoundAddend,
               m->marioObj->header.gfx.cameraToObject);
    adjust_sound_for_speed(m);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, airAction, 0);
            break;

        case GROUND_STEP_NONE:
            set_mario_animation(m, animation);
            align_with_floor(m);
            m->particleFlags |= PARTICLE_DUST;
            break;

        case GROUND_STEP_HIT_WALL:
            /* Hitting a wall mid-slide only costs you speed if you were
             * actually moving into it; a glancing scrape is free. */
            if (m->floor != NULL && !mario_floor_is_slippery(m)) {
                if (m->forwardVel * m->forwardVel > 16.0f) {
                    mario_bonk_reflection(m, TRUE);
                    m->particleFlags |= PARTICLE_VERTICAL_STAR;
                    set_mario_action(m, endAction, 0);
                } else {
                    mario_set_forward_vel(m, 0.0f);
                }
            } else {
                m->particleFlags |= PARTICLE_VERTICAL_STAR;
                set_mario_action(m, endAction, 0);
            }
            break;

        default:
            break;
    }
    (void) pos;
    return FALSE;
}

static s32 common_slide_action_with_jump(struct MarioState *m, u32 stopAction,
                                         u32 jumpAction, u32 airAction, s32 animation) {
    if (m->actionTimer == 5) {
        if (m->input & INPUT_A_PRESSED) {
            return set_jumping_action(m, jumpAction, 0);
        }
    } else {
        m->actionTimer++;
    }

    if (update_sliding(m, 4.0f)) {
        return set_mario_action(m, stopAction, 0);
    }
    common_slide_action(m, stopAction, airAction, animation);
    return FALSE;
}

static s32 act_butt_slide(struct MarioState *m) {
    s32 cancel = common_slide_action_with_jump(m, ACT_BUTT_SLIDE_STOP, ACT_JUMP,
                                               ACT_BUTT_SLIDE_AIR, MARIO_ANIM_SLIDE);

    adjust_sound_for_speed(m);
    return cancel;
}

static s32 act_crouch_slide(struct MarioState *m) {
    if (m->actionTimer < 30) {
        m->actionTimer++;
        if (m->input & INPUT_A_PRESSED) {
            /* Crouch-slide into A within 30 frames is a long jump. This window
             * is why the long jump is a deliberate input and not accidental. */
            if (m->forwardVel > 10.0f) {
                return set_jumping_action(m, ACT_LONG_JUMP, 0);
            }
        }
    }
    if (m->input & INPUT_B_PRESSED) {
        if (m->forwardVel >= 10.0f) {
            return set_mario_action(m, ACT_SLIDE_KICK, 0);
        }
        return set_mario_action(m, ACT_MOVE_PUNCHING, 0x0009);
    }
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_JUMP, 0);
    }

    /*
     * Deliberately no INPUT_Z_PRESSED branch here.  A crouch slide is *entered*
     * by a Z press, and because set_mario_action() re-dispatches within the same
     * frame, this action sees that very same edge flag still set.  Any Z handler
     * here would therefore fire immediately on entry and the crouch slide could
     * never be held -- which also means the long jump (A during a crouch slide)
     * would be unreachable.
     */
    return common_slide_action_with_jump(m, ACT_CROUCHING, ACT_JUMP, ACT_FREEFALL,
                                        MARIO_ANIM_START_CROUCHING);
}

static s32 act_stomach_slide(struct MarioState *m) {
    if (m->actionTimer++ > 0) {
        if (m->input & (INPUT_A_PRESSED | INPUT_B_PRESSED)) {
            return drop_and_set_mario_action(m, ACT_FORWARD_ROLLOUT, 0);
        }
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        /* nothing: stay sliding */
    }
    if (update_sliding(m, 4.0f)) {
        return set_mario_action(m, ACT_BRAKING_STOP, 0);
    }
    return common_slide_action(m, ACT_BRAKING_STOP, ACT_FREEFALL,
                               MARIO_ANIM_SLIDE_DIVE);
}

static s32 act_dive_slide(struct MarioState *m) {
    if (!(m->input & INPUT_ABOVE_SLIDE)
        && (m->input & (INPUT_A_PRESSED | INPUT_B_PRESSED))) {
        /* Dive into A/B is a forward rollout: the recovery that keeps momentum. */
        return set_mario_action(m, ACT_FORWARD_ROLLOUT, 0);
    }

    play_mario_heavy_landing_sound_once(m, SOUND_ACTION_TERRAIN_BODY_HIT_GROUND);

    if (update_sliding(m, 8.0f) && is_anim_at_end(m)) {
        mario_set_forward_vel(m, 0.0f);
        set_mario_action(m, ACT_STOMACH_SLIDE_STOP, 0);
    }
    common_slide_action(m, ACT_STOMACH_SLIDE_STOP, ACT_FREEFALL, MARIO_ANIM_DIVE);
    return FALSE;
}

static s32 act_slide_kick_slide(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_jumping_action(m, ACT_FORWARD_ROLLOUT, 0);
    }

    set_mario_animation(m, MARIO_ANIM_SLIDE_KICK);
    if (is_anim_at_end(m) && m->forwardVel < 1.0f) {
        return set_mario_action(m, ACT_SLIDE_KICK_SLIDE_STOP, 0);
    }

    update_sliding(m, 1.0f);
    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            set_mario_action(m, ACT_BACKWARD_GROUND_KB, 0);
            break;
        default:
            break;
    }

    m->particleFlags |= PARTICLE_DUST;
    return FALSE;
}

static s32 act_move_punching(struct MarioState *m) {
    if (should_begin_sliding(m)) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    if (m->actionState == 0 && (m->input & INPUT_A_PRESSED)) {
        return set_mario_action(m, ACT_JUMP, 0);
    }

    m->flags |= MARIO_PUNCHING;
    mario_set_forward_vel(m, approach_f32(m->forwardVel, 0.0f, 1.0f, 1.0f));

    if (m->forwardVel == 0.0f) {
        return set_mario_action(m, ACT_IDLE, 0);
    }

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;
        default:
            break;
    }

    set_mario_animation(m, MARIO_ANIM_FIRST_PUNCH_FAST);
    if (is_anim_at_end(m)) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    return FALSE;
}

/* --- Landing ------------------------------------------------------------ */

static s32 common_landing_cancels(struct MarioState *m, struct LandingAction *landingAction,
                                  s32 (*setAPressAction)(struct MarioState *, u32, u32)) {
    if (m->floor != NULL && m->floor->normal.y < 0.2923717f) {
        /* Landed on something far too steep to stand on. */
        return mario_push_off_steep_floor(m, landingAction->verySteepAction, 0);
    }

    m->doubleJumpTimer = (u8) landingAction->unused;

    if (++m->actionTimer >= landingAction->numFrames) {
        return set_mario_action(m, landingAction->endAction, 0);
    }
    if (m->input & INPUT_A_PRESSED) {
        return setAPressAction(m, landingAction->aPressedAction, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, landingAction->offFloorAction, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, landingAction->slideAction, 0);
    }
    return FALSE;
}

static s32 common_landing_action(struct MarioState *m, s16 animation, u32 airAction) {
    if (m->input & INPUT_NONZERO_ANALOG) {
        /* Landing with the stick held keeps momentum: apply light friction and
         * let the land action fall through to walking. */
        apply_landing_accel(m, 0.98f);
    } else if (m->forwardVel >= 16.0f) {
        apply_slope_decel(m, 2.0f);
    } else {
        m->vel[1] = 0.0f;
    }

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, airAction, 0);
            break;
        case GROUND_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            break;
        default:
            break;
    }

    if (m->forwardVel > 16.0f) {
        m->particleFlags |= PARTICLE_DUST;
    }
    set_mario_animation(m, animation);
    return FALSE;
}

static s32 act_jump_land(struct MarioState *m) {
    if (common_landing_cancels(m, &sJumpLandAction, set_jumping_action)) {
        return TRUE;
    }
    return common_landing_action(m, MARIO_ANIM_LAND_FROM_SINGLE_JUMP, ACT_FREEFALL);
}

static s32 act_freefall_land(struct MarioState *m) {
    if (common_landing_cancels(m, &sFreefallLandAction, set_jumping_action)) {
        return TRUE;
    }
    return common_landing_action(m, MARIO_ANIM_GENERAL_LAND, ACT_FREEFALL);
}

static s32 act_side_flip_land(struct MarioState *m) {
    if (common_landing_cancels(m, &sSideFlipLandAction, set_jumping_action)) {
        return TRUE;
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    return common_landing_action(m, MARIO_ANIM_SLIDEFLIP_LAND, ACT_FREEFALL);
}

static s32 act_double_jump_land(struct MarioState *m) {
    if (common_landing_cancels(m, &sDoubleJumpLandAction, set_jumping_action)) {
        return TRUE;
    }
    return common_landing_action(m, MARIO_ANIM_LAND_FROM_DOUBLE_JUMP, ACT_FREEFALL);
}

static s32 act_triple_jump_land(struct MarioState *m) {
    m->input &= ~INPUT_A_PRESSED;
    if (common_landing_cancels(m, &sTripleJumpLandAction, set_jumping_action)) {
        return TRUE;
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    play_mario_landing_sound_once(m, SOUND_ACTION_TERRAIN_LANDING);
    return common_landing_action(m, MARIO_ANIM_TRIPLE_JUMP_LAND, ACT_FREEFALL);
}

static s32 act_backflip_land(struct MarioState *m) {
    if (!(m->input & INPUT_Z_DOWN) || m->forwardVel >= 0.0f) {
        m->input &= ~INPUT_A_PRESSED;
    }
    if (common_landing_cancels(m, &sBackflipLandAction, set_jumping_action)) {
        return TRUE;
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    play_mario_landing_sound_once(m, SOUND_ACTION_TERRAIN_LANDING);
    return common_landing_action(m, MARIO_ANIM_TRIPLE_JUMP_LAND, ACT_FREEFALL);
}

static s32 act_long_jump_land(struct MarioState *m) {
    if (!(m->input & INPUT_Z_DOWN)) {
        m->input &= ~INPUT_B_PRESSED;
    }
    if (common_landing_cancels(m, &sLongJumpLandAction, set_jumping_action)) {
        return TRUE;
    }
    if (!(m->input & INPUT_NONZERO_ANALOG)) {
        play_mario_landing_sound_once(m, SOUND_ACTION_TERRAIN_LANDING);
    }
    return common_landing_action(
        m, m->marioObj->oMarioLongJumpIsSlow ? MARIO_ANIM_CROUCH_FROM_SLOW_LONGJUMP
                                             : MARIO_ANIM_CROUCH_FROM_FAST_LONGJUMP,
        ACT_FREEFALL);
}

static s32 act_quicksand_jump_land(struct MarioState *m) {
    if (m->actionTimer++ >= 5) {
        return set_mario_action(m, ACT_IDLE, 0);
    }
    stop_and_set_height_to_floor(m);
    set_mario_animation(m, MARIO_ANIM_LAND_FROM_SINGLE_JUMP);
    return FALSE;
}

static s32 act_ground_bonk(struct MarioState *m) {
    if (m->actionTimer++ >= 20) {
        return set_mario_action(m, ACT_IDLE, 0);
    }
    stop_and_set_height_to_floor(m);
    set_mario_animation(m, MARIO_ANIM_GROUND_BOUNCE);
    return FALSE;
}

/* --- Group dispatch ----------------------------------------------------- */

/*
 * Exits shared by every grounded action, checked before the action itself runs.
 * Plunging into water and being squished both outrank whatever the action was
 * about to do, so they are handled here rather than repeated 25 times.
 */
static s32 check_common_moving_cancels(struct MarioState *m) {
    if (m->waterLevel > FLOOR_LOWER_LIMIT_MISC && m->pos[1] < (f32) (m->waterLevel - 100)) {
        return set_water_plunge_action(m);
    }
    if (m->input & INPUT_SQUISHED) {
        return drop_and_set_mario_action(m, ACT_SQUISHED, 0);
    }
    return FALSE;
}


s32 mario_execute_moving_action(struct MarioState *m) {
    s32 cancel = FALSE;

    if (check_common_moving_cancels(m)) {
        return TRUE;
    }
    if (mario_update_quicksand(m, 0.25f)) {
        return TRUE;
    }

    switch (m->action) {
        case ACT_WALKING:            cancel = act_walking(m); break;
        case ACT_HOLD_WALKING:       cancel = act_walking(m); break;
        case ACT_TURNING_AROUND:     cancel = act_turning_around(m); break;
        case ACT_FINISH_TURNING_AROUND: cancel = act_finish_turning_around(m); break;
        case ACT_BRAKING:            cancel = act_braking(m); break;
        case ACT_DECELERATING:       cancel = act_decelerating(m); break;
        case ACT_HOLD_DECELERATING:  cancel = act_decelerating(m); break;
        case ACT_CRAWLING:           cancel = act_crawling(m); break;
        case ACT_BUTT_SLIDE:         cancel = act_butt_slide(m); break;
        case ACT_HOLD_BUTT_SLIDE:    cancel = act_butt_slide(m); break;
        case ACT_STOMACH_SLIDE:      cancel = act_stomach_slide(m); break;
        case ACT_HOLD_STOMACH_SLIDE: cancel = act_stomach_slide(m); break;
        case ACT_DIVE_SLIDE:         cancel = act_dive_slide(m); break;
        case ACT_CROUCH_SLIDE:       cancel = act_crouch_slide(m); break;
        case ACT_SLIDE_KICK_SLIDE:   cancel = act_slide_kick_slide(m); break;
        case ACT_MOVE_PUNCHING:      cancel = act_move_punching(m); break;
        case ACT_JUMP_LAND:          cancel = act_jump_land(m); break;
        case ACT_FREEFALL_LAND:      cancel = act_freefall_land(m); break;
        case ACT_DOUBLE_JUMP_LAND:   cancel = act_double_jump_land(m); break;
        case ACT_TRIPLE_JUMP_LAND:   cancel = act_triple_jump_land(m); break;
        case ACT_BACKFLIP_LAND:      cancel = act_backflip_land(m); break;
        case ACT_SIDE_FLIP_LAND:     cancel = act_side_flip_land(m); break;
        case ACT_LONG_JUMP_LAND:     cancel = act_long_jump_land(m); break;
        case ACT_HOLD_JUMP_LAND:     cancel = act_jump_land(m); break;
        case ACT_HOLD_FREEFALL_LAND: cancel = act_freefall_land(m); break;
        case ACT_QUICKSAND_JUMP_LAND: cancel = act_quicksand_jump_land(m); break;
        case ACT_HOLD_QUICKSAND_JUMP_LAND: cancel = act_quicksand_jump_land(m); break;
        case ACT_GROUND_BONK:        cancel = act_ground_bonk(m); break;
        default:
            /* Unimplemented moving action: fail safe to something controllable
             * rather than freezing in an unhandled state. */
            cancel = set_mario_action(m, ACT_IDLE, 0);
            break;
    }
    return cancel;
}
