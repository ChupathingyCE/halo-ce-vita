/*
COMMAND_NET_PROTOCOL.C

The checks of network Command's messages (command_net_protocol.h), with no
part of the game: the host's of a client's request, a client's of the
host's state, the requests' sequence and rate. Tested and fuzzed by
port/vita/tests/command_net_test.c.
*/

#include "command_net_protocol.h"

#include <math.h>
#include <string.h>

int command_net_real_valid(
	float value)
{
	/* (finite: a NaN is not itself, an infinity less itself is a NaN) */
	volatile float difference = value - value;

	return value == value && difference == 0.f;
}

int command_net_point_valid(
	float const point[3])
{
	int index;

	for (index = 0; index < 3; index++)
	{
		if (!command_net_real_valid(point[index]) || (point[index] > COMMAND_NET_MAXIMUM_COORDINATE || point[index] < -COMMAND_NET_MAXIMUM_COORDINATE))
			return 0;
	}
	return 1;
}

static int command_net_zero(
	void const *bytes,
	unsigned size)
{
	unsigned char const *at = (unsigned char const *)bytes;
	unsigned index;

	for (index = 0; index < size; index++)
	{
		if (at[index])
			return 0;
	}
	return 1;
}

/* an object's index from the other machine: none, or a datum's (its identifier never
0: the client checks it is an object it has) */
static int command_net_object_valid(
	int32_t object_index)
{
	return object_index == COMMAND_NET_NO_OBJECT || ((uint32_t)object_index >> 16) != 0;
}

int command_net_request_valid(
	struct command_net_request const *request,
	int rows)
{
	static float const zero_point[3] = { 0.f, 0.f, 0.f };
	uint32_t squads_known = (uint32_t)((1ULL << COMMAND_NET_MAXIMUM_SQUADS) - 1);

	if (!request || request->op == _command_net_request_none || request->op >= NUMBER_OF_COMMAND_NET_REQUESTS ||
		request->side >= COMMAND_NET_SIDES || request->reserved0 || request->reserved1)
	{
		return 0;
	}
	if (rows < 0 || rows > COMMAND_NET_MAXIMUM_ROWS)
		return 0;
	switch (request->op)
	{
	case _command_net_request_order:
		if (!request->squads || (request->squads & ~squads_known) || !command_net_point_valid(request->point) ||
			request->row || !command_net_object_valid(request->target))
		{
			return 0;
		}
		return 1;
	case _command_net_request_hold:
		return request->squads && !(request->squads & ~squads_known) && !memcmp(request->point, zero_point, sizeof(zero_point)) &&
			request->target == COMMAND_NET_NO_OBJECT && !request->row;
	case _command_net_request_buy:
		return !request->squads && !memcmp(request->point, zero_point, sizeof(zero_point)) &&
			request->target == COMMAND_NET_NO_OBJECT && request->row < (unsigned)rows;
	case _command_net_request_possess:
	case _command_net_request_leave:
	case _command_net_request_sync:
		return !request->squads && !memcmp(request->point, zero_point, sizeof(zero_point)) &&
			request->target == COMMAND_NET_NO_OBJECT && !request->row;
	default:
		return 0;
	}
}

/* a side's byte: a side, or none if allowed */
static int command_net_side_valid(
	uint8_t side,
	int none_allowed)
{
	return side < COMMAND_NET_SIDES || (none_allowed && side == COMMAND_NET_NO_SIDE);
}

int command_net_state_valid(
	struct command_net_state const *state,
	int rows,
	int events)
{
	int index;

	if (!state || state->reserved || rows < 0 || rows > COMMAND_NET_MAXIMUM_ROWS || events < 0)
		return 0;
	switch (state->kind)
	{
	case _command_net_state_game:
	{
		struct command_net_state_game const *game = &state->u.game;

		if (state->index || (game->flags & ~COMMAND_NET_GAME_FLAGS_KNOWN) || game->objectives > 3 ||
			game->winner > COMMAND_NET_SIDES || game->version != COMMAND_NET_VERSION ||
			game->score_limit < 0 || game->score_limit > COMMAND_NET_MAXIMUM_SCORE ||
			game->seconds_left < -1 || game->seconds_left > COMMAND_NET_MAXIMUM_SECONDS)
		{
			return 0;
		}
		for (index = 0; index < COMMAND_NET_SIDES; index++)
		{
			if (game->resources[index] < -COMMAND_NET_MAXIMUM_RESOURCES || game->resources[index] > COMMAND_NET_MAXIMUM_RESOURCES ||
				game->scores[index] < 0 || game->scores[index] > COMMAND_NET_MAXIMUM_SCORE ||
				(game->upgrades[index] >> COMMAND_NET_UPGRADES) || (game->building[index] >> COMMAND_NET_UPGRADES) ||
				!command_net_object_valid(game->spartans[index]) || !command_net_object_valid(game->flag_objects[index]) ||
				(game->possessors[index] >= COMMAND_NET_MAXIMUM_PLAYERS && game->possessors[index] != COMMAND_NET_NO_PLAYER) ||
				game->owned[index] > 1 || game->ai[index] > 1 || game->coming[index] > 8 ||
				game->flag_states[index] >= NUMBER_OF_COMMAND_NET_FLAG_STATES)
			{
				return 0;
			}
		}
		return 1;
	}
	case _command_net_state_squad:
	{
		struct command_net_state_squad const *squad = &state->u.squad;

		if (state->index >= COMMAND_NET_MAXIMUM_SQUADS || (squad->flags & ~COMMAND_NET_SQUAD_FLAGS_KNOWN) || squad->reserved ||
			squad->reserved2)
		{
			return 0;
		}
		/* (an unused slot: nothing else in it) */
		if (!(squad->flags & COMMAND_NET_SQUAD_USED))
		{
			return command_net_zero(&squad->side, sizeof(*squad) - sizeof(squad->flags));
		}
		if (!command_net_side_valid(squad->side, 0) || squad->order >= COMMAND_NET_ORDERS ||
			squad->number > COMMAND_NET_MAXIMUM_SQUAD_NUMBER || !command_net_point_valid(squad->point) ||
			memchr(squad->name, 0, sizeof(squad->name)) == NULL)
		{
			return 0;
		}
		for (index = 0; index < COMMAND_NET_SQUAD_SIZE; index++)
		{
			if (!command_net_object_valid(squad->units[index]))
				return 0;
		}
		/* (a name's characters printable: the HUD draws them) */
		for (index = 0; index < COMMAND_NET_NAME_SIZE && squad->name[index]; index++)
		{
			if ((unsigned char)squad->name[index] < 0x20 || (unsigned char)squad->name[index] > 0x7E)
				return 0;
		}
		return 1;
	}
	case _command_net_state_territory:
	{
		struct command_net_state_territory const *territory = &state->u.territory;

		if (state->index >= COMMAND_NET_MAXIMUM_TERRITORIES || territory->used > 1 ||
			!command_net_zero(territory->reserved, sizeof(territory->reserved)))
		{
			return 0;
		}
		if (!territory->used)
			return command_net_zero(&state->u.territory, sizeof(*territory));
		if (!command_net_side_valid(territory->owner, 1) || !command_net_side_valid(territory->taker, 1) ||
			(territory->letter && (territory->letter < 'A' || territory->letter > 'Z')) ||
			!command_net_point_valid(territory->center) || !command_net_real_valid(territory->radius) ||
			territory->radius < 0.f || territory->radius > 100.f)
		{
			return 0;
		}
		return 1;
	}
	case _command_net_state_event:
	{
		struct command_net_state_event const *event = &state->u.event;

		if (state->index || event->code >= events || !command_net_side_valid(event->subject, 1) ||
			(event->audience >= COMMAND_NET_SIDES && event->audience != COMMAND_NET_AUDIENCE_EVERYONE) || event->reserved ||
			!command_net_zero(event->reserved2, sizeof(event->reserved2)) || event->a < -1 || event->a > 0xFFFF ||
			event->b < -1 || event->b > 0xFFFF)
		{
			return 0;
		}
		return 1;
	}
	case _command_net_state_tally:
	{
		struct command_net_state_tally const *tally = &state->u.tally;
		int32_t const *counts = &tally->captures[0];

		if (state->index || !command_net_zero(tally->reserved, sizeof(tally->reserved)))
			return 0;
		/* (every count: the eight sides' and the Spartans' two) */
		for (index = 0; index < 4 * COMMAND_NET_SIDES + 2; index++)
		{
			if (counts[index] < 0 || counts[index] > COMMAND_NET_MAXIMUM_TALLY)
				return 0;
		}
		return 1;
	}
	default:
		return 0;
	}
}

int command_net_sequence_take(
	struct command_net_sequence *sequence,
	uint16_t value)
{
	if (!sequence->started)
	{
		sequence->started = 1;
		sequence->last = value;
		return 1;
	}
	/* (newer: within half the space ahead) */
	if ((uint16_t)(value - sequence->last) == 0 || (uint16_t)(value - sequence->last) >= 0x8000)
		return 0;
	sequence->last = value;
	return 1;
}

int command_net_bucket_take(
	struct command_net_bucket *bucket,
	uint32_t now,
	float rate,
	float burst)
{
	if (!bucket->started)
	{
		bucket->started = 1;
		bucket->tokens = burst;
		bucket->time = now;
	}
	else
	{
		uint32_t elapsed = now - bucket->time;

		/* (a clock that went back, or a long time: full) */
		if (elapsed > 60000)
			bucket->tokens = burst;
		else
			bucket->tokens += rate * (float)elapsed / 1000.f;
		if (bucket->tokens > burst)
			bucket->tokens = burst;
		bucket->time = now;
	}
	if (bucket->tokens < 1.f)
		return 0;
	bucket->tokens -= 1.f;
	return 1;
}
