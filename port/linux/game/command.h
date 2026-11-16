/*
COMMAND.H

Command mode (command.c): a commander's game in a Halo CE multiplayer map.
The commander sees the map from above, orders the side's AI squads, and
drops into any of the side's units to play it first-person; leaving gives
the unit back to its AI. Nothing changes until it is started (the console's
"command on"). The design: docs/superpowers/specs/2026-11-05-command-mode-design.md
*/

#ifndef __COMMAND_H
#define __COMMAND_H

#include "cseries.h"

/* ---------- constants */

/* the mode's name, as the menus and the HUD say it (one lang key: the
owner's "Command mode", "COMMAND" in the menu) */
#define COMMAND_MODE_NAME "COMMAND"

/* ---------- prototypes/COMMAND.C */

/* a console command that starts with "command" (hs.c, as typed, init.txt,
HALO_TEST_COMMANDS): what follows the word. TRUE if it was understood */
boolean command_command(
	char const *arguments);

/* (the Forge menu's COMMAND, a widget's event that may run beside the
tick) command mode on or off, at the next command_update */
void command_toggle_request(
	void);

/* (the Forge menu's CONTROLS in command mode) the commander's controls,
as the player's buttons are named, several lines */
char const *command_controls_text(
	void);

/* (the Forge lobby's START COMMAND) the next Forge game a Command game: its
map built out as it loads, the commander above it once he has spawned */
void command_game_request(
	void);
/* (1.1.4: the settings panel's Start Command) the request forgotten, the
game not started */
void command_game_cancel(
	void);

/* (forge.c's forge_map_loaded, a map's tags loaded, after Forge's packs) the
last map's squads forgotten; a Command game asked for begun on its map
(Blood Gulch's Forge game): the map built out now */
void command_map_loaded(
	char const *scenario_name,
	boolean forge_game);

/* whether a Command game is played */
boolean command_game_running(
	void);
/* (editor6, Ark Editor's play) whether a Command game is asked for, on its
way or played */
boolean command_game_playing(
	void);

/* whether the commander's mode runs (its camera, or a unit dropped into) */
boolean command_active(
	void);

/* (director.c, each frame before the cameras update, beside Forge's) whether
this local player is the commander above the map: its flying camera then
moves by the commander's controls, and the player's own are not read */
struct forge_camera_input;
boolean command_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input);

/* (main.c, each frame once the cameras have updated, before the tick
starts, after forge_update) the commander's buttons, the squads, the
dropping in and out */
void command_update(
	void);

/* (actors.c, each tick after an actor's action control, beside Halo 2's
command scripts) a squad member's order steered: its move */
void command_actor_orders(
	long actor_index);

/* (game.c, each tick before the objects update) the dropships flown along
their paths */
void command_tick(
	void);

/* (damage.c, as damage reaches an object) a dropship's hull kept: shot
down once it has taken enough */
void command_object_damaged(
	long object_index,
	real damage,
	long owner_object_index);

/* (the Forge lobby's OBJECTIVE row) the Command game's objectives: the
row's words, and the next choice (territories and flag, territories, flag) */
char const *command_objective_text(
	void);
void command_objective_next(
	void);

/* (the Command lobby's AI COMMANDER row) the AI commander's difficulty
(Easy, Normal, Heroic, Legendary; the soldiers always fight on Legendary's
rules): the row's words, and the next */
char const *command_ai_level_text(
	void);
/* (1.1.4: the settings panel's Forge page) the objectives chosen (0 both, 1
territories, 2 flag) and the AI commander's difficulty (0 Easy to 3
Legendary) */
int command_objective_index(
	void);
int command_ai_level_index(
	void);
void command_ai_level_next(
	void);

/* (source/game/game_globals.c, source/objects/damage.c) a Command game is
played: every soldier, both sides', on Halo CE's Legendary rules (C13);
whether a damage's owner is a Command side's AI (its damage scaled as the
player's enemies' is) */
boolean command_legendary_rules(
	void);
boolean command_ai_damage_owner(
	long owner_object_index);

/* (PREPARING COMMAND: the lobbies' START COMMAND and START FIREFIGHT, the
modes' own packs - their soldiers, dropships and Shades - built once from the
player's campaign maps into pack files of their own) the lobby's builds
begun (paced); whether a pack is not built yet (asked for now if it was
not); the builds wanted now; each frame's progress (the state: 0 idle, 1
building, 2 stopping, 3 done, 4 failed; the words to show - the share done
and the time left -, the share done, 0 to 1); backing out (the builds go on
at the lobby's pace); whether a game follows, and which */
void command_packs_prebuild(
	boolean firefight);
boolean command_prepare_needed_for(
	boolean firefight);
boolean command_prepare_needed(
	void);
boolean command_prepare_begin_for(
	boolean firefight);
boolean command_prepare_begin(
	void);
short command_prepare_update(
	char *line,
	int line_size,
	real *progress);
void command_prepare_cancel(
	void);
boolean command_prepare_starts_game(
	void);
boolean command_prepare_firefight(
	void);
/* (a joiner of a network Command game, in its lobby: forge_net.inc) whether
Command's packs are still being built here, and how far (0 to 100) */
boolean command_packs_building(
	short *percent);

/* (forge.c, as a Forge game's variant is made) whether the game is to be a
Command game (its name the mode's) */
boolean command_game_asked(
	void);

/* (carnage_report.c, the game's report read) a Command game's: its two
sides as the teams, each its score, kills, losses, captures and territory
time, the players under their side; FALSE (the report as it was) if no
Command game is played */
struct carnage_report;
boolean command_carnage_report(
	struct carnage_report *report,
	boolean in_game);

/* (hud.c, as the HUD draws) the commander's HUD */
void command_draw(
	short local_player_index);

#endif
