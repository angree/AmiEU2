/* mkfont.c - renders a TrueType font into the port's EUF1 bitmap font (used at 640x480, where the
 * game's own faces are too big). Host tool; stb_truetype (public domain) is fetched next to it.
 *   mkfont <font.ttf> <pixel height> <threshold 0..255> <out.euf> [preview.pgm|-] [extra advance (1)] [cell (height+2)]
 * Pixel fonts (Pixel Operator, CC0) at their own size: extra advance 0, threshold 128.
 * EUF1: "EUF1", cell (u8), first (u8 = 32), count (u8), ascent (u8), advances[count] (u8),
 * then count glyphs of cell x cell bytes (0/1), rows top to bottom. Characters 32..255 as Latin-1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

int main(int argc, char **argv)
{
	FILE *f;
	unsigned char *ttf, *bits, adv[224];
	long n;
	stbtt_fontinfo fi;
	float sc;
	int px, thr, ascent, descent, gap, cell, c, first = 32, count = 224;
	if (argc < 5) { fprintf(stderr, "usage: mkfont font.ttf px threshold out.euf [preview.pgm]\n"); return 2; }
	if (!(f = fopen(argv[1], "rb"))) return 1;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	ttf = malloc((size_t)n); if (fread(ttf, 1, (size_t)n, f) != (size_t)n) return 1; fclose(f);
	stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
	px = atoi(argv[2]); thr = atoi(argv[3]);
	sc = stbtt_ScaleForPixelHeight(&fi, (float)px);
	stbtt_GetFontVMetrics(&fi, &ascent, &descent, &gap);
	cell = argc > 7 ? atoi(argv[7]) : px + 2;
	bits = calloc((size_t)count * cell * cell, 1);
	for (c = 0; c < count; c++) {
		int ch = first + c, aw, lsb, x0, y0, x1, y1, w, h, x, y, base = (int)(ascent * sc + 0.5f);
		unsigned char *g;
		if (ch >= 127 && ch < 160) { adv[c] = 0; continue; }
		stbtt_GetCodepointHMetrics(&fi, ch, &aw, &lsb);
		adv[c] = (unsigned char)(aw * sc + 0.5f + (argc > 6 ? atoi(argv[6]) : 1));   /* +1: 1-bit outline glyphs need the air */
		g = stbtt_GetCodepointBitmap(&fi, sc, sc, ch, &w, &h, &x0, &y0);
		(void)x1; (void)y1;
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				int cx = x0 + x + (lsb < 0 ? 1 : 0), cy = base + y0 + y;
				if (cx >= 0 && cx < cell && cy >= 0 && cy < cell && g[y * w + x] >= thr) bits[((long)c * cell + cy) * cell + cx] = 1;
			}
		stbtt_FreeBitmap(g, NULL);
	}
	if (!(f = fopen(argv[4], "wb"))) return 1;
	fwrite("EUF1", 1, 4, f);
	fputc(cell, f); fputc(first, f); fputc(count, f); fputc((int)(ascent * sc + 0.5f), f);
	fwrite(adv, 1, (size_t)count, f);
	fwrite(bits, 1, (size_t)count * cell * cell, f);
	fclose(f);
	if (argc > 5 && strcmp(argv[5], "-")) {                                       /* preview: the alphabet and a sample line */
		static const char *t[3] = { "The Grand Campaign 1419 - France", "Land Technology  Stability  Treasury", "Raise troops here. Zuerich, Koeln, Malmoe" };
		int W = 420, H = 3 * (cell + 4), l, X;
		unsigned char *img = calloc((size_t)W * H, 1);
		for (l = 0; l < 3; l++) {
			const char *s;
			for (s = t[l], X = 2; *s; s++) {
				int k = (unsigned char)*s - first, x, y;
				if (k < 0 || k >= count) continue;
				for (y = 0; y < cell; y++) for (x = 0; x < cell; x++)
					if (bits[((long)k * cell + y) * cell + x] && X + x < W) img[(l * (cell + 4) + y + 2) * W + X + x] = 255;
				X += adv[k];
			}
		}
		f = fopen(argv[5], "wb"); fprintf(f, "P5 %d %d 255\n", W, H); fwrite(img, 1, (size_t)W * H, f); fclose(f);
	}
	return 0;
}
