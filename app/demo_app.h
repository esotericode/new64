/*
 * new64 -- the host application layer.
 *
 * This is the part a real game would own: it allocates the player, wires up the
 * area and camera that the movement code reads, ticks the state machine, and
 * runs the camera afterwards.  Everything here is deliberately thin -- if you
 * find yourself wanting to add movement logic at this level, it belongs in the
 * state machine instead.
 */
#ifndef NEW64_DEMO_APP_H
#define NEW64_DEMO_APP_H

#include "m64_camera.h"
#include "m64_types.h"
#include "types.h"

struct M64World {
    struct MarioState marioState;
    struct Object marioObj;
    struct Object dummyObj;
    struct Area area;
    struct Camera camera;      /* what the movement code reads (yaw only) */
    struct M64Camera viewCam;  /* the engine's actual follow camera */
    struct PlayerCameraState camState;
    struct MarioBodyState bodyState;
    struct MarioAnimation animation;
    struct Controller controller;
    u32 frame;
};

/* Build the demo level, place the player, and bring the camera up behind them. */
void m64_world_init(struct M64World *w);

/*
 * Advance one movement tick.
 *
 * Order matters: input is sampled, then the state machine runs, then the camera
 * follows.  Running the camera *after* movement means the yaw the next frame's
 * stick input is interpreted against reflects where the player actually ended
 * up -- matching how the original sequences its frame.
 */
void m64_world_step(struct M64World *w, s16 rawStickX, s16 rawStickY, u16 buttonDown,
                    s16 camYawInput);

/* Human-readable name for an action id, for traces and the HUD overlay. */
const char *m64_action_name(u32 action);

/* One line of state, formatted for a trace file. */
void m64_world_format_trace(const struct M64World *w, char *out, s32 outSize);

#endif /* NEW64_DEMO_APP_H */
