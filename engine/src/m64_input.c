/*
 * new64 -- controller sampling implementation.
 */
#include "m64_input.h"

#include "sm64.h"
#include "types.h"

#include <math.h>

void m64_controller_update(struct Controller *c, s16 rawStickX, s16 rawStickY,
                           u16 buttonDown) {
    u16 previousDown = c->buttonDown;
    f32 mag;

    c->rawStickX = rawStickX;
    c->rawStickY = rawStickY;

    /* Edges are derived here so no front-end has to track them itself. */
    c->buttonDown = buttonDown;
    c->buttonPressed = (u16) (buttonDown & (previousDown ^ buttonDown));

    /*
     * Dead zone: readings inside +/-8 are discarded, and everything outside is
     * shifted *toward* zero by 6 rather than rescaled.  So the smallest usable
     * deflection is 2, not a fraction of a unit -- which is why very light
     * stick pressure produces no movement at all rather than a crawl.
     */
    c->stickX = 0.0f;
    c->stickY = 0.0f;

    if (c->rawStickX <= -8) {
        c->stickX = (f32) (c->rawStickX + 6);
    }
    if (c->rawStickX >= 8) {
        c->stickX = (f32) (c->rawStickX - 6);
    }
    if (c->rawStickY <= -8) {
        c->stickY = (f32) (c->rawStickY + 6);
    }
    if (c->rawStickY >= 8) {
        c->stickY = (f32) (c->rawStickY + -6);
    }

    mag = sqrtf(c->stickX * c->stickX + c->stickY * c->stickY);

    /*
     * Clamp magnitude to 64 while preserving direction.  Note this makes the
     * usable stick region a *circle*, so a diagonal is no faster than an axis --
     * without this, diagonal movement would be 1.41x faster.
     */
    if (mag > 64.0f) {
        c->stickX *= 64.0f / mag;
        c->stickY *= 64.0f / mag;
        mag = 64.0f;
    }
    c->stickMag = mag;
}
