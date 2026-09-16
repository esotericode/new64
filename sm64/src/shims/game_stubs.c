/*
 * new64 -- stubs for the subsystems the demo does not have.
 *
 * Every function here is called by real movement code.  They are deliberately
 * *present and inert* rather than #ifdef'd out of the call sites, because the
 * call sites are the part that has to stay unmodified for hosted code to drop
 * in.  Where a stub's return value influences movement, the chosen value and
 * its consequence are noted.
 */
#include "types.h"
#include "sm64.h"
#include "area.h"
#include "camera.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "object_helpers.h"
#include "platform_displacement.h"
#include "rumble_init.h"
#include "save_file.h"
#include "sound_init.h"

#include <stddef.h>

/* --- Globals hosted code expects to exist -------------------------------- */

struct Controller *gPlayer1Controller;
struct Controller *gPlayer2Controller;
struct Controller *gPlayer3Controller;
struct Area *gCurrentArea;
struct Camera *gCamera;
u32 gGlobalTimer;
s16 gCurrCourseNum;
s16 gCurrAreaIndex;
s16 gCurrLevelNum;
s16 gCurrSaveFileNum;
u16 gHudFlash;
s16 gPrevFrameObjectCount;

/* --- Camera ------------------------------------------------------------- */
/*
 * Mode requests are ignored: new64's camera is a single follow camera.  Note
 * that this is *not* a fidelity compromise for movement, because movement only
 * ever reads camera yaw -- it never reads the mode back.
 */
void set_camera_mode(struct Camera *c, s16 mode, s16 frames) {
    (void) c; (void) mode; (void) frames;
}
void set_camera_shake_from_hit(s16 shake) { (void) shake; }
void set_camera_shake_from_point(s16 shake, f32 posX, f32 posY, f32 posZ) {
    (void) shake; (void) posX; (void) posY; (void) posZ;
}
void set_fov_shake_from_point_preset(u8 preset, f32 posX, f32 posY, f32 posZ) {
    (void) preset; (void) posX; (void) posY; (void) posZ;
}
void set_camera_pitch_shake(s16 mag, s16 decay, s16 inc) { (void) mag; (void) decay; (void) inc; }
void set_camera_yaw_shake(s16 mag, s16 decay, s16 inc) { (void) mag; (void) decay; (void) inc; }
void set_camera_roll_shake(s16 mag, s16 decay, s16 inc) { (void) mag; (void) decay; (void) inc; }
s32 set_cam_angle(s32 mode) { return mode; }
void reset_camera(struct Camera *c) { (void) c; }

/* --- Rumble ------------------------------------------------------------- */

void queue_rumble_data(s16 a0, s16 a1) { (void) a0; (void) a1; }
void queue_rumble_data_mario(struct MarioState *m, s16 a0, s16 a1) {
    (void) m; (void) a0; (void) a1;
}
void reset_rumble_timers(void) {}
void queue_rumble_submerged(void) {}

/* --- Save data ---------------------------------------------------------- */
/*
 * Reports an empty file.  The one movement-visible consequence: no cap powerups
 * are owned, so wing-cap gravity and metal-cap water handling never engage.
 */
u32 save_file_get_flags(void) { return 0; }
void save_file_set_flags(u32 flags) { (void) flags; }
void save_file_clear_flags(u32 flags) { (void) flags; }
s32 save_file_get_total_star_count(s32 fileIndex, s32 minCourse, s32 maxCourse) {
    (void) fileIndex; (void) minCourse; (void) maxCourse;
    return 0;
}
s32 save_file_get_course_star_count(s32 fileIndex, s32 courseIndex) {
    (void) fileIndex; (void) courseIndex;
    return 0;
}
u32 save_file_get_star_flags(s32 fileIndex, s32 courseIndex) {
    (void) fileIndex; (void) courseIndex;
    return 0;
}

/* --- Music -------------------------------------------------------------- */

void play_infinite_stairs_music(void) {}
void set_background_music(u16 a, u16 seqArgs, s16 fadeTimer) {
    (void) a; (void) seqArgs; (void) fadeTimer;
}
void fadeout_music(s16 fadeOutTime) { (void) fadeOutTime; }
void play_menu_sounds(s16 soundMenuFlags) { (void) soundMenuFlags; }
void play_painting_eject_sound(void) {}
void play_shell_music(void) {}
void stop_shell_music(void) {}
void play_cap_music(u16 seqArgs) { (void) seqArgs; }
void stop_cap_music(void) {}
void fadeout_cap_music(void) {}

/* --- Levels / warps ----------------------------------------------------- */
/*
 * The demo is one persistent area, so a warp request (including the one a death
 * plane raises) is swallowed.  The demo level instead surrounds its pit with a
 * respawn trigger handled by the app, so falling in is recoverable.
 */
void level_trigger_warp(struct MarioState *m, s32 warpOp) { (void) m; (void) warpOp; }
void fade_into_special_warp(u32 arg, u32 color) { (void) arg; (void) color; }
s16 level_control_timer(s32 timerOp) { (void) timerOp; return 0; }
void load_level_init_text(u32 arg) { (void) arg; }

/* No wind volumes in the demo. Returning FALSE keeps air movement on its
 * normal path rather than the wind-override path. */
s32 check_horizontal_wind(struct MarioState *m) { (void) m; return FALSE; }

/* --- Interaction -------------------------------------------------------- */
/*
 * No objects means: nothing to grab, nothing to hold, nothing to ride, and no
 * damage.  Movement code branches on these constantly; answering "no" uniformly
 * keeps it on the plain-locomotion paths.
 */
u32 mario_check_object_grab(struct MarioState *m) { (void) m; return FALSE; }
s32 mario_can_bubble(struct MarioState *m) { (void) m; return FALSE; }
void mario_stop_riding_object(struct MarioState *m) { (void) m; }
void mario_grab_used_object(struct MarioState *m) { (void) m; }
void mario_drop_held_object(struct MarioState *m) { (void) m; }
void mario_throw_held_object(struct MarioState *m) { (void) m; }
void mario_stop_riding_and_holding(struct MarioState *m) { (void) m; }
u32 does_mario_have_normal_cap_on_head(struct MarioState *m) {
    return (m->flags & (MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD))
           == (MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD);
}
void mario_blow_off_cap(struct MarioState *m, f32 capSpeed) { (void) m; (void) capSpeed; }
u32 mario_lose_cap_to_enemy(u32 arg) { (void) arg; return FALSE; }
void mario_retrieve_cap(void) {}
u32 able_to_grab_object(struct MarioState *m, struct Object *o) {
    (void) m; (void) o;
    return FALSE;
}
u32 mario_process_interactions(struct MarioState *m) { (void) m; return FALSE; }
void check_kick_or_punch_wall(struct MarioState *m) { (void) m; }
u32 determine_interaction(struct MarioState *m, struct Object *o) {
    (void) m; (void) o;
    return 0;
}
u32 take_damage_and_knock_back(struct MarioState *m, struct Object *o) {
    (void) m; (void) o;
    return FALSE;
}
void reset_mario_pitch(struct MarioState *m) {
    if (m->action == ACT_WATER_JUMP || m->action == ACT_SHOT_FROM_CANNON
        || m->action == ACT_FLYING) {
        m->faceAngle[0] = 0;
    }
}

/* Lava walls exist in the type table but not in the demo level. */
s32 lava_boost_on_wall(struct MarioState *m) { (void) m; return FALSE; }

/* --- Objects ------------------------------------------------------------ */

void obj_set_gfx_pos_at_obj_pos(struct Object *obj1, struct Object *obj2) {
    if (obj1 == NULL || obj2 == NULL) {
        return;
    }
    obj1->header.gfx.pos[0] = obj2->header.gfx.pos[0];
    obj1->header.gfx.pos[1] = obj2->header.gfx.pos[1];
    obj1->header.gfx.pos[2] = obj2->header.gfx.pos[2];
}
struct Object *spawn_object(struct Object *parent, s32 model, const void *behavior) {
    (void) parent; (void) model; (void) behavior;
    return NULL;
}
void cur_obj_play_sound_2(s32 soundMagic) { (void) soundMagic; }
s32 is_point_within_radius_of_mario(f32 x, f32 y, f32 z, s32 dist) {
    (void) x; (void) y; (void) z; (void) dist;
    return FALSE;
}
void set_object_visibility(struct Object *obj, s32 dist) { (void) obj; (void) dist; }

/* Static platforms only, so nothing to carry the rider by. */
void apply_platform_displacement(u32 isMario, struct Object *platform) {
    (void) isMario; (void) platform;
}
