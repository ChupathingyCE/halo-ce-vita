/*
SETUP_TEST.C

The first start's flow and screens (port/vita/setup) on Linux, for
tools/vita_setup_test.py: the same code the Vita runs, its pictures written
to PNG files and its buttons pressed from a script.

	setup_test maps DATA SHOTS SCRIPT [--maps FOLDER LABEL]... [--images FOLDER]...
	setup_test update SHOTS SCRIPT

The script is steps separated by commas:

	shot:NAME           the last picture to SHOTS/NAME.png
	press:BUTTON        up, down, cross, circle, triangle, square, start
	wait:MS             nothing pressed for MS milliseconds
	waitlog:TEXT        until a log line has TEXT
	waitpartial:MB      until DATA/maps.partial holds MB megabytes

SETUP_TEST_FREE_MB stands for the card's free space. The result is printed
("result own", "result elsewhere FOLDER", "result quit"), and the log to
standard error.
*/

#include "setup.h"
#include "xiso.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

static struct
{
	const char *data;
	const char *shots;
	char steps[64][256];
	int count;
	int step;
	unsigned char *last;
	unsigned int pressing;
	int press_polls;
	unsigned long long wait_until;
	char log[256][256];
	int log_count;
} test;

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

static unsigned long long now_ms(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (unsigned long long)time.tv_sec * 1000 + (unsigned long long)time.tv_nsec / 1000000;
}

static void sleep_ms(unsigned int milliseconds)
{
	usleep(milliseconds * 1000);
}

static void present(const unsigned char *pixels)
{
	if (!test.last)
		test.last = malloc(SETUP_WIDTH * SETUP_HEIGHT * 4);
	if (test.last)
		memcpy(test.last, pixels, SETUP_WIDTH * SETUP_HEIGHT * 4);
}

static void log_line(const char *line)
{
	fprintf(stderr, "%s\n", line);
	if (test.log_count < 256)
		snprintf(test.log[test.log_count++], sizeof(test.log[0]), "%s", line);
}

static long long free_space(const char *path)
{
	const char *free_mb = getenv("SETUP_TEST_FREE_MB");

	(void)path;
	return free_mb ? (long long)strtoull(free_mb, NULL, 10) << 20 : -1;
}

static void busy(int on)
{
	fprintf(stderr, "test: busy %d\n", on);
}

static void keep_awake(void)
{
}

/* the copy at a memory card's pace (SETUP_TEST_MB_PER_S), so a step can
come part way through it: the harness is linked with
--wrap=xiso_extract_maps_with */
static xiso_progress_proc slow_next;
static unsigned long long slow_start;
static unsigned long long slow_rate;

static void slow_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	unsigned long long due = slow_start + done / slow_rate;

	while (now_ms() < due)
		usleep(2000);
	slow_next(context, file, done, total);
}

int __real_xiso_extract_maps_with(const char *image_path, const char *destination, int flags,
	const volatile int *cancel, xiso_progress_proc progress, void *context, char *error, int error_size);

int __wrap_xiso_extract_maps_with(const char *image_path, const char *destination, int flags,
	const volatile int *cancel, xiso_progress_proc progress, void *context, char *error, int error_size)
{
	const char *rate = getenv("SETUP_TEST_MB_PER_S");

	if (!rate || !progress)
		return __real_xiso_extract_maps_with(image_path, destination, flags, cancel, progress, context, error,
			error_size);
	slow_next = progress;
	slow_start = now_ms();
	/* (bytes a millisecond) */
	slow_rate = (strtoull(rate, NULL, 10) << 20) / 1000;
	if (!slow_rate)
		slow_rate = 1;
	return __real_xiso_extract_maps_with(image_path, destination, flags, cancel, slow_progress, context, error,
		error_size);
}

static unsigned long long folder_bytes(const char *folder)
{
	DIR *directory = opendir(folder);
	struct dirent *entry;
	unsigned long long total = 0;

	if (!directory)
		return 0;
	while ((entry = readdir(directory)) != NULL)
	{
		char path[1200];
		struct stat information;

		if (entry->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		if (!stat(path, &information))
			total += (unsigned long long)information.st_size;
	}
	closedir(directory);
	return total;
}

static unsigned int button_named(const char *name)
{
	static const struct { const char *name; unsigned int button; } names[] =
	{
		{ "up", SETUP_BUTTON_UP }, { "down", SETUP_BUTTON_DOWN }, { "left", SETUP_BUTTON_LEFT },
		{ "right", SETUP_BUTTON_RIGHT }, { "cross", SETUP_BUTTON_CROSS }, { "circle", SETUP_BUTTON_CIRCLE },
		{ "triangle", SETUP_BUTTON_TRIANGLE }, { "square", SETUP_BUTTON_SQUARE }, { "start", SETUP_BUTTON_START },
	};
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(*names); index++)
	{
		if (!strcmp(name, names[index].name))
			return names[index].button;
	}
	fprintf(stderr, "test: no button %s\n", name);
	exit(2);
}

/* the script, a step at each poll of the buttons */
static unsigned int buttons(void)
{
	for (;;)
	{
		const char *step;

		if (test.press_polls > 0)
		{
			/* (held for three polls, then let go for three) */
			test.press_polls--;
			return test.press_polls >= 3 ? test.pressing : 0;
		}
		if (test.step >= test.count)
			return 0;
		step = test.steps[test.step];
		if (!strncmp(step, "shot:", 5))
		{
			char path[1200];

			snprintf(path, sizeof(path), "%s/%s.png", test.shots, step + 5);
			if (!test.last || !stbi_write_png(path, SETUP_WIDTH, SETUP_HEIGHT, 4, test.last, SETUP_WIDTH * 4))
			{
				fprintf(stderr, "test: could not write %s\n", path);
				exit(2);
			}
			fprintf(stderr, "test: shot %s\n", path);
			test.step++;
			continue;
		}
		if (!strncmp(step, "press:", 6))
		{
			test.pressing = button_named(step + 6);
			test.press_polls = 6;
			fprintf(stderr, "test: press %s\n", step + 6);
			test.step++;
			continue;
		}
		if (!strncmp(step, "wait:", 5))
		{
			if (!test.wait_until)
				test.wait_until = now_ms() + strtoull(step + 5, NULL, 10);
			if (now_ms() < test.wait_until)
				return 0;
			test.wait_until = 0;
			test.step++;
			continue;
		}
		if (!strncmp(step, "waitlog:", 8))
		{
			int index;

			for (index = 0; index < test.log_count; index++)
			{
				if (strstr(test.log[index], step + 8))
					break;
			}
			if (index == test.log_count)
				return 0;
			test.step++;
			continue;
		}
		if (!strncmp(step, "waitpartial:", 12))
		{
			char partial[1200];

			snprintf(partial, sizeof(partial), "%s/maps.partial", test.data);
			if (folder_bytes(partial) < strtoull(step + 12, NULL, 10) << 20)
				return 0;
			test.step++;
			continue;
		}
		fprintf(stderr, "test: no step %s\n", step);
		exit(2);
	}
}

static void script_read(const char *script)
{
	const char *at = script;

	while (*at && test.count < 64)
	{
		size_t length = strcspn(at, ",");

		snprintf(test.steps[test.count++], sizeof(test.steps[0]), "%.*s", (int)length, at);
		at += length;
		if (*at == ',')
			at++;
	}
}

int main(int argc, char **argv)
{
	struct setup_system system;
	const char *maps[32];
	const char *images[16];
	struct setup_places places;
	char root[600] = "";
	char choice[700];
	int maps_count = 0, image_count = 0, index, result;

	memset(&system, 0, sizeof(system));
	system.present = present;
	system.buttons = buttons;
	system.now_ms = now_ms;
	system.sleep_ms = sleep_ms;
	system.free_space = free_space;
	system.busy = busy;
	system.keep_awake = keep_awake;
	system.log = log_line;
	system.circle_confirms = getenv("SETUP_TEST_CIRCLE_CONFIRMS") != NULL;
	system.version = "0.9.2 (build 1)";
	if (argc >= 4 && !strcmp(argv[1], "update"))
	{
		void *screen;

		test.shots = argv[2];
		script_read(argv[3]);
		result = setup_update_question(&system, "0.9.3 (build 2)", "0.9.2 (build 1)");
		printf("answer %d\n", result);
		screen = setup_progress_open(&system);
		setup_progress_show(screen, 0.42f, "Downloading the update...", "21 of 50 MB");
		buttons();
		setup_progress_close(screen);
		setup_notice(&system, "The update failed, and the game goes on as it was:",
			"The download was cut short (the test's own failure).");
		buttons();
		setup_release();
		return 0;
	}
	if (argc < 5 || strcmp(argv[1], "maps"))
	{
		fprintf(stderr, "usage: setup_test maps DATA SHOTS SCRIPT [--maps FOLDER LABEL]... [--images FOLDER]...\n"
			"       setup_test update SHOTS SCRIPT\n");
		return 2;
	}
	test.data = argv[2];
	test.shots = argv[3];
	script_read(argv[4]);
	for (index = 5; index < argc; index++)
	{
		if (!strcmp(argv[index], "--maps") && index + 2 < argc && maps_count < 30)
		{
			maps[maps_count++] = argv[++index];
			maps[maps_count++] = argv[++index];
		}
		else if (!strcmp(argv[index], "--images") && index + 1 < argc && image_count < 16)
			images[image_count++] = argv[++index];
		else
			return 2;
	}
	snprintf(choice, sizeof(choice), "%s/maps_source.txt", test.data);
	memset(&places, 0, sizeof(places));
	places.data_folder = test.data;
	places.maps_folders = maps;
	places.maps_folder_count = maps_count / 2;
	places.image_folders = images;
	places.image_folder_count = image_count;
	places.choice_file = choice;
	result = setup_find_maps(&system, &places, root, sizeof(root));
	/* (the steps left run against the last picture: a closing shot) */
	while (test.step < test.count && strncmp(test.steps[test.step], "press:", 6))
	{
		if (!strncmp(test.steps[test.step], "shot:", 5))
			buttons();
		else
			test.step++;
	}
	printf("result %s%s%s\n", result == SETUP_MAPS_OWN ? "own" : result == SETUP_MAPS_ELSEWHERE ? "elsewhere" : "quit",
		root[0] ? " " : "", root);
	printf("steps %d of %d\n", test.step, test.count);
	setup_release();
	free(test.last);
	return 0;
}
