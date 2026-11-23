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
- the power's events (a power callback on its own thread, each logged with
  its flags): a true low battery stops the job (what it got kept); the power
  button or a suspend pauses it, and the wake carries it on by itself - its
  connections made again, the kept sign-in reused; the hold on auto-suspend
  renewed every second while a job runs (the screen may dim and turn off);
- the kept sign-in's device secret: the console's OpenPSID
  (sceKernelGetOpenPsId; port/steamdl/steam_signin.h);
- at start, a sign-in left behind (its no-secret marker,
  steam-session.txt): the panel's message says to remove "Halo CE Vita (PS
  Vita)" in the Steam app.
1.1.x's: Halo CE's files to the game folder, ux0:data/haloce-vita or the uma0:
or imc0: one chosen at start (vita_folder.h) (1.2.0's: project-ark).
In a test build (configure.py --steam-test-ca) env.txt's HALO_STEAM_TEST_ROOT
takes the files (a scratch folder, never the game's own), and
HALO_STEAM_TEST_CM / HALO_STEAM_TEST_CDN the fake server (steam_job.c).
*/

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/openpsid.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "steam_job.h"
#include "vita_folder.h"
#include "vita_host.h"

void vita_settings_message(const char *title, const char *text);

/* (the game folder, vita_folder.h: Halo CE's files and the app's own folder, one and the same in 1.1.x) */
#define DATA_DIRECTORY vita_folder()

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

/* the kept Steam sign-in's device secret (port/steamdl/steam_signin.h): this console's OpenPSID - not on the memory
card, so the sealed file does not open on another Vita or from the card alone; any app on this Vita can read it
(docs/steam_download_security.md section 10 says what that does and does not protect) */
static size_t device_secret(uint8_t *out, size_t capacity)
{
	SceKernelOpenPsId id;
	size_t index, zero = 1;

	memset(&id, 0, sizeof(id));
	if (capacity < sizeof(id.id) || sceKernelGetOpenPsId(&id) < 0)
		return 0;
	for (index = 0; index < sizeof(id.id); index++)
		zero &= id.id[index] == 0;
	if (zero)
		return 0;
	memcpy(out, id.id, sizeof(id.id));
	memset(&id, 0, sizeof(id));
	return sizeof(id.id);
}

/* the power callback's flags, named for the log (which event it was: the owner's 1.1.3-insider.8d "power warning"
stopped two downloads with nothing in the log saying which) */
static void power_flags_text(int argument, char *text, size_t size)
{
	static const struct { unsigned int flag; const char *name; } names[] = {
		{ SCE_POWER_CB_BUTTON_POWER_PRESS, "power button" }, { SCE_POWER_CB_BUTTON_POWER_HOLD, "power button held" },
		{ SCE_POWER_CB_LOW_BATTERY, "low battery" }, { SCE_POWER_CB_LOW_BATTERY_SUSPEND, "low-battery suspend" },
		{ SCE_POWER_CB_THERMAL_SUSPEND, "thermal suspend" }, { SCE_POWER_CB_SYSTEM_SUSPEND, "system suspend" },
		{ SCE_POWER_CB_SYSTEM_RESUMING, "system resuming" }, { SCE_POWER_CB_SYSTEM_RESUME, "system resume" },
		{ SCE_POWER_CB_APP_SUSPEND, "app suspend" }, { SCE_POWER_CB_APP_RESUMING, "app resuming" },
		{ SCE_POWER_CB_APP_RESUME, "app resume" }, { SCE_POWER_CB_AFTER_SYSTEM_RESUME, "after system resume" },
		{ SCE_POWER_CB_UNK_0x100000, "display switch" }, { SCE_POWER_CB_POWER_ONLINE, "charger in" },
		{ SCE_POWER_CB_BATTERY_ONLINE, "battery" }, { SCE_POWER_CB_BUTTON_PS_PRESS, "PS button" },
	};
	size_t index, used = 0;

	text[0] = 0;
	for (index = 0; index < sizeof(names) / sizeof(names[0]) && used + 2 < size; index++)
		if ((unsigned int)argument & names[index].flag)
			used += (size_t)snprintf(text + used, size - used, "%s%s", used ? ", " : "", names[index].name);
	if (!used)
		snprintf(text, size, "none named");
}

#define SLEEP_FLAGS (SCE_POWER_CB_SYSTEM_SUSPEND | SCE_POWER_CB_APP_SUSPEND | SCE_POWER_CB_BUTTON_POWER_PRESS | \
	SCE_POWER_CB_THERMAL_SUSPEND)
#define WAKE_FLAGS (SCE_POWER_CB_SYSTEM_RESUME | SCE_POWER_CB_SYSTEM_RESUMING | SCE_POWER_CB_APP_RESUME | \
	SCE_POWER_CB_APP_RESUMING | SCE_POWER_CB_AFTER_SYSTEM_RESUME)

/* (a sleep's notice with no suspend after it - the power button's press the system did not act on: the job carries
on after this long; a real suspend stops this thread, and its resume wakes the job first) */
#define SLEEP_WITHOUT_SUSPEND_US (10 * 1000 * 1000)
static volatile int sleeping;
static volatile SceUInt64 sleep_since;

/* the power's events while a job runs: a true low battery stops it (what it got kept, the screen says why); the power
button or a suspend pauses it - the sockets die with the suspend - and the wake carries it on by itself (the kept
sign-in reused): never a cancel. Every event logged with its flags */
static int power_callback(int notify, int count, int argument, void *common)
{
	struct steam_job_status status;
	char flags[200], line[260];

	(void)notify;
	(void)count;
	(void)common;
	steam_job_get(&status);
	if (status.phase == STEAM_JOB_IDLE || status.phase >= STEAM_JOB_DONE)
		return 0;
	power_flags_text(argument, flags, sizeof(flags));
	snprintf(line, sizeof(line), "steam: power event 0x%08x (%s) during a download", (unsigned int)argument, flags);
	vita_host_log(line);
	if (argument & (SCE_POWER_CB_LOW_BATTERY | SCE_POWER_CB_LOW_BATTERY_SUSPEND))
	{
		vita_host_log("steam: the battery is low: the download stops (what it got is kept)");
		steam_job_stop_battery(4000);
	}
	else if (argument & SLEEP_FLAGS)
	{
		sleep_since = sceKernelGetProcessTimeWide();
		sleeping = 1;
		steam_job_sleep(1);
	}
	else if ((argument & WAKE_FLAGS) && sleeping)
	{
		sleeping = 0;
		steam_job_sleep(0);
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
		vita_host_log("steam: no power callback");
	for (;;)
	{
		sceKernelDelayThreadCB(1000 * 1000);
		/* (the hold on auto-suspend renewed every second for the whole job, not only as bytes come: a long check or a
		slow server keeps the Vita awake too; the screen may still dim and turn off on its own) */
		if (locked)
			keep_awake();
		if (sleeping && sceKernelGetProcessTimeWide() - sleep_since > SLEEP_WITHOUT_SUSPEND_US)
		{
			sleeping = 0;
			vita_host_log("steam: no suspend followed the power event: the download carries on");
			steam_job_sleep(0);
		}
	}
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
	struct steam_job_platform platform = { free_space, battery_low, keep_awake, busy, log_line, roots, battery_ok,
		device_secret };
	char date[32];
	SceUID thread;

	steam_job_set_platform(&platform);
	/* (whether this console gives the kept sign-in's secret - its size alone is said, never its bytes) */
	{
		uint8_t secret[32];
		size_t size = device_secret(secret, sizeof(secret));
		int days = 0, state = steam_job_signin_state(&days);

		char line[160];

		memset(secret, 0, sizeof(secret));
		if (size)
			snprintf(line, sizeof(line), "steam: this console can keep a Steam sign-in (sealed to its %u-byte console id)",
				(unsigned int)size);
		else
			snprintf(line, sizeof(line), "steam: this console gives no console id: a Steam sign-in is never kept");
		vita_host_log(line);
		if (state)
		{
			snprintf(line, sizeof(line), "steam: a kept Steam sign-in: %s", state == 1 ? "here" : state == 2 ? "expired" :
				"unreadable");
			vita_host_log(line);
		}
	}
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
