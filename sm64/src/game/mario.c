/*
 * new64 -- the player module: state transitions, input digest, frame driver.
 *
 * ===================== HOW A FRAME OF MOVEMENT RUNS ======================
 *
 * execute_mario_action() is called once per tick and does, in order:
 *
 *   1. reset the per-frame scratch (particles, input digest, body state);
 *   2. sample the controller into the INPUT_* digest, and sample the world
 *      under the player into floor/ceil/water and the INPUT_* terrain bits;
 *   3. dispatch to the current action's group handler -- *in a loop*.
 *
 * That loop is the important part.  An action function returns TRUE to mean "I
 * changed action, run again this same frame" and FALSE to mean "I am done for
 * this frame".  So a single tick can traverse several states: landing from a
 * jump can go ACT_JUMP -> ACT_JUMP_LAND -> ACT_WALKING before the frame ends.
 * Transitions are therefore free, and action code is written assuming they are.
 *
 * ===================== WHY set_mario_action() EXISTS ======================
 *
 * Nothing assigns m->action directly.  set_mario_action() clears actionState,
 * actionTimer and actionArg, resets the per-action sound latches, and -- for
 * moving and airborne actions -- applies the entry velocity for that state.
 * All the jump heights live in set_mario_action_airborne() rather than in the
 * jump actions themselves, which is why "how high is a double jump" is
 * answered in one place for every route into it.
 */
#include "mario.h"

#include "area.h"
#include "audio/external.h"
#include "audio_defines.h"
#include "camera.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario_actions_airborne.h"
#include "mario_actions_automatic.h"
#include "mario_actions_cutscene.h"
#include "mario_actions_moving.h"
#include "mario_actions_object.h"
#include "mario_actions_stationary.h"
#include "mario_actions_submerged.h"
#include "mario_animation_ids.h"
#include "mario_step.h"
#include "object_fields.h"
#include "rumble_init.h"
#include "save_file.h"
#include "sm64.h"
#include "surface_terrains.h"

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

struct MarioState *gMarioState;

/* --- Animation ---------------------------------------------------------- */

/*
 * new64 ships no animation data, but animation *length* is physics: several
 * actions end via is_anim_at_end().  This table gives every id a length, and
 * m64_anim_load_table() can override it from a text file so real values can be
 * dropped in without a rebuild.  Defaults are chosen to be plausible; where an
 * action's duration matters for feel, this engine's own action code uses an
 * explicit timer instead so the default table cannot make it wrong.
 */
#define M64_ANIM_DEFAULT_LEN 20

static s16 sAnimLength[MARIO_ANIM_COUNT];
static struct Animation sAnimPool[MARIO_ANIM_COUNT];

void m64_anim_init(void) {
    s32 i;

    for (i = 0; i < MARIO_ANIM_COUNT; i++) {
        if (sAnimLength[i] == 0) {
            sAnimLength[i] = M64_ANIM_DEFAULT_LEN;
        }
        sAnimPool[i].flags = 0;
        sAnimPool[i].animYTransDivisor = 0;
        sAnimPool[i].startFrame = 0;
        sAnimPool[i].loopStart = 0;
        sAnimPool[i].loopEnd = sAnimLength[i];
        sAnimPool[i].values = NULL;
        sAnimPool[i].index = NULL;
        sAnimPool[i].length = 0;
    }
}

void m64_anim_set_length(s32 animID, s16 frames) {
    if (animID >= 0 && animID < MARIO_ANIM_COUNT && frames > 0) {
        sAnimLength[animID] = frames;
        sAnimPool[animID].loopEnd = frames;
    }
}

/*
 * Name -> id, for the loadable timing table.
 *
 * The table is generated from the enum by stringifying each entry, so it cannot
 * drift out of sync with mario_animation_ids.h the way a hand-written list
 * would.
 */
static const char *const sAnimNames[MARIO_ANIM_COUNT] = {
#define M64_ANIM_NAME(id) [id] = #id,
    M64_ANIM_NAME(MARIO_ANIM_SLOW_LEDGE_GRAB)
    M64_ANIM_NAME(MARIO_ANIM_BACKFLIP)
    M64_ANIM_NAME(MARIO_ANIM_SKID_ON_GROUND)
    M64_ANIM_NAME(MARIO_ANIM_STOP_SKID)
    M64_ANIM_NAME(MARIO_ANIM_CROUCH_FROM_FAST_LONGJUMP)
    M64_ANIM_NAME(MARIO_ANIM_CROUCH_FROM_SLOW_LONGJUMP)
    M64_ANIM_NAME(MARIO_ANIM_FAST_LONGJUMP)
    M64_ANIM_NAME(MARIO_ANIM_SLOW_LONGJUMP)
    M64_ANIM_NAME(MARIO_ANIM_AIRBORNE_ON_STOMACH)
    M64_ANIM_NAME(MARIO_ANIM_STOP_CROUCHING)
    M64_ANIM_NAME(MARIO_ANIM_START_CROUCHING)
    M64_ANIM_NAME(MARIO_ANIM_CROUCHING)
    M64_ANIM_NAME(MARIO_ANIM_CRAWLING)
    M64_ANIM_NAME(MARIO_ANIM_STOP_CRAWLING)
    M64_ANIM_NAME(MARIO_ANIM_START_CRAWLING)
    M64_ANIM_NAME(MARIO_ANIM_WALK_PANTING)
    M64_ANIM_NAME(MARIO_ANIM_TURNING_PART1)
    M64_ANIM_NAME(MARIO_ANIM_TURNING_PART2)
    M64_ANIM_NAME(MARIO_ANIM_SLIDEFLIP_LAND)
    M64_ANIM_NAME(MARIO_ANIM_SLIDEFLIP)
    M64_ANIM_NAME(MARIO_ANIM_TRIPLE_JUMP_LAND)
    M64_ANIM_NAME(MARIO_ANIM_TRIPLE_JUMP)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_HEAD_LEFT)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_HEAD_RIGHT)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_HEAD_CENTER)
    M64_ANIM_NAME(MARIO_ANIM_START_TIPTOE)
    M64_ANIM_NAME(MARIO_ANIM_SLIDEJUMP)
    M64_ANIM_NAME(MARIO_ANIM_START_WALLKICK)
    M64_ANIM_NAME(MARIO_ANIM_FORWARD_SPINNING_FLIP)
    M64_ANIM_NAME(MARIO_ANIM_DIVE)
    M64_ANIM_NAME(MARIO_ANIM_SLIDE_DIVE)
    M64_ANIM_NAME(MARIO_ANIM_SLIDE_KICK)
    M64_ANIM_NAME(MARIO_ANIM_CROUCH_FROM_SLIDE_KICK)
    M64_ANIM_NAME(MARIO_ANIM_STOP_SLIDE)
    M64_ANIM_NAME(MARIO_ANIM_SLIDE)
    M64_ANIM_NAME(MARIO_ANIM_TIPTOE)
    M64_ANIM_NAME(MARIO_ANIM_TWIRL_LAND)
    M64_ANIM_NAME(MARIO_ANIM_TWIRL)
    M64_ANIM_NAME(MARIO_ANIM_START_TWIRL)
    M64_ANIM_NAME(MARIO_ANIM_GROUND_POUND_LANDING)
    M64_ANIM_NAME(MARIO_ANIM_TRIPLE_JUMP_GROUND_POUND)
    M64_ANIM_NAME(MARIO_ANIM_START_GROUND_POUND)
    M64_ANIM_NAME(MARIO_ANIM_GROUND_POUND)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_WITH_LIGHT_OBJ)
    M64_ANIM_NAME(MARIO_ANIM_WALKING)
    M64_ANIM_NAME(MARIO_ANIM_LAND_FROM_DOUBLE_JUMP)
    M64_ANIM_NAME(MARIO_ANIM_DOUBLE_JUMP_FALL)
    M64_ANIM_NAME(MARIO_ANIM_SINGLE_JUMP)
    M64_ANIM_NAME(MARIO_ANIM_LAND_FROM_SINGLE_JUMP)
    M64_ANIM_NAME(MARIO_ANIM_AIR_KICK)
    M64_ANIM_NAME(MARIO_ANIM_DOUBLE_JUMP_RISE)
    M64_ANIM_NAME(MARIO_ANIM_FALL_FROM_SLIDE_KICK)
    M64_ANIM_NAME(MARIO_ANIM_GENERAL_FALL)
    M64_ANIM_NAME(MARIO_ANIM_GENERAL_LAND)
    M64_ANIM_NAME(MARIO_ANIM_GROUND_KICK)
    M64_ANIM_NAME(MARIO_ANIM_FIRST_PUNCH)
    M64_ANIM_NAME(MARIO_ANIM_SECOND_PUNCH)
    M64_ANIM_NAME(MARIO_ANIM_FIRST_PUNCH_FAST)
    M64_ANIM_NAME(MARIO_ANIM_SECOND_PUNCH_FAST)
    M64_ANIM_NAME(MARIO_ANIM_PUSHING)
    M64_ANIM_NAME(MARIO_ANIM_FORWARD_SPINNING)
    M64_ANIM_NAME(MARIO_ANIM_BACKWARD_SPINNING)
    M64_ANIM_NAME(MARIO_ANIM_RUNNING)
    M64_ANIM_NAME(MARIO_ANIM_SOFT_BACK_KB)
    M64_ANIM_NAME(MARIO_ANIM_SOFT_FRONT_KB)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_IN_QUICKSAND)
    M64_ANIM_NAME(MARIO_ANIM_MOVE_IN_QUICKSAND)
    M64_ANIM_NAME(MARIO_ANIM_BACKWARD_KB)
    M64_ANIM_NAME(MARIO_ANIM_FORWARD_KB)
    M64_ANIM_NAME(MARIO_ANIM_STAND_AGAINST_WALL)
    M64_ANIM_NAME(MARIO_ANIM_WALL_KICK_AIR)
    M64_ANIM_NAME(MARIO_ANIM_A_POSE)
    M64_ANIM_NAME(MARIO_ANIM_BACKWARD_AIR_KB)
    M64_ANIM_NAME(MARIO_ANIM_AIR_FORWARD_KB)
    M64_ANIM_NAME(MARIO_ANIM_CLIMB_DOWN_LEDGE)
    M64_ANIM_NAME(MARIO_ANIM_IDLE_ON_LEDGE)
    M64_ANIM_NAME(MARIO_ANIM_FAST_LEDGE_GRAB)
    M64_ANIM_NAME(MARIO_ANIM_COUGHING)
    M64_ANIM_NAME(MARIO_ANIM_HANG_ON_CEILING)
    M64_ANIM_NAME(MARIO_ANIM_HANDSTAND_IDLE)
    M64_ANIM_NAME(MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT)
    M64_ANIM_NAME(MARIO_ANIM_WATER_IDLE)
#undef M64_ANIM_NAME
};

s32 m64_anim_id_from_name(const char *name) {
    s32 i;

    for (i = 0; i < MARIO_ANIM_COUNT; i++) {
        if (sAnimNames[i] != NULL && strcmp(sAnimNames[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

s32 m64_anim_load_table(const char *path) {
    FILE *f = fopen(path, "r");
    char line[256];
    s32 applied = 0;

    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char name[128];
        s32 frames;
        char *hash = strchr(line, '#');

        if (hash != NULL) {
            *hash = '\0';
        }
        if (sscanf(line, "%127s %d", name, &frames) == 2) {
            s32 id = m64_anim_id_from_name(name);

            if (id >= 0 && frames > 0) {
                m64_anim_set_length(id, (s16) frames);
                applied++;
            }
        }
    }
    fclose(f);

    /* Rebuild the pool so the new lengths take effect. */
    m64_anim_init();
    return applied;
}

s16 set_mario_animation(struct MarioState *m, s32 targetAnimID) {
    struct Object *o = m->marioObj;

    if (targetAnimID < 0 || targetAnimID >= MARIO_ANIM_COUNT) {
        return 0;
    }

    if (o->header.gfx.animInfo.animID != targetAnimID) {
        o->header.gfx.animInfo.animID = targetAnimID;
        o->header.gfx.animInfo.curAnim = &sAnimPool[targetAnimID];
        o->header.gfx.animInfo.animAccel = 0x10000;
        o->header.gfx.animInfo.animFrame = 0;
        o->header.gfx.animInfo.animFrameAccelAssist = 0;
    }
    return o->header.gfx.animInfo.animFrame;
}

s16 set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel) {
    struct Object *o = m->marioObj;

    set_mario_animation(m, targetAnimID);
    /* accel is 16.16 fixed point: 0x10000 is one animation frame per tick.
     * Run animations scale this by speed, which is why a sprint looks faster
     * rather than just covering ground faster. */
    o->header.gfx.animInfo.animAccel = accel;
    return o->header.gfx.animInfo.animFrame;
}

void set_anim_to_frame(struct MarioState *m, s16 animFrame) {
    m->marioObj->header.gfx.animInfo.animFrame = animFrame;
    m->marioObj->header.gfx.animInfo.animFrameAccelAssist = animFrame << 16;
}

/* Advance the current animation one tick. Called once per frame by the driver. */
static void advance_mario_animation(struct MarioState *m) {
    struct AnimInfo *info = &m->marioObj->header.gfx.animInfo;
    s32 accel = info->animAccel ? info->animAccel : 0x10000;

    if (info->curAnim == NULL) {
        return;
    }
    info->animFrameAccelAssist += accel;
    info->animFrame = (s16) (info->animFrameAccelAssist >> 16);

    if (info->curAnim->loopEnd > 0 && info->animFrame >= info->curAnim->loopEnd) {
        info->animFrame = info->curAnim->loopStart;
        info->animFrameAccelAssist = info->animFrame << 16;
    }
    info->animTimer++;
}

s32 is_anim_at_end(struct MarioState *m) {
    struct AnimInfo *info = &m->marioObj->header.gfx.animInfo;

    if (info->curAnim == NULL) {
        return TRUE;
    }
    return (info->animFrame + 1) >= info->curAnim->loopEnd;
}

s32 is_anim_past_end(struct MarioState *m) {
    struct AnimInfo *info = &m->marioObj->header.gfx.animInfo;

    if (info->curAnim == NULL) {
        return TRUE;
    }
    return info->animFrame >= (info->curAnim->loopEnd - 2);
}

s32 is_anim_past_frame(struct MarioState *m, s16 animFrame) {
    return m->marioObj->header.gfx.animInfo.animFrame >= animFrame;
}

/* No animation data means no baked root motion. */
s16 return_mario_anim_y_translation(struct MarioState *m) {
    (void) m;
    return 0;
}

void update_mario_pos_for_anim(struct MarioState *m) {
    (void) m;
}

/* --- Sound -------------------------------------------------------------- */

/*
 * The two latch flags exist so that an action entered on one frame and held for
 * many does not retrigger its sound every frame.  set_mario_action() clears
 * them, which is what makes "once per action entry" work.
 */
void play_sound_if_no_flag(struct MarioState *m, u32 soundBits, u32 flags) {
    if (!(m->flags & flags)) {
        play_sound(soundBits, m->marioObj->header.gfx.cameraToObject);
        m->flags |= flags;
    }
}

void play_mario_sound(struct MarioState *m, s32 primarySoundBits, s32 secondarySoundBits) {
    if (primarySoundBits == SOUND_ACTION_TERRAIN_JUMP) {
        play_mario_action_sound(
            m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_JUMP
                                            : SOUND_ACTION_TERRAIN_JUMP,
            1);
    } else {
        play_sound_if_no_flag(m, primarySoundBits, MARIO_ACTION_SOUND_PLAYED);
    }
    if (secondarySoundBits != 0) {
        play_sound_if_no_flag(m, (u32) secondarySoundBits, MARIO_MARIO_SOUND_PLAYED);
    }
}

void play_mario_jump_sound(struct MarioState *m) {
    if (!(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
        if (m->action == ACT_TRIPLE_JUMP) {
            play_sound(SOUND_MARIO_YAHOO_WAHA_YIPPEE,
                       m->marioObj->header.gfx.cameraToObject);
        } else {
            play_sound(SOUND_MARIO_YAH_WAH_HOO, m->marioObj->header.gfx.cameraToObject);
        }
        m->flags |= MARIO_MARIO_SOUND_PLAYED;
    }
}

void adjust_sound_for_speed(struct MarioState *m) {
    s32 absForwardVel = (m->forwardVel > 0.0f) ? (s32) m->forwardVel : (s32) -m->forwardVel;

    set_sound_moving_speed(0, (absForwardVel > 100) ? 100 : (u8) absForwardVel);
}

void play_sound_and_spawn_particles(struct MarioState *m, u32 soundBits,
                                    u32 waveParticleType) {
    /* Particles are recorded as flags for a future renderer to consume. */
    if (waveParticleType != 0) {
        m->particleFlags |= PARTICLE_DUST;
    }
    play_sound(soundBits + m->terrainSoundAddend, m->marioObj->header.gfx.cameraToObject);
}

void play_mario_action_sound(struct MarioState *m, u32 soundBits, u32 waveParticleType) {
    if (!(m->flags & MARIO_ACTION_SOUND_PLAYED)) {
        play_sound_and_spawn_particles(m, soundBits, waveParticleType);
        m->flags |= MARIO_ACTION_SOUND_PLAYED;
    }
}

void play_mario_landing_sound(struct MarioState *m, u32 soundBits) {
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits, 1);
}

void play_mario_landing_sound_once(struct MarioState *m, u32 soundBits) {
    play_mario_action_sound(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits, 1);
}

void play_mario_heavy_landing_sound(struct MarioState *m, u32 soundBits) {
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

void play_mario_heavy_landing_sound_once(struct MarioState *m, u32 soundBits) {
    play_mario_action_sound(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

/* --- Ground queries ----------------------------------------------------- */

/*
 * Slipperiness class of the floor under the player.
 *
 * Two things beyond the surface type feed in.  A level may declare its whole
 * terrain to be slide terrain, which makes everything very slippery by default.
 * And *crawling* promotes an ordinary floor to not-slippery, which is why you
 * can crawl up a slope that you would slide back down while walking.  That is a
 * real mechanic, not a quirk.
 */
s32 mario_get_floor_class(struct MarioState *m) {
    s32 floorClass;

    if (m->area != NULL && (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE) {
        floorClass = SURFACE_CLASS_VERY_SLIPPERY;
    } else {
        floorClass = SURFACE_CLASS_DEFAULT;
    }

    if (m->floor != NULL) {
        s32 typeClass = m64_surface_class(m->floor);

        if (typeClass != SURFACE_CLASS_DEFAULT) {
            floorClass = typeClass;
        }
    }

    if (m->action == ACT_CRAWLING && m->floor != NULL && m->floor->normal.y > 0.5f
        && floorClass == SURFACE_CLASS_DEFAULT) {
        floorClass = SURFACE_CLASS_NOT_SLIPPERY;
    }
    return floorClass;
}

u32 mario_get_terrain_sound_addend(struct MarioState *m) {
    s32 terrainType = (m->area != NULL) ? (m->area->terrainType & TERRAIN_MASK) : TERRAIN_GRASS;

    /* The addend selects a footstep variant. With audio stubbed it only shows
     * up in the trace, but keeping it derived from terrain means a real audio
     * back-end needs no changes here. */
    if (m->floor != NULL && m->floor->type == SURFACE_SLOW) {
        return (u32) TERRAIN_SAND;
    }
    return (u32) terrainType;
}

/*
 * Is the player's facing within 90 degrees of downhill?
 *
 * turnYaw is passed FALSE by every caller in practice; when TRUE it accounts for
 * moving backwards. The comparison is a plain s16 subtraction, which wraps, so
 * the window is genuinely +/-90 degrees with no special cases at the seam.
 */
s32 mario_facing_downhill(struct MarioState *m, s32 turnYaw) {
    s16 faceAngleYaw = m->faceAngle[1];

    if (turnYaw && m->forwardVel < 0.0f) {
        faceAngleYaw += 0x8000;
    }
    faceAngleYaw = m->floorAngle - faceAngleYaw;
    return (faceAngleYaw > -0x4000 && faceAngleYaw < 0x4000);
}

/*
 * The three slope predicates.
 *
 * All three are the same shape -- compare the floor normal's Y against a
 * threshold picked by floor class -- but they answer different questions, and
 * the thresholds are what give each surface class its character:
 *
 *   is_slippery : will I slide on this at all?        (default ~38 deg)
 *   is_slope    : does gravity accelerate me here?    (default ~15 deg)
 *   is_steep    : is this too steep to stand on?      (default ~30 deg)
 *
 * Note is_slope triggers well before is_steep: there is a band where you keep
 * full control but gravity is already pulling you downhill.  That band is what
 * makes SM64's slopes feel like slopes rather than like ramps.
 */
u32 mario_floor_is_slippery(struct MarioState *m) {
    f32 normY;

    if (m->floor == NULL) {
        return FALSE;
    }
    if (m->area != NULL && (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f) { /* ~1 degree */
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_CLASS_VERY_SLIPPERY: normY = 0.9848077f; break; /* ~10 deg */
        case SURFACE_CLASS_SLIPPERY:      normY = 0.9396926f; break; /* ~20 deg */
        case SURFACE_CLASS_NOT_SLIPPERY:  normY = 0.0f;       break; /* never */
        default:                          normY = 0.7880108f; break; /* ~38 deg */
    }
    return m->floor->normal.y <= normY;
}

s32 mario_floor_is_slope(struct MarioState *m) {
    f32 normY;

    if (m->floor == NULL) {
        return FALSE;
    }
    if (m->area != NULL && (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f) {
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_CLASS_VERY_SLIPPERY: normY = 0.9961947f; break; /* ~5 deg */
        case SURFACE_CLASS_SLIPPERY:      normY = 0.9848077f; break; /* ~10 deg */
        case SURFACE_CLASS_NOT_SLIPPERY:  normY = 0.9396926f; break; /* ~20 deg */
        default:                          normY = 0.9659258f; break; /* ~15 deg */
    }
    return m->floor->normal.y <= normY;
}

s32 mario_floor_is_steep(struct MarioState *m) {
    f32 normY;
    s32 result = FALSE;

    if (m->floor == NULL) {
        return FALSE;
    }

    /*
     * Facing downhill exempts you entirely: a slope is never "too steep" in the
     * direction you are already sliding.  This is what lets you run down a
     * steep face under control but not stand still on it.
     */
    if (!mario_facing_downhill(m, FALSE)) {
        switch (mario_get_floor_class(m)) {
            case SURFACE_CLASS_VERY_SLIPPERY: normY = 0.9659258f; break; /* ~15 deg */
            case SURFACE_CLASS_SLIPPERY:      normY = 0.9396926f; break; /* ~20 deg */
            default:                          normY = 0.8660254f; break; /* ~30 deg */
        }
        result = m->floor->normal.y <= normY;
    }
    return result;
}

f32 find_floor_height_relative_polar(struct MarioState *m, s16 angleFromMario,
                                     f32 distFromMario) {
    struct Surface *floor;
    f32 x = m->pos[0] + distFromMario * sins(m->faceAngle[1] + angleFromMario);
    f32 z = m->pos[2] + distFromMario * coss(m->faceAngle[1] + angleFromMario);

    return find_floor(x, m->pos[1] + 160.0f, z, &floor);
}

/*
 * Pitch of the floor along a given direction, sampled 5 units either side.
 * Whichever side deviates less from the current height wins, which biases the
 * answer toward the surface actually underfoot when straddling an edge.
 */
s16 find_floor_slope(struct MarioState *m, s16 yawOffset) {
    struct Surface *floor;
    f32 forwardFloorY, backwardFloorY;
    f32 forwardYDelta, backwardYDelta;
    s16 result;
    f32 x = sins(m->faceAngle[1] + yawOffset) * 5.0f;
    f32 z = coss(m->faceAngle[1] + yawOffset) * 5.0f;

    forwardFloorY = find_floor(m->pos[0] + x, m->pos[1] + 100.0f, m->pos[2] + z, &floor);
    backwardFloorY = find_floor(m->pos[0] - x, m->pos[1] + 100.0f, m->pos[2] - z, &floor);

    forwardYDelta = forwardFloorY - m->pos[1];
    backwardYDelta = m->pos[1] - backwardFloorY;

    if (forwardYDelta * forwardYDelta < backwardYDelta * backwardYDelta) {
        result = atan2s(5.0f, forwardYDelta);
    } else {
        result = atan2s(5.0f, backwardYDelta);
    }
    return result;
}

/* --- Velocity ----------------------------------------------------------- */

void mario_set_forward_vel(struct MarioState *m, f32 forwardVel) {
    m->forwardVel = forwardVel;
    m->slideVelX = sins(m->faceAngle[1]) * m->forwardVel;
    m->slideVelZ = coss(m->faceAngle[1]) * m->forwardVel;
    m->vel[0] = m->slideVelX;
    m->vel[2] = m->slideVelZ;
}

/* --- Action entry ------------------------------------------------------- */

void update_mario_sound_and_camera(struct MarioState *m) {
    u32 action = m->action;
    s32 camPreset = (m->area != NULL && m->area->camera != NULL) ? m->area->camera->mode : 0;

    if (action == ACT_FIRST_PERSON) {
        /* Leaving first person stops the moving-speed sound. */
        set_sound_moving_speed(0, 0);
    } else if (action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE) {
        set_sound_moving_speed(0, 0);
    }
    (void) camPreset;
}

/*
 * Airborne entry: this is where every jump's launch velocity is defined.
 *
 * set_mario_y_vel_based_on_fspeed() adds a fraction of horizontal speed to the
 * vertical launch, so a running jump goes higher than a standing one -- for the
 * jumps that opt into it.  The multipliers are as important as the base values:
 *
 *   single jump   42.0 + 0.25 * forwardVel
 *   double jump   52.0 + 0.25 * forwardVel, then forwardVel is *zeroed*
 *   triple jump   69.0 + 0.00 * forwardVel, then forwardVel *= 0.8
 *   backflip      62.0, forwardVel forced to -16 (backwards)
 *   long jump     30.0, forwardVel *= 1.5 capped at 48 -- low and far
 *   side flip     62.0 (shares the backflip launch)
 *   slide kick    vel[1] = 12.0, forwardVel floored at 32
 *   jump kick     vel[1] = 20.0
 *
 * The double jump zeroing its forward speed is why a double jump goes nearly
 * straight up, and the long jump's halved gravity (see apply_gravity) is why 30
 * is enough to clear a long gap.
 */
static void set_mario_y_vel_based_on_fspeed(struct MarioState *m, f32 initialVelY,
                                            f32 multiplier) {
    m->vel[1] = initialVelY + get_additive_y_vel_for_jumps() + m->forwardVel * multiplier;

    /* Being squished or waist-deep in quicksand halves every jump. */
    if (m->squishTimer != 0 || m->quicksandDepth > 1.0f) {
        m->vel[1] *= 0.5f;
    }
}

static u32 set_mario_action_airborne(struct MarioState *m, u32 action, u32 actionArg) {
    f32 forwardVel;

    if (m->squishTimer != 0 || m->quicksandDepth >= 1.0f) {
        /* No fancy jumps while compressed. */
        if (action == ACT_DOUBLE_JUMP || action == ACT_TWIRLING) {
            action = ACT_JUMP;
        }
    }

    switch (action) {
        case ACT_DOUBLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 52.0f, 0.25f);
            /*
             * Sheds a fifth of forward speed -- it does NOT stop you.  A double
             * jump is the middle of a chain performed at a run, so zeroing
             * speed here would bring a running player to a dead halt in mid-air
             * and make the chain unusable for covering ground.
             */
            m->forwardVel *= 0.8f;
            break;

        case ACT_BACKFLIP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            m->forwardVel = -16.0f;
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            break;

        /*
         * A side flip shares the backflip's launch height.  Without a case here
         * it fell through with whatever vertical velocity it already had --
         * i.e. zero -- so pressing A during a turnaround produced no jump at
         * all, which is what made the move look missing rather than wrong.
         *
         * Forward speed is left alone: a side flip is entered mid-turn, when
         * speed has already been bled off by the turnaround.
         */
        case ACT_SIDE_FLIP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            break;

        case ACT_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 69.0f, 0.0f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_FLYING_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 82.0f, 0.0f);
            break;

        case ACT_WATER_JUMP:
        case ACT_HOLD_WATER_JUMP:
            if (actionArg == 0) {
                set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.0f);
            }
            break;

        case ACT_BURNING_JUMP:
            m->vel[1] = 31.5f;
            m->forwardVel = 8.0f;
            break;

        case ACT_RIDING_SHELL_JUMP:
        case ACT_JUMP:
        case ACT_HOLD_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            break;

        case ACT_STEEP_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            m->faceAngle[0] = -0x2000;
            break;

        case ACT_LAVA_BOOST:
            m->vel[1] = 84.0f;
            if (actionArg == 0) {
                m->forwardVel = 0.0f;
            }
            break;

        case ACT_DIVE:
            /* A dive converts standing still into real speed: +15, capped. */
            forwardVel = m->forwardVel + 15.0f;
            if (forwardVel > 48.0f) {
                forwardVel = 48.0f;
            }
            mario_set_forward_vel(m, forwardVel);
            break;

        case ACT_LONG_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 30.0f, 0.0f);
            /* Remembered so the animation can differ for a slow long jump. */
            m->marioObj->oMarioLongJumpIsSlow = m->forwardVel > 16.0f ? FALSE : TRUE;

            /*
             * The 1.5x multiplier is applied to whatever forward speed you
             * arrive with.  Because it multiplies rather than sets, speed
             * gained by other means carries in -- this is the hook that
             * backwards-long-jump speed accumulation hangs off in the original.
             * It is preserved rather than clamped symmetrically.
             */
            if ((m->forwardVel *= 1.5f) > 48.0f) {
                m->forwardVel = 48.0f;
            }
            break;

        case ACT_SLIDE_KICK:
            m->vel[1] = 12.0f;
            if (m->forwardVel < 32.0f) {
                m->forwardVel = 32.0f;
            }
            break;

        case ACT_JUMP_KICK:
            m->vel[1] = 20.0f;
            break;

        default:
            break;
    }

    m->peakHeight = m->pos[1];
    return action;
}

/*
 * Ground entry.  Two jobs: give walking a minimum speed so that tapping the
 * stick produces visible movement, and resolve the "begin sliding" request into
 * a butt slide or a stomach slide depending on which way you face.
 */
static u32 set_mario_action_moving(struct MarioState *m, u32 action, u32 actionArg) {
    s16 floorClass = mario_get_floor_class(m);
    f32 forwardVel = m->forwardVel;
    f32 mag = m->intendedMag < 8.0f ? m->intendedMag : 8.0f;

    (void) actionArg;

    switch (action) {
        case ACT_WALKING:
            /* Skipped on ice, where you keep whatever speed you had. */
            if (floorClass != SURFACE_CLASS_VERY_SLIPPERY) {
                if (0.0f <= forwardVel && forwardVel < mag) {
                    m->forwardVel = mag;
                }
            }
            m->marioObj->oMarioWalkingPitch = 0;
            break;

        case ACT_HOLD_WALKING:
            if (0.0f <= forwardVel && forwardVel < mag / 2.0f) {
                m->forwardVel = mag / 2.0f;
            }
            break;

        case ACT_BEGIN_SLIDING:
            action = mario_facing_downhill(m, FALSE) ? ACT_BUTT_SLIDE : ACT_STOMACH_SLIDE;
            break;

        case ACT_HOLD_BEGIN_SLIDING:
            action = mario_facing_downhill(m, FALSE) ? ACT_HOLD_BUTT_SLIDE
                                                    : ACT_HOLD_STOMACH_SLIDE;
            break;

        default:
            break;
    }
    return action;
}

static u32 set_mario_action_submerged(struct MarioState *m, u32 action, u32 actionArg) {
    (void) actionArg;
    if (action == ACT_METAL_WATER_JUMP || action == ACT_HOLD_METAL_WATER_JUMP) {
        m->vel[1] = 32.0f;
    }
    return action;
}

u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    switch (action & ACT_GROUP_MASK) {
        case ACT_GROUP_MOVING:
            action = set_mario_action_moving(m, action, actionArg);
            break;
        case ACT_GROUP_AIRBORNE:
            action = set_mario_action_airborne(m, action, actionArg);
            break;
        case ACT_GROUP_SUBMERGED:
            action = set_mario_action_submerged(m, action, actionArg);
            break;
        default:
            break;
    }

    /* Re-arm the per-action sound latches so the new action can speak. */
    m->flags &= ~(MARIO_ACTION_SOUND_PLAYED | MARIO_MARIO_SOUND_PLAYED);

    if (!(m->action & ACT_FLAG_AIR)) {
        m->flags &= ~MARIO_UNKNOWN_18;
    }

    m->prevAction = m->action;
    m->action = action;
    m->actionArg = actionArg;
    m->actionState = 0;
    m->actionTimer = 0;

    return TRUE;
}

/*
 * A jump off a steep slope keeps only three quarters of the speed component
 * along the slope, and redirects facing to the resulting vector. The asymmetry
 * (0.75 across, full along) is what makes a steep jump veer downhill.
 */
void set_steep_jump_action(struct MarioState *m) {
    m->marioObj->header.gfx.animInfo.animID = -1;

    if (m->forwardVel > 0.0f) {
        s16 angleTemp = m->floorAngle + 0x8000;
        s16 faceAngleTemp = m->faceAngle[1] - angleTemp;

        f32 y = sins(faceAngleTemp) * m->forwardVel;
        f32 x = coss(faceAngleTemp) * m->forwardVel * 0.75f;

        m->forwardVel = sqrtf(y * y + x * x);
        m->faceAngle[1] = atan2s(x, y) + angleTemp;
    }
    set_mario_action(m, ACT_STEEP_JUMP, 0);
}

s32 set_jumping_action(struct MarioState *m, u32 action, u32 actionArg) {
    if (m->quicksandDepth >= 11.0f) {
        return set_mario_action(m, m->heldObj == NULL ? ACT_QUICKSAND_JUMP_LAND
                                                     : ACT_HOLD_QUICKSAND_JUMP_LAND,
                                0);
    }
    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else {
        set_mario_action(m, action, actionArg);
    }
    return TRUE;
}

/*
 * The jump-chain escalation, driven off what you were doing before you landed.
 * The landing-action tables in mario_actions_moving.c are the primary route
 * into the chain; this is the fallback path used from the stationary land-stop
 * actions.
 */
s32 set_jump_from_landing(struct MarioState *m) {
    if (m->quicksandDepth >= 11.0f) {
        set_mario_action(m, m->heldObj == NULL ? ACT_QUICKSAND_JUMP_LAND
                                              : ACT_HOLD_QUICKSAND_JUMP_LAND,
                         0);
        m->doubleJumpTimer = 0;
        return TRUE;
    }

    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else if (m->doubleJumpTimer == 0 || m->squishTimer != 0) {
        /* Window expired: back to a plain jump. */
        set_mario_action(m, ACT_JUMP, 0);
    } else {
        switch (m->prevAction) {
            case ACT_JUMP_LAND:
            case ACT_FREEFALL_LAND:
            case ACT_SIDE_FLIP_LAND_STOP:
                set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                break;
            case ACT_DOUBLE_JUMP_LAND:
                set_mario_action(m, ACT_TRIPLE_JUMP, 0);
                break;
            case ACT_TRIPLE_JUMP_LAND:
                set_mario_action(m, ACT_JUMP, 0);
                break;
            case ACT_BACKFLIP_LAND:
                set_mario_action(m, ACT_BACKFLIP, 0);
                break;
            case ACT_LONG_JUMP_LAND:
                set_mario_action(m, ACT_LONG_JUMP, 0);
                break;
            default:
                set_mario_action(m, ACT_JUMP, 0);
                break;
        }
    }

    m->doubleJumpTimer = 0;
    return TRUE;
}

s32 drop_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    mario_stop_riding_and_holding(m);
    return set_mario_action(m, action, actionArg);
}

s32 hurt_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg,
                              s16 hurtCounter) {
    m->hurtCounter = (u8) hurtCounter;
    return set_mario_action(m, action, actionArg);
}

/*
 * The exits every grounded, controllable action shares.  Ordering is the
 * priority order: being stomped beats jumping, which beats punching, which
 * beats falling off a ledge.
 */
s32 check_common_action_exits(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
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

s32 check_common_hold_action_exits(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_HOLD_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_HOLD_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_HOLD_BEGIN_SLIDING, 0);
    }
    return FALSE;
}

s32 transition_submerged_to_walking(struct MarioState *m) {
    set_camera_mode(m->area != NULL ? m->area->camera : NULL, m->area != NULL ? 1 : 0, 1);
    m->faceAngle[0] = 0;
    if (m->heldObj == NULL) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    return set_mario_action(m, ACT_HOLD_WALKING, 0);
}

s32 set_water_plunge_action(struct MarioState *m) {
    m->forwardVel = m->forwardVel / 4.0f;
    m->vel[1] = m->vel[1] / 2.0f;
    m->pos[1] = m->waterLevel - 100;
    m->faceAngle[2] = 0;
    vec3s_set(m->angleVel, 0, 0, 0);
    return set_mario_action(m, ACT_WATER_PLUNGE, 0);
}

/* --- Per-frame input digest --------------------------------------------- */

void mario_reset_bodystate(struct MarioState *m) {
    struct MarioBodyState *bodyState = m->marioBodyState;

    bodyState->capState = 0;
    bodyState->eyeState = 0;
    bodyState->handState = 0;
    bodyState->modelState = 0;
    bodyState->wingFlutter = FALSE;
    m->flags &= ~MARIO_METAL_SHOCK;
}

static void update_mario_button_inputs(struct MarioState *m) {
    if (m->controller->buttonPressed & A_BUTTON) {
        m->input |= INPUT_A_PRESSED;
    }
    if (m->controller->buttonDown & A_BUTTON) {
        m->input |= INPUT_A_DOWN;
    }

    /* Saturating counters: action code asks "was A pressed recently" to allow
     * buffered inputs, e.g. queueing a jump just before landing. */
    if (m->framesSinceA < 0xFF) {
        m->framesSinceA++;
    }
    if (m->framesSinceB < 0xFF) {
        m->framesSinceB++;
    }

    if (m->controller->buttonPressed & B_BUTTON) {
        m->input |= INPUT_B_PRESSED;
        m->framesSinceB = 0;
    }
    if (m->controller->buttonDown & Z_TRIG) {
        m->input |= INPUT_Z_DOWN;
    }
    if (m->controller->buttonPressed & Z_TRIG) {
        m->input |= INPUT_Z_PRESSED;
    }
    if (m->input & INPUT_A_PRESSED) {
        m->framesSinceA = 0;
    }

    if (m->wallKickTimer > 0) {
        m->wallKickTimer--;
    }
    if (m->doubleJumpTimer > 0) {
        m->doubleJumpTimer--;
    }
}

/*
 * Analog stick -> intended direction and magnitude.
 *
 * Two things here are easy to get wrong and both are felt immediately:
 *
 *   * The response is *quadratic*: mag = (stickMag/64)^2 * 64, halved.  Small
 *     deflections therefore produce very small speeds, which is what makes
 *     tiptoeing and fine positioning possible on an analog stick.  Max is 32.
 *   * The direction is rotated by the camera's yaw.  Stick-up means "away from
 *     the camera", not "world +Z", so camera yaw is an input to the physics.
 */
static void update_mario_joystick_inputs(struct MarioState *m) {
    struct Controller *controller = m->controller;
    f32 mag = ((controller->stickMag / 64.0f) * (controller->stickMag / 64.0f)) * 64.0f;

    if (m->squishTimer == 0) {
        m->intendedMag = mag / 2.0f;
    } else {
        m->intendedMag = mag / 8.0f;
    }

    if (m->intendedMag > 0.0f) {
        s16 camYaw = (m->area != NULL && m->area->camera != NULL) ? m->area->camera->yaw : 0;

        /*
         * Stick direction -> world direction, relative to the camera.
         *
         * camera->yaw points from the player TOWARD the camera, so the
         * away-from-camera direction -- what "stick up" means -- is camYaw plus
         * half a turn. atan2s(stickY, stickX) then measures the stick's own
         * angle with up as 0 and right as a quarter turn, and adding the two
         * lands on the world direction.
         *
         * The sign of the stickX term is the whole ball game here: negating it
         * mirrors the horizontal axis, so pushing right walks the player left.
         * That reads as the controls being broken rather than as a bug in one
         * formula, which is why the four cardinal directions are asserted in
         * the test suite rather than left to inspection.
         */
        m->intendedYaw = (s16) (camYaw + 0x8000
                                + atan2s(controller->stickY, controller->stickX));
        m->input |= INPUT_NONZERO_ANALOG;
    } else {
        /* No stick: intend to keep facing where we already face, so that code
         * which unconditionally eases toward intendedYaw becomes a no-op. */
        m->intendedYaw = m->faceAngle[1];
    }
}

static void update_mario_geometry_inputs(struct MarioState *m) {
    f32 gasLevel;
    f32 ceilToFloorDist;

    m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);

    /*
     * If the collision position has left the world, fall back to the graphics
     * position -- which the step code does not update when it refuses a move --
     * and re-query there.  This is the recovery path that keeps a near-OOB
     * player from losing their floor entirely.
     */
    if (m->floor == NULL) {
        vec3f_copy(m->pos, m->marioObj->header.gfx.pos);
        m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);
    }

    m->ceilHeight = vec3f_find_ceil(&m->pos[0], m->floorHeight, &m->ceil);
    gasLevel = find_poison_gas_level(m->pos[0], m->pos[2]);
    m->waterLevel = (s16) find_water_level(m->pos[0], m->pos[2]);

    if (m->floor != NULL) {
        /* floorAngle is the *downhill* yaw of the floor. Comparing it against
         * faceAngle is how every slope decision is phrased. */
        m->floorAngle = atan2s(m->floor->normal.z, m->floor->normal.x);
        m->terrainSoundAddend = mario_get_terrain_sound_addend(m);

        if ((m->pos[1] > m->waterLevel - 40) && mario_floor_is_slippery(m)) {
            m->input |= INPUT_ABOVE_SLIDE;
        }

        /* Being pinched between a moving surface and another one. */
        if ((m->floor->flags & SURFACE_FLAG_DYNAMIC)
            || (m->ceil != NULL && (m->ceil->flags & SURFACE_FLAG_DYNAMIC))) {
            ceilToFloorDist = m->ceilHeight - m->floorHeight;
            if (0.0f <= ceilToFloorDist && ceilToFloorDist <= 150.0f) {
                m->input |= INPUT_SQUISHED;
            }
        }

        /* 100 units of slack before you are considered airborne: this is what
         * lets you run over small dips without leaving the ground. */
        if (m->pos[1] > m->floorHeight + 100.0f) {
            m->input |= INPUT_OFF_FLOOR;
        }
        if (m->pos[1] < (m->waterLevel - 10)) {
            m->input |= INPUT_IN_WATER;
        }
        if (m->pos[1] < (gasLevel - 100.0f)) {
            m->input |= INPUT_IN_POISON_GAS;
        }
    }
}

void update_mario_inputs(struct MarioState *m) {
    m->particleFlags = 0;
    m->input = 0;
    m->collidedObjInteractTypes = m->marioObj->collidedObjInteractTypes;

    /* Keep the low 24 bits (persistent state), drop the volatile high bits. */
    m->flags &= 0xFFFFFF;

    update_mario_button_inputs(m);
    update_mario_joystick_inputs(m);
    update_mario_geometry_inputs(m);

    if (!(m->input & (INPUT_NONZERO_ANALOG | INPUT_A_PRESSED))) {
        m->input |= INPUT_UNKNOWN_5;
    }
    if (m->squishTimer != 0 || m->quicksandDepth > 30.0f) {
        m->input |= INPUT_SQUISHED;
    }
    if (m->pos[1] < m->waterLevel - 40) {
        m->input |= INPUT_IN_WATER;
    }
}

/* --- Frame driver ------------------------------------------------------- */

void mario_handle_special_floors(struct MarioState *m) {
    if (m->floor == NULL) {
        return;
    }
    switch (m->floor->type) {
        case SURFACE_DEATH_PLANE:
            /* The app handles respawn; see app/demo_main.c. */
            break;
        default:
            break;
    }
}

static void sink_mario_in_quicksand(struct MarioState *m) {
    struct Object *o = m->marioObj;

    if (o->header.gfx.throwMatrix == NULL) {
        o->header.gfx.pos[1] -= m->quicksandDepth;
    }
}

s32 execute_mario_action(struct Object *o) {
    s32 inLoop = TRUE;
    s32 guard = 0;

    (void) o;

    if (gMarioState->action == 0) {
        return 0;
    }

    gMarioState->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
    mario_reset_bodystate(gMarioState);
    update_mario_inputs(gMarioState);
    mario_handle_special_floors(gMarioState);
    mario_process_interactions(gMarioState);

    /* No floor at all means no action can run meaningfully this frame. */
    if (gMarioState->floor == NULL) {
        return 0;
    }

    /*
     * Re-dispatch while actions keep transitioning.  The guard is a new64
     * addition: a mis-specified action pair can ping-pong forever, and on real
     * hardware that is a hang.  Here it bails out and leaves the state as-is,
     * which is diagnosable instead of fatal.  A correct action set never
     * approaches this bound -- the deepest legitimate chains are a handful of
     * transitions.
     */
    while (inLoop) {
        if (++guard > 64) {
            break;
        }
        switch (gMarioState->action & ACT_GROUP_MASK) {
            case ACT_GROUP_STATIONARY:
                inLoop = mario_execute_stationary_action(gMarioState);
                break;
            case ACT_GROUP_MOVING:
                inLoop = mario_execute_moving_action(gMarioState);
                break;
            case ACT_GROUP_AIRBORNE:
                inLoop = mario_execute_airborne_action(gMarioState);
                break;
            case ACT_GROUP_SUBMERGED:
                inLoop = mario_execute_submerged_action(gMarioState);
                break;
            case ACT_GROUP_CUTSCENE:
                inLoop = mario_execute_cutscene_action(gMarioState);
                break;
            case ACT_GROUP_AUTOMATIC:
                inLoop = mario_execute_automatic_action(gMarioState);
                break;
            case ACT_GROUP_OBJECT:
                inLoop = mario_execute_object_action(gMarioState);
                break;
            default:
                inLoop = FALSE;
                break;
        }
    }

    sink_mario_in_quicksand(gMarioState);
    advance_mario_animation(gMarioState);

    if (gMarioState->hurtCounter > 0) {
        gMarioState->hurtCounter--;
    }
    if (gMarioState->healCounter > 0) {
        gMarioState->healCounter--;
    }
    if (gMarioState->invincTimer > 0) {
        gMarioState->invincTimer--;
    }
    if (gMarioState->squishTimer > 0) {
        gMarioState->squishTimer--;
    }

    gMarioState->marioBodyState->action = gMarioState->action;

    return (s32) gMarioState->particleFlags;
}

void init_mario(void) {
    struct MarioState *m = gMarioState;

    m64_anim_init();

    m->actionTimer = 0;
    m->actionState = 0;
    m->actionArg = 0;
    m->invincTimer = 0;
    m->framesSinceA = 0xFF;
    m->framesSinceB = 0xFF;
    m->wallKickTimer = 0;
    m->doubleJumpTimer = 0;
    m->flags = MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD;
    m->forwardVel = 0.0f;
    m->squishTimer = 0;
    m->hurtCounter = 0;
    m->healCounter = 0;
    m->quicksandDepth = 0.0f;
    m->peakHeight = m->pos[1];
    m->faceAngle[0] = 0;
    m->faceAngle[2] = 0;
    vec3s_set(m->angleVel, 0, 0, 0);
    vec3f_set(m->vel, 0.0f, 0.0f, 0.0f);
    m->slideVelX = 0.0f;
    m->slideVelZ = 0.0f;

    m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);
    if (m->pos[1] < m->floorHeight) {
        m->pos[1] = m->floorHeight;
    }
    m->marioObj->header.gfx.pos[1] = m->pos[1];
    m->action = (m->pos[1] <= m->floorHeight) ? ACT_IDLE : ACT_FREEFALL;
    m->marioObj->header.gfx.animInfo.animID = -1;
}

void init_mario_from_save_file(void) {
    init_mario();
}
