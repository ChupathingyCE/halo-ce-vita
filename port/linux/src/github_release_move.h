/*
GITHUB_RELEASE_MOVE.H

The one github.com -> github.com redirect a release download may follow:
GitHub's 301 from a release file under one of this project's own homes to
the same file under another (the repository was moved: BirchWoodGod/
halo-ce-vita became ChupathingyCE/halo-ce-vita in November 2026, and is to
be renamed ChupathingyCE/warthog-vita). Both ends must be
https://github.com/<home>/releases/download/<tag>/<file>, the homes both in
GITHUB_RELEASE_HOMES and different, and <tag>/<file> exactly the same, of
GitHub's tag and file characters alone: nothing else of github.com, no
other owner's or repository's releases, no other host. (The file then
still goes to GitHub's storage, the githubusercontent hosts each caller
allows exactly, and is kept only if its signature checks:
vita_update_verify.c, netversion_table.c.)

Header-only (static), so that the Vita's updater (vita_update_verify.c), the
crossplay table's fetch (netversion_fetch.c) and the tests build it alone.
*/

#ifndef GITHUB_RELEASE_MOVE_H
#define GITHUB_RELEASE_MOVE_H

#include <string.h>

/* every <owner>/<repository> this project's releases have lived or will
live under (GitHub's exact names), the current one (UPDATE_REPOSITORY,
update_check.h) among them */
#define GITHUB_RELEASE_HOMES { "BirchWoodGod/halo-ce-vita", "ChupathingyCE/halo-ce-vita", "ChupathingyCE/warthog-vita" }

/* url's home's index in GITHUB_RELEASE_HOMES and, in *rest, what follows
"/releases/download/" (a "<tag>/<file>" of letters, digits and "._+-", a
part neither "." nor ".." nor empty, at most 200 characters); else -1 */
static int github_release_home(const char *url, const char **rest)
{
	static const char *const homes[] = GITHUB_RELEASE_HOMES;
	static const char prefix[] = "https://github.com/", middle[] = "/releases/download/";
	size_t index;

	if (!url || strncmp(url, prefix, sizeof(prefix) - 1))
		return -1;
	url += sizeof(prefix) - 1;
	for (index = 0; index < sizeof(homes) / sizeof(homes[0]); index++)
	{
		size_t length = strlen(homes[index]);
		const char *cursor = url + length + sizeof(middle) - 1;
		int part;

		if (strncmp(url, homes[index], length) || strncmp(url + length, middle, sizeof(middle) - 1))
			continue;
		*rest = cursor;
		for (part = 0; part < 2; part++)
		{
			const char *start = cursor;

			while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
				(*cursor >= '0' && *cursor <= '9') || *cursor == '.' || *cursor == '_' || *cursor == '+' || *cursor == '-')
			{
				cursor++;
			}
			if (cursor == start || (cursor - start <= 2 && !strncmp(start, "..", (size_t)(cursor - start))))
				return -1;
			if (part == 0 && *cursor++ != '/')
				return -1;
		}
		if (*cursor || cursor - *rest > 200)
			return -1;
		return (int)index;
	}
	return -1;
}

/* 1 if a download that asked from_url may follow GitHub's redirect to
to_url as a move of the project's repository (above), else 0 */
static int github_release_moved(const char *from_url, const char *to_url)
{
	const char *from_rest = NULL, *to_rest = NULL;
	int from_home = github_release_home(from_url, &from_rest), to_home = github_release_home(to_url, &to_rest);

	return from_home >= 0 && to_home >= 0 && from_home != to_home && !strcmp(from_rest, to_rest);
}

#endif
