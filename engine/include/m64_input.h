/*
 * new64 -- controller sampling.
 *
 * A front-end fills in the *raw* stick reading and the button bitmasks, then
 * calls m64_controller_update(), which derives the dead-zone-corrected stick
 * and its magnitude.  Doing the derivation here rather than in the front-end is
 * deliberate: the dead zone and the magnitude clamp are part of the movement
 * model, not part of the hardware, and every front-end must get them identical
 * or the physics differs between them.
 *
 * Raw axis range is -80..80, matching the hardware the movement model was tuned
 * for.  A keyboard front-end should report +/-80, not +/-1.
 */
#ifndef M64_INPUT_H
#define M64_INPUT_H

#include "m64_types.h"

struct Controller;

/* Feed one frame of raw input, then derive stick/stickMag and button edges. */
void m64_controller_update(struct Controller *c, s16 rawStickX, s16 rawStickY,
                           u16 buttonDown);

#endif /* M64_INPUT_H */
