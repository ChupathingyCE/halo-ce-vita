/*
NET_FUZZ_COMMAND.C

A fuzz target for network Command's messages (port/linux/game/
command_net_protocol.c): a client's requests as the host takes them, the
host's state entries (the game, the squads, the territories, the notices)
as a client takes them, the replay window and the rate limit. The game's
side (command.c's command_net.inc) acts only on what these let through, and
then checks what only the game knows (the side is the sender's player's
team, the squads that side's, the point on the map, the target a living
unit); the netns cases (run_netns_online_test.sh commandfuzz) send the host
junk of every kind.

net_fuzz_checks: known good and bad messages of each kind, and the
invariants of what passes (a taken request's point finite and in the
world, its squads within the table, its row one the HQ has; a taken
entry's slot, sides, numbers and name in range; a sequence never taken
twice; a bucket never below zero).

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a request (struct command_net_request), as the host takes it (the
     HQ's rows: 12, the game's count)
  1  a state entry (struct command_net_state), as a client takes it
  2  a sequence number (2 bytes) to the replay window
  3  a bucket's take: the time (4 bytes)
Built as net_fuzz_forge.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/command_net_protocol.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's (command.c's COMMAND_BUYS, NUMBER_OF_COMMAND_EVENTS) */
#define FUZZ_ROWS 12
#define FUZZ_EVENTS 17

static int fuzz_point_sane(
	float const point[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (point[axis] != point[axis] || point[axis] < -COMMAND_NET_MAXIMUM_COORDINATE ||
			point[axis] > COMMAND_NET_MAXIMUM_COORDINATE)
		{
			return 0;
		}
	}
	return 1;
}

/* what a taken request may hold */
static void fuzz_request(
	uint8_t const *data,
	size_t size)
{
	struct command_net_request request;

	if (size < sizeof(request))
		return;
	memcpy(&request, data, sizeof(request));
	if (!command_net_request_valid(&request, FUZZ_ROWS))
		return;
	if (request.op == _command_net_request_none || request.op >= NUMBER_OF_COMMAND_NET_REQUESTS || request.side >= COMMAND_NET_SIDES ||
		(request.squads >> COMMAND_NET_MAXIMUM_SQUADS) || request.row >= FUZZ_ROWS || request.reserved0 || request.reserved1)
	{
		abort();
	}
	if (request.op == _command_net_request_order && (!request.squads || !fuzz_point_sane(request.point)))
		abort();
	if (request.op != _command_net_request_order && (request.point[0] != 0.f || request.point[1] != 0.f || request.point[2] != 0.f))
		abort();
	if (request.op != _command_net_request_buy && request.row)
		abort();
}

/* what a taken state entry may hold */
static void fuzz_state(
	uint8_t const *data,
	size_t size)
{
	struct command_net_state state;
	int index;

	if (size < sizeof(state))
		return;
	memcpy(&state, data, sizeof(state));
	if (!command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS))
		return;
	switch (state.kind)
	{
	case _command_net_state_game:
		for (index = 0; index < COMMAND_NET_SIDES; index++)
		{
			if (state.u.game.possessors[index] >= COMMAND_NET_MAXIMUM_PLAYERS && state.u.game.possessors[index] != COMMAND_NET_NO_PLAYER)
				abort();
			if (state.u.game.flag_states[index] >= NUMBER_OF_COMMAND_NET_FLAG_STATES)
				abort();
		}
		if (state.u.game.winner > COMMAND_NET_SIDES)
			abort();
		break;
	case _command_net_state_squad:
		if (state.index >= COMMAND_NET_MAXIMUM_SQUADS)
			abort();
		if (state.u.squad.flags & COMMAND_NET_SQUAD_USED)
		{
			if (state.u.squad.side >= COMMAND_NET_SIDES || state.u.squad.order >= COMMAND_NET_ORDERS ||
				!fuzz_point_sane(state.u.squad.point) || !memchr(state.u.squad.name, 0, COMMAND_NET_NAME_SIZE))
			{
				abort();
			}
			/* (the name as the HUD draws it: printable, ended) */
			for (index = 0; state.u.squad.name[index]; index++)
			{
				if ((unsigned char)state.u.squad.name[index] < 0x20 || (unsigned char)state.u.squad.name[index] > 0x7E)
					abort();
			}
		}
		break;
	case _command_net_state_territory:
		if (state.index >= COMMAND_NET_MAXIMUM_TERRITORIES)
			abort();
		if (state.u.territory.used && (!fuzz_point_sane(state.u.territory.center) ||
				!(state.u.territory.radius >= 0.f && state.u.territory.radius <= 100.f)))
		{
			abort();
		}
		break;
	case _command_net_state_tally:
		for (index = 0; index < COMMAND_NET_SIDES; index++)
		{
			if (state.u.tally.kills[index] < 0 || state.u.tally.losses[index] > COMMAND_NET_MAXIMUM_TALLY ||
				state.u.tally.captures[index] < 0 || state.u.tally.territory_seconds[index] < 0)
			{
				abort();
			}
		}
		break;
	case _command_net_state_event:
		if (state.u.event.code >= FUZZ_EVENTS ||
			(state.u.event.audience >= COMMAND_NET_SIDES && state.u.event.audience != COMMAND_NET_AUDIENCE_EVERYONE))
		{
			abort();
		}
		break;
	default:
		abort();
	}
}

static struct command_net_sequence fuzz_sequence;
static struct command_net_bucket fuzz_bucket;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t at = 0;
	uint16_t last = 0;
	int have_last = 0;

	memset(&fuzz_sequence, 0, sizeof(fuzz_sequence));
	memset(&fuzz_bucket, 0, sizeof(fuzz_bucket));
	while (at + 3 <= size)
	{
		uint8_t kind = data[at];
		size_t length = (size_t)data[at + 1] | ((size_t)data[at + 2] << 8);

		at += 3;
		if (length > size - at)
			length = size - at;
		switch (kind % 4)
		{
		case 0:
			fuzz_request(data + at, length);
			break;
		case 1:
			fuzz_state(data + at, length);
			break;
		case 2:
			if (length >= 2)
			{
				uint16_t value = (uint16_t)(data[at] | (data[at + 1] << 8));

				if (command_net_sequence_take(&fuzz_sequence, value))
				{
					/* (never the same twice running, never behind) */
					if (have_last && (value == last || (uint16_t)(value - last) >= 0x8000))
						abort();
					last = value;
					have_last = 1;
				}
			}
			break;
		case 3:
			if (length >= 4)
			{
				uint32_t time = (uint32_t)data[at] | ((uint32_t)data[at + 1] << 8) | ((uint32_t)data[at + 2] << 16) |
					((uint32_t)data[at + 3] << 24);

				command_net_bucket_take(&fuzz_bucket, time, COMMAND_NET_REQUESTS_PER_SECOND, COMMAND_NET_REQUESTS_BURST);
				if (fuzz_bucket.tokens < 0.f || fuzz_bucket.tokens > COMMAND_NET_REQUESTS_BURST)
					abort();
			}
			break;
		}
		at += length;
	}
	return 0;
}

/* ---------- the checks */

void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);

static int failures;

static void check(
	int condition,
	char const *what)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL command: %s\n", what);
		failures++;
	}
}

static void request_init(
	struct command_net_request *request,
	uint8_t op)
{
	memset(request, 0, sizeof(*request));
	request->op = op;
	request->target = COMMAND_NET_NO_OBJECT;
}

static void state_squad(
	struct command_net_state *state)
{
	memset(state, 0, sizeof(*state));
	state->kind = _command_net_state_squad;
	state->index = 3;
	state->u.squad.flags = COMMAND_NET_SQUAD_USED;
	state->u.squad.side = 1;
	state->u.squad.order = 1;
	state->u.squad.number = 7;
	state->u.squad.units[0] = (int32_t)0xE2CA0035u;
	state->u.squad.units[1] = COMMAND_NET_NO_OBJECT;
	state->u.squad.units[2] = COMMAND_NET_NO_OBJECT;
	state->u.squad.units[3] = COMMAND_NET_NO_OBJECT;
	state->u.squad.point[0] = 60.f;
	state->u.squad.point[1] = -100.f;
	strcpy(state->u.squad.name, "Marines");
}

static void state_game(
	struct command_net_state *state)
{
	memset(state, 0, sizeof(*state));
	state->kind = _command_net_state_game;
	state->u.game.version = COMMAND_NET_VERSION;
	state->u.game.flags = COMMAND_NET_GAME_RUNNING;
	state->u.game.objectives = 3;
	state->u.game.score_limit = 1000;
	state->u.game.seconds_left = 1200;
	state->u.game.resources[0] = 250;
	state->u.game.resources[1] = 250;
	state->u.game.spartans[0] = COMMAND_NET_NO_OBJECT;
	/* (a datum's index looks negative as a 32-bit int: its identifier's high
	bit; a client must take it) */
	state->u.game.spartans[1] = (int32_t)0xE302003Au;
	state->u.game.possessors[0] = COMMAND_NET_NO_PLAYER;
	state->u.game.possessors[1] = 1;
	state->u.game.flag_objects[0] = (int32_t)0xE2CA0035u;
	state->u.game.flag_objects[1] = COMMAND_NET_NO_OBJECT;
	state->u.game.flag_states[0] = _command_net_flag_home;
	state->u.game.flag_states[1] = _command_net_flag_carried;
}

int net_fuzz_checks(void)
{
	struct command_net_request request, copy;
	struct command_net_state state, bad;
	unsigned int nan_bits = 0x7FC00000u, infinity_bits = 0x7F800000u;
	float nan_value, infinity_value;
	int index;
	char const *seeds = getenv("NET_FUZZ_WRITE_SEEDS");

	memcpy(&nan_value, &nan_bits, sizeof(nan_value));
	memcpy(&infinity_value, &infinity_bits, sizeof(infinity_value));
	failures = 0;
	/* requests */
	request_init(&request, _command_net_request_order);
	request.squads = 0x5;
	request.point[0] = 60.f;
	request.point[1] = -100.f;
	request.point[2] = 1.f;
	check(command_net_request_valid(&request, FUZZ_ROWS), "an order is taken");
	copy = request;
	copy.target = (int32_t)0xE2CA0035u;
	check(command_net_request_valid(&copy, FUZZ_ROWS), "an order at a unit (a datum index, high bit set) is taken");
	copy.target = 0x35;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order at an index with no identifier is refused");
	copy = request;
	copy.squads = 0;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order of no squad is refused");
	copy.squads = 1u << COMMAND_NET_MAXIMUM_SQUADS;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order of a squad past the table is refused");
	copy = request;
	copy.point[1] = nan_value;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order at a NaN is refused");
	copy.point[1] = infinity_value;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order at an infinity is refused");
	copy.point[1] = 6000.f;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an order out of the world is refused");
	copy = request;
	copy.side = 2;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a side past red and blue is refused");
	copy = request;
	copy.reserved1 = 1;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a reserved field set is refused");
	copy = request;
	copy.op = 99;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "an unknown operation is refused");
	copy.op = _command_net_request_none;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "no operation is refused");
	request_init(&copy, _command_net_request_buy);
	copy.row = FUZZ_ROWS - 1;
	check(command_net_request_valid(&copy, FUZZ_ROWS), "the HQ's last row is bought");
	copy.row = FUZZ_ROWS;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a row past the HQ's is refused");
	copy.row = 0;
	copy.squads = 1;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a buy with squads is refused");
	request_init(&copy, _command_net_request_hold);
	copy.squads = 1;
	check(command_net_request_valid(&copy, FUZZ_ROWS), "a hold is taken");
	copy.point[0] = 1.f;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a hold with a point is refused");
	request_init(&copy, _command_net_request_possess);
	check(command_net_request_valid(&copy, FUZZ_ROWS), "a possess is taken");
	copy.target = 5;
	check(!command_net_request_valid(&copy, FUZZ_ROWS), "a possess naming an object is refused");
	request_init(&copy, _command_net_request_sync);
	check(command_net_request_valid(&copy, FUZZ_ROWS), "a sync is taken");
	check(!command_net_request_valid(&copy, COMMAND_NET_MAXIMUM_ROWS + 1), "rows past the most are refused");
	/* state entries */
	state_game(&state);
	check(command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS), "the game is taken");
	bad = state;
	bad.u.game.version = 2;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "another version's game is refused");
	bad = state;
	bad.u.game.possessors[0] = 200;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a possessor past the players is refused");
	bad = state;
	bad.u.game.spartans[0] = 12;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a Spartan with no identifier is refused");
	bad = state;
	bad.u.game.resources[1] = 2000000;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "resources past the most are refused");
	bad = state;
	bad.u.game.flag_states[1] = 9;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unknown flag state is refused");
	bad = state;
	bad.u.game.upgrades[0] = 0x80;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an upgrade past the HQ's is refused");
	bad = state;
	bad.reserved = 1;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a reserved field set is refused");
	if (seeds)
		net_fuzz_write_seed(seeds, "state-game", &state, sizeof(state));
	state_squad(&state);
	check(command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS), "a squad is taken");
	bad = state;
	bad.index = COMMAND_NET_MAXIMUM_SQUADS;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a squad past the table is refused");
	bad = state;
	memset(bad.u.squad.name, 'A', sizeof(bad.u.squad.name));
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a squad's name not ended is refused");
	bad = state;
	bad.u.squad.name[2] = 0x07;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a squad's name with a control character is refused");
	bad = state;
	bad.u.squad.side = 2;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a squad of no side is refused");
	bad = state;
	bad.u.squad.order = COMMAND_NET_ORDERS;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unknown order is refused");
	bad = state;
	bad.u.squad.point[2] = nan_value;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a squad's NaN point is refused");
	bad = state;
	bad.u.squad.flags = 0;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unused squad slot with anything in it is refused");
	memset(&bad.u, 0, sizeof(bad.u));
	check(command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unused squad slot is taken");
	if (seeds)
		net_fuzz_write_seed(seeds, "state-squad", &state, sizeof(state));
	memset(&state, 0, sizeof(state));
	state.kind = _command_net_state_territory;
	state.index = 11;
	state.u.territory.used = 1;
	state.u.territory.owner = COMMAND_NET_NO_SIDE;
	state.u.territory.taker = 0;
	state.u.territory.letter = 'B';
	state.u.territory.center[0] = 65.5f;
	state.u.territory.radius = 7.f;
	check(command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS), "a territory is taken");
	bad = state;
	bad.index = COMMAND_NET_MAXIMUM_TERRITORIES;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a territory past the table is refused");
	bad = state;
	bad.u.territory.radius = -1.f;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a negative radius is refused");
	bad = state;
	bad.u.territory.letter = '!';
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a letter not a letter is refused");
	memset(&state, 0, sizeof(state));
	state.kind = _command_net_state_event;
	state.u.event.code = FUZZ_EVENTS - 1;
	state.u.event.subject = 1;
	state.u.event.audience = COMMAND_NET_AUDIENCE_EVERYONE;
	state.u.event.a = 3;
	check(command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS), "an event is taken");
	bad = state;
	bad.u.event.code = FUZZ_EVENTS;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unknown event is refused");
	bad = state;
	bad.u.event.a = 0x10000;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an event's number past 16 bits is refused");
	bad = state;
	bad.u.event.audience = 5;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an event's audience of no side is refused");
	bad = state;
	bad.kind = NUMBER_OF_COMMAND_NET_STATES;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "an unknown entry is refused");
	memset(&state, 0, sizeof(state));
	state.kind = _command_net_state_tally;
	state.u.tally.kills[0] = 12;
	state.u.tally.captures[1] = 2;
	state.u.tally.spartan_kills = 3;
	check(command_net_state_valid(&state, FUZZ_ROWS, FUZZ_EVENTS), "a tally is taken");
	bad = state;
	bad.u.tally.losses[1] = -1;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a negative count is refused");
	bad = state;
	bad.u.tally.spartan_captures = COMMAND_NET_MAXIMUM_TALLY + 1;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a count past the most is refused");
	bad = state;
	bad.index = 1;
	check(!command_net_state_valid(&bad, FUZZ_ROWS, FUZZ_EVENTS), "a tally with a slot is refused");
	/* the replay window and the rate */
	{
		struct command_net_sequence sequence;
		struct command_net_bucket bucket;
		int taken = 0;

		memset(&sequence, 0, sizeof(sequence));
		check(command_net_sequence_take(&sequence, 0xFFFE), "the first sequence is taken");
		check(command_net_sequence_take(&sequence, 0xFFFF), "the next is taken");
		check(command_net_sequence_take(&sequence, 2), "a sequence past the wrap is taken");
		check(!command_net_sequence_take(&sequence, 2), "a sequence replayed is refused");
		check(!command_net_sequence_take(&sequence, 0xFFFF), "an older sequence is refused");
		memset(&bucket, 0, sizeof(bucket));
		for (index = 0; index < 200; index++)
			taken += command_net_bucket_take(&bucket, 5000, COMMAND_NET_REQUESTS_PER_SECOND, COMMAND_NET_REQUESTS_BURST);
		check(taken == (int)COMMAND_NET_REQUESTS_BURST, "a burst takes the bucket's burst alone");
		check(command_net_bucket_take(&bucket, 5100, COMMAND_NET_REQUESTS_PER_SECOND, COMMAND_NET_REQUESTS_BURST),
			"the bucket fills again with time");
	}
	/* (the seeds: a list of every record's kind) */
	if (seeds)
	{
		uint8_t buffer[512];
		size_t length = 0;

		request_init(&request, _command_net_request_order);
		request.squads = 3;
		request.point[0] = 70.f;
		buffer[length++] = 0;
		buffer[length++] = sizeof(request);
		buffer[length++] = 0;
		memcpy(buffer + length, &request, sizeof(request));
		length += sizeof(request);
		state_squad(&state);
		buffer[length++] = 1;
		buffer[length++] = sizeof(state);
		buffer[length++] = 0;
		memcpy(buffer + length, &state, sizeof(state));
		length += sizeof(state);
		buffer[length++] = 2;
		buffer[length++] = 2;
		buffer[length++] = 0;
		buffer[length++] = 0x10;
		buffer[length++] = 0x00;
		buffer[length++] = 3;
		buffer[length++] = 4;
		buffer[length++] = 0;
		buffer[length++] = 0x88;
		buffer[length++] = 0x13;
		buffer[length++] = 0;
		buffer[length++] = 0;
		net_fuzz_write_seed(seeds, "seed-messages", buffer, length);
	}
	if (!failures)
		printf("command: every check passed\n");
	return failures;
}
