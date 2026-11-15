/*
GAME_MANAGER.C

(game_manager.h) The checks of the menus' GAME MANAGER, GAMES tab, for 1.1.x: Halo CE alone (1.2.0's game_manager.c, its
Halo 2 and Halo 3 left out, and no import screen). Both installs Halo CE plays from, whichever are there:
- the Xbox's: the maps folder's maps (d:\maps\), each read by its header - the magic, and the file size the header says
  against the real one: an Xbox map is stored packed, so only a file longer than its header says is seen as changed -,
  its campaign's maps and ui.map looked for, the disc's default.xbe read for its "XBEH";
- MCC's: the Halo content folder's halo1\maps (d:\mcc\halo1\maps\, only read, as everywhere), each map read the same
  (an MCC map is not packed: any other size than its header's is a copy that stopped or a changed file), its campaign's
  maps looked for (and its ui.map when there is no Xbox install: the menus are then MCC's);
the PC files (bitmaps.map, sounds.map, loc.map: the maps folder's, else MCC's halo1\maps\custom_edition\) and the movies
counted. An MCC map in the Xbox maps folder is said to be in the wrong folder (the game plays it from mcc/halo1/maps).
The checks run on a thread with a stack of its own size (1.2.0's 3fc33030a: on the PS Vita a thread made with attributes
and no size gets the SDK's 4 KiB minimum, and GAME MANAGER's opening overran it); the menus' thread only reads what they
say.
*/

#include "game_manager.h"
#include "mcc_content.h"

#include "../src/posix.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
void platform_log(char const *format, ...);
const char *cache_files_map_directory(void);
int halo_content_library_found(void);
void *platform_read_file_in_parent_folder(const char *xbox_directory, const char *name, long maximum_size,
	long *size, char *host_path, unsigned long host_path_size);
void platform_read_file_free(void *bytes);

/* the checks' thread's stack (game_manager_scan; 1.2.0's 3fc33030a) */
#define GAME_MANAGER_STACK_BYTES (128 * 1024)
/* MCC's Halo CE maps folder, and its PC files' (the Halo content folder's) */
#define GAME_MANAGER_MCC_MAPS "d:\\mcc\\halo1\\maps\\"
#define GAME_MANAGER_MCC_PC_FILES "d:\\mcc\\halo1\\maps\\custom_edition\\"
/* the cache header's versions: the Xbox's, MCC's (Halo: CE Anniversary's) */
#define XBOX_CACHE_VERSION 5
#define MCC_CACHE_VERSION 13

/* the campaign's maps an install must have (the disc's; tools/installer XBOX_REQUIRED_MAPS), then the menus' */
static char const *const campaign_maps[] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};
/* the PC files, in game_manager_game.pc_files' bits; MCC's halo1\maps has a bitmaps.map of its own too (a resource
file, no map) */
static char const *const pc_files[] = { "bitmaps", "sounds", "loc" };

#define NUMBER_OF_CAMPAIGN_MAPS ((int)(sizeof(campaign_maps) / sizeof(campaign_maps[0])))

static struct
{
	pthread_mutex_t lock;
	int running;
	struct game_manager_game games[NUMBER_OF_GAME_MANAGER_GAMES];
} manager = { PTHREAD_MUTEX_INITIALIZER };

/* (what the worker is given by the menus' thread: the folders, looked up there) */
struct game_manager_job
{
	char maps[256];
	int content;
};

static struct game_manager_job job;

/* an Xbox-style folder (d:\\maps\\) as a host path, without its closing slash */
static void host_folder(char const *source, char *host, size_t size)
{
	size_t length;

	platform_translate_path(source, host, (unsigned long)size);
	length = strlen(host);
	while (length > 1 && (host[length - 1] == '/' || host[length - 1] == '\\'))
		host[--length] = 0;
}

static unsigned long read_le32(unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

/* the first bytes of a file by its Xbox path (the game's fopen takes them); -1 if it cannot be opened */
static int read_start(char const *xbox_path, void *buffer, size_t size)
{
	FILE *file = fopen(xbox_path, "rb");
	size_t got;

	if (!file)
		return -1;
	got = fread(buffer, 1, size, file);
	fclose(file);
	return (int)got;
}

static int file_size_of(char const *host_path, unsigned long long *size)
{
	struct posix_file_information information;

	*size = 0;
	if (posix_stat(host_path, &information) != 0)
		return 0;
	*size = ((unsigned long long)information.size_high << 32) | information.size_low;
	return 1;
}

/* (the lock held) one more thing wrong */
static void issue(struct game_manager_game *game, int kind, int source, char const *name)
{
	platform_log("game manager: %s: issue %d (%s)", name, kind, source == _game_manager_source_mcc ? "MCC" : "Xbox");
	if (game->issue_count < GAME_MANAGER_MAXIMUM_ISSUES)
	{
		game->issues[game->issue_count].kind = kind;
		game->issues[game->issue_count].source = source;
		snprintf(game->issues[game->issue_count].name, sizeof(game->issues[0].name), "%s", name);
	}
	game->issue_count++;
	game->state = 3;
}

/* a map's header: the magic, its version into `version`, and the size it says against the real size; -1 good, else
what is wrong */
static int header_problem(char const *xbox_path, unsigned long long real, unsigned long *version)
{
	unsigned char header[16];
	int got = read_start(xbox_path, header, sizeof(header));
	unsigned long said;

	*version = 0;
	if (got < 0)
		return _game_manager_issue_unreadable;
	if (got != (int)sizeof(header) || read_le32(header) != 0x68656164UL)
		return _game_manager_issue_not_a_map;
	*version = read_le32(header + 4);
	said = read_le32(header + 8);
	/* (an Xbox map is stored packed: its file is shorter than the header's size, and a packed copy that stopped is not
	seen here - the game's own check when it loads is; a file longer than its header says was changed. An MCC map is
	not packed: its file is the header's size) */
	if (*version == XBOX_CACHE_VERSION && said && real > said)
		return _game_manager_issue_changed;
	if (*version == MCC_CACHE_VERSION && said && real != said)
		return _game_manager_issue_changed;
	return -1;
}

static int count_files(char const *xbox_folder, char const *extension)
{
	char folder[1024], entry[256];
	void *listing;
	int count = 0;

	host_folder(xbox_folder, folder, sizeof(folder));
	listing = posix_directory_open(folder);
	while (listing && posix_directory_next(listing, entry, sizeof(entry)))
	{
		size_t length = strlen(entry), extension_length = strlen(extension);

		if (length > extension_length && !strcmp(entry + length - extension_length, extension))
			count++;
	}
	if (listing)
		posix_directory_close(listing);
	return count;
}

/* the disc's default.xbe where the loading screen looks for it (progress_bar.c): the data root (d:\default.xbe, its
first bytes into `program`: their count, -1 when not there), else the folder holding the maps folder or the one above
the data root (only whether it is there: 4, "XBEH" put in `program`) */
static int read_program(unsigned char *program)
{
	static char const *const folders[] = { "d:\\maps", "d:\\" };
	int got = read_start("d:\\default.xbe", program, 4);
	int index;

	for (index = 0; got < 0 && index < (int)(sizeof(folders) / sizeof(folders[0])); index++)
	{
		char path[1024];
		long size = 0;
		unsigned long long bytes;
		/* (a maximum of one byte: nothing read, only its path found) */
		void *read = platform_read_file_in_parent_folder(folders[index], "default.xbe", 1, &size, path, sizeof(path));

		if (read)
			platform_read_file_free(read);
		if (path[0] && file_size_of(path, &bytes) && bytes > 4)
		{
			memcpy(program, "XBEH", 4);
			got = 4;
		}
	}
	return got;
}

static int pc_file_index(char const *stem)
{
	int index;

	for (index = 0; index < (int)(sizeof(pc_files) / sizeof(pc_files[0])); index++)
	{
		if (!strcmp(stem, pc_files[index]))
			return index;
	}
	return -1;
}

/* one install's maps folder (`xbox_folder`, d:\maps\ or MCC's), each map read by its header; the campaign's and the
menus' maps found into `found` (NUMBER_OF_CAMPAIGN_MAPS, then ui); `total` all the .map files of both, for the progress */
static void scan_maps(char const *xbox_folder, int source, int *found, int total, int *checked)
{
	char folder[1024], entry[256];
	struct game_manager_game *game = &manager.games[_game_manager_halo_ce];
	void *listing;
	int index;

	host_folder(xbox_folder, folder, sizeof(folder));
	listing = posix_directory_open(folder);
	platform_log("game manager: %s maps folder %s: %s", source == _game_manager_source_mcc ? "MCC" : "Xbox", folder,
		listing ? "listed" : "not opened");
	while (listing && posix_directory_next(listing, entry, sizeof(entry)))
	{
		size_t length = strlen(entry);
		char host_path[1400], xbox_path[600], stem[256];
		unsigned long long size;
		unsigned long version = 0;
		int problem = -1, pc_file;

		if (length < 5 || strcmp(entry + length - 4, ".map"))
			continue;
		snprintf(stem, sizeof(stem), "%.*s", (int)(length - 4), entry);
		snprintf(host_path, sizeof(host_path), "%s/%s", folder, entry);
		snprintf(xbox_path, sizeof(xbox_path), "%s%s", xbox_folder, entry);
		file_size_of(host_path, &size);
		pc_file = pc_file_index(stem);
		/* (the PC files - and MCC's own bitmaps.map - have no such header: counted, not read) */
		if (pc_file < 0)
			problem = header_problem(xbox_path, size, &version);
		/* (a map of another kind - a Custom Edition map, another game's - is the MODS tab's: not counted here; an MCC
		Halo CE map in the Xbox maps folder is not played from there) */
		if (pc_file < 0 && version && version != (source == _game_manager_source_mcc ? MCC_CACHE_VERSION : XBOX_CACHE_VERSION))
		{
			if (source == _game_manager_source_xbox && version == MCC_CACHE_VERSION &&
				(mcc_content_stem_kind(stem) != _mcc_content_none || !strcmp(stem, "ui")))
				problem = _game_manager_issue_wrong_folder;
			else
				continue;
		}
		pthread_mutex_lock(&manager.lock);
		if (pc_file >= 0)
		{
			if (source == _game_manager_source_xbox)
				game->pc_files |= 1 << pc_file;
		}
		else if (problem < 0 || problem == _game_manager_issue_changed)
		{
			game->map_counts[source]++;
			game->map_count++;
			game->sources |= 1 << source;
			for (index = 0; index < NUMBER_OF_CAMPAIGN_MAPS; index++)
			{
				if (!strcmp(stem, campaign_maps[index]))
					found[index] = 1;
			}
			if (!strcmp(stem, "ui"))
				found[NUMBER_OF_CAMPAIGN_MAPS] = 1;
		}
		game->bytes += size;
		if (problem >= 0)
			issue(game, problem, source, stem);
		(*checked)++;
		game->progress = total ? *checked * 90 / total : 90;
		pthread_mutex_unlock(&manager.lock);
	}
	if (listing)
		posix_directory_close(listing);
}

static void *worker(void *unused)
{
	struct game_manager_game *game = &manager.games[_game_manager_halo_ce];
	int found[NUMBER_OF_GAME_MANAGER_SOURCES][NUMBER_OF_CAMPAIGN_MAPS + 1];
	int index, total, checked = 0;
	unsigned char program[4];

	(void)unused;
	memset(found, 0, sizeof(found));
	/* (how many, for the progress) */
	total = count_files(job.maps, ".map") + (job.content ? count_files(GAME_MANAGER_MCC_MAPS, ".map") : 0);
	scan_maps(job.maps, _game_manager_source_xbox, found[_game_manager_source_xbox], total, &checked);
	if (job.content)
		scan_maps(GAME_MANAGER_MCC_MAPS, _game_manager_source_mcc, found[_game_manager_source_mcc], total, &checked);

	/* the PC files not in the maps folder: MCC's custom_edition folder's (platform_halo_pc_resource_map's order) */
	if (job.content)
	{
		for (index = 0; index < (int)(sizeof(pc_files) / sizeof(pc_files[0])); index++)
		{
			char xbox_path[256], host_path[1024];
			unsigned long long size;

			if (game->pc_files & (1 << index))
				continue;
			snprintf(xbox_path, sizeof(xbox_path), "%s%s.map", GAME_MANAGER_MCC_PC_FILES, pc_files[index]);
			platform_translate_path(xbox_path, host_path, sizeof(host_path));
			if (file_size_of(host_path, &size))
			{
				pthread_mutex_lock(&manager.lock);
				game->pc_files |= 1 << index;
				game->bytes += size;
				pthread_mutex_unlock(&manager.lock);
			}
		}
	}

	/* the disc's executable (the loading screen's picture and hum: the Xbox install's), the movies (the PS Vita's MP4s
	in movies, else the PC's Bink files) */
	{
		int got = read_program(program);
		int movies = count_files("d:\\movies", ".mp4");

		if (!movies)
			movies = count_files("d:\\bink", ".bik");
		pthread_mutex_lock(&manager.lock);
		game->installed = game->map_count > 0;
		if (game->sources & (1 << _game_manager_source_xbox))
		{
			for (index = 0; index <= NUMBER_OF_CAMPAIGN_MAPS; index++)
			{
				if (!found[_game_manager_source_xbox][index])
					issue(game, _game_manager_issue_missing, _game_manager_source_xbox,
						index < NUMBER_OF_CAMPAIGN_MAPS ? campaign_maps[index] : "ui");
			}
			if (got < 0)
				issue(game, _game_manager_issue_program_missing, _game_manager_source_xbox, "default.xbe");
			else if (got != 4 || memcmp(program, "XBEH", 4))
				issue(game, _game_manager_issue_program_damaged, _game_manager_source_xbox, "default.xbe");
		}
		if (game->sources & (1 << _game_manager_source_mcc))
		{
			/* (MCC's ui.map is the menus' only without the Xbox's) */
			int needed = game->sources & (1 << _game_manager_source_xbox) ? NUMBER_OF_CAMPAIGN_MAPS : NUMBER_OF_CAMPAIGN_MAPS + 1;

			for (index = 0; index < needed; index++)
			{
				if (!found[_game_manager_source_mcc][index])
					issue(game, _game_manager_issue_missing, _game_manager_source_mcc,
						index < NUMBER_OF_CAMPAIGN_MAPS ? campaign_maps[index] : "ui");
			}
		}
		game->movies = movies;
		if (game->state != 3)
			game->state = 2;
		game->progress = 100;
		manager.running = 0;
		pthread_mutex_unlock(&manager.lock);
	}
	platform_log("game manager: checked: Halo CE %d maps (Xbox %d, MCC %d), sources %d, %d issues, PC files %d, %d movies",
		game->map_count, game->map_counts[_game_manager_source_xbox], game->map_counts[_game_manager_source_mcc],
		game->sources, game->issue_count, game->pc_files, game->movies);
	return NULL;
}

void game_manager_scan(void)
{
	pthread_t thread;
	pthread_attr_t attributes;

	pthread_mutex_lock(&manager.lock);
	if (manager.running)
	{
		pthread_mutex_unlock(&manager.lock);
		return;
	}
	manager.running = 1;
	memset(manager.games, 0, sizeof(manager.games));
	manager.games[_game_manager_halo_ce].state = 1;
	pthread_mutex_unlock(&manager.lock);
	/* (looked up here, on the menus' thread: the worker reads these alone) */
	snprintf(job.maps, sizeof(job.maps), "%s", cache_files_map_directory());
	job.content = halo_content_library_found();
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	/* (its own stack size: on the Vita a thread made with attributes and none set gets the SDK's 4 KiB minimum, not
	the 256 KB the port gives one made without (vita_stubs.c) - the worker's listings and paths take several KB: 1.2.0's
	insider.5 died opening GAME MANAGER) */
	pthread_attr_setstacksize(&attributes, GAME_MANAGER_STACK_BYTES);
	if (pthread_create(&thread, &attributes, worker, NULL) != 0)
	{
		pthread_mutex_lock(&manager.lock);
		manager.running = 0;
		manager.games[_game_manager_halo_ce].state = 0;
		pthread_mutex_unlock(&manager.lock);
	}
	pthread_attr_destroy(&attributes);
}

void game_manager_get(int game, struct game_manager_game *out)
{
	memset(out, 0, sizeof(*out));
	if (game < 0 || game >= NUMBER_OF_GAME_MANAGER_GAMES)
		return;
	pthread_mutex_lock(&manager.lock);
	*out = manager.games[game];
	pthread_mutex_unlock(&manager.lock);
}
