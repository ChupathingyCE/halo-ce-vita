/*
VITA_IME.C

The system's keyboard (the IME dialog) for the settings panel's lines of
text: the Play page's lobby name and password (vita_settings.c). The
dialog is one of the system's common dialogs, drawn over the game's frames
while vgxm_common_dialog(1) holds (vita_gxm.c) and reading the pad itself;
the panel polls it once a frame (vita_ime_poll) and hands the game no
buttons meanwhile. The text goes in and out as UTF-16; the panel keeps only
printable ASCII (what the game's font and the game lists show).
*/

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/system_param.h>

#include <stdio.h>
#include <string.h>

#include "vita_gxm.h"
#include "vita_host.h"

/* (the dialog's text, and its title, as UTF-16) */
#define IME_TEXT_SIZE 64

static SceWChar16 ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static SceWChar16 ime_initial[IME_TEXT_SIZE];
static SceWChar16 ime_input[IME_TEXT_SIZE + 1];
static int ime_open;
/* (when it closed: the moment after it, 0 once the touches are all the game's again) and the front panel's fingers
down as it closed (their report ids), the dialog's until each lifts; the front fingers of the last read while it was
open */
static unsigned long long ime_closed_us;
#define IME_HELD_MAXIMUM 16
static int ime_held[IME_HELD_MAXIMUM], ime_held_count;
static int ime_seen[IME_HELD_MAXIMUM], ime_seen_count;
/* (whether a read has come since it closed) */
static int ime_read_since_close;
/* (how long after it closes every touch stays the dialog's - the finger that pressed its OK or Cancel lifting -; and
how long at most a finger down as it closed stays the dialog's: a thumb resting on the glass, a ghost touch) */
#define IME_AFTER_US 500000ULL
#define IME_HELD_MAXIMUM_US 3000000ULL
#define IME_FIRST_READ_US 100000ULL

static void ime_log(const char *step, int result)
{
	char line[128];

	snprintf(line, sizeof(line), "ime: %s -> 0x%08x", step, (unsigned int)result);
	vita_host_log(line);
}

/* UTF-8 text (a translated title, lang.c; the lobby names' printable
ASCII) as UTF-16, the Basic Multilingual Plane's */
static void ascii_to_utf16(SceWChar16 *out, int size, const char *text)
{
	int length = 0;

	while (text && *text && length < size - 1)
	{
		unsigned int character = (unsigned char)*text++;

		if (character >= 0xE0 && (text[0] & 0xC0) == 0x80 && (text[1] & 0xC0) == 0x80)
		{
			character = ((character & 0x0F) << 12) | (((unsigned char)text[0] & 0x3Fu) << 6) |
				((unsigned char)text[1] & 0x3Fu);
			text += 2;
		}
		else if (character >= 0xC0 && (text[0] & 0xC0) == 0x80)
		{
			character = ((character & 0x1F) << 6) | ((unsigned char)text[0] & 0x3Fu);
			text++;
		}
		while ((*text & 0xC0) == 0x80)
			text++;
		out[length++] = (SceWChar16)character;
	}
	out[length] = 0;
}

int vita_ime_open(const char *title, const char *text, int maximum_length, int password)
{
	static int configured;
	SceImeDialogParam parameters;
	int result;

	if (ime_open)
		return -1;
	if (!configured)
	{
		SceCommonDialogConfigParam configuration;
		int value;

		/* (the system's language and confirm button) */
		configured = 1;
		sceCommonDialogConfigParamInit(&configuration);
		if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &value) >= 0)
			configuration.language = (SceSystemParamLang)value;
		if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &value) >= 0)
			configuration.enterButtonAssign = (SceSystemParamEnterButtonAssign)value;
		sceCommonDialogSetConfigParam(&configuration);
	}
	if (maximum_length < 1 || maximum_length > IME_TEXT_SIZE)
		maximum_length = IME_TEXT_SIZE;
	ascii_to_utf16(ime_title, SCE_IME_DIALOG_MAX_TITLE_LENGTH, title);
	ascii_to_utf16(ime_initial, maximum_length + 1 < IME_TEXT_SIZE ? maximum_length + 1 : IME_TEXT_SIZE, text);
	memset(ime_input, 0, sizeof(ime_input));
	sceImeDialogParamInit(&parameters);
	parameters.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
	parameters.languagesForced = SCE_FALSE;
	parameters.type = SCE_IME_TYPE_BASIC_LATIN;
	parameters.option = 0;
	parameters.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
	parameters.textBoxMode = password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
	parameters.title = ime_title;
	parameters.maxTextLength = (SceUInt32)maximum_length;
	parameters.initialText = ime_initial;
	parameters.inputTextBuffer = ime_input;
	result = sceImeDialogInit(&parameters);
	ime_log("sceImeDialogInit", result);
	if (result < 0)
		return -1;
	ime_open = 1;
	ime_seen_count = 0;
	vgxm_common_dialog(1);
	return 0;
}

int vita_ime_poll(char *text, int size)
{
	SceImeDialogResult result;
	int status, code, length = 0, index;

	if (!ime_open)
		return -1;
	status = sceImeDialogGetStatus();
	if (status != SCE_COMMON_DIALOG_STATUS_FINISHED && status >= 0)
		return 0;
	vgxm_common_dialog(0);
	memset(&result, 0, sizeof(result));
	code = sceImeDialogGetResult(&result);
	ime_log("sceImeDialogGetResult", code);
	ime_log("sceImeDialogTerm", sceImeDialogTerm());
	ime_open = 0;
	ime_closed_us = sceKernelGetProcessTimeWide();
	/* (the fingers down as it closed: the last read's while it was open) */
	memcpy(ime_held, ime_seen, sizeof(ime_held));
	ime_held_count = ime_seen_count;
	ime_seen_count = 0;
	ime_read_since_close = 0;
	if (status < 0 || code < 0 || result.button != SCE_IME_DIALOG_BUTTON_ENTER)
	{
		memset(ime_input, 0, sizeof(ime_input));
		return -1;
	}
	for (index = 0; ime_input[index] && length < size - 1; index++)
		if (ime_input[index] >= 0x20 && ime_input[index] <= 0x7E)
			text[length++] = (char)ime_input[index];
	text[length] = 0;
	/* (what was typed - a password, maybe - kept here no longer) */
	memset(ime_input, 0, sizeof(ime_input));
	return 1;
}

/* 1.1.3-insider.8v: the touches are the dialog's while it is open and for a moment after it closes - the system's
keyboard does not keep them from the game: 8u device test, Nov 26, taps on the Workshop search's keyboard reached
the Workshop's cards behind it (card tapped -> details -> download, twice). The pad's buttons were already the
dialog's (vita_settings.c game_text_input, held_after_close). After it closes every touch stays the dialog's for
half a second; then the front panel's fingers that were down as it closed stay the dialog's until each lifts (8v
audit: the finger that pressed OK, still down at 0.5 s, came to the game as a new press), three seconds at most (a
thumb resting on the glass, or a ghost touch, must not keep the touches from the game); any other finger is the
game's */
void vita_ime_touch_frame(const int *front_ids, int count)
{
	int index, other, kept = 0;

	if (count > IME_HELD_MAXIMUM)
		count = IME_HELD_MAXIMUM;
	if (ime_open)
	{
		memcpy(ime_seen, front_ids, (size_t)count * sizeof(*front_ids));
		ime_seen_count = count;
		return;
	}
	if (!ime_closed_us)
		return;
	if (sceKernelGetProcessTimeWide() - ime_closed_us >= IME_HELD_MAXIMUM_US)
	{
		ime_closed_us = 0;
		ime_held_count = 0;
		return;
	}
	/* (a finger down as it closed that has lifted is gone: its id may come again as a new finger's) */
	for (index = 0; index < ime_held_count; index++)
	{
		for (other = 0; other < count && front_ids[other] != ime_held[index]; other++)
			;
		if (other < count)
			ime_held[kept++] = ime_held[index];
	}
	ime_held_count = kept;
	/* (a finger down at the first read after it closed counts as down as it closed: the read before was a frame ago -
	a read soon after alone, never one that came late) */
	if (!ime_read_since_close)
	{
		ime_read_since_close = 1;
		for (index = 0; index < count && ime_held_count < IME_HELD_MAXIMUM &&
			sceKernelGetProcessTimeWide() - ime_closed_us < IME_FIRST_READ_US; index++)
		{
			for (other = 0; other < ime_held_count && ime_held[other] != front_ids[index]; other++)
				;
			if (other == ime_held_count)
				ime_held[ime_held_count++] = front_ids[index];
		}
	}
	if (!ime_held_count && sceKernelGetProcessTimeWide() - ime_closed_us >= IME_AFTER_US)
		ime_closed_us = 0;
}

int vita_ime_touch_blocked(int front, int id)
{
	int index;

	if (ime_open)
		return 1;
	if (!ime_closed_us)
		return 0;
	if (sceKernelGetProcessTimeWide() - ime_closed_us < IME_AFTER_US)
		return 1;
	if (!front)
		return 0;
	for (index = 0; index < ime_held_count; index++)
		if (ime_held[index] == id)
			return 1;
	return 0;
}
