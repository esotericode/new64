/*
 * new64 -- headless demo runner.
 *
 * Runs a scripted input sequence against the movement code and emits:
 *   - a per-frame state trace (stdout or a file)
 *   - rendered PNG stills at chosen frames
 *   - an animated GIF of the whole run
 *
 * This is the primary way to review the engine: movement is a time-domain thing
 * and cannot be judged from a static screenshot, but it also cannot be judged
 * reliably by eye alone, so the trace and the animation are produced together
 * from the same run.
 */
#include "demo_app.h"
#include "input_script.h"

#include "engine/math_util.h"
#include "m64_image.h"
#include "m64_level.h"
#include "m64_render.h"
#include "mario.h"
#include "sm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Directory creation is the one place this runner touches the platform.
 * It was previously a system("mkdir -p") shell-out, which is POSIX-only and
 * also hands a user-supplied path to a shell -- neither acceptable once this
 * builds for Windows.
 */
#ifdef _WIN32
#include <direct.h>
#define m64_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define m64_mkdir(path) mkdir((path), 0777)
#endif

struct Options {
    const char *scriptName;
    const char *scriptFile;
    const char *outDir;
    const char *tracePath;
    s32 width;
    s32 height;
    s32 extraFrames;
    s32 writeGif;
    s32 writePngs;
    s32 pngStride;
    s32 listScripts;
    s32 quiet;
    const char *animTable;
};

static void usage(const char *argv0) {
    s32 i;

    printf("new64 headless demo runner\n\n");
    printf("usage: %s [options]\n\n", argv0);
    printf("  --script NAME     run a built-in script (default: tour)\n");
    printf("  --file PATH       run a script from a file instead\n");
    printf("  --out DIR         output directory (default: out)\n");
    printf("  --trace PATH      write the state trace to a file as well\n");
    printf("  --size WxH        render size (default: 640x360)\n");
    printf("  --tail N          extra frames to run after the script ends\n");
    printf("  --gif / --no-gif  write out.gif (default: on)\n");
    printf("  --png / --no-png  write frame stills (default: on)\n");
    printf("  --png-stride N    write every Nth frame as a PNG (default: 10)\n");
    printf("  --anims PATH      load animation frame counts (see assets/)\n");
    printf("  --list            list the built-in scripts\n");
    printf("  --quiet           do not print the trace to stdout\n");
    printf("\nbuilt-in scripts:\n");
    for (i = 0; i < m64_script_builtin_count(); i++) {
        printf("  %s\n", m64_script_builtin_name(i));
    }
}

static s32 parse_options(struct Options *o, s32 argc, char **argv) {
    s32 i;

    o->scriptName = "tour";
    o->scriptFile = NULL;
    o->outDir = "out";
    o->tracePath = NULL;
    o->width = 640;
    o->height = 360;
    o->extraFrames = 20;
    o->writeGif = TRUE;
    o->writePngs = TRUE;
    o->pngStride = 10;
    o->listScripts = FALSE;
    o->quiet = FALSE;
    o->animTable = NULL;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return 1;
        } else if (strcmp(a, "--list") == 0) {
            o->listScripts = TRUE;
        } else if (strcmp(a, "--script") == 0 && i + 1 < argc) {
            o->scriptName = argv[++i];
        } else if (strcmp(a, "--file") == 0 && i + 1 < argc) {
            o->scriptFile = argv[++i];
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            o->outDir = argv[++i];
        } else if (strcmp(a, "--trace") == 0 && i + 1 < argc) {
            o->tracePath = argv[++i];
        } else if (strcmp(a, "--size") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &o->width, &o->height) != 2) {
                fprintf(stderr, "new64: bad --size (want WxH)\n");
                return -1;
            }
        } else if (strcmp(a, "--tail") == 0 && i + 1 < argc) {
            o->extraFrames = atoi(argv[++i]);
        } else if (strcmp(a, "--gif") == 0) {
            o->writeGif = TRUE;
        } else if (strcmp(a, "--no-gif") == 0) {
            o->writeGif = FALSE;
        } else if (strcmp(a, "--png") == 0) {
            o->writePngs = TRUE;
        } else if (strcmp(a, "--no-png") == 0) {
            o->writePngs = FALSE;
        } else if (strcmp(a, "--png-stride") == 0 && i + 1 < argc) {
            o->pngStride = atoi(argv[++i]);
            if (o->pngStride < 1) {
                o->pngStride = 1;
            }
        } else if (strcmp(a, "--anims") == 0 && i + 1 < argc) {
            o->animTable = argv[++i];
        } else if (strcmp(a, "--quiet") == 0) {
            o->quiet = TRUE;
        } else {
            fprintf(stderr, "new64: unknown option '%s' (try --help)\n", a);
            return -1;
        }
    }
    return 0;
}

/*
 * Create a directory and any missing parents, portably.
 *
 * Walks the path creating each component in turn, which is what made the
 * original shell-out convenient; doing it directly avoids both the POSIX
 * dependency and passing a path through a shell.
 */
static void ensure_directory(const char *dir) {
    char buf[512];
    size_t len;
    size_t i;

    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    len = strlen(dir);
    if (len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, dir, len + 1);

    /* Drop a trailing separator so the final component is created by the
     * call after the loop rather than being missed. */
    while (len > 1 && (buf[len - 1] == '/' || buf[len - 1] == '\\')) {
        buf[--len] = '\0';
    }

    for (i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];

            buf[i] = '\0';
            m64_mkdir(buf);
            buf[i] = saved;
        }
    }
    m64_mkdir(buf);
}

/* The live state readout drawn over each frame. */
static void draw_hud(struct M64Framebuffer *fb, const struct M64World *w,
                     const struct M64Script *script, s32 frame) {
    const struct MarioState *m = &w->marioState;
    char line[160];
    s32 y = 6;

    m64_render_rect_blend(fb, 0, 0, fb->width, 62, 0x000000, 150);

    snprintf(line, sizeof(line), "NEW64  %s  FRAME %d", script->name, frame);
    m64_render_text(fb, 6, y, 1, 0xFFFFFFFF, line);
    y += 10;

    snprintf(line, sizeof(line), "ACTION %s", m64_action_name(m->action));
    m64_render_text(fb, 6, y, 1, 0xFF90FF90, line);
    y += 10;

    snprintf(line, sizeof(line), "FWD %6.2f  VELY %7.2f  YAW %6d", m->forwardVel,
             m->vel[1], m->faceAngle[1]);
    m64_render_text(fb, 6, y, 1, 0xFFFFE080, line);
    y += 10;

    snprintf(line, sizeof(line), "POS %7.0f %7.0f %7.0f  FLOOR %6.0f T%d", m->pos[0],
             m->pos[1], m->pos[2], m->floorHeight,
             m->floor != NULL ? m->floor->type : -1);
    m64_render_text(fb, 6, y, 1, 0xFFB0D0FF, line);
    y += 10;

    /* Live input echo, so the animation shows what was pressed. */
    snprintf(line, sizeof(line), "STICK %4d %4d   %s%s%s", w->controller.rawStickX,
             w->controller.rawStickY,
             (w->controller.buttonDown & A_BUTTON) ? "A " : "- ",
             (w->controller.buttonDown & B_BUTTON) ? "B " : "- ",
             (w->controller.buttonDown & Z_TRIG) ? "Z" : "-");
    m64_render_text(fb, 6, y, 1, 0xFFFFFFFF, line);
}

static void render_frame(struct M64Framebuffer *fb, struct M64World *w,
                         const struct M64Script *script, s32 frame) {
    const struct MarioState *m = &w->marioState;
    Vec3f velEnd;

    m64_fb_clear_sky(fb);
    m64_render_set_camera(fb, w->viewCam.pos, w->viewCam.focus, 55.0f);
    m64_render_level(fb);

    /* Body dimensions are the collision dimensions: 160 tall. */
    m64_render_capsule(fb, m->pos, m->faceAngle[1], 45.0f, 160.0f, 0xE04040);

    /* Velocity vector, scaled up so it is legible at walking speeds. */
    velEnd[0] = m->pos[0] + m->vel[0] * 4.0f;
    velEnd[1] = m->pos[1] + 80.0f + m->vel[1] * 4.0f;
    velEnd[2] = m->pos[2] + m->vel[2] * 4.0f;
    {
        Vec3f from = { m->pos[0], m->pos[1] + 80.0f, m->pos[2] };

        m64_render_line(fb, from, velEnd, 0xFFFFFF00);
    }

    draw_hud(fb, w, script, frame);
}

int main(int argc, char **argv) {
    struct Options opt;
    struct M64Script script;
    struct M64World world;
    struct M64Framebuffer fb;
    struct M64Gif *gif = NULL;
    FILE *traceFile = NULL;
    char path[512];
    s32 totalFrames;
    s32 frame;
    s32 parsed;

    parsed = parse_options(&opt, argc, argv);
    if (parsed != 0) {
        return parsed > 0 ? 0 : 1;
    }

    if (opt.listScripts) {
        s32 i;

        for (i = 0; i < m64_script_builtin_count(); i++) {
            printf("%s\n", m64_script_builtin_name(i));
        }
        return 0;
    }

    if (opt.scriptFile != NULL) {
        if (m64_script_load_file(&script, opt.scriptFile) != 0) {
            fprintf(stderr, "new64: could not read script '%s'\n", opt.scriptFile);
            return 1;
        }
    } else {
        const char *text = m64_script_builtin_find(opt.scriptName);

        if (text == NULL) {
            fprintf(stderr, "new64: no built-in script '%s' (try --list)\n",
                    opt.scriptName);
            return 1;
        }
        if (m64_script_parse(&script, text, opt.scriptName) != 0) {
            fprintf(stderr, "new64: built-in script '%s' is empty\n", opt.scriptName);
            return 1;
        }
    }

    /*
     * Animation lengths must be set before init_mario(), because some actions
     * end when their animation does. Without a table the built-in defaults
     * apply -- see docs/SLOTTING_IN_DECOMP.md.
     */
    if (opt.animTable != NULL) {
        s32 applied = m64_anim_load_table(opt.animTable);

        if (applied < 0) {
            fprintf(stderr, "new64: could not read animation table '%s'\n",
                    opt.animTable);
        } else {
            printf("# loaded %d animation lengths from %s\n", applied, opt.animTable);
        }
    }

    m64_world_init(&world);

    /* Apply the script's setup directives. */
    if (script.warpLandmark != NULL) {
        const struct M64Landmark *lm = m64_level_landmark_by_name(script.warpLandmark);

        if (lm == NULL) {
            fprintf(stderr, "new64: unknown landmark '%s'\n", script.warpLandmark);
            return 1;
        }
        m64_vec3f_copy(world.marioState.pos, lm->pos);
        m64_vec3f_copy(world.marioObj.header.gfx.pos, lm->pos);
    }
    if (script.hasFace) {
        world.marioState.faceAngle[1] = script.faceYaw;
    }
    /* Re-init so the player settles onto the floor at the warped position. */
    init_mario();
    if (script.hasCamYaw) {
        world.camera.yaw = script.camYaw;
        m64_camera_init(&world.viewCam, world.marioState.pos, script.camYaw);
    } else {
        m64_camera_init(&world.viewCam, world.marioState.pos,
                        (s16) (world.marioState.faceAngle[1] + 0x8000));
    }

    if (m64_fb_create(&fb, opt.width, opt.height) != 0) {
        fprintf(stderr, "new64: could not allocate a %dx%d framebuffer\n", opt.width,
                opt.height);
        return 1;
    }

    /* Create the output directory. Failure is non-fatal and usually just means
     * it already exists; a genuine problem surfaces when a file write fails. */
    ensure_directory(opt.outDir);

    if (opt.tracePath != NULL) {
        /*
         * Binary mode deliberately: on Windows, text mode would translate \n to
         * \r\n and the trace would no longer be byte-comparable against one
         * produced on another platform. Diffing traces across platforms (and
         * before/after a change) is the main way this engine is verified, so
         * the output must be identical everywhere.
         */
        traceFile = fopen(opt.tracePath, "wb");
        if (traceFile == NULL) {
            fprintf(stderr, "new64: could not open trace '%s'\n", opt.tracePath);
        }
    }

    if (opt.writeGif) {
        snprintf(path, sizeof(path), "%s/%s.gif", opt.outDir, script.name);
        /* 30fps source; GIF delay is in centiseconds, so 3 is the closest. */
        gif = m64_gif_begin(path, opt.width, opt.height, 3);
        if (gif == NULL) {
            fprintf(stderr, "new64: could not open '%s' for writing\n", path);
        }
    }

    totalFrames = script.totalFrames + opt.extraFrames;

    if (!opt.quiet) {
        printf("# new64 trace: script '%s', %d frames\n", script.name, totalFrames);
        printf("# frame action               pos                          "
               "vel                        fwd     yaw    floor\n");
    }

    for (frame = 0; frame < totalFrames; frame++) {
        s16 stickX, stickY, camTurn;
        u16 buttons;
        char line[512];

        m64_script_input_at(&script, frame, &stickX, &stickY, &buttons, &camTurn);

        /* A script that sets its own camera yaw keeps it fixed, so the
         * stick-to-world mapping stays constant and the run is reproducible. */
        m64_world_step(&world, stickX, stickY, buttons,
                       script.camFollow && !script.hasCamYaw ? camTurn : camTurn);
        if (script.hasCamYaw) {
            world.viewCam.yaw = script.camYaw;
            world.camera.yaw = script.camYaw;
        }

        m64_world_format_trace(&world, line, sizeof(line));
        if (!opt.quiet) {
            printf("%s\n", line);
        }
        if (traceFile != NULL) {
            fprintf(traceFile, "%s\n", line);
        }

        render_frame(&fb, &world, &script, frame);

        if (gif != NULL) {
            m64_gif_add_frame(gif, fb.color);
        }
        if (opt.writePngs && (frame % opt.pngStride) == 0) {
            snprintf(path, sizeof(path), "%s/%s_%04d.png", opt.outDir, script.name, frame);
            if (m64_png_write(path, fb.color, fb.width, fb.height) != 0) {
                fprintf(stderr, "new64: failed to write '%s'\n", path);
            }
        }
    }

    if (gif != NULL) {
        m64_gif_end(gif);
        printf("# wrote %s/%s.gif (%d frames)\n", opt.outDir, script.name, totalFrames);
    }
    if (traceFile != NULL) {
        fclose(traceFile);
        printf("# wrote %s\n", opt.tracePath);
    }

    m64_fb_destroy(&fb);
    return 0;
}
