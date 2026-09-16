/*
 * new64 -- follow camera.
 *
 * The camera is *not* purely presentational.  Analog stick direction is
 * interpreted relative to camera yaw (see update_mario_joystick_inputs), so the
 * camera is an input to the physics and a different camera gives different
 * movement from identical stick input.  That is why it lives in the engine
 * rather than the renderer, and why its yaw is exported to the player through
 * struct Camera.
 *
 * Convention: `yaw` is the s16 angle of the vector *from the player to the
 * camera*.  Pushing the stick away from the camera therefore moves the player
 * away from the camera, which is what a player expects.
 */
#ifndef M64_CAMERA_H
#define M64_CAMERA_H

#include "m64_types.h"

struct M64Camera {
    Vec3f pos;
    Vec3f focus;
    s16 yaw;      /* player -> camera, the value the physics reads */
    s16 pitch;
    f32 dist;
    f32 height;
    s32 initialized;
};

void m64_camera_init(struct M64Camera *c, const Vec3f targetPos, s16 yaw);

/*
 * Advance the camera one tick.
 *
 * `yawInput` rotates the camera (from the C buttons / Q and E); when it is zero
 * the camera eases toward behind the player's *motion*, not their facing, so
 * that turning on the spot does not swing the view.
 */
void m64_camera_update(struct M64Camera *c, const Vec3f targetPos, s16 targetFaceYaw,
                       f32 targetSpeed, s16 yawInput);

#endif /* M64_CAMERA_H */
