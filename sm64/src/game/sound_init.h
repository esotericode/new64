/* new64 -- music/ambience hooks, all no-ops. */
#ifndef NEW64_SOUND_INIT_H
#define NEW64_SOUND_INIT_H

#include "types.h"

void play_infinite_stairs_music(void);
void set_background_music(u16 a, u16 seqArgs, s16 fadeTimer);
void fadeout_music(s16 fadeOutTime);
void play_menu_sounds(s16 soundMenuFlags);
void play_painting_eject_sound(void);
void play_shell_music(void);
void stop_shell_music(void);
void play_cap_music(u16 seqArgs);
void stop_cap_music(void);
void fadeout_cap_music(void);

#endif /* NEW64_SOUND_INIT_H */
