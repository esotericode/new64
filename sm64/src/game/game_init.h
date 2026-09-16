/* new64 -- per-frame globals hosted code reads. */
#ifndef NEW64_GAME_INIT_H
#define NEW64_GAME_INIT_H

#include "types.h"

extern struct Controller *gPlayer1Controller;
extern struct Controller *gPlayer2Controller;
extern struct Controller *gPlayer3Controller;

/* Frame counter, incremented once per movement tick. Some behaviour is phased
 * off this (alternating footstep sounds, idle animation variety). */
extern u32 gGlobalTimer;

#endif /* NEW64_GAME_INIT_H */
