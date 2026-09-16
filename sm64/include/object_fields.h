/*
 * new64 -- object scratch-field aliases.
 *
 * Objects in the N64 titles carry an untyped scratch array reinterpreted per
 * behaviour through macros like these.  new64 keeps the indirection because
 * hosted code uses these names; only the fields movement code touches are
 * defined.
 */
#ifndef NEW64_OBJECT_FIELDS_H
#define NEW64_OBJECT_FIELDS_H

/* Graphics-node passthroughs. */
#define oPosX             header.gfx.pos[0]
#define oPosY             header.gfx.pos[1]
#define oPosZ             header.gfx.pos[2]
#define oFaceAnglePitch   header.gfx.angle[0]
#define oFaceAngleYaw     header.gfx.angle[1]
#define oFaceAngleRoll    header.gfx.angle[2]
#define oAnimState        rawData.asS32[0x38]

/* Generic behaviour slots. */
#define oAction           rawData.asS32[0x0F]
#define oPrevAction       rawData.asS32[0x10]
#define oSubAction        rawData.asS32[0x11]
#define oTimer            rawData.asS32[0x14]
#define oMoveAngleYaw     rawData.asS32[0x10]
#define oVelX             rawData.asF32[0x07]
#define oVelY             rawData.asF32[0x08]
#define oVelZ             rawData.asF32[0x09]
#define oForwardVel       rawData.asF32[0x0A]
#define oInteractStatus   rawData.asS32[0x25]
#define oInteractType     rawData.asU32[0x27]
#define oDamageOrCoinValue rawData.asS32[0x28]
#define oIntangibleTimer  rawData.asS32[0x29]
#define oHeldState        rawData.asS32[0x2A]

/* Player-object slots used by pole and swim code. */
#define oMarioPolePos        rawData.asF32[0x30]
#define oMarioPoleYawVel     rawData.asS32[0x31]
#define oMarioPoleUnk108     rawData.asF32[0x32]
#define oMarioWalkingPitch   rawData.asS32[0x33]
#define oMarioLongJumpIsSlow rawData.asU32[0x34]
#define oMarioBurnTimer      rawData.asS32[0x35]

#endif /* NEW64_OBJECT_FIELDS_H */
