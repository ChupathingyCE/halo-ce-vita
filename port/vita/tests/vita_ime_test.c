/*
VITA_IME_TEST.C

A desktop test of the system keyboard's hold on the touches (port/vita/host/
vita_ime.c, included whole; the system's dialog calls stood in for here):
every finger is the dialog's from its opening, while it is open, and for half
a second after it closes; then the front fingers down as it closed stay its
own until each lifts (8v audit: a finger held on OK past half a second), three
seconds at most (a thumb resting on the glass), and any other finger is the
game's - vita_input.c's touch_read gives the
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

/* a read of the front panel with these fingers (report ids; -1 ends) and a rear one if rear: which are the game's */
static int front_ids[8], front_count;

static void read_front(int a, int b)
{
	front_count = 0;
	if (a >= 0)
		front_ids[front_count++] = a;
	if (b >= 0)
		front_ids[front_count++] = b;
	vita_ime_touch_frame(front_ids, front_count);
}

static int game_has(int id)
{
	return !vita_ime_touch_blocked(1, id);
}

static void close_with_ok(int finger)
{
	char text[64];

	dialog_status = SCE_COMMON_DIALOG_STATUS_RUNNING;
	check(vita_ime_open("Search", "", 32, 0) == 0, "the keyboard opened");
	read_front(finger, -1);
	clock_us += 16000;
	dialog_status = SCE_COMMON_DIALOG_STATUS_FINISHED;
	check(vita_ime_poll(text, sizeof(text)) == 1, "closed with OK");
}

int main(void)
{
	char text[64];

	read_front(-1, -1);
	check(game_has(1) && !vita_ime_touch_blocked(0, 1), "no keyboard yet: the touches are the game's");
	check(vita_ime_open("Search", "", 32, 0) == 0, "the keyboard opened");
	read_front(1, -1);
	check(!game_has(1) && !game_has(7) && vita_ime_touch_blocked(0, 2),
		"open: no finger is the game's, front or rear (8u device test: the Workshop's cards tapped)");
	clock_us += 30000000;
	check(vita_ime_poll(text, sizeof(text)) == 0 && !game_has(1), "30 s of typing, still open: still not the game's");

	/* (the finger on OK still down as it closes) */
	read_front(5, -1);
	dialog_status = SCE_COMMON_DIALOG_STATUS_FINISHED;
	check(vita_ime_poll(text, sizeof(text)) == 1, "closed with OK, finger 5 on OK");
	read_front(5, -1);
	check(!game_has(5) && vita_ime_touch_blocked(0, 1), "at once: finger 5 and the rear not the game's");
	clock_us += 400000;
	read_front(5, 6);
	check(!game_has(5) && !game_has(6) && vita_ime_touch_blocked(0, 1), "0.4 s after: every finger the keyboard's, rear too");
	clock_us += 200000;
	read_front(5, 6);
	check(!game_has(5), "0.6 s after, finger 5 (down as it closed) still down: still the keyboard's (8v audit: never a new press)");
	check(game_has(6) && !vita_ime_touch_blocked(0, 1), "0.6 s after: finger 6, put down after the close, and the rear are the game's");
	clock_us += 1400000;
	read_front(5, -1);
	check(!game_has(5), "2 s after, finger 5 still held: still the keyboard's");
	read_front(-1, -1);
	read_front(5, -1);
	check(game_has(5), "finger 5 lifted, then id 5 down again: a new finger, the game's");

	/* (8v audit: a thumb resting on the glass, or a ghost touch, down as it closes and never lifting) */
	close_with_ok(9);
	read_front(9, -1);
	clock_us += 1000000;
	read_front(9, 10);
	check(!game_has(9) && game_has(10), "1 s after, the resting thumb 9 held: a new tap, 10, is the game's");
	check(!vita_ime_touch_blocked(0, 3), "1 s after: the rear pad is the game's");
	read_front(9, -1);
	clock_us += 1900000;
	read_front(9, -1);
	check(!game_has(9), "2.9 s after: thumb 9 still the keyboard's");
	clock_us += 200000;
	read_front(9, -1);
	check(game_has(9), "3.1 s after: nothing is the keyboard's any more, thumb 9 the game's");
	clock_us += 7000000;
	read_front(9, 11);
	check(game_has(9) && game_has(11), "10 s after: the thumb and a new tap the game's");
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
