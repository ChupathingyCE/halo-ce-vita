/*
H2_SOUND_CODEC.C

(port) A sound stream made Xbox ADPCM (h2_sound_codec.h). The 1.1 line's
cut of 1.2.0's file of the same name: FMOD's CELT (MCC's Halo CE dialogue,
music and streamed sounds, fsb_celt.h; each chunk a run of FMOD's frames,
celt_stream) and Xbox ADPCM (whole blocks of 36 bytes a channel for 64
frames, xbox_adpcm_block_decode, as the mixer decodes them). 1.2.0's Halo 2
codecs (Opus, WMA) are not in this build: those kinds are refused.

The frames are mixed to mono ((left + right) / 2) or kept stereo and taken
to the output rate by a windowed-sinc converter (a Blackman window, its
cutoff 0.9 of the lower of the two Nyquists, normalised for unit gain at
every phase). The converter works on the rational ratio: the output frame j
lies at j * in / out input frames, its phase one of out / gcd(in, out)
kernels computed once. The result goes to the Xbox ADPCM encoder.
*/

/* ---------- headers */

#include "h2_sound_codec.h"

#include <math.h>
#include <string.h>

#include <stdlib.h>
#include "fsb_celt.h"

/* ---------- constants */

enum
{
	INPUT_BYTES = 16 * 1024,
	/* the most frames a CELT frame decodes to, a channel */
	MAXIMUM_PACKET_FRAMES = FSB_CELT_FRAME_SAMPLES,
	/* a CELT decoder's state */
	DECODER_STATE_BYTES = FSB_CELT_DECODER_MAXIMUM_BYTES,
	/* the converter's kernels: at most this many floats, its history this
	many frames */
	TABLE_FLOATS = 8192,
	HISTORY_FRAMES = 4096,
	MAXIMUM_TAPS = 96,
	/* a stream the converter would take for more than this is refused */
	MAXIMUM_SOURCE_RATE = 192000,
	MINIMUM_RATE = 1000,
	MAXIMUM_OUTPUT_RATE = 48000
};

/* ---------- structures */

struct converter
{
	/* the ratio in lowest terms, the kernel's taps (even) and its phases */
	uint32_t in;
	uint32_t out;
	/* the next output frame's place: the input frame it lies at or after, and
	its phase (in 1/out of an input frame), and what each output frame adds
	to them (no division in the loop) */
	uint64_t position;
	uint32_t phase;
	uint32_t step_whole;
	uint32_t step_phase;
	int taps;
	int half;
	int pass_through;
	/* the history: HISTORY_FRAMES frames at most, the first of them input
	frame `base` (absolute), and the next output frame */
	uint32_t length;
	uint64_t base;
	uint64_t next;
	/* the input frames given so far */
	uint64_t given;
};

struct work
{
	uint8_t decoder_state[DECODER_STATE_BYTES] __attribute__((aligned(16)));
	uint8_t input[INPUT_BYTES];
	int16_t pcm[MAXIMUM_PACKET_FRAMES * 2];
	float table[TABLE_FLOATS];
	float history[2][HISTORY_FRAMES];
	struct converter converter;
};

/* the stream being read through a window of INPUT_BYTES */
struct reader
{
	h2_sound_codec_read_proc read;
	void *context;
	struct work *work;
	uint32_t window;
	uint32_t window_bytes;
	int failed;
};

/* where a stream's mono frames go, and whether it has all it wants */
struct frames
{
	void (*give)(void *context, int left, int right);
	int (*done)(void *context);
	void *context;
};

/* what is being made */
struct output
{
	struct xbox_adpcm_encoder encoder;
	struct work *work;
	uint32_t target;
	uint32_t produced;
	/* the output's channels (1: the stream's mixed to mono) */
	int channels;
	/* the output frames left out before the first made (a link of a
	chain: its first frame in the whole) */
	uint32_t skip;
};

/* ---------- private code */

static uint32_t gcd(uint32_t a, uint32_t b)
{
	while (b)
	{
		uint32_t t = a % b;

		a = b;
		b = t;
	}
	return a;
}

static int rates_ok(long input_rate, long output_rate)
{
	return input_rate >= MINIMUM_RATE && input_rate <= MAXIMUM_SOURCE_RATE && output_rate >= MINIMUM_RATE &&
		output_rate <= MAXIMUM_OUTPUT_RATE;
}

/* `size` bytes at `offset` of the dat, within [offset, limit), in the
window; NULL when they cannot be read */
static uint8_t const *reader_get(struct reader *reader, uint32_t offset, uint32_t size, uint32_t limit)
{
	if (size > INPUT_BYTES || offset > limit || size > limit - offset)
		return NULL;
	if (!reader->window_bytes || offset < reader->window || offset - reader->window > reader->window_bytes ||
		size > reader->window_bytes - (offset - reader->window))
	{
		uint32_t bytes = limit - offset < INPUT_BYTES ? limit - offset : INPUT_BYTES;

		reader->window_bytes = 0;
		if (!reader->read(reader->context, offset, bytes, reader->work->input))
		{
			reader->failed = 1;
			return NULL;
		}
		reader->window = offset;
		reader->window_bytes = bytes;
	}
	return reader->work->input + (offset - reader->window);
}

static int converter_begin(struct work *work, long input_rate, long output_rate)
{
	struct converter *converter = &work->converter;
	uint32_t g = gcd((uint32_t)input_rate, (uint32_t)output_rate);
	double cutoff;
	double half_width;
	uint32_t phase;
	int k;

	memset(converter, 0, sizeof(*converter));
	converter->in = (uint32_t)input_rate / g;
	converter->out = (uint32_t)output_rate / g;
	converter->step_whole = converter->in / converter->out;
	converter->step_phase = converter->in % converter->out;
	if (converter->in == converter->out)
	{
		converter->pass_through = 1;
		return 1;
	}
	/* (the cutoff as a fraction of the input's Nyquist) */
	cutoff = converter->out < converter->in ? 0.9 * (double)converter->out / (double)converter->in : 0.95;
	half_width = 8.0 / cutoff;
	converter->half = (int)ceil(half_width);
	converter->taps = 2 * converter->half;
	if (converter->taps > MAXIMUM_TAPS || (uint64_t)converter->out * (uint64_t)converter->taps > TABLE_FLOATS)
		return 0;
	for (phase = 0; phase < converter->out; phase++)
	{
		double fraction = (double)phase / (double)converter->out;
		double sum = 0.0;
		float *kernel = work->table + (size_t)phase * (size_t)converter->taps;

		for (k = 0; k < converter->taps; k++)
		{
			double x = (double)(k - converter->half + 1) - fraction;
			double t = x / (double)converter->half;
			double window = fabs(t) < 1.0 ? 0.42 + 0.5 * cos(3.14159265358979323846 * t) + 0.08 * cos(2.0 * 3.14159265358979323846 * t) : 0.0;
			double sinc = fabs(x) < 1e-9 ? 1.0 : sin(3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * cutoff * x);
			double weight = cutoff * sinc * window;

			kernel[k] = (float)weight;
			sum += weight;
		}
		for (k = 0; k < converter->taps; k++)
			kernel[k] = (float)((double)kernel[k] / sum);
	}
	return 1;
}

static int clamp16(int value)
{
	return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
}

static void output_frame(struct output *output, int left, int right)
{
	int frame[2];

	if (output->skip)
	{
		output->skip--;
		output->produced++;
		return;
	}
	frame[0] = clamp16(left);
	frame[1] = clamp16(right);
	xbox_adpcm_encoder_frame(&output->encoder, frame);
	output->produced++;
}

/* the output frames whose input lies in the history (`flush`: the frames
past the input's end are zeros) */
static void converter_emit(struct output *output, int flush)
{
	struct work *work = output->work;
	struct converter *converter = &work->converter;

	while (output->produced < output->target && !output->encoder.full && !output->encoder.stopped)
	{
		uint64_t n = converter->position;
		int64_t first = (int64_t)n - converter->half + 1;
		float const *kernel = work->table + (size_t)converter->phase * (size_t)converter->taps;
		float sum[2] = { 0.f, 0.f };
		int k, c;

		/* (the last input frame it needs is n + half) */
		if (!flush && n + (uint64_t)converter->half >= converter->given)
			break;
		if (first >= (int64_t)converter->base && n + (uint64_t)converter->half < converter->given &&
			(uint64_t)first + (uint64_t)converter->taps - converter->base <= converter->length)
		{
			/* (all of it in the history) */
			for (c = 0; c < output->channels; c++)
			{
				float const *source = work->history[c] + ((uint64_t)first - converter->base);

				for (k = 0; k < converter->taps; k++)
					sum[c] += kernel[k] * source[k];
			}
		}
		else
		{
			for (k = 0; k < converter->taps; k++)
			{
				int64_t m = first + k;

				if (m < 0 || (uint64_t)m >= converter->given)
					continue;
				if ((uint64_t)m < converter->base || (uint64_t)m - converter->base >= converter->length)
					continue;
				for (c = 0; c < output->channels; c++)
					sum[c] += kernel[k] * work->history[c][(uint64_t)m - converter->base];
			}
		}
		output_frame(output, (int)lrintf(sum[0]), (int)lrintf(sum[output->channels - 1]));
		converter->next++;
		converter->position += converter->step_whole;
		converter->phase += converter->step_phase;
		if (converter->phase >= converter->out)
		{
			converter->phase -= converter->out;
			converter->position++;
		}
	}
	/* (what the next output frame needs is kept) */
	{
		int64_t keep_from = (int64_t)converter->position - converter->half + 1;

		if (keep_from > (int64_t)converter->base)
		{
			uint64_t drop = (uint64_t)keep_from - converter->base;

			if (drop >= converter->length)
			{
				converter->base += converter->length;
				converter->length = 0;
			}
			else
			{
				int c;

				for (c = 0; c < output->channels; c++)
					memmove(work->history[c], work->history[c] + drop, (size_t)(converter->length - drop) * sizeof(float));
				converter->length -= (uint32_t)drop;
				converter->base += drop;
			}
		}
	}
}

/* one frame of the stream (a mono stream's twice) */
static void converter_give(struct output *output, int left, int right)
{
	struct converter *converter = &output->work->converter;

	if (output->channels == 1)
		left = right = (left + right) / 2;
	if (converter->pass_through)
	{
		converter->given++;
		if (output->produced < output->target)
			output_frame(output, left, right);
		return;
	}
	if (converter->length >= HISTORY_FRAMES)
		converter_emit(output, 0);
	if (converter->length >= HISTORY_FRAMES)
	{
		/* (cannot be: the history holds more than the taps) */
		return;
	}
	output->work->history[0][converter->length] = (float)left;
	output->work->history[1][converter->length] = (float)right;
	converter->length++;
	converter->given++;
}

static int output_done(struct output const *output)
{
	return output->encoder.full || output->encoder.stopped || output->produced >= output->target;
}

static void output_give(void *context, int left, int right)
{
	converter_give((struct output *)context, left, right);
}

static int output_finished(void *context)
{
	return output_done((struct output const *)context);
}

/* ---------- the streams */

/* (FMOD's CELT, MCC's Halo CE banks: fsb_celt.h) each chunk a run of
frames, each an 8-byte header and its payload; one decoder for the chunk.
From `stream->first_frame` (a link of a long sound's chain): the frames
before a few ahead of it are passed over by their headers alone, those few
decoded to bring the decoder's state (its overlap and energy prediction) in
line, and their samples before the first frame left out */
static enum h2_sound_codec_status celt_stream(
	struct h2_sound_codec_stream const *stream,
	struct reader *reader,
	struct work *work,
	struct frames const *frames_out)
{
	enum
	{
		/* the frames decoded before a link's first and left out */
		WARM_FRAMES = 4
	};
	uint32_t chunk_index;
	int channels = stream->channels;
	uint64_t position = 0;
	uint64_t first = stream->first_frame;
	uint64_t decode_from = first > (uint64_t)WARM_FRAMES * FSB_CELT_FRAME_SAMPLES ?
		first - (uint64_t)WARM_FRAMES * FSB_CELT_FRAME_SAMPLES : 0;

	if (stream->rate != FSB_CELT_RATE || fsb_celt_decoder_bytes(channels) > sizeof(work->decoder_state) ||
		!fsb_celt_decoder_bytes(channels))
	{
		return _h2_sound_codec_unsupported;
	}
	for (chunk_index = 0; chunk_index < stream->chunk_count && !frames_out->done(frames_out->context); chunk_index++)
	{
		struct h2_sound_codec_chunk const *chunk = &stream->chunks[chunk_index];
		uint32_t at = chunk->offset;
		uint32_t end;
		int started = 0;

		if (chunk->offset > UINT32_MAX - chunk->bytes)
			return _h2_sound_codec_bad_stream;
		end = chunk->offset + chunk->bytes;
		/* (FMOD pads a stream to its alignment after the last frame: what
		is not a frame's header ends it) */
		while (at + FSB_CELT_HEADER_BYTES <= end && !frames_out->done(frames_out->context))
		{
			uint8_t const *header = reader_get(reader, at, FSB_CELT_HEADER_BYTES, end);
			uint32_t length;
			uint8_t const *payload;
			int frame;

			if (!header)
				return reader->failed ? _h2_sound_codec_read_failed : _h2_sound_codec_bad_stream;
			if (header[0] != 0x17 || header[1] != 0xC3 || header[2] != 0x0D || header[3] != 0xF3)
				break;
			length = (uint32_t)header[4] | (uint32_t)header[5] << 8 | (uint32_t)header[6] << 16 | (uint32_t)header[7] << 24;
			if (!length || length > FSB_CELT_MAXIMUM_PAYLOAD_BYTES || length > end - at - FSB_CELT_HEADER_BYTES)
				return _h2_sound_codec_bad_stream;
			if (position + FSB_CELT_FRAME_SAMPLES <= decode_from)
			{
				/* (before a link's first frame and its warming: passed over) */
				position += FSB_CELT_FRAME_SAMPLES;
				at += FSB_CELT_HEADER_BYTES + length;
				continue;
			}
			if (!started)
			{
				if (!fsb_celt_decoder_start(work->decoder_state, sizeof(work->decoder_state), channels))
					return _h2_sound_codec_unsupported;
				started = 1;
			}
			payload = reader_get(reader, at + FSB_CELT_HEADER_BYTES, length, end);
			if (!payload)
				return reader->failed ? _h2_sound_codec_read_failed : _h2_sound_codec_bad_stream;
			if (fsb_celt_decode(work->decoder_state, payload, length, work->pcm) != FSB_CELT_FRAME_SAMPLES)
				return _h2_sound_codec_bad_stream;
			for (frame = 0; frame < FSB_CELT_FRAME_SAMPLES && !frames_out->done(frames_out->context); frame++, position++)
			{
				int left = work->pcm[channels * frame];
				int right = channels == 2 ? work->pcm[2 * frame + 1] : left;

				if (position >= first)
					frames_out->give(frames_out->context, left, right);
			}
			at += FSB_CELT_HEADER_BYTES + length;
		}
	}
	return _h2_sound_codec_ok;
}

static enum h2_sound_codec_status adpcm_stream(
	struct h2_sound_codec_stream const *stream,
	struct reader *reader,
	struct frames const *frames_out)
{
	uint32_t block_bytes = (uint32_t)XBOX_ADPCM_BLOCK_BYTES * (uint32_t)stream->channels;
	uint32_t chunk_index;

	for (chunk_index = 0; chunk_index < stream->chunk_count && !frames_out->done(frames_out->context); chunk_index++)
	{
		struct h2_sound_codec_chunk const *chunk = &stream->chunks[chunk_index];
		uint32_t at = chunk->offset;
		uint32_t end;

		if (chunk->offset > UINT32_MAX - chunk->bytes)
			return _h2_sound_codec_bad_stream;
		end = chunk->offset + chunk->bytes;
		while (at + block_bytes <= end && !frames_out->done(frames_out->context))
		{
			uint8_t const *block = reader_get(reader, at, block_bytes, end);
			short frames[XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_MAXIMUM_CHANNELS];
			int frame;

			if (!block)
				return reader->failed ? _h2_sound_codec_read_failed : _h2_sound_codec_bad_stream;
			xbox_adpcm_block_decode(block, stream->channels, frames);
			for (frame = 0; frame < XBOX_ADPCM_BLOCK_SAMPLES; frame++)
			{
				int left = frames[stream->channels * frame];
				int right = stream->channels == 2 ? frames[2 * frame + 1] : left;

				frames_out->give(frames_out->context, left, right);
			}
			at += block_bytes;
		}
	}
	return _h2_sound_codec_ok;
}

/* a mono stream already at the output rate, copied as it is */
static enum h2_sound_codec_status copy_stream(
	struct h2_sound_codec_stream const *stream,
	struct reader *reader,
	uint8_t *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	uint32_t first_frame,
	xbox_adpcm_sink_proc sink,
	void *sink_context,
	struct h2_sound_codec_result *result)
{
	uint32_t block_bytes = (uint32_t)XBOX_ADPCM_BLOCK_BYTES * (uint32_t)stream->channels;
	uint32_t piece_bytes = staging_bytes / block_bytes * block_bytes;
	uint32_t written = 0;
	uint32_t chunk_index;
	/* (a link: the whole blocks before its first frame left out) */
	uint64_t skip = (uint64_t)(first_frame / XBOX_ADPCM_BLOCK_SAMPLES) * block_bytes;

	if (piece_bytes < block_bytes)
		return _h2_sound_codec_bad_arguments;
	result->copied = 1;
	for (chunk_index = 0; chunk_index < stream->chunk_count && written < output_bytes; chunk_index++)
	{
		struct h2_sound_codec_chunk const *chunk = &stream->chunks[chunk_index];
		uint32_t usable = chunk->bytes / block_bytes * block_bytes;
		uint32_t done = 0;

		if (chunk->offset > UINT32_MAX - chunk->bytes)
			return _h2_sound_codec_bad_stream;
		if (skip >= usable)
		{
			skip -= usable;
			continue;
		}
		done = (uint32_t)skip;
		skip = 0;
		while (done < usable && written < output_bytes)
		{
			uint32_t size = usable - done < piece_bytes ? usable - done : piece_bytes;

			if (size > output_bytes - written)
				size = (output_bytes - written) / block_bytes * block_bytes;
			if (!size)
				break;
			if (!reader->read(reader->context, chunk->offset + done, size, staging))
				return _h2_sound_codec_read_failed;
			if (!sink(sink_context, staging, size, written))
			{
				result->stopped = 1;
				return _h2_sound_codec_ok;
			}
			done += size;
			written += size;
		}
	}
	result->frames_decoded = written / block_bytes * XBOX_ADPCM_BLOCK_SAMPLES;
	/* (silence after: blocks of zeros decode to zero) */
	memset(staging, 0, piece_bytes);
	while (written < output_bytes)
	{
		uint32_t size = output_bytes - written < piece_bytes ? output_bytes - written : piece_bytes;

		if (!sink(sink_context, staging, size, written))
		{
			result->stopped = 1;
			return _h2_sound_codec_ok;
		}
		written += size;
	}
	return _h2_sound_codec_ok;
}

/* ---------- public code */

size_t h2_sound_codec_work_bytes(void)
{
	return sizeof(struct work);
}

uint32_t h2_sound_codec_output_frames(
	uint32_t source_frames,
	long source_rate,
	long output_rate)
{
	uint64_t frames;

	if (!rates_ok(source_rate, output_rate))
		return 0;
	frames = ((uint64_t)source_frames * (uint64_t)output_rate + (uint64_t)source_rate - 1) / (uint64_t)source_rate;
	return frames > UINT32_MAX ? 0 : (uint32_t)frames;
}

int h2_sound_codec_is_copy(
	int kind,
	int channels,
	long rate,
	long output_rate,
	int output_channels)
{
	return kind == H2_SOUND_CODEC_KIND_XBOX_ADPCM && channels == output_channels && rate == output_rate;
}

enum h2_sound_codec_status h2_sound_codec_transcode(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work_memory,
	size_t work_bytes,
	long output_rate,
	int output_channels,
	uint8_t *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	xbox_adpcm_sink_proc sink,
	void *sink_context,
	struct h2_sound_codec_result *result)
{
	return h2_sound_codec_transcode_from(stream, read, read_context, work_memory, work_bytes, output_rate, output_channels, staging,
		staging_bytes, output_bytes, 0, sink, sink_context, result);
}

enum h2_sound_codec_status h2_sound_codec_transcode_from(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work_memory,
	size_t work_bytes,
	long output_rate,
	int output_channels,
	uint8_t *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	uint32_t first_frame,
	xbox_adpcm_sink_proc sink,
	void *sink_context,
	struct h2_sound_codec_result *result)
{
	struct work *work = (struct work *)work_memory;
	struct reader reader;
	struct output output;
	enum h2_sound_codec_status status;

	memset(result, 0, sizeof(*result));
	if (!stream || !read || !sink || !staging || !work || work_bytes < sizeof(struct work) || ((uintptr_t)work & 15) ||
		staging_bytes < 2 * XBOX_ADPCM_BLOCK_BYTES * (uint32_t)(output_channels < 1 ? 1 : output_channels) || !output_bytes ||
		output_channels < 1 || output_channels > 2 || stream->channels < 1 || stream->channels > 2 ||
		stream->chunk_count > H2_SOUND_CODEC_MAXIMUM_CHUNKS || (stream->chunk_count && !stream->chunks))
	{
		return result->status = _h2_sound_codec_bad_arguments;
	}
	if (stream->kind != H2_SOUND_CODEC_KIND_XBOX_ADPCM && stream->kind != H2_SOUND_CODEC_KIND_FSB_CELT)
	{
		return result->status = _h2_sound_codec_unsupported;
	}
	if (!rates_ok(stream->rate, output_rate) || (stream->first_frame && stream->kind != H2_SOUND_CODEC_KIND_FSB_CELT))
	{
		return result->status = _h2_sound_codec_unsupported;
	}
	reader.read = read;
	reader.context = read_context;
	reader.work = work;
	reader.window = 0;
	reader.window_bytes = 0;
	reader.failed = 0;
	if (h2_sound_codec_is_copy(stream->kind, stream->channels, stream->rate, output_rate, output_channels))
	{
		status = copy_stream(stream, &reader, staging, staging_bytes, output_bytes, first_frame, sink, sink_context, result);
		result->source_frames = result->frames_decoded;
		result->frames_padded = output_bytes / ((uint32_t)XBOX_ADPCM_BLOCK_BYTES * (uint32_t)output_channels) *
			XBOX_ADPCM_BLOCK_SAMPLES - result->frames_decoded;
		return result->status = status;
	}
	memset(&output, 0, sizeof(output));
	output.work = work;
	output.channels = output_channels;
	output.target = h2_sound_codec_output_frames(stream->frames, stream->rate, output_rate);
	output.skip = first_frame < output.target ? first_frame : output.target;
	if (!converter_begin(work, stream->rate, output_rate) ||
		!xbox_adpcm_encoder_begin_sink(&output.encoder, staging, staging_bytes, output_bytes, output_channels, sink, sink_context))
	{
		return result->status = _h2_sound_codec_unsupported;
	}
	{
		struct frames frames_out;
		struct h2_sound_codec_stream from = *stream;

		/* (a link: begun a few blocks before its first frame - the converter's
		history filled, the encoder's step index warmed by them, none of them
		written - so it joins the link before it without a click) */
		if (stream->first_frame)
		{
			uint32_t margin = 4 * XBOX_ADPCM_BLOCK_SAMPLES;
			uint32_t margin_in = (uint32_t)(((uint64_t)margin * (uint64_t)stream->rate + (uint64_t)output_rate / 2) /
				(uint64_t)output_rate);

			if (margin_in > stream->first_frame)
				margin = margin_in = 0;
			from.first_frame -= margin_in;
			output.encoder.warm_frames = margin;
			output.target += margin;
		}
		frames_out.give = output_give;
		frames_out.done = output_finished;
		frames_out.context = &output;
		status = stream->kind == H2_SOUND_CODEC_KIND_FSB_CELT ? celt_stream(&from, &reader, work, &frames_out) :
			adpcm_stream(stream, &reader, &frames_out);
	}
	result->source_frames = (uint32_t)work->converter.given;
	if (status == _h2_sound_codec_ok && !work->converter.pass_through)
	{
		/* the filter's tail: the zeros after the input's end */
		converter_emit(&output, 1);
	}
	result->frames_padded = xbox_adpcm_encoder_finish(&output.encoder);
	result->frames_decoded = output.encoder.frames;
	result->stopped = output.encoder.stopped;
	result->sum_of_squares = output.encoder.sum_of_squares;
	result->peak = output.encoder.peak;
	return result->status = status;
}

/* the stream's mono frames at its own rate, given to `give` (a test's) */
struct decode_context
{
	h2_sound_codec_frame_proc give;
	void *context;
	uint32_t count;
	uint32_t limit;
};

static void decode_give(void *context, int left, int right)
{
	struct decode_context *decode = (struct decode_context *)context;

	if (decode->count < decode->limit)
		decode->give(decode->context, (left + right) / 2);
	decode->count++;
}

static int decode_done(void *context)
{
	struct decode_context const *decode = (struct decode_context const *)context;

	return decode->count >= decode->limit;
}

enum h2_sound_codec_status h2_sound_codec_decode(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work_memory,
	size_t work_bytes,
	h2_sound_codec_frame_proc give,
	void *give_context,
	uint32_t *count)
{
	struct work *work = (struct work *)work_memory;
	struct reader reader;
	struct decode_context decode;
	struct frames frames_out;
	enum h2_sound_codec_status status;

	*count = 0;
	if (!stream || !read || !give || !work || work_bytes < sizeof(struct work) || ((uintptr_t)work & 15) ||
		stream->channels < 1 || stream->channels > 2 || stream->chunk_count > H2_SOUND_CODEC_MAXIMUM_CHUNKS ||
		(stream->chunk_count && !stream->chunks))
	{
		return _h2_sound_codec_bad_arguments;
	}
	if (stream->kind != H2_SOUND_CODEC_KIND_XBOX_ADPCM && stream->kind != H2_SOUND_CODEC_KIND_FSB_CELT)
		return _h2_sound_codec_unsupported;
	reader.read = read;
	reader.context = read_context;
	reader.work = work;
	reader.window = 0;
	reader.window_bytes = 0;
	reader.failed = 0;
	decode.give = give;
	decode.context = give_context;
	decode.count = 0;
	decode.limit = stream->frames;
	frames_out.give = decode_give;
	frames_out.done = decode_done;
	frames_out.context = &decode;
	status = stream->kind == H2_SOUND_CODEC_KIND_FSB_CELT ? celt_stream(stream, &reader, work, &frames_out) :
		adpcm_stream(stream, &reader, &frames_out);
	*count = decode.count < decode.limit ? decode.count : decode.limit;
	return status;
}

char const *h2_sound_codec_status_describe(
	enum h2_sound_codec_status status)
{
	switch (status)
	{
		case _h2_sound_codec_ok:
			return "ok";
		case _h2_sound_codec_read_failed:
			return "the dat could not be read";
		case _h2_sound_codec_bad_stream:
			return "not the stream the tag says, or damaged";
		case _h2_sound_codec_unsupported:
			return "a format or rate not taken";
		case _h2_sound_codec_bad_arguments:
			return "bad arguments";
		default:
			break;
	}
	return "?";
}
