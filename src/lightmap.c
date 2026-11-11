/* lightmap.c - see lightmap.h; algorithm described in docs/MAP_FORMAT.md, reference reader
 * tools/maptool.py (decode_tile, edge_left, edge_top, tile_shade). */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lightmap.h"

#define W0 18944
#define H0 7296

int lm_load(Lightmap *lm, Pak *map, int level)
{
	char name[32];
	long cand[3];
	int k;
	memset(lm, 0, sizeof *lm);
	snprintf(name, sizeof name, "map/lightmap%d.tbl", level);
	lm->raw = pak_load(map, name, &lm->size);
	if (!lm->raw) return -1;
	lm->level = level;
	lm->tw = (W0 >> (level - 1)) / 32;
	lm->th = ((H0 >> (level - 1)) + 31) / 32;
	cand[0] = (long)lm->tw * lm->th; cand[1] = (long)lm->tw * (lm->th - 1); cand[2] = (long)lm->tw * (lm->th + 1);
	for (k = 0; k < 3; k++) {
		unsigned long p = 3UL * (unsigned long)cand[k];
		const unsigned char *d = lm->raw + p;
		if (p + 4 <= lm->size && (d[0] | d[1] << 8 | (unsigned long)d[2] << 16 | (unsigned long)d[3] << 24) == lm->size - p - 4) {
			lm->ntiles = cand[k];
		}
	}
	if (!lm->ntiles) { lm_free(lm); return -2; }
	lm->th = (int)(lm->ntiles / lm->tw);
	lm->base = 3UL * (unsigned long)lm->ntiles + 4;
	return 0;
}

void lm_cache_reset(void);
void lm_free(Lightmap *lm) { free(lm->raw); memset(lm, 0, sizeof *lm); lm_cache_reset(); }

/* ------------------------------------------------------------ decode */
typedef struct { const unsigned char *bits; unsigned pos; LmLeaf *out; int n; } Walk;

static int bit(Walk *w) { unsigned k = w->pos++; return (w->bits[k >> 3] >> (k & 7)) & 1; }

static void walk(Walk *w, int x, int y, int lg)
{
	if (bit(w)) {
		int h = 1 << (lg - 1);
		if (lg > 1) {
			walk(w, x + h, y + h, lg - 1);
			walk(w, x, y + h, lg - 1);
			walk(w, x + h, y, lg - 1);
			walk(w, x, y, lg - 1);
		} else {
			LmLeaf *o = w->out + w->n;
			o[0].x = (unsigned char)(x + 1); o[0].y = (unsigned char)(y + 1); o[0].lg = 0;
			o[1].x = (unsigned char)x;       o[1].y = (unsigned char)(y + 1); o[1].lg = 0;
			o[2].x = (unsigned char)(x + 1); o[2].y = (unsigned char)y;       o[2].lg = 0;
			o[3].x = (unsigned char)x;       o[3].y = (unsigned char)y;       o[3].lg = 0;
			w->n += 4;
		}
	} else {
		LmLeaf *o = w->out + w->n++;
		o->x = (unsigned char)x; o->y = (unsigned char)y; o->lg = (unsigned char)lg;
	}
}

static const unsigned short *g_adj_start, *g_adj_prov;
static int g_adj_np;
void lm_set_adjacency(const unsigned short *start, const unsigned short *prov, int np) { g_adj_start = start; g_adj_prov = prov; g_adj_np = np; lm_cache_reset(); }

int lm_tile_has(const Lightmap *lm, int tx, int ty, unsigned p)
{
	const unsigned char *d, *t;
	unsigned short pal[256];
	int npal = 0, i;
	tx %= lm->tw; if (tx < 0) tx += lm->tw;
	if (ty < 0) ty = 0;
	if (ty >= lm->th) ty = lm->th - 1;
	t = lm->raw + 3 * ((long)ty * lm->tw + tx);
	d = lm->raw + lm->base + (t[0] | t[1] << 8 | (unsigned long)t[2] << 16);
	for (;;) {
		unsigned v = d[0] | d[1] << 8;
		d += 2;
		if (npal < 256) pal[npal++] = (unsigned short)(v & 0x7FFF);
		if (v & 0x8000) break;
	}
	for (i = 0; i < npal; i++) {
		unsigned v = pal[i];
		if (v == p) return 1;
		if (v >= LM_SPECIAL) {                              /* a river pixel can name a neighbour (as lm_leaves) */
			int k = (int)((v >> 9) & 0x3F) - 4, b = (int)((v >> 5) & 15);
			unsigned q = (k >= 0 && k < npal && pal[k] < LM_SPECIAL) ? pal[k] : 0;
			if (b != 15 && g_adj_start && (int)q < g_adj_np && g_adj_start[q] + b < g_adj_start[q + 1] && g_adj_prov[g_adj_start[q] + b] == p) return 1;
		}
	}
	return 0;
}

int lm_leaves(const Lightmap *lm, int tx, int ty, LmLeaf *out)
{
	const unsigned char *d, *t;
	unsigned short pal[256];
	int npal = 0, nb, i;
	long ti;
	Walk w;
	tx %= lm->tw; if (tx < 0) tx += lm->tw;
	if (ty < 0) ty = 0;
	if (ty >= lm->th) ty = lm->th - 1;
	ti = (long)ty * lm->tw + tx;
	t = lm->raw + 3 * ti;
	d = lm->raw + lm->base + (t[0] | t[1] << 8 | (unsigned long)t[2] << 16);
	for (;;) {
		unsigned v = d[0] | d[1] << 8;
		d += 2;
		if (npal < 256) pal[npal++] = (unsigned short)(v & 0x7FFF);
		if (v & 0x8000) break;
	}
	w.bits = d; w.pos = 0; w.out = out; w.n = 0;
	walk(&w, 0, 0, 5);
	d += (w.pos + 7) >> 3;
	nb = npal == 1 ? 0 : npal == 2 ? 1 : npal <= 4 ? 2 : npal <= 16 ? 4 : 8;
	if (nb == 0) for (i = 0; i < w.n; i++) out[i].val = pal[0];
	else {
		int per = 8 / nb, m = (1 << nb) - 1;
		for (i = 0; i < w.n; i++) out[i].val = pal[(d[i / per] >> ((i % per) * nb)) & m];
		d += (w.n + per - 1) / per;
	}
	for (i = 0; i < w.n; i += 4) {
		unsigned b0 = d[0], b1 = d[1], b2 = d[2];
		out[i].shade = (unsigned char)(b0 & 63);
		if (i + 1 < w.n) out[i + 1].shade = (unsigned char)((b0 >> 6) | (b1 & 15) << 2);
		if (i + 2 < w.n) out[i + 2].shade = (unsigned char)((b1 >> 4) | (b2 & 3) << 4);
		if (i + 3 < w.n) out[i + 3].shade = (unsigned char)(b2 >> 2);
		d += 3;
	}
	/* border/river pixels (eu2.exe 0x476e98): the province P they belong to is another palette entry; bits 5..8
	   name P's neighbour b in the adjacency list unless 15 (rivers: checked against id.tbl, 95 % of the river
	   pixels, the rest are its own border line). Bits 1..4 pick the neighbour for the border test, which the
	   border pass of mapview does on its own. */
	for (i = 0; i < w.n; i++) {
		unsigned v = out[i].val;
		if (v >= LM_SPECIAL) {
			int k = (int)((v >> 9) & 0x3F) - 4, b = (int)((v >> 5) & 15);
			unsigned p = (k >= 0 && k < npal && pal[k] < LM_SPECIAL) ? pal[k] : 0;
			if (b != 15 && g_adj_start && (int)p < g_adj_np && g_adj_start[p] + b < g_adj_start[p + 1]) p = g_adj_prov[g_adj_start[p] + b];
			out[i].val = (unsigned short)p;
		}
	}
	return w.n;
}

/* ------------------------------------------------------------- shade */
static void edge_left(const LmLeaf *lv, int n, int bottom, int *col)
{
	int i, j;
	col[32] = bottom;
	for (i = 0; i < n; i++) {
		const LmLeaf *l = &lv[i];
		int s, a, st;
		if (l->x) continue;
		if (!l->lg) { col[l->y] = l->shade; continue; }
		s = 1 << l->lg;
		a = (int)l->shade << 24;
		st = (int)(((long)col[l->y + s] << 24) - a) >> l->lg;
		for (j = 0; j < s; j++) col[l->y + j] = (a + j * st) >> 24;
	}
}

static void edge_top(const LmLeaf *lv, int n, int right, int *row)
{
	int i, j;
	row[32] = right;
	for (i = 0; i < n; i++) {
		const LmLeaf *l = &lv[i];
		int s, a, st;
		if (l->y) continue;
		if (!l->lg) { row[l->x] = l->shade; continue; }
		s = 1 << l->lg;
		a = (int)l->shade << 24;
		st = (int)(((long)row[l->x + s] << 24) - a) >> l->lg;
		for (j = 0; j < s; j++) row[l->x + j] = (a + j * st) >> 24;
	}
}

/* Leaf cache: drawing goes row by row, and every tile also needs its right, lower and diagonal
 * neighbours, so two rows of 32 tile columns are kept (slot = tx & 31 | (ty & 1) << 5). This cut
 * the decodes per tile from 4 to about 1. */
#define LC_SLOTS 64
typedef struct { const Lightmap *lm; int level, tx, ty, n; LmLeaf lv[1024]; } LeafSlot;
static LeafSlot *g_lc;

static const LmLeaf *cached_leaves(const Lightmap *lm, int tx, int ty, int *n)
{
	LeafSlot *s;
	tx %= lm->tw; if (tx < 0) tx += lm->tw;
	if (ty < 0) ty = 0;
	if (ty >= lm->th) ty = lm->th - 1;
	if (!g_lc) {
		g_lc = calloc(LC_SLOTS, sizeof *g_lc);
		if (!g_lc) { static LmLeaf tmp[1024]; *n = lm_leaves(lm, tx, ty, tmp); return tmp; }
	}
	s = &g_lc[(tx & 31) | ((ty & 1) << 5)];
	if (s->lm != lm || s->level != lm->level || s->tx != tx || s->ty != ty) {
		s->n = lm_leaves(lm, tx, ty, s->lv);
		s->lm = lm; s->level = lm->level; s->tx = tx; s->ty = ty;
	}
	*n = s->n;
	return s->lv;
}

void lm_cache_reset(void) { if (g_lc) memset(g_lc, 0, LC_SLOTS * sizeof *g_lc); }

/* One routine for both outputs. shade != NULL: 0..63 per pixel (the reference behaviour).
 * pens != NULL: screen pens straight away - per leaf a 72-byte table (prov_tab[id]) is picked once,
 * per pixel only tab[shade + dither] is read; no id buffer, no second pass (mapview). */
unsigned long lm_flat_px, lm_slope_px;   /* statistics: pixels in flat / sloped leaves */
static const unsigned char dith4[4][4] = { { 0, 4, 1, 5 }, { 6, 2, 7, 3 }, { 1, 5, 0, 4 }, { 7, 3, 6, 2 } };

static void tile_core(const Lightmap *lm, int tx, int ty, unsigned short *ids, unsigned char *shade,
                      unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab)
{
	static int buf[33][33];
	const LmLeaf *lv, *nb;
	int right[33], bottom[33], n, i, j, k, diag;
	lv = cached_leaves(lm, tx, ty, &n);
	if (ids) for (i = 0; i < n; i++) {
		const LmLeaf *l = &lv[i];
		int s = 1 << l->lg, yy;
		for (yy = 0; yy < s; yy++) {
			unsigned short *r = ids + (l->y + yy) * 32 + l->x;
			for (k = 0; k < s; k++) r[k] = l->val;
		}
	}
	if (!shade && !pens) return;
	if (ty + 1 < lm->th) {
		int m;
		nb = cached_leaves(lm, tx + 1, ty + 1, &m);
		diag = nb[m - 1].shade;                          /* last leaf = top-left corner */
		nb = cached_leaves(lm, tx, ty + 1, &m);
		edge_top(nb, m, diag, bottom);
	} else {
		diag = 6;
		for (j = 0; j < 33; j++) bottom[j] = 6;          /* the game fills 6 below the map */
	}
	{ int m; nb = cached_leaves(lm, tx + 1, ty, &m); edge_left(nb, m, diag, right); }
	for (j = 0; j < 33; j++) buf[j][32] = right[j];
	for (j = 0; j < 33; j++) buf[32][j] = bottom[j];
	for (i = 0; i < n; i++) {
		const LmLeaf *l = &lv[i];
		int x = l->x, y = l->y, lg = l->lg, s, tl, tr, bl, br, hs, ls, rs, L, R;
		const unsigned char *tab = NULL;
		if (pens) tab = prov_tab[l->val < nprov_tab ? l->val : 0];
		if (!lg) {
			buf[y][x] = l->shade;
			if (pens) pens[y * 32 + x] = tab[l->shade + dith4[y & 3][x & 3]];
			else shade[y * 32 + x] = l->shade;
			continue;
		}
		s = 1 << lg;
		tl = (int)l->shade << 16;
		tr = buf[y][x + s] << 16;
		bl = buf[y + s][x] << 16;
		br = buf[y + s][x + s] << 16;
		if (tl == tr && tl == bl && tl == br) {          /* flat leaf: no interpolation (most of the map) */
			int val = l->shade;
			lm_flat_px += s * s;
			for (k = 0; k < s; k++) buf[y][x + k] = val;
			for (j = 0; j < s; j++) {
				buf[y + j][x] = val;
				if (pens) {
					const unsigned char *d = dith4[(y + j) & 3];
					unsigned char *o = pens + (y + j) * 32 + x;
					if (s >= 4) {                        /* x is a multiple of 4: write the 4-pixel pattern as longs */
						unsigned char pat[4];
						unsigned long pl;
						pat[0] = tab[val + d[0]]; pat[1] = tab[val + d[1]]; pat[2] = tab[val + d[2]]; pat[3] = tab[val + d[3]];
						memcpy(&pl, pat, 4);
						for (k = 0; k < s; k += 4) memcpy(o + k, &pl, 4);
					} else for (k = 0; k < s; k++) o[k] = tab[val + d[(x + k) & 3]];
				} else memset(shade + (y + j) * 32 + x, val, s);
			}
			continue;
		}
		lm_slope_px += s * s;
		hs = (tr - tl) >> lg;
		for (k = 0; k < s; k++) buf[y][x + k] = (tl + k * hs) >> 16;
		ls = (bl - tl) >> lg;
		rs = (br - tr) >> lg;
		L = tl; R = tr;
		for (j = 0; j < s; j++) {
			int st = (L - R) >> lg, v = R + 0x8000;
			buf[y + j][x] = L >> 16;
			if (pens) {
				unsigned char *o = pens + (y + j) * 32 + x;
				const unsigned char *d = dith4[(y + j) & 3];
				for (k = s - 1; k >= 0; k--) { o[k] = tab[(v >> 16) + d[(x + k) & 3]]; v += st; }
			} else {
				unsigned char *o = shade + (y + j) * 32 + x;
				for (k = s - 1; k >= 0; k--) { o[k] = (unsigned char)(v >> 16); v += st; }
			}
			L += ls; R += rs;
		}
	}
}

void lm_tile(const Lightmap *lm, int tx, int ty, unsigned short *ids, unsigned char *shade)
{
	tile_core(lm, tx, ty, ids, shade, NULL, NULL, 0);
}

/* The map's shade (0..63 per pixel) becomes one of the 8 light levels of the province's scale. The
 * 4x4 ordered pattern (dith4) looked bad (user, 2026-11-09): the levels are now chosen by Floyd-Steinberg
 * error diffusion at 60 % strength, per 32x32 tile. A province table (72 entries, shade + 0..7) changes
 * level every 8 entries starting at its own phase (the map mode's province shade shifts it), found once
 * per table; the error is measured from the centre of the band the pixel fell in. Fixed point x16. */
static int band_phase(const unsigned char *tab)
{
	int k;
	for (k = 1; k < 72; k++) if (tab[k] != tab[k - 1]) return k;   /* a brightened province (shade > 15) changes late */
	return 0;                                           /* one level only (or a border pen): no bands */
}

static void fs_pens(const unsigned char *shade, const unsigned short *ids, unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab)
{
	static int e0[34], e1[34];
	int *cur = e0, *nxt = e1, x, y, last_id = -1, ph = 0;
	const unsigned char *tab = NULL, *last_tab = NULL;
	memset(e0, 0, sizeof e0); memset(e1, 0, sizeof e1);
	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			int id = ids[y * 32 + x], v, k, c, err;
			if (id != last_id) { tab = prov_tab[id < nprov_tab ? id : 0]; if (tab != last_tab) { ph = band_phase(tab); last_tab = tab; } last_id = id; }
			v = shade[y * 32 + x] * 16 + 56 + cur[x + 1];   /* index x16: shade + 3.5 */
			k = v < 0 ? 0 : v >> 4;
			if (k > 71) k = 71;
			pens[y * 32 + x] = tab[k];
			if (!ph) continue;
			/* the centre (x16) of the 8-entry band the pen stands for: bands start at ph + 8j, the first one is
			   [ph - 8, ph) even when the table clamps it wider */
			c = k < ph ? ph * 16 - 64 : (((k - ph) & ~7) + ph) * 16 + 64;
			err = (v - c) * 154 >> 8;                        /* 60 % */
			cur[x + 2] += err * 7 >> 4;
			nxt[x] += err * 3 >> 4;
			nxt[x + 1] += err * 5 >> 4;
			nxt[x + 2] += err >> 4;
		}
		{ int *t = cur; cur = nxt; nxt = t; memset(nxt, 0, sizeof e0); }
	}
}

unsigned long (*lm_probe_us)(void), lm_probe_t[2];   /* probes (mapview's probemodes): tile_core, fs_pens */
#define LPT(k, call) do { unsigned long a_ = lm_probe_us ? lm_probe_us() : 0; call; if (lm_probe_us) lm_probe_t[k] += lm_probe_us() - a_; } while (0)
void lm_pens_from(const unsigned char *shade, const unsigned short *ids, unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab)
{
	LPT(1, fs_pens(shade, ids, pens, prov_tab, nprov_tab));
}

void lm_tile_pens(const Lightmap *lm, int tx, int ty, unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab)
{
	static unsigned short ids[1024];
	static unsigned char sh[1024];
	LPT(0, tile_core(lm, tx, ty, ids, sh, NULL, NULL, 0));
	LPT(1, fs_pens(sh, ids, pens, prov_tab, nprov_tab));
}

void lm_tile_pens_ids(const Lightmap *lm, int tx, int ty, unsigned char *pens, unsigned short *ids, const unsigned char *const *prov_tab, int nprov_tab)
{
	static unsigned char sh[1024];
	LPT(0, tile_core(lm, tx, ty, ids, sh, NULL, NULL, 0));
	LPT(1, fs_pens(sh, ids, pens, prov_tab, nprov_tab));
}
