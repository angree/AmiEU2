/* revolts.c - see revolts.h. Sections refer to docs/SPEC_WAR_DIPLOMACY.md §13. */
#include <stdio.h>
#include <string.h>
#include "revolts.h"
#include "buildings.h"
#include "religion.h"
#include "script.h"
#include "calendar.h"
#include "peace.h"
#include "diplo.h"
#include <stdlib.h>

static unsigned long g_seed = 4711;
static int rnd(void) { g_seed = (g_seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((g_seed >> 16) & 0x7FFF); }
void rev_seed(unsigned long s) { g_seed = s ? s : 1; }

static int at_war_any(const World *w, int c)
{
	int i, j;
	for (i = 0; i < w->nwar; i++) {
		for (j = 0; j < w->war[i].natt; j++) if (w->war[i].att[j] == c) return 1;
		for (j = 0; j < w->war[i].ndef; j++) if (w->war[i].def[j] == c) return 1;
	}
	return 0;
}

static int is_core(const Province *p, int c) { int k; for (k = 0; k < 8; k++) if (p->cores[k] == c) return 1; return 0; }

/* revolt risk, docs revolt_religion 1 (0x481a30). Not modelled: war taxes, event modifiers other than the
 * province's own, years since acquisition (a non-core counts as nationalism 1 - NIEPEWNE) */
static int is_capital(const World *w, int p) { int c = w->prov[p].owner; return c >= 0 && w->country[c].capital == p; }

int rev_risk(const World *w, const Military *m, int p)
{
	const Province *pv = &w->prov[p];
	const Country *C;
	int c = pv->controller, t, ri, si, same, N;
	if (pv->owner < 0 || pv->population < 1000 || c < 0 || c == w->reb) return 0;
	C = &w->country[c];
	ri = world_religion_index(w, pv->religion); si = world_religion_index(w, C->religion);
	same = ri == si || ((ri == 2 || ri == 3) && (si == 2 || si == 3));
	t = (10 - (ri >= 0 ? C->tol[ri] : 0)) / 10;                    /* trunc(1 - tolerance) */
	if (same) { if (t > 0) t *= 2; }
	else if (C->defender) t += 2;
	if (t < 0) t /= 2;
	if (!world_culture_ok(w, c, pv->culture)) { t++; if (at_war_any(w, c)) t++; }
	t -= C->stability + 2;
	if (pv->manu) t -= 1;
	if (is_capital(w, p)) t -= 2;
	if (pv->owner != pv->controller) t++;
	if (m) {
		short nb[32];
		int n = mil_adjacent(m, p, nb, 32), k;
		for (k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < w->nprov && w->prov[nb[k]].controller == w->reb && w->reb >= 0) { t += 2; break; }
	}
	{	/* war exhaustion 0x481920: trunc(WE) x m (core 1, else culture/religion: both ours 1.25, one differs 1.5,
		 * both 2), floored at 0; t = trunc(t + that) */
		int k, core = 0, cul = world_culture_ok(w, c, pv->culture);
		long m = 2000, x;
		(void)k; core = is_core(pv, c);
		if (core) m = 1000; else if (cul && same) m = 1250; else if (cul || same) m = 1500;
		x = (C->we_m / 1000) * m;
		if (x > 0) t = (int)((t * 1000L + x) / 1000);
	}
	if (C->bankrupt_day > 0 && cal_day(w->now.year, w->now.month, w->now.day) - C->bankrupt_day < 360) t += 1;   /* RRISK_BANKRUPTCY */
	if (C->war_tax > 0) t += 1;                              /* RRISK_WARTAXES (0x481cb8) */
	if (C->ev_rr && cal_day(w->now.year, w->now.month, w->now.day) < C->ev_rr_until) t += C->ev_rr;   /* an event's country revolt risk (code 0x0b) */
	if (pv->bflags & PB_TAXCOL) t += 3;                     /* bailiff */
	if (pv->bflags & PB_JUDGE) t -= 1;                      /* courthouse */
	t += (int)(pv->revoltrisk_m / 1000);                     /* the province's event modifier */
	{	int k, core = 0;
		for (k = 0; k < 8; k++) if (pv->cores[k] == pv->owner) core = 1;
		/* not a core and not pagan: by years since the owner got it (P+0x3be): < 10 -> 3, < 20 -> 2, < 30 -> 1
		 * (a province held since before the game counts as long held) */
		if (core || ri == 7) N = 0;
		else {
			int A = pv->acq_year > 0 ? w->now.year - pv->acq_year : 99;
			N = A < 10 ? 3 : A < 20 ? 2 : A < 30 ? 1 : 0;
		}
	}
	t += N;
	return t < N ? N : t;
}

/* §13.2 + §13.3: one roll per city a month; a revolt raises a rebel army in the province */
static void track_owners(World *w)                       /* P+0x3be: the year a province changed hands */
{
	int i;
	for (i = 1; i < w->nprov; i++) {
		Province *p = &w->prov[i];
		if (p->owner != p->last_owner) { if (p->owner >= 0) p->acq_year = w->now.year; p->last_owner = p->owner; }
	}
}

/* 0x461780: the rebel army of province p: S = 2 stability + rand % 4 + base tax / 2 + 4, x 500 men (A = B = C = 1,
 * NIEPEWNE); the Dutch revolt (SPEC_SMALLRULES 2.3 J) in the Low Countries after Calvin while Holland does not
 * exist and the risk is over 10: infantry >= 5000 fort + 1000, cavalry a third, artillery (fort + 2) x 5 */
static int spawn_rebels(World *w, Military *m, int p)
{
	Province *pv = &w->prov[p];
	const Country *C;
	long men, S, cav = 0, art = 0;
	int k;
	LandUnit *u;
	if (w->reb < 0 || pv->owner < 0) return 0;
	C = &w->country[pv->controller >= 0 ? pv->controller : pv->owner];
	S = 2L * C->stability + rnd() % 4 + pv->income_m / 2000 + 4;
	men = S * 500;
	if (men < 100) men = 100;
	if (pv->population < 5000 && men > pv->population) men = pv->population;
	if (pv->population >= 5000 && men < 5000L * pv->fort + 1000) men = 5000L * pv->fort + 1000;
	if ((p >= 337 && p <= 340) || (p >= 377 && p <= 380)) {
		int hol = world_country(w, "HOL");
		if (world_flag(w, 2) && !(hol >= 0 && w->country[hol].nprov_owned > 0) && rev_risk(w, m, p) > 10) {
			if (men < 5000L * pv->fort + 1000) men = 5000L * pv->fort + 1000;
			cav = men / 3; art = (pv->fort + 2) * 5L;
		}
	}
	for (k = 0; k < w->nland && w->land[k].dead != 1; k++) ;          /* a destroyed unit's slot first */
	if (k == w->nland) { if (w->nland >= MAX_LAND) return 0; w->nland++; }
	u = &w->land[k];
	memset(u, 0, sizeof *u);
	u->id = 990000L + k; u->name = "Rebels"; u->owner = w->reb; u->location = (short)p; u->infantry = men; u->cavalry = cav; u->artillery = art;
	u->morale_m = 2000;
	world_note(w, NOTE_REVOLT, pv->owner, p, -1);
	return 1;
}

int rev_spawn(World *w, Military *m, int p) { return p > 0 && p < w->nprov && w->prov[p].terrain < 5 ? spawn_rebels(w, m, p) : 0; }

/* the revolt roll 0x460450 over the provinces of country `only` (-1 = all), risk + bonus */
static int rev_roll(World *w, Military *m, int only, int bonus)
{
	int p, n = 0;
	if (w->reb < 0 || w->tutorial) return 0;                  /* 0x460478: no revolts in a tutorial */
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		int r, k;
		long men;
		LandUnit *u;
		if (pv->owner < 0 || pv->controller == w->reb || pv->terrain >= 5) continue;
		{	/* 0x460450: rand % 1500 (human) / % 400 (AI) < risk, in countries of more than one province */
			int own = pv->controller >= 0 ? pv->controller : pv->owner;
			if (only >= 0 && own != only) continue;
			if (w->country[own].nprov_owned <= 1) continue;
			r = rev_risk(w, m, p);
			if (r > 0) r += bonus;
			if (r <= 0 || rnd() % (w->country[own].human ? 1500 : 400) >= r) continue;
		}
		(void)men; (void)u; (void)k;
		if (!spawn_rebels(w, m, p)) break;
		n++;
	}
	return n;
}

static int has_real_city(const Province *pv) { return pv->population >= 1000; }   /* a city that is not a colony */

/* COUNTRY_BROKEN (0x460aa0 / 0x4550a0, SPEC_SMALLRULES 4.2): more owned provinces held by rebels than
 * controlled real cities -> the government falls */
static void broken(World *w, Military *m, Peace *pc, int c, long today)
{
	Country *C = &w->country[c];
	int p, i;
	if (pc) peace_leave_all(pc, c, today);
	C->m_adm = C->m_dip = C->m_mil = 3;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		if (pv->owner == c) {
			if (pv->controller == w->reb || p == C->capital) pv->controller = (short)c;
			else if (pv->controller >= 0 && pv->controller != c && pv->controller != w->reb) {   /* ceded to the occupier */
				int o = pv->controller, k, core = 0;
				for (k = 0; k < 8; k++) if (pv->cores[k] == o) core = 1;
				pv->owner = (short)o; C->nprov_owned--; w->country[o].nprov_owned++; w->owner_epoch++;
				if (!core) w->country[o].badboy_m += 2000;
			}
		} else if (pv->controller == c && pv->owner >= 0) pv->controller = pv->owner;   /* step 5 */
	}
	for (i = 0; i < w->nland; i++) {                          /* rebel armies in c's provinces are disbanded */
		LandUnit *u = &w->land[i];
		if (!u->dead && u->owner == w->reb && u->location > 0 && u->location < w->nprov && w->prov[u->location].owner == c) u->dead = 1;
	}
	(void)m;
	C->defender = 0;
	world_vp(w, c, VP_DIP, -100000L);
	C->stability = 3;
	if (C->human) world_note(w, NOTE_BROKEN, c, -1, -1);
	else world_news(w, NOTE_BROKEN, c, -1, -1);
}

static Peace *g_peace;
static Diplo *g_dipl;
void rev_set_peace(Peace *p) { g_peace = p; }
void rev_set_diplo(Diplo *d) { g_dipl = d; }

/* ------------------------------------------------ revolt.txt (SPEC_REVOLTERS 0) */
#define RV_MAXP 4096
typedef struct {
	unsigned char have, revolt, group;   /* group: latin 0, orthodox 1, muslim 2, china 3, exotic 4 (255 none) */
	short no, capital, culture;           /* no: a tag that must not exist (-1 none); culture: interned name, -1 = the capital's */
	signed char religion;                 /* 0..10, -1 = the capital's */
	long date, expiry;
	int min0, nmin, ext0, next;           /* ranges in g_rv_prov */
} RevRec;
static RevRec g_rv[MAX_COUNTRY];
static short g_rv_prov[RV_MAXP]; static int g_rv_np;
static short g_rv_first[MAX_PROV], g_rv_link[RV_MAXP], g_rv_tagof[RV_MAXP];   /* per province: the tags whose minimum holds it, file order */
static const World *g_rv_w;

static long rv_date(const SNode *b)
{
	static const char *mn[12] = { "january", "february", "march", "april", "may", "june", "july", "august",
	                              "september", "october", "november", "december" };
	const char *m;
	int k, mo = 1;
	if (!b) return 0;
	m = script_str(b, "month", "january");
	for (k = 0; k < 12; k++) if (script_ieq(m, mn[k])) mo = k + 1;
	return cal_day((int)script_int(b, "year", 0), mo, (int)script_int(b, "day", 1));
}

static int rv_list(const SNode *l)
{
	const SNode *q;
	int n = 0;
	for (q = l ? l->child : NULL; q; q = q->next) if (!q->key && q->val && g_rv_np < RV_MAXP) { g_rv_prov[g_rv_np++] = (short)atoi(q->val); n++; }
	return n;
}

int rev_load(World *w, Pak *t)
{
	static const char *rn[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite", "pagan", "confucianism", "buddhism", "hinduism" };
	static const char *gn[5] = { "latin", "orthodox", "muslim", "china", "exotic" };
	unsigned long sz;
	char *buf, err[80];
	Arena *a;
	SNode *root, *n;
	int p, k, last[MAX_PROV];
	memset(g_rv, 0, sizeof g_rv); g_rv_np = 0; g_rv_w = w;
	for (p = 0; p < MAX_PROV; p++) { g_rv_first[p] = -1; last[p] = -1; }
	{	long start = cal_day(w->now.year, w->now.month, w->now.day);   /* control "since" the start where a save does not say */
		for (p = 1; p < w->nprov; p++) { if (w->prov[p].ctl_day == 0) w->prov[p].ctl_day = start; if (w->prov[p].last_ctl == -32768) w->prov[p].last_ctl = w->prov[p].controller; }
	}
	if (!(buf = (char *)pak_load(t, "db/revolt.txt", &sz))) return -1;
	a = arena_new(65536);
	if (!(root = script_parse(buf, sz, a, err, sizeof err))) { arena_free(a); free(buf); return -1; }
	for (n = root->child; n; n = n->next) {
		int c, i;
		RevRec *r;
		const char *s;
		if (!n->key || n->val || (c = world_country(w, n->key)) < 0) continue;
		r = &g_rv[c];
		r->have = 1; r->revolt = (unsigned char)(script_ieq(script_str(n, "revolt", "yes"), "no") ? 0 : 1);
		s = script_str(n, "no", NULL); r->no = (short)(s ? world_country(w, s) : -1);
		r->date = rv_date(script_get(n, "date")); r->expiry = rv_date(script_get(n, "expirydate"));
		r->min0 = g_rv_np; r->nmin = rv_list(script_get(n, "minimum"));
		r->ext0 = g_rv_np; r->next = rv_list(script_get(n, "extra"));
		r->capital = (short)script_int(n, "capital", 0);
		s = script_str(n, "group", ""); r->group = 255; for (i = 0; i < 5; i++) if (script_ieq(s, gn[i])) r->group = (unsigned char)i;
		s = script_str(n, "culture", NULL); r->culture = s ? world_intern(w, s) : -1;
		s = script_str(n, "religion", NULL); r->religion = -1; if (s) for (i = 0; i < 11; i++) if (script_ieq(s, rn[i])) r->religion = (signed char)i;
		for (k = 0; k < r->nmin; k++) {                        /* the reverse index: tags whose minimum holds the province */
			int q = g_rv_prov[r->min0 + k], e = r->min0 + k;
			if (q <= 0 || q >= MAX_PROV) continue;
			g_rv_tagof[e] = (short)c; g_rv_link[e] = -1;
			if (last[q] < 0) g_rv_first[q] = (short)e; else g_rv_link[last[q]] = (short)e;
			last[q] = e;
		}
	}
	arena_free(a); free(buf);
	return 0;
}

void rev_swap(int a, int b)                               /* after world_swap_identity: the records follow the tags */
{
	RevRec t;
	int i;
	if (a < 0 || b < 0 || a >= MAX_COUNTRY || b >= MAX_COUNTRY) return;
	t = g_rv[a]; g_rv[a] = g_rv[b]; g_rv[b] = t;
	for (i = 0; i < g_rv_np; i++) g_rv_tagof[i] = (short)(g_rv_tagof[i] == a ? b : g_rv_tagof[i] == b ? a : g_rv_tagof[i]);
	for (i = 0; i < MAX_COUNTRY; i++) if (g_rv[i].no == a) g_rv[i].no = (short)b; else if (g_rv[i].no == b) g_rv[i].no = (short)a;
}

static int rv_in(const RevRec *r, int p, int extra)
{
	int k, o = extra ? r->ext0 : r->min0, n = extra ? r->next : r->nmin;
	for (k = 0; k < n; k++) if (g_rv_prov[o + k] == p) return 1;
	return 0;
}

void rev_day(World *w, long today)
{
	int p;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		if (pv->controller != pv->last_ctl) { pv->ctl_day = today; pv->last_ctl = pv->controller; }
		if (pv->owner >= 0 && pv->owner < MAX_COUNTRY) WORLD_DISCOVER(w, pv->owner, p);   /* an owner knows its provinces (as a load does) */
	}
}

static int exists(const World *w, int c) { return c >= 0 && c < w->ncountry && w->country[c].nprov_owned > 0; }

/* 0x481010 + 0x8d: p goes to x for owner and controller */
static void give(World *w, int p, int x, long today)
{
	Province *pv = &w->prov[p];
	if (pv->owner >= 0 && pv->owner != x) w->country[pv->owner].nprov_owned--;
	if (pv->owner != x) { w->country[x].nprov_owned++; w->owner_epoch++; }
	pv->owner = pv->controller = (short)x; pv->last_ctl = (short)x; pv->ctl_day = today;
	pv->acq_year = 0;                                         /* P+0x3bc cleared */
	WORLD_DISCOVER(w, x, p);                                  /* an owner knows its province (a load would add it anyway) */
}

/* the rebel units in p: land armies lose 75 % of their infantry, then all change to x (0x36 + 0xd5) */
static void rebels_join(World *w, int p, int x)
{
	int i;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		if (u->dead || u->owner != w->reb || u->location != p) continue;
		u->infantry -= u->infantry * 3 / 4;                   /* NIEPEWNE: the rounding of 0x5bc2a6 */
		u->owner = (short)x; u->npath = 0;
		if (u->infantry + u->cavalry + u->artillery <= 0) u->dead = 1;
	}
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == w->reb && w->naval[i].location == p) w->naval[i].owner = (short)x;
}

static int rel_compatible(int a, int b) { if (a == 2) a = 3; if (b == 2) b = 3; return a == b; }   /* 0x431270 (checked: equal, or both counter-reformed / catholic) */

/* 0x461220 / 0x4614b0: a province held by the rebels for 4 years defects to a neighbour (or a revolter tag) */
static void defect(World *w, Military *m, int c, int p, long today)
{
	Province *pv = &w->prov[p];
	short nb[32];
	int n, k, best = -1, x = -1, pr = rel_index(w, pv->religion);
	long bs = 0;
	if ((today - pv->ctl_day) / 360 < 4 || pv->capital || (m && mil_besieger(m, p) >= 0)) return;
	n = m ? mil_adjacent(m, p, nb, 32) : 0;
	for (k = 0; k < n; k++) {
		const Province *q;
		int X, qr, xr, e;
		long s;
		if (nb[k] <= 0 || nb[k] >= w->nprov) continue;
		q = &w->prov[nb[k]]; X = q->owner;
		if (X < 0 || X == c || X == w->reb || mil_at_war(w, X, c)) continue;
		if (!WORLD_KNOWN(w, X, p)) continue;                     /* the neighbour must know the province (every country's map is saved now) */
		s = world_culture_ok(w, X, q->culture) ? 35 : 10;
		qr = rel_index(w, q->religion); xr = rel_index(w, w->country[X].religion);
		if (qr >= 0 && xr >= 0 && rel_compatible(xr, qr)) s += 30;
		else if (qr >= 0) s = s + w->country[X].tol[qr] / 10;
		if (g_rv[X].have && rv_in(&g_rv[X], p, 0)) s += 30;
		if (g_rv[X].have && rv_in(&g_rv[X], p, 1)) s += 20;
		if (q->controller == w->reb) s = -1;
		e = q->controller >= 0 ? rel_index(w, w->country[q->controller].religion) : -1;
		if (pr != 7 && e == 7) s = -1;
		if (s > bs) { bs = s; best = X; }
	}
	if (best >= 0) x = best;
	else for (k = g_rv_first[p]; k >= 0; k = g_rv_link[k]) {   /* the fallback: the revolter tags of the province */
		int X = g_rv_tagof[k];
		if (!exists(w, X) || X == c || mil_at_war(w, X, c)) continue;
		if (w->country[X].human && !WORLD_KNOWN(w, X, p)) continue;
		if (pr == 7 || rel_index(w, w->country[X].religion) != 7) x = X;
		break;
	}
	if (x < 0) return;
	give(w, p, x, today);
	world_give_cb(w, c, x, today + 5 * 360);                 /* GIVECB 5 years */
	rebels_join(w, p, x);
	if (w->country[c].human) world_note(w, NOTE_DEFECT_US, c, p, x);
	else if (w->country[x].human) world_note(w, NOTE_DEFECT_THEM, x, p, c);
	else world_news(w, NOTE_DEFECT, x, p, c);
}

/* 0x460ae0 + CREATE_COUNTRY 0x452de0: a revolter country x from the rebel-held provinces of c */
static void form(World *w, Military *m, int c, int x, int force, long today)
{
	const RevRec *r = &g_rv[x];
	Country *N = &w->country[x], *C = &w->country[c];
	short L[96];
	int nl = 0, k, cap = -1, i;
	for (k = 0; k < r->nmin + r->next && nl < 96; k++) {
		int p = g_rv_prov[r->min0 + k];                       /* min0 + nmin == ext0: both lists in a row */
		if (p > 0 && p < w->nprov && w->prov[p].controller == w->reb && w->prov[p].owner >= 0) L[nl++] = (short)p;
	}
	if (!nl) return;
	for (k = 0; k < nl; k++) { give(w, L[k], x, today); rebels_join(w, L[k], x); }
	N->treasury_m = 0; N->badboy_m = 0; N->we_m = 0; N->manpower_m = 10000; N->war_tax = 0; N->defender = 0;
	N->stability = 0; N->inflation_u = 0;                       /* NIEZNANE: the ctor's stability */
	N->land_tech = C->land_tech; N->infra_tech = C->infra_tech; N->trade_tech = C->trade_tech; N->naval_tech = C->naval_tech;
	if (r->group < 5) { static const char *gn[5] = { "latin", "orthodox", "muslim", "china", "exotic" }; N->techgroup = world_intern(w, gn[r->group]); }
	for (k = 0; k < r->nmin + r->next; k++) { int p = g_rv_prov[r->min0 + k]; if (p > 0 && p < w->nprov) world_add_core(w, p, (short)x); }
	for (i = 0; i < (int)sizeof w->known[0]; i++) w->known[x][i] |= w->known[c][i];   /* the old owner's map */
	if (r->capital > 0 && r->capital < w->nprov && w->prov[r->capital].owner == x && w->prov[r->capital].population > 0) cap = r->capital;
	else for (k = 0; k < nl; k++) if (cap < 0 || w->prov[L[k]].population > w->prov[cap].population) cap = L[k];
	if (N->capital > 0 && N->capital < w->nprov) w->prov[N->capital].capital = 0;
	N->capital = (short)cap; w->prov[cap].capital = 1;
	N->culture = r->culture >= 0 ? r->culture : w->prov[cap].culture;
	{ short rn = r->religion >= 0 ? rel_name(w, r->religion) : w->prov[cap].religion; if (rn >= 0) N->religion = rn; }
	world_default_tol(w, x);
	if (!(r->religion >= 0 ? r->religion == 7 : rel_index(w, N->religion) == 7)) { C->badboy_m -= 1000L * nl; if (C->badboy_m < 0) C->badboy_m = 0; }
	world_news(w, NOTE_NEWNATION, x, cap, c);
	if (!force) {
		N->diplomats_m += 1000;
		world_give_cb(w, x, c, today + 5 * 360);              /* GIVECB 5 years */
		if (g_dipl) diplo_declare_war(g_dipl, x, c, today);
	}
	(void)m;
}

/* 0x460ae0: at most one revolter a month (force: COUNTRY_BROKEN, no delay and no roll); the
 * rebel-held provinces that formed nothing may defect */
static int form_revolter(World *w, Military *m, int c, int force, long today)
{
	int p;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		int k, x;
		if (pv->owner != c || pv->controller != w->reb) continue;
		for (k = g_rv_first[p]; k >= 0; k = g_rv_link[k]) {
			const RevRec *r;
			x = g_rv_tagof[k]; r = &g_rv[x];
			if (exists(w, x) || !r->revolt || (r->no >= 0 && exists(w, r->no))) continue;
			if (!(today > r->date && today < r->expiry)) continue;
			if (x == world_country(w, "ROY")) { int e = world_country(w, "ENG"); if (exists(w, e) && rel_index(w, w->country[e].religion) != 1) continue; }
			if (x == world_country(w, "FPR") && !world_flag(w, 2)) continue;
			if (x == world_country(w, "SPR") && !world_flag(w, 1)) continue;
			if (!force) {                                     /* 3 years + m months after the rebels took it, then 50 % */
				int s1 = w->country[c].policy[1], mm = (int)((s1 - 5) * 6 / 5);   /* trunc(1.2 (s1 - 5)); s1 = centralisation (slider order confirmed, docs/SPEC_DOMESTIC.md 5) */
				if (!(today > pv->ctl_day + 3L * 360 + 30L * mm)) continue;
				if (rnd() % 100 >= 50) continue;
			}
			form(w, m, c, x, force, today);
			return 1;
		}
		if (!force) defect(w, m, c, p, today);
	}
	return 0;
}

/* event `independence` (code 0x4b): TAG is released from c with the provinces of c that are TAG's cores
 * (NIEPEWNE: which provinces the exe hands over); 1 done */
int rev_release(World *w, int x, int c)
{
	long today = cal_day(w->now.year, w->now.month, w->now.day);
	int p, n = 0, cap = -1, k;
	if (x < 0 || x >= w->ncountry || w->country[x].nprov_owned > 0) return 0;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		if (pv->owner != c || p == w->country[c].capital) continue;
		for (k = 0; k < 8; k++) if (pv->cores[k] == x) break;
		if (k == 8) continue;
		give(w, p, x, today); n++;
		if (cap < 0 || pv->population > w->prov[cap].population) cap = p;
	}
	if (!n) return 0;
	{	Country *N = &w->country[x];
		if (N->capital <= 0 || N->capital >= w->nprov || w->prov[N->capital].owner != x) N->capital = (short)cap;
		w->prov[N->capital].capital = 1;
		if (N->culture < 0) N->culture = w->prov[N->capital].culture;
		if (N->religion < 0) N->religion = w->prov[N->capital].religion;
		N->land_tech = w->country[c].land_tech; N->naval_tech = w->country[c].naval_tech; N->infra_tech = w->country[c].infra_tech; N->trade_tech = w->country[c].trade_tech;
		world_default_tol(w, x);
		world_news(w, NOTE_NEWNATION, x, N->capital, c);
	}
	return 1;
}

/* SPEC_NATIVES 2 / 2.2 / 3: growth by the style (+1 Americas, +3, +5 Asia) up to the cap; natives join a settlement
 * of more than 900; the uprising roll in provinces with natives, an owner and a settlement or trading post */
static void natives_month(World *w, Military *m)
{
	int p;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		if (pv->natives > 0 && pv->natives < pv->maxnatives) {
			int s = mil_native_style(w, p);
			pv->natives += s <= 2 ? 1 : s <= 4 || s == 9 ? 3 : 5;
			if (pv->natives > pv->maxnatives) pv->natives = pv->maxnatives;
		}
		if (pv->natives > 0 && pv->population > 900) { pv->population += 100L * pv->natives; pv->natives = 0; }
		if (pv->natives > 0 && pv->owner >= 0 && (pv->population > 0 || pv->tp)) {
			int r = pv->tolerance - (pv->population > 0 ? 2 : 4);
			if (r > 0) r /= 2;
			r += pv->ferocity / 10;
			if (r > 0) r = r * r;
			if (pv->tp && pv->population <= 0) { r -= 10; if (r > 5) r = 5; }
			if (rnd() % 1000 < r && m) mil_native_attack(m, p);
		}
	}
}

int rev_month(World *w, Military *m)
{
	int n, c, p;
	long today = cal_day(w->now.year, w->now.month, w->now.day);
	natives_month(w, m);
	track_owners(w);
	n = rev_roll(w, m, -1, 0);
	if (w->reb < 0) return n;
	for (c = 0; c < w->ncountry; c++) {
		int r = 0, k = 0;
		if (c == w->reb || w->country[c].nprov_owned <= 0) continue;
		if (g_rv_w == w) form_revolter(w, m, c, 0, today);
		for (p = 1; p < w->nprov; p++) {
			const Province *pv = &w->prov[p];
			if (pv->owner == c && pv->controller == w->reb) r++;
			if (pv->controller == c && has_real_city(pv)) k++;
		}
		if (r > k) {
			if (g_rv_w == w) while (form_revolter(w, m, c, 1, today)) ;
			if (w->country[c].nprov_owned > 0) broken(w, m, g_peace, c, today);
		}
	}
	return n;
}

int rev_roll_country(World *w, Military *m, int c, int bonus) { track_owners(w); return rev_roll(w, m, c, bonus); }

void rev_save(FILE *f) { fprintf(f, "amieu2_revolts = { seed = %lu }\n", g_seed); }
void rev_restore(const SNode *root) { const SNode *b = script_get(root, "amieu2_revolts"); if (b) g_seed = (unsigned long)script_int(b, "seed", 4711); }
