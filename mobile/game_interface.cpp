// AVP's Portable* implementation. See docs/engines/avp.md.

// All C system headers first: the engine's own headers redefine common names.
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <android/log.h>

#include "SDL3/SDL.h"

#include "game_interface.h"
#include "avp_touch_input.h"

#define LOG_TAG "AVP"
#define AVP_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))

extern "C" int main(int argc, char *argv[]);

// Defined in avp_menus.c (Android only) - front-end or in-game menus are up.
extern "C" int AVP_MenusAreRunning(void);

// AVP logs only through printf/stderr, which go nowhere on Android.
static void *stdio_pump(void *arg)
{
    int fd = (int) (long) arg;
    char buf[512];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        if (buf[n - 1] == '\n')
            n--;

        buf[n] = 0;
        AVP_LOGI("%s", buf);
    }

    return NULL;
}

static void redirect_stdio_to_logcat()
{
    int pipes[2];
    pthread_t thread;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (pipe(pipes) != 0)
        return;

    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);

    if (pthread_create(&thread, NULL, stdio_pump, (void *) (long) pipes[0]) == 0)
        pthread_detach(thread);
}

void PortableInit(int argc, const char **argv)
{
    redirect_stdio_to_logcat();

    AVP_LOGI("PortableInit, starting engine");

    main(argc, (char **) argv);
}

// Touch thread pushes, engine thread drains from CheckForWindowsMessages().
// Scancodes, not KEY_* codes, to keep engine headers out of this file.

#define KEY_QUEUE_SIZE 64

struct KeyEvent
{
    int scancode;
    int press;
};

static KeyEvent keyQueue[KEY_QUEUE_SIZE];
static volatile int keyQueueHead; // written by the touch thread
static volatile int keyQueueTail; // written by the engine thread

static void queueKey(int scancode, int press)
{
    int head = keyQueueHead;
    int next = (head + 1) % KEY_QUEUE_SIZE;

    if (next == keyQueueTail) // full, drop rather than block the touch thread
        return;

    keyQueue[head].scancode = scancode;
    keyQueue[head].press = press;
    keyQueueHead = next;
}

// One state change per key per frame: the menus poll held keys, so a press and
// release in the same frame reads as never-pressed. See docs/engines/avp.md.
extern "C" int AVP_PopPortableKey(int *scancode, int *press)
{
    static int changedThisFrame[KEY_QUEUE_SIZE];
    static int changedCount;

    if (keyQueueTail == keyQueueHead) // queue drained, frame is over
    {
        changedCount = 0;
        return 0;
    }

    int candidate = keyQueue[keyQueueTail].scancode;

    for (int i = 0; i < changedCount; i++)
    {
        if (changedThisFrame[i] == candidate)
        {
            changedCount = 0; // hold the rest back until the next frame
            return 0;
        }
    }

    *scancode = candidate;
    *press = keyQueue[keyQueueTail].press;

    if (changedCount < KEY_QUEUE_SIZE)
        changedThisFrame[changedCount++] = candidate;

    keyQueueTail = (keyQueueTail + 1) % KEY_QUEUE_SIZE;

    return 1;
}

void PortableBackButton(void)
{
    queueKey(SDL_SCANCODE_ESCAPE, 1);
    queueKey(SDL_SCANCODE_ESCAPE, 0);
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    // The shared touch layer (keyboard, blank-screen tap) sends SDL scancodes.
    queueKey(code, state);
    return 0;
}

// -------------------------------------------------------------------------
// Gameplay input. The touch thread only writes these; the engine thread reads
// them once per frame (AVP_GetTouchInput from ReadPlayerGameInput, and
// AVP_GetTouchLook from CheckForWindowsMessages). Buttons go in as engine
// request flags rather than synthetic keys, so rebinding controls in-game
// cannot break them.
// -------------------------------------------------------------------------

#define ONE_FIXED 65536

// Divisor, so a bigger value moves slower. touch_interface_base.cpp's left
// stick tops out near +-15 fwd / +-10 strafe at the default sensitivity, and
// the engine takes this as a direct speed multiplier (keyboard passes 1.0), so
// saturate a little before the stick edge to make full speed reachable.
#define FWD_STICK_RANGE    6.0f
#define STRAFE_STICK_RANGE 4.0f

// Look feeds the engine's mouse path. Mouse scale converts a screen-fraction
// swipe to mouse pixels; joystick scale goes straight to a mouse velocity,
// which saturates the turn rate at about 1024 with the default sensitivity.
#define LOOK_MOUSE_YAW_SCALE   2500.0f
#define LOOK_MOUSE_PITCH_SCALE 1500.0f
#define LOOK_JOY_YAW_SCALE     100.0f
#define LOOK_JOY_PITCH_SCALE   500.0f

static volatile float s_moveStick, s_strafeStick;   // analog stick
static volatile int s_moveDigital, s_strafeDigital; // dpad, -1/0/+1
static volatile unsigned int s_buttons;
static volatile int s_weaponSlot;

static volatile float s_yawMouse, s_pitchMouse; // accumulated, drained per frame
static volatile float s_yawJoy, s_pitchJoy;     // held rate

static void setButton(int state, unsigned int bit)
{
    if (state)
        s_buttons |= bit;
    else
        s_buttons &= ~bit;
}

static int clampFixed(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int)(v * ONE_FIXED);
}

extern "C" void AVP_GetTouchInput(AVP_TouchInput *out)
{
    out->move = s_moveDigital ? s_moveDigital * ONE_FIXED : clampFixed(s_moveStick);
    out->strafe = s_strafeDigital ? s_strafeDigital * ONE_FIXED : clampFixed(s_strafeStick);
    out->buttons = s_buttons;
    out->weaponSlot = s_weaponSlot;

    // One-shots fire on the frame they are collected, not for as long as held.
    s_buttons &= ~AVP_TOUCH_ONESHOT_MASK;
    s_weaponSlot = 0;
}

extern "C" void AVP_GetTouchLook(float *yawMouse, float *pitchMouse, float *yawJoy, float *pitchJoy)
{
    *yawMouse = -s_yawMouse;
    *pitchMouse = -s_pitchMouse;
    *yawJoy = s_yawJoy;
    *pitchJoy = s_pitchJoy;

    s_yawMouse = 0.0f;
    s_pitchMouse = 0.0f;
}

void PortableAction(int state, int action)
{
    if (action >= PORT_ACT_MENU_UP && action <= PORT_ACT_MENU_ABORT)
    {
        static const int menuScancode[] = {
            SDL_SCANCODE_UP,     // PORT_ACT_MENU_UP
            SDL_SCANCODE_DOWN,   // PORT_ACT_MENU_DOWN
            SDL_SCANCODE_LEFT,   // PORT_ACT_MENU_LEFT
            SDL_SCANCODE_RIGHT,  // PORT_ACT_MENU_RIGHT
            SDL_SCANCODE_RETURN, // PORT_ACT_MENU_SELECT
            SDL_SCANCODE_ESCAPE  // PORT_ACT_MENU_ABORT
        };

        queueKey(menuScancode[action - PORT_ACT_MENU_UP], state);
        return;
    }

    switch (action)
    {
        case PORT_ACT_ATTACK:      setButton(state, AVP_TOUCH_ATTACK); break;
        case PORT_ACT_ALT_ATTACK:  setButton(state, AVP_TOUCH_ALT_ATTACK); break;
        case PORT_ACT_JUMP:        setButton(state, AVP_TOUCH_JUMP); break;
        case PORT_ACT_USE:         setButton(state, AVP_TOUCH_OPERATE); break;
        // AVP runs by default, so these walk instead (same as :Unreal).
        case PORT_ACT_SPEED:
        case PORT_ACT_SPRINT:
        case PORT_ACT_SMART_TOGGLE_RUN:
        case PORT_ACT_ALWAYS_RUN:  setButton(state, AVP_TOUCH_WALK); break;
        case PORT_ACT_STRAFE:      setButton(state, AVP_TOUCH_STRAFE); break;
        case PORT_ACT_NEXT_WEP:    setButton(state, AVP_TOUCH_NEXT_WEAPON); break;
        case PORT_ACT_PREV_WEP:    setButton(state, AVP_TOUCH_PREV_WEAPON); break;

        case PORT_ACT_DOWN:
        case PORT_ACT_CROUCH:
            setButton(state, AVP_TOUCH_CROUCH);
            break;

        case PORT_ACT_TOGGLE_CROUCH:
            if (state)
                s_buttons ^= AVP_TOUCH_CROUCH;
            break;

        // Digital movement buttons; the analog stick uses PortableMove* instead.
        case PORT_ACT_FWD:        s_moveDigital = state ? 1 : 0; break;
        case PORT_ACT_BACK:       s_moveDigital = state ? -1 : 0; break;
        case PORT_ACT_MOVE_RIGHT: s_strafeDigital = state ? 1 : 0; break;
        case PORT_ACT_MOVE_LEFT:  s_strafeDigital = state ? -1 : 0; break;

        // Turn buttons have no analog equivalent here, so drive the look rate.
        case PORT_ACT_RIGHT: s_yawJoy = state ? LOOK_JOY_YAW_SCALE * 5.0f : 0.0f; break;
        case PORT_ACT_LEFT:  s_yawJoy = state ? -LOOK_JOY_YAW_SCALE * 5.0f : 0.0f; break;

        // IOFOCUS_Toggle is hardwired to this key, not rebindable.
        case PORT_ACT_CONSOLE: queueKey(SDL_SCANCODE_GRAVE, state); break;

        // Species abilities. Held ones the engine debounces itself; the rest
        // are latched here and cleared once the engine has collected them.
        case PORT_ACT_AVP_VISION:      setButton(state, AVP_TOUCH_VISION); break;
        case PORT_ACT_AVP_CLOAK:       setButton(state, AVP_TOUCH_VISION); break;
        case PORT_ACT_AVP_JETPACK:     setButton(state, AVP_TOUCH_JETPACK); break;
        case PORT_ACT_AVP_RECALL_DISC: setButton(state, AVP_TOUCH_RECALL_DISC); break;

        case PORT_ACT_AVP_CYCLE_VISION: if (state) s_buttons |= AVP_TOUCH_CYCLE_VISION; break;
        case PORT_ACT_AVP_FLARE:        if (state) s_buttons |= AVP_TOUCH_FLARE; break;
        case PORT_ACT_AVP_GRAPPLE:      if (state) s_buttons |= AVP_TOUCH_GRAPPLE; break;
        case PORT_ACT_AVP_ZOOM_IN:      if (state) s_buttons |= AVP_TOUCH_ZOOM_IN; break;
        case PORT_ACT_AVP_ZOOM_OUT:     if (state) s_buttons |= AVP_TOUCH_ZOOM_OUT; break;

        default:
            // Weapon number grid. PORT_ACT_WEAP0 is slot 10, as on the keyboard.
            if (state && action >= PORT_ACT_WEAP0 && action <= PORT_ACT_WEAP9)
            {
                int n = action - PORT_ACT_WEAP0;
                s_weaponSlot = n ? n : 10;
            }
            break;
    }
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableMoveFwd(float fwd)
{
    s_moveStick = fwd / FWD_STICK_RANGE;
}

void PortableMoveSide(float strafe)
{
    s_strafeStick = strafe / STRAFE_STICK_RANGE;
}

void PortableLookPitch(int mode, float pitch)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_pitchJoy = pitch * LOOK_JOY_PITCH_SCALE;
    else
        s_pitchMouse += pitch * LOOK_MOUSE_PITCH_SCALE;
}

void PortableLookYaw(int mode, float yaw)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_yawJoy = yaw * LOOK_JOY_YAW_SCALE;
    else
        s_yawMouse += yaw * LOOK_MOUSE_YAW_SCALE;
}

void PortableMouse(float dx, float dy)
{
    // A bare swipe (no virtual stick) counts as mouse-mode look.
    s_yawMouse += dx * LOOK_MOUSE_YAW_SCALE;
    s_pitchMouse += dy * LOOK_MOUSE_PITCH_SCALE;
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    if (button == 1)
        setButton(state, AVP_TOUCH_ATTACK);
    else if (button == 2)
        setButton(state, AVP_TOUCH_ALT_ATTACK);
}

void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return run;
}

touchscreemode_t PortableGetScreenMode()
{
    return AVP_MenusAreRunning() ? TS_MENU : TS_GAME;
}
