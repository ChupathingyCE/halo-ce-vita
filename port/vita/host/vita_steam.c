/*
VITA_STEAM.C

The Vita's side of the Steam download (port/steamdl/steam_job.c;
docs/steam_download_security.md):
- the free space where the files go (sceIoDevctl, as the updater asks);
- the battery: a download does not start under 30% unless charging
  (vita_steam_battery_ok, the screen asks) and pauses under 10% when not
  charging (steam_job's battery_low);
- awake while a job runs: auto-suspend held off (sceKernelPowerTick) and the
  PS button locked (sceShellUtilLock), so the system does not suspend the app
  and kill its sockets mid-download;
- the power warnings: a suspend, a low-battery suspend, the power button or
  the app sent to the background (a power callback on its own thread) abort
  the job: its session revoked and logged off before the system acts, as far
  as the time given allows (a power loss kills the RAM, and the token with it);
- at start, a sign-in left behind (its no-secret marker,
  steam-session.txt): the panel's message says to remove "Halo CE Vita (PS
  Vita)" in the Steam app.
1.1.x's: Halo CE's files to ux0:data/haloce-vita (1.2.0's: project-ark).
In a test build (configure.py --steam-test-ca) env.txt's HALO_STEAM_TEST_ROOT
takes the files (a scratch folder, never the game's own), and
HALO_STEAM_TEST_CM / HALO_STEAM_TEST_CDN the fake server (steam_job.c).
*/

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "steam_job.h"
#include "vita_host.h"

void vita_settings_message(const char *title, const char *text);

/* (vita_main.c's VITA_DATA_DIRECTORY: Halo CE's files and the app's own folder, one and the same in 1.1.x) */
#define DATA_DIRECTORY "ux0:data/haloce-vita"

static int shell_ready, locked;

static int64_t free_space(const char *root)
{
	struct { unsigned long long maximum, free; unsigned int cluster; unsigned int unknown; } space;
	char device[8];
	const char *colon = strchr(root, ':');

	snprintf(device, sizeof(device), "%.*s", colon && colon - root < 6 ? (int)(colon - root + 1) : 4, colon ? root : "ux0:");
	memset(&space, 0, sizeof(space));
	if (sceIoDevctl(device, 0x3001, NULL, 0, &space, sizeof(space)) >= 0)
		return (int64_t)space.free;
	/* (the devctl is refused an app on some firmware - the dev Vita's, the 1.1.3-insider.4 smoke -: the app
	manager's count, as vita_main.c's startup check and vita_update.c read it) */
	{
		uint64_t maximum = 0, free_bytes = 0;

		if (sceAppMgrGetDevInfo(device, &maximum, &free_bytes) >= 0 && maximum)
			return (int64_t)free_bytes;
	}
	return -1;
}

static int battery_low(void)
{
	return !scePowerIsBatteryCharging() && scePowerGetBatteryLifePercent() < 10;
}

static int battery_ok(void)
{
	return scePowerIsBatteryCharging() || scePowerGetBatteryLifePercent() >= 30;
}

static void keep_awake(void)
{
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
}

static void busy(int on)
{
	if (!shell_ready)
		shell_ready = sceShellUtilInitEvents(0) >= 0 ? 1 : -1;
	if (shell_ready < 0)
		return;
	if (on && !locked)
		locked = sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) >= 0;
	else if (!on && locked)
	{
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		locked = 0;
	}
	vita_host_log(on ? "steam: PS button locked, auto-suspend held off" : "steam: PS button unlocked");
}

static void log_line(const char *line)
{
	vita_host_log(line);
	/* (the memory around a download: its start, its measurements' line at the end) */
	if (!strncmp(line, "steam job: started", 18) || !strncmp(line, "steam job: CM connection", 24) ||
		!strncmp(line, "depot ", 6))
	{
		vita_host_log_memory("steam download");
	}
}

/* a power warning: the job aborted, its session ended in the time the system gives */
static int power_callback(int notify, int count, int argument, void *common)
{
	(void)notify;
	(void)count;
	(void)common;
	if (argument & (SCE_POWER_CB_SYSTEM_SUSPEND | SCE_POWER_CB_LOW_BATTERY_SUSPEND | SCE_POWER_CB_APP_SUSPEND |
		SCE_POWER_CB_BUTTON_POWER_PRESS | SCE_POWER_CB_LOW_BATTERY))
	{
		struct steam_job_status status;

		steam_job_get(&status);
		if (status.phase != STEAM_JOB_IDLE && status.phase < STEAM_JOB_DONE)
		{
			vita_host_log("steam: a power warning: the download stops and signs out");
			steam_job_abort(4000);
		}
	}
	return 0;
}

static int power_thread(SceSize size, void *argument)
{
	SceUID callback;

	(void)size;
	(void)argument;
	callback = sceKernelCreateCallback("halo steam power", 0, power_callback, NULL);
	if (callback < 0 || scePowerRegisterCallback(callback) < 0)
	{
		vita_host_log("steam: no power callback");
		return 0;
	}
	for (;;)
		sceKernelDelayThreadCB(1000 * 1000);
	return 0;
}

/* (a test build's scratch folder: steam_job.c's HALO_STEAM_TEST_ROOT) */
static void roots(char *ce_root, char *app_root, size_t size)
{
	snprintf(ce_root, size, "%s", DATA_DIRECTORY);
	snprintf(app_root, size, "%s", DATA_DIRECTORY);
}

#ifdef HALO_STEAM_TEST
/* (a test build alone: HALO_STEAM_TEST_AUTOSTART=<games> starts a download 15 s after the start, against the fake
server, without the menus - the dev Vita's measurements: the job's log lines) */
static void autostart_thread(void *argument)
{
	const char *games;

	(void)argument;
	vita_host_sleep_us(15 * 1000 * 1000);
	games = getenv("HALO_STEAM_TEST_AUTOSTART");
	if (games && atoi(games) > 0)
		vita_host_log(steam_job_start_games(atoi(games)) > 0 ? "steam: test autostart" : "steam: test autostart refused");
}
#endif

void vita_steam_init(void)
{
	struct steam_job_platform platform = { free_space, battery_low, keep_awake, busy, log_line, roots, battery_ok };
	char date[32];
	SceUID thread;

	steam_job_set_platform(&platform);
	thread = sceKernelCreateThread("halo steam power", power_thread, 0x10000100, 0x4000, 0, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
	/* a sign-in left behind (the app killed during a download) */
	if (steam_job_marker_check(date, sizeof(date)))
	{
		char text[512];

		vita_host_log("steam: a sign-in from an earlier start was not signed out");
		snprintf(text, sizeof(text), T("A Steam sign-in from %s was not signed out (the game stopped during a "
			"download). On your phone, open the Steam app, Steam Guard, Authorized devices, and remove "
			"\"Halo CE Vita (PS Vita)\"."), date);
		/* (the marker stays until a sign-in signs out: DOWNLOAD FROM STEAM says it too) */
		vita_settings_message(T("Steam"), text);
	}
#ifdef HALO_STEAM_TEST
	/* (env.txt is read after this: the autostart looks a little later) */
	if (vita_host_thread_start_priority("halo steam test", autostart_thread, NULL, -1, 190) != 0)
		vita_host_log("steam: no test autostart thread");
#endif
}
