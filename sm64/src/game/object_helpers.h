/* new64 -- object helpers reachable from movement code. The demo has no
 * behaviour-driven objects, so these are minimal. */
#ifndef NEW64_OBJECT_HELPERS_H
#define NEW64_OBJECT_HELPERS_H

#include "types.h"

void obj_set_gfx_pos_at_obj_pos(struct Object *obj1, struct Object *obj2);
struct Object *spawn_object(struct Object *parent, s32 model, const void *behavior);
void cur_obj_play_sound_2(s32 soundMagic);
s32 is_point_within_radius_of_mario(f32 x, f32 y, f32 z, s32 dist);
void set_object_visibility(struct Object *obj, s32 dist);

#endif /* NEW64_OBJECT_HELPERS_H */
