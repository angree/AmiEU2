/* peace.c - see peace.h. Section numbers refer to docs/SPEC_WAR_DIPLOMACY.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "peace.h"
#include "calendar.h"
#include "military.h"

#define MAXTRUCE 2048
#define TRUCE_DAYS (5L * 360)          /* §5.4: 5 years */
#define TRUCE_KEEP (37L * 360)         /* a record also gives the day of the last peace (country+0x8ee, 0x442abf): kept 10 + 3 x 9 years */
#define AUTO_PEACE_DAYS (36L * 30)     /* §5.4: 36 months without hostile action */

typedef struct { short a, b; long until; } Truce;
#define MAXREC 512
typedef struct { short u, t; long last; short bestv, bests; } AiRec;   /* the AI record of u about t (R+0x18, R+0x00, R+0x04) */

struct Peace {
	World *w;
	Truce truce[MAXTRUCE]; int ntruce;
	unsigned long seed;
	PeaceLogFn log; void *loguser;
	PeaceOfferFn offer; void *offer_user;
	long next_offer;
	long today;
	unsigned char side[MAX_COUNTRY];   /* scratch: 1 / 2 per country for the war being looked at */
	Military *m;                       /* adjacency (may be NULL) */
	const unsigned char *fero;         /* .ai ferocity per country (diplo.c) */
	int (*mode)(void *, int); void *mode_user;     /* the AI's strategic mode */
	int (*nbfn)(void *, int, int); void *nb_user;  /* neighbours */
	long (*strfn)(void *, int, int); void *str_user;   /* the AI's strength index FS (naval 0) / FN (1), x1000 (ai.c 0x43c010) */
	int (*overfn)(void *, int);                    /* over the badboy limit */
	AiRec rec[MAXREC]; int nrec;
	long stats_day;
	short wsc[MAX_WAR]; unsigned char wsc_ok[MAX_WAR]; long wsc_day;
	struct { short war, u, t, v; } ps[64]; int nps;   /* pair scores of the day */   /* war scores cached for the AI (cleared when the stats are rebuilt or a war ends) */
	long men[MAX_COUNTRY]; short ships[MAX_COUNTRY], ctl[MAX_COUNTRY], cities[MAX_COUNTRY], rebels[MAX_COUNTRY];
	unsigned char coast[MAX_COUNTRY];
};

void peace_set_military(Peace *p, Military *m) { p->m = m; }
void peace_set_strength(Peace *p, long (*fn)(void *, int, int), void *user) { if (p) { p->strfn = fn; p->str_user = user; } }
void peace_set_ai(Peace *p, const unsigned char *fero, int (*mode)(void *, int), int (*nb)(void *, int, int), int (*over)(void *, int), void *user)
{ p->fero = fero; p->mode = mode; p->mode_user = user; p->nbfn = nb; p->nb_user = user; p->overfn = over; }

static int prand(Peace *p) { p->seed = (p->seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((p->seed >> 16) & 0x7FFF); }

static void logp(Peace *p, const char *fmt, ...)
{
	char b[220];
	va_list a;
	if (!p->log) return;
	va_start(a, fmt);
	vsnprintf(b, sizeof b, fmt, a);
	va_end(a);
	p->log(p->loguser, p->today, b);
}

void peace_set_log(Peace *p, PeaceLogFn fn, void *user) { p->log = fn; p->loguser = user; }

Peace *peace_new(World *w, long today, unsigned long seed)
{
	Peace *p = (Peace *)calloc(1, sizeof *p);
	int i;
	if (!p) return NULL;
	p->w = w; p->seed = seed ? seed : 1; p->today = today;
	for (i = 0; i < w->nwar; i++) {                 /* scenario wars: started "now", no combat yet */
		if (!w->war[i].start_day) w->war[i].start_day = w->war[i].began.year > 0 ? cal_day(w->war[i].began.year, w->war[i].began.month, w->war[i].began.day) : today;
		if (!w->war[i].last_combat) w->war[i].last_combat = today;
	}
	return p;
}

void peace_free(Peace *p) { free(p); }

int peace_side(const World *w, int war, int c)
{
	const War *wr = &w->war[war];
	int i;
	for (i = 0; i < wr->natt; i++) if (wr->att[i] == c) return 1;
	for (i = 0; i < wr->ndef; i++) if (wr->def[i] == c) return 2;
	return 0;
}

static void fill_sides(Peace *p, int war)
{
	const War *wr = &p->w->war[war];
	int i;
	memset(p->side, 0, sizeof p->side);
	for (i = 0; i < wr->natt; i++) p->side[wr->att[i]] = 1;
	for (i = 0; i < wr->ndef; i++) p->side[wr->def[i]] = 2;
}

/* a and b on one side of some war (the dip record's +0x3d) */
static int peace_cobel(const Peace *p, int a, int b)
{
	const World *w = p->w;
	int i, j, k;
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		int sa = 0, sb = 0;
		for (j = 0; j < wr->natt; j++) { if (wr->att[j] == a) sa = 1; if (wr->att[j] == b) sb = 1; }
		if (sa && sb) return 1;
		sa = sb = 0;
		for (k = 0; k < wr->ndef; k++) { if (wr->def[k] == a) sa = 1; if (wr->def[k] == b) sb = 1; }
		if (sa && sb) return 1;
	}
	return 0;
}

/* §5.3 0x47ca60: trunc(base tax / 2 + 2 + 0.25 V) + 10 for a CoT home + trunc(fortress) / 2, V = the province's manpower
 * value 0x47cc80 (world_prov_manpower_m); city+0x3a read as the fortress level (NIEPEWNE). */
int peace_prov_value(const World *w, int prov)
{
	const Province *p = &w->prov[prov];
	int v, k;
	if (p->population <= 0) return 1;                /* no city */
	if (p->population < 1000) return 2;              /* colony */
	v = (int)((p->income_m / 2 + 2000L + world_prov_manpower_m(w, prov) / 4) / 1000) + p->fort / 2;
	for (k = 0; k < w->ncot; k++) if (w->cot[k].prov == prov) { v += 10; break; }
	return v;
}

static int score_sides(const World *w, int war, const unsigned char *side)
{
	const War *wr = &w->war[war];
	long bt = 0, occ = 0;
	int i, j, all_def = 1, all_att = 1, lead_att = wr->natt ? wr->att[0] : -1, lead_def = wr->ndef ? wr->def[0] : -1;
	for (i = 0; i < wr->natt; i++)
		for (j = 0; j < wr->ndef; j++) bt += (long)wr->bp_att[i][j] - (long)wr->bp_def[j][i];
	if (bt > 95) bt = 95;
	if (bt < -95) bt = -95;
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		int so, sc;
		if (p->owner < 0) continue;
		so = side[p->owner]; sc = p->controller >= 0 ? side[p->controller] : 0;
		if (so == 1 && sc == 2) occ -= peace_prov_value(w, i);
		if (so == 2 && sc == 1) occ += peace_prov_value(w, i);
		if (p->owner == lead_def && sc != 1) all_def = 0;
		if (p->owner == lead_att && sc != 2) all_att = 0;
	}
	if (lead_def >= 0 && w->country[lead_def].nprov_owned > 0 && all_def) return 100;
	if (lead_att >= 0 && w->country[lead_att].nprov_owned > 0 && all_att) return -100;
	bt += occ;
	return (int)(bt > 99 ? 99 : bt < -99 ? -99 : bt);
}

int peace_war_score(const World *w, int war)
{
	static unsigned char side[MAX_COUNTRY];
	const War *wr = &w->war[war];
	int i;
	memset(side, 0, sizeof side);
	for (i = 0; i < wr->natt; i++) side[wr->att[i]] = 1;
	for (i = 0; i < wr->ndef; i++) side[wr->def[i]] = 2;
	return score_sides(w, war, side);
}

int peace_truce(const Peace *p, int a, int b, long today)
{
	int i;
	for (i = 0; i < p->ntruce; i++)
		if (p->truce[i].until > today && ((p->truce[i].a == a && p->truce[i].b == b) || (p->truce[i].a == b && p->truce[i].b == a))) return 1;
	return 0;
}

long peace_last(const Peace *p, int a, int b)
{
	int i;
	long d = 0;
	if (!p) return 0;
	for (i = 0; i < p->ntruce; i++)
		if (((p->truce[i].a == a && p->truce[i].b == b) || (p->truce[i].a == b && p->truce[i].b == a)) && p->truce[i].until - TRUCE_DAYS > d) d = p->truce[i].until - TRUCE_DAYS;
	return d;
}

void peace_truces_of(const Peace *p, int c, long today, unsigned char *flags)
{
	int i;
	for (i = 0; i < p->ntruce; i++) if (p->truce[i].until > today) {
		if (p->truce[i].a == c) flags[p->truce[i].b] = 1;
		else if (p->truce[i].b == c) flags[p->truce[i].a] = 1;
	}
}

static void add_truce(Peace *p, int a, int b, long until)
{
	int i, slot = -1;
	for (i = 0; i < p->ntruce && slot < 0; i++) if ((p->truce[i].a == a && p->truce[i].b == b) || (p->truce[i].a == b && p->truce[i].b == a)) slot = i;   /* the pair's newer peace */
	for (i = 0; i < p->ntruce && slot < 0; i++) if (p->truce[i].until - TRUCE_DAYS + TRUCE_KEEP <= p->today) slot = i;
	if (slot < 0) { if (p->ntruce >= MAXTRUCE) return; slot = p->ntruce++; }
	p->truce[slot].a = (short)a; p->truce[slot].b = (short)b; p->truce[slot].until = until;
}

static int is_core(const Province *p, int c)
{
	int k;
	for (k = 0; k < 8; k++) if (p->cores[k] == c) return 1;
	return 0;
}

static void drop_records(Peace *p, int war, int only)          /* the AI records of pairs in that war (only >= 0: that member's) */
{
	const World *w = p->w;
	int i, n = 0;
	for (i = 0; i < p->nrec; i++) {
		int su = peace_side(w, war, p->rec[i].u), st = peace_side(w, war, p->rec[i].t);
		if (su && st && su != st && (only < 0 || p->rec[i].u == only || p->rec[i].t == only)) continue;
		p->rec[n++] = p->rec[i];
	}
	p->nrec = n;
}

static void remove_war(World *w, int war)
{
	memmove(&w->war[war], &w->war[war + 1], (size_t)(w->nwar - war - 1) * sizeof w->war[0]);
	w->nwar--;
}

void peace_make(Peace *p, int war, int winner, const short *provs, int nprovs, long gold_m, long today)
{
	World *w = p->w;
	War *wr = &w->war[war];
	int i, j, lead_w, lead_l;
	p->today = today;
	fill_sides(p, war);
	lead_w = winner == 1 ? wr->att[0] : winner == 2 ? wr->def[0] : -1;
	lead_l = winner == 1 ? wr->def[0] : winner == 2 ? wr->att[0] : -1;
	world_news(w, NOTE_OTHER, wr->def[0], OM_PEACE, wr->att[0]);   /* PEACEACCEPTOTHER (0x44d5b0; terms not listed: NIEPEWNE) */
	for (i = 0; i < wr->natt; i++) world_sfx(w, wr->att[i], "diplomat_peace");   /* EXEC_ACCEPT_PEACE / WHITE_PEACE with the player (SPEC_SOUND 0x37) */
	for (j = 0; j < wr->ndef; j++) world_sfx(w, wr->def[j], "diplomat_peace");
	if (!winner) {
		logp(p, "white peace: %s and %s", w->country[wr->att[0]].tag, w->country[wr->def[0]].tag);
		for (i = 0; i < wr->natt; i++) world_note(w, NOTE_WHITEPEACE, wr->att[i], -1, wr->def[0]);   /* WHITEPEACEUS for a human member */
		for (j = 0; j < wr->ndef; j++) world_note(w, NOTE_WHITEPEACE, wr->def[j], -1, wr->att[0]);
	}
	else logp(p, "peace: %s wins against %s (score %d), %d provinces, %ld ducats",
	          w->country[lead_w].tag, w->country[lead_l].tag, peace_war_score(w, war), nprovs, gold_m / 1000);
	if (winner) {	/* 0x44e6e1..0x44e866: v = |offer+0x24| x 0.05 (ours: the war score, NIEPEWNE); every member of the losing side
		 * -2v VP, of the winning side +2v (category 1) */
		int sc = peace_war_score(w, war);
		long v2 = (sc < 0 ? -sc : sc) * 100L;                 /* 2 x 0.05 x |score|, x1000 */
		for (i = 0; i < wr->natt; i++) world_vp(w, wr->att[i], VP_PEACE, winner == 1 ? v2 : -v2);
		for (j = 0; j < wr->ndef; j++) world_vp(w, wr->def[j], VP_PEACE, winner == 2 ? v2 : -v2);
	}
	for (i = 0; winner && i < nprovs; i++) {
		Province *pv = &w->prov[provs[i]];
		int old = pv->owner, nw = pv->controller >= 0 && p->side[pv->controller] == winner ? pv->controller : lead_w;
		if (old < 0 || old == nw) continue;
		if (!is_core(pv, nw)) {                                    /* 0x44e319..0x44e3f5: badboy for a non-core gain */
			int cap = w->country[nw].capital, ka, att = 0;
			int eur = cap > 0 && cap < w->nprov && script_ieq(world_name(w, w->prov[cap].continent) ? world_name(w, w->prov[cap].continent) : "", "europe");
			if (!(eur && world_religion_index(w, w->country[old].religion) == 7)) {   /* none for a European taking from pagans */
				long amt = w->country[old].human ? 333 : 1000;         /* a third from a human */
				for (ka = 0; ka < wr->natt; ka++) if (wr->att[ka] == nw) att = 1;
				w->country[nw].badboy_m += amt;
				if (att) w->country[nw].badboy_m += amt;              /* twice for the war's attacking side (+0x1e) */
			}
		}
		if (!is_core(pv, old)) { w->country[old].badboy_m -= 1000; if (w->country[old].badboy_m < 0) w->country[old].badboy_m = 0; }   /* 0x44e436: giving up a non-core */
		world_vp(w, old, VP_PEACE, -pv->income_m); world_vp(w, nw, VP_PEACE, pv->income_m);   /* 0x44e087: the province's income value moves */
		pv->owner = pv->controller = (short)nw;
		w->country[old].nprov_owned--; w->country[nw].nprov_owned++;
		if (w->country[old].capital == provs[i]) w->country[old].capital = -1;
		w->owner_epoch++;
		logp(p, "  %s cedes %s to %s (value %d)", w->country[old].tag, pv->name, w->country[nw].tag, peace_prov_value(w, provs[i]));
		if (w->country[old].nprov_owned <= 0) {
			logp(p, "  %s is annexed", w->country[old].tag);
			for (j = 0; j < w->nland; j++) if (w->land[j].owner == old) w->land[j].dead = 1;
		}
	}
	if (winner && gold_m > 0) { w->country[lead_l].treasury_m -= gold_m; w->country[lead_w].treasury_m += gold_m; world_book_inc(w, lead_w, INC_PEACE, gold_m); }
	if (winner) {                                    /* VP (0x44e6e1): giver side -0.1 |score|, gaining side +0.1 |score|; DoF +15 */
		int sc = peace_war_score(w, war), k2;
		long v = 100L * (sc < 0 ? -sc : sc);
		for (k2 = 0; k2 < wr->natt; k2++) { int c2 = wr->att[k2]; world_vp(w, c2, VP_PEACE, winner == 1 ? v : -v); if (winner == 1 && w->country[c2].defender) world_vp(w, c2, VP_PEACE, 15000); }
		for (k2 = 0; k2 < wr->ndef; k2++) { int c2 = wr->def[k2]; world_vp(w, c2, VP_PEACE, winner == 2 ? v : -v); if (winner == 2 && w->country[c2].defender) world_vp(w, c2, VP_PEACE, 15000); }
	}
	for (i = 1; i < w->nprov; i++) {                 /* occupations end */
		Province *pv = &w->prov[i];
		if (pv->owner >= 0 && pv->controller >= 0 && pv->controller != pv->owner &&
		    p->side[pv->owner] && p->side[pv->controller] && p->side[pv->owner] != p->side[pv->controller]) pv->controller = pv->owner;
	}
	for (i = 0; i < wr->natt; i++)
		for (j = 0; j < wr->ndef; j++) add_truce(p, wr->att[i], wr->def[j], today + TRUCE_DAYS);
	drop_records(p, war, -1);
	remove_war(w, war);
	memset(p->wsc_ok, 0, sizeof p->wsc_ok); p->nps = 0;
}

/* AI demands (ours, NIEPEWNE: the exe's AI is NIEZNANE): every loser province the winners control
 * or the winning leader has a core in, never a capital - except a one-province country, which is
 * annexed when occupied (§5.4) - most valuable first while the total fits the score; the rest of
 * the score in cash at 25 ducats per percent, at most half the loser's treasury. */
int peace_terms(Peace *p, int war, int winner, int score, short *take, int max, long *gold_m)
{
	World *w = p->w;
	War *wr = &w->war[war];
	static short cand[MAX_PROV];
	static int val[MAX_PROV];
	int i, n = 0, nt = 0, used = 0, lead_w = winner == 1 ? wr->att[0] : wr->def[0], lead_l = winner == 1 ? wr->def[0] : wr->att[0];
	long gold;
	fill_sides(p, war);
	for (i = 1; i < w->nprov; i++) {
		const Province *pv = &w->prov[i];
		int lo;
		if (pv->owner < 0 || p->side[pv->owner] != 3 - winner) continue;
		lo = pv->owner;
		if (w->country[lo].capital == i && w->country[lo].nprov_owned > 1) continue;
		if ((pv->controller >= 0 && p->side[pv->controller] == winner) || is_core(pv, lead_w)) { cand[n] = (short)i; val[n++] = peace_prov_value(w, i); }
	}
	while (nt < max) {
		int b = -1;
		for (i = 0; i < n; i++) if (val[i] > 0 && used + val[i] <= score && (b < 0 || val[i] > val[b])) b = i;
		if (b < 0) break;
		take[nt++] = cand[b]; used += val[b]; val[b] = 0;
	}
	gold = (long)(score - used) * 25 * 1000;
	if (gold > w->country[lead_l].treasury_m / 2) gold = w->country[lead_l].treasury_m / 2;
	if (gold < 0) gold = 0;
	*gold_m = gold;
	return nt;
}

void peace_month(Peace *p, long today)
{
	World *w = p->w;
	int i, j;
	p->today = today;
	for (i = w->nwar - 1; i >= 0; i--) {
		War *wr = &w->war[i];
		int occupied = 0;
		for (j = wr->natt - 1; j >= 0; j--)              /* members without land leave the war */
			if (w->country[wr->att[j]].nprov_owned <= 0) { memmove(&wr->att[j], &wr->att[j + 1], (size_t)(wr->natt - j - 1) * sizeof wr->att[0]); wr->natt--; }
		for (j = wr->ndef - 1; j >= 0; j--)
			if (w->country[wr->def[j]].nprov_owned <= 0) { memmove(&wr->def[j], &wr->def[j + 1], (size_t)(wr->ndef - j - 1) * sizeof wr->def[0]); wr->ndef--; }
		if (!wr->natt || !wr->ndef) { logp(p, "war %d ends: one side has no members left", i); drop_records(p, i, -1); remove_war(w, i); continue; }
		/* NOTE: removing members shifts the battle-point rows; v1 accepts that (members rarely die) */
		fill_sides(p, i);
		for (j = 1; j < w->nprov && !occupied; j++) {
			const Province *pv = &w->prov[j];
			if (pv->owner >= 0 && pv->controller >= 0 && p->side[pv->owner] && p->side[pv->controller] && p->side[pv->owner] != p->side[pv->controller]) occupied = 1;
		}
		if (!occupied && !w->tutorial && today - wr->last_combat >= AUTO_PEACE_DAYS) { peace_make(p, i, 0, NULL, 0, 0, today); continue; }   /* (not in a tutorial: 0x5b23e7) */
	}
	p->stats_day = 0;                                   /* the AI's per-month counts are rebuilt on first use */
}

void peace_set_offer(Peace *p, PeaceOfferFn fn, void *user) { p->offer = fn; p->offer_user = user; }

int peace_find_war(const World *w, int a, int b)
{
	int i;
	for (i = 0; i < w->nwar; i++) {
		int sa = peace_side(w, i, a), sb = peace_side(w, i, b);
		if (sa && sb && sa != sb) return i;
	}
	return -1;
}

/* The player (leader of its side) proposes peace. AI answers are ours (NIEPEWNE, §3.3/§5.4 give no
 * formula): white peace when the AI is not ahead by more than 10; demands when the player is ahead
 * by 10 or more (terms = peace_terms for the player's score); surrender always (AI's terms). */
static int player_to_ai(Peace *p, int war, int player, int them, const PeaceOffer *po, int sep);
int peace_player_offer(Peace *p, int war, int player, int kind, long today)
{
	World *w = p->w;
	War *wr;
	int s, score, sp, nt;
	static PeaceOffer po;
	p->today = today;
	if (war < 0 || war >= w->nwar) return PEACE_NOWAR;
	wr = &w->war[war];
	s = peace_side(w, war, player);
	if (!s || (s == 1 ? wr->att[0] : wr->def[0]) != player) return PEACE_NOTLEADER;
	score = peace_war_score(w, war);
	sp = s == 1 ? score : -score;
	memset(&po, 0, sizeof po);
	if (kind == PEACE_DEMAND) {
		if (sp < 1) return PEACE_REFUSED;
		po.mode = PO_DEMAND;
		nt = peace_terms(p, war, s, sp, po.prov, PO_MAXPROV, &po.gold_m);
		po.nprov = nt;
	} else if (kind == PEACE_SURRENDER) {
		po.mode = PO_OFFER;
		nt = peace_terms(p, war, 3 - s, sp < 0 ? -sp : 0, po.prov, PO_MAXPROV, &po.gold_m);
		po.nprov = nt;
	}
	return player_to_ai(p, war, player, s == 1 ? wr->def[0] : wr->att[0], &po, 0) ? PEACE_ACCEPTED : PEACE_REFUSED;
}

/* ------------------------------------------------------- the peace window */
int peace_candidates(Peace *p, int war, int player, int mode, short *out, int max)
{
	World *w = p->w;
	int s, i, n = 0, give;
	if (war < 0 || war >= w->nwar || !(s = peace_side(w, war, player))) return 0;
	fill_sides(p, war);
	give = mode == PO_OFFER ? s : 3 - s;                       /* the side that cedes */
	for (i = 1; i < w->nprov && n < max; i++) {
		const Province *pv = &w->prov[i];
		if (pv->owner < 0 || p->side[pv->owner] != give) continue;
		if (w->country[pv->owner].capital == i && w->country[pv->owner].nprov_owned > 1) continue;
		if (mode == PO_OFFER ? pv->owner != player                       /* only our own provinces (NIEPEWNE) */
		                     : !((pv->controller >= 0 && p->side[pv->controller] == s) || is_core(pv, player))) continue;
		out[n++] = (short)i;
	}
	return n;
}

int peace_offer_value(const World *w, const PeaceOffer *o)
{
	int i, v = 0;
	for (i = 0; i < o->nprov; i++) v += peace_prov_value(w, o->prov[i]);
	v += (int)(o->gold_m / 25000);
	if (o->mode == PO_DEMAND) v += (o->vassal ? 40 : 0) + (o->religion ? 20 : 0) + (o->access ? 5 : 0);
	return v;
}

static int propose_sep(Peace *p, int war, int player, const PeaceOffer *o, long today);
int peace_player_propose(Peace *p, int war, int player, const PeaceOffer *o, long today)
{
	World *w = p->w;
	War *wr;
	int s;
	p->today = today;
	if (war < 0 || war >= w->nwar) return PEACE_NOWAR;
	if (o->sep_with > 0) return propose_sep(p, war, player, o, today);
	wr = &w->war[war];
	s = peace_side(w, war, player);
	if (!s || (s == 1 ? wr->att[0] : wr->def[0]) != player) return PEACE_NOTLEADER;
	return player_to_ai(p, war, player, s == 1 ? wr->def[0] : wr->att[0], o, 0) ? PEACE_ACCEPTED : PEACE_REFUSED;
}

/* a member leaves its war: the side lists and the battle-point matrices close the gap */
static void leave_war(World *w, int war, int c)
{
	War *wr = &w->war[war];
	int i, j, k;
	world_sfx(w, c, "diplomat_peace");                            /* a separate peace */
	for (k = 0; k < wr->natt && wr->att[k] != c; k++) ;
	if (k < wr->natt) {
		for (i = k; i + 1 < wr->natt; i++) { wr->att[i] = wr->att[i + 1]; memcpy(wr->bp_att[i], wr->bp_att[i + 1], sizeof wr->bp_att[i]); }
		for (j = 0; j < wr->ndef; j++) for (i = k; i + 1 < wr->natt; i++) wr->bp_def[j][i] = wr->bp_def[j][i + 1];
		wr->natt--;
	} else {
		for (k = 0; k < wr->ndef && wr->def[k] != c; k++) ;
		if (k == wr->ndef) return;
		for (i = k; i + 1 < wr->ndef; i++) { wr->def[i] = wr->def[i + 1]; memcpy(wr->bp_def[i], wr->bp_def[i + 1], sizeof wr->bp_def[i]); }
		for (j = 0; j < wr->natt; j++) for (i = k; i + 1 < wr->ndef; i++) wr->bp_att[j][i] = wr->bp_att[j][i + 1];
		wr->ndef--;
	}
	if (!wr->natt || !wr->ndef) remove_war(w, war);
}

/* COUNTRY_BROKEN (0x4550a0 step 3): c leaves every war it is in, ending hostilities with the other side both
 * ways (a truce); a war left with an empty side is over (white peace for c, no peace VP) */
void peace_leave_all(Peace *p, int c, long today)
{
	World *w = p->w;
	int war, j, s;
	p->today = today;
	for (war = w->nwar - 1; war >= 0; war--) {
		War *wr = &w->war[war];
		if (!(s = peace_side(w, war, c))) continue;
		for (j = 0; j < (s == 1 ? wr->ndef : wr->natt); j++) add_truce(p, c, s == 1 ? wr->def[j] : wr->att[j], today + TRUCE_DAYS);
		drop_records(p, war, c);
		leave_war(w, war, c);
		memset(p->wsc_ok, 0, sizeof p->wsc_ok); p->nps = 0;
	}
}

int peace_separate_ok(const World *w, int war, int player, int them)
{
	int s, t;
	if (war < 0 || war >= w->nwar || them < 0) return 0;
	s = peace_side(w, war, player); t = peace_side(w, war, them);
	if (!s || !t || s == t) return 0;
	return (s == 1 ? w->war[war].att[0] : w->war[war].def[0]) != player || (t == 1 ? w->war[war].att[0] : w->war[war].def[0]) != them;
}

int peace_candidates_sep(Peace *p, int war, int player, int them, int mode, short *out, int max)
{
	World *w = p->w;
	int i, n = 0, s = peace_side(w, war, player), giver = mode == PO_OFFER ? player : them;
	for (i = 1; i < w->nprov && n < max; i++) {
		const Province *pv = &w->prov[i];
		if (pv->owner != giver) continue;
		if (w->country[giver].capital == i && w->country[giver].nprov_owned > 1) continue;
		if (mode != PO_OFFER && !((pv->controller >= 0 && peace_side(w, war, pv->controller) == s) || is_core(pv, player))) continue;
		out[n++] = (short)i;
	}
	return n;
}

/* the separate peace itself: called by peace_player_propose when o->sep_with > 0 */
static int propose_sep(Peace *p, int war, int player, const PeaceOffer *o, long today)
{
	int them = o->sep_with - 1;
	p->today = today;
	if (!peace_separate_ok(p->w, war, player, them)) return PEACE_NOWAR;
	return player_to_ai(p, war, player, them, o, 1) ? PEACE_ACCEPTED : PEACE_REFUSED;
}

/* §4.5 page 2 (0x45cd00): war score 100 and we hold the enemy leader's capital (the option and the
 * religion / province-count tests of the exe: NIEPEWNE, left out) */
int peace_can_annex(Peace *p, int war, int player)
{
	World *w = p->w;
	War *wr;
	int s, e, cap;
	if (war < 0 || war >= w->nwar || !(s = peace_side(w, war, player))) return 0;
	wr = &w->war[war];
	if ((s == 1 ? wr->att[0] : wr->def[0]) != player) return 0;
	if ((s == 1 ? peace_war_score(w, war) : -peace_war_score(w, war)) < 100) return 0;
	e = s == 1 ? wr->def[0] : wr->att[0];
	cap = w->country[e].capital;
	return cap > 0 && cap < w->nprov && w->prov[cap].controller == player;
}

int peace_player_annex(Peace *p, int war, int player, long today)
{
	World *w = p->w;
	static short all[MAX_PROV];
	int s, e, i, n = 0;
	if (!peace_can_annex(p, war, player)) return PEACE_REFUSED;
	s = peace_side(w, war, player);
	e = s == 1 ? w->war[war].def[0] : w->war[war].att[0];
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == e) all[n++] = (short)i;
	logp(p, "  %s annexes %s", w->country[player].tag, w->country[e].tag);
	peace_make(p, war, s, all, n, 0, today);
	return PEACE_ACCEPTED;
}

/* ------------------------------------------------------------- the AI and peace (docs/SPEC_AI.md §4)
 * 0x53f240 (does an AI at war want peace), 0x5382f0 (the offer plan's gates), 0x539e10 (the offer it
 * builds), 0x4ccda0 + 0x5366c0 (how an AI answers any offer, the player's included). The war score of a
 * pair is the war's (NIEPEWNE: the exe scores the pair when they are not leaders). */
typedef struct {
	int sender, recv, war, demand, sep, annex, vassal, access, religion, better;
	int ws;                                   /* P+0x24: the sender's war score */
	int value;                                /* P+0x28 */
	short prov[64]; int nprov;
	long gold_m;
} AiOffer;

static Peace *g_wsp;                                      /* the Peace whose score cache ws_of uses */
static AiRec *ai_rec(Peace *p, int u, int t, int create)
{
	int i, free_i;
	for (i = 0; i < p->nrec; i++) if (p->rec[i].u == u && p->rec[i].t == t) return &p->rec[i];
	if (!create || p->nrec >= MAXREC) return NULL;
	free_i = p->nrec++;                                       /* appended; ended wars are compacted monthly (saves stay in order) */
	p->rec[free_i].u = (short)u; p->rec[free_i].t = (short)t; p->rec[free_i].last = 0; p->rec[free_i].bestv = -9999; p->rec[free_i].bests = -9999;
	return &p->rec[free_i];
}

static void ai_month_stats(Peace *p)
{
	World *w = p->w;
	int i;
	memset(p->men, 0, sizeof p->men); memset(p->ships, 0, sizeof p->ships); memset(p->ctl, 0, sizeof p->ctl);
	memset(p->coast, 0, sizeof p->coast); memset(p->cities, 0, sizeof p->cities); memset(p->rebels, 0, sizeof p->rebels);
	memset(p->wsc_ok, 0, sizeof p->wsc_ok); p->nps = 0; g_wsp = p;
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].owner >= 0) p->men[w->land[i].owner] += w->land[i].infantry + w->land[i].cavalry + 100 * w->land[i].artillery;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner >= 0) p->ships[w->naval[i].owner] += w->naval[i].warships + w->naval[i].galleys;
	for (i = 1; i < w->nprov; i++) {
		const Province *pv = &w->prov[i];
		if (pv->controller >= 0) { p->ctl[pv->controller]++; if (pv->population >= 1000) p->cities[pv->controller]++; }
		if (pv->owner >= 0 && pv->kind == 1) p->coast[pv->owner] = 1;
		if (pv->owner >= 0 && w->reb >= 0 && pv->controller == w->reb) p->rebels[pv->owner]++;
	}
	/* records of wars that are over are dropped when the war ends (drop_records; NIEPEWNE reset) */
}

/* the strength indices +0x364 / +0x368 (docs/SPEC_AI_STRENGTH.md): the AI's monthly values, read through strfn */
static long land_pow(const Peace *p, int c)
{
	const Country *C = &p->w->country[c];
	if (p->strfn) return p->strfn(p->str_user, c, 0);
	return 1000L * C->land_tech + 2000L * (C->m_mil ? C->m_mil : 3) + p->men[c];   /* (no AI linked: the old stand-in) */
}
static long sea_pow(const Peace *p, int c)
{
	const Country *C = &p->w->country[c];
	if (p->strfn) return p->strfn(p->str_user, c, 1);
	return 1000L * C->naval_tech + 2000L * (C->m_mil ? C->m_mil : 3) + 1000L * p->ships[c];
}

static int sas(const Peace *p, int s, int u)   /* the sender is at least as strong */
{
	if (land_pow(p, s) < land_pow(p, u)) return 0;
	if (p->coast[s] && p->coast[u] && sea_pow(p, s) < sea_pow(p, u)) return 0;
	return 1;
}

static int bucket(long a, long b)              /* 0x5396f0: ratio bucket of a / b, a and b >= 0 */
{
	static const int lim[14] = { 4000, 3000, 2000, 1700, 1500, 1250, 1000, 800, 660, 580, 500, 330, 250, 200 };
	long r;
	int i;
	if (b <= 0) return 1;
	r = a * 1000 / b;
	for (i = 0; i < 14; i++) if (r > lim[i]) return 7 - i;
	return -7;
}

/* 5.2 (0x4d0800) for a pair: u against t, each standing for its whole side when it is that side's
 * leader. Battle points of the members involved (clamped +-95), minus the war value of u's side's
 * provinces t's side controls, plus the reverse; +-100 when one controls every city of the other. */
static int pair_score(const World *w, int war, int u, int t)
{
	const War *wr = &w->war[war];
	static unsigned char in[MAX_COUNTRY];
	int su = peace_side(w, war, u), st = peace_side(w, war, t), i, j, ua, ta, lu, lt;
	long bt = 0, occ = 0;
	int all_t = 1, all_u = 1, any_t = 0, any_u = 0;
	if (!su || !st || su == st) return 0;
	lu = su == 1 ? wr->att[0] : wr->def[0]; lt = st == 1 ? wr->att[0] : wr->def[0];
	ua = lu == u; ta = lt == t;
	memset(in, 0, sizeof in);
	for (i = 0; i < wr->natt; i++) { int c = wr->att[i]; if (su == 1 ? (ua || c == u) : (ta || c == t)) in[c] = su == 1 ? 1 : 2; }
	for (i = 0; i < wr->ndef; i++) { int c = wr->def[i]; if (su == 2 ? (ua || c == u) : (ta || c == t)) in[c] = su == 2 ? 1 : 2; }
	for (i = 0; i < wr->natt; i++)
		for (j = 0; j < wr->ndef; j++) {
			int a = wr->att[i], d = wr->def[j];
			long v = (long)wr->bp_att[i][j] - (long)wr->bp_def[j][i];          /* attacker a against defender d */
			if (!in[a] || !in[d]) continue;
			bt += in[a] == 1 ? v : -v;
		}
	if (bt > 95) bt = 95;
	if (bt < -95) bt = -95;
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		int so, sc;
		if (p->owner < 0) continue;
		so = in[p->owner]; sc = p->controller >= 0 ? in[p->controller] : 0;
		if (so == 1 && sc == 2) occ -= peace_prov_value(w, i);
		if (so == 2 && sc == 1) occ += peace_prov_value(w, i);
		if (p->population >= 1000) {                          /* 0x4cfd50: every full city */
			if (p->owner == t) { any_t = 1; if (sc != 1) all_t = 0; }
			if (p->owner == u) { any_u = 1; if (sc != 2) all_u = 0; }
		}
	}
	if (any_t && all_t) return 100;
	if (any_u && all_u) return -100;
	bt += occ;
	return (int)(bt > 99 ? 99 : bt < -99 ? -99 : bt);
}

static int ws_pair(const World *w, int war, int u, int t)
{
	int i, k;
	if (!g_wsp || war < 0 || war >= MAX_WAR) return pair_score(w, war, u, t);
	if (g_wsp->wsc_day != g_wsp->today) { g_wsp->nps = 0; g_wsp->wsc_day = g_wsp->today; }
	for (i = 0; i < g_wsp->nps; i++) if (g_wsp->ps[i].war == war && g_wsp->ps[i].u == u && g_wsp->ps[i].t == t) return g_wsp->ps[i].v;
	k = pair_score(w, war, u, t);
	if (g_wsp->nps < 64) { g_wsp->ps[g_wsp->nps].war = (short)war; g_wsp->ps[g_wsp->nps].u = (short)u; g_wsp->ps[g_wsp->nps].t = (short)t; g_wsp->ps[g_wsp->nps].v = (short)k; g_wsp->nps++; }
	return k;
}
static int months(long a, long b) { return (int)((a - b) / 30); }
static int leader_of(const World *w, int war, int c) { const War *wr = &w->war[war]; return peace_side(w, war, c) == 1 ? wr->att[0] : wr->def[0]; }
static long pdist(const World *w, int prov, int c)   /* 0x43a820: province to c's capital / 100 */
{
	int cap = w->country[c].capital;
	long dx, dy;
	if (cap <= 0 || cap >= w->nprov) return 100;
	dx = w->prov[prov].city_x - w->prov[cap].city_x; if (dx < 0) dx = -dx;
	if (dx > 0x2500) dx = 0x4a00 - dx;
	dy = w->prov[prov].city_y - w->prov[cap].city_y;
	{ long v = dx * dx + dy * dy, r = 0, bt = 1L << 30; while (bt > v) bt >>= 2; while (bt) { if (v >= r + bt) { v -= r + bt; r = (r >> 1) + bt; } else r >>= 1; bt >>= 2; } return r / 100; }
}

/* 0x539880: are we (u) doing well against t */
static int winning(Peace *p, int war, int u, int t)
{
	const World *w = p->w;
	long su = land_pow(p, u), st = land_pow(p, t);
	int v = ws_pair(w, war, u, t);
	v += bucket(su, st);
	v += bucket(w->country[u].inc_m, w->country[t].inc_m);
	v += bucket(p->ctl[u], w->country[u].nprov_owned) - bucket(p->ctl[t], w->country[t].nprov_owned);
	v += bucket(w->country[u].manpower_m, w->country[t].manpower_m);
	return v > 0;
}

/* 0x53a7f0: could c hold province prov */
static int may_go(const Peace *p, int c, int prov)
{
	const World *w = p->w;
	const Province *pv = &w->prov[prov];
	if (p->m) {
		short nb[32];
		int n = mil_adjacent(p->m, prov, nb, 32), k;
		for (k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < w->nprov && w->prov[nb[k]].owner == c) return 1;
	}
	if (world_culture_ok(w, c, pv->culture) && pv->religion == w->country[c].religion) return 1;
	if (p->ships[c] > 0 && pv->kind == 1 && p->ships[c] * 10L > pdist(w, prov, c)) return 1;
	return 0;
}

static int full_cities(const Peace *p, int c) { return p->cities[c]; }

static int wv(const World *w, int prov) { const Province *pv = &w->prov[prov]; return peace_prov_value(w, prov) * (pv->owner == pv->controller ? 4 : 2); }

static int offer_value(const World *w, const AiOffer *o)   /* 0x4ccde0 */
{
	int i, v = 0;
	for (i = 0; i < o->nprov; i++) v += wv(w, o->prov[i]);
	v += (int)(o->gold_m / 25000);
	if (o->religion) v += 50;
	if (o->access) v += 20;
	if (o->vassal) v += 70;
	return v;
}

/* our share of income under occupation (A), occupied provinces (occ), listed cores (C) */
static void occupation(const Peace *p, int u, const AiOffer *o, long *A_m, int *occ, int *ncore)
{
	const World *w = p->w;
	long tot = 0, a = 0;
	int i, k;
	*occ = 0; *ncore = 0;
	for (i = 1; i < w->nprov; i++) {
		const Province *pv = &w->prov[i];
		if (pv->owner != u) continue;
		tot += pv->income_m;
		if (pv->controller != u) { a += pv->income_m; (*occ)++; }
	}
	*A_m = tot > 0 ? a * 1000 / tot : 0;
	for (i = 0; i < o->nprov; i++) for (k = 0; k < 8; k++) if (w->prov[o->prov[i]].cores[k] == u) { (*ncore)++; break; }
}

/* 0x4ccda0 + 0x5366c0: does AI country u accept offer o */
static int ai_accept(Peace *p, int u, AiOffer *o)
{
	World *w = p->w;
	const War *wr = &w->war[o->war];
	int x = o->sender, S = -o->ws, V = o->value, n = o->nprov, M, T, occ, ncore, aS, leader_us;
	long A, pm = 0, today = p->today;
	AiRec *r;
	if (!o->annex && !o->religion) {                          /* forced acceptance */
		if (o->demand ? o->ws - V >= 50 : V + o->ws >= 50) return 1;
	}
	if (x < 0 || w->country[x].nprov_owned <= 0) return 0;
	if (p->fero && p->fero[u] && full_cities(p, u) > 0 && full_cities(p, x) > 0) return 0;
	if (V > 100) return 0;
	T = months(today, wr->start_day) * (int)(w->country[u].we_m / 5000 + 1);
	M = months(today, wr->last_combat);
	occupation(p, u, o, &A, &occ, &ncore);
	if (A == 0 && n == 0 && o->sep && T < 120) return 0;
	leader_us = leader_of(w, o->war, u) == u;
	if (!o->demand && p->mode && p->mode(p->mode_user, u) == 2 && T > 40 && n > 0) return 1;   /* COLONISE */
	if (!o->demand) { int i; for (i = 0; i < n; i++) if (!may_go(p, u, o->prov[i])) return 0; }
	aS = S < 0 ? -S : S;
	if (o->demand) {
		if (S >= -1) return 0;
		if (o->annex) {
			if (p->ctl[u] == 0 && p->men[u] == 0 && w->country[u].nprov_owned < 2) pm = 990;
			else {
				long xx = 50L * w->country[u].nprov_owned + 100L * p->ctl[u] + w->country[u].treasury_m / 200;   /* 0.05 own + 0.1 ctl + 0.005 treasury, x1000 */
				int mem = peace_side(w, o->war, u) == 1 ? wr->natt : wr->ndef;
				if (xx > 660) xx = 660;
				pm = 1000 - xx + 10L * w->country[x].nprov_owned + 50L * M;
				if (mem > 2) pm /= mem;
			}
		} else {
			long t;
			if (A >= 1000 && p->men[u] == 0) return 1;
			t = 1000 - (aS ? 1000L * V / aS : 0) - 200L * ncore;
			if (V <= aS) t += 50L * M; else t /= 2;
			t = t * (50L * occ + A + 500) / 1000;
			if (V > 90) t /= 2;
			if (V >= 100) t /= 20;
			pm = t + 10L * (aS - V);
			if (V < 1 && T < 60 && S > -10) pm /= 20;
		}
	} else if (V < 1) {                                       /* white peace */
		int i, held = 0;
		for (i = 1; i < w->nprov && !held; i++) {
			const Province *pv = &w->prov[i];
			if (pv->owner == u && pv->population >= 1000 && pv->controller != u && pv->controller >= 0 && pv->controller != w->reb) held = 1;
		}
		if (M > 12 && !held) return 1;
		if (T < 24 && A < 10) return 0;
		return sas(p, x, u);
	} else if (S < 0) {                                       /* an offer while we lose */
		if (sas(p, x, u) || M >= 2) return 1;
		return aS > prand(p) % 10;
	} else {                                                  /* an offer while we win */
		long t;
		int st = sas(p, x, u);
		if (V > S) t = 10L * V;
		else { t = 1000L * V / 150 + 10L * T - 250; if (!st) t -= 250; }
		if (st) t += 500;
		pm = t + 50L * M;
		if (S > 99 && n < 1) pm = 0;
	}
	/* FINAL */
	if (A < 1 && !leader_us && n == 0) return 0;
	if (winning(p, o->war, u, x) && o->demand) pm /= 10;
	r = ai_rec(p, u, x, 1);
	if (!o->annex && r) {
		int v = o->demand ? -V : V;
		if (!o->better && v < r->bestv && o->ws >= r->bests && M < 4 && A < 660) return 0;
		if (o->ws > r->bests) r->bests = (short)o->ws;
		if (v > r->bestv) r->bestv = (short)v;
	}
	if (o->better) pm *= 3;
	if (w->country[u].nprov_owned > 0) pm += 100L * p->rebels[u] / w->country[u].nprov_owned;
	return (long)(prand(p) % 100) * 10 < pm;
}

/* 0x539ab0: how much u wants province prov to go to holder h (f = 1 when u concedes) */
static long prov_want(Peace *p, int u, int war, int prov, int h, int f, int demand)
{
	World *w = p->w;
	const Province *pv = &w->prov[prov];
	long v, a, dd, s;
	int k, core_u = 0, core_h = 0;
	if (!may_go(p, h, prov)) return -1;
	v = pv->income_m / 1000 + 5L * pv->fort + (pv->kind == 1 ? 5 : 0);
	if (f) v = 100 - v;
	if (v > 100) v = 100;
	for (k = 0; k < 8; k++) { if (pv->cores[k] == u) core_u = 1; if (pv->cores[k] == h) core_h = 1; }
	if (pv->owner == u && core_u && demand) a = 25;
	else if (pv->owner == u && core_h && f) a = 15;
	else if (pv->population >= 1000 && pv->controller != h) a = -150;
	else a = 0;
	if (pv->kind == 1) a += 1;
	else {
		int near = 0;
		if (p->m) { short nb[32]; int n = mil_adjacent(p->m, prov, nb, 32); for (k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < w->nprov && w->prov[nb[k]].owner == h) near = 1; }
		a += near ? 10 : -25;
	}
	dd = pdist(w, prov, h);
	if (dd < 10) dd = 10;
	s = v + a - dd + months(p->today, w->war[war].start_day) / 12;
	if (f && pv->owner != u) s -= (world_rel(w, u, pv->owner) / 1000 + 50) / 2;
	return s < 1 ? 1 : s;
}

/* 0x539e10: the offer u sends to t (0 = none) */
static int build_offer(Peace *p, int u, int t, int war, AiOffer *o)
{
	World *w = p->w;
	const War *wr = &w->war[war];
	static short cand[MAX_PROV]; static long sc[MAX_PROV];
	int ws = ws_pair(w, war, u, t), nc = 0, i, j, pr, pg, sideR, B, b, D = w->opt_diff + 2;
	AiRec *r = ai_rec(p, u, t, 1);
	memset(o, 0, sizeof *o);
	o->sender = u; o->recv = t; o->war = war; o->ws = ws;
	o->demand = winning(p, war, u, t) || ws > -10;
	o->sep = leader_of(w, war, u) != u || leader_of(w, war, t) != t || (p->fero && p->fero[u]);
	{	/* annexation (0x45cd00 + 0x539d10): score 100, we hold their capital, at most one city or pagan */
		int cap = w->country[t].capital;
		if (ws >= 100 && cap > 0 && cap < w->nprov && w->prov[cap].controller == u && (p->cities[t] <= 1 || world_religion_index(w, w->country[t].religion) == 7)) {
			long pa = 100;
			int yes = world_overlord(w, t) >= 0, rt = world_religion_index(w, w->country[t].religion);
			if (w->country[t].elector && rt >= 0 && rt <= 4) pa = 50;   /* a Christian elector (0x539d39, SPEC_HRE 3.5) */
			for (i = 1; i < w->nprov && !yes; i++) {
				const Province *pv = &w->prov[i];
				if (pv->owner != t) continue;
				for (j = 0; j < 8; j++) if (pv->cores[j] == u) yes = 1;
				if (pv->religion != w->country[u].religion) pa -= 10;
				if (!world_culture_ok(w, u, pv->culture)) pa -= 10;
			}
			if (yes || prand(p) % 100 < pa) { o->demand = 1; o->annex = 1; o->sep = 1; return 1; }
		}
	}
	pr = o->demand ? u : t; pg = o->demand ? t : u;
	sideR = peace_side(w, war, pr);
	for (i = 1; i < w->nprov; i++) {                          /* 0x4cca90 candidates */
		const Province *pv = &w->prov[i];
		int ok = 0, h;
		if (pv->owner != pg || w->country[pg].capital == i) continue;
		if (o->sep ? pv->controller == pr : (pv->controller >= 0 && peace_side(w, war, pv->controller) == sideR)) ok = 1;
		else for (j = 0; j < 8; j++) if (pv->cores[j] == pr && (pv->controller == pg || pv->controller == w->reb || pv->controller == pr)) ok = 1;
		if (!ok) continue;
		h = pv->controller >= 0 && peace_side(w, war, pv->controller) == sideR ? pv->controller : pr;
		sc[nc] = prov_want(p, u, war, i, h, !o->demand, o->demand);
		if (sc[nc] > 0) cand[nc++] = (short)i;
	}
	if (o->demand && nc == 0 && ws > 10)
		for (i = 1; i < w->nprov; i++) {
			const Province *pv = &w->prov[i];
			if (pv->owner == pg && w->country[pg].capital != i && pv->controller >= 0 && peace_side(w, war, pv->controller) == sideR) { sc[nc] = 5; cand[nc++] = (short)i; }
		}
	for (i = 1; i < nc; i++) {                                /* best first */
		long s = sc[i]; short c = cand[i];
		for (j = i; j > 0 && sc[j - 1] < s; j--) { sc[j] = sc[j - 1]; cand[j] = cand[j - 1]; }
		sc[j] = s; cand[j] = c;
	}
	b = ws < 0 ? -ws : ws;
	if (b > 10) b = prand(p) % (b / 4) + 90 * b / 100;
	if (!o->demand) b -= b / 5;
	if (r && b < r->bestv) b = r->bestv;
	B = b;
	if (ws < 0 && o->demand) B = 1 + prand(p) % 3;
	if (o->demand) {
		int rt = world_religion_index(w, w->country[t].religion), ru = world_religion_index(w, w->country[u].religion);
		int rel_ok = ws >= 50 && rt != ru && !((rt == 2 || rt == 3) && (ru == 2 || ru == 3)) &&
		             (rt == 7 || (rt != 4 && ru != 4 && ((rt >= 0 && rt <= 3 && ru >= 0 && ru <= 3) || ((rt == 5 || rt == 6) && (ru == 5 || ru == 6)))));
		int cap = w->country[t].capital;
		if (prand(p) % 10 == 0 && rel_ok) { o->religion = 1; B -= 50; }
		if (nc < B / 10 && prand(p) % 100 < 20 && p->nbfn && p->nbfn(p->nb_user, u, t) && B > 20) { B -= 20; o->access = 1; }
		if (world_overlord(w, t) < 0 && cap > 0 && cap < w->nprov && w->prov[cap].controller == u && B > 70) { B -= 70; o->vassal = 1; }
	}
	if (!(B <= 3 || (!o->demand && B <= 20)))
		for (i = 0; i < nc && B > 0 && o->nprov < 64; i++) { o->prov[o->nprov++] = cand[i]; B -= wv(w, cand[i]); }
	if (B > 0) {
		long cap;
		if (o->sep) cap = w->country[pg].treasury_m > 0 ? w->country[pg].treasury_m : 0;
		else {
			int s = peace_side(w, war, pg), cnt = s == 1 ? wr->natt : wr->ndef;
			long mn = 5000000L;
			for (i = 0; i < cnt; i++) { long tr = w->country[s == 1 ? wr->att[i] : wr->def[i]].treasury_m; if (tr < mn) mn = tr; }
			cap = mn > 0 ? mn * cnt : 0;
		}
		o->gold_m = 25000L * B;
		if (o->gold_m > cap) o->gold_m = cap / 1000 * 1000;
	}
	o->value = offer_value(w, o);
	if (o->demand && o->nprov == 0 && p->ctl[u] > 75 * w->country[u].nprov_owned / 100) return 0;
	{	int m2 = months(p->today, wr->start_day) * (int)(w->country[u].we_m / 5000 + 1);
		if (w->country[t].human && m2 < 60 && D >= 4 && p->overfn && p->overfn(p->nb_user, t) && (b == 0 || o->nprov == 0)) return 0;
	}
	return 1;
}

/* carry out an accepted offer */
static void apply_offer(Peace *p, AiOffer *o)
{
	World *w = p->w;
	int war = o->war, i, give, get, win;
	long gold;
	if (o->annex) {
		static short all[MAX_PROV];
		int n = 0, t = o->demand ? o->recv : o->sender, u = o->demand ? o->sender : o->recv;
		for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == t) all[n++] = (short)i;
		logp(p, "  %s annexes %s", w->country[u].tag, w->country[t].tag);
		peace_make(p, war, peace_side(w, war, u), all, n, 0, p->today);
		return;
	}
	give = o->demand ? o->recv : o->sender; get = give == o->recv ? o->sender : o->recv;
	if (o->religion && w->country[give].religion != w->country[get].religion) {
		{	/* 0x44e991..0x44ec86: badboy for forcing a conversion, by the two religions before it */
			int r1 = world_religion_index(w, w->country[get].religion), r2 = world_religion_index(w, w->country[give].religion);
			long bb;
			if (r1 >= 0 && r1 <= 4 && r2 >= 0 && r2 <= 4) bb = 5000;              /* Christian on Christian */
			else if ((r1 == 5 || r1 == 6) && (r2 == 5 || r2 == 6)) bb = 5000;    /* Muslim on Muslim */
			else if (r1 != 7 && r2 == 7) bb = 0;                                  /* converting pagans */
			else bb = 2000;
			w->country[get].badboy_m += bb;
		}
		{	/* 0x44e8bf..0x44e96b: -100 relation with every other country of the converted one's (old) faith that is
			 * neither in a military alliance with the forcer (+0x3c) nor on its side in a war (+0x3d) */
			int x2, rg = world_religion_index(w, w->country[give].religion);
			for (x2 = 0; x2 < w->ncountry; x2++) {
				if (x2 == get || x2 == give || w->country[x2].nprov_owned <= 0) continue;
				if (world_religion_index(w, w->country[x2].religion) != rg) continue;
				if (world_allied(w, get, x2, ALLY_MILITARY) || peace_cobel(p, get, x2)) continue;
				world_rel_add(w, get, x2, -100000L);
			}
		}
		w->country[give].religion = w->country[get].religion; logp(p, "  %s converts to the religion of %s", w->country[give].tag, w->country[get].tag);
		world_default_tol(w, give);                           /* 0x44ecf2: the new faith's tolerances */
		w->country[give].conv_day = p->today;                 /* +0x11a: the conversion date */
		world_vp(w, get, VP_PEACE, w->country[give].tracked ? 100000L : 20000L);   /* 0x44ecbf: +100 for a featured country (+0x11e), else +20 */
	}
	if (o->access) w->dipflag[give][get] |= DF_ACCESS;
	if (o->vassal && w->nally < MAX_ALLY && world_overlord(w, give) < 0) {
		Alliance *al = &w->ally[w->nally++];
		memset(al, 0, sizeof *al);
		al->type = ALLY_VASSAL; al->member[0] = (short)get; al->member[1] = (short)give; al->n = 2;
		al->expiry = w->now; al->expiry.year = (short)(w->now.year + 100);
		logp(p, "  %s becomes a vassal of %s", w->country[give].tag, w->country[get].tag);
	}
	if (!o->sep) {
		win = o->value <= 0 ? 0 : peace_side(w, war, get);
		gold = o->gold_m;
		peace_make(p, war, win, o->prov, o->nprov, gold, p->today);
		return;
	}
	for (i = 0; i < o->nprov; i++) {                          /* separate: the two exchange only */
		Province *pv = &w->prov[o->prov[i]];
		if (pv->owner != give) continue;
		if (!is_core(pv, get)) w->country[get].badboy_m += 1000;
		world_vp(w, give, VP_PEACE, -pv->income_m); world_vp(w, get, VP_PEACE, pv->income_m);
		pv->owner = pv->controller = (short)get;
		w->country[give].nprov_owned--; w->country[get].nprov_owned++;
		if (w->country[give].capital == o->prov[i]) w->country[give].capital = -1;
		w->owner_epoch++;
	}
	gold = o->gold_m;
	if (gold > w->country[give].treasury_m) gold = w->country[give].treasury_m > 0 ? w->country[give].treasury_m : 0;
	w->country[give].treasury_m -= gold; w->country[get].treasury_m += gold; world_book_inc(w, get, INC_PEACE, gold);
	for (i = 1; i < w->nprov; i++) {
		Province *pv = &w->prov[i];
		if ((pv->owner == o->sender && pv->controller == o->recv) || (pv->owner == o->recv && pv->controller == o->sender)) pv->controller = pv->owner;
	}
	{	long v = 100L * (o->ws < 0 ? -o->ws : o->ws);
		world_vp(w, give, VP_PEACE, -v); world_vp(w, get, VP_PEACE, v);
		if (w->country[get].defender) world_vp(w, get, VP_PEACE, 15000);
	}
	add_truce(p, o->sender, o->recv, p->today + TRUCE_DAYS);
	logp(p, "separate peace: %s and %s (%d provinces, %ld ducats)", w->country[o->sender].tag, w->country[o->recv].tag, o->nprov, gold / 1000);
	{ int c = leader_of(w, war, o->sender) == o->sender ? o->recv : o->sender; drop_records(p, war, c); leave_war(w, war, c); memset(p->wsc_ok, 0, sizeof p->wsc_ok); p->nps = 0; }
}

/* send o: an AI receiver answers, the human leader gets the dialog (whole-war offers only) */
static int send_offer(Peace *p, AiOffer *o)
{
	World *w = p->w;
	AiRec *r = ai_rec(p, o->sender, o->recv, 1);
	if (r) r->last = p->today;
	o->better = o->ws > 40 && (!o->demand || o->value < o->ws);  /* PEACEBETTER (0x442837) */
	logp(p, "%s sends a 'OfferPeace' to %s (%s, value %d, %d provinces)", w->country[o->sender].tag, w->country[o->recv].tag,
	     o->annex ? "annex" : o->demand ? "demand" : o->value ? "offer" : "white peace", o->value, o->nprov);
	if (w->country[o->recv].human) {
		int winner;
		if (o->sep || !p->offer) return 0;
		winner = o->value <= 0 ? 0 : peace_side(w, o->war, o->demand ? o->sender : o->recv);
		if (!winner) { peace_make(p, o->war, 0, NULL, 0, 0, p->today); return 1; }   /* white peace for the player: the dialog shows terms only */
		if (p->offer(p->offer_user, o->war, winner, o->prov, o->nprov, o->gold_m)) { apply_offer(p, o); return 1; }
		logp(p, "the player declines the peace terms");
		return 0;
	}
	if (ai_accept(p, o->recv, o)) { logp(p, "%s accepts peace with %s", w->country[o->recv].tag, w->country[o->sender].tag); apply_offer(p, o); return 1; }
	logp(p, "%s declines peace with %s", w->country[o->recv].tag, w->country[o->sender].tag);
	if (o->better) {                                          /* EXEC_DECLINE_PEACE 0x44eea9: stability -1, VP -25 */
		if (w->country[o->recv].stability > -3) w->country[o->recv].stability--;
		world_vp(w, o->recv, VP_PEACE, -25000);
	}
	return 0;
}

/* 0x5382f0 gates and score, then the offer is built and sent at once (the plan's execute, NIEPEWNE: no delay) */
static int try_offer(Peace *p, int u, int t, int war, int maw)
{
	World *w = p->w;
	const War *wr = &w->war[war];
	AiRec *r = ai_rec(p, u, t, 1);
	int ws = ws_pair(w, war, u, t), su = peace_side(w, war, u), i;
	long base, a = 0, b = 0;
	AiOffer o;
	if (w->tutorial) return 0;                                /* 0x538388: the AI offers no peace in a tutorial */
	if (!r) return 0;
	if (r->last > 0) {                                        /* 0x535520: one month at war */
		long dt = p->today - r->last;
		if (dt < 30) return 0;
		if (dt < 60 && prand(p) % 100 >= 50) return 0;
	}
	base = 25L * (ws < 0 ? -ws : ws);
	if (ws < 0 && land_pow(p, u) > land_pow(p, t) && maw < 6) base = 0;
	if (su == 1) base -= 5L * (32 - maw > 0 ? 32 - maw : 0);
	for (i = 0; i < wr->natt; i++) { long f = land_pow(p, wr->att[i]) / 1000; if (su == 1) a += f; else b += f; }
	for (i = 0; i < wr->ndef; i++) { long f = land_pow(p, wr->def[i]) / 1000; if (su == 2) a += f; else b += f; }
	if (a < 1) a = 1;
	if (b < 1) b = 1;
	base += 10 * (a > b ? a : b) / (a > b ? b : a);
	if (ws > 0 && w->country[t].nprov_owned > 0) base += 50L * (w->country[t].nprov_owned - p->ctl[t]) / w->country[t].nprov_owned;
	if (leader_of(w, war, u) != u) base -= (world_rel(w, u, leader_of(w, war, u)) / 1000) / 2 + 50;
	if (leader_of(w, war, t) != t) base -= 20;
	{	long rr = w->country[u].nprov_owned > 0 ? 1000L * p->rebels[u] / w->country[u].nprov_owned : 0;
		base += 10 * rr / 1000;
		if (winning(p, war, u, t) && ws <= 0 && rr < 250) return 0;
	}
	if (p->fero && p->fero[u] && full_cities(p, u) > 0 && full_cities(p, t) > 0) return 0;
	if (prand(p) % 100 >= base) return 0;
	if (!build_offer(p, u, t, war, &o)) return 0;
	send_offer(p, &o);
	return 1;
}

/* 0x53f240 */
int peace_ai_think(Peace *p, int u, long today)
{
	World *w = p->w;
	int war, k, maw = 0;
	long start = today;
	p->today = today;
	if (!p->stats_day) { ai_month_stats(p); p->stats_day = today; }   /* once a month (peace_month clears it) */
	for (war = 0; war < w->nwar; war++) if (peace_side(w, war, u) && w->war[war].start_day < start) start = w->war[war].start_day;
	maw = months(today, start);                               /* country+0x38a: continuous months at war (NIEPEWNE: the oldest war) */
	for (war = 0; war < w->nwar; war++) {
		const War *wr = &w->war[war];
		int su = peace_side(w, war, u);
		if (!su) continue;
		for (k = 0; k < (su == 1 ? wr->ndef : wr->natt); k++) {
			int x = su == 1 ? wr->def[k] : wr->att[k], m;
			long base = maw;
			if (w->country[x].nprov_owned <= 0) continue;
			if (su == 1) base -= 18;
			m = months(today, wr->last_combat);
			if (m > 3) base += 5L * m;
			base += labs((long)w->country[u].nprov_owned - p->ctl[u]) + labs((long)p->ctl[x] - w->country[x].nprov_owned);
			if (p->ctl[u] == 0) {
				if (w->country[u].nprov_owned > 1 && try_offer(p, u, x, war, maw)) return 1;
				base += 50;
			}
			base += 4L * (w->country[u].we_m / 1000);
			if (w->country[u].stability < -2) base += 10;
			if (w->country[u].stability < -1) base += 10;
			if (w->country[x].nprov_owned > 0 && p->ctl[x] * 100L < 26L * w->country[x].nprov_owned) { if (try_offer(p, u, x, war, maw)) return 1; }
			else if (prand(p) % 100 < base && try_offer(p, u, x, war, maw)) return 1;
			if (war >= w->nwar || &w->war[war] != wr) return 0;
		}
	}
	return 0;
}

/* the player's offer o to the AI: AiOffer from PeaceOffer; 1 = accepted and carried out */
static int player_to_ai(Peace *p, int war, int player, int them, const PeaceOffer *po, int sep)
{
	World *w = p->w;
	AiOffer o;
	int i;
	if (p->stats_day != p->today) { ai_month_stats(p); p->stats_day = p->today; }
	memset(&o, 0, sizeof o);
	o.sender = player; o.recv = them; o.war = war; o.sep = sep;
	o.demand = po->mode == PO_DEMAND;
	o.ws = ws_pair(w, war, player, them);
	for (i = 0; i < po->nprov && i < 64; i++) o.prov[o.nprov++] = po->prov[i];
	o.gold_m = po->mode ? po->gold_m : 0;
	if (o.demand) { o.vassal = po->vassal; o.religion = po->religion; o.access = po->access; }
	o.value = po->mode ? offer_value(w, &o) : 0;
	o.better = o.ws > 40 && (!o.demand || o.value < o.ws);
	logp(p, "%s offers peace to %s (%s, value %d)", w->country[player].tag, w->country[them].tag, o.demand ? "demand" : o.value ? "offer" : "white peace", o.value);
	if (!ai_accept(p, them, &o)) {
		logp(p, "%s declines peace with %s", w->country[them].tag, w->country[player].tag);
		if (o.better) { if (w->country[them].stability > -3) w->country[them].stability--; world_vp(w, them, VP_PEACE, -25000); }
		return 0;
	}
	apply_offer(p, &o);
	return 1;
}

/* ------------------------------------------------------------- save game */
static void ai_month_stats(Peace *p);
void peace_save(const Peace *p, FILE *f)
{
	int i;
	ai_month_stats((Peace *)p); ((Peace *)p)->stats_day = p->today ? p->today : 1;   /* as the load rebuilds them */
	fprintf(f, "amieu2_peace = {\n\tnextoffer = %ld seed = { %ld %ld }\n\ttruces = {", p->next_offer,
	        (long)(p->seed >> 16), (long)(p->seed & 0xFFFF));
	for (i = 0; i < p->ntruce; i++)
		if (p->truce[i].until - TRUCE_DAYS + TRUCE_KEEP > p->today) fprintf(f, "\n\t\t%s %s %ld", p->w->country[p->truce[i].a].tag, p->w->country[p->truce[i].b].tag, p->truce[i].until);
	fprintf(f, " }\n\tairec = {");
	for (i = 0; i < p->nrec; i++)
		if (p->rec[i].u >= 0) fprintf(f, " %s %s %ld %d %d", p->w->country[p->rec[i].u].tag, p->w->country[p->rec[i].t].tag, p->rec[i].last, p->rec[i].bestv, p->rec[i].bests);
	fprintf(f, " }\n}\n");
}

void peace_restore(Peace *p, const SNode *root)
{
	const SNode *blk = script_get(root, "amieu2_peace"), *q;
	long v[2]; int n = 0;
	if (!blk) return;
	p->next_offer = script_int(blk, "nextoffer", 0);
	for (q = script_get(blk, "seed"), q = q ? q->child : NULL; q && n < 2; q = q->next) if (!q->key && q->val) v[n++] = parse_int(q->val);
	if (n == 2) p->seed = (unsigned long)v[0] << 16 | (unsigned long)v[1];
	p->ntruce = 0;
	for (q = script_get(blk, "truces"), q = q ? q->child : NULL; q; ) {
		const char *t[3];
		int got = 0;
		for (; q && got < 3; q = q->next) if (!q->key && q->val) t[got++] = q->val;
		if (got == 3 && p->ntruce < MAXTRUCE) {
			int a = world_country(p->w, t[0]), b = world_country(p->w, t[1]);
			if (a >= 0 && b >= 0) { p->truce[p->ntruce].a = (short)a; p->truce[p->ntruce].b = (short)b; p->truce[p->ntruce].until = parse_int(t[2]); p->ntruce++; }
		}
	}
	p->today = cal_day(p->w->now.year, p->w->now.month, p->w->now.day);
	ai_month_stats(p); p->stats_day = p->today ? p->today : 1;
	p->nrec = 0;
	for (q = script_get(blk, "airec"), q = q ? q->child : NULL; q; ) {
		const char *t[5];
		int got = 0;
		for (; q && got < 5; q = q->next) if (!q->key && q->val) t[got++] = q->val;
		if (got == 5 && p->nrec < MAXREC) {
			int a = world_country(p->w, t[0]), b = world_country(p->w, t[1]);
			if (a >= 0 && b >= 0) {
				AiRec *r = &p->rec[p->nrec++];
				r->u = (short)a; r->t = (short)b; r->last = parse_int(t[2]); r->bestv = (short)parse_int(t[3]); r->bests = (short)parse_int(t[4]);
			}
		}
	}
}
