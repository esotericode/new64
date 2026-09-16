/* new64 -- area/level container. Only terrainType and the camera matter to
 * movement code: terrain type picks footstep sounds, camera yaw steers input. */
#ifndef NEW64_AREA_H
#define NEW64_AREA_H

#include "types.h"

extern struct Area *gCurrentArea;
extern s16 gCurrCourseNum;
extern s16 gCurrAreaIndex;
extern s16 gCurrLevelNum;

#endif /* NEW64_AREA_H */
