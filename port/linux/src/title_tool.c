/*
TITLE_TOOL.C

The title tool (title_tool.h): the Halo CE menus' heading lettering rebuilt
from the player's own ui.map, and words of the port's own set in it.

Adapted from ChupathingyCE's title tool (port/linux/src/title_tool.c,
31024b132, "Halo's own title lettering, made from your ui.map", Milenko,
with OpenRift412; GPL-3.0-or-later), which rebuilds the maps' titles at 4x.
This port uses its reading of the pictures and its letters' shapes as they
were, and sets new words with them at the game's own size instead
(title_tool_set): Menu style Classic+'s headings for its own screens
(port/linux/game/menu_titles.c). The pictures come decoded from the caller
(the game's own bitmap reads), not from the map file.

1. Each title picture (DXT3 or DXT5, decoded: title_tool_dxt_decode) is read
as ChupathingyCE's tools/title_assets.py reads it: which texels are text
(text_mask), how much of each the text covers (text_coverage), the text's
color and opacity, and its baseline.
2. Every letter of every title is an instance of its character, placed where
the title's table says it starts and on its title's baseline. Its texels
near it are samples of the character's shape at that sub-texel position;
all the instances' samples, put together on a grid GRID times finer than a
texel and smoothed, make the character's shape (its field: how much of a
texel centered there the letter covers). Each instance is then moved, by
eighths of a texel, to where it best matches the shape (registration), and
the shapes made again. The samples a neighboring letter covers more than
the letter itself are left out (the L's foot under a following A), and, for
a character seen three times or more, the samples that differ from the
shape by much (an odd neighbor's ink), and the shapes made a last time.
3. (this port) A word is set: each letter after the one before it by the
advance the titles show for that pair, else ink to ink the titles' median
gap apart (a space the median gap across their spaces); X, which no title
has, is Y's arms over its middle and the same mirrored under it. Each
letter's shape is drawn where it is set, sharpened to an edge about one
pixel wide (the field's 0.5 contour, by its gradient), over a plate made of
the title of the nearest width's (its old text filled in from around it,
its middle stretched to the word's width); the text in that title's own
color and opacity.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-FileCopyrightText: 2026 Halo CE for PS Vita contributors (the set words, X, the plate's stretch)
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "title_tool.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a character's shape: GRID samples a texel, over a box of WIDTH by HEIGHT
texels whose corner is (LEFT, TOP) from where the letter starts on its
baseline */
#define GRID 4
#define WIDTH 40
#define HEIGHT 30
#define LEFT (-4)
#define TOP (-26)
#define FIELD_WIDTH (WIDTH * GRID)
#define FIELD_HEIGHT (HEIGHT * GRID)
/* how much the shapes are smoothed (texels) */
#define SMOOTHING 0.85f
/* how far an instance is moved to match its shape: eighths of a texel */
#define MOVE_X 8
#define MOVE_Y 3
/* a sample a neighbor covers more than this (and more than its own
letter) is the neighbor's; one that differs from the shape by more than
OUTLIER is left out */
#define NEIGHBOR 0.3f
#define OUTLIER 0.45f
#define FORMAT_DXT3 15
#define FORMAT_DXT5 16

struct instance
{
	long title;
	int character;
	/* where the letter starts and its baseline, and its move */
	float x, y, dx, dy;
	/* its samples: where (texel centers), what they cover, whether used */
	long count;
	float *px, *py, *value;
	unsigned char *keep;
};

struct shape
{
	float *field;
	/* (title_tool_set) its ink's columns, from where the letter starts, in
	texels: the field's 0.5 contour's leftmost and rightmost */
	float ink_left, ink_right;
};

struct picture
{
	int present;
	int width, height;
	/* RGBA, as decoded */
	unsigned char *texels;
	/* the old text's texels and coverage */
	unsigned char *mask;
	float *coverage;
	float color[3], opacity, baseline;
	/* (title_tool_set) its plate, made as a word first needs it */
	float *plate;
	/* its letters: instances first .. first + letters; whether a space
follows each (title_tool_set's spacing) */
	long first, letters;
	unsigned char *spaced;
};

/* (title_tool_set) the characters a pair's advance is kept for: ' ' to
'_' (the titles' capitals, digits and colon) */
#define PAIR_FIRST 32
#define PAIR_COUNT 64

struct title_tool
{
	long count;
	struct picture *pictures;
	struct instance *instances;
	long instance_count;
	struct shape shapes[256];
	/* (title_tool_set) the advance from a letter to the next one, the
	titles' median, by pair (0: none seen); the median gap between two
	letters' ink, and across a space; the titles' median baseline */
	float pair_advance[PAIR_COUNT][PAIR_COUNT];
	float gap, space, baseline;
};

static unsigned long u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned int u16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
}

/* DXT3 or DXT5 blocks (row by row) as RGBA, as ChupathingyCE's
tools/hud_assets.py decodes them */
unsigned char *title_tool_dxt_decode(const unsigned char *blocks, int width, int height, int format)
{
	unsigned char *texels = malloc((size_t)width * (size_t)height * 4);
	int bx, by, i;

	if (!texels)
		return NULL;
	for (by = 0; by < height / 4; by++)
	{
		for (bx = 0; bx < width / 4; bx++)
		{
			const unsigned char *block = blocks + ((size_t)by * (size_t)(width / 4) + (size_t)bx) * 16;
			unsigned int c0 = u16(block + 8), c1 = u16(block + 10);
			unsigned long bits = u32(block + 12);
			int palette[4][3], alphas[8];
			unsigned long long codes = 0;

			palette[0][0] = (int)((c0 >> 11) & 31) * 255 / 31;
			palette[0][1] = (int)((c0 >> 5) & 63) * 255 / 63;
			palette[0][2] = (int)(c0 & 31) * 255 / 31;
			palette[1][0] = (int)((c1 >> 11) & 31) * 255 / 31;
			palette[1][1] = (int)((c1 >> 5) & 63) * 255 / 63;
			palette[1][2] = (int)(c1 & 31) * 255 / 31;
			for (i = 0; i < 3; i++)
			{
				palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3;
				palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3;
			}
			if (format == FORMAT_DXT5)
			{
				int a0 = block[0], a1 = block[1];

				alphas[0] = a0;
				alphas[1] = a1;
				for (i = 1; i < 7; i++)
				{
					if (a0 > a1)
						alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
					else if (i < 5)
						alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5;
				}
				if (a0 <= a1)
				{
					alphas[6] = 0;
					alphas[7] = 255;
				}
				for (i = 0; i < 6; i++)
					codes |= (unsigned long long)block[2 + i] << (8 * i);
			}
			for (i = 0; i < 16; i++)
			{
				unsigned char *texel = texels + (((size_t)(by * 4 + i / 4) * (size_t)width) + (size_t)(bx * 4 + i % 4)) * 4;
				int code = (int)((bits >> (2 * i)) & 3);

				texel[0] = (unsigned char)palette[code][0];
				texel[1] = (unsigned char)palette[code][1];
				texel[2] = (unsigned char)palette[code][2];
				texel[3] = (unsigned char)(format == FORMAT_DXT5 ? alphas[(codes >> (3 * i)) & 7] :
					((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
			}
		}
	}
	return texels;
}

/* ---------- reading a picture (tools/title_assets.py's text_mask and
text_coverage) */

static int compare_floats(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

/* the median of count values (which it sorts), as NumPy's */
static float median(float *values, long count)
{
	if (count <= 0)
		return 0;
	qsort(values, (size_t)count, sizeof(*values), compare_floats);
	return count & 1 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
}

/* mask eroded or dilated by one texel, by its four neighbors (SciPy's
default; outside counts as not set) */
static void morphology(const unsigned char *mask, unsigned char *out, int width, int height, int grow)
{
	int x, y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int here = mask[y * width + x];
			int left = x > 0 && mask[y * width + x - 1], right = x + 1 < width && mask[y * width + x + 1];
			int up = y > 0 && mask[(y - 1) * width + x], down = y + 1 < height && mask[(y + 1) * width + x];

			out[y * width + x] = (unsigned char)(grow ? (here || left || right || up || down) :
				(here && left && right && up && down));
		}
	}
}

/* the text's texels, its coverage, color and opacity, and its baseline */
static int picture_read(struct picture *picture)
{
	int width = picture->width, height = picture->height, x, y, c;
	long texels = (long)width * height, i, white = 0, dark = 0, solid_count = 0, count;
	const unsigned char *rgba = picture->texels;
	unsigned char *eroded = malloc((size_t)texels), *solid;
	float *values = malloc((size_t)texels * sizeof(float)), maximum_alpha = 0;
	int style;

	picture->mask = malloc((size_t)texels);
	picture->coverage = calloc((size_t)texels, sizeof(float));
	if (!eroded || !values || !picture->mask || !picture->coverage)
	{
		free(eroded);
		free(values);
		return 0;
	}
	for (i = 0; i < texels; i++)
	{
		const unsigned char *t = rgba + i * 4;
		int low = t[0] < t[1] ? t[0] : t[1], high = t[0] > t[1] ? t[0] : t[1];

		low = low < t[2] ? low : t[2];
		high = high > t[2] ? high : t[2];
		white += low > 200 && t[3] > 128;
		dark += t[3] > 0 && high < 64;
		if (t[3] > maximum_alpha)
			maximum_alpha = t[3];
	}
	/* (white text in a glow, a header's bright text on a dark plate, or text
	alone, flat and translucent) */
	style = white > 40 ? 0 : dark > 40 ? 1 : 2;
	for (i = 0; i < texels; i++)
	{
		const unsigned char *t = rgba + i * 4;
		int low = t[0] < t[1] ? t[0] : t[1], high = t[0] > t[1] ? t[0] : t[1];

		low = low < t[2] ? low : t[2];
		high = high > t[2] ? high : t[2];
		picture->mask[i] = (unsigned char)(style == 0 ? low > 200 && t[3] > 128 :
			style == 1 ? t[3] > 0 && high >= 96 : t[3] >= 0.5f * maximum_alpha);
	}
	morphology(picture->mask, eroded, width, height, 0);
	for (i = 0; i < texels; i++)
		solid_count += eroded[i];
	solid = solid_count > 20 ? eroded : picture->mask;
	for (c = 0; c < 4; c++)
	{
		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + c];
		}
		if (c < 3)
			picture->color[c] = median(values, count);
		else
			picture->opacity = median(values, count) / 255;
	}
	if (style == 1)
	{
		/* (by green, between the plate's and the text's) */
		float text, plate;

		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + 1];
		}
		text = median(values, count);
		count = 0;
		for (i = 0; i < texels; i++)
		{
			const unsigned char *t = rgba + i * 4;

			if (t[3] > 0 && t[0] < 64 && t[1] < 64 && t[2] < 64)
				values[count++] = t[1];
		}
		plate = median(values, count);
		for (i = 0; i < texels; i++)
		{
			float v = text != plate ? (rgba[i * 4 + 1] - plate) / (text - plate) : 0;

			picture->coverage[i] = rgba[i * 4 + 3] ? (v < 0 ? 0 : v > 1 ? 1 : v) : 0;
		}
	}
	else
	{
		/* (white text and flat text: by alpha, the solid texels' being whole;
		the tool makes the headers, style 1, so these are read simply) */
		float full;

		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + 3];
		}
		full = median(values, count);
		for (i = 0; i < texels; i++)
		{
			float v = full > 0 ? rgba[i * 4 + 3] / full : 0;

			picture->coverage[i] = picture->mask[i] ? (v > 1 ? 1 : v) : 0;
		}
	}
	free(eroded);
	free(values);
	/* the baseline: under the last row with solid text */
	picture->baseline = -1;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (picture->coverage[y * width + x] >= 0.5f)
			{
				picture->baseline = (float)y + 1;
				break;
			}
		}
	}
	return picture->baseline > 0;
}

/* ---------- the letters' shapes */

/* a 1D Gaussian kernel of sigma (truncated at four sigma, as SciPy's) */
static float *gaussian_kernel(float sigma, int *radius)
{
	int r = (int)(4 * sigma + 0.5f), i;
	float *kernel = malloc((size_t)(2 * r + 1) * sizeof(float)), sum = 0;

	if (!kernel)
		return NULL;
	for (i = -r; i <= r; i++)
		sum += kernel[i + r] = expf(-0.5f * (float)(i * i) / (sigma * sigma));
	for (i = 0; i <= 2 * r; i++)
		kernel[i] /= sum;
	*radius = r;
	return kernel;
}

/* image (width by height, one channel) blurred by the kernel, edges taken
as their nearest value (this port: the edges' taps clamped, the rest
straight, for speed; the sums as ChupathingyCE's blur makes them) */
static void blur1(float *image, int width, int height, const float *kernel, int radius)
{
	float *line, *column;
	int x, y, k, x0 = width, x1 = -1, y0 = height, y1 = -1;

	/* (only around what is not zero: the rest stays zero) */
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (image[(size_t)y * (size_t)width + (size_t)x] != 0)
			{
				x0 = x < x0 ? x : x0;
				x1 = x > x1 ? x : x1;
				y0 = y < y0 ? y : y0;
				y1 = y > y1 ? y : y1;
			}
		}
	}
	if (x1 < 0)
		return;
	line = malloc((size_t)(width > height ? width : height) * sizeof(float));
	column = malloc((size_t)height * sizeof(float));
	if (!line || !column)
	{
		free(line);
		free(column);
		return;
	}
	x0 = x0 - radius < 0 ? 0 : x0 - radius;
	x1 = x1 + radius >= width ? width - 1 : x1 + radius;
	for (y = y0; y <= y1; y++)
	{
		float *row = image + (size_t)y * (size_t)width;

		for (x = x0; x <= x1; x++)
		{
			float sum = 0;

			if (x - radius >= 0 && x + radius < width)
			{
				const float *at = row + x - radius;

				for (k = 0; k <= 2 * radius; k++)
					sum += kernel[k] * at[k];
			}
			else
			{
				for (k = -radius; k <= radius; k++)
				{
					int clamped = x + k < 0 ? 0 : x + k >= width ? width - 1 : x + k;

					sum += kernel[k + radius] * row[clamped];
				}
			}
			line[x] = sum;
		}
		memcpy(row + x0, line + x0, (size_t)(x1 - x0 + 1) * sizeof(float));
	}
	y0 = y0 - radius < 0 ? 0 : y0 - radius;
	y1 = y1 + radius >= height ? height - 1 : y1 + radius;
	for (x = x0; x <= x1; x++)
	{
		for (y = 0; y < height; y++)
			column[y] = image[(size_t)y * (size_t)width + (size_t)x];
		for (y = y0; y <= y1; y++)
		{
			float sum = 0;

			if (y - radius >= 0 && y + radius < height)
			{
				const float *at = column + y - radius;

				for (k = 0; k <= 2 * radius; k++)
					sum += kernel[k] * at[k];
			}
			else
			{
				for (k = -radius; k <= radius; k++)
				{
					int clamped = y + k < 0 ? 0 : y + k >= height ? height - 1 : y + k;

					sum += kernel[k + radius] * column[clamped];
				}
			}
			line[y] = sum;
		}
		for (y = y0; y <= y1; y++)
			image[(size_t)y * (size_t)width + (size_t)x] = line[y];
	}
	free(line);
	free(column);
}

/* (this port) floorf to an int without the library's call or a change of
the rounding mode (ARMv7's VFP and x87 have no floor of their own); the
fields' coordinates are small */
static int floor_int(float value)
{
	int whole = (int)value;

	return whole - (value < (float)whole);
}

/* a shape's field at grid position (gx, gy), linearly; zero outside */
static float field_linear(const float *field, float gx, float gy)
{
	int x0 = floor_int(gx), y0 = floor_int(gy);
	float ax = gx - (float)x0, ay = gy - (float)y0, sum = 0;
	int dx, dy;

	for (dy = 0; dy < 2; dy++)
	{
		for (dx = 0; dx < 2; dx++)
		{
			int x = x0 + dx, y = y0 + dy;

			if (x >= 0 && x < FIELD_WIDTH && y >= 0 && y < FIELD_HEIGHT)
				sum += (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay) * field[y * FIELD_WIDTH + x];
		}
	}
	return sum;
}

/* the Catmull-Rom weights at t (0 to 1) */
static void cubic_weights(float t, float weights[4])
{
	float t2 = t * t, t3 = t2 * t;

	weights[0] = 0.5f * (-t3 + 2 * t2 - t);
	weights[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
	weights[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
	weights[3] = 0.5f * (t3 - t2);
}

/* the field at (gx, gy), cubically; zero outside */
static float field_cubic(const float *field, float gx, float gy)
{
	int x0 = floor_int(gx), y0 = floor_int(gy), i, j;
	float wx[4], wy[4], sum = 0;

	cubic_weights(gx - (float)x0, wx);
	cubic_weights(gy - (float)y0, wy);
	for (j = 0; j < 4; j++)
	{
		int y = y0 - 1 + j;

		if (y < 0 || y >= FIELD_HEIGHT)
			continue;
		for (i = 0; i < 4; i++)
		{
			int x = x0 - 1 + i;

			if (x >= 0 && x < FIELD_WIDTH)
				sum += wx[i] * wy[j] * field[y * FIELD_WIDTH + x];
		}
	}
	return sum;
}

/* where sample s of an instance falls in its shape's grid (moved by its
move and (mx, my) more) */
static void sample_position(const struct instance *instance, long s, float mx, float my, float *gx, float *gy)
{
	*gx = (instance->px[s] - instance->x - instance->dx - mx - LEFT) * GRID;
	*gy = (instance->py[s] - instance->y - instance->dy - my - TOP) * GRID;
}

/* the shape of character from its instances' kept samples */
static void shape_build(struct title_tool *tool, int character, const float *kernel, int radius)
{
	float *field = tool->shapes[character].field;
	float *weight = calloc(FIELD_WIDTH * FIELD_HEIGHT, sizeof(float));
	long i, s;

	if (!weight)
		return;
	memset(field, 0, FIELD_WIDTH * FIELD_HEIGHT * sizeof(float));
	for (i = 0; i < tool->instance_count; i++)
	{
		const struct instance *instance = &tool->instances[i];

		if (instance->character != character)
			continue;
		for (s = 0; s < instance->count; s++)
		{
			float gx, gy, ax, ay;
			int x0, y0, dx, dy;

			if (!instance->keep[s])
				continue;
			sample_position(instance, s, 0, 0, &gx, &gy);
			x0 = floor_int(gx);
			y0 = floor_int(gy);
			ax = gx - (float)x0;
			ay = gy - (float)y0;
			for (dy = 0; dy < 2; dy++)
			{
				for (dx = 0; dx < 2; dx++)
				{
					int x = x0 + dx, y = y0 + dy;
					float w = (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay);

					if (x >= 0 && x < FIELD_WIDTH && y >= 0 && y < FIELD_HEIGHT)
					{
						field[y * FIELD_WIDTH + x] += w * instance->value[s];
						weight[y * FIELD_WIDTH + x] += w;
					}
				}
			}
		}
	}
	blur1(field, FIELD_WIDTH, FIELD_HEIGHT, kernel, radius);
	blur1(weight, FIELD_WIDTH, FIELD_HEIGHT, kernel, radius);
	for (i = 0; i < FIELD_WIDTH * FIELD_HEIGHT; i++)
		field[i] = weight[i] > 1e-3f ? field[i] / weight[i] : 0;
	free(weight);
}

/* (this port: the registration's speed) the field at every half grid
position, field_linear's values exactly: (2 gx + HALF_MARGIN, 2 gy +
HALF_MARGIN); every sample the registration looks at falls on one, the
titles' letters starting at eighths of a texel on whole baselines */
#define HALF_MARGIN 8
#define HALF_WIDTH (2 * FIELD_WIDTH + 2 * HALF_MARGIN)
#define HALF_HEIGHT (2 * FIELD_HEIGHT + 2 * HALF_MARGIN)

static float *half_table(const float *field)
{
	float *half = malloc((size_t)HALF_WIDTH * HALF_HEIGHT * sizeof(float));
	int x, y;

	if (!half)
		return NULL;
	for (y = 0; y < HALF_HEIGHT; y++)
	{
		for (x = 0; x < HALF_WIDTH; x++)
			half[y * HALF_WIDTH + x] = field_linear(field, (float)(x - HALF_MARGIN) / 2, (float)(y - HALF_MARGIN) / 2);
	}
	return half;
}

/* the instance moved to where its samples best match its shape (half: the
field's half_table, or NULL) */
static void instance_register(struct instance *instance, const float *field, const float *half)
{
	float best = -1, best_x = 0, best_y = 0;
	int mx, my;
	long s;
	int *columns = half ? malloc((size_t)(instance->count > 0 ? instance->count : 1) * 2 * sizeof(int)) : NULL;

	/* (each sample's place in the half grid, unmoved; a place between
	them - never, but then - is looked up as before) */
	for (s = 0; columns && s < instance->count; s++)
	{
		float gx, gy, u, v;

		sample_position(instance, s, 0, 0, &gx, &gy);
		u = 2 * (gx - 0.5f);
		v = 2 * (gy - 0.5f);
		if (fabsf(u - floorf(u + 0.5f)) > 1e-3f || fabsf(v - floorf(v + 0.5f)) > 1e-3f)
		{
			free(columns);
			columns = NULL;
			break;
		}
		columns[s * 2] = (int)floorf(u + 0.5f) + HALF_MARGIN;
		columns[s * 2 + 1] = (int)floorf(v + 0.5f) + HALF_MARGIN;
	}
	for (my = -MOVE_Y; my <= MOVE_Y; my++)
	{
		for (mx = -MOVE_X; mx <= MOVE_X; mx++)
		{
			float error = 0;

			if (columns)
			{
				for (s = 0; s < instance->count; s++)
				{
					int x = columns[s * 2] - mx, y = columns[s * 2 + 1] - my;
					float d = (x >= 0 && x < HALF_WIDTH && y >= 0 && y < HALF_HEIGHT ? half[y * HALF_WIDTH + x] : 0) -
						instance->value[s];

					error += d * d;
				}
			}
			else
			{
				for (s = 0; s < instance->count; s++)
				{
					float gx, gy, d;

					sample_position(instance, s, 0, 0, &gx, &gy);
					d = field_linear(field, gx - mx * GRID / 8.0f - 0.5f, gy - my * GRID / 8.0f - 0.5f) - instance->value[s];
					error += d * d;
				}
			}
			if (best < 0 || error < best)
			{
				best = error;
				best_x = mx / 8.0f;
				best_y = my / 8.0f;
			}
		}
	}
	free(columns);
	instance->dx = best_x;
	instance->dy = best_y;
}

/* an instance's shape at sample s of another instance (or its own) */
static float instance_cover(const struct title_tool *tool, const struct instance *shape_of,
	const struct instance *at, long s)
{
	float gx = (at->px[s] - shape_of->x - shape_of->dx - LEFT) * GRID - 0.5f;
	float gy = (at->py[s] - shape_of->y - shape_of->dy - TOP) * GRID - 0.5f;

	return field_linear(tool->shapes[shape_of->character].field, gx, gy);
}

static int shapes_build(struct title_tool *tool)
{
	int radius, character, counts[256] = { 0 };
	float *kernel = gaussian_kernel(SMOOTHING * GRID, &radius);
	long i, s;

	if (!kernel)
		return 0;
	for (i = 0; i < tool->instance_count; i++)
		counts[tool->instances[i].character]++;
	for (character = 0; character < 256; character++)
	{
		if (!counts[character])
			continue;
		tool->shapes[character].field = malloc(FIELD_WIDTH * FIELD_HEIGHT * sizeof(float));
		if (!tool->shapes[character].field)
		{
			free(kernel);
			return 0;
		}
		shape_build(tool, character, kernel, radius);
		if (counts[character] > 1)
		{
			float *half = half_table(tool->shapes[character].field);

			for (i = 0; i < tool->instance_count; i++)
			{
				if (tool->instances[i].character == character)
					instance_register(&tool->instances[i], tool->shapes[character].field, half);
			}
			free(half);
			shape_build(tool, character, kernel, radius);
		}
	}
	/* (the samples a neighbor covers more than the letter itself) */
	for (i = 0; i < tool->instance_count; i++)
	{
		struct instance *instance = &tool->instances[i];

		for (s = 0; s < instance->count; s++)
		{
			float neighbor = 0, own = instance_cover(tool, instance, instance, s);
			int side;

			for (side = -1; side <= 1; side += 2)
			{
				long other = i + side;

				if (other >= 0 && other < tool->instance_count && tool->instances[other].title == instance->title)
				{
					float cover = instance_cover(tool, &tool->instances[other], instance, s);

					neighbor = cover > neighbor ? cover : neighbor;
				}
			}
			instance->keep[s] = !(neighbor > NEIGHBOR && neighbor > own);
		}
	}
	for (character = 0; character < 256; character++)
	{
		if (counts[character])
			shape_build(tool, character, kernel, radius);
	}
	/* (and, where there are enough instances to tell, those far from the
	shape) */
	for (character = 0; character < 256; character++)
	{
		if (counts[character] < 3)
			continue;
		for (i = 0; i < tool->instance_count; i++)
		{
			struct instance *instance = &tool->instances[i];

			if (instance->character != character)
				continue;
			for (s = 0; s < instance->count; s++)
			{
				if (fabsf(instance_cover(tool, instance, instance, s) - instance->value[s]) >= OUTLIER)
					instance->keep[s] = 0;
			}
		}
		shape_build(tool, character, kernel, radius);
	}
	free(kernel);
	return 1;
}

/* ---------- the tool */

static int instances_add(struct title_tool *tool, long title, const struct title_tool_title *entry)
{
	struct picture *picture = &tool->pictures[title];
	const char *text = entry->text;
	long letters = 0, k, seen, length = (long)strlen(text);
	struct instance *grown;

	for (k = 0; k < length; k++)
		letters += text[k] != ' ';
	grown = realloc(tool->instances, (size_t)(tool->instance_count + letters) * sizeof(*grown));
	if (!grown)
		return 0;
	tool->instances = grown;
	picture->first = tool->instance_count;
	picture->letters = letters;
	picture->spaced = calloc((size_t)(letters > 0 ? letters : 1), 1);
	if (!picture->spaced)
		return 0;
	for (k = 0, seen = -1; k < length; k++)
	{
		if (text[k] != ' ')
			seen++;
		else if (seen >= 0 && seen < letters)
			picture->spaced[seen] = 1;
	}
	for (k = 0; k < letters; k++)
	{
		struct instance *instance = &tool->instances[tool->instance_count];
		float x = entry->lefts[k], low = x - 0.75f;
		float high = k + 1 < letters ? entry->lefts[k + 1] - 0.25f : x + 40;
		int row0 = (int)picture->baseline + TOP, column0 = (int)floorf(low), column1 = (int)ceilf(high);
		int row, column, character = 0;
		long letter = -1, at;

		for (at = 0; at < length; at++)
		{
			if (text[at] != ' ' && ++letter == k)
			{
				character = (unsigned char)text[at];
				break;
			}
		}
		memset(instance, 0, sizeof(*instance));
		instance->title = title;
		instance->character = character;
		instance->x = x;
		instance->y = picture->baseline;
		instance->px = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->py = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->value = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->keep = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1));
		tool->instance_count++;
		if (!instance->px || !instance->py || !instance->value || !instance->keep)
			return 0;
		for (row = row0; row < row0 + HEIGHT; row++)
		{
			if (row < 0 || row >= picture->height)
				continue;
			for (column = column0; column <= column1; column++)
			{
				if (column < 0 || column >= picture->width || column + 0.5f < low || column + 0.5f > high)
					continue;
				instance->px[instance->count] = (float)column + 0.5f;
				instance->py[instance->count] = (float)row + 0.5f;
				instance->value[instance->count] = picture->coverage[row * picture->width + column];
				instance->keep[instance->count] = 1;
				instance->count++;
			}
		}
	}
	return 1;
}


/* (title_tool_set) a shape's ink's columns: where its field reaches 0.5 */
static void shape_measure(struct shape *shape)
{
	int x, y, left = -1, right = -1;

	for (x = 0; x < FIELD_WIDTH; x++)
	{
		for (y = 0; y < FIELD_HEIGHT; y++)
		{
			if (shape->field[y * FIELD_WIDTH + x] >= 0.5f)
			{
				if (left < 0)
					left = x;
				right = x;
				break;
			}
		}
	}
	shape->ink_left = left < 0 ? 0 : (float)left / GRID + LEFT;
	shape->ink_right = right < 0 ? 0 : ((float)right + 1) / GRID + LEFT;
}

/* (title_tool_set) X, which no title has, made of Y's: Y's arms down to
where they meet, stretched to the middle of the capitals' height, and the
same mirrored under it (as port/linux/game/menu_words.c makes the main
menu's X); FALSE if there is no Y or its arms cannot be told */
static int letter_x_make(struct title_tool *tool)
{
	const float *y_field = tool->shapes['Y'].field;
	float *field;
	int top = -1, bottom = -1, meet = -1, row, x, seen_two = 0;
	float middle;

	if (!y_field || tool->shapes['X'].field)
		return y_field != NULL;
	/* (the capitals' rows: a 0.5 anywhere; where the arms meet: the first
	row of one run of ink after rows of two) */
	for (row = 0; row < FIELD_HEIGHT; row++)
	{
		int runs = 0, inside = 0;

		for (x = 0; x < FIELD_WIDTH; x++)
		{
			int ink = y_field[row * FIELD_WIDTH + x] >= 0.5f;

			runs += ink && !inside;
			inside = ink;
		}
		if (runs)
		{
			if (top < 0)
				top = row;
			bottom = row;
		}
		if (runs >= 2)
			seen_two = 1;
		else if (runs == 1 && seen_two && meet < 0)
			meet = row;
	}
	if (top < 0 || meet <= top || bottom <= meet)
		return 0;
	field = calloc(FIELD_WIDTH * FIELD_HEIGHT, sizeof(float));
	if (!field)
		return 0;
	middle = (top + bottom) / 2.0f;
	for (row = 0; row < FIELD_HEIGHT; row++)
	{
		/* (the upper half's row this one is, mirrored; then Y's row it is
		made of: above the capitals as it is, the arms stretched) */
		float upper = row <= middle ? (float)row : 2 * middle - (float)row, from;
		int from0;
		float t;

		if (upper < 0)
			continue;
		from = upper <= top ? upper : top + (upper - top) * (meet - top) / (middle - top);
		from0 = (int)floorf(from);
		t = from - (float)from0;
		for (x = 0; x < FIELD_WIDTH; x++)
		{
			float a = from0 >= 0 && from0 < FIELD_HEIGHT ? y_field[from0 * FIELD_WIDTH + x] : 0;
			float b = from0 + 1 >= 0 && from0 + 1 < FIELD_HEIGHT ? y_field[(from0 + 1) * FIELD_WIDTH + x] : 0;

			field[row * FIELD_WIDTH + x] = a + (b - a) * t;
		}
	}
	tool->shapes['X'].field = field;
	shape_measure(&tool->shapes['X']);
	return 1;
}

/* (title_tool_set) the advances, gaps and baseline the titles show */
static void spacing_measure(struct title_tool *tool)
{
	long count = tool->instance_count, i, gaps = 0, spaces = 0, baselines = 0;
	float *gap_values = malloc((size_t)(count > 0 ? count : 1) * sizeof(float));
	float *space_values = malloc((size_t)(count > 0 ? count : 1) * sizeof(float));
	float *pair_values = malloc((size_t)(count > 0 ? count : 1) * sizeof(float));
	float baseline_values[512];
	int first, second;

	memset(tool->pair_advance, 0, sizeof(tool->pair_advance));
	tool->gap = 2;
	tool->space = 8;
	tool->baseline = 0;
	if (!gap_values || !space_values || !pair_values)
	{
		free(gap_values);
		free(space_values);
		free(pair_values);
		return;
	}
	for (i = 0; i < tool->count; i++)
	{
		if (tool->pictures[i].present && baselines < (long)(sizeof(baseline_values) / sizeof(*baseline_values)))
			baseline_values[baselines++] = tool->pictures[i].baseline;
	}
	tool->baseline = median(baseline_values, baselines);
	/* (each title's letters one after another: whether a space is between
	them is its text's) */
	for (i = 0; i < tool->count; i++)
	{
		const struct picture *picture = &tool->pictures[i];
		long k;

		if (!picture->present)
			continue;
		for (k = 0; k + 1 < picture->letters; k++)
		{
			const struct instance *a = &tool->instances[picture->first + k];
			const struct instance *b = &tool->instances[picture->first + k + 1];
			float right = a->x + a->dx + tool->shapes[a->character].ink_right;
			float left = b->x + b->dx + tool->shapes[b->character].ink_left;

			if (picture->spaced && picture->spaced[k])
				space_values[spaces++] = left - right;
			else
				gap_values[gaps++] = left - right;
		}
	}
	if (gaps)
		tool->gap = median(gap_values, gaps);
	if (spaces)
		tool->space = median(space_values, spaces);
	/* (the pairs': each pair the titles have side by side) */
	for (first = 0; first < PAIR_COUNT; first++)
	{
		for (second = 0; second < PAIR_COUNT; second++)
		{
			long values = 0;

			for (i = 0; i < tool->count; i++)
			{
				const struct picture *picture = &tool->pictures[i];
				long k;

				if (!picture->present)
					continue;
				for (k = 0; k + 1 < picture->letters; k++)
				{
					const struct instance *a = &tool->instances[picture->first + k];
					const struct instance *b = &tool->instances[picture->first + k + 1];

					if ((!picture->spaced || !picture->spaced[k]) && a->character == first + PAIR_FIRST &&
						b->character == second + PAIR_FIRST)
					{
						pair_values[values++] = (b->x + b->dx) - (a->x + a->dx);
					}
				}
			}
			if (values)
				tool->pair_advance[first][second] = median(pair_values, values);
		}
	}
	free(gap_values);
	free(space_values);
	free(pair_values);
}

struct title_tool *title_tool_new(const struct title_tool_title *titles, long count, unsigned char **texels,
	const int *widths, const int *heights)
{
	struct title_tool *tool = calloc(1, sizeof(*tool));
	long index, found = 0;
	int character;

	if (!tool || !(tool->pictures = calloc((size_t)(count > 0 ? count : 1), sizeof(*tool->pictures))))
	{
		free(tool);
		for (index = 0; index < count; index++)
			free(texels[index]);
		return NULL;
	}
	tool->count = count;
	for (index = 0; index < count; index++)
	{
		struct picture *picture = &tool->pictures[index];

		picture->texels = texels[index];
		texels[index] = NULL;
		picture->width = widths[index];
		picture->height = heights[index];
		if (!picture->texels || !picture_read(picture) || !instances_add(tool, index, &titles[index]))
			continue;
		picture->present = 1;
		found++;
	}
	if (!found || !shapes_build(tool))
	{
		title_tool_free(tool);
		return NULL;
	}
	for (character = 0; character < 256; character++)
	{
		if (tool->shapes[character].field)
			shape_measure(&tool->shapes[character]);
	}
	spacing_measure(tool);
	letter_x_make(tool);
	return tool;
}

int title_tool_has(const struct title_tool *tool, long index)
{
	return tool && index >= 0 && index < tool->count && tool->pictures[index].present;
}

unsigned char *title_tool_copy(const struct title_tool *tool, long index)
{
	const struct picture *picture;
	unsigned char *copy;

	if (!title_tool_has(tool, index))
		return NULL;
	picture = &tool->pictures[index];
	copy = malloc((size_t)picture->width * (size_t)picture->height * 4);
	if (copy)
		memcpy(copy, picture->texels, (size_t)picture->width * (size_t)picture->height * 4);
	return copy;
}

/* ---------- drawing a title */

/* the picture with its old text (and a texel around it) taken from the
nearest texels not in it, then softened (tools/title_assets.py's fill_in) */
static float *plate(const struct picture *picture)
{
	int width = picture->width, height = picture->height, x, y, c, r;
	long texels = (long)width * height, i;
	unsigned char *edge = malloc((size_t)texels);
	float *filled = malloc((size_t)texels * 4 * sizeof(float)), *result = malloc((size_t)texels * 4 * sizeof(float));

	if (!edge || !filled || !result)
	{
		free(edge);
		free(filled);
		free(result);
		return NULL;
	}
	morphology(picture->mask, edge, width, height, 1);
	for (i = 0; i < texels * 4; i++)
		filled[i] = result[i] = picture->texels[i];
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			long best = -1, best_distance = 0;

			if (!edge[y * width + x])
				continue;
			/* (the nearest texel not in it: searched in growing squares,
			then the one found checked against the rest of its circle) */
			for (r = 1; r < 64 && (best < 0 || (long)(r - 1) * (r - 1) <= best_distance); r++)
			{
				int dx, dy;

				for (dy = -r; dy <= r; dy++)
				{
					for (dx = -r; dx <= r; dx++)
					{
						int sx = x + dx, sy = y + dy;
						long distance = (long)dx * dx + (long)dy * dy;

						if ((dx != -r && dx != r && dy != -r && dy != r) || sx < 0 || sy < 0 || sx >= width ||
							sy >= height || edge[sy * width + sx])
							continue;
						if (best < 0 || distance < best_distance)
						{
							best = (long)sy * width + sx;
							best_distance = distance;
						}
					}
				}
			}
			for (c = 0; c < 4; c++)
				filled[((long)y * width + x) * 4 + c] = best >= 0 ? picture->texels[best * 4 + c] : 0;
		}
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (!edge[y * width + x])
				continue;
			for (c = 0; c < 4; c++)
			{
				float sum = 0;
				int dx, dy;

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						int sx = x + dx < 0 ? 0 : x + dx >= width ? width - 1 : x + dx;
						int sy = y + dy < 0 ? 0 : y + dy >= height ? height - 1 : y + dy;

						sum += filled[((long)sy * width + sx) * 4 + c];
					}
				}
				result[((long)y * width + x) * 4 + c] = sum / 9;
			}
		}
	}
	free(edge);
	free(filled);
	return result;
}

/* transparent pixels given the color of the nearest covered one in their
row or column, so that filtering at the edges blends in no black
(tools/title_assets.py's bleed, near enough) */
static void bleed(unsigned char *rgba, int width, int height)
{
	int x, y, pass;

	for (pass = 0; pass < 2; pass++)
	{
		int lines = pass ? width : height, length = pass ? height : width, line;

		for (line = 0; line < lines; line++)
		{
			int last = -1, at;

			for (at = 0; at < length; at++)
			{
				x = pass ? line : at;
				y = pass ? at : line;
				if (rgba[((size_t)y * (size_t)width + (size_t)x) * 4 + 3])
				{
					int fill;

					for (fill = last + 1; fill < at; fill++)
					{
						int fx = pass ? line : fill, fy = pass ? fill : line;
						int nearer = last >= 0 && fill - last < at - fill ? last : at;
						int nx = pass ? line : nearer, ny = pass ? nearer : line;

						memcpy(rgba + ((size_t)fy * (size_t)width + (size_t)fx) * 4,
							rgba + ((size_t)ny * (size_t)width + (size_t)nx) * 4, 3);
					}
					last = at;
				}
			}
			if (last >= 0)
			{
				for (at = last + 1; at < length; at++)
				{
					int fx = pass ? line : at, fy = pass ? at : line, nx = pass ? line : last, ny = pass ? last : line;

					memcpy(rgba + ((size_t)fy * (size_t)width + (size_t)fx) * 4,
						rgba + ((size_t)ny * (size_t)width + (size_t)nx) * 4, 3);
				}
			}
		}
	}
}


/* ---------- setting a word (this port) */

/* where each letter of text is set: its character and where it starts
(x, on the baseline), at most letters_max; the number set, -1 if a letter
has no shape */
static long word_layout(const struct title_tool *tool, const char *text, float left, int *characters, float *xs,
	long letters_max)
{
	long count = 0;
	int previous = -1, spaced = 0;
	float x = left;
	const char *at;

	for (at = text; *at; at++)
	{
		int character = (unsigned char)*at;
		const struct shape *shape;

		if (character == ' ')
		{
			spaced = previous >= 0;
			continue;
		}
		shape = &tool->shapes[character];
		if (!shape->field || count >= letters_max)
			return -1;
		if (previous >= 0)
		{
			const struct shape *before = &tool->shapes[previous];
			float pair = !spaced && previous >= PAIR_FIRST && previous < PAIR_FIRST + PAIR_COUNT &&
				character >= PAIR_FIRST && character < PAIR_FIRST + PAIR_COUNT ?
				tool->pair_advance[previous - PAIR_FIRST][character - PAIR_FIRST] : 0;

			x = pair > 0 ? x + pair :
				x + before->ink_right + (spaced ? tool->space : tool->gap) - shape->ink_left;
		}
		characters[count] = character;
		xs[count] = x;
		count++;
		previous = character;
		spaced = 0;
	}
	return count;
}

/* the word's coverage: each letter's shape where it is set, sharpened to
an edge a pixel wide (ChupathingyCE's letters_draw, at the titles' own
size) */
static float *word_draw(const struct title_tool *tool, const int *characters, const float *xs, long letters,
	float baseline, int width, int height)
{
	float *coverage = calloc((size_t)width * (size_t)height, sizeof(float));
	long k;

	if (!coverage)
		return NULL;
	for (k = 0; k < letters; k++)
	{
		const float *field = tool->shapes[characters[k]].field;
		float x = xs[k], y = baseline;
		int x0 = (int)(x + LEFT), x1 = (int)(x + LEFT + WIDTH), y0 = (int)(y + TOP), y1 = (int)(y + TOP + HEIGHT), px, py;

		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > width ? width : x1;
		y1 = y1 > height ? height : y1;
		for (py = y0; py < y1; py++)
		{
			float gy = ((float)py + 0.5f - y - TOP) * GRID - 0.5f;

			for (px = x0; px < x1; px++)
			{
				float gx = ((float)px + 0.5f - x - LEFT) * GRID - 0.5f;
				float value = field_cubic(field, gx, gy), gradient, alpha;
				float ddx = (field_linear(field, gx + 1, gy) - field_linear(field, gx - 1, gy)) / 2;
				float ddy = (field_linear(field, gx, gy + 1) - field_linear(field, gx, gy - 1)) / 2;
				float *out = &coverage[(size_t)py * (size_t)width + (size_t)px];

				if (value < 0.02f && *out >= 1)
					continue;
				gradient = sqrtf(ddx * ddx + ddy * ddy) * GRID;
				alpha = 0.5f + (value - 0.5f) / (gradient > 0.3f ? gradient : 0.3f);
				alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
				if (alpha > *out)
					*out = alpha;
			}
		}
	}
	return coverage;
}

/* a title's text's ink's span, from its letters as registered */
static void title_span(const struct title_tool *tool, const struct picture *picture, float *left, float *right)
{
	const struct instance *first = &tool->instances[picture->first];
	const struct instance *last = &tool->instances[picture->first + picture->letters - 1];

	*left = first->x + first->dx + tool->shapes[first->character].ink_left;
	*right = last->x + last->dx + tool->shapes[last->character].ink_right;
}

unsigned char *title_tool_set(const struct title_tool *tool, const char *text, int width, int height, float left)
{
	int characters[64];
	float xs[64], word_left, word_right, best_left = 0, best_right = 0, best_difference = 0;
	long letters, index, best = -1, i;
	const struct picture *reference;
	float *base, *letters_coverage;
	unsigned char *result;
	int x, y, c;

	if (!tool || !text || width <= 0 || height <= 0)
		return NULL;
	letters = word_layout(tool, text, left, characters, xs, (long)(sizeof(xs) / sizeof(*xs)));
	if (letters <= 0)
		return NULL;
	word_left = xs[0] + tool->shapes[characters[0]].ink_left;
	word_right = xs[letters - 1] + tool->shapes[characters[letters - 1]].ink_right;
	/* (a word that runs past its picture is not set) */
	if (word_left < 1 || word_right > (float)width - 2 || tool->baseline + TOP < 0 || tool->baseline > (float)height)
		return NULL;
	/* (the plate: the title of the same size whose text is nearest the
	word's width) */
	for (index = 0; index < tool->count; index++)
	{
		const struct picture *picture = &tool->pictures[index];
		float span_left, span_right, difference;

		if (!picture->present || picture->width != width || picture->height != height || picture->letters < 1)
			continue;
		title_span(tool, picture, &span_left, &span_right);
		difference = fabsf((span_right - span_left) - (word_right - word_left));
		if (best < 0 || difference < best_difference)
		{
			best = index;
			best_difference = difference;
			best_left = span_left;
			best_right = span_right;
		}
	}
	if (best < 0)
		return NULL;
	reference = &tool->pictures[best];
	if (!reference->plate)
		((struct picture *)reference)->plate = plate(reference);
	base = reference->plate;
	letters_coverage = base ? word_draw(tool, characters, xs, letters, tool->baseline, width, height) : NULL;
	result = letters_coverage ? malloc((size_t)width * (size_t)height * 4) : NULL;
	if (!result)
	{
		free(letters_coverage);
		return NULL;
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			/* (the plate's column this one is: as it is before the text, the
			text's span stretched to the word's, after it moved with its end) */
			float from = (float)x + 0.5f, back, cover, alpha, plate_texel[4];
			int from0;
			float t;

			if (from <= word_left)
				from = from - word_left + best_left;
			else if (from < word_right)
				from = best_left + (from - word_left) * (best_right - best_left) / (word_right - word_left);
			else
				from = from - word_right + best_right;
			from -= 0.5f;
			from0 = floor_int(from);
			t = from - (float)from0;
			for (c = 0; c < 4; c++)
			{
				float a = from0 >= 0 && from0 < width ? base[((size_t)y * (size_t)width + (size_t)from0) * 4 + c] : 0;
				float b = from0 + 1 >= 0 && from0 + 1 < width ?
					base[((size_t)y * (size_t)width + (size_t)(from0 + 1)) * 4 + c] : 0;

				plate_texel[c] = a + (b - a) * t;
			}
			i = (long)y * width + x;
			cover = letters_coverage[i] * reference->opacity;
			back = plate_texel[3] / 255;
			alpha = cover + back * (1 - cover);
			for (c = 0; c < 3; c++)
			{
				float v = alpha > 1e-4f ? (reference->color[c] * cover + plate_texel[c] * back * (1 - cover)) / alpha : 0;

				result[i * 4 + c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
			}
			result[i * 4 + 3] = (unsigned char)(alpha * 255 + 0.5f);
		}
	}
	free(letters_coverage);
	bleed(result, width, height);
	return result;
}

void title_tool_free(struct title_tool *tool)
{
	long i;

	if (!tool)
		return;
	for (i = 0; i < tool->count; i++)
	{
		free(tool->pictures[i].texels);
		free(tool->pictures[i].mask);
		free(tool->pictures[i].coverage);
		free(tool->pictures[i].spaced);
		free(tool->pictures[i].plate);
	}
	for (i = 0; i < tool->instance_count; i++)
	{
		free(tool->instances[i].px);
		free(tool->instances[i].py);
		free(tool->instances[i].value);
		free(tool->instances[i].keep);
	}
	for (i = 0; i < 256; i++)
		free(tool->shapes[i].field);
	free(tool->pictures);
	free(tool->instances);
	free(tool);
}

void title_tool_picture_free(unsigned char *picture)
{
	free(picture);
}
