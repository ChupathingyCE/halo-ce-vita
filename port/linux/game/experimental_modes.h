/*
EXPERIMENTAL_MODES.H

Command (command.c) and Firefight (forge_modes.inc) are experimental in
1.1.3 (the owner, Nov 21 2026: "lock them behind a setting so people can
enable them but express that they're experimental, and only allow them to
play Blood Gulch"): one setting, game.experimental_modes
(HALO_EXPERIMENTAL_MODES; the Vita's settings panel, Game, Menus: Experimental
modes), off by default. Off, their rows are hidden in every menu style and
nothing starts or joins them (a plain message points to the setting); on,
they are shown marked EXPERIMENTAL and play Blood Gulch alone.
*/

#ifndef __EXPERIMENTAL_MODES_H
#define __EXPERIMENTAL_MODES_H

/* whether the setting is on (its variable, live, as the panel sets it; else
config.toml's) */
int experimental_modes_on(void);

/* whether a map - a scenario path (levels\test\bloodgulch\bloodgulch), a
map file's name (bloodgulch.map) or a short name - is one the experimental
modes play: Blood Gulch */
int experimental_modes_map_ok(const char *map);

/* the plain words for a refusal: the setting is off; the map is not Blood
Gulch (translated: lang.h) */
const char *experimental_modes_off_text(void);
const char *experimental_modes_map_text(void);

/* a Command or Firefight game asked for (mode: "Command", "Firefight"; for
the log) on map (NULL: not known yet): nonzero if it is refused - the
setting off, or another map than Blood Gulch -, logged and said in a message
(once a second at most) */
int experimental_modes_refused(const char *mode, const char *map);

#endif
