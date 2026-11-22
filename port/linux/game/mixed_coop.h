/*
MIXED_COOP.H

Mixed co-op: a network co-op campaign game in which some machines play the
level from the Xbox's maps and others from Halo: The Master Chief Collection's
files (the Halo content folder, mcc_content.h), the host either. The two are
map families: everywhere else they are kept apart (an MCC multiplayer map is
mcc_<name>, never the Xbox's), and co-op on a stock campaign level is the one
place they play together, because the research (Nov 13 2026,
~/halo-test-out/mixmaps-runs) found every campaign level's collision the same
in both and MCC's level the Xbox's with things appended.

What makes the two one game (mixed_coop.c):

- The level's name on the wire stays the Xbox's (levels\a10\a10). A machine
  with the Xbox's a10.map plays that; one without it but with MCC's plays
  MCC's (the cache loader takes the name as MCC's file: mixed_coop_mcc_alias)
  - in a network game only; alone, the campaign's MCC levels stay the mods
  folder's mods\mcc_a10 as before.
- A machine playing MCC's file in network co-op plays the Xbox's level: the
  MCC-only extras appended to the scenario (the scavenger hunt's Moa scenery)
  are not placed, so the placed objects take the same object indices as on
  the Xbox, and the scripts MCC alone has (breadcrumb nav points) never run;
  and its gameplay is the Xbox's where MCC's tags differ (the Xbox rules
  profile: mixed_coop_rules).
- Each machine works out digests of what must match once the level is loaded
  (the layout of the scenario's placed objects; every gameplay tag's fields;
  each structure BSP's collision as it loads). The host sends a mixed client
  its tag dictionary - every tag by a hash of its group and path and its
  digest, every animation graph's animations by name hash - and the client
  compares before it says it is ready: a level that differs is left with a
  message naming what differs, before a tick of it is played (never a
  desync).
- The client translates the host's tag indices (and animation indices)
  into its own as each message comes in, and its own into the host's as its
  hit reports go out (mixed_coop_tag_from_host ...). Object indices need no
  translation: the placed objects are at the same indices.

The lobby says which machines can (mixed_coop_protocol.h): the host of co-op
on a stock campaign level says hello and its family; a joiner answers with
its own. An older build never answers: playing the host's own family it plays
as before, the other it never could (an MCC host's level carries an identity
older builds refuse, custom_edition_maps.c; an Xbox host's a joiner of MCC's
files is refused here for want of the hello).

Mixed multiplayer (Nov 22 2026) is the same on the multiplayer maps whose
collision is the same in both families' files (Chill Out, Longest, Prisoner,
Chiron TL-34, Rat Race, Wizard, Boarding Action): an MCC host names MCC's
copy the Xbox's (mixed_coop_host_map_name), the lobby's messages for those
maps are version 2 (mixed_coop_protocol.h), the host's netgame flags go in
its dictionary, and the rules of what can come into play follow what the
multiplayer game engine makes (mixed_coop.c). The six maps Halo PC rebuilt
stay apart (mixed_coop_multiplayer_refusal). What the owner has not decided
yet is refused by default, one line each (mixed_coop.c's
mixed_coop_decisions).

Never on the crossplay wire (OpenCE hosts have none of it).
*/

#ifndef HALO_MIXED_COOP_H
#define HALO_MIXED_COOP_H

#include "mixed_coop_protocol.h"

/* the identity an MCC host gives a stock campaign level in its game settings
(custom_edition_maps_network_identity): never a file's (an Xbox file's is
its header checksum and length, so an older build with the Xbox's file
refuses it as another copy), and never 0 (the Xbox host's, compared by no one) */
#define MIXED_COOP_MCC_IDENTITY 0x4D434331UL /* 'MCC1' */

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;
struct structure_bsp;

/* ---------- the level and its files */

/* the level a level name names: a campaign level (levels\a10\a10, a10), 0
to 9; a mixed multiplayer map (chillout, levels\test\chillout\chillout, MCC's
mcc_chillout), 10 on; or -1 */
int mixed_coop_level_index(char const *level_name);
/* whether a level index is a multiplayer map's (mixed multiplayer: the maps
whose collision is the same in both families' files) */
int mixed_coop_level_is_multiplayer(int level);
/* whether mixed multiplayer is on */
int mixed_coop_multiplayer_enabled(void);
/* for a multiplayer map Halo PC rebuilt (Blood Gulch ...: never mixed), the
text a player refused a mixed game of it is shown; else NULL */
char const *mixed_coop_multiplayer_refusal(char const *level_name);
/* the family this machine plays a stock campaign level from: the Xbox's
file in the maps folder, else MCC's in the Halo content folder, else none */
int mixed_coop_local_family(int level);
/* (network_server_manager.c, the host's map chosen) a mixed multiplayer
map MCC's name names (levels\test\mcc_chillout\mcc_chillout) on a machine
with MCC's file of it and no Xbox file: renamed the Xbox's
(levels\test\chillout\chillout), which the cache loader then takes as MCC's
file (mixed_coop_mcc_alias), so that Xbox machines can join; TRUE when
renamed */
int mixed_coop_host_map_name(char *map_name, long size);
/* the same, not logged (tests) */
int mixed_coop_host_map_name_quiet(char *map_name, long size);
/* (custom_edition_cache.c) whether the cache loader takes `map_name` (a stock
campaign level's name, levels\a10\a10 or a10) as MCC's file of that level:
in a network game, with no Xbox file of the level and MCC's there; its
stem into `stem` (MCC_CONTENT_STEM_BYTES, may be NULL) */
int mixed_coop_mcc_alias(char const *map_name, char *stem);

/* ---------- the map's load */

/* (cache_files.c) the game's map's tags loaded: an MCC level of network
co-op made the Xbox's (its extras not placed, its scripts not run, the Xbox
rules), then the digests and the dictionary worked out */
void mixed_coop_tags_loaded(long scenario_index, int custom_edition);
/* (cache_files.c) a structure BSP loaded: its collision's digest, compared
with the host's on a mixed client, sent to the mixed clients by the host */
void mixed_coop_structure_bsp_loaded(short bsp_index, struct structure_bsp const *structure_bsp);

/* ---------- the lobby */

/* (main.c, each frame) the host's hellos */
void mixed_coop_frame(void);
void mixed_coop_client_handle_lobby(struct network_game_client *client, unsigned short *message, short message_size);
void mixed_coop_server_handle_lobby(struct network_game_server *server, struct network_game_server_client_machine *machine,
	unsigned short *message, short message_size);
/* (custom_edition_maps.c) a client's check of the host's level, its
identity: whether this machine may play it (FALSE: refused, why said) */
int mixed_coop_host_level_acceptable(char const *level_name, unsigned long host_identity);
/* (network_client_manager.c, each frame) FALSE once this machine must leave
the game (a level that differs, a host that cannot play mixed co-op): the
player told why */
int mixed_coop_client_update(struct network_game_client *client);
/* (network_client_manager.c) the client's game gone */
void mixed_coop_client_dispose(void);

/* ---------- after the load */

/* (network_objects.c, before a client's ready) whether the client may say it
is ready: not a mixed game, or the host's dictionary taken and matched;
asks the host for it meanwhile */
int mixed_coop_client_ready_allowed(void);
/* (network_distributed.c) the kinds' handlers */
void mixed_coop_handle_request(long machine_index, void const *entries, short count);
void mixed_coop_handle_dictionary(void const *entries, short count, unsigned short entries_size);
void mixed_coop_handle_bsp_digest(void const *entries, short count);

/* ---------- translation (a client's) */

/* whether this machine's host messages are translated (a mixed client
whose dictionary matched) */
int mixed_coop_translating(void);
/* whether a host's message naming tags is dropped (a mixed client still
waiting for the dictionary) */
int mixed_coop_holding(void);
/* a host's tag index as this machine's (NONE stays NONE; a tag this machine
does not have: NONE, counted); this machine's as the host's */
long mixed_coop_tag_from_host(long host_tag_index);
long mixed_coop_tag_to_host(long local_tag_index);
/* a host's animation index in the graph that is this machine's
`local_graph_tag_index`, as this machine's (NONE when it has none of that
name) */
short mixed_coop_animation_from_host(long local_graph_tag_index, short host_animation_index);
/* (network_distributed.c) a host's message translated in place, or FALSE to
drop it */
int mixed_coop_translate_incoming(unsigned char type, void *entries, short count, unsigned short entries_size);
/* (network_distributed.c) a client's message to the host translated in place */
void mixed_coop_translate_outgoing(unsigned char type, void *entries, short count);

/* (r3, the Xbox-host-decides mode) a map Halo PC rebuilt played by an
Xbox-files host and an MCC machine: the host's collision decides where every
player is. On the MCC client: TRUE once it matched such a host's game (its
own player corrected to the host's copy past a small tolerance). On the
host: TRUE for such a client's machine (its predictions taken within that
tolerance alone, never across the host's walls; its shots the host's walls
refuse not counted toward a kick). MIXED_COOP_HOST_DECIDES_TOLERANCE in
world units. */
#define MIXED_COOP_HOST_DECIDES_TOLERANCE 0.75f
int mixed_coop_host_decides_here(void);
int mixed_coop_host_decides_machine(long machine_index);

/* ---------- tests */

/* (HALO_MIXED_COOP_HASH=1) the state both machines must agree on, a line each
tick in debug.txt: "mixed hash <tick> <objects> <hash>" (mixed_coop.c) */
void mixed_coop_tick_hash(void);
/* (network_game_manager.c, a network game's map loaded) "mixed placed
<objects> <hash>": every object the map placed, by index */
void mixed_coop_placed_hash(void);

#endif
