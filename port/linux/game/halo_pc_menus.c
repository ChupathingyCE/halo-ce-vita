/*
HALO_PC_MENUS.C

Halo PC's own menu functions, for Menu style Classic ("original") on MCC's ui.map (the
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

Halo PC's scrolling list (the second part of this file): its PROFILES,
CHANGE COLOR, SELECT LEVEL, LOAD GAME and EDIT GAMETYPES screens are one
column list each of eleven rows (ui\shell\main_menu\new_select\list_item_0
to 10: a text, the arrows of the chosen row, a scroll arrow up on the first
and down on the last) and a button bar, its items made in code and scrolled
under the rows (the Xbox's lists were 3-wide spinners, one child an item,
and the Xbox's functions of the same numbers that these lists run - 34 35
for the profiles, 5 7 8 the levels, 29 30 38 the game types, 61 62 63 the
colors, 74 75 the deletes, 96 97 a new profile - expect those spinners:
they are taken over here, ui_widget_event_handler_function_invoke), with
a panel to the right (the list's extended description) showing the chosen
item as the Xbox's spinner items showed each of theirs. Halo PC's own:

	160       a row's A: the row's item chosen and the list's "custom
	          activation" (its event 32) raised, which runs the screen's
	          choice (139 PROFILES: the profile played with; 8 SELECT LEVEL:
	          the level, then CHOOSE DIFFICULTY; 178 LOAD GAME: that save
	          resumed; 63 CHANGE COLOR: the color; 38 EDIT GAMETYPES: the
	          game type edited)
	164       the button bar's OK: the same activation for the chosen item
	128       DELETE: the X button, as Halo PC's mouse raised it (the
	          screen's "request delete", then the confirm dialog's OK)
	139       PROFILES: the chosen profile played with, as Halo PC's current
	174 175   CAMPAIGN: the campaign menu (CONTINUE, NEW CAMPAIGN, LOAD GAME)
	          where the profile has a save or progress, CONTINUE resuming the
	          save; else (Halo PC's own branch on failure) CHOOSE DIFFICULTY
	          for a new game on the first level
	176-178   LOAD GAME: the profiles with a campaign save, the save's level
	188 189   and difficulty at the right; one chosen is played with and
	          resumed; DELETE removes a save

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
(none with MCC's maps in 1.1.3; its EDIT GAMETYPES works). Any other of
Halo PC's functions fails as before (custom_edition_cache_menu_handler_failed
logs it).
*/

#include "cseries.h"
#include "interface/ui_widget.h"
#include "interface/player_ui.h"
#include "interface/event_manager.h"
#include "interface/virtual_keyboard.h"
#include "input/input.h"
#include "main/main.h"
#include "game/game_engine.h"
#include "saved games/player_profile.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "saved games/game_state.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "cseries/errors.h"
#include "custom_edition_maps.h"
#include "../src/port_config.h"
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
#include "halo_key_bindings.h"
#endif
#if defined(HALO_VITA)
#include "vita_controls.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
boolean custom_edition_cache_menus_are_mcc_ui(void);
int halo_menu_style_original(void);
void ui_widget_port_spinner_text(struct widget_instance *spinner, wchar_t const *text);
void ui_widget_port_give_focus(struct widget_instance *list, struct widget_instance *child);
char const *ui_widget_port_solo_level_map_name(short level);
void ui_widget_port_set_delete_profile_index(long profile_index);
boolean ui_widget_port_switch_to_single_player(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted);
boolean game_state_saved_game_header_in_directory(const char *directory, char *map_name, short *difficulty);
boolean game_state_delete_saved_game_in_directory(const char *directory);
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;

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
	/* (CONTROLS SETUP) */
	PC_CONTROLS_INIT = 113,
	PC_CONTROLS_BEGIN_BINDING = 115,
	PC_CONTROLS_DISPOSE = 126,
	PC_CONTROLS_CHANGE = 127,
	PC_CONTROLS_DEFAULTS = 136,
	PC_CONTROLS_BACK = 152,
	PC_CONTROLS_ADVANCED = 153,
	PC_CONTROLS_ADVANCED_OK = 154,
	/* (the scrolling lists) */
	PC_MOUSE_EMIT_X = 128,
	PC_PROFILE_MANAGER_DELETE = 138,
	PC_PROFILE_MANAGER_SELECT = 139,
	PC_LIST_ITEM_ACTIVATED = 160,
	PC_EMIT_CUSTOM_ACTIVATION = 164,
	PC_CAMPAIGN_MENU_INIT = 174,
	PC_CAMPAIGN_CONTINUE = 175,
	PC_LOAD_GAME_INIT = 176,
	PC_LOAD_GAME_DISPOSE = 177,
	PC_LOAD_GAME_ACTIVATED = 178,
	PC_LOAD_GAME_DELETE_REQUEST = 188,
	PC_LOAD_GAME_DELETE_FINISH = 189,
};

/* the Xbox's functions Halo PC's lists run (by the Xbox's numbers) */
enum
{
	XBOX_SOLO_LIST_INIT = 5,
	XBOX_SOLO_LIST_DISPOSE = 7,
	XBOX_SOLO_SET_MAP = 8,
	XBOX_VARIANTS_INIT = 29,
	XBOX_VARIANTS_DISPOSE = 30,
	XBOX_PROFILES_INIT = 34,
	XBOX_PROFILES_DISPOSE = 35,
	XBOX_VARIANT_BEGIN_EDITING = 38,
	XBOX_COLORS_INIT = 61,
	XBOX_COLORS_DISPOSE = 62,
	XBOX_COLOR_SELECT = 63,
	XBOX_DELETE_PROFILE_REQUEST = 74,
	XBOX_DELETE_VARIANT_REQUEST = 75,
	XBOX_NEW_CAMPAIGN_CHOSEN = 96,
	XBOX_NEW_CAMPAIGN_DECISION = 97,
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
	/* the campaign menu's CONTINUE: greyed where the profile has no save */
	_row_continue,
	/* CONTROLS SETUP's list of rows (its DEVICE and CATEGORY spinners and
	eight command rows) and its help line */
	_row_controls_list,
	_row_controls_help,
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
	/* (the main menu: MULTIPLAYER opens its menu, whose network rows are
	greyed below and whose EDIT GAMETYPES works; PROFILES its list) */
	/* (the campaign menu) */
	{ "menu_item_continue", _row_continue, NULL },
	/* (the profile's screen) */
#if defined(HALO_ANDROID)
	{ "controller_setup_profile_item", _row_greyed,
		"Halo PC's keyboard binding editor: this build's controls are the settings panel's" },
#endif
	{ "gamepads_profile_item", _row_greyed, "Halo PC's DirectInput gamepad list: the platform's controller is the one used" },
	/* (CONTROLS SETUP: the PC builds' keys and mouse settings, the Vita's
	buttons read only; its ADVANCED is the look sensitivity, MOUSE SETUP's) */
	{ "controls_options_menu", _row_controls_list, NULL },
	{ "controls_setup_help", _row_controls_help, NULL },
	{ "controls_button_advanced", _row_greyed, "the look sensitivity is MOUSE SETUP's here" },
	{ "network_settings_profile_item", _row_greyed,
		"Halo PC's connection type and UDP ports: this build's network finds its own" },
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
	/* the campaign menu's: whether the profile played with has a save */
	boolean save_valid;
	/* LOAD GAME's DELETE: the profile whose save the confirm dialog's OK deletes */
	long delete_save_profile;
} pc_menus;

/* Halo PC's own menus: MCC's ui.map, Menu style Classic (not Classic+,
whose screens there are the Xbox's: classic/xbox_screens.xml); the style
read again as the settings change */
static boolean halo_pc_menus_active(void)
{
	static unsigned long generation_seen = (unsigned long)-1;
	static boolean original;

	if (!custom_edition_cache_menus_are_mcc_ui())
		return FALSE;
	if (generation_seen != halo_settings_generation)
	{
		generation_seen = halo_settings_generation;
		original = halo_menu_style_original() != 0;
	}
	return original;
}

/* (ui_widget.c) whether Halo PC's own menus are up */
boolean halo_pc_menu_active(void)
{
	return halo_pc_menus_active();
}

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

/* ---------- Halo PC's scrolling list */

enum
{
	/* the rows a list shows (new_select\list_item_0 to 10) */
	PC_LIST_ROWS = 11,
	/* the most items a list holds (the Xbox enumerates 100 profiles) */
	PC_LIST_ITEMS = 160,
	/* the lists alive at once (one screen's, and the one it replaces as a
	screen changes) */
	PC_LISTS = 4,
	/* Halo PC's "custom activation" event, which a row's A and the button
	bar's OK raise on the list (the handlers' event 32: past the Xbox's
	buttons, sticks, created and deleted) */
	PC_EVENT_CUSTOM_ACTIVATION = 32,
	/* the virtual keyboard's caption for a new profile's name (the Xbox's
	"new campaign chosen" asks with it) */
	PC_KEYBOARD_PROFILE_NAME_CAPTION = 8,
	/* the game's stock campaign levels */
	PC_CAMPAIGN_LEVELS = 10,
	/* the Xbox's string for a level not reached (map_list, map_data) */
	PC_LEVEL_UNKNOWN_STRING = 10,
};

enum
{
	_pc_list_none,
	/* PROFILES: the player profiles (profile_manager_select_list_2) */
	_pc_list_profiles,
	/* CHANGE COLOR: the profile's primary colors (color_select_list_2) */
	_pc_list_colors,
	/* SELECT LEVEL: the campaign's levels, then the Custom Edition campaign
	maps (solo_level_select_list) */
	_pc_list_levels,
	/* LOAD GAME: the profiles with a campaign save (load_level_select_list) */
	_pc_list_saves,
	/* EDIT GAMETYPES: the game variants (playlist_select_list) */
	_pc_list_variants,
};

struct pc_list
{
	struct widget_instance *widget;
	short kind;
	/* the item under the first row */
	short top;
	/* the item chosen (the focused row's; kept while the button bar has
	the focus) */
	short cursor;
	short count;
	/* profile indices (profiles, saves, variants), colors, levels */
	long items[PC_LIST_ITEMS];
	/* (saves) each save's campaign level (NONE: a custom map) and difficulty */
	short level[PC_LIST_ITEMS];
	short difficulty[PC_LIST_ITEMS];
};

static struct pc_list pc_lists[PC_LISTS];

/* the name a new profile is given (the keyboard's buffer: "new campaign
chosen" fills it, "new campaign decision" reads it), and the screen that
asked (the keyboard closes it, and the decision runs on its deletion) */
static wchar_t pc_new_profile_name[128];
static short pc_new_profile_controller = NONE;

static long list_chosen(struct pc_list const *list);

static struct pc_list *list_find(struct widget_instance const *widget)
{
	short index;

	for (index = 0; widget && index < PC_LISTS; index++)
	{
		if (pc_lists[index].widget == widget)
			return &pc_lists[index];
	}
	return NULL;
}

/* the kind of Halo PC's list a widget is, by its tag's name, or none (the
Xbox's lists never: Xbox ui.map's widgets are never active here) */
static short list_kind_of(struct widget_instance const *widget)
{
	static struct
	{
		char const *leaf;
		short kind;
	} const kinds[] =
	{
		{ "profile_manager_select_list_2", _pc_list_profiles },
		{ "color_select_list_2", _pc_list_colors },
		{ "solo_level_select_list", _pc_list_levels },
		{ "load_level_select_list", _pc_list_saves },
		{ "playlist_select_list", _pc_list_variants },
	};
	char const *leaf;
	short index;

	if (!widget || widget->type != _pc_widget_type_column_list || !widget->child)
		return _pc_list_none;
	leaf = tag_leaf(widget->definition_tag_index);
	for (index = 0; leaf && index < (short)NUMBEROF(kinds); index++)
	{
		if (!strcmp(leaf, kinds[index].leaf))
			break;
	}
	if (!leaf || index == (short)NUMBEROF(kinds))
		return _pc_list_none;
	/* (its rows: new_select's list_item_0 first) */
	leaf = tag_leaf(widget->child->definition_tag_index);
	return leaf && !strcmp(leaf, "list_item_0") ? kinds[index].kind : _pc_list_none;
}

static struct widget_instance *nth_child(struct widget_instance *widget, short n)
{
	struct widget_instance *child = widget ? widget->child : NULL;

	while (child && n-- > 0)
		child = child->next;
	return child;
}

static short child_index(struct widget_instance const *widget)
{
	struct widget_instance const *child;
	short index = 0;

	if (!widget || !widget->parent)
		return NONE;
	for (child = widget->parent->child; child; child = child->next, index++)
	{
		if (child == widget)
			return index;
	}
	return NONE;
}

/* the first widget under (or at) widget whose tag's name ends in leaf */
static struct widget_instance *find_leaf(struct widget_instance *widget, char const *leaf)
{
	struct widget_instance *child, *found;
	char const *name;

	if (!widget)
		return NULL;
	name = tag_leaf(widget->definition_tag_index);
	if (name && !strcmp(name, leaf))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		if ((found = find_leaf(child, leaf)) != NULL)
			return found;
	}
	return NULL;
}

/* the nth widget under widget whose tag's name ends in leaf (the right
panel's three difficulty_indicator pictures) */
static struct widget_instance *find_leaf_nth(struct widget_instance *widget, char const *leaf, short *n)
{
	struct widget_instance *child, *found;
	char const *name;

	if (!widget)
		return NULL;
	name = tag_leaf(widget->definition_tag_index);
	if (name && !strcmp(name, leaf) && (*n)-- == 0)
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		if ((found = find_leaf_nth(child, leaf, n)) != NULL)
			return found;
	}
	return NULL;
}

static void shown(struct widget_instance *widget, boolean visible)
{
	if (widget)
		widget->visible = visible;
}

static void frame_set(struct widget_instance *widget, long frame)
{
	if (widget)
		widget->animation.current_frame_index = (short)frame;
}

static void string_index_set(struct widget_instance *widget, long index)
{
	if (widget && widget->type == _pc_widget_type_text_box)
		widget->parameters.text_box.string_list_index = (short)index;
}

/* a text box's text (its own, not its string list's), kept in the widgets'
pool as the Xbox's list code kept the item names */
static void text_set(struct widget_instance *widget, wchar_t const *text)
{
	size_t length, index;

	if (!widget || widget->type != _pc_widget_type_text_box || !text)
		return;
	for (length = 0; text[length] && length < 255; length++)
		;
	widget->parameters.text_box.text = ui_widget_realloc(widget->parameters.text_box.text,
		(word)((length + 1) * sizeof(wchar_t)), __FILE__, __LINE__);
	if (!widget->parameters.text_box.text)
		return;
	for (index = 0; index < length; index++)
		widget->parameters.text_box.text[index] = text[index];
	widget->parameters.text_box.text[length] = 0;
	widget->parameters.text_box.string_list_index = NONE;
}

static void text_set_ascii(struct widget_instance *widget, char const *text)
{
	wchar_t wide[256];
	size_t index;

	for (index = 0; text && text[index] && index < NUMBEROF(wide) - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
	text_set(widget, wide);
}

/* a string of one of the menus' string lists (the leaf of its tag's name
under ui\shell\main_menu, or a path), or NULL */
static wchar_t const *menu_string(char const *list_name, long index)
{
	long tag_index = tag_loaded(UNICODE_STRING_LIST_TAG, list_name);

	return tag_index != NONE && index >= 0 ? unicode_string_list_get_string(tag_index, (short)index) : NULL;
}

/* a level's name on one line (map_list's run to two) */
static void level_name_one_line(long level, wchar_t *text, size_t size)
{
	wchar_t const *name = menu_string("ui\\shell\\main_menu\\map_list", level);
	size_t out = 0;

	while (name && *name && out + 1 < size)
	{
		if (*name == L'\r')
		{
			name++;
			continue;
		}
		if (*name == L'\n')
		{
			if (out && text[out - 1] != L' ')
				text[out++] = L' ';
		}
		else
			text[out++] = *name;
		name++;
	}
	text[out] = 0;
}

static boolean profile_index_valid(long profile_index)
{
	return profile_index != NONE && (profile_index & 0x80000000) != 0;
}

/* the campaign level of a saved game's map, NONE for a custom map's */
static short level_of_map(char const *map_name)
{
	short level;

	for (level = 0; level < PC_CAMPAIGN_LEVELS; level++)
	{
		char const *name = ui_widget_port_solo_level_map_name(level);

		if (name && !csstrcasecmp(name, map_name))
			return level;
	}
	return NONE;
}

/* a profile's campaign save: its level and difficulty */
static boolean profile_saved_game(long profile_index, char *map_name, short *level, short *difficulty)
{
	char directory[256];

	if (!profile_index_valid(profile_index) || !player_profile_get_enclosing_directory_path(profile_index, directory))
		return FALSE;
	if (!game_state_saved_game_header_in_directory(directory, map_name, difficulty))
	{
		platform_log("menus: Halo PC's campaign: profile %08lx has no save in %s", (unsigned long)profile_index, directory);
		return FALSE;
	}
	*level = level_of_map(map_name);
	*difficulty = (short)PIN(*difficulty, 0, 3);
	return TRUE;
}

/* the profile played with (Halo PC's current profile): the active one,
else the one last used, else the first there is, made active; NONE with
no profile at all */
static long current_profile_index(void)
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
	if (profile_index != NONE && player_ui_get_active_player_profile_index(0) != profile_index)
	{
		struct player_profile profile;

		if (player_profile_get(profile_index, &profile))
			player_ui_set_active_player_profile(0, profile_index, &profile);
		else
			profile_index = NONE;
	}
	return profile_index;
}

/* ---------- the lists' items */

static void list_fill(struct pc_list *list)
{
	long cursor_item = NONE;
	short index;

	list->count = 0;
	list->top = 0;
	list->cursor = 0;
	switch (list->kind)
	{
	case _pc_list_profiles:
	{
		word count = PC_LIST_ITEMS;

		player_profiles_enumerate_available_to_local_player_index(0, &count, list->items, FALSE);
		list->count = (short)MIN(count, PC_LIST_ITEMS);
		cursor_item = player_ui_get_active_player_profile_index(0);
		if (cursor_item == NONE)
			cursor_item = player_ui_get_player1_last_used_profile_index();
		break;
	}
	case _pc_list_colors:
	{
		struct player_profile *profile = player_ui_get_edit_player_profile();

		list->count = (short)MIN(player_profile_number_of_available_primary_colors(), PC_LIST_ITEMS);
		for (index = 0; index < list->count; index++)
			list->items[index] = index;
		if (profile)
			cursor_item = PIN(profile->primary_color_index, 0, list->count - 1);
		break;
	}
	case _pc_list_levels:
	{
		short customs = custom_edition_maps_campaigns_find();

		for (index = 0; index < PC_CAMPAIGN_LEVELS; index++)
			list->items[index] = index;
		list->count = PC_CAMPAIGN_LEVELS;
		for (index = 0; index < customs && list->count < PC_LIST_ITEMS; index++)
		{
			if (custom_edition_maps_campaign_level_name(index))
				list->items[list->count++] = PC_CAMPAIGN_LEVELS + index;
		}
		cursor_item = PIN(player_ui_get_last_single_player_level_played(0), 0, PC_CAMPAIGN_LEVELS - 1);
		break;
	}
	case _pc_list_saves:
	{
		long profiles[PC_LIST_ITEMS];
		word count = PC_LIST_ITEMS;
		long active = player_ui_get_active_player_profile_index(0);

		player_profiles_enumerate_available_to_local_player_index(0, &count, profiles, FALSE);
		for (index = 0; index < (short)MIN(count, PC_LIST_ITEMS); index++)
		{
			char map_name[256];
			short level, difficulty;

			if (!profile_saved_game(profiles[index], map_name, &level, &difficulty))
				continue;
			list->items[list->count] = profiles[index];
			list->level[list->count] = level;
			list->difficulty[list->count] = difficulty;
			list->count++;
		}
		cursor_item = active;
		break;
	}
	case _pc_list_variants:
	{
		word count = PC_LIST_ITEMS;
		char directory[256];

		playlist_profiles_enumerate_available_to_local_player_index(0, &count, list->items);
		list->count = (short)MIN(count, PC_LIST_ITEMS);
		if (saved_game_file_retrieve_last_used_multiplayer_variant_directory(directory))
			cursor_item = saved_game_file_find_profile_index_for_directory_path(directory, _saved_game_file_type_game_variant);
		break;
	}
	}
	for (index = 0; cursor_item != NONE && index < list->count; index++)
	{
		if (list->items[index] == cursor_item)
		{
			list->cursor = index;
			break;
		}
	}
	/* (a profile just made: its index as made and as enumerated differ,
	its directory not) */
	if (cursor_item != NONE && index == list->count &&
		(list->kind == _pc_list_profiles || list->kind == _pc_list_saves))
	{
		char wanted[256], directory[256];

		if (player_profile_get_enclosing_directory_path(cursor_item, wanted))
		{
			for (index = 0; index < list->count; index++)
			{
				if (player_profile_get_enclosing_directory_path(list->items[index], directory) &&
					!csstrcasecmp(wanted, directory))
				{
					list->cursor = index;
					break;
				}
			}
		}
	}
	if (list->cursor >= PC_LIST_ROWS)
		list->top = (short)MIN(list->cursor - PC_LIST_ROWS + 1, list->count - PC_LIST_ROWS);
}

/* a row's text: the item's name */
static void list_item_text(struct pc_list const *list, short item, wchar_t *text, size_t size)
{
	text[0] = 0;
	if (item < 0 || item >= list->count)
		return;
	switch (list->kind)
	{
	case _pc_list_profiles:
	case _pc_list_saves:
	{
		struct player_profile profile;

		if (player_profile_get(list->items[item], &profile))
		{
			size_t index;

			for (index = 0; index + 1 < size && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1 && profile.player_name[index];
				index++)
			{
				text[index] = profile.player_name[index];
			}
			text[index] = 0;
		}
		break;
	}
	case _pc_list_colors:
	{
		wchar_t const *name = menu_string(
			"ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list",
			list->items[item]);
		size_t index;

		for (index = 0; name && index + 1 < size && name[index]; index++)
			text[index] = name[index];
		text[index] = 0;
		break;
	}
	case _pc_list_levels:
		if (list->items[item] < PC_CAMPAIGN_LEVELS)
			level_name_one_line(list->items[item], text, size);
		else
		{
			short display = custom_edition_maps_campaign_display_index((short)(list->items[item] - PC_CAMPAIGN_LEVELS));

			level_name_one_line(display == NONE ? PC_LEVEL_UNKNOWN_STRING : display, text, size);
		}
		break;
	case _pc_list_variants:
	{
		struct game_variant variant;

		if (playlist_profile_get(list->items[item], &variant))
		{
			size_t index;

			for (index = 0; index + 1 < size && index < NUMBEROF(variant.human_readable_game_description) &&
				variant.human_readable_game_description[index]; index++)
			{
				text[index] = variant.human_readable_game_description[index];
			}
			text[index] = 0;
		}
		break;
	}
	}
}

/* whether a campaign level may be played by the profile played with: its
first, one it has played, or the one after its highest (the Xbox's rule) */
static boolean level_available(struct player_profile const *profile, short level)
{
	short highest, difficulty;

	if (level >= PC_CAMPAIGN_LEVELS)
		return TRUE;
	player_profile_get_highest_completed_solo_level((struct player_profile *)profile, &highest, &difficulty);
	return level == 0 || profile->single_player_map_flags[level] != 0 || level == highest + 1;
}

/* the panel to the right of the list (its extended description): the
chosen item, as the Xbox's spinner items showed each of theirs */
static void list_panel_update(struct pc_list *list)
{
	struct widget_instance *panel = list->widget->parameters.list.extended_description;
	short item = list->cursor;

	if (!panel)
		return;
	switch (list->kind)
	{
	case _pc_list_profiles:
	{
		struct player_profile profile;
		boolean have = item < list->count && player_profile_get(list->items[item], &profile);
		wchar_t text[64];

		list_item_text(list, item, text, NUMBEROF(text));
		text_set(find_leaf(panel, "player_profile_right_name"), text);
		shown(find_leaf(panel, "player_profile_right_name"), have);
		shown(find_leaf(panel, "qtr_screen_profile_color_pic"), have);
		shown(find_leaf(panel, "empty_profile_slot_text"), !have);
		shown(find_leaf(panel, "current_level_label"), have);
		shown(find_leaf(panel, "current_level"), have);
		shown(find_leaf(panel, "skill_level_label"), have);
		shown(find_leaf(panel, "skill_level"), have);
		shown(find_leaf(panel, "joystick_controls_label"), have);
		shown(find_leaf(panel, "joystick_controls"), have);
		if (have)
		{
			short highest, difficulty;

			player_profile_get_highest_completed_solo_level(&profile, &highest, &difficulty);
			frame_set(find_leaf(panel, "qtr_screen_profile_color_pic"),
				PIN(profile.primary_color_index, 0, (long)player_profile_number_of_available_primary_colors() - 1));
			string_index_set(find_leaf(panel, "current_level"), MIN(highest + 1, PC_CAMPAIGN_LEVELS - 1));
			string_index_set(find_leaf(panel, "skill_level"), PIN(difficulty, 0, 3));
			string_index_set(find_leaf(panel, "joystick_controls"), profile.controller_settings.invert_look ? 1 : 0);
		}
		break;
	}
	case _pc_list_colors:
	{
		boolean have = item < list->count;

		shown(find_leaf(panel, "color_right_name"), have);
		shown(find_leaf(panel, "color_right_pic"), have);
		if (have)
		{
			string_index_set(find_leaf(panel, "color_right_name"), list->items[item]);
			frame_set(find_leaf(panel, "color_right_pic"), list->items[item]);
		}
		break;
	}
	case _pc_list_levels:
	case _pc_list_saves:
	{
		boolean have = item < list->count;
		long level = have ? (list->kind == _pc_list_levels ? list->items[item] : list->level[item]) : NONE;
		long display = level;
		struct player_profile profile;
		boolean marks = FALSE;
		short n;

		player_ui_get_active_player_profile(0, &profile);
		if (list->kind == _pc_list_levels && level >= PC_CAMPAIGN_LEVELS)
		{
			display = custom_edition_maps_campaign_display_index((short)(level - PC_CAMPAIGN_LEVELS));
			if (display == NONE)
				display = PC_LEVEL_UNKNOWN_STRING;
		}
		else if (level != NONE && level < PC_CAMPAIGN_LEVELS)
		{
			if (list->kind == _pc_list_levels && !level_available(&profile, (short)level))
				display = PC_LEVEL_UNKNOWN_STRING;
			else
				marks = list->kind == _pc_list_levels;
		}
		else if (level == NONE)
			display = PC_LEVEL_UNKNOWN_STRING;
		shown(find_leaf(panel, "replay_level_right_name"), have);
		shown(find_leaf(panel, "replay_level_right_pic"), have);
		shown(find_leaf(panel, "replay_level_right_data"), have);
		shown(find_leaf(panel, "load_level_right_name"), have);
		shown(find_leaf(panel, "load_level_right_pic"), have);
		shown(find_leaf(panel, "load_level_right_data"), have);
		if (have)
		{
			string_index_set(find_leaf(panel, "replay_level_right_name"), display);
			frame_set(find_leaf(panel, "replay_level_right_pic"), display);
			string_index_set(find_leaf(panel, "replay_level_right_data"), display);
			string_index_set(find_leaf(panel, "load_level_right_name"), display);
			frame_set(find_leaf(panel, "load_level_right_pic"), display);
			if (list->kind == _pc_list_saves)
			{
				/* (the save's difficulty, in the profile list's words) */
				wchar_t const *difficulty = menu_string(
					"ui\\shell\\main_menu\\player_profiles_select\\difficulty_names", list->difficulty[item]);

				text_set(find_leaf(panel, "load_level_right_data"), difficulty ? difficulty : L"");
			}
		}
		/* (the Xbox's marks: the level done, done on Legendary, done in
		co-op: the profile's flags, as its 3-wide list showed them) */
		for (n = 0; n < 3; n++)
		{
			short which = n;
			struct widget_instance *mark = find_leaf_nth(panel, "difficulty_indicator", &which);
			boolean mark_shown = FALSE;

			if (mark && marks && level < PC_CAMPAIGN_LEVELS)
				mark_shown = (profile.single_player_map_flags[level] >> (n + 1)) & 1;
			frame_set(mark, n + 1);
			shown(mark, have && mark_shown);
		}
		break;
	}
	case _pc_list_variants:
	{
		struct game_variant variant;
		boolean have = item < list->count && playlist_profile_get(list->items[item], &variant);
		wchar_t text[64];

		list_item_text(list, item, text, NUMBEROF(text));
		text_set(find_leaf(panel, "gametype_right_name"), text);
		shown(find_leaf(panel, "gametype_right_name"), have);
		shown(find_leaf(panel, "gametype_right_pic"), have);
		shown(find_leaf(panel, "gametype_right_data"), have);
		shown(find_leaf(panel, "locked_gametype_icon"), have && (list->items[item] & 0x40000000) != 0);
		if (have)
		{
			/* (the Xbox's ruleset pictures: ctf, king, slayer, oddball, race,
			unknown, and game_variant_descriptions' words: a stock
			variant's own, else the engine's with or without teams) */
			static short const frames[] = { 5, 0, 2, 3, 1, 4 };
			long engine = PIN(variant.game_engine_index, 0, 5);
			long description;

			frame_set(find_leaf(panel, "gametype_right_pic"), frames[engine]);
			if (TEST_FLAG(variant.flags, 0))
				description = (variant.flags >> 8) + 10;
			else
				description = engine >= 1 ? (engine - 1) * 2 + (variant.universal_variant.teams ? 1 : 0) : NONE;
			{
				wchar_t const *words = menu_string("ui\\shell\\strings\\game_variant_descriptions", description);

				text_set(find_leaf(panel, "gametype_right_data"), words ? words : L"");
			}
		}
		break;
	}
	}
}

/* each frame, before the list's rows draw: the items under the rows, the
chosen row's arrows, the scroll arrows, and the panel */
static void list_update(struct pc_list *list)
{
	struct widget_instance *focus = list->widget->focused_child;
	short focused_row = child_index(focus);
	short visible = (short)MIN(list->count - list->top, PC_LIST_ROWS);
	short row;

	if (focused_row != NONE && focused_row < visible)
		list->cursor = (short)(list->top + focused_row);
	for (row = 0; row < PC_LIST_ROWS; row++)
	{
		struct widget_instance *item = nth_child(list->widget, row);
		boolean row_shown = row < visible;

		if (!item)
			break;
		item->visible = row_shown;
		item->disabled = !row_shown;
		if (!row_shown)
			continue;
		{
			wchar_t text[128];

			list_item_text(list, (short)(list->top + row), text, NUMBEROF(text));
			text_set(find_leaf(item, "list_item_text"), text);
		}
		shown(find_leaf(item, "list_item_arrows"), list->top + row == list->cursor);
		shown(find_leaf(item, "scroll_up_button"), row == 0 && list->top > 0);
		shown(find_leaf(item, "scroll_down_button"), row == visible - 1 && list->top + visible < list->count);
	}
	list_panel_update(list);
}

/* a list opened (its "created"): its items, the chosen one in view with
the focus (none: the button bar's) */
static struct pc_list *list_open(struct widget_instance *widget, short kind)
{
	struct pc_list *list = list_find(widget);
	short index;

	if (!list)
	{
		for (index = 0; index < PC_LISTS && pc_lists[index].widget; index++)
			;
		if (index == PC_LISTS)
		{
			/* (none free: the oldest taken over) */
			index = 0;
		}
		list = &pc_lists[index];
	}
	memset(list, 0, sizeof(*list));
	list->widget = widget;
	list->kind = kind;
	list_fill(list);
	list_update(list);
	if (list->count)
		ui_widget_port_give_focus(widget, nth_child(widget, (short)(list->cursor - list->top)));
	else
		ui_widget_port_give_focus(widget, nth_child(widget, PC_LIST_ROWS));
	platform_log("menus: Halo PC's list %s: %d items, item %d chosen (%08lx; the profile played with %08lx)",
		tag_leaf(widget->definition_tag_index), list->count, list->cursor, (unsigned long)list_chosen(list),
		(unsigned long)player_ui_get_active_player_profile_index(0));
	return list;
}

static void list_close(struct widget_instance *widget)
{
	struct pc_list *list = list_find(widget);

	if (list)
		memset(list, 0, sizeof(*list));
}

/* (ui_widget.c, as up or down would move the focus off a row) the items
scrolled under the rows instead where there are more that way: TRUE when
they were */
boolean halo_pc_menu_list_step(
	struct widget_instance *widget,
	boolean forward)
{
	struct pc_list *list = list_find(widget);
	short row, visible;

	if (!list || !list->count || !halo_pc_menus_active())
		return FALSE;
	row = child_index(widget->focused_child);
	visible = (short)MIN(list->count - list->top, PC_LIST_ROWS);
	if (row == NONE || row >= visible)
		return FALSE;
	if (forward)
	{
		if (row + 1 < visible || list->top + visible >= list->count)
			return FALSE;
		list->top++;
	}
	else
	{
		if (row > 0 || list->top == 0)
			return FALSE;
		list->top--;
	}
	list->cursor = (short)(list->top + row);
	list_update(list);
	platform_log("menus: Halo PC's list %s: scrolled, item %d first, item %d chosen", tag_leaf(widget->definition_tag_index),
		list->top, list->cursor);
	return TRUE;
}

/* the list a widget of a list's screen belongs to: the list itself, a row
or the button bar (or a button of it) */
static struct pc_list *list_of(struct widget_instance *widget)
{
	struct pc_list *list;

	for (; widget; widget = widget->parent)
	{
		if ((list = list_find(widget)) != NULL)
			return list;
	}
	return NULL;
}

/* the list of the screen (the topmost widget) a widget is on */
static struct pc_list *list_on_screen(struct widget_instance *widget)
{
	struct widget_instance *screen = topmost(widget);
	short index;

	for (index = 0; screen && index < PC_LISTS; index++)
	{
		if (pc_lists[index].widget && topmost(pc_lists[index].widget) == screen)
			return &pc_lists[index];
	}
	return NULL;
}

/* a row's A (160): the row's item chosen, and the list's custom activation
raised (the event the list's handlers run their choice on); the button
bar's OK (164): the activation for the item chosen */
static boolean list_activate(struct widget_instance *widget, struct event_record *event, boolean from_row)
{
	struct pc_list *list = list_of(widget);
	short controller = event && event->controller_index >= 0 ? event->controller_index : 0;

	if (!list)
		return FALSE;
	if (from_row)
	{
		short row = child_index(widget);

		if (row != NONE && row < PC_LIST_ROWS && list->top + row < list->count)
			list->cursor = (short)(list->top + row);
	}
	if (!list->count)
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	event_manager_post_button(controller, PC_EVENT_CUSTOM_ACTIVATION);
	return TRUE;
}

/* the chosen item's value, NONE with none */
static long list_chosen(struct pc_list const *list)
{
	return list && list->cursor >= 0 && list->cursor < list->count ? list->items[list->cursor] : NONE;
}

/* ---------- the lists' choices */

/* (139, PROFILES) the chosen profile played with and remembered, as Halo
PC's current profile */
static boolean profiles_select(struct pc_list *list)
{
	long profile_index = list_chosen(list);
	struct player_profile profile;

	if (!profile_index_valid(profile_index) || !player_profile_get(profile_index, &profile))
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	if (player_ui_get_edit_player_profile())
		player_ui_end_editing_profile();
	player_ui_set_active_player_profile(0, profile_index, &profile);
	player_ui_remember_player1_profile(TRUE);
	platform_log("menus: Halo PC's PROFILES: profile %08lx played with", (unsigned long)profile_index);
	return TRUE;
}

/* (63, CHANGE COLOR) the chosen color the edited profile's */
static boolean colors_select(struct pc_list *list)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();
	long color = list_chosen(list);

	if (!profile || color == NONE)
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	profile->primary_color_index = (short)color;
	platform_log("menus: Halo PC's CHANGE COLOR: color %ld", color);
	return TRUE;
}

/* (8, SELECT LEVEL) the chosen level named for CHOOSE DIFFICULTY, which
starts it (the Xbox's "solo level set map", for a level the profile may
play) */
static boolean levels_select(struct pc_list *list)
{
	long level = list_chosen(list);
	char const *map_name = NULL;
	struct player_profile profile;

	if (level == NONE)
		return FALSE;
	player_ui_get_active_player_profile(0, &profile);
	if (level < PC_CAMPAIGN_LEVELS)
	{
		if (level_available(&profile, (short)level))
			map_name = ui_widget_port_solo_level_map_name((short)level);
	}
	else
		map_name = custom_edition_maps_campaign_level_name((short)(level - PC_CAMPAIGN_LEVELS));
	if (!map_name)
	{
		error(_error_silent, "this level is unavailable to you!");
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	player_ui_remember_player1_profile(FALSE);
	main_set_map_name(map_name);
	main_defer_map_map_change();
	platform_log("menus: Halo PC's SELECT LEVEL: %s", map_name);
	return TRUE;
}

/* a profile's campaign save resumed: the save's map and difficulty, then
"main menu switch to solo game" (the map loads its save:
game_state_try_and_load_from_persistent_storage) */
static boolean saved_game_resume(long profile_index, struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted)
{
	char map_name[256];
	short level, difficulty;
	struct player_profile profile;

	if (!profile_saved_game(profile_index, map_name, &level, &difficulty) || !player_profile_get(profile_index, &profile))
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	if (player_ui_get_active_player_profile_index(0) != profile_index)
		player_ui_set_active_player_profile(0, profile_index, &profile);
	player_ui_remember_player1_profile(TRUE);
	main_set_map_name(map_name);
	main_set_difficulty(difficulty);
	platform_log("menus: Halo PC's campaign: profile %08lx's save resumed: %s on difficulty %d",
		(unsigned long)profile_index, map_name, difficulty);
	return ui_widget_port_switch_to_single_player(widget, event, widget_deleted);
}

/* (174, the campaign menu's "created") whether the profile played with has
a save or progress: the menu stays (CONTINUE, NEW CAMPAIGN, LOAD GAME); else
Halo PC's own branch on failure, CHOOSE DIFFICULTY for a new game on the
first level */
static boolean campaign_menu_init(void)
{
	long profile_index = current_profile_index();
	char map_name[256];
	short level, difficulty, highest;
	struct player_profile profile;
	boolean progress = FALSE;

	pc_menus.save_valid = profile_index != NONE && profile_saved_game(profile_index, map_name, &level, &difficulty);
	if (profile_index != NONE && player_profile_get(profile_index, &profile))
	{
		player_profile_get_highest_completed_solo_level(&profile, &highest, &difficulty);
		progress = highest >= 0;
	}
	/* (the first level named for CHOOSE DIFFICULTY, which starts it: named
	and the change put off, as the Xbox's level list names one) */
	main_set_map_name(ui_widget_port_solo_level_map_name(0));
	main_defer_map_map_change();
	platform_log("menus: Halo PC's CAMPAIGN: profile %08lx %s", (unsigned long)profile_index,
		pc_menus.save_valid ? "has a save" : progress ? "has progress, no save" : "has no save: a new game");
	return pc_menus.save_valid || progress;
}

/* (169, CHOOSE DIFFICULTY's choice: ui_widget_event_handler_functions.c)
the profile played with the active one as the level starts (its save goes
to its directory), and said */
void halo_pc_menu_campaign_starting(void)
{
	long active;

	if (!halo_pc_menus_active())
		return;
	active = player_ui_get_active_player_profile_index(0);
	platform_log("menus: Halo PC's CHOOSE DIFFICULTY: starting %s with profile %08lx%s", main_get_map_name(),
		(unsigned long)active, active == NONE ? " (none active: the profile played with made active)" : "");
	if (active == NONE)
		current_profile_index();
}

/* (38, EDIT GAMETYPES) the chosen game type edited (then its screen, the
Xbox's: the handler opens it) */
static boolean variants_edit(struct pc_list *list)
{
	long profile_index = list_chosen(list);

	ui_widget_port_set_delete_profile_index(NONE);
	if (!profile_index_valid(profile_index))
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	if (profile_index & 0x40000000)
	{
		/* (a stock game type: read only, as the Xbox says) */
		display_error_deferred(31, NONE, TRUE, FALSE);
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	player_ui_begin_editing_profile(profile_index);
	platform_log("menus: Halo PC's EDIT GAMETYPES: game type %08lx edited", (unsigned long)profile_index);
	return TRUE;
}

/* (74 75 188: DELETE) the chosen one named for the confirm dialog's OK
(the Xbox's "final delete" reads it; LOAD GAME's 189 here) */
static boolean delete_request(struct pc_list *list)
{
	long profile_index = list_chosen(list);

	if (!profile_index_valid(profile_index) || (list->kind != _pc_list_saves && (profile_index & 0x40000000)))
	{
		ui_widget_port_set_delete_profile_index(NONE);
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	ui_widget_port_set_delete_profile_index(profile_index);
	pc_menus.delete_save_profile = list->kind == _pc_list_saves ? profile_index : NONE;
	return TRUE;
}

/* (189) the save of the profile chosen deleted */
static boolean saved_game_delete_finish(void)
{
	long profile_index = pc_menus.delete_save_profile;
	char directory[256];

	pc_menus.delete_save_profile = NONE;
	if (!profile_index_valid(profile_index) || !player_profile_get_enclosing_directory_path(profile_index, directory))
		return FALSE;
	platform_log("menus: Halo PC's LOAD GAME: profile %08lx's save deleted", (unsigned long)profile_index);
	return game_state_delete_saved_game_in_directory(directory);
}

/* (96, CREATE NEW and a first start's CAMPAIGN) a new profile's name asked
on the keyboard, the first untitled one offered (the Xbox's "new campaign
chosen") */
static boolean new_profile_chosen(struct event_record *event)
{
	wchar_t name[128];

	saved_game_file_get_useable_untitled_profile_name(name);
	memcpy(pc_new_profile_name, name, 11 * sizeof(wchar_t));
	pc_new_profile_name[11] = 0;
	pc_new_profile_controller = event && event->controller_index >= 0 ? event->controller_index : 0;
	if (!virtual_keyboard_launch(pc_new_profile_name, 24, PC_KEYBOARD_PROFILE_NAME_CAPTION))
	{
		error(_error_silent, "failed to invoke the virtual keyboard for a new profile name");
		return FALSE;
	}
	return TRUE;
}

/* (97, as the keyboard's screen closes) the profile made with the name
typed and played with. The Xbox's "new campaign decision" started the first
level too; Halo PC's screens go on to CHOOSE DIFFICULTY (CAMPAIGN) or back
to PROFILES, which starts nothing */
static boolean new_profile_decision(void)
{
	long profile_index = NONE;
	struct player_profile profile;
	short controller = pc_new_profile_controller != NONE ? pc_new_profile_controller : 0;

	if (!virtual_keyboard_last_exit_saved_text() || !pc_new_profile_name[0])
	{
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	player_ui_set_single_player_local_player_controller(0, controller);
	profile_index = player_profile_new(controller, pc_new_profile_name);
	if (profile_index == NONE)
	{
		wchar_t name[128];

		saved_game_file_get_useable_untitled_profile_name(name);
		memcpy(pc_new_profile_name, name, 11 * sizeof(wchar_t));
		pc_new_profile_name[11] = 0;
		profile_index = player_profile_new(controller, pc_new_profile_name);
	}
	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
	{
		display_error_deferred(37, NONE, TRUE, FALSE);
		ui_play_audio_feedback_sound(4);
		return FALSE;
	}
	player_ui_set_active_player_profile(0, profile_index, &profile);
	player_ui_remember_player1_profile(TRUE);
	main_set_map_name(ui_widget_port_solo_level_map_name(0));
	main_defer_map_map_change();
	platform_log("menus: Halo PC's new profile %08lx made and played with", (unsigned long)profile_index);
	return TRUE;
}

/* ---------- CONTROLS SETUP */

/* Halo PC's keyboard binding screen: DEVICE, CATEGORY (MOVEMENT, COMBAT,
ACTIONS, MISC.: its own spinner) and eight command rows, a label and the
keys each. The PC builds show and bind their own keys and mouse settings on
it (port/linux/game/controls_menu.c: the KEYBOARD & MOUSE page's actions by
its groups, A asking for a key, the mouse's rows stepped), in force at once
and put back by CANCEL or B; the Vita shows its buttons for each action
(the settings panel's Controls tab sets them) and the profile's look
settings, read only. */

enum
{
	PC_CONTROLS_ROWS = 8,
	PC_CONTROLS_GROUPS = 4,
	/* the Vita's rows' look settings (the mouse page's rows' numbers) */
	PC_CONTROLS_LOOK_SENSITIVITY = -2,
	PC_CONTROLS_INVERT_LOOK = -3,
};

short controls_menu_pc_action(short group, short row);
char const *controls_menu_pc_label(short action);
void controls_menu_pc_value(short action, char *text, size_t size);
void controls_menu_pc_accept(short action);
unsigned char controls_menu_pc_update(char *status, size_t size);
void controls_menu_pc_open(void);
unsigned char controls_menu_pc_cancel(void);
void controls_menu_pc_reset(void);
int controls_menu_available(void);

static struct
{
	struct widget_instance *screen;
	short group;
	boolean accepted;
	boolean capturing;
	char status[256];
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
	/* the keys as the screen opened (CANCEL and B put them back) */
	struct halo_bindings kept;
#endif
} pc_controls;

#if defined(HALO_VITA)
/* the Vita's rows: an action and the game control it is (the Xbox's
controller layout's order, player_ui.c: 0 jump 1 switch grenades 2 action
3 switch weapon 4 melee 5 flashlight 6 grenade 7 fire 8 start 9 back
10 crouch 11 zoom), or a look setting */
static struct
{
	char const *label;
	short control;
} const vita_rows[PC_CONTROLS_GROUPS][PC_CONTROLS_ROWS] =
{
	{ { "JUMP", 0 }, { "CROUCH", 10 } },
	{ { "FIRE", 7 }, { "THROW GRENADE", 6 }, { "MELEE", 4 }, { "SWITCH WEAPON", 3 }, { "SWITCH GRENADES", 1 },
		{ "ZOOM", 11 } },
	{ { "ACTION / RELOAD", 2 }, { "FLASHLIGHT", 5 }, { "SCOREBOARD", 9 }, { "PAUSE", 8 } },
	{ { "LOOK SENSITIVITY", PC_CONTROLS_LOOK_SENSITIVITY }, { "INVERT LOOK", PC_CONTROLS_INVERT_LOOK } },
};

/* the Vita button a game control is on: the profile's button preset gives
the Xbox button (player_ui.c's table), the settings panel's Controls tab
the Vita button it is on (vita_controls.h) */
static void vita_control_button(short control, char *text, size_t size)
{
	/* (the Xbox buttons of the standard preset, as VITA_XBOX_* numbers;
	Start is the Vita's Start) */
	static short const standard[12] = { VITA_XBOX_A, VITA_XBOX_BLACK, VITA_XBOX_X, VITA_XBOX_Y, VITA_XBOX_B,
		VITA_XBOX_WHITE, VITA_XBOX_LEFT_TRIGGER, VITA_XBOX_RIGHT_TRIGGER, NONE, VITA_XBOX_BACK, VITA_XBOX_LEFT_STICK,
		VITA_XBOX_RIGHT_STICK };
	static char const *const values[VITA_BUTTON_CHOICES] = { VITA_BUTTON_VALUES };
	static char const *const names[VITA_BUTTON_CHOICES] = { VITA_BUTTON_NAMES };
	struct player_profile *profile = player_ui_get_edit_player_profile();
	short buttons[12];
	struct vita_controls_config config;
	unsigned long button;
	short index;

	memcpy(buttons, standard, sizeof(buttons));
	if (profile)
	{
		short swap;

		switch (profile->controller_settings.button_preset)
		{
		case _button_preset_swap_triggers:
			swap = buttons[6]; buttons[6] = buttons[7]; buttons[7] = swap;
			break;
		case _button_preset_swap_a_and_left_trigger:
			swap = buttons[0]; buttons[0] = buttons[6]; buttons[6] = swap;
			break;
		case _button_preset_swap_b_and_left_trigger:
			swap = buttons[4]; buttons[4] = buttons[6]; buttons[6] = swap;
			break;
		case _button_preset_swap_b_and_right_thumb:
			swap = buttons[4]; buttons[4] = buttons[11]; buttons[11] = swap;
			break;
		}
	}
	if (control < 0 || control >= 12)
	{
		text[0] = 0;
		return;
	}
	if (buttons[control] == NONE)
	{
		snprintf(text, size, "%s", "Start");
		return;
	}
	vita_controls_config_load(&config);
	button = config.xbox_button[buttons[control]];
	for (index = 0; index < VITA_BUTTON_CHOICES; index++)
	{
		if (vita_button_named(values[index], 0) == button)
		{
			snprintf(text, size, "%s", names[index]);
			return;
		}
	}
	snprintf(text, size, "%s", names[VITA_BUTTON_CHOICES - 1]);
}
#endif

/* a row's label and value for the group, FALSE past the group's rows */
static boolean controls_row(short group, short row, char *label, size_t label_size, char *value, size_t value_size)
{
	label[0] = value[0] = 0;
	if (group < 0 || group >= PC_CONTROLS_GROUPS || row < 0 || row >= PC_CONTROLS_ROWS)
		return FALSE;
#if defined(HALO_VITA)
	{
		short control = vita_rows[group][row].control;
		struct player_profile *profile = player_ui_get_edit_player_profile();

		if (!vita_rows[group][row].label)
			return FALSE;
		snprintf(label, label_size, "%s", vita_rows[group][row].label);
		if (control == PC_CONTROLS_LOOK_SENSITIVITY)
			snprintf(value, value_size, "%d", profile ? (int)profile->controller_settings.look_sensitivity : 0);
		else if (control == PC_CONTROLS_INVERT_LOOK)
			snprintf(value, value_size, "%s", profile && profile->controller_settings.invert_look ? "YES" : "NO");
		else
			vita_control_button(control, value, value_size);
		return TRUE;
	}
#elif defined(HALO_ANDROID)
	(void)label_size;
	(void)value_size;
	return FALSE;
#else
	{
		short action = controls_menu_pc_action(group, row);

		if (action == -1)
			return FALSE;
		snprintf(label, label_size, "%s", controls_menu_pc_label(action));
		controls_menu_pc_value(action, value, value_size);
		return TRUE;
	}
#endif
}

/* the row of a command widget ("op_command_1" to 8), NONE for another */
static short controls_row_of(struct widget_instance const *widget)
{
	char const *leaf = widget ? tag_leaf(widget->definition_tag_index) : NULL;

	if (leaf && !strncmp(leaf, "op_command_", 11) && leaf[11] >= '1' && leaf[11] <= '8' && !leaf[12])
		return (short)(leaf[11] - '1');
	return NONE;
}

/* whether a command row has nothing in the group (greyed: skipped) */
static boolean controls_row_empty(struct widget_instance const *widget)
{
	short row = controls_row_of(widget);
	char label[64], value[64];

	return row != NONE && pc_controls.screen && !controls_row(pc_controls.group, row, label, sizeof(label), value,
		sizeof(value));
}

static void controls_init(struct widget_instance *screen)
{
	pc_controls.screen = screen;
	pc_controls.group = 0;
	pc_controls.accepted = FALSE;
	pc_controls.capturing = FALSE;
	pc_controls.status[0] = 0;
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
	pc_controls.kept = *halo_key_bindings();
#endif
	controls_menu_pc_open();
	platform_log("menus: Halo PC's CONTROLS SETUP opened%s", controls_menu_available() ? "" : " (read only)");
}

static void controls_dispose(void)
{
	controls_menu_pc_cancel();
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
	if (!pc_controls.accepted && memcmp(&pc_controls.kept, halo_key_bindings(), sizeof(pc_controls.kept)))
	{
		halo_key_bindings_write(&pc_controls.kept);
		platform_log("menus: Halo PC's CONTROLS SETUP: cancelled, the keys put back");
	}
#endif
	pc_controls.screen = NULL;
}

/* (115) A on a command row */
static boolean controls_begin_binding(struct widget_instance *widget)
{
	short row = controls_row_of(widget);

	if (row == NONE || !controls_menu_available())
		return TRUE;
#if !defined(HALO_VITA) && !defined(HALO_ANDROID)
	controls_menu_pc_accept(controls_menu_pc_action(pc_controls.group, row));
#endif
	return TRUE;
}

/* (152, B) a key asked for: the asking cancelled, the screen stays; else
back to the profile's screen, as Halo PC's own handler went */
static boolean controls_back(struct event_record *event)
{
	void ui_widget_port_go_back(short screen_index);
	short index = event && event->controller_index >= 0 && event->controller_index < 4 ? event->controller_index : 0;

	if (controls_menu_pc_cancel())
		return TRUE;
	ui_widget_port_go_back(index);
	return TRUE;
}

/* each frame: the spinners, the rows of the group and the help line */
static void controls_update(struct widget_instance *list)
{
	struct widget_instance *screen = topmost(list);
	struct widget_instance *spinner;
	char text[256];
	short row;

	if (pc_controls.screen != screen)
		controls_init(screen);
	pc_controls.capturing = controls_menu_pc_update(pc_controls.status, sizeof(pc_controls.status));
	if ((spinner = find_leaf(list, "device_spinner")) != NULL)
	{
		wchar_t device[32];
		char const *name;
		size_t index;

#if defined(HALO_VITA)
		name = "PS VITA";
#else
		name = "KEYBOARD & MOUSE";
#endif
		for (index = 0; name[index] && index < NUMBEROF(device) - 1; index++)
			device[index] = (wchar_t)(unsigned char)name[index];
		device[index] = 0;
		ui_widget_port_spinner_text(spinner, device);
	}
	if ((spinner = find_leaf(list, "command_group_spinner")) != NULL && spinner->type == _pc_widget_type_spinner_list)
		pc_controls.group = (short)PIN(spinner->parameters.list.selected_index, 0, PC_CONTROLS_GROUPS - 1);
	for (row = 0; row < PC_CONTROLS_ROWS; row++)
	{
		char leaf[24], label[64], value[64];
		struct widget_instance *item;
		boolean have;

		snprintf(leaf, sizeof(leaf), "op_command_%d", row + 1);
		if ((item = find_leaf(list, leaf)) == NULL)
			continue;
		have = controls_row(pc_controls.group, row, label, sizeof(label), value, sizeof(value));
		item->visible = have;
		item->disabled = !have;
		if (!have)
			continue;
		text_set_ascii(find_leaf(item, "command_1_label"), label);
		text_set_ascii(find_leaf(item, "command_1_button"), value);
		shown(find_leaf(item, "command_mk_button_container"), FALSE);
	}
	/* (a row gone with the group: the focus to CATEGORY) */
	if (list->focused_child && controls_row_empty(list->focused_child))
	{
		struct widget_instance *group = find_leaf(list, "op_command_group");

		if (group)
			ui_widget_port_give_focus(list, group);
	}
	{
		struct widget_instance *help = find_leaf(screen, "controls_setup_help");

		if (pc_controls.status[0])
			snprintf(text, sizeof(text), "%s", pc_controls.status);
		else if (!controls_menu_available())
			snprintf(text, sizeof(text), "%s", "The Vita's buttons for each action (the settings panel's Controls tab sets them).");
		else
			snprintf(text, sizeof(text), "%s", "A asks for a key or mouse button for the action (its first free one). DEFAULTS: Halo MCC's keys.");
		text_set_ascii(help, text);
	}
}

/* ---------- the functions */

/* (137, SETTINGS) Halo PC's current profile edited: the one played with,
else the one last used, else the first there is; none: a new one (Halo PC
makes one as it first starts) */
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
		/* (none yet: one made, as Halo PC makes its first profile, with the
		first free untitled name, and played with) */
		wchar_t name[128];
		struct player_profile profile;

		saved_game_file_get_useable_untitled_profile_name(name);
		name[11] = 0;
		profile_index = player_profile_new(0, name);
		if (profile_index != NONE && player_profile_get(profile_index, &profile))
			player_ui_set_active_player_profile(0, profile_index, &profile);
		platform_log("menus: Halo PC's SETTINGS: no profile yet: %s", profile_index != NONE ? "one made" :
			"none could be made");
		if (profile_index == NONE)
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
	if (!halo_pc_menus_active() || !widget)
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
	/* (CONTROLS SETUP) */
	case PC_CONTROLS_INIT:
		controls_init(screen);
		return TRUE;
	case PC_CONTROLS_DISPOSE:
		controls_dispose();
		return TRUE;
	case PC_CONTROLS_CHANGE:
		pc_controls.accepted = TRUE;
		platform_log("menus: Halo PC's CONTROLS SETUP: OK");
		return TRUE;
	case PC_CONTROLS_DEFAULTS:
		controls_menu_pc_reset();
		return TRUE;
	case PC_CONTROLS_BEGIN_BINDING:
		*result = controls_begin_binding(widget);
		return TRUE;
	case PC_CONTROLS_BACK:
		*result = controls_back(event);
		return TRUE;
	case PC_CONTROLS_ADVANCED:
	case PC_CONTROLS_ADVANCED_OK:
		return TRUE;
	/* (the scrolling lists) */
	case PC_LIST_ITEM_ACTIVATED:
		*result = list_activate(widget, event, TRUE);
		return TRUE;
	case PC_EMIT_CUSTOM_ACTIVATION:
		*result = list_activate(widget, event, FALSE);
		return TRUE;
	case PC_MOUSE_EMIT_X:
		/* (DELETE: the X button raised, as Halo PC's mouse raised it; the
		screen's or the list's X handler asks to confirm) */
		event_manager_post_button(event && event->controller_index >= 0 ? event->controller_index : 0,
			_gamepad_analog_button_x);
		return TRUE;
	case PC_PROFILE_MANAGER_SELECT:
		*result = profiles_select(list_on_screen(widget));
		return TRUE;
	case PC_PROFILE_MANAGER_DELETE:
		*result = delete_request(list_on_screen(widget));
		return TRUE;
	case PC_CAMPAIGN_MENU_INIT:
		*result = campaign_menu_init();
		return TRUE;
	case PC_CAMPAIGN_CONTINUE:
		*result = saved_game_resume(current_profile_index(), widget, event, widget_deleted);
		return TRUE;
	case PC_LOAD_GAME_INIT:
		*result = list_open(widget, _pc_list_saves) != NULL;
		return TRUE;
	case PC_LOAD_GAME_DISPOSE:
		list_close(widget);
		return TRUE;
	case PC_LOAD_GAME_ACTIVATED:
		*result = saved_game_resume(list_chosen(list_on_screen(widget)), widget, event, widget_deleted);
		return TRUE;
	case PC_LOAD_GAME_DELETE_REQUEST:
		*result = delete_request(list_on_screen(widget));
		return TRUE;
	case PC_LOAD_GAME_DELETE_FINISH:
		*result = saved_game_delete_finish();
		return TRUE;
	}
	return FALSE;
}

/* an Xbox function (by the Xbox's numbers) run by a widget of Halo PC's
list screens: taken over where the Xbox's expects its 3-wide spinner (the
list itself, its screen's deletes, a new profile); TRUE when it was */
boolean halo_pc_menu_xbox_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	word function_index,
	boolean *widget_deleted,
	boolean *result)
{
	short kind;

	(void)widget_deleted;
	if (!halo_pc_menus_active() || !widget)
		return FALSE;
	pending_reset();
	*result = TRUE;
	kind = list_kind_of(widget);
	switch (function_index)
	{
	case XBOX_PROFILES_INIT:
	case XBOX_SOLO_LIST_INIT:
	case XBOX_VARIANTS_INIT:
	case XBOX_COLORS_INIT:
		if (kind == _pc_list_none)
			return FALSE;
		*result = list_open(widget, kind) != NULL;
		return TRUE;
	case XBOX_PROFILES_DISPOSE:
	case XBOX_SOLO_LIST_DISPOSE:
	case XBOX_VARIANTS_DISPOSE:
	case XBOX_COLORS_DISPOSE:
		if (kind == _pc_list_none)
			return FALSE;
		list_close(widget);
		return TRUE;
	case XBOX_SOLO_SET_MAP:
		if (kind != _pc_list_levels)
			return FALSE;
		*result = levels_select(list_find(widget));
		return TRUE;
	case XBOX_COLOR_SELECT:
		if (kind != _pc_list_colors)
			return FALSE;
		*result = colors_select(list_find(widget));
		return TRUE;
	case XBOX_VARIANT_BEGIN_EDITING:
		if (kind != _pc_list_variants)
			return FALSE;
		*result = variants_edit(list_find(widget));
		return TRUE;
	case XBOX_DELETE_PROFILE_REQUEST:
	case XBOX_DELETE_VARIANT_REQUEST:
	{
		/* (the screen's X: its list's chosen one) */
		struct pc_list *list = list_on_screen(widget);

		if (!list || (list->kind != _pc_list_profiles && list->kind != _pc_list_variants))
			return FALSE;
		*result = delete_request(list);
		return TRUE;
	}
	case XBOX_NEW_CAMPAIGN_CHOSEN:
		*result = new_profile_chosen(event);
		return TRUE;
	case XBOX_NEW_CAMPAIGN_DECISION:
		*result = new_profile_decision();
		return TRUE;
	}
	return FALSE;
}

/* a game data input of a widget on MCC's ui.map: Halo PC's lists' updates
(the Xbox's 3-wide lists' numbers: 14 the profiles, 19 the colors, and its
own past the Xbox's 40: 51 the levels, 53 54 the game types, 55 the saves,
46 the button bars) are done as the lists draw (halo_pc_menu_render); any
of Halo PC's own past the Xbox's does nothing here; TRUE when taken */
boolean halo_pc_menu_game_data_invoke(
	struct widget_instance *widget,
	word function)
{
	if (!halo_pc_menus_active() || !widget)
		return FALSE;
	if (list_find(widget) || list_kind_of(widget) != _pc_list_none)
		return TRUE;
	/* (7, the Xbox's "difficulty menu update desc": Halo PC's CHOOSE
	DIFFICULTY panel has a third child, the profile's name, which the
	Xbox's update asserts against (its line 568, skipped each frame): the
	same update, the picture and words of the focused difficulty) */
	if (function == 7 && widget->type == _pc_widget_type_column_list && widget->parameters.list.extended_description)
	{
		struct widget_instance *panel = widget->parameters.list.extended_description;
		char const *leaf = tag_leaf(panel->definition_tag_index);
		short index = child_index(widget->focused_child);

		if (!leaf || strcmp(leaf, "difficulty_ext_desc") || !panel->child || !panel->child->next)
			return FALSE;
		if (index != NONE && index < 4)
		{
			frame_set(panel->child, index);
			string_index_set(panel->child->next, index);
		}
		return TRUE;
	}
	return function >= 40 && function < 256;
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

	if (!halo_pc_menus_active() || !widget)
		return FALSE;
	entry = widget_entry(widget);
	if (entry != NONE)
	{
		if (pc_rows[entry].row == _row_continue)
			return !pc_menus.save_valid;
		return pc_rows[entry].row == _row_greyed;
	}
	/* (a row of one of Halo PC's lists with no item under it, or a command
	row of CONTROLS SETUP with none in its group) */
	{
		struct pc_list const *list = widget->parent ? list_find(widget->parent) : NULL;
		short row = list ? child_index(widget) : NONE;

		if (row != NONE)
			return row < PC_LIST_ROWS && row >= MIN(list->count - list->top, PC_LIST_ROWS);
	}
	return controls_row_empty(widget);
}

void halo_pc_menu_render(
	struct widget_instance *widget)
{
	short entry, row;
	struct pc_list *list;

	if (!halo_pc_menus_active())
		return;
	if ((list = list_find(widget)) != NULL)
	{
		list_update(list);
		return;
	}
	if ((entry = widget_entry(widget)) == NONE)
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
	case _row_controls_list:
		controls_update(widget);
		break;
	case _row_controls_help:
		/* (set as the list draws) */
		break;
	case _row_continue:
		/* (CONTINUE: greyed where the profile played with has no save) */
		widget->alpha_modifier = pc_menus.save_valid ? 1.0f : 0.333f;
		widget->disabled = !pc_menus.save_valid;
		break;
	}
}
