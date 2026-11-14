/*
COMMAND_NET.INC

Network Command (included by command.c; the rules: command_net_protocol.h).
A Command game is played by several machines, each its human's commander's
view above the map (or first person in his side's Spartan): red's
commanders on team 0, blue's on team 1 (the lobby's side), as many on a
side as play it (co-op: the side's squads, resources and Spartan shared, as
Halo Wars' co-op shares an army - either may order any squad and buy at the
HQ; the first to take the Spartan is in it), a side with no human the AI
commander's.

The host runs the game - the AI and its actors (network_actors.c), both
AI commanders, the economy, the flights, the objectives, the Spartans - and
the netcode gives every machine its objects. A client runs its commander's
view alone: what it chooses is its own (the selection, the camera, the HQ
panel's row); what it does is a request (command_net_protocol.h) the host
checks and makes, as its own commander's (the side his player's team on the
host, never what the request says). The host sends each client what the HUD
and the view show - the game (resources, scores, the time, upgrades, the
Spartans), the squads' table, the territories - as each entry changes
(compared with what it sent last, a few times a second), all of them to a
client that asks (as its game begins, or joining one in progress), and its
notices (command_event). A client leaving: its side's Spartan let go back
to the AI (command_player_quitting), and its side the AI commander's once
it has no human (command_humans_update).
*/

/* ---------- constants */

enum
{
	/* the state entries a message holds (the squads, the territories and
	the game's in one), the requests */
	COMMAND_NET_STATES_PER_MESSAGE = 40,
	COMMAND_NET_REQUESTS_PER_MESSAGE = 16,
	/* the host's state compared and sent this often */
	COMMAND_NET_SEND_MILLISECONDS = 100,
	/* the events waiting to go */
	COMMAND_NET_EVENTS = 32,
	/* the machines tracked (the netcode's machine indices) */
	COMMAND_NET_MACHINES = 16,
	/* a machine's drops logged at most this often */
	COMMAND_NET_DROP_LOG_MILLISECONDS = 1000,
};

typedef char command_net_squads_assert[COMMAND_MAXIMUM_SQUADS == COMMAND_NET_MAXIMUM_SQUADS ? 1 : -1];
typedef char command_net_squad_size_assert[COMMAND_SQUAD_SIZE == COMMAND_NET_SQUAD_SIZE ? 1 : -1];
typedef char command_net_territories_assert[COMMAND_TERRITORIES == COMMAND_NET_MAXIMUM_TERRITORIES ? 1 : -1];
typedef char command_net_upgrades_assert[NUMBER_OF_COMMAND_UPGRADES == COMMAND_NET_UPGRADES ? 1 : -1];
typedef char command_net_orders_assert[NUMBER_OF_COMMAND_ORDERS == COMMAND_NET_ORDERS ? 1 : -1];
typedef char command_net_ships_assert[NUMBER_OF_COMMAND_SHIPS == COMMAND_NET_SHIPS ? 1 : -1];
typedef char command_net_rows_assert[COMMAND_BUYS <= COMMAND_NET_MAXIMUM_ROWS ? 1 : -1];
typedef char command_net_draw_assert[COMMAND_DRAW == COMMAND_NET_SIDES ? 1 : -1];
typedef char command_net_players_assert[COMMAND_PLAYERS <= COMMAND_NET_MAXIMUM_PLAYERS ? 1 : -1];

/* ---------- structures */

struct command_net_requests_message
{
	struct distributed_message_header header;
	struct command_net_request requests[COMMAND_NET_REQUESTS_PER_MESSAGE];
};

struct command_net_states_message
{
	struct distributed_message_header header;
	struct command_net_state states[COMMAND_NET_STATES_PER_MESSAGE];
};

typedef char command_net_requests_message_fits[sizeof(struct command_net_requests_message) <= 0xFFF ? 1 : -1];
typedef char command_net_states_message_fits[sizeof(struct command_net_states_message) <= 0xFFF ? 1 : -1];

/* ---------- globals */

static struct
{
	/* --- the host: what it sent last of each entry (a delta's base, valid
	once sent), when it last compared; each client machine's requests'
	sequence, rate, drops, its sync asked */
	struct command_net_state sent_game;
	struct command_net_state sent_tally;
	struct command_net_state sent_squads[COMMAND_MAXIMUM_SQUADS];
	struct command_net_state sent_territories[COMMAND_TERRITORIES];
	boolean sent_valid;
	unsigned long sent_time;
	struct
	{
		boolean used;
		long machine_index;
		struct command_net_sequence sequence;
		struct command_net_bucket bucket;
		boolean sync_pending;
		unsigned long sync_time;
		long drops[NUMBER_OF_COMMAND_NET_DROPS];
		long drops_said[NUMBER_OF_COMMAND_NET_DROPS];
		unsigned long drops_logged;
		long taken;
		long anticheat_tick;
	} machines[COMMAND_NET_MACHINES];
	/* the host's events waiting to go (to everyone; a refusal to its
	machine at once) */
	struct command_net_state events[COMMAND_NET_EVENTS];
	short event_count;
	/* --- a client: its requests' sequence and those waiting; the host's
	state had (its sync's answer come), when it last asked; the host's
	squads coming, each side's; counts */
	uint16_t sequence;
	struct command_net_request requests[COMMAND_NET_REQUESTS_PER_MESSAGE];
	short request_count;
	boolean synced;
	unsigned long sync_asked;
	short coming[NUMBER_OF_COMMAND_SIDES];
	long states_taken;
	long states_dropped;
	long requests_sent;
} command_net;

/* ---------- the roles */

static boolean command_net_host(
	void)
{
	return game_connection() == _game_connection_network_server && command_globals.game_running &&
		forge_net_command_session();
}

static boolean command_net_client(
	void)
{
	return game_connection() == _game_connection_network_client && forge_net_command_session();
}

/* ---------- the host: the state */

static uint8_t command_net_side_byte(
	short side)
{
	return side >= 0 && side < NUMBER_OF_COMMAND_SIDES ? (uint8_t)side : COMMAND_NET_NO_SIDE;
}

/* an object's index for a client: a living unit's (the netcode makes it
there at the host's index), else none */
static int32_t command_net_unit(
	long unit_index)
{
	return command_living_unit(unit_index) ? (int32_t)unit_index : COMMAND_NET_NO_OBJECT;
}

static void command_net_game_fill(
	struct command_net_state *state)
{
	struct command_net_state_game *game = &state->u.game;
	long left = command_time_left();
	short side, index;

	csmemset(state, 0, sizeof(*state));
	state->kind = _command_net_state_game;
	game->version = COMMAND_NET_VERSION;
	game->flags = (command_globals.game_running ? COMMAND_NET_GAME_RUNNING : 0) |
		(command_objectives.over ? COMMAND_NET_GAME_OVER : 0);
	game->objectives = (uint8_t)(command_objectives.objectives & 3);
	game->winner = command_objectives.over && command_objectives.winner >= 0 && command_objectives.winner <= COMMAND_DRAW ?
		(uint8_t)command_objectives.winner : 0;
	game->score_limit = (int32_t)PIN(command_objectives.score_limit, 0, COMMAND_NET_MAXIMUM_SCORE);
	game->seconds_left = left == NONE ? -1 : (int32_t)PIN(left / TICKS_PER_SECOND, 0, COMMAND_NET_MAXIMUM_SECONDS);
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		long most = 0;

		game->resources[side] = (int32_t)PIN(command_globals.resources[side], -COMMAND_NET_MAXIMUM_RESOURCES,
			COMMAND_NET_MAXIMUM_RESOURCES);
		game->scores[side] = (int32_t)PIN(command_objectives.scores[side], 0, COMMAND_NET_MAXIMUM_SCORE);
		for (index = 0; index < NUMBER_OF_COMMAND_UPGRADES; index++)
		{
			if (command_upgrades[side][index])
				game->upgrades[side] |= (uint8_t)(1 << index);
			else if (command_upgrade_done_time[side][index])
			{
				long seconds = (command_upgrade_done_time[side][index] - game_time_get()) / TICKS_PER_SECOND + 1;

				game->building[side] |= (uint8_t)(1 << index);
				most = MAX(most, seconds);
			}
		}
		game->building_seconds[side] = (uint8_t)PIN(most, 0, 255);
		game->spartans[side] = command_net_unit(command_spartans[side].unit_index);
		game->possessors[side] = command_spartans[side].possessor != NONE ? (uint8_t)command_spartans[side].possessor :
			COMMAND_NET_NO_PLAYER;
		game->owned[side] = command_spartan_owned[side] ? 1 : 0;
		game->coming[side] = (uint8_t)PIN(command_squads_coming(side), 0, 8);
		game->ai[side] = command_ai[side].plays ? 1 : 0;
		if (!command_objectives.flags[side].valid)
			game->flag_states[side] = _command_net_flag_none;
		else if (!command_objectives.flags[side].away)
			game->flag_states[side] = _command_net_flag_home;
		else if (command_objectives.flags[side].carrier_index != NONE)
			game->flag_states[side] = _command_net_flag_carried;
		else
			game->flag_states[side] = _command_net_flag_dropped;
		game->flag_objects[side] = command_objectives.flags[side].valid && object_try_and_get(command_objectives.flags[side].weapon_index) ?
			(int32_t)command_objectives.flags[side].weapon_index : COMMAND_NET_NO_OBJECT;
	}
}

static void command_net_squad_fill(
	short index,
	struct command_net_state *state)
{
	struct command_squad const *squad = &command_globals.squads[index];
	struct command_net_state_squad *entry = &state->u.squad;
	short member;

	csmemset(state, 0, sizeof(*state));
	state->kind = _command_net_state_squad;
	state->index = (uint8_t)index;
	/* (another mode's squads are never Command's: none) */
	if (!squad->used || squad->hosted || squad->side < 0 || squad->side >= NUMBER_OF_COMMAND_SIDES)
		return;
	entry->flags = COMMAND_NET_SQUAD_USED | (squad->hero ? COMMAND_NET_SQUAD_HERO : 0) | (squad->crew ? COMMAND_NET_SQUAD_CREW : 0) |
		(squad->airborne ? COMMAND_NET_SQUAD_AIRBORNE : 0) | (squad->arrived ? COMMAND_NET_SQUAD_ARRIVED : 0);
	entry->side = (uint8_t)squad->side;
	entry->order = (uint8_t)(squad->order >= 0 && squad->order < NUMBER_OF_COMMAND_ORDERS ? squad->order : 0);
	entry->number = (uint16_t)PIN(squad->number, 0, COMMAND_NET_MAXIMUM_SQUAD_NUMBER);
	for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
		entry->units[member] = command_net_unit(squad->units[member]);
	entry->point[0] = squad->point.x;
	entry->point[1] = squad->point.y;
	entry->point[2] = squad->point.z;
	if (!command_net_point_valid(entry->point))
		entry->point[0] = entry->point[1] = entry->point[2] = 0.f;
	/* (the name's printable characters, ended within its room) */
	{
		short at, length = 0;

		for (at = 0; squad->name[at] && length < COMMAND_NET_NAME_SIZE - 1; at++)
		{
			if ((unsigned char)squad->name[at] >= 0x20 && (unsigned char)squad->name[at] <= 0x7E)
				entry->name[length++] = squad->name[at];
		}
	}
}

static void command_net_territory_fill(
	short index,
	struct command_net_state *state)
{
	struct command_territory const *territory = &command_territories[index];
	struct command_net_state_territory *entry = &state->u.territory;

	csmemset(state, 0, sizeof(*state));
	state->kind = _command_net_state_territory;
	state->index = (uint8_t)index;
	if (index >= command_territory_count)
		return;
	entry->used = 1;
	entry->owner = command_net_side_byte(territory->owner);
	entry->taker = command_net_side_byte(territory->taker);
	entry->letter = territory->objective && territory->letter >= 'A' && territory->letter <= 'Z' ? (uint8_t)territory->letter : 0;
	entry->center[0] = territory->center.x;
	entry->center[1] = territory->center.y;
	entry->center[2] = territory->center.z;
	entry->radius = PIN(territory->radius, 0.f, 100.f);
	if (!command_net_point_valid(entry->center))
		entry->center[0] = entry->center[1] = entry->center[2] = 0.f;
}

static void command_net_tally_fill(
	struct command_net_state *state)
{
	struct command_net_state_tally *tally = &state->u.tally;
	short side;

	csmemset(state, 0, sizeof(*state));
	state->kind = _command_net_state_tally;
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		tally->captures[side] = (int32_t)PIN(command_objectives.captures[side], 0, COMMAND_NET_MAXIMUM_TALLY);
		tally->territory_seconds[side] = (int32_t)PIN(command_objectives.territory_seconds[side], 0, COMMAND_NET_MAXIMUM_TALLY);
		tally->kills[side] = (int32_t)PIN(command_objectives.kills[side], 0, COMMAND_NET_MAXIMUM_TALLY);
		tally->losses[side] = (int32_t)PIN(command_objectives.losses[side], 0, COMMAND_NET_MAXIMUM_TALLY);
	}
	tally->spartan_kills = (int32_t)PIN(command_objectives.spartan_kills, 0, COMMAND_NET_MAXIMUM_TALLY);
	tally->spartan_captures = (int32_t)PIN(command_objectives.spartan_captures, 0, COMMAND_NET_MAXIMUM_TALLY);
}

/* states sent in messages: to every client (machine NONE) or to one */
static void command_net_send_states(
	struct command_net_state const *states,
	short count,
	long machine_index)
{
	struct command_net_states_message message;
	short at = 0;

	while (at < count)
	{
		short chunk = (short)MIN(count - at, COMMAND_NET_STATES_PER_MESSAGE);
		word size = (word)(sizeof(message.header) + chunk * sizeof(struct command_net_state));

		csmemcpy(message.states, states + at, chunk * sizeof(struct command_net_state));
		if (machine_index == NONE)
			distributed_send(&message, _distributed_message_command_state, chunk, size, _distributed_to_clients_reliably);
		else
			distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_command_state, chunk, size);
		at = (short)(at + chunk);
	}
}

/* the whole state now (the game's, every squad's and territory's) */
static short command_net_snapshot(
	struct command_net_state *states)
{
	short count = 0, index;

	command_net_game_fill(&states[count++]);
	command_net_tally_fill(&states[count++]);
	for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
		command_net_squad_fill(index, &states[count++]);
	for (index = 0; index < COMMAND_TERRITORIES; index++)
		command_net_territory_fill(index, &states[count++]);
	return count;
}

/* (the host, each frame) what changed sent to every client, the events with
it; the whole to a client that asked */
static void command_net_host_frame(
	void)
{
	struct command_net_state now[2 + COMMAND_MAXIMUM_SQUADS + COMMAND_TERRITORIES];
	struct command_net_state changed[2 + COMMAND_MAXIMUM_SQUADS + COMMAND_TERRITORIES + COMMAND_NET_EVENTS];
	short count, changes = 0, index;
	unsigned long time = system_milliseconds();

	if (!command_net_host())
		return;
	if (time - command_net.sent_time < COMMAND_NET_SEND_MILLISECONDS && !command_net.event_count)
		return;
	command_net.sent_time = time;
	count = command_net_snapshot(now);
	if (!command_net.sent_valid || memcmp(&now[0], &command_net.sent_game, sizeof(now[0])))
		changed[changes++] = now[0];
	if (!command_net.sent_valid || memcmp(&now[1], &command_net.sent_tally, sizeof(now[0])))
		changed[changes++] = now[1];
	for (index = 0; index < COMMAND_MAXIMUM_SQUADS; index++)
	{
		if (!command_net.sent_valid || memcmp(&now[2 + index], &command_net.sent_squads[index], sizeof(now[0])))
			changed[changes++] = now[2 + index];
	}
	for (index = 0; index < COMMAND_TERRITORIES; index++)
	{
		if (!command_net.sent_valid || memcmp(&now[2 + COMMAND_MAXIMUM_SQUADS + index], &command_net.sent_territories[index],
				sizeof(now[0])))
		{
			changed[changes++] = now[2 + COMMAND_MAXIMUM_SQUADS + index];
		}
	}
	/* (the events after the state they speak of) */
	for (index = 0; index < command_net.event_count; index++)
		changed[changes++] = command_net.events[index];
	command_net.event_count = 0;
	if (changes)
		command_net_send_states(changed, changes, NONE);
	command_net.sent_game = now[0];
	command_net.sent_tally = now[1];
	csmemcpy(command_net.sent_squads, &now[2], sizeof(command_net.sent_squads));
	csmemcpy(command_net.sent_territories, &now[2 + COMMAND_MAXIMUM_SQUADS], sizeof(command_net.sent_territories));
	command_net.sent_valid = TRUE;
	/* (a client's sync: the whole, to it alone) */
	for (index = 0; index < COMMAND_NET_MACHINES; index++)
	{
		if (!command_net.machines[index].used || !command_net.machines[index].sync_pending)
			continue;
		command_net.machines[index].sync_pending = FALSE;
		command_net_send_states(now, count, command_net.machines[index].machine_index);
		platform_log("command net: machine %ld given the game (%d entries)", command_net.machines[index].machine_index, (int)count);
	}
	/* (each machine's drops, said once a second while there are new ones) */
	for (index = 0; index < COMMAND_NET_MACHINES; index++)
	{
		short drop;
		boolean news = FALSE;

		if (!command_net.machines[index].used || time - command_net.machines[index].drops_logged < COMMAND_NET_DROP_LOG_MILLISECONDS)
			continue;
		for (drop = 1; drop < NUMBER_OF_COMMAND_NET_DROPS; drop++)
			news |= command_net.machines[index].drops[drop] != command_net.machines[index].drops_said[drop];
		if (!news)
			continue;
		command_net.machines[index].drops_logged = time;
		csmemcpy(command_net.machines[index].drops_said, command_net.machines[index].drops, sizeof(command_net.machines[index].drops));
		platform_log("command net: machine %ld: %ld malformed, %ld replayed, %ld rate limited, %ld refused requests dropped "
			"(%ld taken)", command_net.machines[index].machine_index, command_net.machines[index].drops[_command_net_drop_malformed],
			command_net.machines[index].drops[_command_net_drop_replayed], command_net.machines[index].drops[_command_net_drop_rate],
			command_net.machines[index].drops[_command_net_drop_refused], command_net.machines[index].taken);
	}
}

/* an event as an entry */
static void command_net_event_fill(
	struct command_net_state *state,
	short audience,
	short subject,
	short code,
	long a,
	long b)
{
	csmemset(state, 0, sizeof(*state));
	state->kind = _command_net_state_event;
	state->u.event.code = (uint8_t)code;
	state->u.event.subject = command_net_side_byte(subject);
	state->u.event.audience = audience == COMMAND_EVENT_EVERYONE ? COMMAND_NET_AUDIENCE_EVERYONE : command_net_side_byte(audience);
	state->u.event.a = (int32_t)PIN(a, -1, 0xFFFF);
	state->u.event.b = (int32_t)PIN(b, -1, 0xFFFF);
}

static void command_net_event_send(
	short audience,
	short subject,
	short code,
	long a,
	long b)
{
	if (!command_net_host() || command_net.event_count >= COMMAND_NET_EVENTS ||
		(audience != COMMAND_EVENT_EVERYONE && (audience < 0 || audience >= NUMBER_OF_COMMAND_SIDES)))
	{
		return;
	}
	command_net_event_fill(&command_net.events[command_net.event_count++], audience, subject, code, a, b);
}

static void command_net_event_send_to_machine(
	long machine_index,
	short subject,
	short code,
	long a,
	long b)
{
	struct command_net_state state;

	if (!command_net_host())
		return;
	command_net_event_fill(&state, subject, subject, code, a, b);
	command_net_send_states(&state, 1, machine_index);
}

/* ---------- the host: the requests */

static short command_net_machine_slot(
	long machine_index,
	boolean make)
{
	short index, free_index = NONE;

	for (index = 0; index < COMMAND_NET_MACHINES; index++)
	{
		if (command_net.machines[index].used && command_net.machines[index].machine_index == machine_index)
			return index;
		if (!command_net.machines[index].used && free_index == NONE)
			free_index = index;
	}
	if (!make || free_index == NONE)
		return NONE;
	csmemset(&command_net.machines[free_index], 0, sizeof(command_net.machines[free_index]));
	command_net.machines[free_index].used = TRUE;
	command_net.machines[free_index].machine_index = machine_index;
	command_net.machines[free_index].anticheat_tick = NONE;
	return free_index;
}

/* the machine's player (its first: a Command game's commander is a
machine's first player), NONE */
static long command_net_machine_player(
	long machine_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short slot = command_player_slot(iterator.datum_index);

		if (slot != NONE && distributed_machine_has_player(machine_index, slot))
			return iterator.datum_index;
	}
	return NONE;
}

/* a drop counted against its machine (a burst of them, a tick's, weighs as
one: network_anticheat.c drops a machine sending them tick after tick) */
static void command_net_drop(
	short slot,
	short why)
{
	command_net.machines[slot].drops[why]++;
	/* (a burst, or what the game refused - squads gone, a point past the
	map's edge -, as a commander's view can ask: not against it) */
	if (why == _command_net_drop_rate || why == _command_net_drop_refused)
		return;
	if (command_net.machines[slot].anticheat_tick == game_time_get() + 1)
		return;
	command_net.machines[slot].anticheat_tick = game_time_get() + 1;
	network_anticheat_violation(command_net.machines[slot].machine_index, _anticheat_forge, TRUE, 1.0f);
}

/* whether a point of a client's is on the map: within the view's bounds
(the map's spawns, flags and items, and a margin), at a height near them */
static boolean command_net_point_on_map(
	real_point3d const *point)
{
	real margin = COMMAND_BOUNDS_MARGIN * 2.f;

	if (!command_globals.bounds_valid)
		return FALSE;
	return point->x >= command_globals.bounds[0] - margin && point->x <= command_globals.bounds[1] + margin &&
		point->y >= command_globals.bounds[2] - margin && point->y <= command_globals.bounds[3] + margin &&
		point->z > -200.f && point->z < 400.f;
}

/* (the host) a client's request: checked, made as its commander's */
static void command_net_host_request(
	short slot,
	struct command_net_request const *request)
{
	long machine_index = command_net.machines[slot].machine_index;
	long player_index;
	short side;
	char const *why = NULL;

	if (!command_net_request_valid(request, COMMAND_BUYS))
	{
		command_net_drop(slot, _command_net_drop_malformed);
		return;
	}
	if (!command_net_sequence_take(&command_net.machines[slot].sequence, request->sequence))
	{
		command_net_drop(slot, _command_net_drop_replayed);
		return;
	}
	if (!command_net_bucket_take(&command_net.machines[slot].bucket, (uint32_t)system_milliseconds(),
			COMMAND_NET_REQUESTS_PER_SECOND, COMMAND_NET_REQUESTS_BURST))
	{
		command_net_drop(slot, _command_net_drop_rate);
		return;
	}
	/* (its commander: the machine's player, its side his team's - never the
	request's word) */
	player_index = command_net_machine_player(machine_index);
	side = command_player_side(player_index);
	if (player_index == NONE || side == NONE || request->side != side)
	{
		platform_log("command net: machine %ld's request (%d) for %s refused: its player %s", machine_index, (int)request->op,
			request->side < NUMBER_OF_COMMAND_SIDES ? command_side_names[request->side] : "?",
			player_index == NONE ? "is not in the game" : side == NONE ? "is on no side" : "commands the other side");
		/* (another side claimed: never a commander's view's - counted
		against the machine as a malformed request) */
		command_net_drop(slot, player_index != NONE && side != NONE ? _command_net_drop_malformed : _command_net_drop_refused);
		return;
	}
	command_net.machines[slot].taken++;
	command_event_machine = machine_index;
	switch (request->op)
	{
	case _command_net_request_order:
	{
		real_point3d point;
		long target = NONE;
		unsigned long mask = request->squads;
		short ordered;

		point.x = request->point[0];
		point.y = request->point[1];
		point.z = request->point[2];
		if (!command_net_point_on_map(&point))
		{
			platform_log("command net: machine %ld's order off the map (%.1f %.1f %.1f) refused", machine_index, point.x, point.y,
				point.z);
			command_net_drop(slot, _command_net_drop_refused);
			break;
		}
		/* (its target: a living unit, the netcode's; else a move there) */
		if (request->target != COMMAND_NET_NO_OBJECT && distributed_object_index_valid(request->target) &&
			command_living_unit(request->target))
		{
			target = request->target;
		}
		ordered = command_order_squads(side, mask, &point, NONE, target);
		platform_log("command net: machine %ld (%s) ordered %d squads (mask %lx) to %.2f %.2f %.2f%s", machine_index,
			command_side_names[side], (int)ordered, mask, point.x, point.y, point.z, target != NONE ? ", a unit there" : "");
		if (!ordered)
			command_net_drop(slot, _command_net_drop_refused);
		break;
	}
	case _command_net_request_hold:
	{
		short held = command_hold_squads(side, request->squads);

		platform_log("command net: machine %ld (%s): %d squads hold", machine_index, command_side_names[side], (int)held);
		if (!held)
			command_net_drop(slot, _command_net_drop_refused);
		break;
	}
	case _command_net_request_buy:
		platform_log("command net: machine %ld (%s) buys %s", machine_index, command_side_names[side],
			command_buys[request->row].key);
		command_buy(side, request->row);
		break;
	case _command_net_request_possess:
		if (!command_possess(player_index, &why))
		{
			platform_log("command net: machine %ld: into the Spartan refused: %s", machine_index, why ? why : "?");
			if (why)
				command_event(side, side, _command_event_refused, command_reason_of(why), 0);
		}
		break;
	case _command_net_request_leave:
		if (command_spartans[side].possessor != NONE && command_spartans[side].possessor == command_player_slot(player_index))
			command_unpossess(side, "the client's combo");
		break;
	case _command_net_request_sync:
		if (system_milliseconds() - command_net.machines[slot].sync_time >= COMMAND_NET_SYNC_MILLISECONDS ||
			!command_net.machines[slot].sync_time)
		{
			command_net.machines[slot].sync_time = system_milliseconds();
			command_net.machines[slot].sync_pending = TRUE;
		}
		break;
	}
	command_event_machine = NONE;
}

void command_net_handle_requests(
	long machine_index,
	void const *entries,
	short count)
{
	short slot;
	short index;

	if (count <= 0)
		return;
	/* (a machine of the game that answered its hello alone; none while no
	Command game is played) */
	if (!command_net_host() || !forge_net_machine_plays(machine_index))
		return;
	slot = command_net_machine_slot(machine_index, TRUE);
	if (slot == NONE)
		return;
	for (index = 0; index < count; index++)
	{
		struct command_net_request request;

		csmemcpy(&request, (byte const *)entries + index * sizeof(request), sizeof(request));
		command_net_host_request(slot, &request);
	}
}

word command_net_entry_size(
	byte type)
{
	return type == _distributed_message_command_requests ? (word)sizeof(struct command_net_request) :
		(word)sizeof(struct command_net_state);
}

/* ---------- a client */

static void command_net_request_add(
	struct command_net_request *request)
{
	if (!command_net_client() || !command_globals.game_running)
		return;
	if (command_net.request_count >= COMMAND_NET_REQUESTS_PER_MESSAGE)
		return;
	request->side = (uint8_t)command_globals.side;
	request->sequence = ++command_net.sequence;
	command_net.requests[command_net.request_count++] = *request;
}

static void command_net_request_init(
	struct command_net_request *request,
	short op)
{
	csmemset(request, 0, sizeof(*request));
	request->op = (uint8_t)op;
	request->target = COMMAND_NET_NO_OBJECT;
}

static void command_net_request_order(
	unsigned long mask,
	real_point3d const *point,
	long unit_index)
{
	struct command_net_request request;

	if (!mask)
		return;
	command_net_request_init(&request, _command_net_request_order);
	request.squads = (uint32_t)(mask & ((1UL << COMMAND_MAXIMUM_SQUADS) - 1));
	request.point[0] = point->x;
	request.point[1] = point->y;
	request.point[2] = point->z;
	request.target = unit_index != NONE ? (int32_t)unit_index : COMMAND_NET_NO_OBJECT;
	command_net_request_add(&request);
	platform_log("command net: asked the host to order squads %lx to %.2f %.2f %.2f", mask, point->x, point->y, point->z);
}

static void command_net_request_hold(
	unsigned long mask)
{
	struct command_net_request request;

	if (!mask)
		return;
	command_net_request_init(&request, _command_net_request_hold);
	request.squads = (uint32_t)(mask & ((1UL << COMMAND_MAXIMUM_SQUADS) - 1));
	command_net_request_add(&request);
	platform_log("command net: asked the host to have squads %lx hold", mask);
}

static void command_net_request_buy(
	short row)
{
	struct command_net_request request;

	if (row < 0 || row >= COMMAND_BUYS)
		return;
	command_net_request_init(&request, _command_net_request_buy);
	request.row = (uint16_t)row;
	command_net_request_add(&request);
	platform_log("command net: asked the host to buy %s", command_buys[row].key);
}

static void command_net_request_possess(
	boolean in)
{
	struct command_net_request request;

	command_net_request_init(&request, in ? _command_net_request_possess : _command_net_request_leave);
	command_net_request_add(&request);
	platform_log("command net: asked the host %s", in ? "for the Spartan" : "to leave the Spartan");
}

/* (a client, its game begun) the host's state asked for, its own state
cleared until it comes */
static void command_net_client_begin(
	void)
{
	short side;

	command_net.synced = FALSE;
	command_net.sync_asked = 0;
	command_net.request_count = 0;
	command_territory_count = 0;
	csmemset(command_territories, 0, sizeof(command_territories));
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		command_net.coming[side] = 0;
		command_globals.resources[side] = 0;
		command_ai[side].plays = FALSE;
	}
	command_objectives.over = FALSE;
}

static void command_net_client_frame(
	void)
{
	if (!command_net_client() || !command_globals.game_running)
		return;
	/* (the host's state: asked until it comes) */
	if (!command_net.synced && (!command_net.sync_asked || system_milliseconds() - command_net.sync_asked >= COMMAND_NET_SYNC_MILLISECONDS))
	{
		struct command_net_request request;

		command_net.sync_asked = system_milliseconds();
		command_net_request_init(&request, _command_net_request_sync);
		command_net_request_add(&request);
		platform_log("command net: the host's game asked for");
	}
	if (command_net.request_count > 0)
	{
		struct command_net_requests_message message;
		word size = (word)(sizeof(message.header) + command_net.request_count * sizeof(struct command_net_request));

		csmemcpy(message.requests, command_net.requests, command_net.request_count * sizeof(struct command_net_request));
		distributed_send(&message, _distributed_message_command_requests, command_net.request_count, size,
			_distributed_to_host_reliably);
		command_net.requests_sent += command_net.request_count;
		command_net.request_count = 0;
	}
}

/* (a client) a unit of the host's: one this machine has (its index whole,
the object there), else none */
static long command_net_unit_of(
	int32_t index)
{
	if (index == COMMAND_NET_NO_OBJECT || !distributed_object_index_valid(index))
		return NONE;
	return object_try_and_get_and_verify_type(index, _object_mask_unit) ? (long)index : NONE;
}

static void command_net_take_game(
	struct command_net_state_game const *game)
{
	short side, index;
	long now = game_time_get();

	command_objectives.objectives = game->objectives;
	command_objectives.score_limit = game->score_limit;
	command_objectives.over = (game->flags & COMMAND_NET_GAME_OVER) != 0;
	command_objectives.winner = game->winner;
	/* (the time left, as command_time_left reads it) */
	if (game->seconds_left < 0)
		command_objectives.time_limit_minutes = 0;
	else
	{
		command_objectives.time_limit_minutes = game->seconds_left / 60 + 1;
		command_objectives.start_time = now - (command_objectives.time_limit_minutes * 60 - game->seconds_left) * TICKS_PER_SECOND;
	}
	for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
	{
		long spartan_index = command_net_unit_of(game->spartans[side]);

		command_globals.resources[side] = game->resources[side];
		command_objectives.scores[side] = game->scores[side];
		for (index = 0; index < NUMBER_OF_COMMAND_UPGRADES; index++)
		{
			command_upgrades[side][index] = (game->upgrades[side] >> index) & 1;
			command_upgrade_done_time[side][index] = (game->building[side] >> index) & 1 ?
				now + game->building_seconds[side] * TICKS_PER_SECOND : 0;
		}
		/* (a unit this machine does not have yet: none until it does) */
		command_spartans[side].unit_index = game->spartans[side] == COMMAND_NET_NO_OBJECT ? NONE :
			spartan_index != NONE ? spartan_index : (long)game->spartans[side];
		command_spartans[side].possessor = game->possessors[side] == COMMAND_NET_NO_PLAYER ? NONE : (short)game->possessors[side];
		command_spartan_owned[side] = game->owned[side] != 0;
		command_net.coming[side] = game->coming[side];
		command_ai[side].plays = game->ai[side] != 0;
		command_objectives.flags[side].valid = game->flag_states[side] != _command_net_flag_none;
		command_objectives.flags[side].away = game->flag_states[side] == _command_net_flag_carried ||
			game->flag_states[side] == _command_net_flag_dropped;
		command_objectives.flags[side].weapon_index = game->flag_objects[side] != COMMAND_NET_NO_OBJECT &&
			distributed_object_index_valid(game->flag_objects[side]) ? (long)game->flag_objects[side] : NONE;
		command_objectives.flags[side].carrier_index = NONE;
		if (game->flag_states[side] == _command_net_flag_carried)
		{
			struct object_datum *flag = object_try_and_get(command_objectives.flags[side].weapon_index);

			command_objectives.flags[side].carrier_index = flag && flag->object.parent_object_index != NONE ?
				flag->object.parent_object_index : 0;
		}
	}
	if (command_objectives.over && command_globals.game_running && !command_globals.hq_open)
		command_globals.hq_open = FALSE;
}

static void command_net_take_squad(
	short index,
	struct command_net_state_squad const *entry)
{
	struct command_squad *squad = &command_globals.squads[index];
	boolean was_mine = squad->used && squad->side == command_globals.side;
	short member;

	if (!(entry->flags & COMMAND_NET_SQUAD_USED))
	{
		squad->used = FALSE;
		command_globals.selection &= ~(1UL << index);
		if (command_globals.selected == index)
			command_selection_set(command_globals.selection, "with a squad gone");
		return;
	}
	if (!squad->used || squad->side != entry->side || squad->number != entry->number)
	{
		csmemset(squad, 0, sizeof(*squad));
		command_globals.selection &= ~(1UL << index);
		was_mine = FALSE;
	}
	squad->used = TRUE;
	squad->side = entry->side;
	squad->number = (short)entry->number;
	squad->hero = (entry->flags & COMMAND_NET_SQUAD_HERO) != 0;
	squad->crew = (entry->flags & COMMAND_NET_SQUAD_CREW) != 0;
	squad->airborne = (entry->flags & COMMAND_NET_SQUAD_AIRBORNE) != 0;
	squad->arrived = (entry->flags & COMMAND_NET_SQUAD_ARRIVED) != 0;
	squad->order = entry->order;
	squad->point.x = entry->point[0];
	squad->point.y = entry->point[1];
	squad->point.z = entry->point[2];
	squad->target_index = NONE;
	squad->ai_goal = NONE;
	squad->actor_variant_index = NONE;
	for (member = 0; member < COMMAND_SQUAD_SIZE; member++)
	{
		long unit_index = command_net_unit_of(entry->units[member]);

		/* (a unit this machine does not have yet: named, shown once it is) */
		squad->units[member] = entry->units[member] == COMMAND_NET_NO_OBJECT ? NONE :
			unit_index != NONE ? unit_index : (long)entry->units[member];
		squad->member_variants[member] = NONE;
	}
	csmemcpy(squad->name, entry->name, COMMAND_NET_NAME_SIZE);
	squad->name[COMMAND_NET_NAME_SIZE - 1] = 0;
	/* (the commander's first squad chosen as it comes down) */
	if (!was_mine && squad->side == command_globals.side && command_squad_selected() == NULL && !squad->airborne &&
		command_globals.active)
	{
		command_selection_set(1UL << index, "as it came");
	}
	if (squad->airborne && (command_globals.selection & (1UL << index)))
		command_selection_set(command_globals.selection & ~(1UL << index), "with a squad in the air");
}

static void command_net_take_territory(
	short index,
	struct command_net_state_territory const *entry)
{
	struct command_territory *territory = &command_territories[index];
	short count = 0, at;

	csmemset(territory, 0, sizeof(*territory));
	territory->owner = territory->taker = NONE;
	if (entry->used)
	{
		territory->owner = entry->owner == COMMAND_NET_NO_SIDE ? NONE : entry->owner;
		territory->taker = entry->taker == COMMAND_NET_NO_SIDE ? NONE : entry->taker;
		territory->letter = (char)entry->letter;
		territory->objective = entry->letter != 0;
		territory->center.x = entry->center[0];
		territory->center.y = entry->center[1];
		territory->center.z = entry->center[2];
		territory->radius = entry->radius;
		territory->hill = 1;
	}
	/* (the count: the last used slot's) */
	for (at = 0; at < COMMAND_TERRITORIES; at++)
	{
		if (command_territories[at].hill)
			count = (short)(at + 1);
	}
	command_territory_count = count;
}

void command_net_handle_state(
	void const *entries,
	short count)
{
	short index;

	if (count <= 0 || !command_net_client() || !command_globals.game_running)
		return;
	for (index = 0; index < count; index++)
	{
		struct command_net_state state;

		csmemcpy(&state, (byte const *)entries + index * sizeof(state), sizeof(state));
		if (!command_net_state_valid(&state, COMMAND_BUYS, NUMBER_OF_COMMAND_EVENTS))
		{
			command_net.states_dropped++;
			if (command_net.states_dropped == 1 || !(command_net.states_dropped % 100))
				platform_log("command net: %ld of the host's state entries dropped (malformed)", command_net.states_dropped);
			continue;
		}
		command_net.states_taken++;
		switch (state.kind)
		{
		case _command_net_state_game:
			command_net_take_game(&state.u.game);
			if (!command_net.synced)
				platform_log("command net: the host's game had");
			command_net.synced = TRUE;
			break;
		case _command_net_state_squad:
			command_net_take_squad(state.index, &state.u.squad);
			break;
		case _command_net_state_territory:
			command_net_take_territory(state.index, &state.u.territory);
			break;
		case _command_net_state_tally:
		{
			short side;

			for (side = 0; side < NUMBER_OF_COMMAND_SIDES; side++)
			{
				command_objectives.captures[side] = state.u.tally.captures[side];
				command_objectives.territory_seconds[side] = state.u.tally.territory_seconds[side];
				command_objectives.kills[side] = state.u.tally.kills[side];
				command_objectives.losses[side] = state.u.tally.losses[side];
			}
			command_objectives.spartan_kills = state.u.tally.spartan_kills;
			command_objectives.spartan_captures = state.u.tally.spartan_captures;
			break;
		}
		case _command_net_state_event:
		{
			short audience = state.u.event.audience == COMMAND_NET_AUDIENCE_EVERYONE ? COMMAND_EVENT_EVERYONE :
				state.u.event.audience;
			short subject = state.u.event.subject == COMMAND_NET_NO_SIDE ? NONE : state.u.event.subject;
			char text[COMMAND_LINE_SIZE];

			if (command_event_text(subject, state.u.event.code, state.u.event.a, state.u.event.b, text, sizeof(text)))
				platform_log("command net: the host says: %s", text);
			command_event_show(audience, subject, state.u.event.code, state.u.event.a, state.u.event.b);
			break;
		}
		}
	}
}

boolean command_net_unit_kept(
	long unit_index)
{
	(void)unit_index;
	return command_globals.game_running && command_net_client();
}

/* (a client) a side's squads bought that have not left yet, as the host
says */
static short command_net_coming(
	short side)
{
	return side >= 0 && side < NUMBER_OF_COMMAND_SIDES ? command_net.coming[side] : 0;
}

/* ---------- tests */

/* (a client, the tests' "command net junk") a volley of requests the host
must drop: malformed (an unknown operation, squads past the table, a point
not a number or off the map, a row past the HQ's, the reserved fields set),
another side's, replayed, and too many at once (the rate) */
static void command_net_junk_send(
	struct command_net_request const *requests,
	short count)
{
	struct command_net_requests_message message;
	short at = 0;

	while (at < count)
	{
		short chunk = (short)MIN(count - at, COMMAND_NET_REQUESTS_PER_MESSAGE);
		word size = (word)(sizeof(message.header) + chunk * sizeof(struct command_net_request));

		csmemcpy(message.requests, requests + at, chunk * sizeof(struct command_net_request));
		distributed_send(&message, _distributed_message_command_requests, chunk, size, _distributed_to_host_reliably);
		at = (short)(at + chunk);
	}
}

static boolean command_net_command(
	char const *text)
{
	if (!strncmp(text, "junk", 4))
	{
		struct command_net_request junk[96];
		short count = 0, index;
		unsigned int nan_bits = 0x7FC00000u;
		float nan_value;

		if (!command_net_client() || !command_globals.game_running)
			return FALSE;
		csmemcpy(&nan_value, &nan_bits, sizeof(nan_value));
		/* (malformed) */
		command_net_request_init(&junk[count], 99);
		count++;
		command_net_request_init(&junk[count], _command_net_request_hold);
		junk[count++].squads = 0xFF000000u;
		command_net_request_init(&junk[count], _command_net_request_order);
		junk[count].squads = 1;
		junk[count].point[0] = nan_value;
		count++;
		command_net_request_init(&junk[count], _command_net_request_order);
		junk[count].squads = 1;
		junk[count].point[0] = 1.0e9f;
		count++;
		command_net_request_init(&junk[count], _command_net_request_buy);
		junk[count++].row = 200;
		command_net_request_init(&junk[count], _command_net_request_possess);
		junk[count++].reserved1 = 7;
		command_net_request_init(&junk[count], _command_net_request_buy);
		junk[count++].side = 9;
		for (index = 0; index < count; index++)
		{
			if (junk[index].side != 9)
				junk[index].side = (uint8_t)command_globals.side;
			junk[index].sequence = ++command_net.sequence;
		}
		/* (another side's: a buy, an order of the other side's squads) */
		command_net_request_init(&junk[count], _command_net_request_buy);
		junk[count].side = (uint8_t)!command_globals.side;
		junk[count++].sequence = ++command_net.sequence;
		/* (off the map, a point it may not order to) */
		command_net_request_init(&junk[count], _command_net_request_order);
		junk[count].side = (uint8_t)command_globals.side;
		junk[count].squads = 0xFFFFFF;
		junk[count].point[0] = 4000.f;
		junk[count].point[1] = -4000.f;
		junk[count].sequence = ++command_net.sequence;
		count++;
		/* (replayed: the last sequence again, an older one) */
		command_net_request_init(&junk[count], _command_net_request_sync);
		junk[count].side = (uint8_t)command_globals.side;
		junk[count++].sequence = command_net.sequence;
		command_net_request_init(&junk[count], _command_net_request_sync);
		junk[count].side = (uint8_t)command_globals.side;
		junk[count++].sequence = (uint16_t)(command_net.sequence - 5);
		/* (the rate: a burst far past it) */
		while (count < (short)NUMBEROF(junk))
		{
			command_net_request_init(&junk[count], _command_net_request_hold);
			junk[count].side = (uint8_t)command_globals.side;
			junk[count].squads = 1;
			junk[count++].sequence = ++command_net.sequence;
		}
		command_net_junk_send(junk, count);
		platform_log("command net: junk: %d bad requests sent to the host", (int)count);
		return TRUE;
	}
	platform_log("command net: %s, %s; requests sent %ld, state entries taken %ld, dropped %ld; red %s, blue %s; "
		"Spartans %lx (in it %d) %lx (in it %d)",
		command_client() ? "a client" : "the host", command_net.synced || !command_client() ? "synced" : "not synced",
		command_net.requests_sent, command_net.states_taken, command_net.states_dropped, command_ai[0].plays ? "AI" : "human",
		command_ai[1].plays ? "AI" : "human", (unsigned long)command_spartans[0].unit_index, (int)command_spartans[0].possessor,
		(unsigned long)command_spartans[1].unit_index, (int)command_spartans[1].possessor);
	return TRUE;
}

/* ---------- each frame */

static void command_net_frame(
	void)
{
	if (command_client())
		command_net_client_frame();
	else
		command_net_host_frame();
}

/* (the host's game begun) nothing sent yet: every client given the whole */
static void command_net_host_begin(
	void)
{
	csmemset(&command_net, 0, sizeof(command_net));
	platform_log("command net: hosting the Command game (red %s, blue %s)", command_ai[0].plays ? "the AI's" : "human",
		command_ai[1].plays ? "the AI's" : "human");
}
