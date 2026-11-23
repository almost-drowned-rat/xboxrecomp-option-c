/* SPDX-License-Identifier: MIT
 * touchpad.c - the touch controls as one virtual SDL gamepad (2026-11-11).
 *
 * TouchOverlay.java draws the controls and sends their state here (JNI, the UI thread); the game's toolkit input
 * (option-c src/input/xinput_device.c) sees an ordinary SDL gamepad: A/B/X/Y = SOUTH/EAST/WEST/NORTH, WHITE / BLACK
 * = the shoulder buttons (the toolkit's default for them), START, BACK, L3 / R3, the D-pad, LT / RT = the trigger
 * axes, the floating stick = the left stick, the fixed one = the right
 * stick. libSDL3.so is shared by libmain.so and the game library, so the device attached here is the game's.
 * A real gamepad's input detaches it (PortActivity, mode AUTO), so the real pad stays player 1; a touch attaches
 * it again. */
#include <SDL3/SDL.h>
#include <android/log.h>
#include <jni.h>
#include <string.h>

#include "port_android.h"
#include "touchpad.h"

static SDL_AtomicInt s_ready;                   /* the gamepad subsystem is up (touchpad_init) */
static SDL_JoystickID s_id;
static SDL_Joystick *s_joy;
static SDL_Mutex *s_mx;

static void attach(void)
{
    SDL_VirtualJoystickDesc d;
    if (s_id)
        return;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;           /* all six: the joystick axis index = the SDL_GamepadAxis */
    d.nbuttons = 14;                            /* the buttons in button_mask (see the map in nativeTouchPad) */
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                  (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                  (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    d.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                    (1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                    (1u << SDL_GAMEPAD_BUTTON_BACK) | (1u << SDL_GAMEPAD_BUTTON_START) |
                    (1u << SDL_GAMEPAD_BUTTON_LEFT_STICK) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_STICK) |
                    (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) |
                    (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) | (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                    (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    d.name = PORT_TOUCH_NAME;
    s_id = SDL_AttachVirtualJoystick(&d);
    s_joy = s_id ? SDL_OpenJoystick(s_id) : NULL;
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[TOUCH] virtual gamepad %s (id %u)", s_joy ? "attached" : "failed",
                        (unsigned)s_id);
}

static void detach(void)
{
    if (!s_id)
        return;
    if (s_joy)
        SDL_CloseJoystick(s_joy);
    SDL_DetachVirtualJoystick(s_id);
    s_joy = NULL;
    s_id = 0;
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[TOUCH] virtual gamepad detached (a real gamepad is used)");
}

void touchpad_init(void)
{
    s_mx = SDL_CreateMutex();
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);        /* stays up: the game's SDL_Init counts on top of it */
    if (SDL_HasGamepad()) {                     /* a real pad (the Odin 2's own controls): it stays player 1 */
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[TOUCH] a gamepad is connected - touch pad attached on touch");
    } else {
        attach();
    }
    SDL_SetAtomicInt(&s_ready, 1);
}

JNIEXPORT void JNICALL Java_xboxrecomp_port_PortActivity_nativeTouchPadEnable(JNIEnv *env, jclass c, jboolean on)
{
    (void)env; (void)c;
    if (!SDL_GetAtomicInt(&s_ready))
        return;
    SDL_LockMutex(s_mx);
    if (on) attach(); else detach();
    SDL_UnlockMutex(s_mx);
}

JNIEXPORT void JNICALL Java_xboxrecomp_port_TouchOverlay_nativeTouchPad(JNIEnv *env, jclass c,
    jint b, jfloat lx, jfloat ly, jfloat lt, jfloat rt, jfloat rx, jfloat ry)
{
    /* TouchOverlay's bit i -> the virtual JOYSTICK button. SDL_SetJoystickVirtualButton takes the joystick index,
     * and with a button_mask SDL numbers only the masked buttons, in SDL_GamepadButton order (SDL3
     * SDL_virtualjoystick.c VIRTUAL_JoystickGetGamepadMapping): SOUTH 0, EAST 1, WEST 2, NORTH 3, BACK 4, START 5,
     * LEFT_STICK 6, RIGHT_STICK 7, LEFT_SHOULDER 8, RIGHT_SHOULDER 9, DPAD_UP 10, DOWN 11, LEFT 12, RIGHT 13.
     * Passing the SDL_GamepadButton enums pressed the wrong buttons or none (2026-11-11: START was WHITE,
     * WHITE / BLACK / LT / RT did nothing). -1 = not a button (triggers, sticks). */
    static const signed char map[18] = {
        0 /* A */, 1 /* B */, 2 /* X */, 3 /* Y */, 9 /* WHITE = RIGHT_SHOULDER */, 8 /* BLACK = LEFT_SHOULDER */,
        5 /* START */, -1 /* LT */, -1 /* RT */, -1 /* STICK */, 4 /* BACK */, 6 /* L3 */, 7 /* R3 */,
        10 /* D-pad up */, 11 /* down */, 12 /* left */, 13 /* right */, -1 /* RIGHT STICK */,
    };
    int i;
    (void)env; (void)c;
    if (!SDL_GetAtomicInt(&s_ready))
        return;
    SDL_LockMutex(s_mx);
    if (!s_joy)
        attach();
    if (s_joy) {
        for (i = 0; i < 18; i++)
            if (map[i] >= 0)
                SDL_SetJoystickVirtualButton(s_joy, map[i], (b >> i) & 1);
        /* all six axes are in the mask, so here the joystick axis = the SDL_GamepadAxis; a trigger rests at
         * SDL_JOYSTICK_AXIS_MIN (SDL maps the joystick's full range onto 0..32767) */
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)(lx * 32767));
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)(ly * 32767));
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_RIGHTX, (Sint16)(rx * 32767));
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_RIGHTY, (Sint16)(ry * 32767));
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, (Sint16)(-32768 + (int)(lt * 65535)));
        SDL_SetJoystickVirtualAxis(s_joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, (Sint16)(-32768 + (int)(rt * 65535)));
    }
    SDL_UnlockMutex(s_mx);
}

/* The pad ranking for the toolkit (xbox_InputSetPadPriority, toolkit branch agent-l/pads; loader.c hands it over
 * after dlopen): an external pad (USB / Bluetooth, the dock's) above a built-in one (the Odin 2 Portal's own
 * controls) and the touch pad, so in a multi-player title the external pads are players 1, 2 ... and in a title
 * where every pad is player 1 (Orta) nothing changes. Android tells built-in from external (InputDevice.isExternal);
 * PortActivity.builtInPadNames() lists the built-in ones, one name per line. */
int touchpad_pad_priority(const char *name)
{
    JNIEnv *env;
    jclass c;
    jmethodID m;
    jstring js;
    int prio = 10;
    if (!name || !strcmp(name, PORT_TOUCH_NAME))
        return 0;
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env)
        return prio;
    c = (*env)->FindClass(env, "xboxrecomp/port/PortActivity");
    if (!c) { (*env)->ExceptionClear(env); return prio; }
    m = (*env)->GetStaticMethodID(env, c, "builtInPadNames", "()Ljava/lang/String;");
    js = m ? (jstring)(*env)->CallStaticObjectMethod(env, c, m) : NULL;
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (js) {
        const char *s = (*env)->GetStringUTFChars(env, js, NULL);
        const char *p = s;
        size_t n = strlen(name);
        while (p && *p) {                           /* one name per line */
            const char *e = strchr(p, '\n');
            size_t l = e ? (size_t)(e - p) : strlen(p);
            if (l == n && !strncmp(p, name, n)) { prio = 5; break; }
            p = e ? e + 1 : NULL;
        }
        (*env)->ReleaseStringUTFChars(env, js, s);
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->DeleteLocalRef(env, c);
    return prio;
}

/* Touch aiming (2026-11-13, for Orta; optional for every port): the game library's PORT_TOUCH_AIM entry
 * (port_android.h; Orta: orta_touch_aim in src/recomp_manual.c), handed over by loader.c after dlopen. TouchOverlay: nativeTouchAim(down, x, y, w, h) on every aiming touch (down 1 / move 1 / up 0, view
 * pixels), nativeTouchAim(-1, ...) to ask whether the game is in flight (the aiming layout shows only then). Returns
 * 0 without the call (older game library, or before the game). */
static int (*s_aim)(int, float, float, float, float);

void touchpad_set_aim(int (*fn)(int, float, float, float, float))
{
    s_aim = fn;
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[TOUCH] touch aiming %s", fn ? "available" : "not in this game library");
}

JNIEXPORT jint JNICALL Java_xboxrecomp_port_TouchOverlay_nativeTouchAim(JNIEnv *env, jclass c,
    jint down, jfloat x, jfloat y, jfloat w, jfloat h)
{
    (void)env; (void)c;
    return s_aim ? s_aim(down, x, y, w, h) : 0;
}
