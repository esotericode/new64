/* new64 -- level/warp hooks. The demo is a single persistent area, so warps,
 * deaths and level transitions are accepted and ignored. */
#ifndef NEW64_LEVEL_UPDATE_H
#define NEW64_LEVEL_UPDATE_H

#include "types.h"

#define WARP_OP_NONE           0x00
#define WARP_OP_WARP_FLOOR     0x01
#define WARP_OP_TRIGGERS_LEVEL_SELECT 0x11
#define WARP_OP_STAR_EXIT      0x12
#define WARP_OP_DEATH          0x14
#define WARP_OP_WARP_OBJECT    0x15
#define WARP_OP_TELEPORT       0x16
#define WARP_OP_CREDITS_END    0x17

#define MARIO_SPAWN_DOOR_WARP  0x01
#define MARIO_SPAWN_UNKNOWN_02 0x02
#define MARIO_SPAWN_UNKNOWN_03 0x03

s32 lava_boost_on_wall(struct MarioState *m);
s32 check_fall_damage(struct MarioState *m, u32 hardFallAction);
s32 check_kick_or_dive_in_air(struct MarioState *m);
s32 should_get_stuck_in_ground(struct MarioState *m);
s32 check_fall_damage_or_get_stuck(struct MarioState *m, u32 hardFallAction);
s32 check_horizontal_wind(struct MarioState *m);
void level_trigger_warp(struct MarioState *m, s32 warpOp);
void fade_into_special_warp(u32 arg, u32 color);
s16 level_control_timer(s32 timerOp);
void load_level_init_text(u32 arg);

extern s16 gCurrSaveFileNum;
extern u16 gHudFlash;

#endif /* NEW64_LEVEL_UPDATE_H */
