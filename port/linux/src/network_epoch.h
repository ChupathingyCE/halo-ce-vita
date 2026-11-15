/*
NETWORK_EPOCH.H

OpenCE's network version 42's distributed layout (docs/crossplay.md 10.15),
as ChupathingyCE (GPL-3.0-or-later; chupa-43a and chupa-44a, its
port/linux/game/network_rules.c "chupa-43a's facts", Milenko) reads it; this
port's code is ported from ChupathingyCE's, GPL-3.0-only here. Pure
functions, no game state: the game's side is network_distributed.c.

- Every distributed message's header (the 8 bytes: message header, kind,
  count, tick) is followed by NETWORK_EPOCH_HEADER_EXTRA more on the wire:
  the host's checkpoint epoch (unsigned 32-bit, little-endian). In a batch
  (kind 16) the batch's header has them, and so does each message in it
  (whose size word counts them).
- The host counts the epoch up (never 0) at each map and each co-op revert,
  and before its objects sends a client, over its stream, its boundary: a
  message of kind NETWORK_EPOCH_BOUNDARY_KIND, the header alone, count 0.
- A client takes nothing of the host's of another epoch than the last
  boundary's (none before the first), and stamps its own messages with it.
- A unit state's part bits 6 (42: a scripted pose, 30 bytes, last) and 7
  (43: a deck, 28 bytes after the up) are passed over by a reader.
*/

#ifndef __NETWORK_EPOCH_H
#define __NETWORK_EPOCH_H

#include <stdint.h>

/* the distributed header every message has (message header 2, kind 1,
count 1, tick 4) and the further bytes from 42 */
#define NETWORK_EPOCH_HEADER_SIZE 8
#define NETWORK_EPOCH_HEADER_EXTRA 4
#define NETWORK_EPOCH_BATCH_KIND 16
#define NETWORK_EPOCH_CLIENT_READY_KIND 9
#define NETWORK_EPOCH_BOUNDARY_KIND 96
/* the largest message a distributed header's 12-bit size can say */
#define NETWORK_EPOCH_MESSAGE_MAXIMUM 0xFFF

/* a unit state's further parts: where, and their sizes */
enum
{
	NETWORK_EPOCH_PART_AFTER_UP = 1,
	NETWORK_EPOCH_PART_LAST = 2,
};
#define NETWORK_EPOCH_POSE_PART_BIT 6
#define NETWORK_EPOCH_POSE_PART_SIZE 30
#define NETWORK_EPOCH_DECK_PART_BIT 7
#define NETWORK_EPOCH_DECK_PART_SIZE 28
/* the part bits this build writes and reads itself (riding, up, damage,
powerups, camouflage, killer) */
#define NETWORK_EPOCH_PARTS_BUILT_IN 0x3F

/* a distributed message (or a batch of them) of size bytes as this build
makes it, its first word the message header in this machine's order (the
size in bits 4 to 15), into out (room bytes) with the further header bytes,
the epoch in them. Returns the new size, or 0 when it does not fit or the
batch is malformed */
int network_epoch_header_expand(const uint8_t *message, int size, uint8_t *out, int room, uint32_t epoch);
/* ... and back: the message (or batch) without the further bytes into out
(room bytes; at least size), its epoch in *epoch. In a batch, a message of
another epoch than want is left out (but one of keep_kind: 0 for none), and
*epoch is the batch's own; a batch is read to its first size word past its
end or too short for a header, the messages before it kept. Returns the
size, or 0 when the message is shorter than its header or does not fit */
int network_epoch_header_strip(const uint8_t *message, int size, uint8_t *out, int room, uint32_t want,
	int keep_kind, uint32_t *epoch);
/* whether epoch is newer than current (a signed 32-bit difference past 0) */
int network_epoch_newer(uint32_t epoch, uint32_t current);
/* whether a client takes a boundary (stripped: its size and count its
header's): over the host's stream, not in a batch, the header alone, count
0, an epoch not 0 and newer than current */
int network_epoch_boundary_taken(int size, int count, int from_stream, int in_batch, uint32_t epoch,
	uint32_t current);
/* the bytes of a unit state's further parts in parts at where
(NETWORK_EPOCH_PART_*) for a host of the version (42: bit 6; 43 and on:
bits 6 and 7); -1 when parts has a bit the version does not have, or a part
where the state cannot have one (after the up: alive and placed alone;
last: alive alone) */
int network_epoch_unit_part_bytes(unsigned int version, unsigned int parts, int where, int alive, int placed);

#endif
