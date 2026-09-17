/*
 * new64 -- scripted input implementation and the built-in demo scripts.
 */
#include "input_script.h"

#include "sm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Degrees -> s16 angle. Scripts are written in degrees for legibility. */
static s16 deg_to_angle(f32 deg) {
    return (s16) (s32) (deg * 65536.0f / 360.0f);
}

static u16 parse_buttons(const char *token) {
    u16 buttons = 0;
    const char *p;

    for (p = token; *p != '\0'; p++) {
        switch (*p) {
            case 'A': case 'a': buttons |= A_BUTTON; break;
            case 'B': case 'b': buttons |= B_BUTTON; break;
            case 'Z': case 'z': buttons |= Z_TRIG; break;
            default: break; /* '-' and anything else means no button */
        }
    }
    return buttons;
}

s32 m64_script_parse(struct M64Script *script, const char *text, const char *name) {
    const char *line = text;

    memset(script, 0, sizeof(*script));
    script->camFollow = TRUE;
    snprintf(script->name, sizeof(script->name), "%s", name != NULL ? name : "script");

    while (*line != '\0') {
        char buf[256];
        const char *end = strchr(line, '\n');
        size_t len = (end != NULL) ? (size_t) (end - line) : strlen(line);
        char *hash;
        char directive[32];
        char arg[64];

        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
        }
        memcpy(buf, line, len);
        buf[len] = '\0';
        line += len;
        if (end != NULL) {
            line++;
        }

        hash = strchr(buf, '#');
        if (hash != NULL) {
            *hash = '\0';
        }

        /* Directives first; they are recognised by a leading keyword. */
        if (sscanf(buf, "%31s %63s", directive, arg) == 2) {
            if (strcmp(directive, "warp") == 0) {
                snprintf(script->warpBuf, sizeof(script->warpBuf), "%s", arg);
                script->warpLandmark = script->warpBuf;
                continue;
            }
            if (strcmp(directive, "face") == 0) {
                script->hasFace = TRUE;
                script->faceYaw = deg_to_angle((f32) atof(arg));
                continue;
            }
            if (strcmp(directive, "camyaw") == 0) {
                script->hasCamYaw = TRUE;
                script->camYaw = deg_to_angle((f32) atof(arg));
                continue;
            }
            if (strcmp(directive, "camfollow") == 0) {
                script->camFollow = (atoi(arg) != 0);
                continue;
            }
        }

        /*
         * "mash" expands into alternating press/release spans.
         *
         * This exists because button *edges*, not held state, drive transitions:
         * A_PRESSED fires only on the frame the button goes down.  Chained moves
         * therefore need a fresh press inside a window that is often only four
         * frames wide, and hand-counting frames to hit it makes a script break
         * the moment any timing changes.  Tapping on a short period hits the
         * window regardless -- which is also exactly what a player does.
         */
        {
            s32 frames = 0;
            s32 sx = 0, sy = 0;
            s32 period = 0;
            char buttons[32] = "-";
            s32 flipPeriod = 0;
            s32 fields = sscanf(buf, "mash %d %d %d %31s %d %d", &frames, &sx, &sy,
                                buttons, &period, &flipPeriod);

            if (fields >= 5) {
                s32 remaining = frames;
                s32 elapsed = 0;
                s32 flipped = FALSE;

                if (period < 2) {
                    period = 2;
                }
                while (remaining > 0 && script->spanCount < M64_SCRIPT_MAX_SPANS - 1) {
                    struct M64InputSpan *on = &script->spans[script->spanCount++];
                    s32 offFrames;
                    s16 curX, curY;

                    /*
                     * The optional flip period reverses the stick every N
                     * frames.  It exists for alternating manoeuvres -- climbing
                     * a wall-kick shaft is the motivating case, where facing
                     * flips on every kick and a fixed stick direction would
                     * fight every other one, reversing your drift mid-air
                     * before you ever reach the opposite wall.
                     */
                    if (flipPeriod > 0) {
                        flipped = ((elapsed / flipPeriod) & 1) != 0;
                    }
                    curX = (s16) (flipped ? -sx : sx);
                    curY = (s16) (flipped ? -sy : sy);

                    on->frames = 1;
                    on->stickX = curX;
                    on->stickY = curY;
                    on->buttonDown = parse_buttons(buttons);
                    on->camTurn = 0;
                    script->totalFrames += 1;
                    remaining -= 1;
                    elapsed += 1;

                    offFrames = period - 1;
                    if (offFrames > remaining) {
                        offFrames = remaining;
                    }
                    if (offFrames > 0) {
                        struct M64InputSpan *off = &script->spans[script->spanCount++];

                        off->frames = offFrames;
                        off->stickX = curX;
                        off->stickY = curY;
                        off->buttonDown = 0;
                        off->camTurn = 0;
                        script->totalFrames += offFrames;
                        remaining -= offFrames;
                        elapsed += offFrames;
                    }
                }
                continue;
            }
        }

        {
            s32 frames = 0;
            s32 sx = 0, sy = 0;
            char buttons[32] = "-";
            s32 camTurn = 0;
            s32 fields;

            fields = sscanf(buf, "%d %d %d %31s %d", &frames, &sx, &sy, buttons, &camTurn);
            if (fields >= 3 && frames > 0) {
                if (script->spanCount >= M64_SCRIPT_MAX_SPANS) {
                    continue;
                }
                {
                    struct M64InputSpan *span = &script->spans[script->spanCount++];

                    span->frames = frames;
                    span->stickX = (s16) sx;
                    span->stickY = (s16) sy;
                    span->buttonDown = (fields >= 4) ? parse_buttons(buttons) : 0;
                    span->camTurn = (fields >= 5) ? (s16) camTurn : 0;
                    script->totalFrames += frames;
                }
            }
        }
    }
    return script->spanCount > 0 ? 0 : -1;
}

s32 m64_script_load_file(struct M64Script *script, const char *path) {
    FILE *f = fopen(path, "rb");
    char *text;
    long size;
    s32 result;
    const char *base;

    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return -1;
    }

    text = (char *) malloc((size_t) size + 1);
    if (text == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(text, 1, (size_t) size, f) != (size_t) size) {
        free(text);
        fclose(f);
        return -1;
    }
    text[size] = '\0';
    fclose(f);

    base = strrchr(path, '/');
    result = m64_script_parse(script, text, base != NULL ? base + 1 : path);
    free(text);
    return result;
}

s32 m64_script_input_at(const struct M64Script *script, s32 frame, s16 *stickX,
                        s16 *stickY, u16 *buttonDown, s16 *camTurn) {
    s32 acc = 0;
    s32 i;

    for (i = 0; i < script->spanCount; i++) {
        const struct M64InputSpan *span = &script->spans[i];

        if (frame < acc + span->frames) {
            *stickX = span->stickX;
            *stickY = span->stickY;
            *buttonDown = span->buttonDown;
            *camTurn = span->camTurn;
            return TRUE;
        }
        acc += span->frames;
    }

    *stickX = 0;
    *stickY = 0;
    *buttonDown = 0;
    *camTurn = 0;
    return FALSE;
}

/* --- Built-in scripts --------------------------------------------------- */

/*
 * Each script isolates one mechanic.  Comments give the expected outcome, so a
 * trace can be checked against an intent rather than against a previous run.
 */

static const char sScriptWalk[] =
    "# Ground speed curve: acceleration is 1.1 - fwdVel/43, so speed eases in\n"
    "# and settles near 32 (the walk cap), NOT at the 48 hard cap.\n"
    "# Then the stick is released and braking takes over.\n"
    "face 0\n"
    "camyaw 180\n"
    "60 0 80 -   # hold full forward: watch fwd climb and flatten near 32\n"
    "40 0 0 -    # release: BRAKING (above 16) then DECELERATING then IDLE\n";

static const char sScriptJumpChain[] =
    "# The jump chain: JUMP (42) -> DOUBLE_JUMP (52) -> TRIPLE_JUMP (69).\n"
    "# Each escalation needs a fresh A press during the 4-frame landing window,\n"
    "# so A is tapped every 3 frames rather than on hand-counted frames.\n"
    "# Watch the double jump zero its forward speed and rise almost vertically,\n"
    "# and the triple jump launch at 69 regardless of speed.\n"
    "face 0\n"
    "camyaw 180\n"
    "30 0 80 -        # reach running speed first\n"
    "mash 150 0 80 A 3\n";

static const char sScriptJumpHeight[] =
    "# Variable jump height. Releasing A while still rising faster than 20\n"
    "# divides vertical velocity by four immediately -- so this short tap\n"
    "# reaches far less height than a held jump would.\n"
    "face 0\n"
    "camyaw 180\n"
    "10 0 0 -\n"
    "2  0 0 A    # tap only\n"
    "50 0 0 -\n";

static const char sScriptLongJump[] =
    "# Long jump: crouch-slide (Z while running) then A within 30 frames.\n"
    "# Forward speed is multiplied by 1.5 and gravity is halved, so it covers\n"
    "# far more ground than a running jump despite launching at only 30.\n"
    "warp gap_start\n"
    "face 90        # the gap runs along +X\n"
    "camyaw 270\n"
    "5  0 0 -\n"
    "22 0 80 -      # run up to speed along the platform\n"
    "1  0 80 Z      # Z while running -> crouch slide\n"
    "2  0 80 AZ     # A within 30 frames of the slide -> long jump\n"
    "70 0 80 -\n";

static const char sScriptWallKick[] =
    "# Wall kick. Running into a wall enters ACT_AIR_HIT_WALL, which accepts A\n"
    "# for exactly two frames. A successful kick sets vertical velocity to 52\n"
    "# and flips facing 180 degrees.\n"
    "warp shaft_narrow\n"
    "face 0          # run north into the 300-wide shaft\n"
    "camyaw 180\n"
    "34 0 80 -       # build past 16 forward speed: below that a wall hit is\n"
    "                # just a stop, and no kick is offered at all\n"
    "# A is tapped every 2 frames: the wall kick window is only 2 frames wide,\n"
    "# so a period of 2 is the only way to guarantee landing inside it.\n"
    "mash 90 0 80 A 2\n"
    "40 0 0 -\n";

static const char sScriptBackflip[] =
    "# Backflip: crouch (Z) then A. Launches at 62 with forward speed forced to\n"
    "# -16, and deliberately lacks ACT_FLAG_CONTROL_JUMP_HEIGHT, so releasing A\n"
    "# early does NOT shorten it.\n"
    "face 0\n"
    "camyaw 180\n"
    "10 0 0 Z    # crouch\n"
    "2  0 0 AZ   # backflip\n"
    "60 0 0 -\n";

static const char sScriptSlopes[] =
    "# Slope classes. Running up a 30 degree ramp on default terrain: it is past\n"
    "# the steep threshold, so footing is lost and the slide takes over.\n"
    "warp ramp_30deg\n"
    "face 180\n"
    "camyaw 0\n"
    "90 0 80 -\n"
    "60 0 0 -\n";

static const char sScriptIce[] =
    "# Very slippery ground. Acceleration still applies but deceleration is\n"
    "# scaled to 0.2, so releasing the stick barely slows you.\n"
    "warp ice_patch\n"
    "face 0\n"
    "camyaw 180\n"
    "50 0 80 -\n"
    "60 0 0 -    # note how long this takes to stop compared to grass\n";

static const char sScriptDive[] =
    "# Dive: B above 29 forward speed with the stick pushed past 48. Adds 15 to\n"
    "# forward speed (capped at 48), then becomes DIVE_SLIDE on landing.\n"
    "face 0\n"
    "camyaw 180\n"
    "45 0 80 -\n"
    "2  0 80 B\n"
    "70 0 80 -\n";

static const char sScriptGroundPound[] =
    "# Ground pound: Z in the air. Winds up while rising slightly, then plunges\n"
    "# at a fixed -50 with forward speed zeroed.\n"
    "face 0\n"
    "camyaw 180\n"
    "20 0 80 -\n"
    "2  0 80 A\n"
    "8  0 80 -\n"
    "2  0 0 Z\n"
    "60 0 0 -\n";

static const char sScriptLedgeGrab[] =
    "# Ledge grab. A single jump at a 300-tall face peaks below its top, so the\n"
    "# lip is caught instead of cleared. Then A climbs up.\n"
    "warp ledge_tower\n"
    "face 180\n"
    "camyaw 0\n"
    "18 0 80 -\n"
    "2  0 80 A\n"
    "26 0 80 -\n"
    "2  0 0 A    # climb up\n"
    "40 0 0 -\n";

static const char sScriptTurnaround[] =
    "# Full 180 reversal. Running one way and slamming the stick the other\n"
    "# enters ACT_TURNING_AROUND, which spends the old momentum, hands off to\n"
    "# ACT_FINISH_TURNING_AROUND, and rebuilds speed in the new direction.\n"
    "# Watch vel Z go strongly positive, through zero, then strongly negative.\n"
    "face 0\n"
    "camyaw 180\n"
    "45 0 80 -       # run north up to the 32 speed cap\n"
    "80 0 -80 -      # slam the stick south\n";

static const char sScriptSideFlip[] =
    "# Side flip: press A *during* a turnaround, before the reversal finishes.\n"
    "# Launches at 62, the same height as a backflip, and is the only way to\n"
    "# get that height while already moving. A is tapped every 3 frames so the\n"
    "# press lands inside the turnaround rather than on a hand-counted frame.\n"
    "face 0\n"
    "camyaw 180\n"
    "45 0 80 -       # run north\n"
    "6  0 -80 -      # reverse the stick: enters TURNING_AROUND\n"
    "mash 40 0 -80 A 3\n"
    "50 0 -80 -\n";

static const char sScriptWallShafts[] =
    "# One wall kick inside the 300-wide shaft, from a standing start.\n"
    "#\n"
    "# The two facing walls are perpendicular to X, so a climb means bouncing\n"
    "# ACROSS the gap rather than running along the shaft. A kick needs more\n"
    "# than 16 forward speed at contact, which takes about 170 units of\n"
    "# run-up -- so even this narrow gap is just enough to earn one.\n"
    "#\n"
    "# It stops at one kick on purpose. Chaining them needs the stick pointed\n"
    "# at each wall AS you reach it, and since facing flips on every kick that\n"
    "# means reversing the stick in reaction to each one. A fixed input cannot\n"
    "# do it: hold one direction and air control cancels your drift before you\n"
    "# cross. Climbing a shaft is a thing to feel on a controller, not to\n"
    "# script. Watch instead for the kick itself -- vertical 52, facing flipped\n"
    "# half a turn, about 364 units of height gained from one contact.\n"
    "warp shaft_narrow_in\n"
    "face 90         # face east, at the far wall\n"
    "camyaw 180\n"
    "18 80 0 -       # cross the gap, building past the 16 speed a kick needs\n"
    "mash 40 80 0 A 2\n"
    "60 0 0 -\n";

static const char sScriptTour[] =
    "# The headline run: accelerate to top speed, chain jumps up to a triple,\n"
    "# steer, dive, and brake to a stop. Meant to be watched rather than read.\n"
    "face 0\n"
    "camyaw 180\n"
    "34 0 80 -        # ramp up to the 32 walk cap\n"
    "mash 84 0 80 A 3 # jump chain: single -> double -> triple\n"
    "20 45 70 -       # steer right at speed (ground turn is 0x800/frame)\n"
    "20 0 80 -\n"
    "2  0 80 B        # above 29 speed, B dives\n"
    "46 0 80 -        # dive -> DIVE_SLIDE on landing\n"
    "40 0 0 -         # release: braking, then idle\n";

struct BuiltinScript {
    const char *name;
    const char *text;
};

static const struct BuiltinScript sBuiltins[] = {
    { "walk", sScriptWalk },
    { "jumpchain", sScriptJumpChain },
    { "jumpheight", sScriptJumpHeight },
    { "longjump", sScriptLongJump },
    { "wallkick", sScriptWallKick },
    { "wallshafts", sScriptWallShafts },
    { "turnaround", sScriptTurnaround },
    { "sideflip", sScriptSideFlip },
    { "backflip", sScriptBackflip },
    { "slopes", sScriptSlopes },
    { "ice", sScriptIce },
    { "dive", sScriptDive },
    { "groundpound", sScriptGroundPound },
    { "ledgegrab", sScriptLedgeGrab },
    { "tour", sScriptTour },
};

s32 m64_script_builtin_count(void) {
    return (s32) (sizeof(sBuiltins) / sizeof(sBuiltins[0]));
}

const char *m64_script_builtin_name(s32 index) {
    if (index < 0 || index >= m64_script_builtin_count()) {
        return NULL;
    }
    return sBuiltins[index].name;
}

const char *m64_script_builtin_text(s32 index) {
    if (index < 0 || index >= m64_script_builtin_count()) {
        return NULL;
    }
    return sBuiltins[index].text;
}

const char *m64_script_builtin_find(const char *name) {
    s32 i;

    for (i = 0; i < m64_script_builtin_count(); i++) {
        if (strcmp(sBuiltins[i].name, name) == 0) {
            return sBuiltins[i].text;
        }
    }
    return NULL;
}
