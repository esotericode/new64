/*
 * new64 -- host application layer implementation.
 */
#include "demo_app.h"

#include "area.h"
#include "audio/external.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "m64_input.h"
#include "m64_level.h"
#include "m64_surface.h"
#include "mario.h"
#include "sm64.h"

#include <stdio.h>
#include <string.h>

void m64_world_init(struct M64World *w) {
    Vec3f spawn;

    memset(w, 0, sizeof(*w));

    m64_level_build_demo(spawn);

    /*
     * Wire the player up.  gMarioState is a global because hosted movement code
     * refers to it that way; everything it points at lives in this struct so
     * that a host can own several worlds if it wants to.
     */
    gMarioState = &w->marioState;
    gPlayer1Controller = &w->controller;
    gCurrentArea = &w->area;

    w->area.camera = &w->camera;
    w->area.index = 1;
    /* TERRAIN_GRASS keeps default slope thresholds; TERRAIN_SLIDE would make the
     * whole level behave as a slide, which is a useful thing to try. */
    w->area.terrainType = TERRAIN_GRASS;

    w->marioState.marioObj = &w->marioObj;
    w->marioState.area = &w->area;
    w->marioState.statusForCamera = &w->camState;
    w->marioState.marioBodyState = &w->bodyState;
    w->marioState.controller = &w->controller;
    w->marioState.animation = &w->animation;

    w->marioState.numCoins = 0;
    w->marioState.numStars = 0;

    m64_vec3f_copy(w->marioState.pos, spawn);
    m64_vec3f_copy(w->marioObj.header.gfx.pos, spawn);
    w->marioObj.header.gfx.scale[0] = 1.0f;
    w->marioObj.header.gfx.scale[1] = 1.0f;
    w->marioObj.header.gfx.scale[2] = 1.0f;

    w->marioState.faceAngle[1] = 0;

    init_mario();

    /* Camera starts behind the player, looking the way they face. */
    w->camera.yaw = (s16) (w->marioState.faceAngle[1] + 0x8000);
    m64_camera_init(&w->viewCam, w->marioState.pos, w->camera.yaw);
    w->frame = 0;
}

void m64_world_step(struct M64World *w, s16 rawStickX, s16 rawStickY, u16 buttonDown,
                    s16 camYawInput) {
    f32 speed;

    m64_sound_log_begin_frame((s32) w->frame);

    m64_controller_update(&w->controller, rawStickX, rawStickY, buttonDown);

    /* Dynamic surfaces are rebuilt each frame; the demo level has none, but the
     * call belongs here so a moving platform would work without restructuring. */
    m64_surface_clear_dynamic();

    execute_mario_action(&w->marioObj);

    speed = w->marioState.forwardVel < 0.0f ? -w->marioState.forwardVel
                                           : w->marioState.forwardVel;
    m64_camera_update(&w->viewCam, w->marioState.pos, w->marioState.faceAngle[1], speed,
                      camYawInput);
    /* Publish the camera yaw the movement code will read next frame. */
    w->camera.yaw = w->viewCam.yaw;

    w->frame++;
    gGlobalTimer++;
}

/* --- Action names ------------------------------------------------------- */

struct ActionName {
    u32 action;
    const char *name;
};

static const struct ActionName sActionNames[] = {
    { ACT_IDLE, "IDLE" },
    { ACT_WALKING, "WALKING" },
    { ACT_TURNING_AROUND, "TURNING_AROUND" },
    { ACT_FINISH_TURNING_AROUND, "FINISH_TURNING" },
    { ACT_BRAKING, "BRAKING" },
    { ACT_BRAKING_STOP, "BRAKING_STOP" },
    { ACT_DECELERATING, "DECELERATING" },
    { ACT_CROUCHING, "CROUCHING" },
    { ACT_START_CROUCHING, "START_CROUCHING" },
    { ACT_STOP_CROUCHING, "STOP_CROUCHING" },
    { ACT_START_CRAWLING, "START_CRAWLING" },
    { ACT_CRAWLING, "CRAWLING" },
    { ACT_STOP_CRAWLING, "STOP_CRAWLING" },
    { ACT_JUMP, "JUMP" },
    { ACT_DOUBLE_JUMP, "DOUBLE_JUMP" },
    { ACT_TRIPLE_JUMP, "TRIPLE_JUMP" },
    { ACT_BACKFLIP, "BACKFLIP" },
    { ACT_SIDE_FLIP, "SIDE_FLIP" },
    { ACT_LONG_JUMP, "LONG_JUMP" },
    { ACT_STEEP_JUMP, "STEEP_JUMP" },
    { ACT_WALL_KICK_AIR, "WALL_KICK_AIR" },
    { ACT_AIR_HIT_WALL, "AIR_HIT_WALL" },
    { ACT_FREEFALL, "FREEFALL" },
    { ACT_DIVE, "DIVE" },
    { ACT_DIVE_SLIDE, "DIVE_SLIDE" },
    { ACT_GROUND_POUND, "GROUND_POUND" },
    { ACT_GROUND_POUND_LAND, "GROUND_POUND_LAND" },
    { ACT_SLIDE_KICK, "SLIDE_KICK" },
    { ACT_SLIDE_KICK_SLIDE, "SLIDE_KICK_SLIDE" },
    { ACT_SLIDE_KICK_SLIDE_STOP, "SLIDE_KICK_STOP" },
    { ACT_JUMP_KICK, "JUMP_KICK" },
    { ACT_BUTT_SLIDE, "BUTT_SLIDE" },
    { ACT_BUTT_SLIDE_AIR, "BUTT_SLIDE_AIR" },
    { ACT_BUTT_SLIDE_STOP, "BUTT_SLIDE_STOP" },
    { ACT_STOMACH_SLIDE, "STOMACH_SLIDE" },
    { ACT_STOMACH_SLIDE_STOP, "STOMACH_SLIDE_STOP" },
    { ACT_CROUCH_SLIDE, "CROUCH_SLIDE" },
    { ACT_JUMP_LAND, "JUMP_LAND" },
    { ACT_JUMP_LAND_STOP, "JUMP_LAND_STOP" },
    { ACT_DOUBLE_JUMP_LAND, "DOUBLE_JUMP_LAND" },
    { ACT_DOUBLE_JUMP_LAND_STOP, "DBL_JUMP_LAND_STOP" },
    { ACT_TRIPLE_JUMP_LAND, "TRIPLE_JUMP_LAND" },
    { ACT_TRIPLE_JUMP_LAND_STOP, "TRP_JUMP_LAND_STOP" },
    { ACT_BACKFLIP_LAND, "BACKFLIP_LAND" },
    { ACT_BACKFLIP_LAND_STOP, "BACKFLIP_LAND_STOP" },
    { ACT_SIDE_FLIP_LAND, "SIDE_FLIP_LAND" },
    { ACT_SIDE_FLIP_LAND_STOP, "SIDE_FLIP_LAND_STOP" },
    { ACT_LONG_JUMP_LAND, "LONG_JUMP_LAND" },
    { ACT_LONG_JUMP_LAND_STOP, "LONG_JUMP_LAND_STOP" },
    { ACT_FREEFALL_LAND, "FREEFALL_LAND" },
    { ACT_FREEFALL_LAND_STOP, "FREEFALL_LAND_STOP" },
    { ACT_FORWARD_ROLLOUT, "FORWARD_ROLLOUT" },
    { ACT_BACKWARD_ROLLOUT, "BACKWARD_ROLLOUT" },
    { ACT_SOFT_BONK, "SOFT_BONK" },
    { ACT_BACKWARD_AIR_KB, "BACKWARD_AIR_KB" },
    { ACT_FORWARD_AIR_KB, "FORWARD_AIR_KB" },
    { ACT_BACKWARD_GROUND_KB, "BACKWARD_GROUND_KB" },
    { ACT_HARD_BACKWARD_GROUND_KB, "HARD_BACK_GND_KB" },
    { ACT_HARD_FORWARD_GROUND_KB, "HARD_FWD_GND_KB" },
    { ACT_GROUND_BONK, "GROUND_BONK" },
    { ACT_LEDGE_GRAB, "LEDGE_GRAB" },
    { ACT_LEDGE_CLIMB_SLOW_1, "LEDGE_CLIMB_SLOW" },
    { ACT_LEDGE_CLIMB_FAST, "LEDGE_CLIMB_FAST" },
    { ACT_LEDGE_CLIMB_DOWN, "LEDGE_CLIMB_DOWN" },
    { ACT_START_HANGING, "START_HANGING" },
    { ACT_HANGING, "HANGING" },
    { ACT_HANG_MOVING, "HANG_MOVING" },
    { ACT_PUNCHING, "PUNCHING" },
    { ACT_MOVE_PUNCHING, "MOVE_PUNCHING" },
    { ACT_TWIRLING, "TWIRLING" },
    { ACT_TWIRL_LAND, "TWIRL_LAND" },
    { ACT_STANDING_AGAINST_WALL, "AGAINST_WALL" },
    { ACT_PANTING, "PANTING" },
    { ACT_SQUISHED, "SQUISHED" },
    { ACT_WATER_PLUNGE, "WATER_PLUNGE" },
    { ACT_FIRST_PERSON, "FIRST_PERSON" },
    { ACT_SHOCKWAVE_BOUNCE, "SHOCKWAVE_BOUNCE" },
};

const char *m64_action_name(u32 action) {
    static char fallback[24];
    s32 i;
    s32 count = (s32) (sizeof(sActionNames) / sizeof(sActionNames[0]));

    for (i = 0; i < count; i++) {
        if (sActionNames[i].action == action) {
            return sActionNames[i].name;
        }
    }
    snprintf(fallback, sizeof(fallback), "ACT_%08X", action);
    return fallback;
}

void m64_world_format_trace(const struct M64World *w, char *out, s32 outSize) {
    const struct MarioState *m = &w->marioState;
    s32 n;
    s32 i;

    n = snprintf(out, (size_t) outSize,
                 "%5u %-20s pos=(%9.2f %9.2f %9.2f) vel=(%7.2f %7.2f %7.2f) "
                 "fwd=%7.2f yaw=%6d floorY=%9.2f floorT=%3d",
                 w->frame, m64_action_name(m->action), m->pos[0], m->pos[1], m->pos[2],
                 m->vel[0], m->vel[1], m->vel[2], m->forwardVel, m->faceAngle[1],
                 m->floorHeight, m->floor != NULL ? m->floor->type : -1);

    /* Append any sound cues raised this frame: a compact, very legible record of
     * what the state machine thought happened. */
    for (i = 0; i < m64_sound_log_count() && n < outSize - 24; i++) {
        const struct M64SoundEvent *ev = m64_sound_log_at(i);
        const char *name = m64_sound_name(ev->soundBits);

        if (name != NULL) {
            n += snprintf(out + n, (size_t) (outSize - n), " [%s]", name);
        }
    }
}
