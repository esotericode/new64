/*
 * new64 -- follow camera implementation.
 */
#include "m64_camera.h"

#include "m64_math.h"
#include "m64_surface.h"

/* Held a little above the player's feet so the view looks at the body. */
#define M64_CAM_FOCUS_HEIGHT 120.0f
#define M64_CAM_DEFAULT_DIST 900.0f
#define M64_CAM_DEFAULT_HEIGHT 350.0f

void m64_camera_init(struct M64Camera *c, const Vec3f targetPos, s16 yaw) {
    c->yaw = yaw;
    c->pitch = 0x0800;
    c->dist = M64_CAM_DEFAULT_DIST;
    c->height = M64_CAM_DEFAULT_HEIGHT;

    c->focus[0] = targetPos[0];
    c->focus[1] = targetPos[1] + M64_CAM_FOCUS_HEIGHT;
    c->focus[2] = targetPos[2];

    c->pos[0] = c->focus[0] + c->dist * m64_sins(c->yaw);
    c->pos[1] = c->focus[1] + c->height;
    c->pos[2] = c->focus[2] + c->dist * m64_coss(c->yaw);
    c->initialized = TRUE;
}

void m64_camera_update(struct M64Camera *c, const Vec3f targetPos, s16 targetFaceYaw,
                       f32 targetSpeed, s16 yawInput) {
    f32 groundHeight;
    s16 desiredYaw;

    if (!c->initialized) {
        m64_camera_init(c, targetPos, (s16) (targetFaceYaw + 0x8000));
        return;
    }

    if (yawInput != 0) {
        /* Manual rotation always wins; no auto-follow fighting the player. */
        c->yaw += yawInput;
    } else if (targetSpeed > 8.0f) {
        /*
         * Ease to behind the player, but only while actually moving.  Keying
         * off speed rather than facing is what stops the camera swinging around
         * when the player pivots on the spot -- and it keeps the stick-to-world
         * mapping stable during a turn, which matters because that mapping is
         * part of the physics.
         */
        desiredYaw = (s16) (targetFaceYaw + 0x8000);
        c->yaw = (s16) (desiredYaw
                        - (s16) m64_approach_s32((s16) (desiredYaw - c->yaw), 0, 0x180,
                                                 0x180));
    }

    /* Focus eases toward the player so the view is not rigidly welded to them. */
    c->focus[0] = m64_approach_f32(c->focus[0], targetPos[0], 1e9f, 1e9f);
    c->focus[2] = m64_approach_f32(c->focus[2], targetPos[2], 1e9f, 1e9f);
    /*
     * Vertical tracking is deliberately much slower than horizontal.  Following
     * Y tightly would keep a jumping player pinned to the same screen position,
     * which makes jump height almost impossible to read -- the whole arc would
     * look flat.  Lagging it lets the player visibly rise and fall in frame,
     * and matches how the original's camera hangs back during a jump.
     */
    c->focus[1] = m64_approach_f32(c->focus[1], targetPos[1] + M64_CAM_FOCUS_HEIGHT,
                                   8.0f, 12.0f);

    c->pos[0] = c->focus[0] + c->dist * m64_sins(c->yaw);
    c->pos[2] = c->focus[2] + c->dist * m64_coss(c->yaw);
    c->pos[1] = c->focus[1] + c->height;

    /* Never sink below the ground at the camera's own position. */
    groundHeight = m64_find_floor_height(c->pos[0], c->pos[1] + 200.0f, c->pos[2]);
    if (groundHeight > FLOOR_LOWER_LIMIT_MISC && c->pos[1] < groundHeight + 100.0f) {
        c->pos[1] = groundHeight + 100.0f;
    }
}
