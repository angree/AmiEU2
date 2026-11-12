/* mapmode.c - see mapmode.h. The rules of eu2.exe's map-mode setter 0x478950 (docs/SPEC_MAPMODES.md);
 * the colour scales are the positions in Map/colorscales.csv (the file's order). */
#include <string.h>
#include "mapmode.h"
#include "religion.h"
#include "script.h"

enum { CS_BLACK = 0, CS_GREEN = 1, CS_ORANGE = 2, CS_LBLUE = 3, CS_DGREY = 8, CS_YELLOW = 7, CS_DGREEN = 11, CS_BLUE = 12,
       CS_WHITE = 14, CS_RED = 16, CS_LGREEN = 17, CS_BROWN = 19, CS_DRED = 21, CS_DORANGE = 22, CS_WATER = 23 };

static int land(const Province *p, int id) { return id > 0 && p->terrain != 5 && p->terrain != 6; }

static int in_ally(const World *w, int type, int a, int b)
{
	int i, k, ha, hb;
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		if (al->type != type) continue;
		for (ha = hb = 0, k = 0; k < al->n; k++) { if (al->member[k] == a) ha = 1; if (al->member[k] == b) hb = 1; }
		if (ha && hb) return 1;
	}
	return 0;
}

static int war_side(const World *w, int a, int b)       /* both on the same side of a running war */
{
	int i, k, sa, sb;
	for (i = 0; i < w->nwar; i++) {
		const War *x = &w->war[i];
		for (sa = sb = 0, k = 0; k < x->natt; k++) { if (x->att[k] == a) sa = 1; if (x->att[k] == b) sb = 1; }
		if (sa && sb) return 1;
		for (sa = sb = 0, k = 0; k < x->ndef; k++) { if (x->def[k] == a) sa = 1; if (x->def[k] == b) sb = 1; }
		if (sa && sb) return 1;
	}
	return 0;
}

/* terra incognita: what the player has not discovered (the black scale, like province 0) */
static void fog(const MapView *v, const MapMode *m, unsigned char *s, unsigned char *sh, int n)
{
	int i;
	if (m->player < 0) return;                          /* terra incognita: always (the fog option does not reveal the world) */
	for (i = 1; i < n; i++) if (!WORLD_KNOWN(m->w, m->player, i)) {
		if (m->w->prov[i].terrain == 6) {               /* a river: the scenarios never list them, so it shows with a known
			                                               neighbour (ours, NIEPEWNE: the exe's rule was not read) */
			const unsigned short *nb;
			int k, c = mv_adj(v, i, &nb);
			for (k = 0; k < c && !(nb[k] < n && m->w->prov[nb[k]].terrain != 6 && WORLD_KNOWN(m->w, m->player, nb[k])); k++) ;
			if (k < c) continue;
		}
		s[i] = CS_BLACK; sh[i] = 255;                   /* land and sea alike */
	}
}

static void colour(void *user, const MapView *v, unsigned char *s, unsigned char *sh, int n)
{
	MapMode *m = (MapMode *)user;
	const World *w = m->w;
	int i, f = m->focus >= 0 ? m->focus : m->player;
	for (i = 1; i < n; i++) {
		const Province *p = &w->prov[i];
		int o = p->owner;
		if (!land(p, i)) { s[i] = CS_WATER; continue; }
		switch (m->mode) {
		case MM_POLITICAL:                                    /* mode 0: the owner's country.csv colour (mapview), unowned Orange */
			if (o < 0) s[i] = CS_ORANGE;
			break;
		case MM_NORMAL:                                       /* mode 1: terrain (winter white: NIEPEWNE, not kept) */
			{	static const unsigned char t[9] = { CS_ORANGE, CS_GREEN, CS_DORANGE, CS_YELLOW, CS_LGREEN, CS_WATER, CS_WATER, CS_ORANGE, CS_BLACK };
				s[i] = t[p->terrain < 9 ? p->terrain : 0];
				if (m->vis && !m->vis[i]) sh[i] |= MV_SHADE_HALF;   /* what the player cannot see: half brightness (exe scale bit 0x20) */
			}
			break;
		case MM_GOODS:                                        /* mode 2 */
			s[i] = m->goods >= 0 && p->goods == m->goods ? CS_LGREEN : CS_ORANGE;
			break;
		case MM_ECONOMIC: {                                   /* mode 3: light green, darker the higher the tax value */
			long t = p->income_m / 1000;
			if (o >= 0 && p->population > 0 && p->population < 1000) t = 0;   /* a colony */
			if (t < 0) t = 0;
			if (t > 50) t = 50;
			s[i] = CS_LGREEN; sh[i] = (unsigned char)((50 - t) * 31 / 50);
			break; }
		case MM_TRADE: {                                      /* mode 4: one scale per centre of trade */
			int k = m->prov_cot ? m->prov_cot[i] : -1, c;
			if (k < 0 || k >= w->ncot) { s[i] = CS_ORANGE; break; }
			c = w->cot[k].prov % 22 + 1;                      /* the centre's id (NIEPEWNE: its province) */
			s[i] = (unsigned char)(c == 2 ? 16 : c);
			break; }
		case MM_DIPLOMATIC:                                   /* mode 5: ties (View Alliances) */
			s[i] = CS_ORANGE; sh[i] = 0;
			if (o < 0 || f < 0) break;
			if (o == f) { s[i] = CS_GREEN; sh[i] = 0; }
			else if (m->at_war && m->at_war(w, f, o)) { s[i] = CS_RED; sh[i] = 0; }
			else if (war_side(w, f, o)) { s[i] = CS_GREEN; sh[i] = 6; }
			else if (in_ally(w, ALLY_VASSAL, f, o)) { s[i] = CS_GREEN; sh[i] = 12; }
			else if (w->dipflag[o][f] & DF_CB_PERM) { s[i] = CS_RED; sh[i] = 12; }
			else if (in_ally(w, ALLY_MILITARY, f, o)) { s[i] = CS_GREEN; sh[i] = 18; }
			else if (w->dipflag[f][o] & DF_CB_PERM) { s[i] = CS_RED; sh[i] = 18; }
			else if (in_ally(w, ALLY_DYNASTIC, f, o)) { s[i] = CS_GREEN; sh[i] = 24; }
			break;
		case MM_RELATIONS: {                                  /* mode 6: the relation value */
			long r;
			if (o < 0) { s[i] = CS_BLACK; break; }
			if (o == f) { s[i] = CS_GREEN; sh[i] = 0; break; }   /* the focus country itself green, as in mode 5 (user 2026-11-12: it was white) */
			r = f >= 0 && o != f ? world_rel(w, f, o) / 1000 : 0;
			s[i] = (unsigned char)(r > 149 ? CS_DGREEN : r >= 76 ? CS_LGREEN : r >= -74 ? CS_ORANGE : r >= -149 ? CS_RED : CS_DRED);
			break; }
		case MM_RELIGIOUS: {                                  /* mode 7: the owner's state religion, darker with revolt risk */
			static const unsigned char rc[11] = { CS_LBLUE, CS_BLUE, CS_WHITE, CS_DGREY, CS_BROWN, CS_GREEN, CS_LGREEN, CS_ORANGE, CS_RED, CS_YELLOW, CS_DORANGE };
			int r;
			if (o < 0) { s[i] = CS_ORANGE; break; }
			r = rel_index(w, w->country[o].religion);
			s[i] = r >= 0 && r < 11 ? rc[r] : CS_ORANGE;
			if (m->risk) { int rr = m->risk(m->user, i); rr = 2 * rr > 16 ? 16 : 2 * rr; sh[i] = (unsigned char)(16 - rr); }
			break; }
		case MM_COLONIAL:                                     /* mode 10: where a colonist or a trader may go */
			s[i] = CS_ORANGE;
			if (m->envoy_ok && m->envoy_ok(m->user, i)) {
				s[i] = CS_LGREEN;
				sh[i] = (unsigned char)(o < 0 ? 31 : p->tp && p->population <= 0 ? 15 : 0);
			}
			break;
		default: break;
		}
	}
	fog(v, m, s, sh, n);
}

void mapmode_set(MapView *v, MapMode *m, int mode)
{
	m->mode = mode >= 0 && mode < MM_N ? mode : MM_NORMAL;
	mv_set_borders(v, m->mode == MM_NORMAL || m->mode == MM_POLITICAL || m->mode == MM_DIPLOMATIC || m->mode == MM_RELATIONS || m->mode == MM_RELIGIOUS);
	mv_set_color_fn(v, colour, m);
}

const char *mapmode_name(int mode)
{
	static const char *n[MM_N] = { "normal", "political", "economic", "religious", "diplomatic", "trade", "colonisation", "relations", "goods" };
	return mode >= 0 && mode < MM_N ? n[mode] : "?";
}
