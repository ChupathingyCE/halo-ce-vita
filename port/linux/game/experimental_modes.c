/*
EXPERIMENTAL_MODES.C

Command and Firefight behind a setting, Blood Gulch alone:
experimental_modes.h.
*/

#include "experimental_modes.h"
#include "../src/lang.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int config_boolean(const char *name);
void platform_log(const char *format, ...);
void platform_show_message(char const *title, char const *text);

int experimental_modes_on(void)
{
	const char *value = getenv("HALO_EXPERIMENTAL_MODES");

	if (value && *value)
		return !strcmp(value, "true") || !strcmp(value, "1") || !strcmp(value, "on");
	return config_boolean("game.experimental_modes");
}

int experimental_modes_map_ok(const char *map)
{
	const char *name = map, *character;
	size_t length;

	if (!map)
		return 0;
	/* (the name after the last folder, without .map) */
	for (character = map; *character; character++)
	{
		if (*character == '\\' || *character == '/')
			name = character + 1;
	}
	/* (MCC's own copy, on an install from MCC alone: forge_map_path's
	levels\test\mcc_bloodgulch\mcc_bloodgulch - Blood Gulch too) */
	if (!strncasecmp(name, "mcc_", 4))
		name += 4;
	length = strlen(name);
	if (length >= 4 && !strcasecmp(name + length - 4, ".map"))
		length -= 4;
	return length == 10 && !strncasecmp(name, "bloodgulch", 10);
}

const char *experimental_modes_off_text(void)
{
#if defined(HALO_VITA)
	return T("Command and Firefight are experimental: turn them on in Settings (Game, Menus: Experimental modes).");
#else
	return T("Command and Firefight are experimental: turn them on with game.experimental_modes = true in config.toml.");
#endif
}

const char *experimental_modes_map_text(void)
{
	return T("Command and Firefight are experimental: they play on Blood Gulch only.");
}

int experimental_modes_refused(const char *mode, const char *map)
{
	static time_t said;
	const char *text = NULL;

	if (!experimental_modes_on())
		text = experimental_modes_off_text();
	else if (map && !experimental_modes_map_ok(map))
		text = experimental_modes_map_text();
	if (!text)
		return 0;
	platform_log("experimental modes: %s refused (%s%s%s)", mode ? mode : "a mode",
		experimental_modes_on() ? "not Blood Gulch: " : "game.experimental_modes is off", experimental_modes_on() && map ?
		map : "", "");
	if (time(NULL) != said)
	{
		said = time(NULL);
		platform_show_message(T("Halo: experimental mode"), text);
	}
	return 1;
}
