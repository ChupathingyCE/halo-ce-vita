/*
VITA_XINPUT.C

The Xbox's controller on the Vita (XInput, in place of the game's
xinput_sdl.c): one gamepad, the Vita itself, read through vita_pad.c (its
buttons, sticks, touch zones and gyroscope, with the controls Halo CE Vita
had: host/vita_input.c, vita_controls.c). There is no keyboard or mouse.
*/

#include "platform.h"
#include "sdl_platform.h"

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
	return 0;
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

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	memset(state, 0, sizeof(*state));
	if (device != (HANDLE)&gamepad || !gamepad.open)
		return ERROR_DEVICE_NOT_CONNECTED;
	platform_pump_events();
	state->dwPacketNumber = ++gamepad.packet;
	vita_pad_state(&state->Gamepad);
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
	memset(keystroke, 0, sizeof(*keystroke));
	return ERROR_HANDLE_EOF;
}
