/*
 * new64 -- audio shim.
 *
 * There is no audio engine.  Instead of discarding sound requests, each one is
 * appended to a per-frame ring buffer.  This makes the state machine's sound
 * calls observable, which is genuinely useful: "SOUND_ACTION_TERRAIN_LANDING
 * fired on frame 41" is a far more legible assertion about landing detection
 * than any velocity comparison, and it is exactly the kind of thing that
 * regresses silently when collision changes.
 */
#include "audio/external.h"
#include "audio_defines.h"
#include "types.h"

#include <stddef.h>

static struct M64SoundEvent sLog[M64_SOUND_LOG_LEN];
static s32 sLogCount;
static s32 sFrame;

void m64_sound_log_begin_frame(s32 frame) {
    sFrame = frame;
    sLogCount = 0;
}

s32 m64_sound_log_count(void) {
    return sLogCount;
}

const struct M64SoundEvent *m64_sound_log_at(s32 index) {
    if (index < 0 || index >= sLogCount) {
        return NULL;
    }
    return &sLog[index];
}

void play_sound(s32 soundBits, f32 *pos) {
    (void) pos;
    if (sLogCount < M64_SOUND_LOG_LEN) {
        sLog[sLogCount].soundBits = soundBits;
        sLog[sLogCount].frame = sFrame;
        sLogCount++;
    }
}

void play_sound_with_freq_scale(s32 soundBits, f32 *pos, f32 freqScale) {
    (void) freqScale;
    play_sound(soundBits, pos);
}

void stop_sound(u32 soundBits, f32 *pos) { (void) soundBits; (void) pos; }
void stop_sounds_from_source(f32 *pos) { (void) pos; }
void play_sequence(u8 player, u8 seqId, u16 fadeTimer) {
    (void) player; (void) seqId; (void) fadeTimer;
}
void seq_player_fade_out(u8 player, u16 fadeTimer) { (void) player; (void) fadeTimer; }
void set_sound_moving_speed(u8 bank, u8 speed) { (void) bank; (void) speed; }
void func_80320A4C(u8 bankIndex, u8 arg1) { (void) bankIndex; (void) arg1; }

/*
 * Names for the trace.  Only the cues movement code raises are worth naming;
 * anything else prints as its raw id, which is enough to look up.
 */
const char *m64_sound_name(s32 soundBits) {
    switch (soundBits) {
        case SOUND_ACTION_TERRAIN_STEP:            return "STEP";
        case SOUND_ACTION_TERRAIN_JUMP:            return "JUMP";
        case SOUND_ACTION_TERRAIN_LANDING:         return "LANDING";
        case SOUND_ACTION_TERRAIN_HEAVY_LANDING:   return "HEAVY_LANDING";
        case SOUND_ACTION_TERRAIN_BODY_HIT_GROUND: return "BODY_HIT_GROUND";
        case SOUND_ACTION_BONK:                    return "BONK";
        case SOUND_ACTION_SIDE_FLIP_UNK:           return "SIDE_FLIP";
        case SOUND_ACTION_SPIN:                    return "SPIN";
        case SOUND_ACTION_TWIRL:                   return "TWIRL";
        case SOUND_ACTION_HIT:                     return "HIT";
        case SOUND_ACTION_THROW:                   return "THROW";
        case SOUND_ACTION_CLIMB_UP_POLE:           return "CLIMB_POLE";
        case SOUND_ACTION_BRUSH_HAIR:              return "BRUSH_HAIR";
        case SOUND_MARIO_YAH_WAH_HOO:              return "VOX_JUMP";
        case SOUND_MARIO_HOOHOO:                   return "VOX_HOOHOO";
        case SOUND_MARIO_YAHOO:                    return "VOX_YAHOO";
        case SOUND_MARIO_UH:                       return "VOX_UH";
        case SOUND_MARIO_HRMM:                     return "VOX_HRMM";
        case SOUND_MARIO_WAH2:                     return "VOX_WAH";
        case SOUND_MARIO_WHOA:                     return "VOX_WHOA";
        case SOUND_MARIO_EEUH:                     return "VOX_EEUH";
        case SOUND_MARIO_OOOF:                     return "VOX_OOOF";
        case SOUND_MARIO_OOOF2:                    return "VOX_OOOF2";
        case SOUND_MARIO_HERE_WE_GO:               return "VOX_HERE_WE_GO";
        case SOUND_MARIO_PUNCH_YAH:                return "VOX_PUNCH_YAH";
        case SOUND_MARIO_PUNCH_HOO:                return "VOX_PUNCH_HOO";
        case SOUND_MARIO_GROUND_POUND_WAH:         return "VOX_POUND";
        case SOUND_MARIO_HAHA:                     return "VOX_HAHA";
        case SOUND_MARIO_DOH:                      return "VOX_DOH";
        case SOUND_MARIO_PANTING:                  return "VOX_PANTING";
        case SOUND_MARIO_YAHOO_WAHA_YIPPEE:        return "VOX_TRIPLE";
        case SOUND_MARIO_IMA_TIRED:                return "VOX_TIRED";
        case SOUND_MARIO_YAWNING:                  return "VOX_YAWN";
        case SOUND_ENV_SLIDING:                    return "ENV_SLIDING";
        case SOUND_MOVING_TERRAIN_SLIDE:           return "MOV_SLIDE";
        case SOUND_MOVING_LAVA_BURN:               return "MOV_BURN";
        default:                                   return NULL;
    }
}
