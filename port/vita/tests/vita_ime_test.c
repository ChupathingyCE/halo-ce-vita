/*
VITA_IME_TEST.C

A desktop test of the system keyboard's hold on the touches (port/vita/host/
vita_ime.c, included whole; the system's dialog calls stood in for here):
vita_ime_blocking is 1 from the dialog's opening, while it is open, and for
half a second after it closes, then 0 - vita_input.c's touch_read gives the
game no fingers meanwhile. 1.1.3-insider.8v, 8u device test, Nov 26: taps
on the Workshop search's keyboard reached the Workshop's cards behind it.

Run by run_vita_settings_test.sh.
*/

#include "../host/vita_ime.c"

#include <stdio.h>

static SceUInt64 clock_us = 1000000;
static int dialog_status = SCE_COMMON_DIALOG_STATUS_RUNNING;
static int failures, checks;

void *sceClibMemset(void *destination, int value, SceSize size)
{
	return memset(destination, value, size);
}
SceUInt64 sceKernelGetProcessTimeWide(void)
{
	return clock_us;
}
SceInt32 sceImeDialogInit(const SceImeDialogParam *param)
{
	(void)param;
	return 0;
}
SceCommonDialogStatus sceImeDialogGetStatus(void)
{
	return (SceCommonDialogStatus)dialog_status;
}
SceInt32 sceImeDialogGetResult(SceImeDialogResult *result)
{
	result->button = SCE_IME_DIALOG_BUTTON_ENTER;
	return 0;
}
SceInt32 sceImeDialogTerm(void)
{
	return 0;
}
int sceCommonDialogSetConfigParam(const SceCommonDialogConfigParam *configParam)
{
	(void)configParam;
	return 0;
}
int sceAppUtilSystemParamGetInt(unsigned int paramId, int *value)
{
	(void)paramId, (void)value;
	return -1;
}
void vgxm_common_dialog(int active)
{
	(void)active;
}
void vita_host_log(const char *line)
{
	printf("  %s\n", line);
}

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

int main(void)
{
	char text[64];

	check(!vita_ime_blocking(), "no keyboard yet: the touches are the game's");
	check(vita_ime_open("Search", "", 32, 0) == 0 && vita_ime_blocking(), "the keyboard opened: the touches are not the game's");
	clock_us += 30000000;
	check(vita_ime_poll(text, sizeof(text)) == 0 && vita_ime_blocking(),
		"30 s of typing, the keyboard still open: still not the game's (8u device test: the Workshop's cards tapped)");
	dialog_status = SCE_COMMON_DIALOG_STATUS_FINISHED;
	check(vita_ime_poll(text, sizeof(text)) == 1 && vita_ime_blocking(),
		"closed with OK: not the game's at once (the finger that pressed OK still down)");
	clock_us += 400000;
	check(vita_ime_blocking(), "0.4 s after: still not the game's");
	clock_us += 200000;
	check(!vita_ime_blocking(), "0.6 s after: the game's again");
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
