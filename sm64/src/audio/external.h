/*
 * new64 -- audio requests. Recorded, never rendered; see audio_defines.h for
 * why recording them is useful.
 */
#ifndef NEW64_AUDIO_EXTERNAL_H
#define NEW64_AUDIO_EXTERNAL_H

#include "types.h"

void play_sound(s32 soundBits, f32 *pos);
void play_sound_with_freq_scale(s32 soundBits, f32 *pos, f32 freqScale);
void stop_sound(u32 soundBits, f32 *pos);
void stop_sounds_from_source(f32 *pos);
void play_sequence(u8 player, u8 seqId, u16 fadeTimer);
void seq_player_fade_out(u8 player, u16 fadeTimer);
void set_sound_moving_speed(u8 bank, u8 speed);
void func_80320A4C(u8 bankIndex, u8 arg1); /* legacy spelling some sources use */

/* --- new64 additions: the recorded cue log ------------------------------ */

#define M64_SOUND_LOG_LEN 32

struct M64SoundEvent {
    s32 soundBits;
    s32 frame;
};

void m64_sound_log_begin_frame(s32 frame);
s32 m64_sound_log_count(void);
const struct M64SoundEvent *m64_sound_log_at(s32 index);
/* Human-readable name for a cue id, for traces and the HUD. */
const char *m64_sound_name(s32 soundBits);

#endif /* NEW64_AUDIO_EXTERNAL_H */
