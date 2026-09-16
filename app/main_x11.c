/*
 * new64 -- interactive X11 front-end.
 *
 * Xlib only: the software renderer's framebuffer is blitted straight into a
 * window, so there is no GL, no SDL, and nothing to install.  The point is that
 * the movement model can be *felt* -- traces and animations verify it, but only
 * holding the controls tells you whether the feel is right.
 *
 * The keyboard is a digital stick, which matters: the movement model has a
 * quadratic analog response, so a keyboard always gives you full-magnitude
 * input and you lose fine speed control. Tiptoeing needs a real analog stick.
 */
#include "demo_app.h"

#include "m64_level.h"
#include "m64_render.h"
#include "mario.h"
#include "sm64.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN_WIDTH  960
#define WIN_HEIGHT 540

/* The movement model is tuned for 30fps; see FRAME_RATE in sm64.h. */
#define TICK_NS (1000000000L / 30)

/* Raw stick deflection a key press reports. Matches the hardware range. */
#define KEY_STICK 80

struct KeyState {
    s32 up, down, left, right;
    s32 a, b, z;
    s32 camLeft, camRight;
    s32 reset;
};

static void draw_hud(struct M64Framebuffer *fb, const struct M64World *w) {
    const struct MarioState *m = &w->marioState;
    char line[160];
    s32 y = 6;

    m64_render_rect_blend(fb, 0, 0, fb->width, 74, 0x000000, 150);

    snprintf(line, sizeof(line), "NEW64  FRAME %u", w->frame);
    m64_render_text(fb, 8, y, 1, 0xFFFFFFFF, line);
    y += 11;

    snprintf(line, sizeof(line), "ACTION %s", m64_action_name(m->action));
    m64_render_text(fb, 8, y, 1, 0xFF90FF90, line);
    y += 11;

    snprintf(line, sizeof(line), "FWD %6.2f  VELY %7.2f  YAW %6d  MAG %5.1f",
             m->forwardVel, m->vel[1], m->faceAngle[1], m->intendedMag);
    m64_render_text(fb, 8, y, 1, 0xFFFFE080, line);
    y += 11;

    snprintf(line, sizeof(line), "POS %7.0f %7.0f %7.0f  FLOOR %6.0f T%d", m->pos[0],
             m->pos[1], m->pos[2], m->floorHeight,
             m->floor != NULL ? m->floor->type : -1);
    m64_render_text(fb, 8, y, 1, 0xFFB0D0FF, line);
    y += 11;

    m64_render_text(fb, 8, y, 1, 0xFFA0A0A0,
                    "WASD MOVE  SPACE JUMP  J PUNCH/DIVE  K CROUCH  Q/E CAM  R RESET");
}

static void handle_key(struct KeyState *keys, KeySym sym, s32 pressed) {
    switch (sym) {
        case XK_w: case XK_W: case XK_Up:    keys->up = pressed; break;
        case XK_s: case XK_S: case XK_Down:  keys->down = pressed; break;
        case XK_a: case XK_A: case XK_Left:  keys->left = pressed; break;
        case XK_d: case XK_D: case XK_Right: keys->right = pressed; break;
        case XK_space:                       keys->a = pressed; break;
        case XK_j: case XK_J:                keys->b = pressed; break;
        case XK_k: case XK_K:
        case XK_Shift_L: case XK_Shift_R:    keys->z = pressed; break;
        case XK_q: case XK_Q:                keys->camLeft = pressed; break;
        case XK_e: case XK_E:                keys->camRight = pressed; break;
        case XK_r: case XK_R:                keys->reset = pressed; break;
        default: break;
    }
}

int main(void) {
    Display *display;
    Window window;
    XImage *image;
    GC gc;
    Atom wmDelete;
    struct M64World world;
    struct M64Framebuffer fb;
    struct KeyState keys;
    s32 screen;
    s32 running = 1;
    struct timespec nextTick;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr,
                "new64: cannot open an X display.\n"
                "       This front-end needs a running X server. On a headless\n"
                "       machine use new64_headless instead, which renders to\n"
                "       PNG and GIF files.\n");
        return 1;
    }

    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, WIN_WIDTH,
                                 WIN_HEIGHT, 0, BlackPixel(display, screen),
                                 BlackPixel(display, screen));
    XStoreName(display, window, "new64 - SM64 movement in a custom engine");
    XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask
                                     | StructureNotifyMask);
    wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDelete, 1);
    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);

    if (m64_fb_create(&fb, WIN_WIDTH, WIN_HEIGHT) != 0) {
        fprintf(stderr, "new64: framebuffer allocation failed\n");
        return 1;
    }

    /* The framebuffer is handed to X directly; no per-frame copy. This assumes
     * a 24/32-bit TrueColor visual, which is universal on modern servers. */
    image = XCreateImage(display, DefaultVisual(display, screen),
                         (unsigned int) DefaultDepth(display, screen), ZPixmap, 0,
                         (char *) fb.color, (unsigned int) fb.width,
                         (unsigned int) fb.height, 32, 0);
    if (image == NULL) {
        fprintf(stderr, "new64: XCreateImage failed\n");
        return 1;
    }

    memset(&keys, 0, sizeof(keys));
    m64_world_init(&world);

    clock_gettime(CLOCK_MONOTONIC, &nextTick);

    while (running) {
        s16 stickX = 0, stickY = 0, camTurn = 0;
        u16 buttons = 0;

        while (XPending(display)) {
            XEvent event;

            XNextEvent(display, &event);
            switch (event.type) {
                case KeyPress: {
                    KeySym sym = XLookupKeysym(&event.xkey, 0);

                    if (sym == XK_Escape) {
                        running = 0;
                    }
                    handle_key(&keys, sym, 1);
                    break;
                }
                case KeyRelease: {
                    KeySym sym = XLookupKeysym(&event.xkey, 0);

                    handle_key(&keys, sym, 0);
                    break;
                }
                case ClientMessage:
                    if ((Atom) event.xclient.data.l[0] == wmDelete) {
                        running = 0;
                    }
                    break;
                default:
                    break;
            }
        }

        if (keys.reset) {
            m64_world_init(&world);
            keys.reset = 0;
        }

        if (keys.up)    stickY += KEY_STICK;
        if (keys.down)  stickY -= KEY_STICK;
        if (keys.right) stickX += KEY_STICK;
        if (keys.left)  stickX -= KEY_STICK;
        if (keys.a) buttons |= A_BUTTON;
        if (keys.b) buttons |= B_BUTTON;
        if (keys.z) buttons |= Z_TRIG;
        if (keys.camLeft)  camTurn -= 0x0300;
        if (keys.camRight) camTurn += 0x0300;

        m64_world_step(&world, stickX, stickY, buttons, camTurn);

        m64_fb_clear_sky(&fb);
        m64_render_set_camera(&fb, world.viewCam.pos, world.viewCam.focus, 55.0f);
        m64_render_level(&fb);
        m64_render_capsule(&fb, world.marioState.pos, world.marioState.faceAngle[1],
                           45.0f, 160.0f, 0xE04040);
        draw_hud(&fb, &world);

        XPutImage(display, window, gc, image, 0, 0, 0, 0, (unsigned int) fb.width,
                  (unsigned int) fb.height);
        XFlush(display);

        /* Fixed 30Hz pacing: the physics is frame-rate dependent by design, so
         * the tick rate is not allowed to drift with rendering cost. */
        nextTick.tv_nsec += TICK_NS;
        while (nextTick.tv_nsec >= 1000000000L) {
            nextTick.tv_nsec -= 1000000000L;
            nextTick.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextTick, NULL);
    }

    /* The image borrowed fb.color, so detach before freeing either. */
    image->data = NULL;
    XDestroyImage(image);
    m64_fb_destroy(&fb);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
