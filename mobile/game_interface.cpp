// AVP's Portable* implementation. See docs/engines/avp.md.

// All C system headers first: the engine's own headers redefine common names.
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <android/log.h>

#include "SDL3/SDL.h"

#include "game_interface.h"

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

    // Gameplay actions are Phase 2.
}

void PortableMove(float fwd, float strafe)
{
}

void PortableMoveFwd(float fwd)
{
}

void PortableMoveSide(float strafe)
{
}

void PortableLookPitch(int mode, float pitch)
{
}

void PortableLookYaw(int mode, float yaw)
{
}

void PortableMouse(float dx, float dy)
{
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
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
