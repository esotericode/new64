/*
 * new64 -- the shared object model.
 *
 * Everything in here exists because hosted movement code reads these fields by
 * name.  struct MarioState in particular is the whole interface between the
 * state machine and the engine: an action function's entire world is this
 * struct plus the four step/collision calls.  Field *names* are therefore part
 * of the contract and must not be "improved".
 *
 * Field ordering is kept close to the original layouts as a courtesy to anyone
 * diffing against a decompilation, but nothing here depends on exact offsets --
 * new64 compiles the hosted sources from source, so the compiler agrees with
 * itself.  Sizes are host-native (64-bit pointers), not N64 sizes.
 */
#ifndef NEW64_TYPES_H
#define NEW64_TYPES_H

#include <PR/ultratypes.h>
#include "m64_types.h"
#include "m64_surface.h" /* struct Surface, struct SurfaceNode, WallCollisionData */

struct Object;
struct Area;
struct Camera;
struct MarioState;

/* --- Animation ---------------------------------------------------------- */

/*
 * new64 ships no animation data.  The struct exists so that hosted code can
 * set and query animations, and so the *timing* fields (loopStart/loopEnd) can
 * be populated from a table -- see assets/mario_anims.txt and
 * docs/SLOTTING_IN_DECOMP.md.  Several original actions end when an animation
 * ends, so those frame counts are genuinely part of the physics.
 */
struct Animation {
    s16 flags;
    s16 animYTransDivisor;
    s16 startFrame;
    s16 loopStart;
    s16 loopEnd;
    s16 unusedBoneCount;
    const s16 *values;
    const u16 *index;
    u32 length;
};

#define ANIM_FLAG_NOLOOP     (1 << 0)
#define ANIM_FLAG_FORWARD    (1 << 1)
#define ANIM_FLAG_2          (1 << 2)
#define ANIM_FLAG_HOR_TRANS  (1 << 3)
#define ANIM_FLAG_VERT_TRANS (1 << 4)
#define ANIM_FLAG_6          (1 << 6)

struct AnimInfo {
    s16 animID;
    s16 animYTrans;
    struct Animation *curAnim;
    s16 animFrame;
    u16 animTimer;
    s32 animFrameAccelAssist;
    s32 animAccel;
};

/* --- Scene graph -------------------------------------------------------- */

struct GraphNode {
    s16 type;
    s16 flags;
    struct GraphNode *prev;
    struct GraphNode *next;
    struct GraphNode *parent;
    struct GraphNode *children;
};

/*
 * The renderable half of an object.  Movement code writes pos/angle here at the
 * end of every step so that rendering and collision cannot disagree: collision
 * uses MarioState.pos, display uses this copy.
 */
struct GraphNodeObject {
    struct GraphNode node;
    struct GraphNode *sharedChild;
    s8 areaIndex;
    s8 activeAreaIndex;
    Vec3s angle;
    Vec3f pos;
    Vec3f scale;
    struct AnimInfo animInfo;
    f32 *throwMatrix;
    Vec3f cameraToObject;
};

struct ObjectNode {
    struct GraphNodeObject gfx;
    struct ObjectNode *next;
    struct ObjectNode *prev;
};

/*
 * Objects use an untyped scratch area addressed through the o* macros in
 * object_fields.h.  That indirection is preserved because hosted code uses
 * those macros; see that header for the rationale.
 */
#define OBJECT_RAW_WORDS 0x50

struct Object {
    struct ObjectNode header;
    struct Object *parentObj;
    struct Object *prevObj;
    u32 collidedObjInteractTypes;
    s16 activeFlags;
    s16 numCollidedObjs;
    struct Object *collidedObjs[4];
    union {
        s32 asS32[OBJECT_RAW_WORDS];
        u32 asU32[OBJECT_RAW_WORDS];
        f32 asF32[OBJECT_RAW_WORDS];
        s16 asS16[OBJECT_RAW_WORDS][2];
        u16 asU16[OBJECT_RAW_WORDS][2];
        void *asPointer[OBJECT_RAW_WORDS];
        struct Object *asObject[OBJECT_RAW_WORDS];
        struct Surface *asSurface[OBJECT_RAW_WORDS];
    } rawData;
    const void *behavior;
    u32 unused1;
    struct Object *platform;
    void *collisionData;
    f32 hitboxRadius;
    f32 hitboxHeight;
    f32 hurtboxRadius;
    f32 hurtboxHeight;
    f32 hitboxDownOffset;
    u8 respawnInfoType;
    void *respawnInfo;
};

/* --- Controller --------------------------------------------------------- */

/*
 * rawStick* is the hardware reading; stick* is the dead-zone-corrected value
 * and stickMag its magnitude, clamped to 64.  Movement code reads stickMag, so
 * a front-end must fill in raw values and let update_controller() derive the
 * rest -- see m64_input.h.
 */
struct Controller {
    s16 rawStickX;
    s16 rawStickY;
    f32 stickX;
    f32 stickY;
    f32 stickMag;
    u16 buttonDown;
    u16 buttonPressed;
};

/* --- Camera ------------------------------------------------------------- */

/*
 * Only the fields movement code touches are modelled.  `yaw` is the important
 * one: analog stick input is interpreted *relative to the camera*, so camera
 * yaw is an input to the physics, not just a display concern.
 */
struct Camera {
    u8 mode;
    u8 defMode;
    f32 areaCenX;
    f32 areaCenZ;
    u16 cutscene;
    Vec3f focus;
    Vec3f pos;
    s16 yaw;
    s16 nextYaw;
};

struct PlayerCameraState {
    u32 action;
    Vec3f pos;
    Vec3s faceAngle;
    Vec3s headRotation;
    s16 headRotationOffset;
    s16 cameraEvent;
    struct Object *usedObj;
};

struct Area {
    s8 index;
    s8 unused;
    u8 terrainType;
    struct Camera *camera;
    s16 *terrainData;
    s8 *surfaceRooms;
    s16 warpNodes;
};

struct SpawnInfo {
    Vec3s startPos;
    Vec3s startAngle;
    s8 areaIndex;
    s8 activeAreaIndex;
    u32 behaviorArg;
    void *behaviorScript;
    struct GraphNode *unk18;
    struct SpawnInfo *next;
};

/* --- Body / render state ------------------------------------------------ */

struct MarioBodyState {
    u32 action;
    s8 capState;
    s8 eyeState;
    s8 handState;
    s8 wingFlutter;
    s16 modelState;
    s8 grabPos;
    u8 punchState;
    Vec3s torsoAngle;
    Vec3s headAngle;
    Vec3f heldObjLastPosition;
};

struct MarioAnimation {
    void *targetAnim;
    u8 padding[16];
};

/* --- MarioState --------------------------------------------------------- */

/*
 * The player.  Read the field groups as: what the controller said this frame
 * (input/intended*), what state machine we are in (action/actionState/
 * actionTimer/actionArg), where we are and how fast (pos/vel/forwardVel/
 * slideVel*), and what we are touching (wall/ceil/floor + their heights).
 *
 * A few fields deserve a note because their behaviour is not guessable:
 *
 *   forwardVel   speed along faceAngle[1]; the authoritative horizontal speed.
 *                vel[0]/vel[2] are *derived* from it each frame, so writing
 *                them directly is overwritten on the next update.
 *   slideVelX/Z  the sliding velocity, which is what actually survives across
 *                frames during slides and air movement.
 *   floorAngle   the downhill direction of the current floor, as an s16 yaw.
 *                Comparing it against faceAngle[1] is how "am I facing
 *                downhill" is decided.
 *   peakHeight   highest Y since the current airborne stretch began; fall
 *                damage reads it.
 *   doubleJumpTimer  counts down after landing and gates the jump chain: land
 *                and re-jump before it expires to escalate to a double or
 *                triple jump.
 */
struct MarioState {
    u16 unk00;
    u16 input;
    u32 flags;
    u32 particleFlags;
    u32 action;
    u32 prevAction;
    u32 terrainSoundAddend;
    u16 actionState;
    u16 actionTimer;
    u32 actionArg;
    f32 intendedMag;
    s16 intendedYaw;
    s16 invincTimer;
    u8 framesSinceA;
    u8 framesSinceB;
    u8 wallKickTimer;
    u8 doubleJumpTimer;
    Vec3s faceAngle;
    Vec3s angleVel;
    s16 slideYaw;
    s16 twirlYaw;
    Vec3f pos;
    Vec3f vel;
    f32 forwardVel;
    f32 slideVelX;
    f32 slideVelZ;
    struct Surface *wall;
    struct Surface *ceil;
    struct Surface *floor;
    f32 ceilHeight;
    f32 floorHeight;
    s16 floorAngle;
    s16 waterLevel;
    struct Object *interactObj;
    struct Object *heldObj;
    struct Object *usedObj;
    struct Object *riddenObj;
    struct Object *marioObj;
    struct SpawnInfo *spawnInfo;
    struct Area *area;
    struct PlayerCameraState *statusForCamera;
    struct MarioBodyState *marioBodyState;
    struct Controller *controller;
    struct MarioAnimation *animation;
    u32 collidedObjInteractTypes;
    s16 numCoins;
    s16 numStars;
    s8 unkB0;
    u8 hurtCounter;
    u8 healCounter;
    u8 squishTimer;
    u16 fadeWarpOpacity;
    u16 capTimer;
    s16 prevNumStarsForDialog;
    f32 peakHeight;
    f32 quicksandDepth;
    f32 unkC4;
};

#endif /* NEW64_TYPES_H */
