/*
 * new64 -- sound cue identifiers.
 *
 * new64 has no audio engine.  Rather than #define these to nothing, each cue
 * gets a distinct id and play_sound() records it in a ring buffer (see
 * sm64/src/shims/audio_stub.c).  That turns the sound calls into a free
 * verification signal: the demo trace shows which cue fired on which frame, so
 * you can confirm a landing or a wall kick was recognised on exactly the frame
 * you expected -- which is much easier to eyeball than a velocity graph.
 *
 * The ids are opaque. Only their distinctness matters.
 */
#ifndef NEW64_AUDIO_DEFINES_H
#define NEW64_AUDIO_DEFINES_H

#define SOUND_NO_FREQUENCY_LOSS 0

/* The terrain-dependent cues take an addend derived from the floor type, so
 * they must occupy reserved ranges rather than single values. */
#define SOUND_ACTION_TERRAIN_STEP             0x0100
#define SOUND_ACTION_TERRAIN_JUMP             0x0110
#define SOUND_ACTION_TERRAIN_LANDING          0x0120
#define SOUND_ACTION_TERRAIN_STUCK_IN_GROUND  0x0130
#define SOUND_ACTION_TERRAIN_HEAVY_LANDING    0x0140
#define SOUND_ACTION_TERRAIN_BODY_HIT_GROUND  0x0150

#define SOUND_ACTION_UNKNOWN430      0x0200
#define SOUND_ACTION_BONK            0x0201
#define SOUND_ACTION_SPIN            0x0202
#define SOUND_ACTION_THROW           0x0203
#define SOUND_ACTION_KEY_SWISH       0x0204
#define SOUND_ACTION_CLIMB_UP_TREE   0x0205
#define SOUND_ACTION_CLIMB_DOWN_TREE 0x0206
#define SOUND_ACTION_UNK45           0x0207
#define SOUND_ACTION_UNKSTAR         0x0208
#define SOUND_ACTION_HIT             0x0209
#define SOUND_ACTION_HIT_2           0x020A
#define SOUND_ACTION_HIT_3           0x020B
#define SOUND_ACTION_BRUSH_HAIR      0x020C
#define SOUND_ACTION_CLIMB_UP_POLE   0x020D
#define SOUND_ACTION_METAL_STEP      0x020E
#define SOUND_ACTION_METAL_HEAVY_LANDING 0x020F
#define SOUND_ACTION_CLAP_HANDS_COLD 0x0210
#define SOUND_ACTION_SIDE_FLIP_UNK   0x0211
#define SOUND_ACTION_TWIRL           0x0212
#define SOUND_ACTION_METAL_JUMP      0x0214
#define SOUND_ACTION_METAL_LANDING   0x0215
#define SOUND_ACTION_SWIM            0x0216
#define SOUND_ACTION_UNKNOWN418      0x0217
#define SOUND_ACTION_SHRINK_INTO_BBH 0x0218
#define SOUND_ACTION_SWIM_FAST       0x0219
#define SOUND_ACTION_METAL_JUMP_WATER 0x021A
#define SOUND_ACTION_METAL_LAND_WATER 0x021B
#define SOUND_ACTION_METAL_STEP_WATER 0x021C
#define SOUND_ACTION_FLYING_FAST     0x021D
#define SOUND_ACTION_TELEPORT        0x021E
#define SOUND_ACTION_INTRO_UNK45E    0x021F
#define SOUND_ACTION_PAT_BACK        0x0220
#define SOUND_ACTION_UNSTUCK_FROM_GROUND 0x0221
#define SOUND_ACTION_QUICKSAND_STEP  0x0222
#define SOUND_ACTION_METAL_STEP_TIPTOE 0x0223
#define SOUND_ACTION_READ_SIGN       0x0224
#define SOUND_ACTION_METAL_BONK      0x0225
#define SOUND_ACTION_UNKNOWN45C      0x0226

/* Vocal cues.  new64 plays nothing, so these are labels for the trace. */
#define SOUND_MARIO_YAH_WAH_HOO      0x0300
#define SOUND_MARIO_HOOHOO           0x0301
#define SOUND_MARIO_YAHOO            0x0302
#define SOUND_MARIO_UH               0x0303
#define SOUND_MARIO_HRMM             0x0304
#define SOUND_MARIO_WAH2             0x0305
#define SOUND_MARIO_WHOA             0x0306
#define SOUND_MARIO_EEUH             0x0307
#define SOUND_MARIO_ATTACKED         0x0308
#define SOUND_MARIO_OOOF             0x0309
#define SOUND_MARIO_OOOF2            0x030A
#define SOUND_MARIO_HERE_WE_GO       0x030B
#define SOUND_MARIO_YAWNING          0x030C
#define SOUND_MARIO_SNORING1         0x030D
#define SOUND_MARIO_SNORING2         0x030E
#define SOUND_MARIO_WAAAOOOW         0x030F
#define SOUND_MARIO_HAHA             0x0310
#define SOUND_MARIO_HAHA_2           0x0311
#define SOUND_MARIO_UH2              0x0312
#define SOUND_MARIO_UH2_2            0x0313
#define SOUND_MARIO_ON_FIRE          0x0314
#define SOUND_MARIO_DYING            0x0315
#define SOUND_MARIO_PANTING_COLD     0x0316
#define SOUND_MARIO_PANTING          0x0318
#define SOUND_MARIO_COUGHING1        0x0319
#define SOUND_MARIO_COUGHING2        0x031A
#define SOUND_MARIO_COUGHING3        0x031B
#define SOUND_MARIO_PUNCH_YAH        0x031C
#define SOUND_MARIO_PUNCH_HOO        0x031D
#define SOUND_MARIO_MAMA_MIA         0x031E
#define SOUND_MARIO_GROUND_POUND_WAH 0x031F
#define SOUND_MARIO_DROWNING         0x0320
#define SOUND_MARIO_IMA_TIRED        0x0321
#define SOUND_MARIO_SNORING3         0x0322
#define SOUND_MARIO_SO_LONGA_BOWSER  0x0323
#define SOUND_MARIO_IMA_TIRED_2      0x0324
#define SOUND_MARIO_DOH              0x0325
#define SOUND_MARIO_GAME_OVER        0x0326
#define SOUND_MARIO_HELLO            0x0327
#define SOUND_MARIO_PRESS_START_TO_PLAY 0x0328
#define SOUND_MARIO_TWIRL_BOUNCE     0x0329
#define SOUND_MARIO_OKEY_DOKEY       0x032A
#define SOUND_MARIO_LETS_A_GO        0x032B
#define SOUND_MARIO_YAHOO_WAHA_YIPPEE 0x032C

#define SOUND_GENERAL_QUIET_POUND1   0x0400
#define SOUND_GENERAL_BOING1         0x0401
#define SOUND_GENERAL_FLAME_OUT      0x0402
#define SOUND_GENERAL_SOFT_LANDING   0x0403
#define SOUND_GENERAL_UNKNOWN1       0x0404

#define SOUND_ENV_WATER              0x0500
#define SOUND_ENV_UNKNOWN2           0x0501
#define SOUND_ENV_WIND1              0x0502
#define SOUND_ENV_WIND2              0x0503
#define SOUND_ENV_METAL_BOX_PUSH     0x0504
#define SOUND_ENV_SINK_QUICKSAND     0x0505
#define SOUND_ENV_DRAINING_WATER     0x0506
#define SOUND_ENV_SLIDING            0x0507
#define SOUND_ENV_STAR               0x0508
#define SOUND_ENV_MOVING_SAND_SNOW   0x0509

#define SOUND_MOVING_TERRAIN_SLIDE   0x0600
#define SOUND_MOVING_LAVA_BURN       0x0601
#define SOUND_MOVING_SLIDE_DOWN_POLE 0x0602
#define SOUND_MOVING_SLIDE_DOWN_TREE 0x0603
#define SOUND_MOVING_QUICKSAND_DEATH 0x0604
#define SOUND_MOVING_ALMOST_SHOCKED  0x0605
#define SOUND_MOVING_AIM_CANNON      0x0606
#define SOUND_MOVING_UNK1AFTERCANNON 0x0607
#define SOUND_MOVING_FLYING          0x0608

#endif /* NEW64_AUDIO_DEFINES_H */
