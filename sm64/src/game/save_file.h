/* new64 -- save data. No persistence; queries answer "fresh file, nothing
 * collected", which is the state the demo wants anyway. */
#ifndef NEW64_SAVE_FILE_H
#define NEW64_SAVE_FILE_H

#include "types.h"

#define SAVE_FLAG_HAVE_WING_CAP   (1 <<  3)
#define SAVE_FLAG_HAVE_METAL_CAP  (1 <<  4)
#define SAVE_FLAG_HAVE_VANISH_CAP (1 <<  5)
#define SAVE_FLAG_CAP_ON_GROUND   (1 << 16)
#define SAVE_FLAG_CAP_ON_KLEPTO   (1 << 17)
#define SAVE_FLAG_CAP_ON_UKIKI    (1 << 18)
#define SAVE_FLAG_CAP_ON_MR_BLIZZARD (1 << 19)

u32 save_file_get_flags(void);
void save_file_set_flags(u32 flags);
void save_file_clear_flags(u32 flags);
s32 save_file_get_total_star_count(s32 fileIndex, s32 minCourse, s32 maxCourse);
s32 save_file_get_course_star_count(s32 fileIndex, s32 courseIndex);
u32 save_file_get_star_flags(s32 fileIndex, s32 courseIndex);

#endif /* NEW64_SAVE_FILE_H */
