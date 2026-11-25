/*
SETTINGS_ROWS.H

The settings panel's rows looked up by name (port/vita/host/vita_settings.c's
settings[]: the one place a row's label, its values, their names and its help
are written), for Menu style Classic+'s option screens (menu_functions.c,
classic/options.xml), which show them in the Xbox's look and set them as the
panel does (setting_set: on the PS Vita vita_settings_set, through
halo_test_setting_hook - applied at once, kept in settings.txt). A name is a
row's environment variable (HALO_RENDER_SCALE), or the label of a row that
has none (an action's: "Reset controls"). The words are the panel's English:
T() them where they are shown (lang.h), so a translation the panel has the
screens have too.

On the PS Vita these read the panel's own rows as they are now (its choice,
the choices it filled in: the languages found). Elsewhere - the Linux harness
the menus' tests run on, which has no panel - port/linux/src/settings_rows.c
builds the same table alone (VITA_SETTINGS_ROWS_ONLY): a row's choice is its
environment variable's value, else the panel's default.

Only 32-bit scalars and pointers cross here: on the PS Vita the game's side
and the host's are built with different ABIs (tools/vita_build.py).
*/

#ifndef SETTINGS_ROWS_H
#define SETTINGS_ROWS_H

#ifdef __cplusplus
extern "C" {
#endif

/* what a row is */
enum
{
	/* a value left and right change */
	SETTINGS_ROW_CHOICE,
	/* something done when chosen (Reset controls, Check for updates...) */
	SETTINGS_ROW_ACTION,
	/* a line of text typed on the keyboard (Lobby name, Password) */
	SETTINGS_ROW_TEXT,
	/* a row that opens a page of the panel's */
	SETTINGS_ROW_PAGE,
};

/* the row of that name (its variable; a row without one, by its label): its
number, else -1 */
int halo_settings_row_find(const char *name);
/* its label and help line, English (T() them) */
const char *halo_settings_row_label(int row);
const char *halo_settings_row_help(int row);
/* SETTINGS_ROW_* */
int halo_settings_row_kind(int row);
/* its choices: how many, each one's value (the environment variable's) and
name (English, T() it) */
int halo_settings_row_count(int row);
const char *halo_settings_row_value(int row, int choice);
const char *halo_settings_row_name(int row, int choice);
/* the choice now */
int halo_settings_row_choice(int row);
/* whether a choice can be chosen (Stable updates on a pre-release build, the
Profile row's Custom - read, never chosen - can not) */
int halo_settings_row_offered(int row, int choice);
/* nonzero: a change applies after a restart */
int halo_settings_row_restart(int row);
/* an action row done as the panel's press does it, for those that need no
screen of the panel's: Reset controls, Check for updates (the answer:
update_check_status), Extract PC files. 0 done (or begun), -1 not possible
here (no installer, no panel) */
int halo_settings_row_act(int row);

#ifdef __cplusplus
}
#endif

#endif
