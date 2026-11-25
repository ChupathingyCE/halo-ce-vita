/*
SETTINGS_ROWS.C

The settings panel's rows (port/vita/host/vita_settings.c's settings[]) for a
build with no panel - the Linux harness the menus' tests run on -, so Menu
style Classic+'s option screens show the same rows there (settings_rows.h).
The PS Vita's build has the panel itself, whose rows these are: nothing here.
*/

#if !defined(HALO_VITA)
#define VITA_SETTINGS_ROWS_ONLY 1
#include "../../vita/host/vita_settings.c"
#endif
