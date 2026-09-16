/*
 * new64 -- the player module's public surface.
 *
 * The functions here divide into three kinds:
 *
 *   * state transitions (set_mario_action and friends) -- the only sanctioned
 *     way to change action, because they also reset the per-action scratch
 *     fields that every action assumes start at zero;
 *   * queries about the ground (slope/steepness/class), which are what slope
 *     physics is expressed in terms of;
 *   * animation and sound requests, which new64 records rather than renders.
 */
#ifndef NEW64_MARIO_H
#define NEW64_MARIO_H

#include "types.h"

/* --- Animation ---------------------------------------------------------- */

s32 is_anim_at_end(struct MarioState *m);
s32 is_anim_past_end(struct MarioState *m);
s16 set_mario_animation(struct MarioState *m, s32 targetAnimID);
s16 set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel);
void set_anim_to_frame(struct MarioState *m, s16 animFrame);
s32 is_anim_past_frame(struct MarioState *m, s16 animFrame);
s16 return_mario_anim_y_translation(struct MarioState *m);
void update_mario_pos_for_anim(struct MarioState *m);

/* --- Sound (recorded, not played; see sm64/src/shims/audio_stub.c) ------- */

void play_sound_if_no_flag(struct MarioState *m, u32 soundBits, u32 flags);
void play_mario_jump_sound(struct MarioState *m);
void adjust_sound_for_speed(struct MarioState *m);
void play_sound_and_spawn_particles(struct MarioState *m, u32 soundBits,
                                    u32 waveParticleType);
void play_mario_action_sound(struct MarioState *m, u32 soundBits, u32 waveParticleType);
void play_mario_landing_sound(struct MarioState *m, u32 soundBits);
void play_mario_landing_sound_once(struct MarioState *m, u32 soundBits);
void play_mario_heavy_landing_sound(struct MarioState *m, u32 soundBits);
void play_mario_heavy_landing_sound_once(struct MarioState *m, u32 soundBits);
void play_mario_sound(struct MarioState *m, s32 primarySoundBits, s32 secondarySoundBits);

/* --- Ground queries ----------------------------------------------------- */

/*
 * The slope predicates are class-dependent: the same physical angle counts as a
 * slope on ice but as flat ground on grass.  mario_get_floor_class() is the
 * hinge, and everything slope-related goes through these three rather than
 * comparing normal.y against a literal.
 */
s32 mario_get_floor_class(struct MarioState *m);
u32 mario_get_terrain_sound_addend(struct MarioState *m);
s32 mario_facing_downhill(struct MarioState *m, s32 turnYaw);
u32 mario_floor_is_slippery(struct MarioState *m);
s32 mario_floor_is_slope(struct MarioState *m);
s32 mario_floor_is_steep(struct MarioState *m);
f32 find_floor_height_relative_polar(struct MarioState *m, s16 angleFromMario,
                                     f32 distFromMario);
s16 find_floor_slope(struct MarioState *m, s16 yawOffset);

/* --- Velocity ----------------------------------------------------------- */

/*
 * Sets forwardVel *and* the derived slide velocity together.  Assigning
 * m->forwardVel by hand leaves slideVel stale for a frame, which is a
 * surprisingly common source of one-frame movement bugs.
 */
void mario_set_forward_vel(struct MarioState *m, f32 forwardVel);

/* --- State transitions -------------------------------------------------- */

void update_mario_sound_and_camera(struct MarioState *m);
void set_steep_jump_action(struct MarioState *m);
u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg);
s32 set_jump_from_landing(struct MarioState *m);
s32 set_jumping_action(struct MarioState *m, u32 action, u32 actionArg);
s32 drop_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg);
s32 hurt_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg,
                              s16 hurtCounter);
s32 check_common_action_exits(struct MarioState *m);
s32 check_common_hold_action_exits(struct MarioState *m);
s32 transition_submerged_to_walking(struct MarioState *m);
s32 set_water_plunge_action(struct MarioState *m);

/* --- Frame driver ------------------------------------------------------- */

void mario_reset_bodystate(struct MarioState *m);
void update_mario_inputs(struct MarioState *m);
s32 execute_mario_action(struct Object *o);
void init_mario(void);
void init_mario_from_save_file(void);

/* --- Animation timing table --------------------------------------------- */
/*
 * Animation *length* is physics: several actions end via is_anim_at_end(), so a
 * wrong frame count makes that state last the wrong number of frames.  new64
 * ships no animation data, so the table is separately loadable.
 */
void m64_anim_init(void);
void m64_anim_set_length(s32 animID, s16 frames);

/* Load "NAME FRAMES" pairs from a text file (see assets/mario_anims.txt).
 * Returns the number of entries applied, or -1 if the file could not be read.
 * Call before init_mario(). */
s32 m64_anim_load_table(const char *path);

/* Resolve an animation name to its id, or -1. Exposed for tooling. */
s32 m64_anim_id_from_name(const char *name);

extern struct MarioState *gMarioState;

#endif /* NEW64_MARIO_H */
