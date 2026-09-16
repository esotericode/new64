/*
 * new64 -- interaction hooks.
 *
 * The demo has no pickups, enemies or NPCs, so the interaction subsystem is
 * stubbed. The *flags* still exist because movement code tests them (for
 * example, whether a grabbable object is in front of you gates the grab
 * branch out of walking), and they read as "nothing there".
 */
#ifndef NEW64_INTERACTION_H
#define NEW64_INTERACTION_H

#include "types.h"

#define INT_GROUND_POUND_OR_TWIRL (1 << 0)
#define INT_PUNCH                 (1 << 1)
#define INT_KICK                  (1 << 2)
#define INT_TRIP                  (1 << 3)
#define INT_SLIDE_KICK            (1 << 4)
#define INT_FAST_ATTACK_OR_SHELL  (1 << 5)
#define INT_HIT_FROM_ABOVE        (1 << 6)
#define INT_HIT_FROM_BELOW        (1 << 7)

#define INT_ATTACK_NOT_FROM_BELOW                                                      \
    (INT_GROUND_POUND_OR_TWIRL | INT_PUNCH | INT_KICK | INT_TRIP | INT_SLIDE_KICK      \
     | INT_FAST_ATTACK_OR_SHELL | INT_HIT_FROM_ABOVE)

#define INT_ANY_ATTACK                                                                 \
    (INT_GROUND_POUND_OR_TWIRL | INT_PUNCH | INT_KICK | INT_TRIP | INT_SLIDE_KICK      \
     | INT_FAST_ATTACK_OR_SHELL | INT_HIT_FROM_ABOVE | INT_HIT_FROM_BELOW)

#define INT_SUBTYPE_DELAY_INVINCIBILITY 0x00000008
#define INT_SUBTYPE_BIG_KNOCKBACK       0x00000800

#define INTERACT_HOOT         (1 <<  0)
#define INTERACT_GRABBABLE    (1 <<  1)
#define INTERACT_DOOR         (1 <<  2)
#define INTERACT_DAMAGE       (1 <<  3)
#define INTERACT_COIN         (1 <<  4)
#define INTERACT_CAP          (1 <<  5)
#define INTERACT_POLE         (1 <<  7)
#define INTERACT_WARP         (1 << 10)
#define INTERACT_STAR_OR_KEY  (1 << 12)
#define INTERACT_BOUNCE_TOP   (1 << 13)
#define INTERACT_WATER_RING   (1 << 14)
#define INTERACT_BULLY        (1 << 15)
#define INTERACT_CLAM_OR_BUBBA (1 << 16)
#define INTERACT_SHOCK        (1 << 17)
#define INTERACT_CANNON_BASE  (1 << 21)
#define INTERACT_KOOPA        (1 << 25)
#define INTERACT_UNKNOWN_31   (1u << 31)

#define MARIO_PUNCH_IMPACT 0
#define MARIO_KICK_IMPACT  1

u32 mario_check_object_grab(struct MarioState *m);
s32 mario_can_bubble(struct MarioState *m);
void mario_stop_riding_object(struct MarioState *m);
void mario_grab_used_object(struct MarioState *m);
void mario_drop_held_object(struct MarioState *m);
void mario_throw_held_object(struct MarioState *m);
void mario_stop_riding_and_holding(struct MarioState *m);
u32 does_mario_have_normal_cap_on_head(struct MarioState *m);
void mario_blow_off_cap(struct MarioState *m, f32 capSpeed);
u32 mario_lose_cap_to_enemy(u32 arg);
void mario_retrieve_cap(void);
u32 able_to_grab_object(struct MarioState *m, struct Object *o);
void mario_handle_special_floors(struct MarioState *m);
u32 mario_process_interactions(struct MarioState *m);
void check_kick_or_punch_wall(struct MarioState *m);
u32 determine_interaction(struct MarioState *m, struct Object *o);
u32 take_damage_and_knock_back(struct MarioState *m, struct Object *o);
void reset_mario_pitch(struct MarioState *m);

#endif /* NEW64_INTERACTION_H */
