/*
 * new64 -- scripted input.
 *
 * Deterministic per-frame input is what makes this project testable.  A script
 * is a list of held-input spans plus a few setup directives, so a movement
 * behaviour can be described exactly ("hold forward 40 frames, tap A, hold
 * forward 30 more") and replayed bit-identically.
 *
 * Directives (one per line, '#' starts a comment):
 *
 *   warp <landmark>          place the player at a named level landmark
 *   face <degrees>           set facing yaw, 0 = +Z, degrees clockwise
 *   camyaw <degrees>         set camera yaw directly
 *   camfollow <0|1>          enable/disable camera auto-follow
 *   <frames> <sx> <sy> <buttons> [camturn]
 *                            hold this input for <frames> ticks
 *   mash <frames> <sx> <sy> <buttons> <period>
 *                            tap the buttons for one frame every <period>
 *                            frames, for <frames> total. Chained moves need a
 *                            button *edge* inside a window that can be only a
 *                            few frames wide, so tapping is far more robust
 *                            than hand-counted frame numbers.
 *
 * Stick values are the raw hardware range, -80..80, with +Y meaning "away from
 * the camera".  Buttons is any combination of the letters A, B, Z (or '-' for
 * none).  camturn is an optional per-frame camera rotation in s16 angle units.
 */
#ifndef NEW64_INPUT_SCRIPT_H
#define NEW64_INPUT_SCRIPT_H

#include "m64_types.h"

#define M64_SCRIPT_MAX_SPANS 256

struct M64InputSpan {
    s32 frames;
    s16 stickX;
    s16 stickY;
    u16 buttonDown;
    s16 camTurn;
};

struct M64Script {
    char name[64];
    const char *warpLandmark; /* NULL for the default spawn */
    char warpBuf[64];
    s32 hasFace;
    s16 faceYaw;
    s32 hasCamYaw;
    s16 camYaw;
    s32 camFollow;

    struct M64InputSpan spans[M64_SCRIPT_MAX_SPANS];
    s32 spanCount;
    s32 totalFrames;
};

/* Parse from a string (used for both files and the built-in scripts). */
s32 m64_script_parse(struct M64Script *script, const char *text, const char *name);
s32 m64_script_load_file(struct M64Script *script, const char *path);

/* Input for a given absolute frame; returns FALSE once the script has ended. */
s32 m64_script_input_at(const struct M64Script *script, s32 frame, s16 *stickX,
                        s16 *stickY, u16 *buttonDown, s16 *camTurn);

/* Built-in scripts, so the demo runs with no external files. */
s32 m64_script_builtin_count(void);
const char *m64_script_builtin_name(s32 index);
const char *m64_script_builtin_text(s32 index);
const char *m64_script_builtin_find(const char *name);

#endif /* NEW64_INPUT_SCRIPT_H */
