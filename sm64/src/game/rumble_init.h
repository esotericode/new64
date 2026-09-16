/* new64 -- rumble requests, accepted and dropped. Kept as real calls so that a
 * front-end with a gamepad can wire them up later without touching action
 * code. */
#ifndef NEW64_RUMBLE_INIT_H
#define NEW64_RUMBLE_INIT_H

#include "types.h"

#define RUMBLE_EVENT_CONSTANT 0
#define RUMBLE_EVENT_LEVELON  1
#define RUMBLE_EVENT_LEVELOFF 2
#define RUMBLE_EVENT_NOCONST  3

void queue_rumble_data(s16 a0, s16 a1);
void queue_rumble_data_mario(struct MarioState *m, s16 a0, s16 a1);
void reset_rumble_timers(void);
void queue_rumble_submerged(void);

#endif /* NEW64_RUMBLE_INIT_H */
