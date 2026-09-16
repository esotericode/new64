/* new64 -- placeholder for <game/platform_displacement.h>; see sm64/src/shims/ for the stub
 * implementations of anything hosted code actually calls. */
#ifndef NEW64_PLATFORM_DISPLACEMENT_H
#define NEW64_PLATFORM_DISPLACEMENT_H

#include "types.h"

#endif /* NEW64_PLATFORM_DISPLACEMENT_H */

/* Riding a moving platform carries the rider with it. The demo's platforms are
 * static, so this is a no-op, but the call site in the step code is real. */
void apply_platform_displacement(u32 isMario, struct Object *platform);
