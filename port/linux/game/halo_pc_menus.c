/*
HALO_PC_MENUS.C

Halo PC's own menu functions, for Menu style Classic on MCC's ui.map (the
menus' map when there is no Xbox ui.map: custom_edition_cache.c). Halo PC's
widgets run event handler functions numbered past the Xbox's 102 (Invader's
ui_widget_definition names them); ui_widget_event_handler_functions.c maps
two (107 QUIT, 169 a difficulty) and hands the others here. Each is mapped
to this build's own settings, so Halo PC's screens work as they stand:

	137       SETTINGS: the profile played with edited (Halo PC's current
	          profile: the active one, else the last used, else the first)
	121 122   MOUSE SETUP: init, OK. The PC builds: the mouse's settings
	133       (config.toml input.mouse_sensitivity, input.invert_mouse); the
	          Vita: the profile's look sensitivity and invert (its sticks),
	          kept as the profile is saved (its SAVE CHANGES)
	123 124   AUDIO SETUP: init, OK, DEFAULTS, cancel. MASTER, MUSIC and
	134 165   EFFECTS volume the settings panel's (audio.volume, ...)
	114 125   VIDEO SETUP: init, OK, DEFAULTS, the test dialog. GAMMA the
	135 145   settings panel's Brightness (display.brightness), a step a
	146 147   press; RESOLUTION and REFRESH RATE show this build's screen
	173
	          and are greyed

Rows of these screens with no equivalent here are greyed (as the Xbox's
"dim if no system link cable" greys a row) and skipped by up and down, and
pressing A on them does nothing; why is logged once (halo_pc_menu_render):
CONTROLS SETUP (Halo PC's keyboard binding editor; the Vita has no keyboard,
the PC builds' keys are config.toml's), GAMEPADS (DirectInput's gamepad
list), NETWORK SETUP (Halo PC's connection type and UDP ports: this build's
network finds its own), the sound card's rows (HARDWARE ACCELERATION, SOUND
QUALITY, ENVIRONMENTAL SOUND (EAX), SOUND VARIETY), the video card's
(FRAMERATE, SPECULAR, SHADOWS, DECALS, PARTICLES, TEXTURE QUALITY: the game
draws them as the Xbox does), the vertical mouse sensitivity (one
sensitivity for both axes here) and the multiplayer menu's network games
(none with MCC's maps in 1.1.3). Any other of Halo PC's functions fails as
before (custom_edition_cache_menu_handler_failed logs it).
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_groups.h"
#include "../src/port_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
boolean custom_edition_cache_menus_are_mcc_ui(void);
void ui_widget_port_spinner_text(struct widget_instance *spinner, wchar_t const *text);
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;
struct event_record;

/* ---------- the widgets, as ui_widget.c has them (controls_menu.c's) */

enum
{
	_pc_widget_type_container,
	_pc_widget_type_text_box,
	_pc_widget_type_spinner_list,
	_pc_widget_type_column_list,
};

struct pc_widget_animation
{
	short current_frame_index;
	short first_frame_index;
	short last_frame_index;
	short number_of_sprite_frames;
};

struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short local_player_index;
	short horizontal_offset;
	short vertical_offset;
	short type;
	boolean visible;
	boolean render_regardless_of_controller_index;
	boolean disabled;
	boolean pause_game_time;
	boolean delete_recursion_lock;
	boolean widget_is_error_dialog;
	boolean close_if_local_player_controller_present;
	byte pad17;
	long creation_time;
	unsigned long milliseconds_to_auto_close;
	unsigned long auto_close_fade_time;
	real alpha_modifier;
	struct widget_instance *previous;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *focused_child;
	union
	{
		struct
		{
			wchar_t *text;
			short string_list_index;
		} text_box;
		struct
		{
			short selected_index;
			short last_list_tab_direction;
			void *list_items;
			word number_of_items;
			struct widget_instance *extended_description;
			wchar_t *item_text;
		} list;
	} parameters;
	struct pc_widget_animation animation;
};

typedef char verify_pc_widget_instance_size[sizeof(struct widget_instance) == 0x58 ? 1 : -1];

/* ---------- Halo PC's functions (Invader's numbers) */

enum
{
	PC_PROFILE_SET_EDIT_BEGIN = 137,
	PC_MOUSE_INIT = 121,
	PC_MOUSE_CHANGE = 122,
	PC_MOUSE_DEFAULTS = 133,
	PC_AUDIO_INIT = 123,
	PC_AUDIO_CHANGE = 124,
	PC_AUDIO_DEFAULTS = 134,
	PC_AUDIO_CANCEL = 165,
	PC_VIDEO_INIT = 114,
	PC_VIDEO_CHANGE = 125,
	PC_VIDEO_DEFAULTS = 135,
	PC_VIDEO_TEST_INIT = 145,
	PC_VIDEO_TEST_DISPOSE = 146,
	PC_VIDEO_TEST_ACCEPT = 147,
	PC_VIDEO_DISPOSE = 173,
};

/* ---------- the rows: each spinner or row by its widget's name (the leaf of
its tag's path) */

enum
{
	_row_none,
	/* greyed: no equivalent here (its reason, below) */
	_row_greyed,
	/* AUDIO SETUP's volumes, 0 to 10 */
	_row_master_volume,
	_row_music_volume,
	_row_effects_volume,
	/* MOUSE SETUP */
	_row_horizontal_sensitivity,
	_row_vertical_sensitivity,
	_row_invert_vertical,
	/* VIDEO SETUP */
	_row_gamma,
	_row_resolution,
	_row_refresh,
	/* a greyed row's spinner, showing what this build does (pc_rows' shown) */
	_row_shown,
	/* VIDEO SETUP's FRAMERATE (NO VSYNC, VSYNC: the display's) */
	_row_framerate,
	/* the help line under a screen's rows */
	_row_audio_help,
	_row_video_help,
	NUMBER_OF_ROWS
};

static struct
{
	char const *leaf;
	short row;
	char const *why;
	/* (_row_shown: the item shown) */
	short shown;
} const pc_rows[] =
{
	/* (the main menu) */
	{ "main_menu_item_multiplayer", _row_greyed,
		"no network game with MCC's maps in this build, and Halo PC's multiplayer is network games alone (its "
		"EDIT GAMETYPES lists are code-generated scrolling lists this build has not)" },
	{ "main_menu_item_profile", _row_greyed,
		"Halo PC's profile list is one of its code-generated scrolling lists, which this build has not (the Xbox's "
		"profile list functions of the same numbers expect the Xbox's 3-wide spinner and crashed); SETTINGS edits "
		"the profile played with" },
	/* (the profile's screen) */
	{ "controller_setup_profile_item", _row_greyed,
		"Halo PC's keyboard binding editor: the Vita has no keyboard; the PC builds' keys are config.toml's" },
	{ "gamepads_profile_item", _row_greyed, "Halo PC's DirectInput gamepad list: the platform's controller is the one used" },
	{ "network_settings_profile_item", _row_greyed,
		"Halo PC's connection type and UDP ports: this build's network finds its own" },
	{ "color_profile_item", _row_greyed,
		"Halo PC's color list is one of its code-generated scrolling lists, which this build has not (the Xbox's "
		"color functions of the same numbers expect the Xbox's 3-wide spinner)" },
	/* (AUDIO SETUP) */
	{ "op_software_buffers", _row_greyed, "the sound card's hardware buffers: this build mixes in software" },
	{ "op_supplementary_buffers", _row_greyed, "the sound card's sound quality: this build plays the game's own" },
	{ "op_environmental_sound", _row_greyed, "EAX: this build has no environmental sound card effects" },
	{ "op_sound_variety", _row_greyed, "the sound variety: this build plays every permutation, as the Xbox does" },
	{ "software_buffers_spinner", _row_shown, NULL, 0 },
	{ "supplementary_buffers_spinner", _row_shown, NULL, 2 },
	{ "environmental_sound_spinner", _row_shown, NULL, 0 },
	{ "sound_variety_spinner", _row_shown, NULL, 2 },
	{ "master_volume_spinner", _row_master_volume, NULL },
	{ "music_volume_spinner", _row_music_volume, NULL },
	{ "effects_volume_spinner", _row_effects_volume, NULL },
	{ "audio_options_menu", _row_audio_help, NULL },
	/* (MOUSE SETUP) */
	{ "horizontal_sensitivity_spinner", _row_horizontal_sensitivity, NULL },
	{ "vertical_sensitivity_spinner", _row_vertical_sensitivity, NULL },
	{ "op_vertical_sensitivity", _row_greyed, "one look sensitivity for both axes here (the horizontal row's)" },
	{ "invert_vertical_spinner", _row_invert_vertical, NULL },
	/* (VIDEO SETUP) */
	{ "op_resolution", _row_greyed, "the screen's resolution is this build's own (shown)" },
	{ "op_refresh", _row_greyed, "the screen's refresh rate is this build's own (shown)" },
	{ "op_framerate", _row_greyed, "the frame rate is the settings panel's" },
	{ "op_specular", _row_greyed, "specular drawn as the Xbox draws it" },
	{ "op_shadows", _row_greyed, "shadows drawn as the Xbox draws them" },
	{ "op_decals", _row_greyed, "decals drawn as the Xbox draws them" },
	{ "op_particles", _row_greyed, "particles drawn as the Xbox draws them" },
	{ "op_texture_quality", _row_greyed, "textures drawn at the game's full detail" },
	{ "framerate_spinner", _row_framerate, NULL },
	{ "specular_spinner", _row_shown, NULL, 1 },
	{ "shadows_spinner", _row_shown, NULL, 1 },
	{ "decals_spinner", _row_shown, NULL, 1 },
	{ "particles_spinner", _row_shown, NULL, 2 },
	{ "texture_quality_spinner", _row_shown, NULL, 2 },
	{ "resolution_spinner", _row_resolution, NULL },
	{ "refresh_spinner", _row_refresh, NULL },
	{ "gamma_spinner", _row_gamma, NULL },
	{ "video_options_menu", _row_video_help, NULL },
	/* (MULTIPLAYER, were it reached: no network game with MCC's maps in 1.1.3) */
	{ "multiplayer_type_join_internet_item", _row_greyed, "no network game with MCC's maps in this build" },
	{ "multiplayer_type_join_lan_item", _row_greyed, "no network game with MCC's maps in this build" },
	{ "multiplayer_type_join_direct_item", _row_greyed, "no network game with MCC's maps in this build" },
	{ "multiplayer_type_create_internet_item", _row_greyed, "no network game with MCC's maps in this build" },
	{ "multiplayer_type_create_lan_item", _row_greyed, "no network game with MCC's maps in this build" },
};

/* the screens' values, read as a screen opens and set by its OK */
static struct
{
	/* (the widget's tag cache: a tag index and its row) */
	struct
	{
		long tag_index;
		short entry;
	} cache[256];
	boolean said[NUMBEROF(pc_rows)];
	/* the brightness the VIDEO SETUP opened with (its cancel puts it back) */
	long brightness_kept;
	boolean video_accepted;
	/* the gamma spinner's index last seen */
	short gamma_index;
	boolean gamma_seen;
	/* a row's value to take as it is first drawn (row_open_with), or NONE */
	long pending[NUMBER_OF_ROWS];
} pc_menus;

static char const *tag_leaf(long tag_index)
{
	char const *name = tag_index != NONE ? tag_get_name(tag_index) : NULL;
	char const *leaf = name ? strrchr(name, '\\') : NULL;

	return leaf ? leaf + 1 : name;
}

/* the entry of pc_rows for a widget, or NONE */
static short widget_entry(struct widget_instance const *widget)
{
	long slot = (widget->definition_tag_index & 0xFFFF) % (long)NUMBEROF(pc_menus.cache);
	char const *leaf;
	short entry;

	if (pc_menus.cache[slot].tag_index == widget->definition_tag_index && widget->definition_tag_index != 0)
		return pc_menus.cache[slot].entry;
	leaf = tag_leaf(widget->definition_tag_index);
	for (entry = 0; leaf && entry < (short)NUMBEROF(pc_rows); entry++)
	{
		if (!strcmp(leaf, pc_rows[entry].leaf))
			break;
	}
	if (!leaf || entry == (short)NUMBEROF(pc_rows))
		entry = NONE;
	pc_menus.cache[slot].tag_index = widget->definition_tag_index;
	pc_menus.cache[slot].entry = entry;
	return entry;
}

static short widget_row(struct widget_instance const *widget)
{
	short entry = widget_entry(widget);

	return entry == NONE ? _row_none : pc_rows[entry].row;
}

/* the first widget under (or at) widget with the row */
static struct widget_instance *find_row(struct widget_instance *widget, short row)
{
	struct widget_instance *child, *found;

	if (!widget)
		return NULL;
	if (widget_row(widget) == row)
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		if ((found = find_row(child, row)) != NULL)
			return found;
	}
	return NULL;
}

static struct widget_instance *topmost(struct widget_instance *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return widget;
}

static void spinner_set(struct widget_instance *spinner, long index)
{
	if (!spinner || spinner->type != _pc_widget_type_spinner_list || !spinner->parameters.list.number_of_items)
		return;
	spinner->parameters.list.selected_index = (short)PIN(index, 0, (long)spinner->parameters.list.number_of_items - 1);
}

/* (none pending: each screen's "created" sets its own) */
static void pending_reset(void)
{
	static boolean done;
	short row;

	if (done)
		return;
	for (row = 0; row < NUMBER_OF_ROWS; row++)
		pc_menus.pending[row] = NONE;
	done = TRUE;
}

/* a row's value as its screen opens: the screen's "created" runs before its
spinners have their items, so each takes it as it is first drawn */
static void row_open_with(short row, long index)
{
	pc_menus.pending[row] = index;
}

static long spinner_get(struct widget_instance *spinner, long otherwise)
{
	return spinner && spinner->type == _pc_widget_type_spinner_list ? spinner->parameters.list.selected_index : otherwise;
}

/* ---------- this build's settings */

/* a settings panel setting, set as menu_functions.c's setting_set sets it
(the Vita: kept in settings.txt; elsewhere config.toml) */
static void setting_write(char const *variable, char const *config, char const *text, int kind, double value)
{
	if (!(halo_test_setting_hook && halo_test_setting_hook(variable, text)))
	{
		setenv(variable, text, 1);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	}
#if !defined(HALO_VITA)
	if (kind == 0)
		config_write_real(config, value);
	else if (kind == 1)
		config_write_integer(config, (long)value);
	else
		config_write_boolean(config, value != 0.0);
#endif
}

static double setting_real(char const *variable, char const *config)
{
	char const *value = getenv(variable);

	return value && *value ? strtod(value, NULL) : config_real(config);
}

static struct
{
	char const *variable;
	char const *config;
} const volumes[] =
{
	{ "HALO_VOLUME", "audio.volume" },
	{ "HALO_MUSIC_VOLUME", "audio.music_volume" },
	{ "HALO_EFFECTS_VOLUME", "audio.effects_volume" },
};

/* a volume, 0 to 10 */
static long volume_get(short index)
{
	return PIN((long)(setting_real(volumes[index].variable, volumes[index].config) * 10.0 + 0.5), 0, 10);
}

static void volume_set(short index, long tenths)
{
	char text[16];

	tenths = PIN(tenths, 0, 10);
	snprintf(text, sizeof(text), "%ld.%ld", tenths / 10, tenths % 10);
	setting_write(volumes[index].variable, volumes[index].config, text, 0, tenths / 10.0);
}

static long brightness_get(void)
{
	char const *value = getenv("HALO_BRIGHTNESS");

	return PIN(value && *value ? atol(value) : config_integer("display.brightness"), 0, 10);
}

static void brightness_set(long brightness)
{
	char text[16];

	brightness = PIN(brightness, 0, 10);
	snprintf(text, sizeof(text), "%ld", brightness);
	setting_write("HALO_BRIGHTNESS", "display.brightness", text, 1, (double)brightness);
}

/* the look's sensitivity (1 to 10) and invert: the PC builds' mouse, the
Vita's profile (its sticks) */
static boolean look_get(long *sensitivity, boolean *invert)
{
#if defined(HALO_VITA) || defined(HALO_ANDROID)
	struct player_profile *profile = player_ui_get_edit_player_profile();

	if (!profile)
		return FALSE;
	*sensitivity = PIN(profile->controller_settings.look_sensitivity, 1, NUMBER_OF_LOOK_SENSITIVITY_SETTINGS);
	*invert = profile->controller_settings.invert_look != 0;
#else
	/* (0.2 a step: 1.0, the default, is 5) */
	*sensitivity = PIN((long)(setting_real("HALO_MOUSE_SENSITIVITY", "input.mouse_sensitivity") * 5.0 + 0.5), 1, 10);
	*invert = config_boolean("input.invert_mouse") != 0;
#endif
	return TRUE;
}

static void look_set(long sensitivity, boolean invert)
{
#if defined(HALO_VITA) || defined(HALO_ANDROID)
	struct player_profile *profile = player_ui_get_edit_player_profile();

	if (!profile)
		return;
	profile->controller_settings.look_sensitivity = (byte)PIN(sensitivity, 1, NUMBER_OF_LOOK_SENSITIVITY_SETTINGS);
	profile->controller_settings.invert_look = invert;
#else
	char text[16];

	sensitivity = PIN(sensitivity, 1, 10);
	snprintf(text, sizeof(text), "%.1f", sensitivity / 5.0);
	setting_write("HALO_MOUSE_SENSITIVITY", "input.mouse_sensitivity", text, 0, sensitivity / 5.0);
	setting_write("HALO_MOUSE_INVERT", "input.invert_mouse", invert ? "true" : "false", 2, invert ? 1.0 : 0.0);
#endif
}

/* ---------- the functions */

/* (137, SETTINGS) Halo PC's current profile edited: the one played with,
else the one last used, else the first there is; none: the handler fails
(nothing opens) */
static boolean profile_set_edit_begin(void)
{
	long profile_index = player_ui_get_active_player_profile_index(0);

	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index == NONE)
	{
		long indices[16];
		word count = NUMBEROF(indices);

		player_profiles_enumerate_available_to_local_player_index(0, &count, indices, FALSE);
		if (count)
			profile_index = indices[0];
	}
	if (player_ui_get_edit_player_profile())
		player_ui_end_editing_profile();
	if (profile_index == NONE)
	{
		platform_log("menus: Halo PC's SETTINGS: no profile to edit");
		return FALSE;
	}
	player_ui_begin_editing_profile(profile_index);
	platform_log("menus: Halo PC's SETTINGS: editing profile %08lx%s", (unsigned long)profile_index,
		player_ui_get_edit_player_profile() ? "" : " (could not be read)");
	return player_ui_get_edit_player_profile() != NULL;
}

static void mouse_init(struct widget_instance *screen)
{
	long sensitivity;
	boolean invert;

	if (!look_get(&sensitivity, &invert))
		return;
	row_open_with(_row_horizontal_sensitivity, sensitivity - 1);
	row_open_with(_row_invert_vertical, invert);
	platform_log("menus: Halo PC's MOUSE SETUP: sensitivity %ld, invert %s", sensitivity, invert ? "yes" : "no");
}

static void mouse_change(struct widget_instance *screen)
{
	long sensitivity = spinner_get(find_row(screen, _row_horizontal_sensitivity), -1) + 1;
	boolean invert = spinner_get(find_row(screen, _row_invert_vertical), 0) != 0;

	if (sensitivity <= 0)
		return;
	look_set(sensitivity, invert);
	platform_log("menus: Halo PC's MOUSE SETUP: set sensitivity %ld, invert %s", sensitivity, invert ? "yes" : "no");
}

static void audio_init(struct widget_instance *screen)
{
	row_open_with(_row_master_volume, volume_get(0));
	row_open_with(_row_music_volume, volume_get(1));
	row_open_with(_row_effects_volume, volume_get(2));
	platform_log("menus: Halo PC's AUDIO SETUP: master %ld, music %ld, effects %ld", volume_get(0), volume_get(1),
		volume_get(2));
}

static void audio_change(struct widget_instance *screen)
{
	short rows[] = { _row_master_volume, _row_music_volume, _row_effects_volume };
	short index;

	for (index = 0; index < (short)NUMBEROF(rows); index++)
	{
		long value = spinner_get(find_row(screen, rows[index]), -1);

		if (value >= 0)
			volume_set(index, value);
	}
	platform_log("menus: Halo PC's AUDIO SETUP: set master %ld, music %ld, effects %ld", volume_get(0), volume_get(1),
		volume_get(2));
}

static void video_init(struct widget_instance *screen)
{
	pc_menus.brightness_kept = brightness_get();
	pc_menus.video_accepted = FALSE;
	pc_menus.gamma_seen = FALSE;
	platform_log("menus: Halo PC's VIDEO SETUP: gamma (brightness) %ld", pc_menus.brightness_kept);
}

/* (each render of the gamma spinner) Halo PC's GAMMA: its two items both
read DARKEN / BRIGHTEN, left darkens and right brightens: a press is the
item changing, its way the spinner's last tab */
static void gamma_update(struct widget_instance *spinner)
{
	short index = spinner->parameters.list.selected_index;

	if (pc_menus.gamma_seen && index != pc_menus.gamma_index)
	{
		long step = spinner->parameters.list.last_list_tab_direction < 0 ? -1 : 1;

		brightness_set(brightness_get() + step);
		platform_log("menus: Halo PC's VIDEO SETUP: gamma (brightness) %ld", brightness_get());
	}
	pc_menus.gamma_index = index;
	pc_menus.gamma_seen = TRUE;
}

boolean halo_pc_menu_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	word function_index,
	boolean *widget_deleted,
	boolean *result)
{
	struct widget_instance *screen = topmost(widget);

	(void)event;
	(void)widget_deleted;
	if (!custom_edition_cache_menus_are_mcc_ui() || !widget)
		return FALSE;
	pending_reset();
	*result = TRUE;
	switch (function_index)
	{
	case PC_PROFILE_SET_EDIT_BEGIN:
		*result = profile_set_edit_begin();
		return TRUE;
	case PC_MOUSE_INIT:
		mouse_init(screen);
		return TRUE;
	case PC_MOUSE_CHANGE:
		mouse_change(screen);
		return TRUE;
	case PC_MOUSE_DEFAULTS:
		spinner_set(find_row(screen, _row_horizontal_sensitivity), 4);
		spinner_set(find_row(screen, _row_vertical_sensitivity), 4);
		spinner_set(find_row(screen, _row_invert_vertical), 0);
		return TRUE;
	case PC_AUDIO_INIT:
		audio_init(screen);
		return TRUE;
	case PC_AUDIO_CHANGE:
		audio_change(screen);
		return TRUE;
	case PC_AUDIO_DEFAULTS:
		spinner_set(find_row(screen, _row_master_volume), 10);
		spinner_set(find_row(screen, _row_music_volume), 10);
		spinner_set(find_row(screen, _row_effects_volume), 10);
		return TRUE;
	case PC_AUDIO_CANCEL:
		/* (the volumes are set by OK alone) */
		return TRUE;
	case PC_VIDEO_INIT:
		video_init(screen);
		return TRUE;
	case PC_VIDEO_CHANGE:
		/* (OK: the gamma is in force as it is changed; Halo PC's test
		dialog that follows asks to keep it) */
		pc_menus.video_accepted = TRUE;
		platform_log("menus: Halo PC's VIDEO SETUP: kept gamma (brightness) %ld", brightness_get());
		return TRUE;
	case PC_VIDEO_DEFAULTS:
		brightness_set(5);
		platform_log("menus: Halo PC's VIDEO SETUP: gamma (brightness) back to 5");
		return TRUE;
	case PC_VIDEO_TEST_INIT:
	case PC_VIDEO_TEST_ACCEPT:
	case PC_VIDEO_TEST_DISPOSE:
		return TRUE;
	case PC_VIDEO_DISPOSE:
		if (!pc_menus.video_accepted && brightness_get() != pc_menus.brightness_kept)
		{
			brightness_set(pc_menus.brightness_kept);
			platform_log("menus: Halo PC's VIDEO SETUP: cancelled, gamma (brightness) back to %ld",
				pc_menus.brightness_kept);
		}
		return TRUE;
	}
	return FALSE;
}

/* ---------- each render */

/* the screen's resolution and refresh rate (Halo PC's lists of the video
card's modes) */
static void screen_mode_text(short row, wchar_t *text, size_t size)
{
	char line[32];
	size_t index;

	{
		void platform_video_drawable_size(int *width, int *height);
		int width = 0, height = 0;

		platform_video_drawable_size(&width, &height);
		if (row == _row_resolution)
			snprintf(line, sizeof(line), "%d X %d", width, height);
		else
			snprintf(line, sizeof(line), "%s", "60 HZ");
	}
	for (index = 0; index + 1 < size && line[index]; index++)
		text[index] = (wchar_t)(unsigned char)line[index];
	text[index] = 0;
}

/* the help line under a screen's rows (its list's extended description):
the string of its focused row (Halo PC's: one per value, in its order) */
static void help_update(struct widget_instance *list, short row)
{
	struct widget_instance *help = list->parameters.list.extended_description;
	struct widget_instance *focus = list->focused_child;
	short focused = _row_none, index = NONE;

	if (!help || help->type != _pc_widget_type_text_box || !focus)
		return;
	/* (the focused option's spinner) */
	{
		struct widget_instance *child;

		for (child = focus->child; child && focused == _row_none; child = child->next)
			focused = widget_row(child);
	}
	switch (row)
	{
	case _row_audio_help:
		/* (master, effects, music) */
		index = focused == _row_master_volume ? 0 : focused == _row_effects_volume ? 1 :
			focused == _row_music_volume ? 2 : NONE;
		break;
	case _row_video_help:
		index = focused == _row_resolution ? 0 : focused == _row_refresh ? 1 : focused == _row_gamma ? 17 : NONE;
		break;
	}
	if (index != NONE)
		help->parameters.text_box.string_list_index = index;
}

boolean halo_pc_menu_row_greyed(struct widget_instance const *widget)
{
	short entry;

	if (!custom_edition_cache_menus_are_mcc_ui())
		return FALSE;
	entry = widget_entry(widget);
	return entry != NONE && pc_rows[entry].row == _row_greyed;
}

void halo_pc_menu_render(
	struct widget_instance *widget)
{
	short entry, row;

	if (!custom_edition_cache_menus_are_mcc_ui() || (entry = widget_entry(widget)) == NONE)
		return;
	pending_reset();
	row = pc_rows[entry].row;
	if (pc_menus.pending[row] != NONE && widget->type == _pc_widget_type_spinner_list &&
		widget->parameters.list.number_of_items)
	{
		spinner_set(widget, pc_menus.pending[row]);
		pc_menus.pending[row] = NONE;
	}
	switch (row)
	{
	case _row_greyed:
		widget->alpha_modifier = 0.333f;
		widget->disabled = TRUE;
		if (!pc_menus.said[entry])
		{
			pc_menus.said[entry] = TRUE;
			platform_log("menus: Halo PC's %s greyed: %s", pc_rows[entry].leaf, pc_rows[entry].why);
		}
		break;
	case _row_shown:
		spinner_set(widget, pc_rows[entry].shown);
		break;
	case _row_framerate:
		/* (VSYNC, or NO VSYNC where the display's vsync is off) */
		spinner_set(widget, config_boolean("display.vsync") ? 1 : 0);
		break;
	case _row_vertical_sensitivity:
		/* (the horizontal row's value: one for both axes) */
		spinner_set(widget, spinner_get(find_row(topmost(widget), _row_horizontal_sensitivity), 0));
		break;
	case _row_resolution:
	case _row_refresh:
	{
		wchar_t text[32];

		screen_mode_text(row, text, NUMBEROF(text));
		ui_widget_port_spinner_text(widget, text);
		break;
	}
	case _row_gamma:
		gamma_update(widget);
		break;
	case _row_audio_help:
	case _row_video_help:
		help_update(widget, row);
		break;
	}
}
