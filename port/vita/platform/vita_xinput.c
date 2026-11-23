/*
VITA_XINPUT.C

The Xbox's controller on the Vita (XInput, in place of the game's
xinput_sdl.c): one gamepad, the Vita itself, read through vita_pad.c (its
buttons, sticks, touch zones and gyroscope, with the controls Halo CE Vita
had: host/vita_input.c, vita_controls.c). There is no keyboard or mouse.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "vita_host.h"
#include "vita_controls.h"
#ifdef HALO_GAME_BROWSER
#include "guide_input.h"
#endif

#include <string.h>

/* (vita_pad.c) the Vita's pad as the Xbox's */
void vita_pad_state(XINPUT_GAMEPAD *gamepad);
int vita_pad_gyro_look(short gamepad_index, float *yaw, float *pitch);

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

static struct
{
	BOOL open;
	DWORD packet;
} gamepad;
static BOOL gamepad_reported;

/* ---------- the mouse's look and aim: the gyroscope's turns, as the
desktop's mouse turns the view (player_control.c) */

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	return vita_pad_gyro_look(gamepad_index, yaw, pitch);
}

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return FALSE;
}

/* ---------- the keyboard's (none) */

int platform_input_scheme(void)
{
	return 0;
}

int platform_prompt_scheme(void)
{
	/* (the overlay's prompts, Online Games' and the Delta Guide's: the
	Vita's own buttons, as a PlayStation pad's - halo_prompt_schemes.h's
	PROMPT_SCHEME_PLAYSTATION) */
	return 2;
}

unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return 0;
}

int halo_keyboard_prompts(void)
{
	return 0;
}

int halo_keyboard_action_shown(int action, char *shown, unsigned long size)
{
	(void)action;
	if (size)
		shown[0] = 0;
	return 0;
}

int halo_input_from_name(const char *name)
{
	(void)name;
	return -1;
}

void halo_input_name(int input, char *name, size_t size)
{
	(void)input;
	if (size)
		name[0] = 0;
}

void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	if (size)
		snprintf(shown, size, "%s", binding ? binding : "");
}

int platform_gamepad_count(void)
{
	return 1;
}

int platform_gamepad_describe(int index, char *name, size_t size)
{
	if (index != 0)
		return 0;
	if (size)
		snprintf(name, size, "PS Vita");
	return 1;
}

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
	platform_sdl_initialize();
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD && !gamepad_reported)
	{
		*insertions = 1;
		gamepad_reported = TRUE;
	}
	return *insertions != 0;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port == 0)
	{
		gamepad.open = TRUE;
		return (HANDLE)&gamepad;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	if (device == (HANDLE)&gamepad)
		gamepad.open = FALSE;
}

#ifdef HALO_GAME_BROWSER
/* ---------- the Delta Guide's input (guide_input.h)

SELECT with D-pad down opens the Guide and closes it, anywhere (in the menus
and in a game: Halo CE Vita's chord for its settings panel); the two stay
away from the game and the Guide until both are let go (vita_pad.c), so it
is no crouch, no Back and no step down. D-pad down alone moves as before. While the
Guide is open it has the Vita's buttons in the menus' fixed layout (Cross
A, Circle B, Square X, Triangle Y, the d-pad or the left stick, START,
SELECT as Back), L and R its LB and RB (the sections), SELECT held with L or R its LT and
RT (a section's pages); the rear touch pad does nothing there; the front
screen is its pointer (d3d8_gxm.c halo_ui_pointer_update: a tap selects
and presses, a drag scrolls); the game gets a controller at rest. What is still held as
it closes stays up for the game until let go of. */

/* (vita_pad.c) */
extern int vita_guide_capturing;
extern unsigned long vita_pad_raw_buttons, vita_pad_raw_touch;
extern int vita_guide_chord_presses, vita_guide_chord_held;

static int guide_capture, guide_confirm_swap, guide_toggles;
static unsigned int guide_held;
/* the chord's buttons, kept from everything until both are up */
static int chord_down;
/* the buttons held as the Guide closed (the pad's, by bit), up for the
game until let go of */
static WORD released_buttons;
static unsigned char released_analog;

void platform_guide_capture(int capture)
{
	guide_capture = capture != 0;
	vita_guide_capturing = guide_capture;
	guide_confirm_swap = 0;
	if (!guide_capture)
		guide_held = 0;
}

void platform_guide_confirm_swap(int swap)
{
	guide_confirm_swap = swap != 0;
}

unsigned int platform_guide_held(void)
{
	return guide_capture ? guide_held : 0;
}

int platform_guide_toggles(void)
{
	int toggles = guide_toggles;

	guide_toggles = 0;
	return toggles;
}

void platform_guide_buzz(void)
{
	/* (the Vita has no rumble) */
}

/* the pad as the game will see it, after vita_pad_state: the chord, and
the Guide's share while it has the input */
static void guide_take(XINPUT_GAMEPAD *pad)
{
	unsigned long raw = vita_pad_raw_buttons;
	int index;

	/* (the chord, taken from the buttons already: vita_pad.c) */
	guide_toggles += vita_guide_chord_presses;
	vita_guide_chord_presses = 0;
	chord_down = vita_guide_chord_held;
	if (guide_capture)
	{
		unsigned int held = 0;
		int select = (raw & VITA_BUTTON_SELECT) && !chord_down;

		if ((pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) || pad->sThumbLY > 16000) held |= GUIDE_INPUT_UP;
		if ((pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || pad->sThumbLY < -16000) held |= GUIDE_INPUT_DOWN;
		if ((pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || pad->sThumbLX < -16000) held |= GUIDE_INPUT_LEFT;
		if ((pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || pad->sThumbLX > 16000) held |= GUIDE_INPUT_RIGHT;
		if (pad->wButtons & XINPUT_GAMEPAD_START) held |= GUIDE_INPUT_START;
		if (pad->bAnalogButtons[guide_confirm_swap ? XINPUT_GAMEPAD_B : XINPUT_GAMEPAD_A] > 30) held |= GUIDE_INPUT_A;
		if (pad->bAnalogButtons[guide_confirm_swap ? XINPUT_GAMEPAD_A : XINPUT_GAMEPAD_B] > 30) held |= GUIDE_INPUT_B;
		if (pad->bAnalogButtons[XINPUT_GAMEPAD_X] > 30) held |= GUIDE_INPUT_X;
		if (pad->bAnalogButtons[XINPUT_GAMEPAD_Y] > 30) held |= GUIDE_INPUT_Y;
		/* (L and R: the bumpers, or with SELECT held the triggers) */
		if (select)
		{
			if (raw & VITA_BUTTON_L) held |= GUIDE_INPUT_LEFT_TRIGGER;
			if (raw & VITA_BUTTON_R) held |= GUIDE_INPUT_RIGHT_TRIGGER;
		}
		else
		{
			if (raw & VITA_BUTTON_L) held |= GUIDE_INPUT_LEFT_BUMPER;
			if (raw & VITA_BUTTON_R) held |= GUIDE_INPUT_RIGHT_BUMPER;
			/* (SELECT alone: Back, once let go of without L or R) */
		}
		{
			/* (SELECT's Back: on its release, unless it was a shift for L or R) */
			static int select_was, select_shifted;

			if (select && (raw & (VITA_BUTTON_L | VITA_BUTTON_R)))
				select_shifted = 1;
			if (!select && select_was && !select_shifted && !chord_down)
				held |= GUIDE_INPUT_BACK;
			if (!select)
				select_shifted = 0;
			select_was = select;
		}
		guide_held = held;
		released_buttons = pad->wButtons;
		released_analog = 0;
		for (index = 0; index < 8; index++)
		{
			if (pad->bAnalogButtons[index])
				released_analog |= (unsigned char)(1u << index);
		}
		memset(pad, 0, sizeof(*pad));
		return;
	}
	/* (what was held as the Guide closed: up until let go of) */
	released_buttons &= pad->wButtons;
	pad->wButtons &= (WORD)~released_buttons;
	for (index = 0; index < 8; index++)
	{
		if (!(released_analog & (1u << index)))
			continue;
		if (pad->bAnalogButtons[index])
			pad->bAnalogButtons[index] = 0;
		else
			released_analog &= (unsigned char)~(1u << index);
	}
}

/* (the Guide's battery line) 1 on the battery, 2 charging */
int platform_power_state(int *percent)
{
	return vita_host_power_state(percent);
}
#endif

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	memset(state, 0, sizeof(*state));
	if (device != (HANDLE)&gamepad || !gamepad.open)
		return ERROR_DEVICE_NOT_CONNECTED;
	platform_pump_events();
	state->dwPacketNumber = ++gamepad.packet;
	vita_pad_state(&state->Gamepad);
#ifdef HALO_GAME_BROWSER
	guide_take(&state->Gamepad);
#endif
	{
		/* (the system's keyboard is up: it reads the pad itself) */
		extern int vita_ime_active;

		if (vita_ime_active)
			memset(&state->Gamepad, 0, sizeof(state->Gamepad));
	}
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	(void)device;
	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct platform_keystroke next;

	/* (what the system's keyboard typed into a field: vita_platform.c) */
	memset(keystroke, 0, sizeof(*keystroke));
	if (!platform_next_keystroke(&next))
		return ERROR_HANDLE_EOF;
	keystroke->VirtualKey = next.virtual_key;
	keystroke->Ascii = next.ascii;
	keystroke->Flags = next.flags;
	return ERROR_SUCCESS;
}
