/* units.c - armies and fleets on the map: one standing sprite per province that holds units,
 * nationality sprite set from country.csv (fallback MIN), drawn after the map tiles. Portable C. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "units.h"
#include "sprite.h"
#include "ui.h"
#include "military.h"

#define MAXSPR 512                         /* stands, cities and A-WALK x 8 directions (a missing name takes a slot too) */
typedef struct { char name[72]; int state; Sprite s; } SprSlot;   /* state 0 empty, 1 ok, -1 missing */
static SprSlot g_spr[MAXSPR];
static Pak *g_text, *g_gfx;
static const unsigned char *g_inv;
static unsigned char g_seen[MAX_PROV];
static short g_own[MAX_PROV][2];                    /* a stack's owner (the first unit there), land / sea */
static long g_cnt[MAX_PROV][2];                     /* its number on the map (0x4902a3) */
static short g_walk[MAX_PROV][2];                   /* the stack's next path province (A-WALK towards it), 0 standing */
static int g_anim;                                  /* units_set_anim: the sprites' frames run (else frame 0) */
void units_set_anim(int on) { g_anim = on; }

/* docs/SPEC_UNIT_UI.md: v1.09 draws no selection ring; the selected units get statusbarselect (9 frames
 * of 44x7) under their position, their routes (ribbon + t-map_arrows), and an order plays
 * moveorderanim (21 frames of 40x40) / illegalmoveorder (10 frames of 24x25) once at the click point */
static Sprite g_sbsel, g_dest, g_illegal, g_arrows, g_ring[2];
static long g_today;
/* the leg in progress, for the route's travelled (green) part: when we first saw the unit on this
 * leg (visual only, not saved) */
static short g_leg_loc[MAX_LAND + MAX_NAVAL];
static long g_leg_arr[MAX_LAND + MAX_NAVAL], g_leg_t0[MAX_LAND + MAX_NAVAL];
void units_set_today(long day) { g_today = day; }
static int g_ring_ok;
static short g_sl[64], g_sf[64];
static int g_nsl, g_nsf;
static unsigned long g_ms, g_anim_t0;
static int g_anim_kind, g_anim_x, g_anim_y;     /* 1 order, 2 refused; screen point in the map area */

void units_init(Pak *text, Pak *gfx, const unsigned char *inv) { g_text = text; g_gfx = gfx; g_inv = inv; g_ring_ok = 0; }
/* the selection animations (ring, statusbarselect) play once from the moment of selecting and stay on
 * their last frame (user 2026-11-11: they looped) */
static unsigned long g_sel_t0;
static int g_sel_new;
void units_select_list(const short *a, int na, const short *f, int nf)
{
	int nl = na < 0 ? 0 : na > 64 ? 64 : na, nn = nf < 0 ? 0 : nf > 64 ? 64 : nf;
	if (nl != g_nsl || nn != g_nsf || (nl && memcmp(g_sl, a, sizeof g_sl[0] * (size_t)nl)) || (nn && memcmp(g_sf, f, sizeof g_sf[0] * (size_t)nn)))
		g_sel_new = nl + nn > 0;
	g_nsl = nl; g_nsf = nn;
	if (g_nsl) memcpy(g_sl, a, sizeof g_sl[0] * (size_t)g_nsl);
	if (g_nsf) memcpy(g_sf, f, sizeof g_sf[0] * (size_t)g_nsf);
}
static int sel_frame(unsigned long step, int frames)
{
	unsigned long k;
	if (g_sel_new) { g_sel_t0 = g_ms; g_sel_new = 0; }
	k = (g_ms - g_sel_t0) / step;
	return k >= (unsigned long)frames ? frames - 1 : (int)k;
}
void units_select(int army, int fleet)
{
	short a = (short)army, f = (short)fleet;
	units_select_list(&a, army >= 0, &f, fleet >= 0);
}
void units_set_time(unsigned long ms) { g_ms = ms; }
void units_order_anim(int x, int y, int ok) { g_anim_kind = ok ? 1 : 2; g_anim_x = x; g_anim_y = y; g_anim_t0 = g_ms; }
int  units_anim_active(void) { return (g_anim_kind && g_ms - g_anim_t0 < (g_anim_kind == 1 ? 21UL : 10UL) * 50UL) || (g_nsl + g_nsf > 0 && (g_sel_new || g_ms - g_sel_t0 < 1100UL)); }
static int sel_army(int k) { int i; for (i = 0; i < g_nsl; i++) if (g_sl[i] == k) return 1; return 0; }
static int sel_fleet(int k) { int i; for (i = 0; i < g_nsf; i++) if (g_sf[i] == k) return 1; return 0; }
static int g_viewer = -1;
void units_set_viewer(int c) { g_viewer = c; }
static const unsigned char *g_fogvis;          /* units_set_fog */
void units_set_fog(const unsigned char *vis) { g_fogvis = vis; }
#define HIDDEN(w, p) (g_viewer >= 0 && !WORLD_KNOWN(w, g_viewer, p))
static const Sprite *get_sprite(const char *kind, const char *tag);

/* the army / navy sprites' size per zoom (user 2026-11-10: zoom 1 full size was too big, zoom 3 quarter too
 * small, zoom 2 half right): three Lanczos-scaled sets instead of the exe's 2^zoom pixel dropping (+0x23e) */
/* user 2026-11-11: closest zoom 700 was too small (the exe draws 1000 / 500 / 250) */
static int g_usz[3] = { 850, 500, 350 };
void units_set_sizes(int z1, int z2, int z3)
{
	if (z1 >= 100 && z1 <= 2000) g_usz[0] = z1;
	if (z2 >= 100 && z2 <= 2000) g_usz[1] = z2;
	if (z3 >= 100 && z3 <= 2000) g_usz[2] = z3;
}
static int unit_scale(const MapView *mv) { long cx, cy; int z; mv_get(mv, &cx, &cy, &z); return g_usz[z <= 1 ? 0 : z == 2 ? 1 : 2]; }
static const Sprite *get_sprite_sc(const char *kind, const char *tag, int sc);
static const Sprite *spr_named_sc(const char *name0, int sc);
static const Sprite *spr_named(const char *name0) { return spr_named_sc(name0, 1000); }
static const Sprite *spr_named_sc(const char *name0, int sc)
{
	char name[72], key[72];
	int i, free_slot = -1;
	snprintf(name, sizeof name, "%s", name0);
	for (i = 0; name[i]; i++) if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 32;
	if (sc != 1000) snprintf(key, sizeof key, "%s@%d", name, sc); else snprintf(key, sizeof key, "%s", name);
	for (i = 0; i < MAXSPR; i++) {
		if (g_spr[i].state && !strcmp(g_spr[i].name, key)) return g_spr[i].state > 0 ? &g_spr[i].s : NULL;
		if (!g_spr[i].state && free_slot < 0) free_slot = i;
	}
	if (free_slot < 0) return NULL;
	snprintf(g_spr[free_slot].name, sizeof g_spr[free_slot].name, "%s", key);
	g_spr[free_slot].state = sprite_load_scaled(&g_spr[free_slot].s, g_text, g_gfx, name, g_inv, sc) == 0 ? 1 : -1;
	return g_spr[free_slot].state > 0 ? &g_spr[free_slot].s : NULL;
}

/* cities, colonies and trading posts at the city position (manual p.11): T-CITY L-<1..3> C-<nationality>
 * [A-Fort0..2 for MIN / MUS], T-BUILD A-COLONY, T-BUILD A-TPOST. Size and fort thresholds: NIEPEWNE. */
static int zsh(const MapView *mv) { long cx, cy; int z; mv_get(mv, &cx, &cy, &z); return z > 1 ? z - 1 : 0; }   /* [0x7fb8cc] */

int units_draw_cities(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh)
{
	int p, n = 0;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		const Sprite *s;
		char name[72];
		int x, y;
		if (pv->owner < 0 || pv->terrain >= 5 || (pv->population <= 0 && !pv->tp) || HIDDEN(w, p)) continue;
		if (!mv_to_screen(mv, pv->city_x, pv->city_y, &x, &y) || x < -64 || y < -64 || x > sw + 64 || y > sh + 64) continue;
		if (pv->tp) snprintf(name, sizeof name, "gfx/map/units/t-build a-tpost.spr");
		else if (pv->population < 1000) snprintf(name, sizeof name, "gfx/map/units/t-build a-colony.spr");
		else {
			const char *nat = w->country[pv->owner].city_nat[0] ? w->country[pv->owner].city_nat : "MIN";
			int L = pv->population < 5000 ? 1 : pv->population < 20000 ? 2 : 3, fl = pv->fort >= 4 ? 2 : pv->fort >= 2 ? 1 : 0;
			if (!strcmp(nat, "MIN") || !strcmp(nat, "MUS")) snprintf(name, sizeof name, "gfx/map/units/t-city l-%d c-%s a-fort%d.spr", L, nat, fl);
			else snprintf(name, sizeof name, "gfx/map/units/t-city l-%d c-%s.spr", L, nat);
		}
		if (w->country[pv->owner].capital == p) {            /* the capital: the stability medallion over the town, drawn first (0x47fa3d) */
			const Sprite *st = spr_named("gfx/map/units/t-stability.spr");
			int f = w->country[pv->owner].stability + 3;      /* -3 stormy red .. +3 clear sky (0x47fa02) */
			if (st) sprite_draw_shift(st, f < 0 ? 0 : f > 6 ? 6 : f, dst, pitch, sw, sh, x, y, zsh(mv));
		}
		if (!(s = spr_named(name)) && !(s = spr_named("gfx/map/units/t-city l-1 c-min a-fort0.spr"))) continue;
		sprite_draw_shift(s, 0, dst, pitch, sw, sh, x, y, zsh(mv));   /* map objects shrink with the zoom (0x40fb1d) */
		n++;
	}
	return n;
}

/* sieges on the map: the siege tower (T-ARMY A-SIEGETOWER) at the besieged city */
int units_draw_sieges(const World *w, const MapView *mv, const Military *m, unsigned char *dst, int pitch, int sw, int sh)
{
	int p, n = 0;
	const Sprite *s = NULL;
	for (p = 1; p < w->nprov; p++) {
		int x, y;
		if (mil_besieger(m, p) < 0) continue;
		if (!mv_to_screen(mv, w->prov[p].city_x, w->prov[p].city_y, &x, &y) || x < -64 || y < -64 || x > sw + 64 || y > sh + 64) continue;
		if (!s && !(s = spr_named("gfx/map/units/t-army a-siegetower.spr"))) return 0;
		sprite_draw_shift(s, 0, dst, pitch, sw, sh, x + (10 >> zsh(mv)), y, zsh(mv));
		n++;
	}
	return n;
}

/* political map (manual p.11): small variants of our shield on our core provinces, at the city */
static Sprite g_core; static int g_core_c = -1, g_core_state;
int units_draw_cores(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int player)
{
	int p, k, n = 0;
	if (player < 0) return 0;
	if (g_core_c != player) {
		char path[64];
		if (g_core_state == 1) sprite_free(&g_core);
		snprintf(path, sizeof path, "gfx/map/shields/small/smallshield_%s.bmp", w->country[player].tag);
		{ char *q; for (q = path; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32); }
		g_core_state = sprite_from_bmp(&g_core, g_gfx, path, g_inv, 4, 1000) ? 2 : 1;
		g_core_c = player;
	}
	if (g_core_state != 1) return 0;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		int x, y, core = 0;
		for (k = 0; k < 8; k++) if (pv->cores[k] == player) core = 1;
		if (!core || !mv_to_screen(mv, pv->city_x, pv->city_y, &x, &y) || x < -32 || y < -32 || x > sw + 32 || y > sh + 32) continue;
		sprite_draw(&g_core, 0, dst, pitch, sw, sh, x - g_core.w / 2, y - g_core.h / 2);
		n++;
	}
	return n;
}

/* landscape sprites of the normal map (GFX/Map/Landscape/<terrain><variant>.bmp at the province.csv
 * positions), drawn under the units at the closest zoom only (NIEPEWNE: what the original shows when
 * zoomed out) */
static Sprite g_land[5][8]; static unsigned char g_land_state[5][8];
int units_draw_terrain(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int zoom)
{
	static const char *tn[5] = { "plains", "forest", "mountain", "desert", "marsh" };
	int p, k, n = 0;
	if (zoom != 1) return 0;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		if (pv->terrain > 4 || HIDDEN(w, p)) continue;
		for (k = 0; k < 4; k++) {
			int x, y, t = pv->terrain, v = pv->tsv[k] & 7;
			Sprite *s = &g_land[t][v];
			if (pv->tsx[k] < 0 || !mv_to_screen(mv, pv->tsx[k], pv->tsy[k], &x, &y) || x < -160 || y < -60 || x > sw + 16 || y > sh + 16) continue;
			if (!g_land_state[t][v]) {
				char path[64];
				snprintf(path, sizeof path, "gfx/map/landscape/%s%d.bmp", tn[t], v);
				g_land_state[t][v] = sprite_from_bmp(s, g_gfx, path, g_inv, 1, 1000) ? 2 : 1;
			}
			if (g_land_state[t][v] != 1) continue;
			sprite_draw(s, 0, dst, pitch, sw, sh, x, y);
			n++;
		}
	}
	return n;
}

static void rings_load(void)
{
	if (g_ring_ok) return;
	g_ring_ok = 1;                                   /* map overlays keep the map's own pixels: no rescale */
	if (sprite_from_bmp(&g_sbsel, g_gfx, "gfx/interface/statusbarselect.bmp", g_inv, 9, 1000)) memset(&g_sbsel, 0, sizeof g_sbsel);
	if (sprite_from_bmp(&g_ring[0], g_gfx, "gfx/interface/selectanim_army.bmp", g_inv, 13, 1000)) memset(&g_ring[0], 0, sizeof g_ring[0]);
	if (sprite_from_bmp(&g_ring[1], g_gfx, "gfx/interface/selectanim_navy.bmp", g_inv, 13, 1000)) memset(&g_ring[1], 0, sizeof g_ring[1]);
	if (sprite_from_bmp(&g_dest, g_gfx, "gfx/interface/moveorderanim.bmp", g_inv, 21, 1000)) memset(&g_dest, 0, sizeof g_dest);
	if (sprite_from_bmp(&g_illegal, g_gfx, "gfx/interface/illegalmoveorder.bmp", g_inv, 10, 1000)) memset(&g_illegal, 0, sizeof g_illegal);
	if (sprite_from_bmp(&g_arrows, g_gfx, "gfx/map/t-map_arrows.bmp", g_inv, 8, 1000)) memset(&g_arrows, 0, sizeof g_arrows);
}

/* the screen position of a unit stack: armies at the province's unit position, fleets in a land
 * province at its port */
static int unit_pos(const World *w, const MapView *mv, int land, int loc, int *x, int *y)
{
	const Province *p = &w->prov[loc];
	int port = !land && p->terrain < 5 && p->port_x;
	return mv_to_screen(mv, port ? p->port_x : p->army_x, port ? p->port_y : p->army_y, x, y);
}

/* the stack's sprite box (one sprite per province and kind) */
static int stack_box(const World *w, const MapView *mv, int land, int loc, int owner, int *x0, int *y0, int *x1, int *y1)
{
	const Sprite *s;
	int x, y;
	if (!unit_pos(w, mv, land, loc, &x, &y)) return 0;
	s = get_sprite_sc(land ? "army" : "navy", land ? w->country[owner].army_spr : w->country[owner].navy_spr, unit_scale(mv));
	if (!s) return 0;
	*x0 = x - s->ox; *y0 = y - s->oy; *x1 = *x0 + s->w; *y1 = *y0 + s->h;
	return 1;
}

/* a click: the province of an own unit sprite under (sx,sy), armies first; *naval = 1 for a fleet; 0 none */
int units_pick(const World *w, const MapView *mv, int sx, int sy, int player, int *naval)
{
	int pass, i, x0, y0, x1, y1;
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < (pass ? w->nnaval : w->nland); i++) {
			int loc = pass ? w->naval[i].location : w->land[i].location, own = pass ? w->naval[i].owner : w->land[i].owner;
			if (own != player || loc <= 0 || loc >= w->nprov || (!pass && w->land[i].dead)) continue;
			if (pass && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports <= 0) continue;
			if (stack_box(w, mv, !pass, loc, own, &x0, &y0, &x1, &y1) && sx >= x0 && sx < x1 && sy >= y0 && sy < y1) { *naval = pass; return loc; }
		}
	return 0;
}

/* the rubber band: own armies whose sprite box meets the rectangle, else own fleets (0x52bd42) */
int units_in_rect(const World *w, const MapView *mv, int rx0, int ry0, int rx1, int ry1, int player, short *out, int max, int *naval)
{
	int pass, i, n = 0, x0, y0, x1, y1;
	for (pass = 0; pass < 2 && !n; pass++)
		for (i = 0; i < (pass ? w->nnaval : w->nland) && n < max; i++) {
			int loc = pass ? w->naval[i].location : w->land[i].location, own = pass ? w->naval[i].owner : w->land[i].owner;
			if (own != player || loc <= 0 || loc >= w->nprov || (!pass && w->land[i].dead)) continue;
			if (pass && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports <= 0) continue;
			if (stack_box(w, mv, !pass, loc, own, &x0, &y0, &x1, &y1) && x1 > rx0 && x0 <= rx1 && y1 > ry0 && y0 <= ry1) { out[n++] = (short)i; *naval = pass; }
		}
	return n;
}

/* ---- the route of a selected unit (0x408f50..0x409ab0): a Hermite curve through the unit and the
 * path's provinces, a 12 map px ribbon (shadow, then red with a bright centre line), and an arrow of
 * t-map_arrows at every known point but the last, its frame by the curve's direction ---- */
static void tri(unsigned char *dst, int pitch, int sw, int sh, int x0, int y0, int x1, int y1, int x2, int y2, int pen, const unsigned char *dark)
{
	int t, y;
	if (y1 < y0) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
	if (y2 < y0) { t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
	if (y2 < y1) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
	if (y2 == y0) return;
	for (y = y0 < 0 ? 0 : y0; y <= y2 && y < sh; y++) {
		long xa = x0 + (long)(x2 - x0) * (y - y0) / (y2 - y0), xb;
		int a, b, x;
		if (y < y1) xb = y1 == y0 ? x1 : x0 + (long)(x1 - x0) * (y - y0) / (y1 - y0);
		else xb = y2 == y1 ? x1 : x1 + (long)(x2 - x1) * (y - y1) / (y2 - y1);
		a = (int)(xa < xb ? xa : xb); b = (int)(xa < xb ? xb : xa);
		if (a < 0) a = 0;
		if (b >= sw) b = sw - 1;
		for (x = a; x <= b; x++) { unsigned char *q = dst + (long)y * pitch + x; *q = dark ? dark[*q] : (unsigned char)pen; }
	}
}

static long isq(long v) { long r = 0, b = 1L << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

static long g_capd[17], g_capl;                         /* the route's end cap: distance to the end per sample, its length */
static int octant(long dx, long dy)                     /* 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW (y down) */
{
	long ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	if (ax * 5 < ay * 2) return dy < 0 ? 0 : 4;
	if (ay * 5 < ax * 2) return dx > 0 ? 2 : 6;
	if (dx > 0) return dy < 0 ? 1 : 3;
	return dy > 0 ? 5 : 7;
}

static void draw_route(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int land, int k, int zoom)
{
	int px[42], py[42], known[42], n = 0, i, s, hw = 6 >> (zoom - 1), owner, loc, done16 = 0, slot;
	long arr;
	long tx[42], ty[42];
	const short *path;
	int np, pos;
	const unsigned char *dark = sprite_get_shadow_table();
	int pen_dark, pen_mid, pen_hi, pen_gd, pen_gm, pen_gh;
	if (land) { const LandUnit *u = &w->land[k]; path = u->path; np = u->npath; pos = u->pathpos; owner = u->owner; loc = u->location; arr = u->arrive_day; }
	else { const NavalUnit *u = &w->naval[k]; path = u->path; np = u->npath; pos = u->pathpos; owner = u->owner; loc = u->location; arr = u->arrive_day; }
	if (pos >= np || loc <= 0) return;
	slot = land ? k : MAX_LAND + k;
	if (g_leg_loc[slot] != loc || g_leg_arr[slot] != arr) { g_leg_loc[slot] = (short)loc; g_leg_arr[slot] = arr; g_leg_t0[slot] = g_today; }
	if (arr > g_leg_t0[slot]) {                                     /* 0x409b80: the travelled part of the leg, in 16ths */
		done16 = (int)((g_today - g_leg_t0[slot]) * 16 / (arr - g_leg_t0[slot]));
		if (done16 < 0) done16 = 0;
		if (done16 > 16) done16 = 16;
	}
	if (!unit_pos(w, mv, land, loc, &px[0], &py[0])) return;
	known[n++] = 1;
	for (i = pos; i < np && n < 41; i++) {
		int p = path[i];
		if (p <= 0 || p >= w->nprov || !unit_pos(w, mv, land, p, &px[n], &py[n])) continue;
		known[n++] = owner >= 0 && WORLD_KNOWN(w, owner, p);
	}
	if (n < 2) return;
	for (i = 0; i < n; i++) {                                      /* Catmull-Rom tangents */
		int a = i > 0 ? i - 1 : i, b = i < n - 1 ? i + 1 : i;
		tx[i] = (long)(px[b] - px[a]) * (i > 0 && i < n - 1 ? 1 : 2) / 2;
		ty[i] = (long)(py[b] - py[a]) * (i > 0 && i < n - 1 ? 1 : 2) / 2;
	}
	if (hw < 1) hw = 1;
	pen_dark = g_inv[(8 << 8) | (0 << 4) | 0]; pen_mid = g_inv[(13 << 8) | (0 << 4) | 0]; pen_hi = g_inv[(15 << 8) | (3 << 4) | 2];
	{	int gd = g_inv[(0 << 8) | (8 << 4) | 0], gm = g_inv[(0 << 8) | (13 << 4) | 0], gh = g_inv[(3 << 8) | (15 << 4) | 3];
		pen_gd = gd; pen_gm = gm; pen_gh = gh; }
	{	/* the end cap: how far each sample of the last segment is from the destination (Hermite samples as below) */
		int i2 = n - 2, j, lx = 0, ly = 0;
		long acc = 0, seg[17];
		for (j = 0; j <= 16; j++) {
			long t = j, t2 = t * t, t3 = t2 * t;
			long h00 = 2 * t3 - 3 * 16 * t2 + 4096, h10 = t3 - 2 * 16 * t2 + 256 * t, h01 = -2 * t3 + 3 * 16 * t2, h11 = t3 - 16 * t2;
			int x = (int)((h00 * px[i2] + h10 * tx[i2] + h01 * px[i2 + 1] + h11 * tx[i2 + 1]) / 4096);
			int y = (int)((h00 * py[i2] + h10 * ty[i2] + h01 * py[i2 + 1] + h11 * ty[i2 + 1]) / 4096);
			seg[j] = j ? isq((long)(x - lx) * (x - lx) + (long)(y - ly) * (y - ly)) : 0;
			lx = x; ly = y;
		}
		for (j = 16; j >= 0; j--) { g_capd[j] = acc; acc += seg[j]; }
		g_capl = 3L * (hw < 2 ? 2 : hw);                         /* the point is three half-widths long */
	}
	for (s = 0; s < 4; s++) {                                      /* 0 shadow, 1..3 the bands, widest first */
		int ox = s ? 0 : 4 >> (zoom - 1), oy = s ? 0 : 6 >> (zoom - 1), bw = s == 0 || s == 1 ? hw : s == 2 ? (hw * 2 + 2) / 3 : (hw + 2) / 3;
		int pen = s == 1 ? pen_dark : s == 2 ? pen_mid : pen_hi;
		if (!s && !dark) continue;
		for (i = 0; i + 1 < n; i++) {
			int j, lx = 0, ly = 0;
			if (!known[i] && !known[i + 1]) continue;
			for (j = 0; j <= 16; j++) {                            /* 16 samples per segment (Hermite, t = j/16) */
				long t = j, t2 = t * t, t3 = t2 * t;            /* scaled by 16, 256, 4096 */
				long h00 = 2 * t3 - 3 * 16 * t2 + 4096, h10 = t3 - 2 * 16 * t2 + 256 * t, h01 = -2 * t3 + 3 * 16 * t2, h11 = t3 - 16 * t2;
				int x = (int)((h00 * px[i] + h10 * tx[i] + h01 * px[i + 1] + h11 * tx[i + 1]) / 4096) + ox;
				int y = (int)((h00 * py[i] + h10 * ty[i] + h01 * py[i + 1] + h11 * ty[i + 1]) / 4096) + oy;
				if (j) {
					long dx = x - lx, dy = y - ly, l = isq(dx * dx + dy * dy);
					if (l > 0) {
						/* the end cap (0x409a49..0x409a92: two triangles from the last sample's edges): the last
						 * segment narrows over its final quarter to a point at the destination */
						int w0 = bw, w1 = bw, tip = i == n - 2 && known[i + 1];
						int nx, ny, mx0, my0;
						int pn = pen;
						if (tip && g_capd[j - 1] < g_capl) w0 = (int)(bw * g_capd[j - 1] / g_capl);
						if (tip && g_capd[j] < g_capl) w1 = (int)(bw * g_capd[j] / g_capl);
						nx = (int)(-dy * w1 / l); ny = (int)(dx * w1 / l);
						mx0 = (int)(-dy * w0 / l); my0 = (int)(dx * w0 / l);
						if (i == 0 && j <= done16) pn = s == 1 ? pen_gd : s == 2 ? pen_gm : pen_gh;   /* travelled: green */
						tri(dst, pitch, sw, sh, lx + mx0, ly + my0, lx - mx0, ly - my0, x + nx, y + ny, pn, s ? NULL : dark);
						tri(dst, pitch, sw, sh, lx - mx0, ly - my0, x - nx, y - ny, x + nx, y + ny, pn, s ? NULL : dark);
					}
				}
				lx = x; ly = y;
			}
		}
	}
	if (g_arrows.w)                                                 /* the arrows, on top */
		for (i = 0; i + 1 < n; i++) if (known[i]) {
			int f = octant(tx[i], ty[i]);
			sprite_draw(&g_arrows, f, dst, pitch, sw, sh, px[i] + g_arrows.ox - g_arrows.w / 2, py[i] + g_arrows.oy - g_arrows.h / 2);
		}
}

static const Sprite *get_sprite(const char *kind, const char *tag);
/* A-WALK l-1 in one of 8 directions (0x4109c0: N NE E SE S SW W NW), the nation's set, else the generic one; NULL none */
static const Sprite *walk_sprite(const char *kind, const char *tag, int dir, int sc)
{
	static const char *const dn[8] = { "n", "ne", "e", "se", "s", "sw", "w", "nw" };
	char name[96];
	const Sprite *s;
	int k;
	for (k = 0; k < 2; k++) {
		const char *t = k ? "min" : tag[0] ? tag : "min";
		snprintf(name, sizeof name, "gfx/map/units/t-%s c-%s a-walk l-1 d-%s.spr", kind, t, dn[dir & 7]);
		if ((s = spr_named_sc(name, sc)) != NULL) return s;
		snprintf(name, sizeof name, "gfx/map/units/t-%s c-%s a-walk d-%s l-1.spr", kind, t, dn[dir & 7]);
		if ((s = spr_named_sc(name, sc)) != NULL) return s;
	}
	return NULL;
}

static const Sprite *get_sprite(const char *kind, const char *tag) { return get_sprite_sc(kind, tag, 1000); }
static const Sprite *get_sprite_sc(const char *kind, const char *tag, int sc)
{
	char name[72];
	const Sprite *s;
	snprintf(name, sizeof name, "gfx/map/units/t-%s c-%s a-stand l-1.spr", kind, tag[0] ? tag : "min");
	if ((s = spr_named_sc(name, sc)) == NULL && strcmp(tag, "MIN") && strcmp(tag, "min")) s = get_sprite_sc(kind, "MIN", sc);   /* no set for this nation: generic */
	return s;
}

const Sprite *units_battle_sprite(const World *w, int c, int attacker, int naval)
{
	char name[96];
	const Sprite *s;
	const char *kind = naval ? "navy" : "army", *dir = attacker ? "ne" : "sw", *tag;
	int L, k;
	if (c < 0 || c >= w->ncountry) return NULL;
	tag = naval ? w->country[c].navy_spr : w->country[c].army_spr;
	L = 1 + (naval ? w->country[c].naval_tech : w->country[c].land_tech) / 15;
	if (L > 4) L = 4;
	for (k = 0; k < 2; k++) {                            /* the nation's set, then the generic one; the file names come in two orders */
		const char *t = k ? "min" : tag[0] ? tag : "min";
		snprintf(name, sizeof name, "gfx/map/units/t-%s c-%s a-fire l-%d d-%s.spr", kind, t, L, dir);
		if ((s = spr_named(name)) != NULL) return s;
		snprintf(name, sizeof name, "gfx/map/units/t-%s c-%s a-fire d-%s l-%d.spr", kind, t, dir, L);
		if ((s = spr_named(name)) != NULL) return s;
	}
	return get_sprite(kind, tag);                        /* standing, as on the map */
}

/* ---- the diplomatic map (eu2.exe 0x47fd20..0x47fee6, loader 0x4cc5e0): no priority, every relation that
 * holds adds its ornament over the shield, all centred on the capital's bounding-box centre (0x475790) */
static Sprite *g_dshield[MAX_COUNTRY];
static signed char g_dshield_tried[MAX_COUNTRY];
static Sprite g_orn[5];                 /* lions (vassal), fist (alliance), crown (marriage), cloud (casus belli), fire (war) */
static int g_orn_ok;
static void draw_centred(const Sprite *s, int fr, unsigned char *dst, int pitch, int sw, int sh, int x, int y, int z)
{
	if (s && s->w) sprite_draw_shift(s, fr, dst, pitch, sw, sh, x - ((s->w / 2 - s->ox) >> z), y - ((s->h / 2 - s->oy) >> z), z);
}
void units_draw_dipl(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int focus)
{
	static const char *const of[5] = { "vassal", "warpact", "dynastic_alliance", "casus_belli", "war" };
	int c, z = zsh(mv);
	if (!g_orn_ok) {
		int k;
		char p[64];
		g_orn_ok = 1;
		for (k = 0; k < 5; k++) {
			snprintf(p, sizeof p, "gfx/map/shields/shield_%s.bmp", of[k]);
			if (sprite_from_bmp(&g_orn[k], g_gfx, p, g_inv, k == 4 ? 11 : 1, 1000)) memset(&g_orn[k], 0, sizeof g_orn[k]);
		}
	}
	for (c = 0; c < w->ncountry && c < MAX_COUNTRY; c++) {
		const Country *co = &w->country[c];
		long fx, fy;
		int x, y;
		if (co->capital <= 0 || co->capital >= w->nprov || w->prov[co->capital].owner != c) continue;
		if (g_viewer >= 0 && !WORLD_KNOWN(w, g_viewer, co->capital)) continue;       /* terra incognita hides it */
		if (!mv_prov_centre(mv, co->capital, &fx, &fy) || !mv_to_screen(mv, fx, fy, &x, &y)) continue;
		if (x < -80 || y < -80 || x > sw + 80 || y > sh + 80) continue;
		if (!g_dshield_tried[c]) {
			char p[64];
			g_dshield_tried[c] = 1;
			snprintf(p, sizeof p, "gfx/map/shields/shield_%s.bmp", co->tag);
			{ char *q; for (q = p + 23; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32); }
			if ((g_dshield[c] = malloc(sizeof(Sprite))) != NULL && sprite_from_bmp(g_dshield[c], g_gfx, p, g_inv, 4, 1000)) { free(g_dshield[c]); g_dshield[c] = NULL; }
		}
		draw_centred(g_dshield[c], 0, dst, pitch, sw, sh, x, y, z);
		if (focus < 0 || focus == c) continue;
		if (world_overlord(w, c) == focus) draw_centred(&g_orn[0], 0, dst, pitch, sw, sh, x, y, z);
		if (world_allied(w, focus, c, ALLY_MILITARY)) draw_centred(&g_orn[1], 0, dst, pitch, sw, sh, x, y, z);
		if (world_allied(w, focus, c, ALLY_DYNASTIC)) draw_centred(&g_orn[2], 0, dst, pitch, sw, sh, x, y, z);
		if (w->dipflag[focus][c] & DF_CB) draw_centred(&g_orn[3], 0, dst, pitch, sw, sh, x, y, z);
		if (mil_at_war(w, focus, c)) draw_centred(&g_orn[4], (int)((g_ms / 100) % 11), dst, pitch, sw, sh, x, y, z);
	}
}

int units_draw(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int top)
{
	int i, n = 0, z = zsh(mv), usc = unit_scale(mv);
	memset(g_seen, 0, sizeof g_seen);
	for (i = 0; i < w->nland + w->nnaval; i++) {         /* the numbers: the stack owner's units, men / 1000 each (truncated), ships */
		int land = i < w->nland, loc, owner, k = land ? 0 : 1;
		long v;
		if (land) { const LandUnit *u = &w->land[i]; if (u->dead) continue; loc = u->location; owner = u->owner; v = (u->infantry + u->cavalry + u->artillery) / 1000; }
		else { const NavalUnit *u = &w->naval[i - w->nland]; loc = u->location; owner = u->owner; v = u->warships + u->galleys + u->transports; }   /* fleet +0x8c: ships, NIEPEWNE */
		if (loc <= 0 || loc >= w->nprov || owner < 0) continue;
		if (!(g_seen[loc] & (land ? 1 : 2))) { g_seen[loc] |= land ? 1 : 2; g_own[loc][k] = (short)owner; g_cnt[loc][k] = 0; g_walk[loc][k] = 0; }
		if (g_own[loc][k] == owner) {
			g_cnt[loc][k] += v;
			if (!g_walk[loc][k]) {                             /* 0x48e9b0: a non-empty path -> A-WALK towards its next province */
				const short *pa = land ? w->land[i].path : w->naval[i - w->nland].path;
				int pp = land ? w->land[i].pathpos : w->naval[i - w->nland].pathpos, np = land ? w->land[i].npath : w->naval[i - w->nland].npath;
				if (pp < np && pa[pp] > 0 && pa[pp] < w->nprov) g_walk[loc][k] = pa[pp];
			}
		}
	}
	memset(g_seen, 0, sizeof g_seen);
	for (i = 0; i < w->nland + w->nnaval; i++) {
		int land = i < w->nland, loc, owner, x, y, k = land ? 0 : 1, fr;
		const Province *p;
		const Sprite *s;
		if (land) { if (w->land[i].dead) continue; loc = w->land[i].location; owner = w->land[i].owner; }
		else { loc = w->naval[i - w->nland].location; owner = w->naval[i - w->nland].owner; }
		if (loc <= 0 || loc >= w->nprov || owner < 0 || (g_seen[loc] & (land ? 1 : 2)) || HIDDEN(w, loc)) continue;
		if (g_fogvis && owner != g_viewer && !g_fogvis[loc]) continue;   /* fog of war: other countries' units only where we see (SPEC_UNIT_UI 6) */
		g_seen[loc] |= land ? 1 : 2;
		p = &w->prov[loc];
		if (!mv_to_screen(mv, land ? p->army_x : p->port_x ? p->port_x : p->army_x,
		                      land ? p->army_y : p->port_y ? p->port_y : p->army_y, &x, &y)) continue;
		if (x < -64 || y < -64 || x > sw + 64 || y > sh + 64) continue;
		s = NULL;
		if (g_walk[loc][k]) {                                  /* walking in place, facing the next province (no interpolation, SPEC_UNIT_UI 6) */
			const Province *q = &w->prov[g_walk[loc][k]];
			long dx = (long)(land ? q->army_x : q->port_x ? q->port_x : q->army_x) - (land ? p->army_x : p->port_x ? p->port_x : p->army_x);
			long dy = (long)(land ? q->army_y : q->port_y ? q->port_y : q->army_y) - (land ? p->army_y : p->port_y ? p->port_y : p->army_y);
			if (dx > 15000) dx -= 18944; else if (dx < -15000) dx += 18944;
			s = walk_sprite(land ? "army" : "navy", land ? w->country[owner].army_spr : w->country[owner].navy_spr, octant(dx, dy), usc);
		}
		if (!s) s = get_sprite_sc(land ? "army" : "navy", land ? w->country[owner].army_spr : w->country[owner].navy_spr, usc);
		if (!s) continue;
		fr = g_anim && s->frames > 1 ? (int)((g_ms / 100) % (unsigned long)s->frames) : 0;   /* Speed = 5 (.spr): 100 ms a frame, NIEPEWNE */
		sprite_draw_shift(s, fr, dst + (long)top * pitch, pitch, sw, sh - top, x, y - top, 0);   /* already at the zoom's size */
		{	/* a selected unit in this stack: statusbarselect under it (the strength bar's place, y + 8) */
			int j, sel = 0;
			if (land) { for (j = 0; j < g_nsl && !sel; j++) if (g_sl[j] >= 0 && g_sl[j] < w->nland && w->land[g_sl[j]].location == loc && !w->land[g_sl[j]].dead) sel = 1; }
			else for (j = 0; j < g_nsf && !sel; j++) if (g_sf[j] >= 0 && g_sf[j] < w->nnaval && w->naval[g_sf[j]].location == loc) sel = 1;
			if (sel) {
				const Sprite *rg;
				rings_load();
				rg = &g_ring[land ? 0 : 1];                     /* the animated ring under the unit (user: the original shows it) */
				if (rg->w) sprite_draw_shift(rg, sel_frame(80, 13), dst + (long)top * pitch, pitch, sw, sh - top, x - ((rg->w / 2 - rg->ox) >> z), y - top - ((rg->h / 2 - rg->oy) >> z), z);
				sprite_draw_shift(s, fr, dst + (long)top * pitch, pitch, sw, sh - top, x, y - top, 0);   /* the unit over its ring */
				if (g_sbsel.w) sprite_draw_shift(&g_sbsel, sel_frame(120, 9), dst + (long)top * pitch, pitch, sw, sh - top, x - ((g_sbsel.w / 2 - g_sbsel.ox) >> z), y - top + (((land ? 8 : 10) + g_sbsel.oy) >> z), z);
			}
		}
		if (!z && y - 6 >= top) ui_unit_count(dst, pitch, sw, sh, x + 6, y - 6, g_cnt[loc][land ? 0 : 1]);   /* 0x49034e: x + 6, y - 6 */
		n++;
	}
	{	/* the selected units' routes, then the order animation at the click point */
		int j;
		long cx, cy;
		int z;
		mv_get(mv, &cx, &cy, &z);
		rings_load();
		for (j = 0; j < g_nsl; j++) if (g_sl[j] >= 0 && g_sl[j] < w->nland && !w->land[g_sl[j]].dead) draw_route(w, mv, dst, pitch, sw, sh, 1, g_sl[j], z);
		for (j = 0; j < g_nsf; j++) if (g_sf[j] >= 0 && g_sf[j] < w->nnaval) draw_route(w, mv, dst, pitch, sw, sh, 0, g_sf[j], z);
		if (units_anim_active()) {
			const Sprite *a = g_anim_kind == 1 ? &g_dest : &g_illegal;
			int f = (int)((g_ms - g_anim_t0) / 50);
			if (a->w && f < a->frames) sprite_draw(a, f, dst, pitch, sw, sh, g_anim_x - a->w / 2 + a->ox, g_anim_y - a->h / 2 + a->oy);
		}
	}
	return n;
}
