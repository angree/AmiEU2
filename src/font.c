/* font.c - see font.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "font.h"
#include "eui.h"

/* The game's own faces are not used on screen (user decision 2026-11-09: unreadable on the Amiga, and
 * the next ones too wide); every face is a proportional copy of the X.Org misc-fixed bitmap fonts
 * (public domain, full Latin-1) made by tools/mkfont/bdf2euf.py: at 640x480 6x9 for the texts, 6x10
 * for titles and bold, 5x8 when a line does not fit (user: smaller, 2026-11-09); at 800x600 6x13 and 7x13B. */
static const char *g_cc0_dir;
static int g_cc0_big;
void font_set_cc0(const char *dir, int big) { g_cc0_dir = dir; g_cc0_big = big; }

static const char *cc0_face(const char *name)
{
	if (!strcmp(name, "arial8") || !strcmp(name, "arial10") || !strcmp(name, "garamond12") || !strcmp(name, "garamond12bold"))
		return g_cc0_big ? "fx6x13" : "fx6x9";
	if (!strcmp(name, "garamond14")) return g_cc0_big ? "fx6x13" : "fx6x9";
	return g_cc0_big ? "fx7x13b" : "fx6x10";               /* 14 / 18 / 20 / 24 bold: 640x480 the next size up, no bold (it widens) */
}

int font_load(Font *f, Pak *gfx, const char *name)
{
	char path[64];
	unsigned long sz;
	unsigned char *buf;
	EuiImage im;
	int c, x, y;
	memset(f, 0, sizeof *f);
	if (g_cc0_dir) {
		snprintf(path, sizeof path, "%s%s.euf", g_cc0_dir, cc0_face(name));
		if (font_load_euf(f, path) == 0) return 0;
	}
	snprintf(path, sizeof path, "gfx/fonts/%s.bmp", name);
	if (!(buf = pak_load(gfx, path, &sz))) return -1;
	if (eui_decode(buf, sz, &im) || im.format != 1) { free(buf); return -2; }
	free(buf);
	f->cell = im.w;
	f->first = 33;
	f->count = im.h / im.w;
	if (f->count > 256 - 33) f->count = 256 - 33;
	f->bits = calloc((size_t)f->count * f->cell * f->cell, 1);
	if (!f->bits) { eui_free(&im); return -3; }
	for (c = 0; c < f->count; c++) {
		int adv = 1;
		for (y = 0; y < f->cell; y++)
			for (x = 0; x < f->cell; x++) {
				unsigned char p = im.pix[(c * f->cell + y) * im.w + x];
				if (p == 1 || p == 0) f->bits[(c * f->cell + y) * f->cell + x] = 1;
				if ((p == 1 || p == 0) && x + 2 > adv) adv = x + 2;   /* ink + 1 px gap */
			}
		f->adv[f->first + c] = (unsigned char)adv;
	}
	f->adv[' '] = (unsigned char)(f->cell / 3 > 2 ? f->cell / 3 : 3);
	eui_free(&im);
	return 0;
}

void font_free(Font *f) { free(f->bits); f->bits = NULL; }

/* our own faces for 640x480 (fonts/NAME.euf from tools/mkfont: EB Garamond, OFL) */
int font_load_euf(Font *f, const char *path)
{
	FILE *fp = fopen(path, "rb");
	unsigned char h[8];
	int k;
	memset(f, 0, sizeof *f);
	if (!fp) return -1;
	if (fread(h, 1, 8, fp) != 8 || memcmp(h, "EUF1", 4)) { fclose(fp); return -2; }
	f->cell = h[4]; f->first = h[5]; f->count = h[6];
	if (f->cell < 4 || f->first + f->count > 256 || !(f->bits = malloc((size_t)f->count * f->cell * f->cell))) { fclose(fp); return -3; }
	if (fread(f->adv + f->first, 1, (size_t)f->count, fp) != (size_t)f->count ||
	    fread(f->bits, 1, (size_t)f->count * f->cell * f->cell, fp) != (size_t)f->count * f->cell * f->cell) { fclose(fp); font_free(f); return -4; }
	fclose(fp);
	for (k = 0; k < f->first; k++) f->adv[k] = 0;
	return 0;
}

/* the game texts' colour codes: 0xA7 'Y' (yellow) and 0xA7 'W' (white) - text.csv uses only these two; they take no room */
static int g_pen_y = -1, g_pen_w = -1;
void font_set_code_pens(int yellow, int white) { g_pen_y = yellow; g_pen_w = white; }
static int is_code(const char *s) { return (unsigned char)s[0] == 0xA7 && (s[1] == 'Y' || s[1] == 'W'); }

int font_width(const Font *f, const char *s)
{
	int w = 0;
	for (; *s; s++) { if (is_code(s)) { s++; continue; } w += f->adv[(unsigned char)*s]; }
	return w;
}

#define FP_N 200
static struct { short x, y, w, h; char s[40]; } g_fp[FP_N];
static int g_fpn = -1;
void font_probe_begin(void) { g_fpn = 0; }
int font_probe_report(const char *tag, int x0, int x1, int (*right)(int, int), void (*log)(const char *))
{
	int i, j, n = 0;
	char b[200];
	for (i = 0; i < g_fpn; i++) {
		int r;
		if (g_fp[i].x >= x1 || g_fp[i].x + g_fp[i].w <= x0) continue;          /* not in this panel */
		r = right ? right(g_fp[i].y, g_fp[i].y + g_fp[i].h) : x1;              /* where the frame starts on these rows */
		if (g_fp[i].x < x0 + 2 || g_fp[i].x + g_fp[i].w > r) {
			snprintf(b, sizeof b, "textprobe %s: '%s' at %d,%d w %d crosses the panel (%d..%d)", tag, g_fp[i].s, g_fp[i].x, g_fp[i].y, g_fp[i].w, x0, r); log(b); n++;
		}
		for (j = i + 1; j < g_fpn; j++) {
			int ox = (g_fp[i].x + g_fp[i].w < g_fp[j].x + g_fp[j].w ? g_fp[i].x + g_fp[i].w : g_fp[j].x + g_fp[j].w) - (g_fp[i].x > g_fp[j].x ? g_fp[i].x : g_fp[j].x);
			int oy = (g_fp[i].y + g_fp[i].h < g_fp[j].y + g_fp[j].h ? g_fp[i].y + g_fp[i].h : g_fp[j].y + g_fp[j].h) - (g_fp[i].y > g_fp[j].y ? g_fp[i].y : g_fp[j].y);
			if (ox > 1 && oy > 2 && strcmp(g_fp[i].s, g_fp[j].s)) {
				snprintf(b, sizeof b, "textprobe %s: '%s' (%d,%d w %d) overlaps '%s' (%d,%d w %d)", tag, g_fp[i].s, g_fp[i].x, g_fp[i].y, g_fp[i].w, g_fp[j].s, g_fp[j].x, g_fp[j].y, g_fp[j].w); log(b); n++;
			}
		}
	}
	g_fpn = -1;
	return n;
}

int font_draw(const Font *f, unsigned char *dst, int pitch, int w, int h, int x, int y, const char *s, int pen)
{
	if (g_fpn >= 0 && g_fpn < FP_N && *s) {
		int k;
		g_fp[g_fpn].x = (short)x; g_fp[g_fpn].y = (short)y; g_fp[g_fpn].w = (short)font_width(f, s); g_fp[g_fpn].h = (short)f->cell;
		for (k = 0; k < 39 && s[k]; k++) g_fp[g_fpn].s[k] = s[k];
		g_fp[g_fpn].s[k] = 0; g_fpn++;
	}
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		int c = ch - f->first, gx, gy;
		if (is_code(s)) {
			int np = s[1] == 'Y' ? g_pen_y : g_pen_w;
			if (np >= 0) pen = np;
			s++;
			continue;
		}
		if (c >= 0 && c < f->count) {
			const unsigned char *g = f->bits + (long)c * f->cell * f->cell;
			for (gy = 0; gy < f->cell; gy++) {
				int yy = y + gy;
				if (yy < 0 || yy >= h) continue;
				for (gx = 0; gx < f->cell; gx++) {
					int xx = x + gx;
					if (g[gy * f->cell + gx] && xx >= 0 && xx < w) dst[(long)yy * pitch + xx] = (unsigned char)pen;
				}
			}
		}
		x += f->adv[ch];
	}
	return x;
}
