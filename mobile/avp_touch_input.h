#ifndef AVP_TOUCH_INPUT_H
#define AVP_TOUCH_INPUT_H

// Touch state pulled by the engine once per frame. Deliberately free of engine
// types so game_interface.cpp still needs no engine headers.

#define AVP_TOUCH_ATTACK      (1u << 0)
#define AVP_TOUCH_ALT_ATTACK  (1u << 1)
#define AVP_TOUCH_JUMP        (1u << 2)
#define AVP_TOUCH_CROUCH      (1u << 3)
#define AVP_TOUCH_OPERATE     (1u << 4)
#define AVP_TOUCH_WALK        (1u << 5)
#define AVP_TOUCH_STRAFE      (1u << 6)
#define AVP_TOUCH_NEXT_WEAPON (1u << 7)
#define AVP_TOUCH_PREV_WEAPON (1u << 8)

typedef struct
{
	int move;            // forward positive, +-ONE_FIXED
	int strafe;          // right positive, +-ONE_FIXED
	unsigned int buttons;
} AVP_TouchInput;

#ifdef __cplusplus
extern "C" {
#endif

void AVP_GetTouchInput(AVP_TouchInput *out);

// Look is fed through the engine's own mouse path so it picks up the in-game
// sensitivity settings. Mouse values are accumulated deltas and are drained by
// this call; joystick values are a held rate.
void AVP_GetTouchLook(float *yawMouse, float *pitchMouse, float *yawJoy, float *pitchJoy);

#ifdef __cplusplus
}
#endif

#endif
