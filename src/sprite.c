/* sprite.c - see sprite.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sprite.h"
#include "script.h"
#include "eui.h"
#include "pak.h"

static const unsigned char *g_shadow;      /* pen -> darkened pen, NULL = shadows not drawn */
void sprite_set_shadow_table(const unsigned char *tab) { g_shadow = tab; }
const unsigned char *sprite_get_shadow_table(void) { return g_shadow; }

static int build_runs(Sprite *s);

/* Shields and flags have no shadow: their red fields are (240..255, 0, 0) and would read as the shadow
 * key (106 shields, measured 2026-11-09); only the green key applies to them. */
static int g_noshadow;

/* pens + colour keys of a decoded image: (0,255,0) -> SPR_CLEAR, (255,0,0) -> SPR_SHADOW (SPEC_UI §1) */
static unsigned char *keyed_pens(const EuiImage *im, const unsigned char *inv)
{
	long n = (long)im->w * im->h, i;
	unsigned char *p = malloc((size_t)n + 1);
	if (!p) return NULL;
	eui_to_pens(im, inv, p);
	if (im->format == 1) {
		unsigned char key[256];
		int k;
		/* palettised bitmaps: index 0 is the transparent green, index 1 the shadow red (SPEC_UI §1);
		 * the exact values vary per file ((4,255,4), (252,4,4) ...), so test closeness, not equality */
		for (k = 0; k < 256; k++) {
			const unsigned char *c = im->pal + (k < im->ncolors ? k : 0) * 3;
			int green = (c[1] >= 240 && c[0] <= 16 && c[2] <= 16) || (c[0] >= 240 && c[2] >= 240 && c[1] <= 16), red = c[0] >= 240 && c[1] <= 16 && c[2] <= 16;   /* magenta: the key of t-map_arrows (palette arrow.bmp) */
			key[k] = green ? SPR_CLEAR : red && !g_noshadow ? SPR_SHADOW : 0;
		}
		for (i = 0; i < n; i++) if (key[im->pix[i]]) p[i] = key[im->pix[i]];
	} else {
		for (i = 0; i < n; i++) {
			unsigned v = (unsigned)(im->pix[2 * i] << 8 | im->pix[2 * i + 1]);
			int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
			if ((g >= 60 && r <= 2 && b <= 2) || (r >= 30 && b >= 30 && g <= 4)) p[i] = SPR_CLEAR; else if (!g_noshadow && r >= 30 && g <= 4 && b <= 2) p[i] = SPR_SHADOW;
		}
	}
	return p;
}


/* ------------------------------------------------------------ Lanczos-3 downscale
 * Per frame, separable, integer only. Channels R G B premultiplied by coverage A, plus a shadow
 * mask S, so the transparent green never bleeds into the edges (the user asked for Lanczos for the
 * 640x480 interface). Weights x16384, renormalised per output pixel. */
#include "lanczos_tab.h"

typedef struct { short first, n; short w[24]; } Taps;    /* scale >= 0.25: at most 24 taps */

static void make_taps(Taps *t, int n, int m)
{
	int i;
	for (i = 0; i < m; i++) {
		long c256 = ((long)(2 * i + 1) * n * 256) / (2L * m) - 128, sum = 0;
		long lo = (c256 - 3L * 256 * n / m) / 256 - 1, hi = (c256 + 3L * 256 * n / m) / 256 + 1, j;
		int k = 0;
		if (lo < 0) lo = 0;
		if (hi > n - 1) hi = n - 1;
		t[i].first = (short)lo;
		for (j = lo; j <= hi && k < 24; j++, k++) {
			long d = j * 256 - c256, t64;
			if (d < 0) d = -d;
			t64 = d * m / (4L * n);
			t[i].w[k] = (short)(t64 < 192 ? lanczos3_tab[t64] : 0);
			sum += t[i].w[k];
		}
		t[i].n = (short)k;
		if (sum > 0) for (j = 0; j < k; j++) t[i].w[j] = (short)(t[i].w[j] * 16384L / sum);
	}
}

/* One 16-bit plane fw x h -> nw x nh. Values are 0..255 (R, G, B premultiplied by coverage, or a
 * coverage/shadow mask), weights x16384 in shorts: every multiply is a 16x16->32 muls.w on the 68020. */
static void resize_plane(const short *src, int fw, int h, short *dst, int nw, int nh, const Taps *tx, const Taps *ty, short *mid)
{
	int x, y, k;
	for (y = 0; y < nh; y++) {
		const Taps *t = &ty[y];
		short *m = mid + (long)y * fw;
		for (x = 0; x < fw; x++) {
			long acc = 0;
			const short *s = src + (long)t->first * fw + x;
			for (k = 0; k < t->n; k++, s += fw) acc += (long)(short)*s * (short)t->w[k];
			m[x] = (short)(acc >> 14);
		}
	}
	for (y = 0; y < nh; y++) {
		const short *m = mid + (long)y * fw;
		short *d = dst + (long)y * nw;
		for (x = 0; x < nw; x++) {
			const Taps *t = &tx[x];
			const short *s = m + t->first;
			long acc = 0;
			for (k = 0; k < t->n; k++) acc += (long)(short)s[k] * (short)t->w[k];
			d[x] = (short)(acc >> 14);
		}
	}
}

/* scales every frame of the image; returns w*frames x h pens with SPR_CLEAR / SPR_SHADOW keys.
 * Planes that an image does not need (no key colour -> no coverage plane, no shadow -> no shadow
 * plane) are skipped. */
static unsigned char *lanczos_pens(const EuiImage *im, const unsigned char *inv, int frames, int nfw, int nh)
{
	int fw = im->w / frames, f, x, y, p, has_key = 0, has_shadow = 0;
	long n = (long)fw * im->h, m = (long)nfw * nh;
	short *src = malloc(sizeof(short) * 5 * n), *dst = malloc(sizeof(short) * 5 * m), *mid = malloc(sizeof(short) * fw * nh);
	unsigned char *out = malloc((size_t)nfw * frames * nh + 1);
	Taps *tx = malloc(sizeof(Taps) * nfw), *ty = malloc(sizeof(Taps) * nh);
	if (!src || !dst || !mid || !out || !tx || !ty) { free(src); free(dst); free(mid); free(out); free(tx); free(ty); return NULL; }
	make_taps(tx, fw, nfw); make_taps(ty, im->h, nh);
	for (f = 0; f < frames; f++) {
		for (y = 0; y < im->h; y++)
			for (x = 0; x < fw; x++) {
				long i = (long)y * im->w + f * fw + x, o = (long)y * fw + x;
				int r, g, b, key = 0, shadow = 0;
				if (im->format == 1) {
					const unsigned char *c = im->pal + im->pix[i] * 3;
					r = c[0]; g = c[1]; b = c[2];
				} else {
					unsigned v = (unsigned)(im->pix[2 * i] << 8 | im->pix[2 * i + 1]);
					r = (v >> 11) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3;
				}
				if ((g >= 240 && r <= 16 && b <= 16) || (r >= 240 && b >= 240 && g <= 16)) key = 1;
				else if (!g_noshadow && r >= 240 && g <= 16 && b <= 16) shadow = 1;
				if (key || shadow) { r = g = b = 0; has_key = 1; }
				if (shadow) has_shadow = 1;
				src[o] = (short)r; src[n + o] = (short)g; src[2 * n + o] = (short)b;
				src[3 * n + o] = (short)(key || shadow ? 0 : 255); src[4 * n + o] = (short)(shadow ? 255 : 0);
			}
		for (p = 0; p < 5; p++) {
			if ((p == 3 && !has_key) || (p == 4 && !has_shadow)) continue;
			resize_plane(src + p * n, fw, im->h, dst + p * m, nfw, nh, tx, ty, mid);
		}
		for (y = 0; y < nh; y++)
			for (x = 0; x < nfw; x++) {
				long o = (long)y * nfw + x;
				int a = has_key ? dst[3 * m + o] : 255, s = has_shadow ? dst[4 * m + o] : 0;
				unsigned char *q = out + (long)y * nfw * frames + f * nfw + x;
				if (a < 128) { *q = s >= 128 ? SPR_SHADOW : SPR_CLEAR; continue; }
				{
					long r = (long)dst[o] * 255 / a, g = (long)dst[m + o] * 255 / a, b = (long)dst[2 * m + o] * 255 / a;
					if (r < 0) r = 0;
					if (r > 255) r = 255;
					if (g < 0) g = 0;
					if (g > 255) g = 255;
					if (b < 0) b = 0;
					if (b > 255) b = 255;
					*q = inv[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)];
				}
			}
	}
	free(src); free(dst); free(mid); free(tx); free(ty);
	return out;
}

/* fill s from an image of 'frames' frames side by side, scaled by scale/1000 with Lanczos-3, frame
 * by frame (only here is the frame count known - a converter-side rescale could not keep strips
 * aligned) */
static int from_image(Sprite *s, const EuiImage *im, const unsigned char *inv, int frames, int scale)
{
	unsigned char *full;
	int fw, nfw, nh, f, x, y;
	if (frames < 1) frames = 1;
	if (scale != 1000 && scale < 1000) {
		fw = im->w / frames;
		nfw = (fw * scale + 500) / 1000; nh = (im->h * scale + 500) / 1000;
		if (nfw < 1) nfw = 1;
		if (nh < 1) nh = 1;
		if (fw >= 2 && im->h >= 2 && (s->pens = lanczos_pens(im, inv, frames, nfw, nh)) != NULL) {
			s->frames = frames; s->w = nfw; s->h = nh;
			s->ox = s->ox * scale / 1000; s->oy = s->oy * scale / 1000;
			return build_runs(s) ? -6 : 0;
		}
	}
	full = keyed_pens(im, inv);
	if (!full) return -5;
	fw = im->w / frames;
	nfw = scale == 1000 ? fw : (fw * scale + 500) / 1000;
	nh = scale == 1000 ? im->h : (im->h * scale + 500) / 1000;
	if (nfw < 1) nfw = 1;
	if (nh < 1) nh = 1;
	s->frames = frames; s->w = nfw; s->h = nh;
	if (scale == 1000 && nfw * frames == im->w) s->pens = full;
	else {
		s->pens = malloc((size_t)nfw * frames * nh + 1);
		if (!s->pens) { free(full); return -5; }
		for (y = 0; y < nh; y++) {
			int sy = y * im->h / nh;
			for (f = 0; f < frames; f++)
				for (x = 0; x < nfw; x++)
					s->pens[(long)y * nfw * frames + f * nfw + x] = full[(long)sy * im->w + f * fw + x * fw / nfw];
		}
		free(full);
	}
	s->ox = s->ox * scale / 1000; s->oy = s->oy * scale / 1000;
	return build_runs(s) ? -6 : 0;
}

static int cache_read(Sprite *s, const char *path, int frames, int scale);
static void cache_write(const Sprite *s, const char *path, int frames, int scale);
int sprite_load(Sprite *s, Pak *text, Pak *gfx, const char *spr, const unsigned char *inv) { return sprite_load_scaled(s, text, gfx, spr, inv, 1000); }
int sprite_load_scaled(Sprite *s, Pak *text, Pak *gfx, const char *spr, const unsigned char *inv, int scale)
{
	unsigned long sz;
	char *txt, err[96], path[160];
	const char *bmp;
	unsigned char *raw;
	Arena *a;
	SNode *root, *sp;
	EuiImage im;
	int frames, rc;
	memset(s, 0, sizeof *s);
	if (!(txt = (char *)pak_load(text, spr, &sz))) return -1;
	a = arena_new(4096);
	root = script_parse(txt, sz, a, err, sizeof err);
	sp = root ? script_get(root, "sprite") : NULL;
	bmp = sp ? script_str(sp, "bitmap", NULL) : NULL;
	if (!bmp) { arena_free(a); free(txt); return -2; }
	frames = (int)script_int(sp, "frames", 1);
	s->ox = (int)script_int(script_get(sp, "origin"), "x", 0);
	s->oy = (int)script_int(script_get(sp, "origin"), "y", 0);
	snprintf(path, sizeof path, "gfx/map/units/bmp/%s", bmp);
	arena_free(a); free(txt);
	if (scale != 1000 && cache_read(s, path, frames, scale) == 0) return 0;   /* Lanczos once, then from the disk cache */
	if (!(raw = pak_load(gfx, path, &sz))) return -3;
	if (eui_decode(raw, sz, &im)) { free(raw); return -4; }
	free(raw);
	rc = from_image(s, &im, inv, frames, scale);
	eui_free(&im);
	if (rc) sprite_free(s);
	else if (scale != 1000) cache_write(s, path, frames, scale);
	return rc;
}


/* ------------------------------------------------------------ disk cache of scaled sprites
 * Lanczos on a 68020 costs seconds per window, so every scaled sprite is written once to
 * <dir>/<hash>.spc (pens + size + a checksum of the screen palette's inverse table) and read back
 * on the next start. The directory must exist (the platform layer creates it). */
static char g_cache_dir[64];
static unsigned long g_inv_sum;
void sprite_set_cache_dir(const char *dir, const unsigned char *inv)
{
	int i;
	snprintf(g_cache_dir, sizeof g_cache_dir, "%s", dir ? dir : "");
	g_inv_sum = 5381;
	if (inv) for (i = 0; i < 4096; i++) g_inv_sum = (g_inv_sum * 33 + inv[i]) & 0xFFFFFFFFUL;
}

static void cache_name(char *out, int n, const char *path, int frames, int scale)
{
	unsigned long h = 5381;
	const char *p;
	for (p = path; *p; p++) h = (h * 33 + (unsigned char)*p) & 0xFFFFFFFFUL;
	h = (h * 33 + (unsigned long)frames * 1000 + (unsigned long)scale) & 0xFFFFFFFFUL;
	h = (h * 33 + g_inv_sum) & 0xFFFFFFFFUL;            /* one file per palette */
	snprintf(out, n, "%s/%08lx.spc", g_cache_dir, h);
}

static int cache_read(Sprite *s, const char *path, int frames, int scale)
{
	char name[96];
	unsigned char h[20];
	FILE *f;
	long n;
	if (!g_cache_dir[0]) return -1;
	cache_name(name, sizeof name, path, frames, scale);
	if (!(f = fopen(name, "rb"))) return -1;
	if (fread(h, 1, 20, f) != 20 || memcmp(h, "SPC3", 4) || BE32(h + 4) != g_inv_sum) { fclose(f); return -1; }
	s->w = (int)BE16(h + 8); s->h = (int)BE16(h + 10); s->frames = (int)BE16(h + 12);
	s->ox = (short)BE16(h + 14); s->oy = (short)BE16(h + 16);
	n = (long)s->w * s->frames * s->h;
	s->pens = malloc((size_t)n + 1);
	if (!s->pens || fread(s->pens, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(s->pens); s->pens = NULL; return -1; }
	fclose(f);
	return build_runs(s) ? -1 : 0;
}

static void cache_write(const Sprite *s, const char *path, int frames, int scale)
{
	char name[96];
	unsigned char h[20];
	FILE *f;
	if (!g_cache_dir[0]) return;
	cache_name(name, sizeof name, path, frames, scale);
	if (!(f = fopen(name, "wb"))) return;
	memcpy(h, "SPC3", 4);                                /* 2: shields without the shadow key; 3: magenta key */
	h[4] = (unsigned char)(g_inv_sum >> 24); h[5] = (unsigned char)(g_inv_sum >> 16); h[6] = (unsigned char)(g_inv_sum >> 8); h[7] = (unsigned char)g_inv_sum;
	h[8] = (unsigned char)(s->w >> 8); h[9] = (unsigned char)s->w; h[10] = (unsigned char)(s->h >> 8); h[11] = (unsigned char)s->h;
	h[12] = (unsigned char)(s->frames >> 8); h[13] = (unsigned char)s->frames;
	h[14] = (unsigned char)((unsigned)s->ox >> 8); h[15] = (unsigned char)s->ox; h[16] = (unsigned char)((unsigned)s->oy >> 8); h[17] = (unsigned char)s->oy;
	h[18] = h[19] = 0;
	fwrite(h, 1, 20, f);
	fwrite(s->pens, 1, (size_t)s->w * s->frames * s->h, f);
	fclose(f);
}

int sprite_from_bmp(Sprite *s, Pak *gfx, const char *path, const unsigned char *inv, int frames, int scale)
{
	unsigned long sz;
	unsigned char *raw;
	EuiImage im;
	int rc;
	memset(s, 0, sizeof *s);
	{	const char *q;
		g_noshadow = 0;
		for (q = path; *q; q++) if (!strncmp(q, "shields/", 8) || !strncmp(q, "flags/", 6)) g_noshadow = 1;
	}
	if (inv && g_cache_dir[0] && scale != 1000) {        /* the cache entry belongs to the inverse table in use (menus have their own) */
		int i;
		g_inv_sum = 5381;
		for (i = 0; i < 4096; i++) g_inv_sum = (g_inv_sum * 33 + inv[i]) & 0xFFFFFFFFUL;
	}
	if (scale != 1000 && cache_read(s, path, frames, scale) == 0) return 0;
	memset(s, 0, sizeof *s);
	if (!(raw = pak_load(gfx, path, &sz))) return -3;
	if (eui_decode(raw, sz, &im)) { free(raw); return -4; }
	free(raw);
	rc = from_image(s, &im, inv, frames, scale);
	eui_free(&im);
	if (rc) sprite_free(s);
	else if (scale != 1000) cache_write(s, path, frames, scale);
	return rc;
}

/* spans per frame row, built once: (x, len, kind) with kind 0 = copy, 1 = shadow */
static int build_runs(Sprite *s)
{
	int sw = s->w * s->frames, f, y, x, pass;
	unsigned long n = 0;
	for (pass = 0; pass < 2; pass++) {
		n = 0;
		for (f = 0; f < s->frames; f++)
			for (y = 0; y < s->h; y++) {
				const unsigned char *r = s->pens + (long)y * sw + (long)f * s->w;
				unsigned long cnt_at = n++;
				int cnt = 0;
				if (pass) s->rowoff[f * s->h + y] = cnt_at;
				for (x = 0; x < s->w; ) {
					int x0, kind;
					while (x < s->w && r[x] == SPR_CLEAR) x++;
					if (x >= s->w) break;
					x0 = x;
					kind = r[x] == SPR_SHADOW;
					while (x < s->w && r[x] != SPR_CLEAR && (r[x] == SPR_SHADOW) == kind) x++;
					if (pass) { s->runs[n] = (unsigned short)x0; s->runs[n + 1] = (unsigned short)(x - x0); s->runs[n + 2] = (unsigned short)kind; }
					n += 3; cnt++;
				}
				if (pass) s->runs[cnt_at] = (unsigned short)cnt;
			}
		if (!pass) {
			s->runs = malloc(n * sizeof *s->runs + 2);
			s->rowoff = malloc((size_t)s->frames * s->h * sizeof *s->rowoff + 4);
			if (!s->runs || !s->rowoff) return -1;
		}
	}
	return 0;
}

void sprite_free(Sprite *s) { free(s->pens); free(s->runs); free(s->rowoff); s->pens = NULL; s->runs = NULL; s->rowoff = NULL; }

void sprite_draw_shift(const Sprite *s, int f, unsigned char *dst, int pitch, int w, int h, int x, int y, int sh)
{
	int sw = s->w * s->frames, yy, step = 1 << sh;
	if (sh <= 0) { sprite_draw(s, f, dst, pitch, w, h, x, y); return; }
	if (!s->pens || !s->runs) return;
	f %= s->frames;
	x -= s->ox >> sh; y -= s->oy >> sh;
	if (x >= w || y >= h || x + (s->w >> sh) <= 0 || y + (s->h >> sh) <= 0) return;
	for (yy = 0; yy < s->h; yy += step) {
		int dy = y + (yy >> sh), k, cnt;
		const unsigned short *r;
		const unsigned char *src;
		unsigned char *d;
		if (dy < 0 || dy >= h) continue;
		r = s->runs + s->rowoff[f * s->h + yy];
		cnt = *r++;
		src = s->pens + (long)yy * sw + (long)f * s->w;
		d = dst + (long)dy * pitch;
		for (k = 0; k < cnt; k++, r += 3) {
			int o = (r[0] + step - 1) & ~(step - 1), e = r[0] + r[1];     /* the run's source columns on the grid */
			for (; o < e; o += step) {
				int dx = x + (o >> sh);
				if (dx < 0 || dx >= w) continue;
				if (!r[2]) d[dx] = src[o];
				else if (g_shadow) d[dx] = g_shadow[d[dx]];
			}
		}
	}
}

void sprite_draw(const Sprite *s, int f, unsigned char *dst, int pitch, int w, int h, int x, int y)
{
	int sw = s->w * s->frames, yy;
	if (!s->pens || !s->runs) return;
	f %= s->frames;
	x -= s->ox; y -= s->oy;
	if (x >= w || y >= h || x + s->w <= 0 || y + s->h <= 0) return;
	for (yy = 0; yy < s->h; yy++) {
		int dy = y + yy, k, cnt;
		const unsigned short *r;
		const unsigned char *src;
		unsigned char *d;
		if (dy < 0 || dy >= h) continue;
		r = s->runs + s->rowoff[f * s->h + yy];
		cnt = *r++;
		src = s->pens + (long)yy * sw + (long)f * s->w;
		d = dst + (long)dy * pitch;
		for (k = 0; k < cnt; k++, r += 3) {
			int x0 = x + r[0], len = r[1], o = r[0];
			if (x0 < 0) { len += x0; o -= x0; x0 = 0; }
			if (x0 + len > w) len = w - x0;
			if (len <= 0) continue;
			if (!r[2]) memcpy(d + x0, src + o, (size_t)len);
			else if (g_shadow) { unsigned char *q = d + x0; while (len--) { *q = g_shadow[*q]; q++; } }
		}
	}
}
