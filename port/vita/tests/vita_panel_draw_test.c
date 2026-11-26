/*
VITA_PANEL_DRAW_TEST.C

A desktop test of how the settings panel is drawn (port/vita/host/
vita_gxm.c menu_build, included whole; nothing of GXM is called): the
panel's text as vita_settings.c hands it to the renderer (vgxm_menu_set),
written by vita_settings_test (VITA_SETTINGS_PANEL_TEXT) as FOLDER/<look>-
<language>.txt - the chosen line's index, then the text -, drawn into the
overlay's rectangles and the Halo 3 fonts' quads, and those painted into a
960x544 picture, as the Vita's overlay and text programs blend them, saved
as FOLDER/<look>-<language>.png.

The checks (1.1.3, the Custom Edition files missing, Game > Menus' Menu
style chosen - device test, Nov 25):
  eldewrito-*  the ElDewrito panel (online_panel) draws the choices the
               row greys out ('\x07' each) after its value, dimmer than it,
               inside the column: it drew the value alone
  classic-*    the classic panel draws its help line inside the panel (its
               rule's right end): it ran off the right edge; its greyed
               choices inside the panel too

Run by run_vita_settings_test.sh:
  vita_panel_draw_test TEXT_FOLDER FONT_FOLDER (app0/menus/halo3)
*/

/* (vita_shader_generator_id.h: the run script's, its id not used here) */
#include "../host/vita_gxm.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- what menu_build's callers reach of the Vita's, never called
here (the fonts read by fonts_read, h3.state set) */
int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr)
{
	(void)base, (void)size, (void)attr;
	return -1;
}
int sceGxmTextureInitLinear(SceGxmTexture *texture, const void *data, SceGxmTextureFormat format, unsigned int width,
	unsigned int height, unsigned int mipmaps)
{
	(void)texture, (void)data, (void)format, (void)width, (void)height, (void)mipmaps;
	return -1;
}
int sceGxmTextureSetMinFilter(SceGxmTexture *texture, SceGxmTextureFilter filter)
{
	(void)texture, (void)filter;
	return -1;
}
int sceGxmTextureSetMagFilter(SceGxmTexture *texture, SceGxmTextureFilter filter)
{
	(void)texture, (void)filter;
	return -1;
}
SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
	(void)file, (void)flags, (void)mode;
	return -1;
}
int sceIoClose(SceUID fd)
{
	(void)fd;
	return -1;
}
SceSSize sceIoRead(SceUID fd, void *buffer, SceSize size)
{
	(void)fd, (void)buffer, (void)size;
	return -1;
}
int sceIoGetstat(const char *file, SceIoStat *stat)
{
	(void)file, (void)stat;
	return -1;
}
SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
	(void)name, (void)type, (void)size, (void)opt;
	return -1;
}
int sceKernelFreeMemBlock(SceUID uid)
{
	(void)uid;
	return -1;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
	(void)uid, (void)base;
	return -1;
}
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info)
{
	(void)info;
	return -1;
}
void vita_host_log(const char *line)
{
	printf("%s\n", line);
}

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static unsigned char *read_file(const char *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data)
		data[length] = 0;
	*size = (unsigned long)length;
	return data;
}

/* the Halo 3 fonts read as h3_ready reads them (its texture apart) */
static int fonts_read(const char *folder)
{
	static const char *const names[3] = { "overlay_title.bin", "overlay_item.bin", "overlay_small.bin" };
	struct overlay_face *faces[3] = { &h3.title, &h3.item, &h3.small };
	int index;

	if (overlay_atlas_init(&h3.atlas, H3_ATLAS_WIDTH, H3_ATLAS_HEIGHT) < 0)
		return 0;
	for (index = 0; index < 3; index++)
	{
		char path[1024];
		unsigned long size;
		unsigned char *data;
		int result;

		snprintf(path, sizeof(path), "%s/%s", folder, names[index]);
		data = read_file(path, &size);
		result = data ? overlay_face_read(faces[index], &h3.atlas, data, size) : -1;
		free(data);
		if (result < 0)
			return 0;
	}
	h3.atlas.height = (h3.atlas.used_height + 7) & ~7;
	h3.built = malloc(H3_GLYPHS * 6 * sizeof(*h3.built));
	h3.state = 1;
	return h3.built != NULL;
}

/* ---------- the picture */

static float picture[DISPLAY_HEIGHT][DISPLAY_WIDTH][3];

static float to_x(float x) { return (x + 1.0f) * 0.5f * DISPLAY_WIDTH; }
static float to_y(float y) { return (1.0f - y) * 0.5f * DISPLAY_HEIGHT; }

/* a quad's rectangle on the display, its corners' colours (top left, top
right, bottom left, bottom right) blended in by how much of each pixel it
covers; a texture's coverage too (the text program: the atlas's value is
its alpha) */
static void paint(float x0, float y0, float x1, float y1, const unsigned char *colors[4], float u0, float v0, float u1,
	float v1, int textured)
{
	int px, py;

	for (py = (int)y0; py < (int)(y1 + 1.0f) && py < DISPLAY_HEIGHT; py++)
		for (px = (int)x0; px < (int)(x1 + 1.0f) && px < DISPLAY_WIDTH; px++)
		{
			float cover_x = (px + 1.0f < x1 ? px + 1.0f : x1) - (px > x0 ? px : x0);
			float cover_y = (py + 1.0f < y1 ? py + 1.0f : y1) - (py > y0 ? py : y0);
			float fx = x1 > x0 ? (px + 0.5f - x0) / (x1 - x0) : 0.0f, fy = y1 > y0 ? (py + 0.5f - y0) / (y1 - y0) : 0.0f;
			float color[4], alpha;
			int channel;

			if (px < 0 || py < 0 || cover_x <= 0.0f || cover_y <= 0.0f)
				continue;
			fx = fx < 0.0f ? 0.0f : fx > 1.0f ? 1.0f : fx;
			fy = fy < 0.0f ? 0.0f : fy > 1.0f ? 1.0f : fy;
			for (channel = 0; channel < 4; channel++)
				color[channel] = ((colors[0][channel] * (1.0f - fx) + colors[1][channel] * fx) * (1.0f - fy) +
					(colors[2][channel] * (1.0f - fx) + colors[3][channel] * fx) * fy) / 255.0f;
			alpha = color[3] * cover_x * cover_y;
			if (textured)
			{
				int u = (int)((u0 + (u1 - u0) * fx) * h3.atlas.width), v = (int)((v0 + (v1 - v0) * fy) * h3.atlas.height);

				u = u < 0 ? 0 : u >= h3.atlas.width ? h3.atlas.width - 1 : u;
				v = v < 0 ? 0 : v >= h3.atlas.height ? h3.atlas.height - 1 : v;
				alpha *= h3.atlas.pixels[v * h3.atlas.width + u] / 255.0f;
			}
			for (channel = 0; channel < 3; channel++)
				picture[py][px][channel] = color[channel] * alpha + picture[py][px][channel] * (1.0f - alpha);
		}
}

/* the overlay's rectangles (6 vertices each: 0 top left, 1 top right, 2
bottom left, 4 bottom right), then the text over them (h3_draw's pass) */
static void paint_all(const struct overlay_vertex *vertices, unsigned int count)
{
	unsigned int index;
	int x, y;

	/* (the game behind: a dark blue) */
	for (y = 0; y < DISPLAY_HEIGHT; y++)
		for (x = 0; x < DISPLAY_WIDTH; x++)
		{
			picture[y][x][0] = 0.06f;
			picture[y][x][1] = 0.10f + 0.06f * y / DISPLAY_HEIGHT;
			picture[y][x][2] = 0.16f + 0.10f * y / DISPLAY_HEIGHT;
		}
	for (index = 0; index < count; index++)
	{
		const struct overlay_vertex *v = vertices + index * 6;
		const unsigned char *colors[4] = { v[0].color, v[1].color, v[2].color, v[4].color };

		paint(to_x(v[0].x), to_y(v[0].y), to_x(v[4].x), to_y(v[4].y), colors, 0, 0, 0, 0, 0);
	}
	for (index = 0; index < h3.count; index++)
	{
		const struct text_vertex *v = h3.built + index * 6;
		const unsigned char *colors[4] = { v[0].color, v[0].color, v[0].color, v[0].color };

		paint(to_x(v[0].x), to_y(v[0].y), to_x(v[4].x), to_y(v[4].y), colors, v[0].u, v[0].v, v[4].u, v[4].v, 1);
	}
}

/* (a PNG of stored deflate blocks: no library) */
static unsigned long crc_table[256];

static unsigned long crc_of(unsigned long crc, const unsigned char *data, size_t size)
{
	size_t index;

	if (!crc_table[1])
	{
		unsigned long n, k, c;

		for (n = 0; n < 256; n++)
		{
			for (c = n, k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
			crc_table[n] = c;
		}
	}
	crc ^= 0xFFFFFFFFUL;
	for (index = 0; index < size; index++)
		crc = crc_table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFUL;
}

static void put32(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)(value >> 24);
	at[1] = (unsigned char)(value >> 16);
	at[2] = (unsigned char)(value >> 8);
	at[3] = (unsigned char)value;
}

static void chunk(FILE *file, const char *type, const unsigned char *data, size_t size)
{
	unsigned char head[8], tail[4];
	unsigned long crc;

	put32(head, (unsigned long)size);
	memcpy(head + 4, type, 4);
	crc = crc_of(0, head + 4, 4);
	crc = crc_of(crc, data, size);
	put32(tail, crc);
	fwrite(head, 1, 8, file);
	fwrite(data, 1, size, file);
	fwrite(tail, 1, 4, file);
}

static int write_png(const char *path)
{
	const size_t row = 1 + DISPLAY_WIDTH * 3, raw_size = row * DISPLAY_HEIGHT;
	const size_t blocks = (raw_size + 65534) / 65535, size = 2 + raw_size + blocks * 5 + 4;
	unsigned char *raw = malloc(raw_size), *zlib = malloc(size), header[13];
	unsigned long a = 1, b = 0;
	size_t index, at = 0;
	FILE *file;
	int x, y;

	if (!raw || !zlib)
		return 0;
	for (y = 0; y < DISPLAY_HEIGHT; y++)
	{
		raw[y * row] = 0;
		for (x = 0; x < DISPLAY_WIDTH * 3; x++)
		{
			float value = picture[y][x / 3][x % 3];

			raw[y * row + 1 + x] = (unsigned char)(value <= 0.0f ? 0 : value >= 1.0f ? 255 : value * 255.0f + 0.5f);
		}
	}
	zlib[at++] = 0x78;
	zlib[at++] = 0x01;
	for (index = 0; index < raw_size; index += 65535)
	{
		size_t length = raw_size - index < 65535 ? raw_size - index : 65535;

		zlib[at++] = index + length == raw_size;
		zlib[at++] = (unsigned char)length;
		zlib[at++] = (unsigned char)(length >> 8);
		zlib[at++] = (unsigned char)~length;
		zlib[at++] = (unsigned char)(~length >> 8);
		memcpy(zlib + at, raw + index, length);
		at += length;
	}
	for (index = 0; index < raw_size; index++)
	{
		a = (a + raw[index]) % 65521;
		b = (b + a) % 65521;
	}
	put32(zlib + at, (b << 16) | a);
	at += 4;
	file = fopen(path, "wb");
	if (file)
	{
		fwrite("\x89PNG\r\n\x1a\n", 1, 8, file);
		put32(header, DISPLAY_WIDTH);
		put32(header + 4, DISPLAY_HEIGHT);
		memcpy(header + 8, "\x08\x02\x00\x00\x00", 5);
		chunk(file, "IHDR", header, 13);
		chunk(file, "IDAT", zlib, at);
		chunk(file, "IEND", NULL, 0);
		fclose(file);
	}
	free(raw);
	free(zlib);
	return file != NULL;
}

/* ---------- the panel drawn */

#define QUADS 8192
static struct overlay_vertex vertices[QUADS * 6];
static unsigned int quads;

/* the look's panel from FOLDER/NAME.txt, drawn and painted into
FOLDER/NAME.png; its chosen line in `chosen` */
static int draw(const char *folder, const char *name, int online, char *chosen, size_t chosen_size)
{
	char path[1024];
	unsigned long size;
	unsigned char *data;
	const char *text, *at;
	int selected, line = 0;

	snprintf(path, sizeof(path), "%s/%s.txt", folder, name);
	data = read_file(path, &size);
	if (!data)
	{
		printf("(no %s)\n", path);
		return 0;
	}
	selected = atoi((const char *)data);
	text = strchr((const char *)data, '\n');
	text = text ? text + 1 : "";
	snprintf(gxm.menu_text[0], sizeof(gxm.menu_text[0]), "%s", text);
	gxm.menu_index = 0;
	gxm.menu_selected = selected;
	gxm.menu_online = online;
	gxm.menu_halo3 = 0;
	/* (the chosen line as menu_build counts them: the diagram's not one) */
	chosen[0] = 0;
	for (at = text; *at; )
	{
		size_t length = strcspn(at, "\n");

		if (at[0] != '\x01' && line++ == selected)
			snprintf(chosen, chosen_size, "%.*s", (int)length, at);
		at += length + (at[length] == '\n');
	}
	free(data);
	h3.count = 0;
	quads = menu_build(vertices, 0, QUADS);
	paint_all(vertices, quads);
	snprintf(path, sizeof(path), "%s/%s.png", folder, name);
	if (!write_png(path))
		printf("(%s not written)\n", path);
	return quads > 0;
}

/* the characters with pixels in UTF-8 text (no spaces): a quad each */
static int drawn_characters(const char *text, size_t bytes)
{
	size_t index;
	int count = 0;

	for (index = 0; index < bytes && text[index]; index++)
		count += ((unsigned char)text[index] & 0xC0) != 0x80 && text[index] != ' ';
	return count;
}

/* the first overlay rectangle of this colour (top left): its top and height */
static int rect_of(uint32_t abgr, float *top, float *height)
{
	unsigned int index;

	for (index = 0; index < quads; index++)
		if (!memcmp(vertices[index * 6].color, &abgr, 4))
		{
			*top = to_y(vertices[index * 6].y);
			*height = to_y(vertices[index * 6 + 4].y) - *top;
			return 1;
		}
	return 0;
}

static void test_eldewrito(const char *folder, const char *language)
{
	const float value_x = ONLINE_LEFT + (ONLINE_RIGHT - ONLINE_LEFT) * 0.54f;
	char name[64], chosen[512], value[96], what[160];
	const char *split, *greyed;
	float top = 0.0f, height = 0.0f, right = 0.0f;
	int left_arrow, right_arrow, wanted_value, wanted_greyed = 0, value_glyphs = 0, other_glyphs = 0, dimmer = 1;
	unsigned int index;

	snprintf(name, sizeof(name), "eldewrito-%s", language);
	snprintf(what, sizeof(what), "%s: the ElDewrito panel drawn, the chosen row on its bar", name);
	check(draw(folder, name, 1, chosen, sizeof(chosen)) && rect_of(ONLINE_BAR, &top, &height) &&
		(split = strchr(chosen, '\x02')) != NULL, what);
	split = strchr(chosen, '\x02');
	if (!split)
		return;
	/* (the value as online_panel shows it; the greyed choices after it) */
	{
		char raw[160];

		snprintf(raw, sizeof(raw), "%.*s", (int)strcspn(split + 1, "\x0b\x07"), split + 1);
		h3_value(value, sizeof(value), raw, &left_arrow, &right_arrow);
	}
	wanted_value = drawn_characters(value, strlen(value));
	for (greyed = strchr(split, '\x07'); greyed; greyed = strchr(greyed + 1, '\x07'))
		wanted_greyed += drawn_characters(greyed + 1, strcspn(greyed + 1, "\x07"));
	printf("(%s: \"%s\", %d characters greyed out after it)\n", name, value, wanted_greyed);
	snprintf(what, sizeof(what), "%s: the chosen row (Menu style) greys choices out", name);
	check(wanted_greyed > 0, what);
	/* (the quads on the row's bar from the value's column on - a glyph
	may start a pixel or two left of its pen -: the "<" is 12 left of it) */
	for (index = 0; index < h3.count; index++)
	{
		const struct text_vertex *v = h3.built + index * 6;
		const float x0 = to_x(v[0].x), x1 = to_x(v[4].x), middle = (to_y(v[0].y) + to_y(v[4].y)) / 2.0f;
		uint32_t color;

		if (middle < top || middle > top + height || x0 < value_x - 4.0f)
			continue;
		memcpy(&color, v[0].color, 4);
		right = x1 > right ? x1 : right;
		if (color == ONLINE_VALUE)
			value_glyphs++;
		else
		{
			other_glyphs++;
			/* (dimmer: less of it seen, or darker) */
			dimmer &= (color >> 24) < (ONLINE_VALUE >> 24) || (color & 0xFFFFFFu) < (ONLINE_VALUE & 0xFFFFFFu);
		}
	}
	printf("(%s: %d of the value's characters drawn, %d others, the last ending at %.0f)\n", name, value_glyphs,
		other_glyphs, right);
	snprintf(what, sizeof(what), "%s: Menu style's value drawn", name);
	check(value_glyphs == wanted_value, what);
	snprintf(what, sizeof(what), "%s: every greyed-out choice drawn after the value (device test, Nov 25: none)", name);
	check(other_glyphs == wanted_greyed, what);
	snprintf(what, sizeof(what), "%s: the greyed-out choices dimmer than the value", name);
	check(other_glyphs > 0 && dimmer, what);
	snprintf(what, sizeof(what), "%s: all of them inside the column, before the >'s place", name);
	check(right <= ONLINE_RIGHT - 34.0f, what);
}

static void test_classic(const char *folder, const char *language)
{
	/* (menu_build's classic panel: 880 wide, centred; its rules end 12 in) */
	const float panel_right = (DISPLAY_WIDTH + 880.0f) / 2.0f - 12.0f;
	const uint32_t help_color = MENU_HELP, info_color = MENU_INFO;
	char name[64], chosen[512], what[160];
	float top = 0.0f, height = 0.0f, help_right = 0.0f, greyed_right = 0.0f;
	int help_rects = 0, greyed_rects = 0;
	unsigned int index;

	snprintf(name, sizeof(name), "classic-%s", language);
	snprintf(what, sizeof(what), "%s: the classic panel drawn, the chosen row on its bar", name);
	check(draw(folder, name, 0, chosen, sizeof(chosen)) && rect_of(MENU_CHOSEN, &top, &height) &&
		strchr(chosen, '\x07') != NULL, what);
	for (index = 0; index < quads; index++)
	{
		const struct overlay_vertex *v = vertices + index * 6;
		const float x1 = to_x(v[4].x), middle = (to_y(v[0].y) + to_y(v[4].y)) / 2.0f;

		if (!memcmp(v[0].color, &help_color, 4))
		{
			help_rects++;
			help_right = x1 > help_right ? x1 : help_right;
		}
		else if (!memcmp(v[0].color, &info_color, 4) && middle >= top && middle <= top + height)
		{
			greyed_rects++;
			greyed_right = x1 > greyed_right ? x1 : greyed_right;
		}
	}
	printf("(%s: the help line ends at %.0f, the greyed-out choices at %.0f, the panel's rule at %.0f)\n", name,
		help_right, greyed_right, panel_right);
	snprintf(what, sizeof(what), "%s: the help line inside the panel (device test, Nov 25: off its right edge)", name);
	check(help_rects > 0 && help_right <= panel_right, what);
	snprintf(what, sizeof(what), "%s: the greyed-out choices drawn, inside the panel", name);
	check(greyed_rects > 0 && greyed_right <= panel_right, what);
}

int main(int argc, char **argv)
{
	static const char *const languages[2] = { "en", "es" };
	int index;

	if (argc < 3)
	{
		printf("usage: vita_panel_draw_test TEXT_FOLDER FONT_FOLDER\n");
		return 2;
	}
	check(fonts_read(argv[2]), "the Halo 3 fonts read (the ElDewrito panel's)");
	if (failures)
		return 1;
	/* (the panel's title, T("Settings"): in the language of the text) */
	lang_init("app0:lang");
	for (index = 0; index < 2; index++)
	{
		lang_select(languages[index]);
		test_eldewrito(argv[1], languages[index]);
		test_classic(argv[1], languages[index]);
	}
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
