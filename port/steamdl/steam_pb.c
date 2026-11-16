/*
STEAM_PB.C

steam_pb.h: the bounded protobuf reader and the fixed-buffer writer.
*/

#include "steam_pb.h"

#include <string.h>

void steam_pb_reader_init(struct steam_pb_reader *reader, const void *data, size_t size)
{
	reader->cursor = data;
	reader->end = reader->cursor + (data ? size : 0);
	reader->error = 0;
}

/* a varint: 1, or 0 if it runs past the end or past 10 bytes (the tenth
holding at most the 64th bit) */
static int read_varint(struct steam_pb_reader *reader, uint64_t *value)
{
	uint64_t result = 0;
	unsigned int shift, count;

	for (count = 0, shift = 0; count < 10; count++, shift += 7)
	{
		uint8_t byte;

		if (reader->cursor >= reader->end)
			return 0;
		byte = *reader->cursor++;
		if (count == 9 && byte > 1)
			return 0;
		result |= (uint64_t)(byte & 0x7f) << shift;
		if (!(byte & 0x80))
		{
			*value = result;
			return 1;
		}
	}
	return 0;
}

static uint64_t read_little(const uint8_t *bytes, unsigned int count)
{
	uint64_t value = 0;

	while (count--)
		value = value << 8 | bytes[count];
	return value;
}

int steam_pb_next(struct steam_pb_reader *reader, struct steam_pb_field *field)
{
	uint64_t key, length;

	memset(field, 0, sizeof(*field));
	if (reader->error)
		return -1;
	if (reader->cursor >= reader->end)
		return 0;
	if (!read_varint(reader, &key) || key >> 3 == 0 || key >> 3 > 0x1fffffff)
		goto bad;
	field->number = (uint32_t)(key >> 3);
	field->wire = (uint32_t)(key & 7);
	switch (field->wire)
	{
	case STEAM_PB_VARINT:
		if (!read_varint(reader, &field->value))
			goto bad;
		return 1;
	case STEAM_PB_FIXED64:
		if (reader->end - reader->cursor < 8)
			goto bad;
		field->value = read_little(reader->cursor, 8);
		reader->cursor += 8;
		return 1;
	case STEAM_PB_FIXED32:
		if (reader->end - reader->cursor < 4)
			goto bad;
		field->value = read_little(reader->cursor, 4);
		reader->cursor += 4;
		return 1;
	case STEAM_PB_BYTES:
		if (!read_varint(reader, &length) || length > (uint64_t)(reader->end - reader->cursor))
			goto bad;
		field->data = reader->cursor;
		field->size = (size_t)length;
		reader->cursor += length;
		return 1;
	default:
		/* (groups, 3 and 4, and the undefined 6 and 7) */
		goto bad;
	}
bad:
	reader->error = 1;
	memset(field, 0, sizeof(*field));
	return -1;
}

/* ---------- the writer */

void steam_pb_writer_init(struct steam_pb_writer *writer, void *buffer, size_t capacity)
{
	writer->data = buffer;
	writer->size = 0;
	writer->capacity = buffer ? capacity : 0;
	writer->error = 0;
}

size_t steam_pb_varint_encode(uint8_t *out, size_t capacity, uint64_t value)
{
	size_t count = 0;

	do
	{
		if (count >= capacity)
			return 0;
		out[count++] = (uint8_t)((value & 0x7f) | (value > 0x7f ? 0x80 : 0));
		value >>= 7;
	} while (value);
	return count;
}

static void put_raw(struct steam_pb_writer *writer, const void *data, size_t size)
{
	if (writer->error)
		return;
	if (size > writer->capacity - writer->size)
	{
		writer->error = 1;
		return;
	}
	if (size)
		memcpy(writer->data + writer->size, data, size);
	writer->size += size;
}

static void put_varint_raw(struct steam_pb_writer *writer, uint64_t value)
{
	uint8_t bytes[10];
	size_t count = steam_pb_varint_encode(bytes, sizeof(bytes), value);

	put_raw(writer, bytes, count);
}

static void put_key(struct steam_pb_writer *writer, uint32_t number, uint32_t wire)
{
	if (number == 0 || number > 0x1fffffff)
	{
		writer->error = 1;
		return;
	}
	put_varint_raw(writer, (uint64_t)number << 3 | wire);
}

void steam_pb_put_varint(struct steam_pb_writer *writer, uint32_t number, uint64_t value)
{
	put_key(writer, number, STEAM_PB_VARINT);
	put_varint_raw(writer, value);
}

void steam_pb_put_int32(struct steam_pb_writer *writer, uint32_t number, int32_t value)
{
	steam_pb_put_varint(writer, number, (uint64_t)(int64_t)value);
}

void steam_pb_put_fixed32(struct steam_pb_writer *writer, uint32_t number, uint32_t value)
{
	uint8_t bytes[4];
	unsigned int index;

	for (index = 0; index < 4; index++)
		bytes[index] = (uint8_t)(value >> (8 * index));
	put_key(writer, number, STEAM_PB_FIXED32);
	put_raw(writer, bytes, 4);
}

void steam_pb_put_fixed64(struct steam_pb_writer *writer, uint32_t number, uint64_t value)
{
	uint8_t bytes[8];
	unsigned int index;

	for (index = 0; index < 8; index++)
		bytes[index] = (uint8_t)(value >> (8 * index));
	put_key(writer, number, STEAM_PB_FIXED64);
	put_raw(writer, bytes, 8);
}

void steam_pb_put_bytes(struct steam_pb_writer *writer, uint32_t number, const void *data, size_t size)
{
	put_key(writer, number, STEAM_PB_BYTES);
	put_varint_raw(writer, size);
	put_raw(writer, data, size);
}

void steam_pb_put_string(struct steam_pb_writer *writer, uint32_t number, const char *text)
{
	steam_pb_put_bytes(writer, number, text, text ? strlen(text) : 0);
}

size_t steam_pb_begin(struct steam_pb_writer *writer, uint32_t number)
{
	put_key(writer, number, STEAM_PB_BYTES);
	return writer->size;
}

void steam_pb_end(struct steam_pb_writer *writer, size_t begun)
{
	uint8_t bytes[10];
	size_t length, count;

	if (writer->error || begun > writer->size)
	{
		writer->error = 1;
		return;
	}
	length = writer->size - begun;
	count = steam_pb_varint_encode(bytes, sizeof(bytes), length);
	if (count > writer->capacity - writer->size)
	{
		writer->error = 1;
		return;
	}
	memmove(writer->data + begun + count, writer->data + begun, length);
	memcpy(writer->data + begun, bytes, count);
	writer->size += count;
}
