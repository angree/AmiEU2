/* mapview.c - see mapview.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mapview.h"
#include "script.h"

#define NSCALES  26
#define CACHE_W  32            /* direct-mapped cache: slot = (ty & (cw-1)) * cw + (tx & (cw-1)); cw = 32, 64 for screens over ~960 px */
#define CACHE_N  (v->cw * v->cw)

typedef struct { unsigned char pens[1024]; short level, tx, ty, pad; } CTile;   /* pens first: long-aligned */
typedef struct { unsigned short ids[1024]; unsigned char sh[1024]; short level, tx, ty, pad; } ITile;   /* mv_set_idcache */
static const int g_ltw[3] = { 592, 296, 148 }, g_lth[3] = { 228, 114, 57 };   /* tiles per level (MAP_FORMAT.md) */
#define DIRTY_BITS (592L * 228 + 296L * 114 + 148L * 57)

struct MapView {
	Pak *map;
	World *w;
	Lightmap lm[MV_LEVELS];
	unsigned char pal[768];
	char scale_name[NSCALES][20];
	int nscales;
	unsigned char prov_scale[MAX_PROV + 512];
	unsigned char prov_shade[MAX_PROV + 512];  /* the map mode's shade per province, 0..31, 15 normal (eu2.exe +0x5C) */
	unsigned char sh_tab[NSCALES][32][72];     /* scale, province shade, pixel shade + dither (0..70) -> pen */
	unsigned char sh_half[NSCALES][32][72];    /* the same at half brightness: fog of war (shade | MV_SHADE_HALF, exe scale bit 0x20) */
	unsigned char half_lut[256];               /* pen -> the pen nearest to its colour x 0.5 */
	unsigned char flat_tab[72];                /* terra incognita: one pen, no relief, no names (shade 255) */
	const unsigned char *prov_tab[MAX_PROV + 512];
	unsigned short *adj_start, *adj_prov;   /* ADJACENT.TBL for the river pixels (lm_set_adjacency) */
	short *bbox;                               /* boundbox.tbl: x1 y1 x2 y2 per province, full-resolution pixels */
	int sel, sel_dark;                         /* mv_select */
	unsigned char sel_fwd[256], sel_inv[256];  /* the selected province's pens: normal -> darker, and back */
	int sel_on, sel_exact;                     /* sel_fwd is in use; sel_inv undoes it exactly */
	CTile *cache;
	ITile *idc;                                /* mv_set_idcache: ids + shade per cache slot, NULL = off */
	const unsigned char *old_tab[MAX_PROV + 512]; /* mv_refresh_owners: the tables before, to repaint only what changed */
	short old_own[MAX_PROV + 512];
	int have_old;
	unsigned char *dirty;                      /* one bit per tile of levels 1..3 */
	long cx, cy;
	int zoom;
	long last_lx0, last_ly0;   /* last frame's top-left in level pixels */
	int last_w, last_h;
	int left, left_y0;         /* mv_set_left: also draw `left` px left of the view, rows left_y0.. (under the sidebar) */
	int cw;                    /* tile cache width = height, a power of 2 */
	int scr_off;               /* mv_set_scr_off: added to mv_to_screen's x */
	int scr_offy;              /* mv_set_scr_off2: added to mv_to_screen's y (drawing into a strip of the map layer) */
	unsigned long gen;         /* +1 whenever cached tile pixels change (flush, selection remap): the map layer's key */
	MvColorFn color_fn; void *color_user;
	int borders, border_pen;                   /* normal map: country borders drawn into the tiles */
	unsigned short bids[1024];
	short own_tab[MAX_PROV + 512];             /* owner per province id; -2 sea / no border */
	unsigned short ids[1024];
	unsigned char shade[1024];
};


static void norm(const char *in, char *out, int n)
{
	int i = 0;
	for (; *in && i < n - 1; in++) {
		char c = *in;
		if (c == ' ' || c == '_' || c == '-') continue;
		out[i++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	out[i] = 0;
}

/* country.csv colour name -> scale position in colorscales.csv; same names, same order and the
 * same default (LightOrange) as the game's lookup (eu2.exe 0x4690e0, read as specification) */
static int country_scale(const char *name)
{
	static const char *names[22] = { "Black", "Green", "Orange", "LightBlue", "LightGray", "LightBrown", "DarkYellow",
		"Yellow", "DarkGray", "LightYellow", "Gray", "DarkGreen", "Blue", "LightRed", "White", "DarkBrown", "Red",
		"LightGreen", "LightOrange", "Brown", "DarkBlue", "DarkRed" };
	int i;
	if (name) {
		for (i = 0; i < 22; i++) if (script_ieq(name, names[i])) return i;
		if (script_ieq(name, "DarkOrange")) return 22;
	}
	return 18;
}

static int scale_index(const MapView *v, const char *name)
{
	char k[20];
	int i;
	if (!name) return -1;
	norm(name, k, sizeof k);
	for (i = 0; i < v->nscales; i++) if (!strcmp(v->scale_name[i], k)) return i;
	return -1;
}

/* Map/colorscales.csv: "Name;;;" then "red;green;blue;index" then control points r;g;b;index (0..64) */
static int load_scales(MapView *v, Pak *text, char *err, int errlen)
{
	unsigned long sz;
	char *buf = (char *)pak_load(text, "map/colorscales.csv", &sz), *cur, *line, *f[8];
	int pr = 0, pg = 0, pb = 0, pi = -1, s = -1, i;
	static unsigned char full[NSCALES][65][3];
	if (!buf) { snprintf(err, errlen, "map/colorscales.csv missing"); return -1; }
	cur = buf;
	while ((line = csv_line(&cur)) != NULL) {
		int n = csv_split(line, f, 8);
		if (n < 1 || !f[0][0]) continue;
		if (f[0][0] >= '0' && f[0][0] <= '9' && s >= 0 && n >= 4) {
			int r = (int)parse_int(f[0]), g = (int)parse_int(f[1]), b = (int)parse_int(f[2]), ix = (int)parse_int(f[3]);
			if (ix > 64) ix = 64;
			if (pi < 0) { for (i = 0; i <= ix; i++) { full[s][i][0] = (unsigned char)r; full[s][i][1] = (unsigned char)g; full[s][i][2] = (unsigned char)b; } }
			else for (i = pi + 1; i <= ix; i++) {
				int t = ix > pi ? (i - pi) * 256 / (ix - pi) : 256;
				full[s][i][0] = (unsigned char)(pr + (r - pr) * t / 256);
				full[s][i][1] = (unsigned char)(pg + (g - pg) * t / 256);
				full[s][i][2] = (unsigned char)(pb + (b - pb) * t / 256);
			}
			pr = r; pg = g; pb = b; pi = ix;
			for (i = ix + 1; i <= 64; i++) { full[s][i][0] = (unsigned char)r; full[s][i][1] = (unsigned char)g; full[s][i][2] = (unsigned char)b; }
		} else if (!(n >= 2 && script_ieq(f[0], "red") && script_ieq(f[1], "green"))) {   /* "Red;;;" is a scale, "red;green;blue;index" a header */
			if (v->nscales >= NSCALES) break;
			s = v->nscales++;
			norm(f[0], v->scale_name[s], sizeof v->scale_name[s]);
			pi = -1;
		}
	}
	free(buf);
	/* 8 light levels per scale: the colour at shade 8*l+4 */
	for (s = 0; s < v->nscales; s++)
		for (i = 0; i < 8; i++) {
			int pen = MV_PEN_SCALES + s * 8 + i;
			memcpy(v->pal + pen * 3, full[s][i * 8 + 4], 3);
		}
	return v->nscales ? 0 : (snprintf(err, errlen, "no colour scales"), -1);
}

static void ui_palette(unsigned char *pal)
{
	int i;
	for (i = 0; i < 32; i++) { pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (unsigned char)(i * 255 / 31); }
	pal[17 * 3] = 0x20; pal[17 * 3 + 1] = 0x10; pal[17 * 3 + 2] = 0x00;     /* pointer */
	pal[18 * 3] = 0x90; pal[18 * 3 + 1] = 0x60; pal[18 * 3 + 2] = 0x20;
	pal[19 * 3] = 0xF8; pal[19 * 3 + 1] = 0xE8; pal[19 * 3 + 2] = 0xB0;
}

/* The selected province draws darker (0x482870: shade - 16, at least 0; not in the trade mode). The cached tiles
 * keep the normal colours; a tile holding the province gets its pixels remapped by sel_fwd (pen of the normal
 * table at index k -> pen of the darker one at k), so selecting needs no new decoding, and deselecting
 * applies sel_inv when the map is one to one, else redoes only the tiles that hold the province. */
static void build_sel(MapView *v)
{
	int p = v->sel, k, s;
	unsigned char set[256];
	const unsigned char *t0, *t1;
	v->sel_on = 0; v->sel_exact = 1;
	if (p <= 0 || !v->sel_dark || p >= MAX_PROV + 512 || v->prov_shade[p] == 255) return;
	s = v->prov_shade[p] & 31;
	t0 = v->prov_tab[p];
	t1 = ((v->prov_shade[p] & MV_SHADE_HALF) ? v->sh_half : v->sh_tab)[v->prov_scale[p] < NSCALES ? v->prov_scale[p] : 0][s < 16 ? 0 : s - 16];
	for (k = 0; k < 256; k++) { v->sel_fwd[k] = v->sel_inv[k] = (unsigned char)k; set[k] = 0; }
	for (k = 0; k < 72; k++) {
		if (!(set[t0[k]] & 1)) { v->sel_fwd[t0[k]] = t1[k]; set[t0[k]] |= 1; } else if (v->sel_fwd[t0[k]] != t1[k]) v->sel_exact = 0;
		if (!(set[t1[k]] & 2)) { v->sel_inv[t1[k]] = t0[k]; set[t1[k]] |= 2; } else if (v->sel_inv[t1[k]] != t0[k]) v->sel_exact = 0;
	}
	v->sel_on = 1;
}

/* pens[1024] of tile (tx, ty) at lightmap lm: the pixels of province p through lut */
static void remap_tile(MapView *v, Lightmap *lm, int tx, int ty, unsigned char *pens, const unsigned short *ids, int p, const unsigned char *lut)
{
	int i;
	if (!ids) {
		if (!lm_tile_has(lm, tx, ty, (unsigned)p)) return;
		lm_tile(lm, tx, ty, v->ids, NULL);
		ids = v->ids;
	}
	for (i = 0; i < 1024; i++) if (ids[i] == p) pens[i] = lut[pens[i]];
}

static void assign_scales(MapView *v)
{
	int water = scale_index(v, "water"), grey = scale_index(v, "light grey"), ti = scale_index(v, "black");
	int i;
	if (water < 0) water = 0;
	if (grey < 0) grey = 0;
	if (ti < 0) ti = 0;
	memset(v->prov_scale, (unsigned char)grey, sizeof v->prov_scale);
	memset(v->prov_shade, 15, sizeof v->prov_shade);
	for (i = 0; i < v->w->nprov; i++) {
		const Province *p = &v->w->prov[i];
		int s = grey;
		if (i == 0) s = ti;                                          /* Terra Incognita */
		else if (p->terrain == 5 || p->terrain == 6) s = water;      /* ocean, river (Terrain types.csv) */
		else if (p->owner >= 0) {
			int k = country_scale(v->w->country[p->owner].color >= 0 ? world_name(v->w, v->w->country[p->owner].color) : NULL);
			s = k >= 0 ? k : grey;
		}
		v->prov_scale[i] = (unsigned char)s;
	}
	if (v->color_fn) v->color_fn(v->color_user, v, v->prov_scale, v->prov_shade, v->w->nprov);
	for (i = 0; i < MAX_PROV + 512; i++)                  /* the borders' owner table */
		v->own_tab[i] = (short)(i > 0 && i < v->w->nprov && v->w->prov[i].terrain < 5 ? v->w->prov[i].owner : -2);
	/* a larger province shade draws lighter: the pixel's index into the scale moves by 15 - shade
	 * (eu2.exe: colour = table[pixel shade - texture - province shade]; 15 = the normal level) */
	for (i = 0; i < NSCALES; i++) {
		int k, sh;
		for (sh = 0; sh < 32; sh++)
			for (k = 0; k < 72; k++) {
				int kk = k + 15 - sh;
				if (kk < 0) kk = 0;
				if (kk > 71) kk = 71;
				v->sh_tab[i][sh][k] = (unsigned char)(MV_PEN_SCALES + i * 8 + (kk >> 3 > 7 ? 7 : kk >> 3));
				v->sh_half[i][sh][k] = v->half_lut[v->sh_tab[i][sh][k]];
			}
	}
	memset(v->flat_tab, MV_PEN_SCALES + 0 * 8 + 1, sizeof v->flat_tab);   /* the "Black" scale (cream), level 1 */
	for (i = 0; i < MAX_PROV + 512; i++) {
		if (v->prov_shade[i] == 255) { v->prov_tab[i] = v->flat_tab; if (i < MAX_PROV + 512) v->own_tab[i] = -2; continue; }
		v->prov_tab[i] = ((v->prov_shade[i] & MV_SHADE_HALF) ? v->sh_half : v->sh_tab)[v->prov_scale[i] < NSCALES ? v->prov_scale[i] : 0][v->prov_shade[i] & 31];
	}
	build_sel(v);
}

MapView *mv_create(Pak *text, Pak *map, World *w, int cache_tiles, char *err, int errlen)
{
	MapView *v = calloc(1, sizeof *v);
	if (v) { v->cw = CACHE_W; while (v->cw < 128 && (v->cw - 2) * 32 < cache_tiles) v->cw *= 2; }   /* cache_tiles = the screen's larger side in pixels */
	if (!v) { snprintf(err, errlen, "out of memory"); return NULL; }
	v->map = map; v->w = w;
	ui_palette(v->pal);
	if (load_scales(v, text, err, errlen)) { free(v); return NULL; }
	{	/* half brightness: nearest pen of the map palette to colour / 2 (once) */
		int i, j;
		for (i = 0; i < 256; i++) {
			int r0 = v->pal[i * 3] / 2, g0 = v->pal[i * 3 + 1] / 2, b0 = v->pal[i * 3 + 2] / 2, best = 0;
			long bd = 0x7fffffffL;
			for (j = 0; j < 256; j++) {
				long dr, dg, db, d;
				if (j >= 17 && j <= 19) continue;                  /* the pointer's pens */
				dr = v->pal[j * 3] - r0; dg = v->pal[j * 3 + 1] - g0; db = v->pal[j * 3 + 2] - b0;
				d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
				if (d < bd) { bd = d; best = j; }
			}
			v->half_lut[i] = (unsigned char)best;
		}
	}
	v->cache = malloc(sizeof(CTile) * CACHE_N);
	if (!v->cache) { free(v); snprintf(err, errlen, "no memory for the tile cache"); return NULL; }
	mv_flush(v);
	v->dirty = malloc((DIRTY_BITS + 7) / 8);   /* 22 KB; without it every change repaints everything */
	assign_scales(v);
	{	/* ADJACENT.TBL (u32 start[1616], then { u32 province, type, unused }) as u16 for the river pixels */
		unsigned long sz, k, n;
		unsigned char *a = pak_load(map, "map/adjacent.tbl", &sz);
		if (a && sz >= 1616 * 4 && (v->adj_start = malloc(1617 * sizeof(unsigned short))) != NULL) {
			n = (sz - 1616 * 4) / 12;
			if ((v->adj_prov = malloc(n * sizeof(unsigned short) + 2)) != NULL) {
				for (k = 0; k < 1616; k++) { unsigned long s = a[4 * k] | a[4 * k + 1] << 8 | (unsigned long)a[4 * k + 2] << 16; v->adj_start[k] = (unsigned short)(s > n ? n : s); }
				v->adj_start[1616] = (unsigned short)n;
				for (k = 0; k < n; k++) v->adj_prov[k] = (unsigned short)(a[1616 * 4 + k * 12] | a[1616 * 4 + k * 12 + 1] << 8);
				lm_set_adjacency(v->adj_start, v->adj_prov, 1615);
			}
		}
		free(a);
		if ((a = pak_load(map, "map/boundbox.tbl", &sz)) != NULL && sz >= 16 && (v->bbox = malloc(sz / 4 * sizeof(short))) != NULL)
			for (k = 0; k < sz / 4; k++) v->bbox[k] = (short)(a[4 * k] | a[4 * k + 1] << 8);   /* i32, all < 32768 */
		free(a);
	}
	v->cx = 9500; v->cy = 1900; v->zoom = 2;
	return v;
}

void mv_destroy(MapView *v)
{
	int i;
	if (!v) return;
	for (i = 0; i < MV_LEVELS; i++) if (v->lm[i].raw) lm_free(&v->lm[i]);
	lm_set_adjacency(NULL, NULL, 0);
	free(v->adj_start); free(v->adj_prov); free(v->bbox); free(v->idc); free(v->dirty);
	free(v->cache);
	free(v);
}

const unsigned char *mv_palette(const MapView *v) { return v->pal; }
void mv_flush(MapView *v) { int i; for (i = 0; i < CACHE_N; i++) v->cache[i].level = -1; v->gen++; }
unsigned long mv_gen(const MapView *v) { return v->gen; }
int mv_prov_centre(const MapView *v, int prov, long *x, long *y)
{
	if (!v->bbox || prov <= 0 || prov >= 1615) return 0;
	*x = ((long)v->bbox[4 * prov] + v->bbox[4 * prov + 2]) / 2; *y = ((long)v->bbox[4 * prov + 1] + v->bbox[4 * prov + 3]) / 2;
	return 1;
}

void mv_set(MapView *v, long cx, long cy, int zoom)
{
	if (zoom < 1) zoom = 1;
	if (zoom > MV_LEVELS) zoom = MV_LEVELS;
	cx %= 18944; if (cx < 0) cx += 18944;
	if (cy < 0) cy = 0;
	if (cy > 7295) cy = 7295;
	v->cx = cx; v->cy = cy; v->zoom = zoom;
}

void mv_get(const MapView *v, long *cx, long *cy, int *zoom) { *cx = v->cx; *cy = v->cy; *zoom = v->zoom; }

static unsigned long (*g_us)(void), g_pt[4];
extern unsigned long (*lm_probe_us)(void), lm_probe_t[2];
void mv_probe_clock(unsigned long (*us)(void)) { g_us = us; lm_probe_us = us; }
void mv_probe_read(unsigned long t[6]) { int i; for (i = 0; i < 4; i++) { t[i] = g_pt[i]; g_pt[i] = 0; } t[4] = lm_probe_t[0]; t[5] = lm_probe_t[1]; lm_probe_t[0] = lm_probe_t[1] = 0; }
#define PT0 unsigned long pt_a = g_us ? g_us() : 0, pt_b
#define PT(k) do { if (g_us) { pt_b = g_us(); g_pt[k] += pt_b - pt_a; pt_a = pt_b; } } while (0)

static const unsigned char *tile_pens(MapView *v, Lightmap *lm, int tx, int ty, int *decoded)
{
	CTile *c = &v->cache[(ty & (v->cw - 1)) * v->cw + (tx & (v->cw - 1))];
	if (c->level == lm->level && c->tx == tx && c->ty == ty) return c->pens;
	if (v->idc) {                     /* ids + shade kept: the dither (and borders) only */
		ITile *d = &v->idc[(ty & (v->cw - 1)) * v->cw + (tx & (v->cw - 1))];
		PT0;
		if (d->level != lm->level || d->tx != tx || d->ty != ty) {
			lm_tile(lm, tx, ty, d->ids, d->sh);
			d->level = (short)lm->level; d->tx = (short)tx; d->ty = (short)ty;
		}
		PT(0);
		lm_pens_from(d->sh, d->ids, c->pens, (const unsigned char *const *)v->prov_tab, MAX_PROV + 512);
		PT(1);
		if (v->borders) {
			const short *own = v->own_tab;
			const unsigned short *id = d->ids;
			unsigned char *pen = c->pens;
			int x, y, bp = v->border_pen;
			for (y = 0; y < 32; y++, id += 32, pen += 32)
				for (x = 0; x < 32; x++) {
					int o = own[id[x]], q;
					if (o == -2) continue;
					if (x < 31 && (q = own[id[x + 1]]) != -2 && q != o) { pen[x] = (unsigned char)bp; continue; }
					if (y < 31 && (q = own[id[x + 32]]) != -2 && q != o) pen[x] = (unsigned char)bp;
				}
		}
		PT(1);
		if (v->sel_on) remap_tile(v, lm, tx, ty, c->pens, d->ids, v->sel, v->sel_fwd);
		PT(2);
	} else if (!v->borders) {
		PT0;
		lm_tile_pens(lm, tx, ty, c->pens, (const unsigned char *const *)v->prov_tab, MAX_PROV + 512);
		PT(0);
		if (v->sel_on) remap_tile(v, lm, tx, ty, c->pens, NULL, v->sel, v->sel_fwd);
		PT(2);
	} else {	/* country borders (manual p.11): a pixel whose right or lower neighbour has another owner turns dark */
		const short *own = v->own_tab;
		const unsigned short *id = v->bids;
		unsigned char *pen = c->pens;
		int x, y, bp = v->border_pen;
		PT0;
		lm_tile_pens_ids(lm, tx, ty, c->pens, v->bids, (const unsigned char *const *)v->prov_tab, MAX_PROV + 512);
		PT(0);
		for (y = 0; y < 32; y++, id += 32, pen += 32)
			for (x = 0; x < 32; x++) {
				int o = own[id[x]], q;
				if (o == -2) continue;
				if (x < 31 && (q = own[id[x + 1]]) != -2 && q != o) { pen[x] = (unsigned char)bp; continue; }
				if (y < 31 && (q = own[id[x + 32]]) != -2 && q != o) pen[x] = (unsigned char)bp;
			}
		PT(1);
		if (v->sel_on) remap_tile(v, lm, tx, ty, c->pens, v->bids, v->sel, v->sel_fwd);
		PT(2);
	}
	c->level = (short)lm->level; c->tx = (short)tx; c->ty = (short)ty;
	(*decoded)++;
	return c->pens;
}

void mv_set_left(MapView *v, int left, int y0) { v->left = left > 0 ? left : 0; v->left_y0 = y0; }
void mv_set_scr_off(MapView *v, int off) { v->scr_off = off; }
void mv_set_scr_off2(MapView *v, int offx, int offy) { v->scr_off = offx; v->scr_offy = offy; }

/* level pixels (lx0, ly0).. of w x h into dst */
static void draw_region(MapView *v, Lightmap *lm, unsigned char *dst, int pitch, long lx0, long ly0, int w, int h, int *decoded)
{
	long lw = (long)lm->tw * 32, lh = (long)lm->th * 32, y;
	lx0 %= lw; if (lx0 < 0) lx0 += lw;
	for (y = 0; y < h; ) {
		long ly = ly0 + y;
		int ty = (int)(ly >> 5), oy = (int)(ly & 31), rows = 32 - oy, x;
		if (rows > h - y) rows = (int)(h - y);
		if (ly >= lh) { int r; for (r = 0; r < rows; r++) memset(dst + (y + r) * pitch, 0, w); y += rows; continue; }
		for (x = 0; x < w; ) {
			long lx = (lx0 + x) % lw;
			int tx = (int)(lx >> 5), ox = (int)(lx & 31), cols = 32 - ox, r;
			const unsigned char *p;
			if (cols > w - x) cols = w - x;
			p = tile_pens(v, lm, tx, ty, decoded) + oy * 32 + ox;
			for (r = 0; r < rows; r++) memcpy(dst + (y + r) * pitch + x, p + r * 32, cols);
			x += cols;
		}
		y += rows;
	}
}

int mv_draw(MapView *v, unsigned char *dst, int pitch, int w, int h)
{
	Lightmap *lm = &v->lm[v->zoom - 1];
	int shift = v->zoom - 1, decoded = 0;
	long lw, lh, lx0, ly0;
	if (!lm->raw && lm_load(lm, v->map, v->zoom)) return -1;
	lw = (long)lm->tw * 32; lh = (long)lm->th * 32;
	lx0 = (v->cx >> shift) - w / 2;
	ly0 = (v->cy >> shift) - h / 2;
	if (ly0 > lh - h) ly0 = lh - h;
	if (ly0 < 0) ly0 = 0;
	lx0 %= lw; if (lx0 < 0) lx0 += lw;
	v->last_lx0 = lx0; v->last_ly0 = ly0; v->last_w = w; v->last_h = h;
	draw_region(v, lm, dst, pitch, lx0, ly0, w, h, &decoded);
	if (v->left > 0 && v->left_y0 < h)              /* the same map continued under the sidebar */
		draw_region(v, lm, dst + (long)v->left_y0 * pitch - v->left, pitch, lx0 - v->left, ly0 + v->left_y0, v->left, h - v->left_y0, &decoded);
	return decoded;
}

/* the view of w x h at the current centre: its top-left in level pixels and the level's width; makes it the
 * current frame (as mv_draw) and draws only the rectangle rx, ry, rw x rh of it into dst (the view's origin) */
int mv_draw_rect(MapView *v, unsigned char *dst, int pitch, int w, int h, int rx, int ry, int rw, int rh, long *plx0, long *ply0, long *plw)
{
	Lightmap *lm = &v->lm[v->zoom - 1];
	int shift = v->zoom - 1, decoded = 0;
	long lw, lh, lx0, ly0;
	if (!lm->raw && lm_load(lm, v->map, v->zoom)) return -1;
	lw = (long)lm->tw * 32; lh = (long)lm->th * 32;
	lx0 = (v->cx >> shift) - w / 2;
	ly0 = (v->cy >> shift) - h / 2;
	if (ly0 > lh - h) ly0 = lh - h;
	if (ly0 < 0) ly0 = 0;
	lx0 %= lw; if (lx0 < 0) lx0 += lw;
	v->last_lx0 = lx0; v->last_ly0 = ly0; v->last_w = w; v->last_h = h;
	if (rw > 0 && rh > 0) draw_region(v, lm, dst + (long)ry * pitch + rx, pitch, lx0 + rx, ly0 + ry, rw, rh, &decoded);
	if (plx0) *plx0 = lx0;
	if (ply0) *ply0 = ly0;
	if (plw) *plw = lw;
	return decoded;
}

int mv_province_at(MapView *v, int sx, int sy, int w, int h)
{
	Lightmap *lm = &v->lm[v->zoom - 1];
	long lx, ly;
	if (!lm->raw || sx < -v->left || (sx < 0 && sy < v->left_y0) || sy < 0 || sx >= v->last_w || sy >= v->last_h) return -1;
	(void)w; (void)h;
	lx = (v->last_lx0 + sx) % ((long)lm->tw * 32);
	if (lx < 0) lx += (long)lm->tw * 32;
	ly = v->last_ly0 + sy;
	lm_tile(lm, (int)(lx >> 5), (int)(ly >> 5), v->ids, NULL);
	{	/* a river line picks the land next to it (nearest pixel of the tile that is not a river) */
		int x = (int)(lx & 31), y = (int)(ly & 31), id = v->ids[y * 32 + x], r, dx, dy;
		if (id <= 0 || id >= v->w->nprov || v->w->prov[id].terrain != 6) return id;
		for (r = 1; r < 32; r++)
			for (dy = -r; dy <= r; dy++)
				for (dx = -r; dx <= r; dx++) {
					int xx = x + dx, yy = y + dy, q;
					if ((dx != -r && dx != r && dy != -r && dy != r) || xx < 0 || yy < 0 || xx > 31 || yy > 31) continue;
					q = v->ids[yy * 32 + xx];
					if (q > 0 && q < v->w->nprov && v->w->prov[q].terrain != 6) return q;
				}
		return id;
	}
}

int mv_to_screen(const MapView *v, long fx, long fy, int *sx, int *sy)
{
	int shift = v->zoom - 1;
	long lw = 18944L >> shift, x = (fx >> shift) - v->last_lx0;
	if (!v->last_w) return 0;
	x %= lw; if (x < 0) x += lw;               /* the globe wraps east-west */
	if (x > lw - 64 - v->left) x -= lw;        /* just left of the view (or under the sidebar) */
	*sx = (int)x + v->scr_off;
	*sy = (int)((fy >> shift) - v->last_ly0) + v->scr_offy;
	return 1;
}

int mv_set_idcache(MapView *v, int on)
{
	int i;
	free(v->idc); v->idc = NULL;
	if (!on) return 0;
	if ((v->idc = malloc(sizeof(ITile) * CACHE_N)) == NULL) return -1;
	for (i = 0; i < CACHE_N; i++) v->idc[i].level = -1;
	return 0;
}

/* Only the tiles of the provinces whose look changed (owner, map-mode colour, winter, fog) go out of the
 * cache (user 2026-11-11); a map mode change touches nearly all of them and flushes everything. A border
 * between two provinces lies in both bounding boxes, so the neighbour's side is repainted too. */
void mv_refresh_owners(MapView *v)
{
	static unsigned short chg[400];
	int i, n = 0, all = !v->have_old || !v->bbox || !v->dirty;
	PT0;
	if (!all) { memcpy(v->old_tab, v->prov_tab, sizeof v->old_tab); memcpy(v->old_own, v->own_tab, sizeof v->old_own); }
	assign_scales(v);
	v->have_old = 1;
	PT(3);
	if (!all)
		for (i = 0; i < MAX_PROV + 512 && !all; i++)
			if (v->prov_tab[i] != v->old_tab[i] || v->own_tab[i] != v->old_own[i]) {
				if (i >= 1615 || n >= 400) all = 1; else chg[n++] = (unsigned short)i;
			}
	if (all) { mv_flush(v); return; }
	if (n == 0) return;
	memset(v->dirty, 0, (DIRTY_BITS + 7) / 8);
	for (i = 0; i < n; i++) {
		int p = chg[i], L;
		long base = 0;
		for (L = 0; L < 3; L++) {
			int x0 = v->bbox[4 * p] >> (5 + L), y0 = v->bbox[4 * p + 1] >> (5 + L), x1 = v->bbox[4 * p + 2] >> (5 + L), y1 = v->bbox[4 * p + 3] >> (5 + L), x, y;
			if (x1 < x0) { x0 = 0; x1 = g_ltw[L] - 1; }                 /* across the date line */
			if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
			if (x1 >= g_ltw[L]) x1 = g_ltw[L] - 1; if (y1 >= g_lth[L]) y1 = g_lth[L] - 1;
			for (y = y0; y <= y1; y++)
				for (x = x0; x <= x1; x++) { long b = base + (long)y * g_ltw[L] + x; v->dirty[b >> 3] |= (unsigned char)(1 << (b & 7)); }
			base += (long)g_ltw[L] * g_lth[L];
		}
	}
	for (i = 0; i < CACHE_N; i++) {
		CTile *t = &v->cache[i];
		long b;
		int L = t->level - 1;
		if (L < 0 || L > 2) continue;
		b = (L > 0 ? 592L * 228 : 0) + (L > 1 ? 296L * 114 : 0) + (long)t->ty * g_ltw[L] + t->tx;
		if (v->dirty[b >> 3] & (1 << (b & 7))) t->level = -1;
	}
	v->gen++;
}

int mv_adj(const MapView *v, int prov, const unsigned short **list)
{
	if (!v->adj_start || prov < 0 || prov >= 1615) { *list = NULL; return 0; }
	*list = v->adj_prov + v->adj_start[prov];
	return v->adj_start[prov + 1] - v->adj_start[prov];
}

static void remap_cached(MapView *v, int p, const unsigned char *lut, int exact)   /* every cached tile holding p */
{
	int i;
	v->gen++;
	for (i = 0; i < CACHE_N; i++) {
		CTile *t = &v->cache[i];
		Lightmap *lm;
		if (t->level <= 0 || t->level > MV_LEVELS) continue;
		lm = &v->lm[t->level - 1];
		if (!lm->raw) { t->level = -1; continue; }
		if (v->bbox && p < 1615) {                      /* outside the province's bounding box (full pixels) */
			int sh = t->level - 1;
			long x0 = (long)t->tx << (5 + sh), y0 = (long)t->ty << (5 + sh), x1 = x0 + (32L << sh), y1 = y0 + (32L << sh);
			if (!(x1 > v->bbox[4 * p] && x0 <= v->bbox[4 * p + 2] && y1 > v->bbox[4 * p + 1] && y0 <= v->bbox[4 * p + 3])) continue;
		}
		if (!lm_tile_has(lm, t->tx, t->ty, (unsigned)p)) continue;
		if (!exact) { t->level = -1; continue; }
		lm_tile(lm, t->tx, t->ty, v->ids, NULL);
		{ int k; for (k = 0; k < 1024; k++) if (v->ids[k] == p) t->pens[k] = lut[t->pens[k]]; }
	}
}

void mv_select(MapView *v, int prov, int dark)
{
	if (prov == v->sel && dark == v->sel_dark) return;
	if (v->sel_on) remap_cached(v, v->sel, v->sel_inv, v->sel_exact);   /* the old one back */
	v->sel = prov; v->sel_dark = dark;
	build_sel(v);
	if (v->sel_on) remap_cached(v, v->sel, v->sel_fwd, 1);             /* the new one darker */
}
void mv_set_borders(MapView *v, int on)
{
	int b = scale_index(v, "border");                      /* the red line (220,0,0), colorscale 24 */
	v->border_pen = MV_PEN_SCALES + (b >= 0 ? b : 0) * 8 + 3;
	if (v->borders != on) { v->borders = on; mv_flush(v); }
}
void mv_set_color_fn(MapView *v, MvColorFn fn, void *user) { v->color_fn = fn; v->color_user = user; mv_refresh_owners(v); }
int  mv_scale_named(const MapView *v, const char *name) { return scale_index(v, name); }
int  mv_scale_rgb(const MapView *v, int r, int g, int b)
{
	int s, best = 0;
	long bd = 0x7fffffffL;
	for (s = 0; s < v->nscales && s < 23; s++) {      /* 23.. = water and the border scales */
		const unsigned char *c = v->pal + (MV_PEN_SCALES + s * 8 + 3) * 3;
		long dr = c[0] - r, dg = c[1] - g, db = c[2] - b, d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < bd) { bd = d; best = s; }
	}
	return best;
}
int  mv_country_scale(const MapView *v, int c)
{
	if (c < 0 || c >= v->w->ncountry) return -1;
	return country_scale(v->w->country[c].color >= 0 ? world_name(v->w, v->w->country[c].color) : NULL);
}
