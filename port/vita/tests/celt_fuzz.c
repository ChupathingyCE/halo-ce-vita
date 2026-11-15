/* celt_fuzz.c: MCC's FMOD CELT streams as the sound cache decodes them
(port/linux/src/h2_sound_codec.c h2_sound_codec_transcode_from, fsb_celt.c,
libcelt 0.11, xbox_adpcm_encoder.c) on hostile chunk bytes - the 1.1.3
audit's harness (run_mcc_sound_test.sh "celtfuzz", libFuzzer under
AddressSanitizer and UBSan). The first 6 bytes choose the stream: byte 0
its channels (bit 0) and the output's (bit 1), bytes 1-4 its first frame,
byte 5 the output's size; the rest is the stream's one chunk. */
#include "h2_sound_codec.h"
#include "fsb_celt.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct memory
{
	const uint8_t *bytes;
	uint32_t size;
};

static int memory_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct memory *memory = context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(buffer, memory->bytes + offset, size);
	return 1;
}

static int sink(void *context, uint8_t const *bytes, uint32_t size, uint32_t offset)
{
	(void)context;
	(void)bytes;
	(void)size;
	(void)offset;
	return 1;
}

static void *work;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct h2_sound_codec_chunk chunk;
	struct h2_sound_codec_stream stream;
	struct h2_sound_codec_result result;
	static uint8_t staging[4096];
	uint8_t *copy;
	struct memory memory;
	uint32_t first_frame = 0, output_bytes;
	int channels, output_channels;

	if (size < 6)
		return 0;
	if (!work)
		work = aligned_alloc(16, (h2_sound_codec_work_bytes() + 15) & ~(size_t)15);
	channels = (data[0] & 1) + 1;
	output_channels = ((data[0] >> 1) & 1) + 1;
	memcpy(&first_frame, data + 1, 4);
	first_frame &= 0x3FFFF;
	output_bytes = (uint32_t)(data[5] + 1) * 36u * (uint32_t)output_channels * 4;
	data += 6;
	size -= 6;
	/* (a copy of its own: reads past it are ASan's) */
	copy = malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	memory.bytes = copy;
	memory.size = (uint32_t)size;
	memset(&stream, 0, sizeof(stream));
	chunk.offset = 0;
	chunk.bytes = (uint32_t)size;
	stream.kind = H2_SOUND_CODEC_KIND_FSB_CELT;
	stream.channels = channels;
	stream.rate = FSB_CELT_RATE;
	stream.frames = 0x7FFFFFFF;
	stream.chunk_count = 1;
	stream.chunks = &chunk;
	stream.first_frame = first_frame;
	h2_sound_codec_transcode_from(&stream, memory_read, &memory, work, h2_sound_codec_work_bytes(),
		output_channels == 2 ? 44100 : 22050, output_channels, staging, sizeof(staging), output_bytes, 0, sink, NULL,
		&result);
	free(copy);
	return 0;
}
