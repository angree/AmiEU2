/* eui.c - see eui.h */
#include <stdlib.h>
#include <string.h>
#include "eui.h"
#include "pak.h"

int eui_decode(const unsigned char *d, unsigned long n, EuiImage *im)
{
	unsigned long p, dl, units, bytes, o = 0;
	int u;
	memset(im, 0, sizeof *im);
	if (n < 16 || memcmp(d, "EUI1", 4)) return -1;
	im->w = (int)BE16(d + 4); im->h = (int)BE16(d + 6);
	im->format = d[8];
	im->ncolors = (int)BE16(d + 10);
	if (im->ncolors > 256 || (im->format != 1 && im->format != 2)) return -2;
	memcpy(im->pal, d + 12, (size_t)im->ncolors * 3);
	p = 12 + (unsigned long)im->ncolors * 3;
	if (p + 4 > n) return -3;
	dl = BE32(d + p); p += 4;
	if (p + dl > n) return -3;
	u = im->format == 2 ? 2 : 1;
	units = (unsigned long)im->w * im->h;
	bytes = units * u;
	im->pix = malloc(bytes + 1);
	if (!im->pix) return -4;
	if (!(d[9] & 1)) {
		if (dl < bytes) { eui_free(im); return -3; }
		memcpy(im->pix, d + p, bytes);
		return 0;
	}
	{	/* PackBits, unit u */
		const unsigned char *s = d + p, *e = d + p + dl;
		while (o < bytes && s < e) {
			unsigned c = *s++;
			if (c < 128) {
				unsigned long k = (unsigned long)(c + 1) * u;
				if (o + k > bytes || s + k > e) break;
				memcpy(im->pix + o, s, k); o += k; s += k;
			} else {
				unsigned r = 257 - c;
				if (o + (unsigned long)r * u > bytes || s + u > e) break;
				if (u == 1) { memset(im->pix + o, *s, r); o += r; }
				else while (r--) { im->pix[o] = s[0]; im->pix[o + 1] = s[1]; o += 2; }
				s += u;
			}
		}
	}
	if (o != bytes) { eui_free(im); return -5; }
	return 0;
}

void eui_free(EuiImage *im) { free(im->pix); im->pix = NULL; }

void eui_build_inverse(const unsigned char *pal, int npens, unsigned char *inv)
{
	int r, g, b, i;
	for (r = 0; r < 16; r++) for (g = 0; g < 16; g++) for (b = 0; b < 16; b++) {
		int R = r * 17, G = g * 17, B = b * 17, best = 0;
		long bd = 0x7fffffffL;
		for (i = 0; i < npens; i++) {
			int dr, dg, db;
			long dd;
			if (i >= 17 && i <= 19) continue;    /* hardware pointer pens (sprite 0) - never for pixels */
			dr = R - pal[i * 3]; dg = G - pal[i * 3 + 1]; db = B - pal[i * 3 + 2];
			dd = (long)dr * dr * 3 + (long)dg * dg * 4 + (long)db * db * 2;
			if (dd < bd) { bd = dd; best = i; }
		}
		inv[(r << 8) | (g << 4) | b] = (unsigned char)best;
	}
}

void eui_to_pens(const EuiImage *im, const unsigned char *inv, unsigned char *out)
{
	unsigned long n = (unsigned long)im->w * im->h, i;
	if (im->format == 1) {
		unsigned char map[256];
		int k;
		for (k = 0; k < 256; k++) {
			const unsigned char *c = im->pal + (k < im->ncolors ? k : 0) * 3;
			map[k] = inv[((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4)];
		}
		for (i = 0; i < n; i++) out[i] = map[im->pix[i]];
	} else {
		const unsigned char *s = im->pix;
		for (i = 0; i < n; i++, s += 2) {
			unsigned v = (unsigned)(s[0] << 8 | s[1]);
			out[i] = inv[((v >> 12) << 8) | (((v >> 7) & 15) << 4) | ((v >> 1) & 15)];
		}
	}
}
