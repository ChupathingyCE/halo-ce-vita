/*
VITA_FRAME_SPLIT.C

Where the game thread's frame goes (configure.py --vita-frame-split, a test
build's): the game's main loop steps, the tick's updates and the render's
passes, timed from outside the game, through the linker's --wrap of each
function the game calls from another unit (port/vita/build.py lists them),
and the time the game thread spends in the Vita's Direct3D device
(d3d8_gxm.c, vita_textures.c and the Cg translators, built with
-finstrument-functions-after-inlining in such a build), apart from the
game's own render code. HALO_FRAME_TIMING=n (env.txt) writes a frame-split
line to halo.log every n frames presented, and each thread's run time a
frame over the same frames (vita_host_threads_report). Nothing in the game is
changed; without HALO_FRAME_TIMING the wrappers only call through.

The steps nest (a tick's updates inside the tick, the render's passes
inside the render): each line gives a step's own total, and "rest" what its
parent spent outside the steps named.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"


enum
{
	/* the main loop */
	_split_input,
	_split_ui,
	_split_player_control,
	_split_tick,
	_split_director,
	_split_observer,
	_split_sound,
	_split_render,
	_split_present,
	/* the tick's updates (game_tick) */
	_split_game_tick,
	_split_units,
	_split_ai,
	_split_effects,
	_split_objects,
	_split_players_before,
	_split_players_after,
	_split_hud,
	_split_hs,
	_split_engine,
	_split_first_person,
	_split_antennas,
	_split_player_effect,
	_split_recorded_animations,
	_split_scenario,
	_split_lua,
	_split_allegiance,
	/* the render's passes (render_frame) */
	_split_visibility,
	_split_sky,
	_split_lights_preprocess,
	_split_structure_preprocess,
	_split_lightmaps,
	_split_diffuse,
	_split_lights_diffuse,
	_split_specular_lightmaps,
	_split_lights_specular,
	_split_reflections,
	_split_structure_transparent,
	_split_fog,
	_split_render_objects,
	_split_shadows,
	_split_particles,
	_split_particle_systems,
	_split_weather,
	_split_contrails,
	_split_transparent,
	_split_decals,
	_split_detail_objects,
	_split_flares,
	_split_interface,
	_split_ui_widgets,
	_split_frame_begin,
	_split_frame_end,
	NUMBER_OF_SPLITS
};

static const char *const split_names[NUMBER_OF_SPLITS] = {
	"input", "ui", "player_control", "tick", "director", "observer", "sound", "render", "present",
	"game_tick", "units", "ai", "effects", "objects", "players_before", "players_after", "hud", "hs", "engine",
	"first_person", "antennas", "player_effect", "recorded_animations", "scenario", "lua", "allegiance",
	"visibility", "sky", "lights_preprocess", "structure_preprocess", "lightmaps", "diffuse", "lights_diffuse",
	"specular_lightmaps", "lights_specular", "reflections", "structure_transparent", "fog", "objects", "shadows",
	"particles", "particle_systems", "weather", "contrails", "transparent", "decals", "detail_objects", "flares",
	"interface", "ui_widgets", "frame_begin", "frame_end",
};

static int split_enabled = -1;
static unsigned long split_period;
static unsigned long game_thread;
static unsigned long long split_us[NUMBER_OF_SPLITS];
static unsigned long split_calls[NUMBER_OF_SPLITS];
static unsigned long long frames_started, frame_us_total, frame_us_worst;
static unsigned long frames;

/* the game thread's time in the Vita's Direct3D device, apart from the
render passes': the outermost instrumented function's (depth 1) */
static int d3d_depth;
static unsigned long long d3d_entered, d3d_us, d3d_us_split[NUMBER_OF_SPLITS];
static unsigned long d3d_calls;
static int current_split = -1;

static int split_on(void)
{
	if (split_enabled < 0)
	{
		const char *setting = getenv("HALO_FRAME_TIMING");

		split_period = setting ? strtoul(setting, NULL, 10) : 0;
		split_enabled = split_period > 0;
		if (split_enabled)
		{
			char line[160];

			game_thread = vita_host_thread_id();
			vita_host_thread_watch("game");
			snprintf(line, sizeof(line), "frame-split: on, a line every %lu frames (HALO_FRAME_TIMING)", split_period);
			vita_host_log(line);
		}
	}
	return split_enabled > 0 && vita_host_thread_id() == game_thread;
}

#define SPLIT_BEGIN(split) \
	int on = split_on(); unsigned long long started = 0; int outer = current_split; \
	if (on) { started = vita_host_time_us(); current_split = (split); split_calls[split]++; }
#define SPLIT_END(split) \
	if (on) { split_us[split] += vita_host_time_us() - started; current_split = outer; }

#define WRAP_VOID(name, split, parameters, arguments) \
	void __real_##name parameters; \
	void __wrap_##name parameters \
	{ \
		SPLIT_BEGIN(split); \
		__real_##name arguments; \
		SPLIT_END(split); \
	}
#define WRAP_RESULT(type, name, split, parameters, arguments) \
	type __real_##name parameters; \
	type __wrap_##name parameters \
	{ \
		type result; \
		SPLIT_BEGIN(split); \
		result = __real_##name arguments; \
		SPLIT_END(split); \
		return result; \
	}

typedef float real;
typedef unsigned char boolean;

/* the main loop (main.c) */
WRAP_VOID(input_update, _split_input, (void), ())
WRAP_VOID(process_ui_widgets, _split_ui, (void), ())
WRAP_RESULT(boolean, player_control_update, _split_player_control, (real seconds), (seconds))
WRAP_VOID(game_time_update, _split_tick, (real seconds), (seconds))
WRAP_VOID(director_update, _split_director, (real seconds), (seconds))
WRAP_VOID(observer_update, _split_observer, (real seconds), (seconds))
WRAP_VOID(sound_render, _split_sound, (void), ())
WRAP_VOID(render_frame, _split_render, (void *windows, short count, void const *page, void const *index, void *bitmap,
	real seconds), (windows, count, page, index, bitmap, seconds))

/* the tick (game_time.c, game.c) */
WRAP_VOID(game_tick, _split_game_tick, (void), ())
WRAP_VOID(units_update, _split_units, (void), ())
WRAP_VOID(ai_update, _split_ai, (void), ())
WRAP_VOID(effects_update, _split_effects, (real seconds), (seconds))
WRAP_VOID(objects_update, _split_objects, (void), ())
WRAP_VOID(players_update_before_game, _split_players_before, (void), ())
WRAP_VOID(players_update_after_game, _split_players_after, (void), ())
WRAP_VOID(hud_update, _split_hud, (void), ())
WRAP_VOID(hs_update, _split_hs, (void), ())
WRAP_VOID(game_engine_update, _split_engine, (void), ())
WRAP_VOID(first_person_weapons_update, _split_first_person, (void), ())
WRAP_VOID(antennas_update, _split_antennas, (real seconds), (seconds))
WRAP_VOID(player_effect_update, _split_player_effect, (void), ())
WRAP_VOID(recorded_animations_update, _split_recorded_animations, (void), ())
WRAP_VOID(scenario_tick_update, _split_scenario, (void), ())
WRAP_VOID(lua_scripting_tick, _split_lua, (void), ())
WRAP_VOID(game_allegiance_update, _split_allegiance, (void), ())

/* the render's passes (render.c) */
WRAP_VOID(structure_visibility_compute, _split_visibility, (void), ())
WRAP_VOID(render_sky, _split_sky, (void), ())
WRAP_VOID(lights_preprocess_scene, _split_lights_preprocess, (void), ())
WRAP_VOID(structure_render_preprocess, _split_structure_preprocess, (void), ())
WRAP_VOID(structure_render_lightmaps, _split_lightmaps, (void), ())
WRAP_VOID(structure_render_diffuse_texture, _split_diffuse, (void), ())
WRAP_VOID(lights_render_diffuse, _split_lights_diffuse, (void), ())
WRAP_VOID(structure_render_specular_lightmaps, _split_specular_lightmaps, (void), ())
WRAP_VOID(lights_render_specular, _split_lights_specular, (void), ())
WRAP_VOID(structure_render_reflections, _split_reflections, (void), ())
WRAP_VOID(structure_render_transparent_geometry, _split_structure_transparent, (void), ())
WRAP_VOID(structure_render_fog, _split_fog, (void), ())
WRAP_VOID(render_objects, _split_render_objects, (void), ())
WRAP_VOID(render_object_shadows, _split_shadows, (void), ())
WRAP_VOID(render_particles, _split_particles, (void), ())
WRAP_VOID(particle_systems_render, _split_particle_systems, (void), ())
WRAP_VOID(weather_particle_systems_render, _split_weather, (void), ())
WRAP_VOID(render_contrails_normal, _split_contrails, (void), ())
WRAP_VOID(rasterizer_transparent_geometry_draw, _split_transparent, (boolean water), (water))
WRAP_VOID(rasterizer_decals_draw, _split_decals, (short cluster), (cluster))
WRAP_VOID(structure_render_detail_objects, _split_detail_objects, (void), ())
WRAP_VOID(rasterizer_lens_flares_draw, _split_flares, (void), ())
WRAP_VOID(interface_draw_screen, _split_interface, (void), ())
WRAP_VOID(render_ui_widgets, _split_ui_widgets, (short player, void const *bounds), (player, bounds))
WRAP_VOID(rasterizer_frame_begin, _split_frame_begin, (void const *parameters), (parameters))
WRAP_VOID(rasterizer_frame_end, _split_frame_end, (void), ())

static void split_report(void)
{
	static char line[2400];
	int n, index;
	double per = 1.0 / (double)frames / 1000.0;
	double frame = (double)frame_us_total * per;
	unsigned long long ticks = split_calls[_split_game_tick];

	n = snprintf(line, sizeof(line), "frame-split (ms/frame, %lu frames, %.1f fps, worst frame %.1f ms, %.2f ticks/frame):"
		" frame %.2f", frames, frame > 0 ? 1000.0 / frame : 0.0, (double)frame_us_worst / 1000.0,
		(double)ticks / (double)frames, frame);
	{
		unsigned long long marked = 0;

		for (index = _split_input; index <= _split_present; index++)
		{
			marked += split_us[index];
			n += snprintf(line + n, sizeof(line) - n, " | %s %.2f", split_names[index], (double)split_us[index] * per);
		}
		n += snprintf(line + n, sizeof(line) - n, " | loop rest %.2f",
			frame_us_total > marked ? (double)(frame_us_total - marked) * per : 0.0);
	}
	vita_host_log(line);

	/* the tick's updates, per tick */
	{
		unsigned long long marked = 0;
		double per_tick = ticks ? 1.0 / (double)ticks / 1000.0 : 0.0;

		n = snprintf(line, sizeof(line), "frame-split tick (ms/tick, %llu ticks): game_tick %.2f", ticks,
			(double)split_us[_split_game_tick] * per_tick);
		for (index = _split_units; index <= _split_allegiance; index++)
		{
			marked += split_us[index];
			if (split_us[index] * 20 >= split_us[_split_game_tick] / 10 || index <= _split_objects)
				n += snprintf(line + n, sizeof(line) - n, " | %s %.2f", split_names[index], (double)split_us[index] * per_tick);
		}
		n += snprintf(line + n, sizeof(line) - n, " | tick rest %.2f | time update outside ticks %.2f ms/frame",
			split_us[_split_game_tick] > marked ? (double)(split_us[_split_game_tick] - marked) * per_tick : 0.0,
			split_us[_split_tick] > split_us[_split_game_tick] ?
				(double)(split_us[_split_tick] - split_us[_split_game_tick]) * per : 0.0);
		vita_host_log(line);
	}

	/* the render's passes, and the Direct3D device's share of each */
	{
		unsigned long long marked = 0, device = d3d_us_split[_split_render];

		for (index = _split_visibility; index < NUMBER_OF_SPLITS; index++)
			device += d3d_us_split[index];
		n = snprintf(line, sizeof(line), "frame-split render (ms/frame, [the Vita's Direct3D device's part]): render %.2f [%.2f]",
			(double)split_us[_split_render] * per, (double)device * per);
		for (index = _split_visibility; index < NUMBER_OF_SPLITS; index++)
		{
			marked += split_us[index];
			if (split_us[index] * per >= 0.05)
				n += snprintf(line + n, sizeof(line) - n, " | %s %.2f [%.2f]", split_names[index],
					(double)split_us[index] * per, (double)d3d_us_split[index] * per);
		}
		n += snprintf(line + n, sizeof(line) - n, " | render rest %.2f [%.2f]",
			split_us[_split_render] > marked ? (double)(split_us[_split_render] - marked) * per : 0.0,
			(double)d3d_us_split[_split_render] * per);
		vita_host_log(line);
	}

	/* the device's time on the game thread, by the step it was called in */
	n = snprintf(line, sizeof(line), "frame-split d3d (ms/frame on the game thread, %.0f calls/frame): all %.2f",
		(double)d3d_calls / (double)frames, (double)d3d_us * per);
	for (index = 0; index < NUMBER_OF_SPLITS; index++)
		if (d3d_us_split[index] * per >= 0.05 && index != _split_render)
			n += snprintf(line + n, sizeof(line) - n, " | %s %.2f", split_names[index], (double)d3d_us_split[index] * per);
	vita_host_log(line);

	vita_host_threads_report(frames);
	vita_host_thread_times_report(frames);

	memset(split_us, 0, sizeof(split_us));
	memset(split_calls, 0, sizeof(split_calls));
	memset(d3d_us_split, 0, sizeof(d3d_us_split));
	d3d_us = 0;
	d3d_calls = 0;
	frame_us_total = 0;
	frame_us_worst = 0;
	frames = 0;
}

/* the present (render.c's render_frame_present): a frame's end */
void __real_render_frame_present(void const *index, void *bitmap);
void __wrap_render_frame_present(void const *index, void *bitmap)
{
	unsigned long long now;

	SPLIT_BEGIN(_split_present);
	__real_render_frame_present(index, bitmap);
	SPLIT_END(_split_present);
	if (!on)
		return;
	now = vita_host_time_us();
	if (frames_started)
	{
		unsigned long long frame = now - frames_started;

		frame_us_total += frame;
		if (frame > frame_us_worst)
			frame_us_worst = frame;
		/* (a map's load, a pause over 2 s: not a frame of the game's) */
		if (frame > 2000000ull)
		{
			frame_us_total -= frame;
			memset(split_us, 0, sizeof(split_us));
			memset(split_calls, 0, sizeof(split_calls));
			memset(d3d_us_split, 0, sizeof(d3d_us_split));
			d3d_us = 0;
			d3d_calls = 0;
			frame_us_total = 0;
			frame_us_worst = 0;
			frames = 0;
		}
		else if (++frames >= split_period)
			split_report();
	}
	frames_started = now;
}

/* -finstrument-functions-after-inlining (the Vita's Direct3D device, in a
--vita-frame-split build): the game thread's time inside it */
void __cyg_profile_func_enter(void *function, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *function, void *site) __attribute__((no_instrument_function));

void __cyg_profile_func_enter(void *function, void *site)
{
	(void)function;
	(void)site;
	if (split_enabled <= 0 || vita_host_thread_id() != game_thread)
		return;
	if (d3d_depth++ == 0)
	{
		d3d_entered = vita_host_time_us();
		d3d_calls++;
	}
}

void __cyg_profile_func_exit(void *function, void *site)
{
	(void)function;
	(void)site;
	if (split_enabled <= 0 || vita_host_thread_id() != game_thread || d3d_depth <= 0)
		return;
	if (--d3d_depth == 0)
	{
		unsigned long long spent = vita_host_time_us() - d3d_entered;

		d3d_us += spent;
		if (current_split >= 0)
			d3d_us_split[current_split] += spent;
	}
}
