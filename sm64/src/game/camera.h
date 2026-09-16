/*
 * new64 -- camera hooks.
 *
 * Camera *yaw* is a physics input: stick direction is interpreted relative to
 * it, so the camera and the movement code cannot be fully decoupled.  Mode
 * changes, shakes and cutscene requests are accepted and ignored.
 */
#ifndef NEW64_CAMERA_H
#define NEW64_CAMERA_H

#include "types.h"

#define CAMERA_MODE_NONE             0x00
#define CAMERA_MODE_RADIAL           0x01
#define CAMERA_MODE_OUTWARD_RADIAL   0x02
#define CAMERA_MODE_BEHIND_MARIO     0x03
#define CAMERA_MODE_CLOSE            0x04
#define CAMERA_MODE_C_UP             0x06
#define CAMERA_MODE_WATER_SURFACE    0x08
#define CAMERA_MODE_SLIDE_HOOT       0x09
#define CAMERA_MODE_INSIDE_CANNON    0x0A
#define CAMERA_MODE_8_DIRECTIONS     0x0B
#define CAMERA_MODE_FREE_ROAM        0x10
#define CAMERA_MODE_SPIRAL_STAIRS    0x11

#define SHAKE_ATTACK        1
#define SHAKE_FALL_DAMAGE   3
#define SHAKE_GROUND_POUND  4
#define SHAKE_SMALL_DAMAGE  5
#define SHAKE_MED_DAMAGE    6
#define SHAKE_LARGE_DAMAGE  7
#define SHAKE_HIT_FROM_BELOW 8
#define SHAKE_FOV_SMALL     1
#define SHAKE_FOV_MEDIUM    2
#define SHAKE_FOV_LARGE     3
#define SHAKE_FOV_UNK       4

void set_camera_mode(struct Camera *c, s16 mode, s16 frames);
void set_camera_shake_from_hit(s16 shake);
void set_camera_shake_from_point(s16 shake, f32 posX, f32 posY, f32 posZ);
void set_fov_shake_from_point_preset(u8 preset, f32 posX, f32 posY, f32 posZ);
void set_camera_pitch_shake(s16 mag, s16 decay, s16 inc);
void set_camera_yaw_shake(s16 mag, s16 decay, s16 inc);
void set_camera_roll_shake(s16 mag, s16 decay, s16 inc);
s32 set_cam_angle(s32 mode);
void reset_camera(struct Camera *c);

extern struct Camera *gCamera;

#endif /* NEW64_CAMERA_H */
