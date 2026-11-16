/*
STEAM_CM.C

steam_cm.h: CM message framing, headers and Multis.
*/

#include "steam_cm.h"

#include "steam_crypto.h"
#include "steam_limits.h"
#include "steam_pb.h"
#include "steam_zip.h"

#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

long steam_cm_build(uint8_t *output, size_t capacity, uint32_t emsg, const struct steam_cm_header *header,
	const void *body, size_t body_size)
{
	struct steam_pb_writer writer;

	if (capacity < 8 || body_size > STEAM_CM_MESSAGE_MAXIMUM)
		return -1;
	steam_pb_writer_init(&writer, output + 8, capacity - 8);
	if (header->steamid)
		steam_pb_put_fixed64(&writer, 1, header->steamid);
	if (header->session_id)
		steam_pb_put_int32(&writer, 2, header->session_id);
	if (header->job_source != STEAM_JOB_NONE)
		steam_pb_put_fixed64(&writer, 10, header->job_source);
	if (header->job_target != STEAM_JOB_NONE)
		steam_pb_put_fixed64(&writer, 11, header->job_target);
	if (header->job_name[0])
		steam_pb_put_string(&writer, 12, header->job_name);
	if (writer.error || body_size > capacity - 8 - writer.size)
		return -1;
	put_le32(output, emsg | STEAM_CM_PROTO_MASK);
	put_le32(output + 4, (uint32_t)writer.size);
	if (body_size)
		memcpy(output + 8 + writer.size, body, body_size);
	return (long)(8 + writer.size + body_size);
}

int steam_cm_parse(const uint8_t *data, size_t size, struct steam_cm_message *message)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	uint32_t raw, header_size;
	int result;

	memset(message, 0, sizeof(*message));
	message->header.job_source = message->header.job_target = STEAM_JOB_NONE;
	message->header.eresult = 2;
	if (!data || size < 8 || size > STEAM_CM_MESSAGE_MAXIMUM)
		return 0;
	raw = le32(data);
	if (!(raw & STEAM_CM_PROTO_MASK))
		return 0;
	message->emsg = raw & ~STEAM_CM_PROTO_MASK;
	header_size = le32(data + 4);
	if (header_size > size - 8)
		return 0;
	steam_pb_reader_init(&reader, data + 8, header_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		switch (field.number)
		{
		case 1:
			if (field.wire != STEAM_PB_FIXED64)
				return 0;
			message->header.steamid = field.value;
			break;
		case 2: message->header.session_id = steam_pb_int32(&field); break;
		case 10:
			if (field.wire != STEAM_PB_FIXED64)
				return 0;
			message->header.job_source = field.value;
			break;
		case 11:
			if (field.wire != STEAM_PB_FIXED64)
				return 0;
			message->header.job_target = field.value;
			break;
		case 12:
			if (field.wire != STEAM_PB_BYTES || field.size >= sizeof(message->header.job_name))
				return 0;
			memcpy(message->header.job_name, field.data, field.size);
			message->header.job_name[field.size] = 0;
			break;
		case 13:
			if (field.wire != STEAM_PB_VARINT)
				return 0;
			message->header.eresult = steam_pb_int32(&field);
			message->header.has_eresult = 1;
			break;
		}
	}
	if (result < 0)
		return 0;
	message->body = data + 8 + header_size;
	message->body_size = size - 8 - header_size;
	return 1;
}

long steam_cm_multi(const uint8_t *body, size_t size, steam_cm_visitor visitor, void *context)
{
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	const uint8_t *payload = NULL;
	uint8_t *unzipped = NULL;
	size_t payload_size = 0, cursor;
	uint64_t size_unzipped = 0;
	long count = 0;
	int result;

	steam_pb_reader_init(&reader, body, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.number == 1 && field.wire == STEAM_PB_VARINT)
			size_unzipped = field.value;
		else if (field.number == 2 && field.wire == STEAM_PB_BYTES)
			payload = field.data, payload_size = field.size;
	}
	if (result < 0 || !payload)
		return -1;
	if (size_unzipped)
	{
		if (size_unzipped > STEAM_CM_MULTI_UNZIPPED_MAXIMUM)
			return -1;
		unzipped = malloc((size_t)size_unzipped);
		if (!unzipped || !steam_gunzip(payload, payload_size, unzipped, (size_t)size_unzipped))
		{
			free(unzipped);
			return -1;
		}
		payload = unzipped;
		payload_size = (size_t)size_unzipped;
	}
	for (cursor = 0; cursor < payload_size;)
	{
		struct steam_cm_message message;
		uint32_t length;

		if (payload_size - cursor < 4 || count >= (long)STEAM_CM_MULTI_MESSAGES_MAXIMUM)
			goto refused;
		length = le32(payload + cursor);
		cursor += 4;
		if (length > payload_size - cursor || !steam_cm_parse(payload + cursor, length, &message) ||
			message.emsg == STEAM_EMSG_MULTI)
		{
			goto refused;
		}
		cursor += length;
		count++;
		if (visitor && visitor(context, &message) != 0)
			goto refused;
	}
	if (unzipped)
	{
		steam_wipe(unzipped, payload_size);
		free(unzipped);
	}
	return count;
refused:
	if (unzipped)
	{
		steam_wipe(unzipped, payload_size);
		free(unzipped);
	}
	return -1;
}
