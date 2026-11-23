/*
VITA_FOLDER_TEST.C

The desktop test of the Vita's game folder choice (port/vita/host/
vita_folder.c, issue #53): the choice by itself with made-up cards, then
in a folder of its own whose ux0:, uma0: and imc0: are folders named so
(the C library takes "uma0:data/..." as a relative path, as newlib takes
it on the Vita): the choice file read and written, the start's pick.
Run by run_vita_folder_test.sh.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vita_folder.h"

static int failures, checks;

#define CHECK(condition, ...) \
	do \
	{ \
		checks++; \
		if (!(condition)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* ---------- made-up cards: the paths that exist, a NULL at the end */

static int listed(const char *path, void *context)
{
	const char *const *paths = context;

	for (; *paths; paths++)
		if (!strcmp(*paths, path))
			return 1;
	return 0;
}

static void expect(const char *name, int choice, const char *const *paths, const char *folder_wanted, const char *why_part)
{
	char folder[VITA_FOLDER_SIZE] = "", why[256] = "";
	int chosen = vita_folder_choose(choice, listed, (void *)paths, folder, sizeof(folder), why, sizeof(why));

	CHECK(!strcmp(folder, folder_wanted), "%s: %s, wanted %s (%s)", name, folder, folder_wanted, why);
	CHECK(!strcmp(vita_folder_of_choice(chosen), folder), "%s: the choice returned (%d) is not %s", name, chosen, folder);
	CHECK(!why_part || strstr(why, why_part), "%s: why \"%s\" does not say \"%s\"", name, why, why_part);
}

static void choice_logic(void)
{
	static const char *const nothing[] = { "ux0:", "ux0:data", NULL };
	static const char *const ux0_xbox[] = { "ux0:data", "ux0:data/haloce-vita", "ux0:data/haloce-vita/maps/ui.map", NULL };
	static const char *const uma0_xbox[] = { "ux0:data", "uma0:", "uma0:data", "uma0:data/haloce-vita",
		"uma0:data/haloce-vita/maps/ui.map", NULL };
	/* (an earlier start on the official card left its logs there; the maps are on the SD2Vita) */
	static const char *const ux0_empty_uma0_mcc[] = { "ux0:data", "ux0:data/haloce-vita", "uma0:", "uma0:data",
		"uma0:data/haloce-vita", "uma0:data/haloce-vita/mcc/halo1/maps/ui.map", NULL };
	static const char *const uma0_mcc_in_data[] = { "ux0:data", "uma0:data", "uma0:data/haloce-vita",
		"uma0:data/haloce-vita/data/mcc/halo1/maps/ui.map", NULL };
	static const char *const both[] = { "ux0:data", "ux0:data/haloce-vita", "ux0:data/haloce-vita/maps/ui.map", "uma0:data",
		"uma0:data/haloce-vita", "uma0:data/haloce-vita/maps/ui.map", NULL };
	static const char *const imc0_only[] = { "ux0:data", "imc0:data", "imc0:data/haloce-vita",
		"imc0:data/haloce-vita/mcc/halo1/maps/ui.map", NULL };
	static const char *const uma0_folder_only[] = { "ux0:data", "uma0:data", "uma0:data/haloce-vita", NULL };
	static const char *const uma0_card_no_data[] = { "ux0:data", "uma0:", NULL };
	static const char *const ux0_and_uma0_folders[] = { "ux0:data", "ux0:data/haloce-vita", "uma0:data",
		"uma0:data/haloce-vita", NULL };

	expect("a new install", VITA_FOLDER_AUTO, nothing, "ux0:data/haloce-vita", "new install");
	expect("ux0 with Xbox maps", VITA_FOLDER_AUTO, ux0_xbox, "ux0:data/haloce-vita", "game's files are there");
	expect("uma0 with Xbox maps", VITA_FOLDER_AUTO, uma0_xbox, "uma0:data/haloce-vita", "game's files are there");
	expect("ux0 left empty, uma0 with MCC's", VITA_FOLDER_AUTO, ux0_empty_uma0_mcc, "uma0:data/haloce-vita", NULL);
	expect("uma0 with MCC's in data", VITA_FOLDER_AUTO, uma0_mcc_in_data, "uma0:data/haloce-vita", NULL);
	expect("both cards have the files: ux0 first", VITA_FOLDER_AUTO, both, "ux0:data/haloce-vita", NULL);
	expect("imc0 alone", VITA_FOLDER_AUTO, imc0_only, "imc0:data/haloce-vita", NULL);
	expect("an empty folder on uma0 only", VITA_FOLDER_AUTO, uma0_folder_only, "uma0:data/haloce-vita", "this folder exists");
	expect("empty folders on both: ux0 first", VITA_FOLDER_AUTO, ux0_and_uma0_folders, "ux0:data/haloce-vita", NULL);
	/* the player's choice */
	expect("uma0 chosen, the files on ux0", VITA_FOLDER_UMA0, both, "uma0:data/haloce-vita", "chosen in the settings");
	expect("uma0 chosen, its folder not made yet", VITA_FOLDER_UMA0, uma0_card_no_data, "uma0:data/haloce-vita",
		"chosen in the settings");
	expect("ux0 chosen over uma0's files", VITA_FOLDER_UX0, uma0_xbox, "ux0:data/haloce-vita", "chosen in the settings");
	expect("uma0 chosen, no uma0 card", VITA_FOLDER_UMA0, ux0_xbox, "ux0:data/haloce-vita", "not there");
	expect("imc0 chosen, no imc0 (a PS Vita 1000), files on uma0", VITA_FOLDER_IMC0, uma0_xbox, "uma0:data/haloce-vita",
		"not there");
	{
		char folder[VITA_FOLDER_SIZE];

		/* (no why asked for) */
		vita_folder_choose(VITA_FOLDER_AUTO, listed, (void *)uma0_xbox, folder, sizeof(folder), NULL, 0);
		CHECK(!strcmp(folder, "uma0:data/haloce-vita"), "no why: %s", folder);
	}
}

static void words(void)
{
	CHECK(vita_folder_choice_parse("auto") == VITA_FOLDER_AUTO, "auto");
	CHECK(vita_folder_choice_parse("ux0") == VITA_FOLDER_UX0, "ux0");
	CHECK(vita_folder_choice_parse("UMA0:\r\n") == VITA_FOLDER_UMA0, "UMA0:");
	CHECK(vita_folder_choice_parse("  imc0\n") == VITA_FOLDER_IMC0, "imc0");
	CHECK(vita_folder_choice_parse("uma0:data/haloce-vita") == VITA_FOLDER_UMA0, "a whole folder: its device");
	CHECK(vita_folder_choice_parse("grw0") == -1, "grw0 is not one");
	CHECK(vita_folder_choice_parse("") == -1, "empty");
	CHECK(vita_folder_choice_parse(NULL) == -1, "NULL");
	CHECK(vita_folder_choice_parse("../../etc") == -1, "a path");
	CHECK(!strcmp(vita_folder_choice_word(VITA_FOLDER_UMA0), "uma0"), "uma0's word");
	CHECK(!strcmp(vita_folder_choice_word(42), "auto"), "no such choice: auto");
	CHECK(vita_folder_of_choice(VITA_FOLDER_AUTO) == NULL, "auto has no folder");
	{
		char path[128];

		vita_folder_set("uma0:data/haloce-vita");
		CHECK(!strcmp(vita_folder(), "uma0:data/haloce-vita"), "set");
		CHECK(!strcmp(vita_folder_device(), "uma0:"), "uma0's device: %s", vita_folder_device());
		CHECK(!strcmp(vita_folder_path(path, sizeof(path), "settings.txt"), "uma0:data/haloce-vita/settings.txt"), "%s", path);
		vita_folder_set(NULL);
		CHECK(!strcmp(vita_folder(), VITA_FOLDER_DEFAULT), "set NULL: the default");
		CHECK(!strcmp(vita_folder_device(), "ux0:"), "the default's device");
	}
}

static void make(const char *path)
{
	char part[256];
	const char *slash;

	for (slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/'))
	{
		snprintf(part, sizeof(part), "%.*s", (int)(slash - path), path);
		mkdir(part, 0777);
	}
	mkdir(path, 0777);
}

static void touch(const char *path)
{
	FILE *file = fopen(path, "w");

	if (file)
		fclose(file);
}

static int exists(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0;
}

static void on_disk(void)
{
	char why[320];

	/* (an SD2Vita's uma0: with the maps; the official card's ux0: with nothing of the game) */
	make("ux0:data");
	make("uma0:data/haloce-vita/maps");
	touch("uma0:data/haloce-vita/maps/ui.map");
	CHECK(vita_folder_choice_read() == VITA_FOLDER_AUTO, "no choice file: auto");
	vita_folder_start(why, sizeof(why));
	CHECK(!strcmp(vita_folder(), "uma0:data/haloce-vita"), "start: %s (%s)", vita_folder(), why);
	CHECK(!strcmp(vita_folder_device(), "uma0:"), "start: device %s", vita_folder_device());
	CHECK(getenv("HALO_VITA_FOLDER") && !strcmp(getenv("HALO_VITA_FOLDER"), "uma0:data/haloce-vita"),
		"start: HALO_VITA_FOLDER for the shared code");
	CHECK(!exists("ux0:data/haloce-vita"), "start made nothing on ux0:");
	CHECK(strstr(why, "Game folder: auto") != NULL, "why says auto: %s", why);

	/* the player picks ux0 in the panel: kept outside the game folder */
	CHECK(vita_folder_choice_write(VITA_FOLDER_UX0), "write ux0");
	CHECK(exists(VITA_FOLDER_CHOICE_FILE), "the choice file is there");
	CHECK(vita_folder_choice_read() == VITA_FOLDER_UX0, "read back ux0");
	vita_folder_start(why, sizeof(why));
	CHECK(!strcmp(vita_folder(), "ux0:data/haloce-vita"), "ux0 chosen: %s (%s)", vita_folder(), why);
	CHECK(exists("ux0:data/haloce-vita"), "ux0's folder made");
	CHECK(exists("uma0:data/haloce-vita/maps/ui.map"), "uma0's files left where they were");
	CHECK(strstr(why, VITA_FOLDER_CHOICE_FILE) != NULL, "why names the file: %s", why);

	/* imc0 chosen on a Vita without one: auto, which finds uma0's files */
	CHECK(vita_folder_choice_write(VITA_FOLDER_IMC0), "write imc0");
	vita_folder_start(why, sizeof(why));
	CHECK(!strcmp(vita_folder(), "uma0:data/haloce-vita"), "imc0 missing: %s (%s)", vita_folder(), why);
	CHECK(!exists("imc0:data"), "nothing made for a missing imc0");

	/* back to auto: the file goes */
	CHECK(vita_folder_choice_write(VITA_FOLDER_AUTO), "write auto");
	CHECK(!exists(VITA_FOLDER_CHOICE_FILE), "auto removes the file");
	CHECK(!exists(VITA_FOLDER_CHOICE_FILE ".new"), "no .new left");

	/* a damaged choice file reads as auto */
	{
		FILE *file = fopen(VITA_FOLDER_CHOICE_FILE, "w");

		fputs("\x01garbage\n", file);
		fclose(file);
		CHECK(vita_folder_choice_read() == VITA_FOLDER_AUTO, "garbage: auto");
		remove(VITA_FOLDER_CHOICE_FILE);
	}

	{
		/* (uma0: emptied of the game's files: uma0 chosen still holds) */
		remove("uma0:data/haloce-vita/maps/ui.map");
		CHECK(vita_folder_choice_write(VITA_FOLDER_UMA0), "write uma0");
		vita_folder_start(why, sizeof(why));
		CHECK(!strcmp(vita_folder(), "uma0:data/haloce-vita"), "uma0 chosen, empty: %s", vita_folder());
		vita_folder_choice_write(VITA_FOLDER_AUTO);
		/* (and auto now: ux0's folder, made by the ux0 start above, is the first that exists) */
		vita_folder_start(why, sizeof(why));
		CHECK(!strcmp(vita_folder(), "ux0:data/haloce-vita"), "auto, no files anywhere: %s (%s)", vita_folder(), why);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	choice_logic();
	words();
	on_disk();
	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
