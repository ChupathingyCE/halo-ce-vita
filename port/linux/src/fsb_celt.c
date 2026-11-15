/*
FSB_CELT.C

(port) FMOD's CELT frames decoded with libcelt 0.11 (fsb_celt.h).
*/

/* ---------- headers */

#include "fsb_celt.h"

#include <string.h>

#ifndef HALO_OGG_SOUND_NO_DECODER
#include <pthread.h>
#include "../../third_party/celt/celt_port.h"
#include "../../third_party/celt/libcelt/celt.h"
/* (modes.h's, with OPUS_BUILD: celt_port.h) the decoder's signalling, which
FMOD's frames have none of */
#define FSB_CELT_SET_SIGNALLING_REQUEST 10003
#endif

/* ---------- constants */

enum
{
	/* the mode's allocations (5.8 KB for 44100 Hz and 512 samples a frame) */
	ARENA_BYTES = 16 * 1024
};

#ifndef HALO_OGG_SOUND_NO_DECODER

/* ---------- globals */

static struct
{
	pthread_once_t once;
	CELTMode *mode;
	/* the mode's memory: handed out in order, never given back */
	unsigned char arena[ARENA_BYTES] __attribute__((aligned(16)));
	size_t arena_used;
	int arena_overflowed;
} fsb_celt_globals = { PTHREAD_ONCE_INIT };

/* ---------- the library's allocations (celt_port.h) */

void *celt011_alloc(int size)
{
	size_t rounded;
	void *pointer;

	if (size <= 0)
		return NULL;
	rounded = ((size_t)size + 15) & ~(size_t)15;
	if (rounded > ARENA_BYTES - fsb_celt_globals.arena_used)
	{
		fsb_celt_globals.arena_overflowed = 1;
		return NULL;
	}
	pointer = fsb_celt_globals.arena + fsb_celt_globals.arena_used;
	fsb_celt_globals.arena_used += rounded;
	memset(pointer, 0, rounded);
	return pointer;
}

void celt011_free(void *pointer)
{
	/* (the mode lives as long as the game) */
	(void)pointer;
}

/* ---------- private code */

static void mode_make(void)
{
	int error = 0;
	CELTMode *mode = celt_mode_create(FSB_CELT_RATE, FSB_CELT_FRAME_SAMPLES, &error);

	if (mode && error == CELT_OK && !fsb_celt_globals.arena_overflowed)
		fsb_celt_globals.mode = mode;
}

static CELTMode *mode_get(void)
{
	pthread_once(&fsb_celt_globals.once, mode_make);
	return fsb_celt_globals.mode;
}

/* ---------- public code */

size_t fsb_celt_decoder_bytes(int channels)
{
	CELTMode *mode = mode_get();
	int bytes;

	if (!mode || channels < 1 || channels > 2)
		return 0;
	bytes = celt_decoder_get_size_custom(mode, channels);
	return bytes > 0 && bytes <= FSB_CELT_DECODER_MAXIMUM_BYTES ? (size_t)bytes : 0;
}

int fsb_celt_decoder_start(void *memory, size_t bytes, int channels)
{
	size_t need = fsb_celt_decoder_bytes(channels);
	int error = 0;
	CELTDecoder *decoder;

	if (!need || !memory || bytes < need || ((uintptr_t)memory & 7))
		return 0;
	memset(memory, 0, need);
	decoder = celt_decoder_init_custom((CELTDecoder *)memory, mode_get(), channels, &error);
	if (!decoder || error != CELT_OK)
		return 0;
	return celt_decoder_ctl(decoder, FSB_CELT_SET_SIGNALLING_REQUEST, 0) == CELT_OK;
}

int fsb_celt_decode(void *decoder, uint8_t const *payload, uint32_t payload_bytes, int16_t *pcm)
{
	int result;

	if (!decoder || !payload || !pcm || !payload_bytes || payload_bytes > FSB_CELT_MAXIMUM_PAYLOAD_BYTES)
		return -1;
	result = celt_decode((CELTDecoder *)decoder, payload, (int)payload_bytes, pcm, FSB_CELT_FRAME_SAMPLES);
	/* (the frames decoded: the mode's 512) */
	return result == FSB_CELT_FRAME_SAMPLES ? result : (result < 0 ? result : -1);
}

#else

size_t fsb_celt_decoder_bytes(int channels)
{
	(void)channels;
	return 0;
}

int fsb_celt_decoder_start(void *memory, size_t bytes, int channels)
{
	(void)memory;
	(void)bytes;
	(void)channels;
	return 0;
}

int fsb_celt_decode(void *decoder, uint8_t const *payload, uint32_t payload_bytes, int16_t *pcm)
{
	(void)decoder;
	(void)payload;
	(void)payload_bytes;
	(void)pcm;
	return -1;
}

#endif
