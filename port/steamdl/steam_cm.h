/*
STEAM_CM.H

Steam's CM messages as one WebSocket message each: a little-endian u32 EMsg
with the protobuf bit (0x80000000), a u32 header length, the header
(CMsgProtoBufHeader), then the body (the message's own protobuf). A Multi
(EMsg 1: CMsgMulti) carries several, each "<u32 length><message>", gzipped
when size_unzipped is set. Non-protobuf messages are refused (a WebSocket CM
sends none the download needs).
*/

#ifndef STEAM_CM_H
#define STEAM_CM_H

#include <stddef.h>
#include <stdint.h>

#define STEAM_CM_PROTO_MASK 0x80000000u
#define STEAM_JOB_NONE 0xffffffffffffffffull

/* the EMsgs the download sends and reads */
enum
{
	STEAM_EMSG_MULTI = 1,
	STEAM_EMSG_SERVICE_METHOD_RESPONSE = 147,
	STEAM_EMSG_SERVICE_METHOD_CALL = 151,
	STEAM_EMSG_CLIENT_HEARTBEAT = 703,
	STEAM_EMSG_CLIENT_LOGOFF = 706,
	STEAM_EMSG_CLIENT_LOGON_RESPONSE = 751,
	STEAM_EMSG_CLIENT_LOGGED_OFF = 757,
	STEAM_EMSG_CLIENT_GET_DEPOT_KEY = 5438,
	STEAM_EMSG_CLIENT_GET_DEPOT_KEY_RESPONSE = 5439,
	STEAM_EMSG_CLIENT_LOGON = 5514,
	STEAM_EMSG_CLIENT_PICS_PRODUCT_INFO_REQUEST = 8903,
	STEAM_EMSG_CLIENT_PICS_PRODUCT_INFO_RESPONSE = 8904,
	STEAM_EMSG_CLIENT_PICS_ACCESS_TOKEN_REQUEST = 8905,
	STEAM_EMSG_CLIENT_PICS_ACCESS_TOKEN_RESPONSE = 8906,
	STEAM_EMSG_SERVICE_METHOD_CALL_NONAUTHED = 9804,
	STEAM_EMSG_CLIENT_HELLO = 9805,
};

struct steam_cm_header
{
	uint64_t steamid;
	int32_t session_id;
	uint64_t job_source, job_target;
	char job_name[256];
	int32_t eresult; /* 2 (fail) when not given, as protobuf's default */
	int has_eresult;
};

struct steam_cm_message
{
	uint32_t emsg;
	struct steam_cm_header header;
	const uint8_t *body;
	size_t body_size;
};

/* a message into output: its length, or -1 if it does not fit */
long steam_cm_build(uint8_t *output, size_t capacity, uint32_t emsg, const struct steam_cm_header *header,
	const void *body, size_t body_size);

/* a received message: 1, or 0 if it is refused (too short, not protobuf, a
header that runs past the end or does not parse) */
int steam_cm_parse(const uint8_t *data, size_t size, struct steam_cm_message *message);

/* a Multi's messages, each handed to a visitor (each parsed, none a Multi
itself): the count handed over, or -1 if it is refused. A message inside that
is not protobuf (Steam still sends a few of the old struct messages - e.g.
ClientVACBanStatus - in the Multi that follows a logon; SteamKit2 and
steam-user read both kinds) is skipped, not a reason to refuse the rest: the
download reads none of them */
typedef int (*steam_cm_visitor)(void *context, const struct steam_cm_message *message);
long steam_cm_multi(const uint8_t *body, size_t size, steam_cm_visitor visitor, void *context);

/* what a Multi held, for the log (no contents: sizes, EMsgs and counts) */
struct steam_cm_multi_facts
{
	/* the Multi's body, its gzip's declared size (0: not zipped), its payload's */
	size_t body_size, unzipped_size, payload_size;
	/* messages handed over, skipped (not protobuf) */
	unsigned int handed, skipped;
	/* the last skipped one's EMsg */
	uint32_t skipped_emsg;
	/* a refusal: why (a fixed text, "" none), the message's offset in the payload, its length word and raw EMsg
	word (0 when not reached) */
	const char *refused;
	size_t refused_offset;
	uint32_t refused_length, refused_emsg;
};
long steam_cm_multi_facts(const uint8_t *body, size_t size, steam_cm_visitor visitor, void *context,
	struct steam_cm_multi_facts *facts);

/* a received message's EMsg word (the proto bit taken off) and whether it is protobuf: 1, or 0 if under 4 bytes */
int steam_cm_peek(const uint8_t *data, size_t size, uint32_t *emsg, int *protobuf);

#endif
