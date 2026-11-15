/*
GAME_MANAGER.H

(port) The menus' GAME MANAGER, its GAMES tab (game_manager.c): Halo CE's files - the Xbox install (the maps folder's
maps, each read by its header, and the disc's default.xbe) and the MCC install (the Halo content folder's halo1\maps, the
same), whichever are there (both when both are) -, the PC files Custom Edition maps use (bitmaps.map, sounds.map,
loc.map) and the movies, and one status, Good or Needs attention, with what is wrong. The checks run on a thread of their
own, with a progress line. 1.1.x's, Halo CE alone (1.2.0's also has Halo 2 and Halo 3), and no import screen: what is
wrong is copied again by the player, or put right by running Setup again.
*/

#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

enum
{
	_game_manager_halo_ce,
	NUMBER_OF_GAME_MANAGER_GAMES,
	GAME_MANAGER_MAXIMUM_ISSUES = 24,
};

/* where an install's files are (the bits of game_manager_game.sources) */
enum
{
	_game_manager_source_xbox,
	_game_manager_source_mcc,
	NUMBER_OF_GAME_MANAGER_SOURCES
};

/* what is wrong with a file, for the menus to say in the player's language (not words here: game_manager.c runs on a thread) */
enum
{
	_game_manager_issue_not_a_map,
	/* (an Xbox map longer than its header says; an MCC map of another size than its header says: cut short or changed) */
	_game_manager_issue_changed,
	_game_manager_issue_missing,
	_game_manager_issue_unreadable,
	/* (default.xbe: not there, or not an Xbox program) */
	_game_manager_issue_program_missing,
	_game_manager_issue_program_damaged,
	/* (an MCC map in the Xbox maps folder: it is played from mcc/halo1/maps) */
	_game_manager_issue_wrong_folder,
	NUMBER_OF_GAME_MANAGER_ISSUES
};

struct game_manager_issue
{
	int kind;
	/* which install's file (_game_manager_source_*) */
	int source;
	/* the map's name, no extension */
	char name[40];
};

struct game_manager_game
{
	int installed;
	/* the installs there (bits of 1 << _game_manager_source_*) and each one's maps */
	int sources;
	int map_counts[NUMBER_OF_GAME_MANAGER_SOURCES];
	int map_count;
	unsigned long long bytes;
	/* 0 not checked, 1 checking (progress), 2 good, 3 needs attention */
	int state;
	int progress;
	/* what is wrong (the first few; issue_count is all of them) */
	struct game_manager_issue issues[GAME_MANAGER_MAXIMUM_ISSUES];
	int issue_count;
	/* (facts, not problems) the PC files there (bits: bitmaps.map 1, sounds.map 2, loc.map 4), and the movies */
	int pc_files;
	int movies;
};

/* the checks begun (again) on their thread, unless one is running */
void game_manager_scan(void);
/* a game's state, as it stands (copied under the lock) */
void game_manager_get(int game, struct game_manager_game *out);

#endif
