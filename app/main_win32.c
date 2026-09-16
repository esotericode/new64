/*
 * new64 -- interactive Win32 front-end.
 *
 * Plain Win32 and GDI: the software framebuffer is blitted with StretchDIBits,
 * so there is no Direct3D, no OpenGL, and nothing to install.  The resulting
 * executable depends only on DLLs that ship with Windows.
 *
 * ========================= WHY THE GAMEPAD MATTERS ========================
 *
 * This front-end supports an analog stick through XInput, and that is a
 * fidelity feature rather than a convenience.  The movement model's stick
 * response is *quadratic*:
 *
 *     intendedMag = ((stickMag / 64)^2 * 64) / 2
 *
 * so half deflection gives a quarter of the speed.  Fine speed control -- the
 * difference between creeping, walking and running -- lives entirely in that
 * curve.  A keyboard can only ever report full deflection, which means the
 * whole low end of the curve is unreachable and the character always runs.
 * With a stick plugged in you get the real thing.
 *
 * XInput is loaded dynamically, so a machine with no XInput DLL and no gamepad
 * still runs fine on the keyboard.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/*
 * timeBeginPeriod/timeEndPeriod live in the multimedia timer header, which
 * WIN32_LEAN_AND_MEAN deliberately excludes. Pull it in explicitly rather than
 * letting them be implicitly declared.
 */
#if defined(__has_include)
#if __has_include(<timeapi.h>)
#include <timeapi.h>
#else
#include <mmsystem.h>
#endif
#else
#include <mmsystem.h>
#endif

#include "demo_app.h"

#include "m64_level.h"
#include "m64_render.h"
#include "mario.h"
#include "sm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_WIDTH  960
#define WIN_HEIGHT 540

/* The movement model is tuned for 30fps; see FRAME_RATE in sm64.h. */
#define TICKS_PER_SECOND 30

/* Raw stick deflection for a key press. Matches the hardware range the
 * movement model expects, so keyboard input lands on the same curve. */
#define KEY_STICK 80

/* --- XInput, loaded at runtime ------------------------------------------ */

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} M64_XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    M64_XINPUT_GAMEPAD Gamepad;
} M64_XINPUT_STATE;

#define M64_XINPUT_A             0x1000
#define M64_XINPUT_B             0x2000
#define M64_XINPUT_X             0x4000
#define M64_XINPUT_Y             0x8000
#define M64_XINPUT_LSHOULDER     0x0100
#define M64_XINPUT_RSHOULDER     0x0200
#define M64_XINPUT_START         0x0010
#define M64_XINPUT_BACK          0x0020

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, M64_XINPUT_STATE *);

static XInputGetStateFn sXInputGetState;
static HMODULE sXInputModule;

/*
 * Try the XInput DLLs newest first.  1_4 ships with Windows 8+, 1_3 comes with
 * the DirectX redistributable, and 9_1_0 is present on Vista and 7.  Failing
 * all three is not an error -- it just means keyboard only.
 */
static void xinput_load(void) {
    static const char *const names[] = { "xinput1_4.dll", "xinput1_3.dll",
                                         "xinput9_1_0.dll" };
    int i;

    for (i = 0; i < 3; i++) {
        sXInputModule = LoadLibraryA(names[i]);
        if (sXInputModule != NULL) {
            sXInputGetState =
                (XInputGetStateFn) (void *) GetProcAddress(sXInputModule, "XInputGetState");
            if (sXInputGetState != NULL) {
                return;
            }
            FreeLibrary(sXInputModule);
            sXInputModule = NULL;
        }
    }
}

/*
 * Map an XInput thumb axis onto the raw range the movement model expects.
 *
 * Deliberately NO dead zone is applied here.  The model has its own dead zone
 * (readings within +/-8 of centre are discarded, and everything beyond is
 * shifted toward zero by 6) and applying XInput's much larger recommended dead
 * zone on top would flatten the bottom of the response curve -- exactly the
 * region the quadratic curve exists to provide.
 */
static s16 thumb_to_raw(SHORT value) {
    f32 scaled = (f32) value * (80.0f / 32767.0f);

    if (scaled > 80.0f) {
        scaled = 80.0f;
    }
    if (scaled < -80.0f) {
        scaled = -80.0f;
    }
    return (s16) scaled;
}

/* --- Keyboard ----------------------------------------------------------- */

struct KeyState {
    int up, down, left, right;
    int a, b, z;
    int camLeft, camRight;
    int reset;
};

static void handle_key(struct KeyState *keys, WPARAM key, int pressed) {
    switch (key) {
        case 'W': case VK_UP:    keys->up = pressed; break;
        case 'S': case VK_DOWN:  keys->down = pressed; break;
        case 'A': case VK_LEFT:  keys->left = pressed; break;
        case 'D': case VK_RIGHT: keys->right = pressed; break;
        case VK_SPACE:           keys->a = pressed; break;
        case 'J':                keys->b = pressed; break;
        case 'K': case VK_SHIFT: keys->z = pressed; break;
        case 'Q':                keys->camLeft = pressed; break;
        case 'E':                keys->camRight = pressed; break;
        case 'R':                keys->reset = pressed; break;
        default: break;
    }
}

/* --- Window ------------------------------------------------------------- */

static struct M64Framebuffer sFb;
static BITMAPINFO sBitmapInfo;
static int sRunning = 1;
static struct KeyState sKeys;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            sRunning = 0;
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                sRunning = 0;
            }
            handle_key(&sKeys, wParam, 1);
            return 0;

        case WM_KEYUP:
            handle_key(&sKeys, wParam, 0);
            return 0;

        /* The framebuffer is repainted every tick anyway; suppressing the
         * background erase avoids a flash between blits. */
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;

            GetClientRect(hwnd, &client);
            StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, sFb.width,
                          sFb.height, sFb.color, &sBitmapInfo, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

static void draw_hud(struct M64Framebuffer *fb, const struct M64World *w, int hasPad) {
    const struct MarioState *m = &w->marioState;
    char line[160];
    s32 y = 6;

    m64_render_rect_blend(fb, 0, 0, fb->width, 74, 0x000000, 150);

    snprintf(line, sizeof(line), "NEW64  FRAME %u  %s", w->frame,
             hasPad ? "GAMEPAD" : "KEYBOARD");
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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine,
                   int showCmd) {
    WNDCLASSA wc;
    HWND hwnd;
    HDC dc;
    struct M64World world;
    LARGE_INTEGER freq, nextTick, now;
    RECT desired;
    int hasPad = 0;

    (void) prevInstance;
    (void) cmdLine;

    if (m64_fb_create(&sFb, WIN_WIDTH, WIN_HEIGHT) != 0) {
        MessageBoxA(NULL, "Could not allocate the framebuffer.", "new64", MB_ICONERROR);
        return 1;
    }

    /*
     * Negative height requests a top-down DIB, matching the framebuffer's row
     * order. The pixel layout (0xAARRGGBB as a little-endian u32) is already
     * what BI_RGB at 32bpp expects, so the blit needs no conversion.
     */
    memset(&sBitmapInfo, 0, sizeof(sBitmapInfo));
    sBitmapInfo.bmiHeader.biSize = sizeof(sBitmapInfo.bmiHeader);
    sBitmapInfo.bmiHeader.biWidth = sFb.width;
    sBitmapInfo.bmiHeader.biHeight = -sFb.height;
    sBitmapInfo.bmiHeader.biPlanes = 1;
    sBitmapInfo.bmiHeader.biBitCount = 32;
    sBitmapInfo.bmiHeader.biCompression = BI_RGB;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = "new64_window";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Could not register the window class.", "new64", MB_ICONERROR);
        return 1;
    }

    /* Size the window so the *client area* is exactly the framebuffer, rather
     * than the frame, which would scale the image slightly. */
    desired.left = 0;
    desired.top = 0;
    desired.right = WIN_WIDTH;
    desired.bottom = WIN_HEIGHT;
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd = CreateWindowExA(0, wc.lpszClassName,
                           "new64 - SM64 movement in a custom engine",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           desired.right - desired.left, desired.bottom - desired.top,
                           NULL, NULL, instance, NULL);
    if (hwnd == NULL) {
        MessageBoxA(NULL, "Could not create the window.", "new64", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, showCmd);
    dc = GetDC(hwnd);

    xinput_load();
    memset(&sKeys, 0, sizeof(sKeys));
    m64_world_init(&world);

    /* 1ms timer resolution so the 30Hz pacing below is actually accurate;
     * without it Sleep() granularity is ~15ms and the tick rate wobbles. */
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&nextTick);

    while (sRunning) {
        MSG msg;
        s16 stickX = 0, stickY = 0, camTurn = 0;
        u16 buttons = 0;
        LONGLONG tickInterval = freq.QuadPart / TICKS_PER_SECOND;

        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!sRunning) {
            break;
        }

        /* Gamepad first; the keyboard is additive on top so both work at once. */
        hasPad = 0;
        if (sXInputGetState != NULL) {
            M64_XINPUT_STATE pad;

            memset(&pad, 0, sizeof(pad));
            if (sXInputGetState(0, &pad) == ERROR_SUCCESS) {
                hasPad = 1;
                stickX = thumb_to_raw(pad.Gamepad.sThumbLX);
                stickY = thumb_to_raw(pad.Gamepad.sThumbLY);

                if (pad.Gamepad.wButtons & M64_XINPUT_A) {
                    buttons |= A_BUTTON;
                }
                if (pad.Gamepad.wButtons & (M64_XINPUT_X | M64_XINPUT_B)) {
                    buttons |= B_BUTTON;
                }
                /* Either trigger or either shoulder acts as Z, since Z has no
                 * natural equivalent on a modern pad. */
                if (pad.Gamepad.bLeftTrigger > 40 || pad.Gamepad.bRightTrigger > 40
                    || (pad.Gamepad.wButtons
                        & (M64_XINPUT_LSHOULDER | M64_XINPUT_RSHOULDER))) {
                    buttons |= Z_TRIG;
                }

                /* Right stick rotates the camera. */
                if (pad.Gamepad.sThumbRX > 10000) {
                    camTurn += 0x0300;
                } else if (pad.Gamepad.sThumbRX < -10000) {
                    camTurn -= 0x0300;
                }

                if (pad.Gamepad.wButtons & M64_XINPUT_BACK) {
                    m64_world_init(&world);
                }
            }
        }

        if (sKeys.up)    stickY += KEY_STICK;
        if (sKeys.down)  stickY -= KEY_STICK;
        if (sKeys.right) stickX += KEY_STICK;
        if (sKeys.left)  stickX -= KEY_STICK;
        if (sKeys.a) buttons |= A_BUTTON;
        if (sKeys.b) buttons |= B_BUTTON;
        if (sKeys.z) buttons |= Z_TRIG;
        if (sKeys.camLeft)  camTurn -= 0x0300;
        if (sKeys.camRight) camTurn += 0x0300;

        if (sKeys.reset) {
            m64_world_init(&world);
            sKeys.reset = 0;
        }

        m64_world_step(&world, stickX, stickY, buttons, camTurn);

        m64_fb_clear_sky(&sFb);
        m64_render_set_camera(&sFb, world.viewCam.pos, world.viewCam.focus, 55.0f);
        m64_render_level(&sFb);
        m64_render_capsule(&sFb, world.marioState.pos, world.marioState.faceAngle[1],
                           45.0f, 160.0f, 0xE04040);
        draw_hud(&sFb, &world, hasPad);

        {
            RECT client;

            GetClientRect(hwnd, &client);
            StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, sFb.width,
                          sFb.height, sFb.color, &sBitmapInfo, DIB_RGB_COLORS, SRCCOPY);
        }

        /*
         * Fixed 30Hz. The physics is frame-rate dependent by design -- gravity
         * is "-4 per frame", not "per second" -- so the tick rate must not drift
         * with rendering cost, and delta-time scaling would be wrong.
         */
        nextTick.QuadPart += tickInterval;
        QueryPerformanceCounter(&now);
        while (now.QuadPart < nextTick.QuadPart) {
            LONGLONG remainingMs =
                ((nextTick.QuadPart - now.QuadPart) * 1000) / freq.QuadPart;

            if (remainingMs > 1) {
                Sleep((DWORD) (remainingMs - 1));
            }
            QueryPerformanceCounter(&now);
        }
        /* If we fell badly behind (window dragged, machine stalled), resync
         * rather than sprinting to catch up -- catching up would run the
         * physics faster than real time. */
        if (now.QuadPart - nextTick.QuadPart > tickInterval * 4) {
            nextTick = now;
        }
    }

    timeEndPeriod(1);
    if (sXInputModule != NULL) {
        FreeLibrary(sXInputModule);
    }
    ReleaseDC(hwnd, dc);
    m64_fb_destroy(&sFb);
    return 0;
}
