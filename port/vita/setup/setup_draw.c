/*
SETUP_DRAW.C

The setup screens' picture (setup_draw.h): drawn by the processor into
memory, as the screens come before the game and its renderer, and shown
by the system's display (host/vita_setup.c) or written to a file (the
Linux test). The backdrop is drawn once and copied in at each picture's
start; the letters are rasterised once each (a size and a face) and kept.
*/

#include "setup_draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

/* the faces' files (tools/vita_setup_fonts.py, made by the build) */
extern const unsigned char *const setup_font_files[SETUP_FONT_COUNT];

enum
{
	PIXELS = SETUP_WIDTH * SETUP_HEIGHT,
	/* the letters kept, at most (a screen uses about 150) */
	GLYPH_SLOTS = 1024,
};

struct glyph
{
	int used;
	int font;
	int size;
	unsigned int code;
	int width, height, x_offset, y_offset;
	float advance;
	unsigned char *coverage;
};

static struct
{
	unsigned char *pixels;
	unsigned char *backdrop;
	int faces_tried;
	int face_ready[SETUP_FONT_COUNT];
	stbtt_fontinfo faces[SETUP_FONT_COUNT];
	struct glyph *glyphs;
	int glyph_count;
} draw;

/* Kenney's PlayStation glyphs (ui_overlay.c's button_glyphs) and each
button's own color */
static const struct
{
	unsigned int code;
	unsigned int color;
} glyphs[] =
{
	[SETUP_GLYPH_CROSS] = { 0xE04B, 0x7CB2E8FFu },
	[SETUP_GLYPH_CIRCLE] = { 0xE041, 0xFF6666FFu },
	[SETUP_GLYPH_SQUARE] = { 0xE051, 0xE399D5FFu },
	[SETUP_GLYPH_TRIANGLE] = { 0xE053, 0x40E2A0FFu },
	[SETUP_GLYPH_START] = { 0xE009, 0xFFFFFFFFu },
};

/* ---------- pixels */

static void blend(unsigned char *pixel, unsigned int color, unsigned int coverage)
{
	unsigned int alpha = ((color & 0xFF) * coverage + 127) / 255;

	if (!alpha)
		return;
	pixel[0] = (unsigned char)((((color >> 24) & 0xFF) * alpha + pixel[0] * (255 - alpha) + 127) / 255);
	pixel[1] = (unsigned char)((((color >> 16) & 0xFF) * alpha + pixel[1] * (255 - alpha) + 127) / 255);
	pixel[2] = (unsigned char)((((color >> 8) & 0xFF) * alpha + pixel[2] * (255 - alpha) + 127) / 255);
	pixel[3] = 0xFF;
}

static unsigned int mix(unsigned int from, unsigned int to, float t)
{
	unsigned int result = 0;
	int shift;

	for (shift = 0; shift < 32; shift += 8)
	{
		float a = (float)((from >> shift) & 0xFF), b = (float)((to >> shift) & 0xFF);

		result |= (unsigned int)(a + (b - a) * t + 0.5f) << shift;
	}
	return result;
}

void setup_draw_rectangle(int x, int y, int width, int height, unsigned int color)
{
	int row, column;

	if (!draw.pixels)
		return;
	if (x < 0)
		width += x, x = 0;
	if (y < 0)
		height += y, y = 0;
	if (x + width > SETUP_WIDTH)
		width = SETUP_WIDTH - x;
	if (y + height > SETUP_HEIGHT)
		height = SETUP_HEIGHT - y;
	for (row = 0; row < height; row++)
	{
		unsigned char *pixel = draw.pixels + ((size_t)(y + row) * SETUP_WIDTH + (size_t)x) * 4;

		for (column = 0; column < width; column++, pixel += 4)
			blend(pixel, color, 255);
	}
}

void setup_draw_gradient(int x, int y, int width, int height, unsigned int top, unsigned int bottom)
{
	int row;

	for (row = 0; row < height; row++)
		setup_draw_rectangle(x, y + row, width, 1, mix(top, bottom, height > 1 ? (float)row / (float)(height - 1) : 0));
}

/* the Guide's backdrop: its three blues from top to bottom, a light from
the upper left, and a few faint stars */
static void backdrop_make(void)
{
	static const unsigned int top = 0x030A1AFFu, middle = 0x061430FFu, bottom = 0x030915FFu;
	unsigned int seed = 0x2545F491u;
	int row, column, star;

	for (row = 0; row < SETUP_HEIGHT; row++)
	{
		float t = (float)row / (float)(SETUP_HEIGHT - 1);
		unsigned int color = t < 0.45f ? mix(top, middle, t / 0.45f) : mix(middle, bottom, (t - 0.45f) / 0.55f);
		unsigned char *pixel = draw.backdrop + (size_t)row * SETUP_WIDTH * 4;

		for (column = 0; column < SETUP_WIDTH; column++, pixel += 4)
		{
			float dx = (float)column / SETUP_WIDTH - 0.18f, dy = t - 0.05f;
			float light = 1.0f - sqrtf(dx * dx * 0.6f + dy * dy * 1.4f);

			pixel[0] = (unsigned char)((color >> 24) & 0xFF);
			pixel[1] = (unsigned char)((color >> 16) & 0xFF);
			pixel[2] = (unsigned char)((color >> 8) & 0xFF);
			pixel[3] = 0xFF;
			if (light > 0)
				blend(pixel, 0x1F5FB8FFu, (unsigned int)(light * light * 46.0f));
		}
	}
	for (star = 0; star < 90; star++)
	{
		unsigned char *pixel;

		seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
		column = (int)(seed % SETUP_WIDTH);
		seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
		row = (int)(seed % (SETUP_HEIGHT * 2 / 3));
		pixel = draw.backdrop + ((size_t)row * SETUP_WIDTH + (size_t)column) * 4;
		blend(pixel, 0xC8E1FFFFu, 40 + (seed >> 24) % 90);
	}
}

int setup_draw_begin(const char *version)
{
	if (!draw.pixels)
	{
		draw.pixels = malloc(PIXELS * 4);
		draw.backdrop = malloc(PIXELS * 4);
		if (!draw.pixels || !draw.backdrop)
		{
			setup_draw_release();
			return 0;
		}
		backdrop_make();
	}
	memcpy(draw.pixels, draw.backdrop, PIXELS * 4);
	/* the corner: whose screens these are */
	setup_draw_text(SETUP_FONT_TITLE, 22.0f, 40.0f, 46.0f, SETUP_ALIGN_LEFT, SETUP_COLOR_TEXT, "WARTHOG");
	if (version && *version)
		setup_draw_text(SETUP_FONT_TEXT, 17.0f, SETUP_WIDTH - 40.0f, 44.0f, SETUP_ALIGN_RIGHT, SETUP_COLOR_FAINT,
			version);
	setup_draw_rectangle(40, 58, SETUP_WIDTH - 80, 1, SETUP_COLOR_SEPARATOR);
	return 1;
}

const unsigned char *setup_draw_pixels(void)
{
	return draw.pixels;
}

void setup_draw_panel(int x, int y, int width, int height)
{
	setup_draw_gradient(x, y, width, height, SETUP_COLOR_GLASS_TOP, SETUP_COLOR_GLASS_BOTTOM);
	setup_draw_rectangle(x, y, width, 1, SETUP_COLOR_PANEL_EDGE);
	setup_draw_rectangle(x, y + height - 1, width, 1, SETUP_COLOR_LINE);
	setup_draw_rectangle(x, y + 1, 1, height - 2, SETUP_COLOR_LINE);
	setup_draw_rectangle(x + width - 1, y + 1, 1, height - 2, SETUP_COLOR_LINE);
}

void setup_draw_selection(int x, int y, int width, int height)
{
	int ring;

	/* (a glow fading outward, the row lit, its edge bright) */
	for (ring = 4; ring >= 1; ring--)
		setup_draw_rectangle(x - ring, y - ring, width + ring * 2, height + ring * 2, 0x3D8BFF00u | (unsigned)(12 - ring * 2));
	setup_draw_gradient(x, y, width, height, 0x2F7FD6A0u, 0x1A4F99A0u);
	setup_draw_rectangle(x, y, width, 1, SETUP_COLOR_SELECTED_GLOW | 0x60);
	setup_draw_rectangle(x, y + height - 1, width, 1, SETUP_COLOR_SELECTED_GLOW);
	setup_draw_rectangle(x, y, 3, height, 0x9FD4FFFFu);
}

void setup_draw_progress(int x, int y, int width, int height, float fraction)
{
	int filled;

	if (fraction < 0)
		fraction = 0;
	if (fraction > 1)
		fraction = 1;
	filled = (int)((float)(width - 4) * fraction + 0.5f);
	setup_draw_rectangle(x, y, width, height, SETUP_COLOR_FIELD);
	setup_draw_rectangle(x, y, width, 1, SETUP_COLOR_LINE);
	setup_draw_rectangle(x, y + height - 1, width, 1, SETUP_COLOR_LINE);
	setup_draw_rectangle(x, y, 1, height, SETUP_COLOR_LINE);
	setup_draw_rectangle(x + width - 1, y, 1, height, SETUP_COLOR_LINE);
	if (filled > 0)
	{
		setup_draw_gradient(x + 2, y + 2, filled, height - 4, SETUP_COLOR_PROGRESS_TOP, SETUP_COLOR_PROGRESS_BOTTOM);
		setup_draw_rectangle(x + 2, y + 2, filled, 1, 0xFFFFFF50u);
	}
}

/* ---------- letters */

static stbtt_fontinfo *face(int font)
{
	if (font < 0 || font >= SETUP_FONT_COUNT)
		return NULL;
	if (!(draw.faces_tried & (1 << font)))
	{
		const unsigned char *data = setup_font_files[font];

		draw.faces_tried |= 1 << font;
		draw.face_ready[font] = data && stbtt_InitFont(&draw.faces[font], data, stbtt_GetFontOffsetForIndex(data, 0));
	}
	return draw.face_ready[font] ? &draw.faces[font] : NULL;
}

static const struct glyph *glyph_of(int font, float size, unsigned int code)
{
	int key_size = (int)(size * 4.0f + 0.5f);
	unsigned int hash = (code * 2654435761u) ^ (unsigned int)(font * 977 + key_size * 131);
	stbtt_fontinfo *info = face(font);
	struct glyph *slot;
	int probe, advance, bearing;
	float scale;

	if (!info)
		return NULL;
	if (!draw.glyphs)
	{
		draw.glyphs = calloc(GLYPH_SLOTS, sizeof(*draw.glyphs));
		if (!draw.glyphs)
			return NULL;
	}
	for (probe = 0; probe < GLYPH_SLOTS; probe++)
	{
		slot = &draw.glyphs[(hash + (unsigned int)probe) % GLYPH_SLOTS];
		if (!slot->used)
			break;
		if (slot->font == font && slot->size == key_size && slot->code == code)
			return slot;
	}
	if (probe == GLYPH_SLOTS || draw.glyph_count >= GLYPH_SLOTS * 3 / 4)
	{
		/* (full: the letters made again as they are drawn) */
		int index;

		for (index = 0; index < GLYPH_SLOTS; index++)
			free(draw.glyphs[index].coverage);
		memset(draw.glyphs, 0, GLYPH_SLOTS * sizeof(*draw.glyphs));
		draw.glyph_count = 0;
		slot = &draw.glyphs[hash % GLYPH_SLOTS];
	}
	scale = stbtt_ScaleForPixelHeight(info, size);
	stbtt_GetCodepointHMetrics(info, (int)code, &advance, &bearing);
	slot->used = 1;
	slot->font = font;
	slot->size = key_size;
	slot->code = code;
	slot->advance = (float)advance * scale;
	slot->coverage = stbtt_GetCodepointBitmap(info, scale, scale, (int)code, &slot->width, &slot->height,
		&slot->x_offset, &slot->y_offset);
	draw.glyph_count++;
	return slot;
}

static unsigned int next_code(const char **text)
{
	const unsigned char *at = (const unsigned char *)*text;
	unsigned int code = *at++;
	int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;

	if (more)
		code &= 0x3F >> more;
	while (more-- > 0 && (*at & 0xC0) == 0x80)
		code = code << 6 | (*at++ & 0x3F);
	*text = (const char *)at;
	return code;
}

float setup_text_width(int font, float size, const char *text)
{
	float width = 0;

	while (*text)
	{
		const struct glyph *glyph = glyph_of(font, size, next_code(&text));

		if (glyph)
			width += glyph->advance;
	}
	return width;
}

static void glyph_draw(const struct glyph *glyph, float x, float baseline, unsigned int color)
{
	int left = (int)floorf(x + 0.5f) + glyph->x_offset, top = (int)floorf(baseline + 0.5f) + glyph->y_offset;
	int row, column;

	if (!glyph->coverage || !draw.pixels)
		return;
	for (row = 0; row < glyph->height; row++)
	{
		int y = top + row;

		if (y < 0 || y >= SETUP_HEIGHT)
			continue;
		for (column = 0; column < glyph->width; column++)
		{
			int px = left + column;
			unsigned int coverage = glyph->coverage[row * glyph->width + column];

			if (coverage && px >= 0 && px < SETUP_WIDTH)
				blend(draw.pixels + ((size_t)y * SETUP_WIDTH + (size_t)px) * 4, color, coverage);
		}
	}
}

float setup_draw_text(int font, float size, float x, float baseline, int align, unsigned int color, const char *text)
{
	float width = setup_text_width(font, size, text);

	if (align == SETUP_ALIGN_CENTER)
		x -= width / 2;
	else if (align == SETUP_ALIGN_RIGHT)
		x -= width;
	while (*text)
	{
		const struct glyph *glyph = glyph_of(font, size, next_code(&text));

		if (!glyph)
			continue;
		glyph_draw(glyph, x, baseline, color);
		x += glyph->advance;
	}
	return width;
}

void setup_draw_title(float x, float baseline, int align, const char *text)
{
	static const int offsets[][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }, { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
	size_t index;

	for (index = 0; index < sizeof(offsets) / sizeof(*offsets); index++)
		setup_draw_text(SETUP_FONT_TITLE, 36.0f, x + offsets[index][0], baseline + offsets[index][1], align,
			(SETUP_COLOR_TITLE_GLOW & 0xFFFFFF00u) | 0x26, text);
	setup_draw_text(SETUP_FONT_TITLE, 36.0f, x, baseline, align, SETUP_COLOR_TITLE, text);
}

float setup_button_width(int glyph, const char *label)
{
	const struct glyph *shape = glyph_of(SETUP_FONT_PROMPTS, 34.0f, glyphs[glyph].code);

	return (shape ? shape->advance : 30.0f) + 4.0f + setup_text_width(SETUP_FONT_TEXT, 22.0f, label);
}

float setup_draw_button(float x, float baseline, int glyph, const char *label)
{
	const struct glyph *shape = glyph_of(SETUP_FONT_PROMPTS, 34.0f, glyphs[glyph].code);
	float width = shape ? shape->advance : 30.0f;

	if (shape)
		glyph_draw(shape, x, baseline + 8.0f, glyphs[glyph].color);
	return width + 4.0f + setup_draw_text(SETUP_FONT_TEXT, 22.0f, x + width + 4.0f, baseline, SETUP_ALIGN_LEFT,
		SETUP_COLOR_TEXT, label);
}

float setup_draw_wrapped(int font, float size, float x, float baseline, float width, float line_height,
	unsigned int color, const char *text)
{
	char line[512];

	while (*text)
	{
		const char *end = text, *cut = NULL;
		size_t length;

		/* (as many words as fit; a word longer than the line, cut at a
		slash where it can be) */
		while (*end && *end != '\n')
		{
			const char *next = end;
			size_t taken;

			while (*next && *next != ' ' && *next != '\n' && *next != '/')
				next++;
			if (*next == '/' || *next == ' ')
				next++;
			taken = (size_t)(next - text);
			if (taken >= sizeof(line))
				break;
			memcpy(line, text, taken);
			line[taken] = 0;
			if (setup_text_width(font, size, line) > width && cut)
				break;
			cut = next;
			end = next;
		}
		if (!cut)
			cut = end > text ? end : text + 1;
		length = (size_t)(cut - text);
		if (length >= sizeof(line))
			length = sizeof(line) - 1;
		memcpy(line, text, length);
		line[length] = 0;
		while (length && line[length - 1] == ' ')
			line[--length] = 0;
		setup_draw_text(font, size, x, baseline, SETUP_ALIGN_LEFT, color, line);
		baseline += line_height;
		text = cut;
		while (*text == ' ')
			text++;
		if (*text == '\n')
			text++;
	}
	return baseline;
}

void setup_draw_release(void)
{
	int index;

	if (draw.glyphs)
	{
		for (index = 0; index < GLYPH_SLOTS; index++)
			free(draw.glyphs[index].coverage);
		free(draw.glyphs);
	}
	free(draw.pixels);
	free(draw.backdrop);
	memset(&draw, 0, sizeof(draw));
}
