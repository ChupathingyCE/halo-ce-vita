/*
COMMAND_NET_PROTOCOL.H

The rules of network Command (command.c's command_net.inc): a Command game
played by several machines, the host authoritative. What can be checked
without the game is here, so that it is tested and fuzzed on its own
(port/vita/tests/command_net_test.c): the messages' layouts, every field's
checks, the requests' sequence and rate.

A network Command game is a network Forge game played, not edited (forge_net.inc:
its hello's FORGE_NET_SESSION_PLAY), whose hello says FORGE_NET_SESSION_COMMAND:
a build that does not play network Command drops the hello (an unknown bit) and
is refused from the game, as one that does not play network Forge; no network
version changes, and never on the crossplay wire. In the game, the host runs
everything - the AI, both AI commanders, the economy, the objectives - and each
client:

  client                                  host
  request (order, hold, buy,         ->
  possess, leave, sync)
                                     <-   state entries (the game, the squads,
                                          the territories: each as it changes,
                                          all of them to a client that asks),
                                          events (the notices)

Requests travel over the client's reliable stream alone (a datagram claiming
to be a client's is never one). The host checks each: its operation and
fields (here), its sequence (a record replayed or older is dropped), its rate
(a bucket a machine), then in the game: the side is the sender's player's
team on the host (a request claiming another side is dropped), the squads
are that side's, the point inside the map, the target a living unit, the
row one the HQ sells.
*/

#ifndef __COMMAND_NET_PROTOCOL_H
#define __COMMAND_NET_PROTOCOL_H

#include <stdint.h>

/* ---------- constants */

/* the protocol's version (the state's game entry says it) */
#define COMMAND_NET_VERSION 1

/* the sides, the squads, the territories, the HQ's rows (command.c's) */
#define COMMAND_NET_SIDES 2
#define COMMAND_NET_MAXIMUM_SQUADS 24
#define COMMAND_NET_SQUAD_SIZE 4
#define COMMAND_NET_MAXIMUM_TERRITORIES 12
#define COMMAND_NET_MAXIMUM_ROWS 32
#define COMMAND_NET_UPGRADES 5
#define COMMAND_NET_ORDERS 6
#define COMMAND_NET_SHIPS 2
#define COMMAND_NET_NAME_SIZE 24
/* a coordinate's bounds (the camera's and Forge's) */
#define COMMAND_NET_MAXIMUM_COORDINATE 5000.f
/* a squad's number on the HUD */
#define COMMAND_NET_MAXIMUM_SQUAD_NUMBER 999
/* the most resources a side has (far past a game's) */
#define COMMAND_NET_MAXIMUM_RESOURCES 1000000
/* a score's, a time limit's */
#define COMMAND_NET_MAXIMUM_SCORE 100000
#define COMMAND_NET_MAXIMUM_SECONDS 360000
/* none */
#define COMMAND_NET_NO_OBJECT (-1)
#define COMMAND_NET_NO_PLAYER 0xFF
#define COMMAND_NET_NO_SIDE 0xFF
/* the players' slots (absolute indices) a possessor is one of */
#define COMMAND_NET_MAXIMUM_PLAYERS 128

/* rate limits, per machine: every request (records a second, the most at
once); a sync at most once in this long */
#define COMMAND_NET_REQUESTS_PER_SECOND 20.f
#define COMMAND_NET_REQUESTS_BURST 40.f
#define COMMAND_NET_SYNC_MILLISECONDS 2000

/* a client's request */
enum
{
	_command_net_request_none = 0,
	/* squads (their bits), point, target (an object, or none): the squads
	ordered there, an enemy there attacked */
	_command_net_request_order,
	/* squads: they hold where they stand */
	_command_net_request_hold,
	/* row: the HQ's row bought */
	_command_net_request_buy,
	/* into the side's Spartan, first person */
	_command_net_request_possess,
	/* out of it, above the map */
	_command_net_request_leave,
	/* every state entry, please (a client that has just begun, or lost
	track) */
	_command_net_request_sync,
	NUMBER_OF_COMMAND_NET_REQUESTS
};

/* the state's entries (host to client) */
enum
{
	_command_net_state_none = 0,
	/* the game: resources, scores, objectives, upgrades, the Spartans */
	_command_net_state_game,
	/* a squad: index its slot (an unused slot: its used flag clear) */
	_command_net_state_squad,
	/* a territory: index its slot */
	_command_net_state_territory,
	/* a notice: event, its subject and audience, its numbers */
	_command_net_state_event,
	/* the game's tally, the carnage report's: each side's captures, seconds
	of territory, kills, losses; the Spartans' kills and captures */
	_command_net_state_tally,
	NUMBER_OF_COMMAND_NET_STATES
};

/* a squad's flags (state) */
enum
{
	COMMAND_NET_SQUAD_USED = 1 << 0,
	COMMAND_NET_SQUAD_HERO = 1 << 1,
	COMMAND_NET_SQUAD_CREW = 1 << 2,
	COMMAND_NET_SQUAD_AIRBORNE = 1 << 3,
	COMMAND_NET_SQUAD_ARRIVED = 1 << 4,
	COMMAND_NET_SQUAD_FLAGS_KNOWN = 0x1F,
};

/* the game's flags (state) */
enum
{
	COMMAND_NET_GAME_RUNNING = 1 << 0,
	COMMAND_NET_GAME_OVER = 1 << 1,
	COMMAND_NET_GAME_FLAGS_KNOWN = 0x03,
};

/* a flag's state (state: each side's) */
enum
{
	_command_net_flag_none = 0,
	_command_net_flag_home,
	_command_net_flag_carried,
	_command_net_flag_dropped,
	NUMBER_OF_COMMAND_NET_FLAG_STATES
};

/* (state: an event's audience) every commander's */
#define COMMAND_NET_AUDIENCE_EVERYONE 0xFE

/* why a request was dropped (the host's tally) */
enum
{
	_command_net_drop_none = 0,
	_command_net_drop_malformed,
	_command_net_drop_replayed,
	_command_net_drop_rate,
	/* (in the game) its side not the sender's, its squads not the side's,
	its target or row not one it may have */
	_command_net_drop_refused,
	NUMBER_OF_COMMAND_NET_DROPS
};

/* ---------- structures */

/* a client's request (32 bytes on the wire, as it is in memory: the
machines are all little-endian) */
struct command_net_request
{
	uint8_t op;
	/* the side it commands, as it believes (the host's word is the
	sender's player's team: another is dropped) */
	uint8_t side;
	/* its sequence (the sender's, rising) */
	uint16_t sequence;
	/* (order, hold) the squads, a bit each */
	uint32_t squads;
	/* (order) the point */
	float point[3];
	/* (order) an object there (COMMAND_NET_NO_OBJECT: none) */
	int32_t target;
	/* (buy) the HQ's row */
	uint16_t row;
	uint16_t reserved0;
	uint32_t reserved1;
};

typedef char command_net_request_size_assert[sizeof(struct command_net_request) == 32 ? 1 : -1];

/* a state entry (64 bytes): each kind reads its part */
struct command_net_state_game
{
	uint8_t flags;
	uint8_t objectives;
	uint8_t winner;
	uint8_t version;
	int32_t resources[COMMAND_NET_SIDES];
	int32_t scores[COMMAND_NET_SIDES];
	int32_t score_limit;
	/* the time left (seconds; -1: no limit) */
	int32_t seconds_left;
	/* each side: its upgrades built (a bit each), building (a bit each) */
	uint8_t upgrades[COMMAND_NET_SIDES];
	uint8_t building[COMMAND_NET_SIDES];
	/* each side: its Spartan's unit, the player in it (an absolute index,
	COMMAND_NET_NO_PLAYER), owned (bought), squads coming (bought, not left
	yet), its flag's state, the AI commands it */
	int32_t spartans[COMMAND_NET_SIDES];
	uint8_t possessors[COMMAND_NET_SIDES];
	uint8_t owned[COMMAND_NET_SIDES];
	uint8_t coming[COMMAND_NET_SIDES];
	uint8_t flag_states[COMMAND_NET_SIDES];
	uint8_t ai[COMMAND_NET_SIDES];
	/* each side's upgrades' seconds left, the most of them (0: none) */
	uint8_t building_seconds[COMMAND_NET_SIDES];
	/* each side's flag (the netcode's object, COMMAND_NET_NO_OBJECT) */
	int32_t flag_objects[COMMAND_NET_SIDES];
};

struct command_net_state_squad
{
	uint8_t flags;
	uint8_t side;
	uint8_t order;
	uint8_t reserved;
	uint16_t number;
	uint16_t reserved2;
	int32_t units[COMMAND_NET_SQUAD_SIZE];
	float point[3];
	char name[COMMAND_NET_NAME_SIZE];
};

struct command_net_state_territory
{
	/* (its owner and taker: a side or COMMAND_NET_NO_SIDE) */
	uint8_t owner;
	uint8_t taker;
	/* (an objective's) its letter, 'A' on (0: a supply point) */
	uint8_t letter;
	uint8_t used;
	float center[3];
	float radius;
	uint8_t reserved[36];
};

struct command_net_state_event
{
	uint8_t code;
	/* its subject (a side, COMMAND_NET_NO_SIDE), its audience (a side,
	COMMAND_NET_AUDIENCE_EVERYONE) */
	uint8_t subject;
	uint8_t audience;
	uint8_t reserved;
	int32_t a;
	int32_t b;
	uint8_t reserved2[48];
};

struct command_net_state_tally
{
	int32_t captures[COMMAND_NET_SIDES];
	int32_t territory_seconds[COMMAND_NET_SIDES];
	int32_t kills[COMMAND_NET_SIDES];
	int32_t losses[COMMAND_NET_SIDES];
	int32_t spartan_kills;
	int32_t spartan_captures;
	uint8_t reserved[20];
};

/* the most of a tally's count */
#define COMMAND_NET_MAXIMUM_TALLY 1000000

struct command_net_state
{
	uint8_t kind;
	/* (squad, territory) its slot */
	uint8_t index;
	uint16_t reserved;
	union
	{
		struct command_net_state_game game;
		struct command_net_state_squad squad;
		struct command_net_state_territory territory;
		struct command_net_state_event event;
		struct command_net_state_tally tally;
		uint8_t bytes[60];
	} u;
};

typedef char command_net_state_size_assert[sizeof(struct command_net_state) == 64 ? 1 : -1];
typedef char command_net_state_game_size_assert[sizeof(struct command_net_state_game) <= 60 ? 1 : -1];
typedef char command_net_state_squad_size_assert[sizeof(struct command_net_state_squad) <= 60 ? 1 : -1];

/* a sender's requests' sequence (the host's, per machine) */
struct command_net_sequence
{
	uint16_t last;
	uint8_t started;
};

/* a rate limit (tokens, refilled by the clock) */
struct command_net_bucket
{
	float tokens;
	uint32_t time;
	uint8_t started;
};

/* ---------- prototypes/COMMAND_NET_PROTOCOL.C */

/* whether a number is finite, a point within the bounds */
int command_net_real_valid(float value);
int command_net_point_valid(float const point[3]);

/* whether a request is well made: its operation known, its fields in
range for it (rows: the HQ's rows the host has), the others zero */
int command_net_request_valid(struct command_net_request const *request, int rows);

/* whether a state entry is well made (the client's check of the host's:
every index, side, number and string in range) */
int command_net_state_valid(struct command_net_state const *state, int rows, int events);

/* a sender's sequence: TRUE if this one is newer than the last (taken) */
int command_net_sequence_take(struct command_net_sequence *sequence, uint16_t value);

/* a rate limit's token taken at a time (milliseconds), refilled at rate a
second up to burst: TRUE if there was one */
int command_net_bucket_take(struct command_net_bucket *bucket, uint32_t now, float rate, float burst);

#endif
