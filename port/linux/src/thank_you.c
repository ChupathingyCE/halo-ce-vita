/*
THANK_YOU.C

1.1.3's thank-you note (thank_you.h; the owner, Nov 23 2026: 1.1.3 is the
last Halo CE Vita update, and the Vita version goes on as Warthog Vita, the
PS Vita build of Chupathingy). The note, in the owner's words, is shown once
on each install: the first time the main menu is up with nothing over it -
no network game, lobby or join under way, no other message or question
open, the settings panel closed - for a few seconds
(port/linux/game/system_link_shortcut.c asks each frame, thank_you_update),
so it comes after the start-up's own messages and never in a game. Then it
is remembered in thank_you.txt in the data folder (HALO_DATA_ROOT; else
beside config.toml, as update_check.txt is). The Vita's settings panel has
it again on the Game tab (Thank you, under Updates: vita_settings.c).

It is the game's own message (platform_show_message): with Menu style Halo 3
or ElDewrito the Halo 3 dialog, else the settings panel's box on the Vita and
the system's box on PC. A run nobody watches (debug.exit_after,
debug.hidden_window, debug.null_renderer: the tests') skips it, and leaves
no thank_you.txt, unless HALO_TEST_THANK_YOU=1.
*/

#include "lang.h"
#include "port_config.h"
#include "thank_you.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEEN_FILE "thank_you.txt"
/* what thank_you.txt holds once the note was shown */
#define SEEN_TEXT "1.1.3\n"
/* the main menu up and nothing over it this long first */
#define SETTLE_MILLISECONDS 3000UL

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void platform_show_message(const char *title, const char *message);

const char *thank_you_title(void)
{
	return T("Thank you");
}

const char *thank_you_text(void)
{
	return T("This is the last Halo CE Vita update. Thank you to everyone who tested, sent logs and kept "
		"playing when stuff broke. It only got here because of you. Big thanks to the OpenCE folks and to "
		"Milenko and the ChupathingyCE crew.\n"
		"Halo CE Vita is heading in a new direction. The Vita version continues as Warthog Vita, the PS Vita "
		"build of Chupathingy, so the Vita gets the Delta Guide, Online Games, the Workshop and every "
		"Chupathingy update. It's still in the works, so keep playing 1.1.3 until it's out.\n"
		"See you online. - BirchWood");
}

void thank_you_show(const char *why)
{
	platform_log("thank you: the note shown (%s)", why ? why : "asked");
	platform_show_message(thank_you_title(), thank_you_text());
}

/* thank_you.txt: in the data folder, else beside config.toml */
static void seen_path(char *path, size_t size)
{
	const char *root = getenv("HALO_DATA_ROOT");

	if (root && *root)
		snprintf(path, size, "%s/%s", root, SEEN_FILE);
	else
	{
		config_folder(path, size);
		snprintf(path + strlen(path), size - strlen(path), "%s", SEEN_FILE);
	}
}

static int seen(void)
{
	char path[1024];
	size_t size = 0;
	char *text;
	int found;

	seen_path(path, sizeof(path));
	text = config_file_read(path, &size);
	if (!text)
		return 0;
	found = strncmp(text, "1.1.3", 5) == 0;
	free(text);
	return found;
}

static void seen_write(void)
{
	char path[1024];

	seen_path(path, sizeof(path));
	if (!config_file_write(path, SEEN_TEXT, strlen(SEEN_TEXT)))
		platform_log("thank you: cannot write %s (the note shows again next start)", path);
}

/* (a run nobody watches: the tests') */
static int unwatched(void)
{
	const char *test = getenv("HALO_TEST_THANK_YOU");

	if (test && *test)
		return atoi(test) == 0;
	return config_real("debug.exit_after") > 0.0 || config_boolean("debug.hidden_window") ||
		config_boolean("debug.null_renderer");
}

void thank_you_update(int ready, unsigned long now_milliseconds)
{
	/* 0 not looked yet, 1 waiting for the main menu, 2 done (shown, seen
	before, or a run nobody watches) */
	static int state;
	static unsigned long since;
	static int waiting;

	if (state == 2)
		return;
	if (!state)
	{
		if (seen())
		{
			state = 2;
			return;
		}
		if (unwatched())
		{
			platform_log("thank you: not shown (a run nobody watches)");
			state = 2;
			return;
		}
		state = 1;
	}
	if (!ready)
	{
		waiting = 0;
		return;
	}
	if (!waiting)
	{
		waiting = 1;
		since = now_milliseconds;
		return;
	}
	if (now_milliseconds - since < SETTLE_MILLISECONDS)
		return;
	state = 2;
	seen_write();
	thank_you_show("first start of 1.1.3");
}
