/*
VITA_FOLDER.C

The game's own folder on the Vita, chosen once at start (vita_folder.h):
the player's choice kept in ux0:data/haloce-vita-folder.txt, else the
first card whose data/haloce-vita has the game's files (ux0:, uma0:,
imc0:), else the first where that folder exists, else ux0:. Only the C
library's calls (newlib's on the Vita, which take "uma0:..." paths), so the
desktop test (port/vita/tests/vita_folder_test.c) runs the same code in a
folder of its own.
*/

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "vita_folder.h"

static char folder_chosen[VITA_FOLDER_SIZE] = VITA_FOLDER_DEFAULT;
static char device_chosen[8] = "ux0:";

static const char *const choice_words[VITA_FOLDER_CHOICES] = { "auto", "ux0", "uma0", "imc0" };
static const char *const choice_folders[VITA_FOLDER_CHOICES] = {
	NULL, "ux0:data/haloce-vita", "uma0:data/haloce-vita", "imc0:data/haloce-vita" };
static const char *const choice_devices[VITA_FOLDER_CHOICES] = { NULL, "ux0:", "uma0:", "imc0:" };

const char *vita_folder(void)
{
	return folder_chosen;
}

const char *vita_folder_device(void)
{
	return device_chosen;
}

char *vita_folder_path(char *path, size_t size, const char *name)
{
	if (size)
		snprintf(path, size, "%s/%s", folder_chosen, name);
	return path;
}

void vita_folder_set(const char *folder)
{
	const char *colon;

	snprintf(folder_chosen, sizeof(folder_chosen), "%s", folder && folder[0] ? folder : VITA_FOLDER_DEFAULT);
	colon = strchr(folder_chosen, ':');
	if (colon && colon - folder_chosen < (long)sizeof(device_chosen) - 1)
		snprintf(device_chosen, sizeof(device_chosen), "%.*s", (int)(colon - folder_chosen + 1), folder_chosen);
	else
		snprintf(device_chosen, sizeof(device_chosen), "ux0:");
}

const char *vita_folder_choice_word(int choice)
{
	return choice >= 0 && choice < VITA_FOLDER_CHOICES ? choice_words[choice] : choice_words[VITA_FOLDER_AUTO];
}

int vita_folder_choice_parse(const char *text)
{
	char word[16];
	size_t length = 0;
	int choice;

	if (!text)
		return -1;
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	while (text[length] && text[length] != ' ' && text[length] != '\t' && text[length] != '\r' && text[length] != '\n' &&
		text[length] != ':' && length < sizeof(word) - 1)
	{
		word[length] = text[length];
		length++;
	}
	word[length] = 0;
	for (choice = 0; choice < VITA_FOLDER_CHOICES; choice++)
		if (!strcasecmp(word, choice_words[choice]))
			return choice;
	return -1;
}

const char *vita_folder_of_choice(int choice)
{
	return choice > VITA_FOLDER_AUTO && choice < VITA_FOLDER_CHOICES ? choice_folders[choice] : NULL;
}

/* whether folder has the game's files: an Xbox ui.map, or MCC's (beside the
data root, or in it: the game's own search order, xbox_files.c) */
static int has_game_files(const char *folder, vita_folder_exists_function exists, void *context)
{
	static const char *const files[] = { "maps/ui.map", "mcc/halo1/maps/ui.map", "data/mcc/halo1/maps/ui.map" };
	char path[160];
	unsigned int index;

	for (index = 0; index < sizeof(files) / sizeof(files[0]); index++)
	{
		snprintf(path, sizeof(path), "%s/%s", folder, files[index]);
		if (exists(path, context))
			return 1;
	}
	return 0;
}

static void say(char *why, size_t size, const char *text)
{
	if (why && size)
		snprintf(why, size, "%s", text);
}

int vita_folder_choose(int choice, vita_folder_exists_function exists, void *context, char *folder, size_t folder_size,
	char *why, size_t why_size)
{
	char line[256], data[16];
	const char *note = "";
	int index;

	if (choice > VITA_FOLDER_AUTO && choice < VITA_FOLDER_CHOICES)
	{
		/* (the player's: kept while its card is there; the folder itself is made at start) */
		snprintf(data, sizeof(data), "%sdata", choice_devices[choice]);
		if (exists(choice_folders[choice], context) || exists(data, context) || exists(choice_devices[choice], context))
		{
			if (folder && folder_size)
				snprintf(folder, folder_size, "%s", choice_folders[choice]);
			snprintf(line, sizeof(line), "chosen in the settings (Game folder: %s)", choice_words[choice]);
			say(why, why_size, line);
			return choice;
		}
		note = choice == VITA_FOLDER_UX0 ? "" : " (the Game folder setting names a card that is not there)";
	}
	/* auto: the first with the game's files */
	for (index = VITA_FOLDER_UX0; index < VITA_FOLDER_CHOICES; index++)
	{
		if (has_game_files(choice_folders[index], exists, context))
		{
			if (folder && folder_size)
				snprintf(folder, folder_size, "%s", choice_folders[index]);
			snprintf(line, sizeof(line), "auto: the game's files are there%s", note);
			say(why, why_size, line);
			return index;
		}
	}
	/* then the first folder that exists (made by hand, or by an earlier start) */
	for (index = VITA_FOLDER_UX0; index < VITA_FOLDER_CHOICES; index++)
	{
		if (exists(choice_folders[index], context))
		{
			if (folder && folder_size)
				snprintf(folder, folder_size, "%s", choice_folders[index]);
			snprintf(line, sizeof(line), "auto: no card has the game's files yet; this folder exists%s", note);
			say(why, why_size, line);
			return index;
		}
	}
	if (folder && folder_size)
		snprintf(folder, folder_size, "%s", choice_folders[VITA_FOLDER_UX0]);
	snprintf(line, sizeof(line), "auto: no game folder on any card yet: a new install on ux0:%s", note);
	say(why, why_size, line);
	return VITA_FOLDER_UX0;
}

int vita_folder_choice_read(void)
{
	FILE *file = fopen(VITA_FOLDER_CHOICE_FILE, "r");
	char text[64];
	int choice = VITA_FOLDER_AUTO;

	if (!file)
		return VITA_FOLDER_AUTO;
	if (fgets(text, sizeof(text), file))
	{
		choice = vita_folder_choice_parse(text);
		if (choice < 0)
			choice = VITA_FOLDER_AUTO;
	}
	fclose(file);
	return choice;
}

int vita_folder_choice_write(int choice)
{
	char temporary[sizeof(VITA_FOLDER_CHOICE_FILE) + 8];
	FILE *file;
	int ok;

	if (choice <= VITA_FOLDER_AUTO || choice >= VITA_FOLDER_CHOICES)
	{
		remove(VITA_FOLDER_CHOICE_FILE);
		return 1;
	}
	snprintf(temporary, sizeof(temporary), "%s.new", VITA_FOLDER_CHOICE_FILE);
	if (!(file = fopen(temporary, "w")))
		return 0;
	ok = fprintf(file, "%s\n", choice_words[choice]) > 0;
	ok = fclose(file) == 0 && ok;
	if (!ok)
	{
		remove(temporary);
		return 0;
	}
	/* (newlib's rename on the Vita does not replace a file) */
	remove(VITA_FOLDER_CHOICE_FILE);
	if (rename(temporary, VITA_FOLDER_CHOICE_FILE) != 0)
	{
		remove(temporary);
		return 0;
	}
	return 1;
}

static int path_exists(const char *path, void *context)
{
	struct stat status;
	size_t length = strlen(path);

	(void)context;
	if (stat(path, &status) == 0)
		return 1;
	/* (a device's root, "uma0:": its folder opened, as VitaShell lists it) */
	if (length && path[length - 1] == ':')
	{
		DIR *directory = opendir(path);

		if (directory)
		{
			closedir(directory);
			return 1;
		}
	}
	return 0;
}

void vita_folder_start(char *why, size_t why_size)
{
	char folder[VITA_FOLDER_SIZE], data[16], reason[256];
	int asked = vita_folder_choice_read(), chosen;

	chosen = vita_folder_choose(asked, path_exists, NULL, folder, sizeof(folder), reason, sizeof(reason));
	/* (a card's data folder made when the player named a card without one) */
	snprintf(data, sizeof(data), "%sdata", choice_devices[chosen]);
	mkdir(data, 0777);
	mkdir(folder, 0777);
	vita_folder_set(folder);
	/* (the shared code's: port/linux/src/vita_game_folder.h) */
	setenv("HALO_VITA_FOLDER", folder, 1);
	if (why && why_size)
		snprintf(why, why_size, "%s (%s)", reason, asked == VITA_FOLDER_AUTO ? "Game folder: auto" : VITA_FOLDER_CHOICE_FILE);
}
