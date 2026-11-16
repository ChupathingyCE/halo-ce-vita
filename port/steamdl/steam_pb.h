/*
STEAM_PB.H

A minimal protobuf reader and writer for the few Steam messages the download
sends and reads (hand-written field numbers, no .proto, no generator).

The reader walks one message's fields; a nested message is read by starting
another reader on the field's bytes (its caller keeps the depth,
STEAM_PB_DEPTH_MAXIMUM). Every length is checked against the bytes there:
a field that runs past the end, a varint longer than 10 bytes, a field
number 0, or a group or unknown wire type stops the reader with an error.

The writer appends to a fixed buffer; when it does not fit, the writer is
marked failed and writes nothing more (steam_pb_writer.error).
*/

#ifndef STEAM_PB_H
#define STEAM_PB_H

#include <stddef.h>
#include <stdint.h>

enum
{
	STEAM_PB_VARINT = 0,
	STEAM_PB_FIXED64 = 1,
	STEAM_PB_BYTES = 2,
	STEAM_PB_FIXED32 = 5,
};

struct steam_pb_reader
{
	const uint8_t *cursor, *end;
	int error;
};

struct steam_pb_field
{
	uint32_t number;
	uint32_t wire;
	/* a varint's or fixed's value */
	uint64_t value;
	/* a length-delimited field's bytes */
	const uint8_t *data;
	size_t size;
};

void steam_pb_reader_init(struct steam_pb_reader *reader, const void *data, size_t size);
/* the next field: 1, 0 at the message's end, -1 on an error (and from then
on) */
int steam_pb_next(struct steam_pb_reader *reader, struct steam_pb_field *field);

/* a field's value as the type the message gives it (int32's negative
values come as 64-bit varints) */
static inline int32_t steam_pb_int32(const struct steam_pb_field *field)
{
	return (int32_t)(uint32_t)field->value;
}

struct steam_pb_writer
{
	uint8_t *data;
	size_t size, capacity;
	int error;
};

void steam_pb_writer_init(struct steam_pb_writer *writer, void *buffer, size_t capacity);
void steam_pb_put_varint(struct steam_pb_writer *writer, uint32_t number, uint64_t value);
/* an int32 (negative ones as 10-byte varints, as protobuf has them) */
void steam_pb_put_int32(struct steam_pb_writer *writer, uint32_t number, int32_t value);
void steam_pb_put_fixed32(struct steam_pb_writer *writer, uint32_t number, uint32_t value);
void steam_pb_put_fixed64(struct steam_pb_writer *writer, uint32_t number, uint64_t value);
void steam_pb_put_bytes(struct steam_pb_writer *writer, uint32_t number, const void *data, size_t size);
void steam_pb_put_string(struct steam_pb_writer *writer, uint32_t number, const char *text);
/* a nested message: its tag now, its fields after, its length put in by
steam_pb_end (the bytes moved up to make room) */
size_t steam_pb_begin(struct steam_pb_writer *writer, uint32_t number);
void steam_pb_end(struct steam_pb_writer *writer, size_t begun);

/* a bare varint (for the callers' own framing); the bytes written, 0 if
it does not fit */
size_t steam_pb_varint_encode(uint8_t *out, size_t capacity, uint64_t value);

#endif
