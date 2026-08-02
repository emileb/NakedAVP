#ifndef AVP_TOUCH_INPUT_H
#define AVP_TOUCH_INPUT_H

// Touch state pulled by the engine once per frame. Deliberately free of engine
// types so game_interface.cpp still needs no engine headers.

// Held buttons.
#define AVP_TOUCH_ATTACK      (1u << 0)
#define AVP_TOUCH_ALT_ATTACK  (1u << 1)
#define AVP_TOUCH_JUMP        (1u << 2)
#define AVP_TOUCH_CROUCH      (1u << 3)
#define AVP_TOUCH_OPERATE     (1u << 4)
#define AVP_TOUCH_WALK        (1u << 5)
#define AVP_TOUCH_STRAFE      (1u << 6)
#define AVP_TOUCH_NEXT_WEAPON (1u << 7)
#define AVP_TOUCH_PREV_WEAPON (1u << 8)
#define AVP_TOUCH_VISION      (1u << 9)  // Marine intensifier / Predator cloak / Alien sense
#define AVP_TOUCH_JETPACK     (1u << 10) // Marine, thrusts while held
#define AVP_TOUCH_RECALL_DISC (1u << 11) // Predator

// One-shot presses, cleared by AVP_GetTouchInput once handed over.
#define AVP_TOUCH_CYCLE_VISION (1u << 12) // Predator
#define AVP_TOUCH_FLARE        (1u << 13) // Marine
#define AVP_TOUCH_GRAPPLE      (1u << 14) // Predator
#define AVP_TOUCH_ZOOM_IN      (1u << 15) // Predator
#define AVP_TOUCH_ZOOM_OUT     (1u << 16) // Predator
#define AVP_TOUCH_QUICKSAVE    (1u << 17)
#define AVP_TOUCH_QUICKLOAD    (1u << 18)

#define AVP_TOUCH_ONESHOT_MASK (AVP_TOUCH_CYCLE_VISION | AVP_TOUCH_FLARE | \
                                AVP_TOUCH_GRAPPLE | AVP_TOUCH_ZOOM_IN | AVP_TOUCH_ZOOM_OUT | \
                                AVP_TOUCH_QUICKSAVE | AVP_TOUCH_QUICKLOAD)

// Matches the engine's I_Marine/I_Predator/I_Alien ordering.
#define AVP_PLAYER_MARINE   0
#define AVP_PLAYER_PREDATOR 1
#define AVP_PLAYER_ALIEN    2

// Per-level abilities (StartingEquipment), so their buttons can auto-show.
#define AVP_ABILITY_JETPACK (1u << 0)
#define AVP_ABILITY_GRAPPLE (1u << 1)

typedef struct
{
	int move;            // forward positive, +-ONE_FIXED
	int strafe;          // right positive, +-ONE_FIXED
	int weaponSlot;      // 1-10, or 0 for no request
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

// Which species the player is, so the touch layer can pick its control set.
int AVP_GetPlayerType(void);

// AVP_ABILITY_* the current level grants.
unsigned int AVP_GetPlayerAbilities(void);

#ifdef __cplusplus
}
#endif

#endif
