/* economy.c - see economy.h. Section numbers refer to docs/SPEC_TIME_ECONOMY.md. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "economy.h"
#include "buildings.h"
#include "script.h"
#include "calendar.h"
static const World *g_rw;                  /* for the world flags of rel_norm (set by econ_load) */

#define NREL 11
#define MAXLEV 64
enum { R_LAND, R_NAVAL, R_INFRA, R_TRADE };
/* budget slider order of §6.3 */
enum { B_NAVAL, B_LAND, B_TRADE, B_INFRA, B_STAB, B_TREAS, B_N };

typedef struct {
	long progress[4];          /* research progress, ducats x1000 */
	long stab_progress;
	short slider[B_N];         /* budget fractions x1000, sum 1000 */
	long manpower_rate_m;      /* yearly, thousands x1000 */
	long last_income_m;
	long grain_tr_m;           /* +0xf72 (= +0xf5a[grain]): sum of city factor x 0.05 x merchants over the grain provinces of our CoT shares, this month */
	EconReport rep;
	long ytd_tax, ytd_prod, ytd_gold, ytd_tolls;   /* year to date, for gold inflation */
	short nloans;
	long loan_m[5], loan_due[5];         /* bank loans: amount (ducats x1000), due day */
	short loan_rate[5];                  /* each loan's interest in percent a year (loan object +0x16), fixed when taken */
	long ext_count;                      /* country+0xebc: loans extended (each +3 % on the next rate, stability -1) */
	long boost_until[5];                 /* one-time increase per budget row: next allowed day (track+4, SPEC_BOOST) */
	long bankruptcies;
	short maint_cut[2];        /* land / naval maintenance level below 100 %, x1000 (0..500; iw_tech slider 50..100 %) */
	unsigned char lock;        /* budget rows locked by the player (bit = ECON_SL_*; the window's knobs +0x86, saved as interface.treasury) */
} CEcon;

struct Economy {
	short europe;                        /* the continent name id "Europe" (continent 2 of the exe) */
	EconRiskFn riskfn; void *risku;      /* the province revolt risk (0x481a30, revolts.c) for the tax modifier; NULL: 0 */
	World *w;
	/* religion.csv columns /100 as x1000 (i.e. value x10) */
	long rel_techspeed[NREL], rel_stab[NREL], rel_prod[NREL], rel_trade[NREL], rel_tax[NREL], rel_col[NREL], rel_dip[NREL], rel_miss[NREL];
	short rel_of_name[MAX_NAMES];        /* interned name -> religion index (-1 unknown) */
	long stab_mod[7];                    /* tax_stab.csv, x1000, index stability+3 */
	long goods_value[MAX_NAMES];         /* interned goods name -> goods.csv base value */
	long price_m[MAX_NAMES];             /* market price x1000 (9.1): base x (2 - supply) x demand, monthly */
	signed char gid[MAX_NAMES];          /* goods name -> the market id 0..20 (+4, 0x41b210; SPEC_MARKET 1) */
	unsigned char is_goods[MAX_NAMES];   /* a row of goods.csv */
	int price_ok;
	int gold_goods;                      /* interned id of "gold" */
	int grain_goods;                     /* of "grain" (the support limit) */
	long cost[4][MAXLEV];                /* base cost per level (ducats), 0 = unreachable */
	short year[4][MAXLEV];               /* the csv year of each row */
	int nlev[4];
	long infra_eff[MAXLEV], trade_eff[MAXLEV];   /* percent */
	CEcon *c;
	short prov_cot[MAX_PROV];            /* CoT index each province trades in, -1 none */
	long trade_acc[MAX_COUNTRY];         /* this month's trade income, ducats x1000 */
	short first_prov[MAX_COUNTRY];       /* owned provinces as linked lists, rebuilt each month */
	short next_prov[MAX_PROV];
};

/* --------------------------------------------------------------- tables */
static void assign_cots(Economy *e);
static void cot_event(void *u, int op, int p);
static long blockade_share_m(const Economy *e, int c);
static int has_port(const World *w, int p);
static long overseas_m(const Economy *e, int c);
static char *load(Pak *t, const char *n) { unsigned long sz; return (char *)pak_load(t, n, &sz); }

static int religion_index(const char *s)
{
	static const char *names[NREL] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni",
	                                   "shiite", "pagan", "confucianism", "buddhism", "hinduism" };
	int i;
	if (!s) return -1;
	for (i = 0; i < NREL; i++) if (script_ieq(s, names[i])) return i;
	if (script_ieq(s, "counterreformed")) return 2;
	if (script_ieq(s, "exotic")) return 7;          /* province.csv uses exotic for natives: pagan (NIEPEWNE) */
	return -1;
}

/* level costs (§6.4): baseCost_k = ((Y_k - 1400) * 137 div 300 + 275) * (Y_k - Y_k-1), first previous year 1418 */
static void load_levels(Economy *e, Pak *t, const char *name, int track, long *eff)
{
	char *buf = load(t, name), *cur, *line, *f[16];
	long prev = 1418;
	int first = 1, k = 0;
	if (!buf) return;
	cur = buf;
	while ((line = csv_line(&cur)) != NULL && k < MAXLEV - 1) {
		long y;
		int n = csv_split(line, f, 16);
		if (first) { first = 0; continue; }
		if (n < 2 || f[0][0] < '0' || f[0][0] > '9') continue;
		y = parse_int(f[1]);
		e->cost[track][k] = (y >= 9999 || y < prev) ? 0 : ((y - 1400) * 137 / 300 + 275) * (y - prev);
		e->year[track][k] = (short)y;
		if (eff && n > 2) eff[k] = parse_int(f[2]);
		prev = y;
		k++;
	}
	e->nlev[track] = k;
	free(buf);
}


Economy *econ_load(World *w, Pak *t, char *err, int errlen)
{
	Economy *e = calloc(1, sizeof *e);
	char *buf, *cur, *line, *f[16];
	int i, first;
	if (!e) { snprintf(err, errlen, "out of memory"); return NULL; }
	e->w = w; g_rw = w;
	e->europe = -1;
	for (i = 0; i < w->nnames; i++) if (script_ieq(world_name(w, (short)i), "europe")) e->europe = (short)i;
	e->c = calloc(MAX_COUNTRY, sizeof *e->c);
	for (i = 0; i < MAX_NAMES; i++) e->rel_of_name[i] = -1;
	for (i = 0; i < 7; i++) e->stab_mod[i] = 1000;
	if ((buf = load(t, "db/religion.csv")) != NULL) {
		int r = 0;
		cur = buf; first = 1;
		while ((line = csv_line(&cur)) != NULL && r < NREL) {
			int n = csv_split(line, f, 16);
			if (first) { first = 0; continue; }
			if (n < 10 || !f[0][0] || script_ieq(f[0], "END")) continue;
			e->rel_techspeed[r] = parse_int(f[1]) * 10; e->rel_stab[r] = parse_int(f[2]) * 10;
			e->rel_prod[r] = parse_int(f[3]) * 10; e->rel_trade[r] = parse_int(f[4]) * 10; e->rel_tax[r] = parse_int(f[5]) * 10;
			e->rel_col[r] = parse_int(f[7]) * 10; e->rel_dip[r] = parse_int(f[8]) * 10; e->rel_miss[r] = parse_int(f[9]) * 10;
			r++;
		}
		free(buf);
	}
	if ((buf = load(t, "db/tax_stab.csv")) != NULL) {
		cur = buf; first = 1;
		while ((line = csv_line(&cur)) != NULL) {
			int n = csv_split(line, f, 4);
			long s;
			if (first) { first = 0; continue; }
			if (n < 2) continue;
			s = parse_int(f[0]);
			if (s >= -3 && s <= 3) e->stab_mod[s + 3] = parse_int(f[1]) * 10 + 1000;
		}
		free(buf);
	}
	if ((buf = load(t, "db/goods.csv")) != NULL) {
		cur = buf; first = 1;
		while ((line = csv_line(&cur)) != NULL) {
			int n = csv_split(line, f, 6);
			short id;
			if (first) { first = 0; continue; }
			if (n < 2 || !f[0][0]) continue;
			id = world_intern(w, f[0]);
			if (id >= 0) {
				static const char *const code[21] = { "NOTHING", "CLO", "GOLD", "COT", "FISH", "FURS", "GRAI", "IVOR", "METAL", "MINERAL", "ORIENT",
				                                      "NAVS", "SALT", "SLAV", "SPIC", "SUG", "TOB", "WINE", "WOOL", "TEA", "COFFEE" };   /* 0x41b210: any other name is 0 */
				int k;
				e->goods_value[id] = parse_int(f[1]);
				e->gid[id] = 0;
				e->is_goods[id] = 1;
				for (k = 0; k < 21; k++) if (script_ieq(f[0], code[k])) e->gid[id] = (signed char)k;   /* NIEPEWNE: case-insensitive (province.csv mixes Clo / clo) */
			}
		}
		free(buf);
	}
	e->gold_goods = world_intern(w, "gold");
	e->grain_goods = world_intern(w, "GRAI");
	load_levels(e, t, "db/land.csv", R_LAND, NULL);
	load_levels(e, t, "db/naval.csv", R_NAVAL, NULL);
	load_levels(e, t, "db/infra.csv", R_INFRA, e->infra_eff);
	load_levels(e, t, "db/trade.csv", R_TRADE, e->trade_eff);
	for (i = 0; i < w->nnames; i++) e->rel_of_name[i] = (short)religion_index(world_name(w, (short)i));
	assign_cots(e);                                           /* (after the trade levels: the site value reads the trade efficiency) */
	w->cot_ev = cot_event; w->cot_evu = e;
	for (i = 0; i < w->ncountry; i++) {
		/* default budget split until the AI/player budget exists (NIEPEWNE: the AI sets its own) */
		short d[B_N] = { 100, 200, 150, 150, 150, 250 };
		memcpy(e->c[i].slider, d, sizeof d);
	}
	return e;
}

void econ_set_riskfn(Economy *e, EconRiskFn fn, void *user) { if (e) { e->riskfn = fn; e->risku = user; } }
void econ_free(Economy *e) { if (e) { free(e->c); free(e); } }

void econ_report(const Economy *e, int c, EconReport *r) { *r = e->c[c].rep; r->manpower_rate_m = e->c[c].manpower_rate_m; r->loans = e->c[c].nloans; r->bankruptcies = e->c[c].bankruptcies; }

/* -------------------------------------------------------------- helpers */
static int dpol(const Country *c, int i) { return c->policy[i] - 5; }        /* d_i of §3.1 */

static int religion_of(const Economy *e, short name)
{
	return (name >= 0 && name < MAX_NAMES) ? e->rel_of_name[name] : -1;
}

/* §5.1 religion comparison: counter-reformed == catholic; protestant/reformed count as catholic until
 * the Reformation flags (not tracked yet: always pre-Reformation) */
/* the effective province religion (0x47ca10, SPEC_SMALLRULES 2.3 B): protestant counts as catholic until
 * flag 1, reformed until flag 2; counter-reform and catholic compare equal (ours, NIEPEWNE) */
static int rel_norm(int r) { return (r == 2 || (r == 0 && !(g_rw && world_flag(g_rw, 1))) || (r == 1 && !(g_rw && world_flag(g_rw, 2)))) ? 3 : r; }

static int has_city(const Province *p) { return p->population > 0; }
static int is_colony(const Province *p) { return p->population < 1000; }

/* city size 0..6 (cities >= 5000) and settlement size (smaller) - §5.2 */
static int city_size(long pop)
{
	static const long lim[6] = { 5000, 10000, 20000, 40000, 80000, 200000 };
	int k;
	for (k = 0; k < 6; k++) if (pop <= lim[k]) return k;
	return 6;
}
static int settlement_size(long pop)
{
	int k;
	for (k = 0; k < 11; k++) if (pop < 200 + 100L * k) return k + 1;
	return 10;
}
static long city_mult_m(long pop)                     /* x1000 */
{
	if (pop >= 5000) return 1000 + city_size(pop) * 1000 / 6;
	return settlement_size(pop) * 100;
}

static long tech_eff(const Economy *e, const long *tab, int track, int level, int relcol_m, int policy_pp)
{
	long v;
	if (level >= e->nlev[track]) level = e->nlev[track] - 1;
	if (level < 0) level = 0;
	v = tab[level] * 10 + relcol_m + policy_pp * 10;     /* x1000 */
	return v < 10 ? 10 : v;
}

static int is_core(const Province *p, int c) { int k; for (k = 0; k < 8; k++) if (p->cores[k] == c) return 1; return 0; }

/* §6.2 the land support limit (0x459c40), men: 5000 + 1000 ftol(e80 x 0.2 x (0.1 s5 + 0.5)) + 2000 ftol(ec4)
 * + 10000 conscription centres + 5000 weapons manufactories + 2000 ftol(grain produced) + 1000 ftol(grain
 * traded); grain produced = +0xf06[grain] (the recount 0x43b1a9: the city factor of every owned grain city), traded =
 * +0xf5a[grain] (0x480ea0: city factor x 0.05 x our merchants there, per province of our CoT shares; reset monthly, NIEPEWNE) */
long econ_support_limit(const Economy *e, int c)
{
	const World *w = e->w;
	const Country *co = &w->country[c];
	long lim = 5000, inc = e->c[c].last_income_m;           /* ducats x1000 */
	int p, cc = 0, wm = 0, s5 = co->policy[5];
	long grain = 0;
	lim += 1000L * (inc / 1000 * (s5 + 5) / 50);            /* ftol(e80 x 0.2 x (0.1 s5 + 0.5)) = e80 (s5 + 5) / 50 */
	lim += 2000L * (e->c[c].manpower_rate_m / 1000);
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		if (pv->owner != c) continue;
		if (pv->bflags & PB_CONSCRIPT) cc++;
		if (pv->manu == 2) wm++;
		if (pv->goods >= 0 && pv->goods == e->grain_goods && has_city(pv)) grain += is_colony(pv) ? 500 : city_mult_m(pv->population);
	}
	return lim + 10000L * cc + 5000L * wm + 2000L * (grain / 1000) + 1000L * (e->c[c].grain_tr_m / 1000);
}

/* ------------------------------------------------------------- province */
static long yearly_tax_m(const Economy *e, const Province *p, const Country *co, int cidx)
{
	long M = co->war_tax > 0 ? 2500 : 1000, base = (p->income_m < 0 ? 0 : p->income_m) + ((p->bflags & PB_TAXCOL) ? 1000 : 0) + ((p->bflags & PB_JUDGE) ? 1000 : 0);   /* §3.2 flags 6a / 6c: base +1 */
	int sr = religion_of(e, co->religion), pr = religion_of(e, p->religion), st = co->stability;
	(void)cidx;
	if (!world_culture_ok(e->w, (int)(co - e->w->country), p->culture)) M -= 300;   /* culture not accepted */
	if (sr >= 0 && pr >= 0 && rel_norm(sr) != rel_norm(pr)) M -= 300;
	if (sr >= 0) M += e->rel_tax[sr];
	{	int k = co->capital, ci = cidx;
		if (k > 0 && k < e->w->nprov && p->lgroup != e->w->prov[k].lgroup)   /* no land connection to the capital (+0x498) */
			M -= has_port(e->w, (int)(p - e->w->prov)) ? 500 * blockade_share_m(e, ci) / 1000 + 20L * (co->policy[3] - 5) + 100 : 100;
	}
	if (st < -3) st = -3;
	if (st > 3) st = 3;
	M += e->stab_mod[st + 3] - 1000;
	if (e->riskfn) M -= 50L * e->riskfn(e->risku, (int)(p - e->w->prov));   /* - 0.05 x RR (0x481a30) */
	if (M < 0) M = 0;
	if (p->blockaded) M /= 2;                                       /* PIWINC_DELAYED5: blockaded provinces give half the tax */
	return base * M / 1000;
}


/* ------------------------------------------------------------- trade */
static long trade_eff_of(const Economy *e, const Country *co);
/* -------------------------------------------------------------- CoT lifecycle (docs/SPEC_COT.md) */
static void set_member(Economy *e, int p, int k)
{
	World *w = e->w;
	e->prov_cot[p] = (short)k;
	w->prov[p].tcot = (short)(k >= 0 ? w->cot[k].prov : 0);
}
static int cot_land_owned(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].id >= 0 && w->prov[p].terrain < 5 && w->prov[p].owner >= 0; }
static int cot_is_home(const World *w, int p) { int k; for (k = 0; k < w->ncot; k++) if (w->cot[k].prov == p) return 1; return 0; }
static long cot_dist(const World *w, int a, int b)       /* 0x43a820: map pixels / 100 (army positions, x wraps at 0x4a00) */
{
	long dx = w->prov[a].army_x - w->prov[b].army_x, dy = w->prov[a].army_y - w->prov[b].army_y, v, r = 0, bit = 1L << 30;
	if (dx < 0) dx = -dx;
	if (dx > 0x2500) dx = 0x4a00 - dx;
	v = dx * dx + dy * dy;
	while (bit > v) bit >>= 2;
	while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
	return r / 100;
}
static int in_europe(const Economy *e, int p) { return e->europe >= 0 && e->w->prov[p].continent == e->europe; }

/* §2 0x47f400, x100 */
static long cot_site(const Economy *e, int p)
{
	const World *w = e->w;
	const Province *pv = &w->prov[p];
	const Country *co;
	long s, A;
	int r;
	if (pv->terrain >= 5 || pv->owner < 0 || (pv->population <= 0 && !pv->tp)) return -100;
	co = &w->country[pv->owner];
	if (pv->tp || pv->population < 1000) A = 1;                       /* a colony or trading post */
	else if (pv->population < 5000) A = 25;
	else A = 50 + 20L * city_size(pv->population);
	if (!pv->tp && pv->population >= 1000 && pv->fort >= 3) A++;
	if (pv->kind == 1) A++;
	s = 100 * (A + (in_europe(e, p) ? 4 : 1) * pv->cot_hist);
	s += 30L * trade_eff_of(e, co) / 10;
	s += 100L * (co->naval_tech / 2);
	s += econ_trade_value_m(e, p) / 50;                              /* 0.2 x V */
	if (pv->manu) s += 1000;
	if (e->prov_cot[p] >= 0 && e->prov_cot[p] < w->ncot) s += 1000L * cot_dist(w, w->cot[e->prov_cot[p]].prov, p);
	r = religion_of(e, co->religion);
	if (r >= 0 && r <= 4) s += 5000;
	else if (r == 7) s = 0;                                          /* pagan (fantasy off) */
	if (!in_europe(e, p)) s += 10000;
	return s;
}

static int cot_pair_war(const World *w, int a, int b)
{
	int i, j, sa, sb;
	if (a == b) return 0;
	for (i = 0; i < w->nwar; i++) {
		sa = sb = 0;
		for (j = 0; j < w->war[i].natt; j++) { if (w->war[i].att[j] == a) sa = 1; if (w->war[i].att[j] == b) sb = 1; }
		for (j = 0; j < w->war[i].ndef; j++) { if (w->war[i].def[j] == a) sa = 2; if (w->war[i].def[j] == b) sb = 2; }
		if (sa && sb && sa != sb) return 1;
	}
	return 0;
}

/* §4 0x49aa0f: the CoT p trades in (S = the homes' site values, x100) */
static void cot_member(Economy *e, int p, const long *S)
{
	World *w = e->w;
	int k, Op, choice = -1;
	long best = -100000000L;
	if (!cot_land_owned(w, p) || cot_is_home(w, p)) return;
	Op = w->prov[p].owner;
	for (k = 0; k < w->ncot; k++) {
		int C = w->cot[k].prov, Oc = w->prov[C].owner, ro, rc, cap;
		long s, d;
		if (Oc < 0) continue;                                         /* NIEZNANE: a home without an owner */
		s = 2 * S[k];
		cap = w->country[Op].capital;
		if (Op == Oc && cap > 0 && cap < w->nprov && in_europe(e, cap)) s += in_europe(e, p) ? 2500 : 100000;
		s += 5L * (10 - w->country[Oc].policy[3]);
		if (Op == Oc) s += 300L * w->country[Op].policy[3];
		if (!cot_pair_war(w, Op, Oc)) s += 500;                         /* R+0x36 (NIEPEWNE: at war) */
		if (!(w->dipflag[Op][Oc] & DF_CB)) s += 200;
		if (world_overlord(w, Op) == Oc || world_overlord(w, Oc) == Op) s += 300;
		if (Op != Oc && world_allied(w, Op, Oc, ALLY_DYNASTIC)) s += 200;
		ro = religion_of(e, w->country[Op].religion); rc = religion_of(e, w->country[Oc].religion);
		if (ro == rc || ((ro == 2 || ro == 3) && (rc == 2 || rc == 3))) s += 200;
		if (ro >= 0 && ro <= 4 && rc >= 0 && rc <= 4) s += 2500;
		if (world_culture_ok(w, Oc, w->prov[p].culture)) s += 2500;
		if (Oc != Op && (w->dipflag[Oc][Op] & DF_NOTRADE)) s -= 5000;
		if (!WORLD_KNOWN(w, Op, C) && !WORLD_KNOWN(w, Oc, p)) s -= 999990L;
		s -= w->cot[k].fail / 40;                                      /* 3 x the counter (fail = 1/12000), x100 */
		d = cot_dist(w, C, p);
		if (d > 1) s /= d;
		if (s > best) { best = s; choice = k; }
	}
	set_member(e, p, choice);
}

static void cot_member_all(Economy *e)
{
	World *w = e->w;
	static long S[MAX_COT];
	int k, p;
	for (k = 0; k < w->ncot; k++) { set_member(e, w->cot[k].prov, k); S[k] = cot_site(e, w->cot[k].prov); }
	for (p = 1; p < w->nprov; p++) cot_member(e, p, S);
}

static void cot_member_one(Economy *e, int p)
{
	World *w = e->w;
	static long S[MAX_COT];
	int k;
	for (k = 0; k < w->ncot; k++) S[k] = cot_site(e, w->cot[k].prov);
	cot_member(e, p, S);
}

/* world init (0x419d80): every province; a loaded game keeps what it saved (tc) */
static void assign_cots(Economy *e)
{
	World *w = e->w;
	int i, k, loaded = 0;
	for (i = 0; i < MAX_PROV; i++) e->prov_cot[i] = -1;
	for (i = 1; i < w->nprov; i++) if (w->prov[i].tcot != -1) loaded = 1;
	if (!loaded) { for (i = 1; i < w->nprov; i++) w->prov[i].tcot = 0; cot_member_all(e); return; }
	for (i = 1; i < w->nprov; i++) {
		int h = w->prov[i].tcot;
		if (h > 0) for (k = 0; k < w->ncot; k++) if (w->cot[k].prov == h) { e->prov_cot[i] = (short)k; break; }
		if (e->prov_cot[i] < 0) w->prov[i].tcot = 0;
	}
	for (k = 0; k < w->ncot; k++) set_member(e, w->cot[k].prov, k);
}

/* §3 0x499e92: the failure counter (fail: 1/12000 of a point, so a yearly rate f adds f x 1000 / 12 / 1000 x 12000 = f x 1000) */
static void cot_month(Economy *e)
{
	World *w = e->w;
	int k, j;
	for (k = 0; k < w->ncot; k++) {
		Cot *ct = &w->cot[k];
		int t = 0, n = 0, mono = 0;
		long f = 0;
		for (j = 0; j < ct->n; j++) if (ct->level[j] > 0) { t += ct->level[j]; n++; if (ct->level[j] > 5) mono = 1; }
		if (mono) f += 1000;
		if (t > 19) f -= 500;
		if (t > 15) f -= 500;
		if (t > 12) f -= 500;
		if (t < 8) f += 1000;
		f += n > 5 ? -500 : n < 2 ? 2000 : 1150;
		ct->fail += f;
		if (ct->fail > 1320000L) ct->fail = 1320000L;
		if (ct->fail < 0) ct->fail = 0;
	}
}

static void cot_remove(Economy *e, int k)                           /* 0x49a86e + 0x4993b0: merchants lost */
{
	World *w = e->w;
	int i;
	memmove(&w->cot[k], &w->cot[k + 1], (size_t)(w->ncot - k - 1) * sizeof w->cot[0]);
	w->ncot--;
	for (i = 1; i < w->nprov; i++) {                                 /* every pointer to it goes (the original clears only the home's) */
		if (e->prov_cot[i] == k) set_member(e, i, -1);
		else if (e->prov_cot[i] > k) e->prov_cot[i]--;
	}
	w->owner_epoch++;
}

static int cot_found(Economy *e, int p)                              /* 0x49a4b2 */
{
	World *w = e->w;
	Cot *ct;
	int o = w->prov[p].owner;
	if (w->ncot >= 69 || w->ncot >= MAX_COT) return -1;
	ct = &w->cot[w->ncot];
	memset(ct, 0, sizeof *ct);
	ct->prov = (short)p;
	if (o >= 0) { ct->tag[0] = (short)o; ct->level[0] = 1; ct->n = 1; }   /* 0x444870(owner, 1) + 0xa7: one merchant placed (NIEPEWNE) */
	set_member(e, p, w->ncot);
	w->ncot++;
	w->owner_epoch++;
	return w->ncot - 1;
}

/* §5 0x499ba2 */
static void cot_year(Economy *e)
{
	World *w = e->w;
	short gone[MAX_COT];
	int ng = 0, k, i, p, own = 0, best = -1;
	long bs = 0;
	for (k = 0; k < w->ncot; k++) if (w->cot[k].fail >= 1200000L) gone[ng++] = w->cot[k].prov;
	for (i = 0; i < ng; i++) {
		for (k = 0; k < w->ncot && w->cot[k].prov != gone[i]; k++) ;
		if (k >= w->ncot) continue;
		cot_remove(e, k);
		if (gone[i] > 0 && gone[i] < w->nprov) { int h = w->prov[gone[i]].cot_hist - 35; w->prov[gone[i]].cot_hist = (signed char)(h < -128 ? -128 : h); }
	}
	if (ng) cot_member_all(e);
	for (p = 1; p < w->nprov; p++) if (cot_land_owned(w, p)) own++;
	if (own / 29 > w->ncot) {
		for (p = 1; p < w->nprov; p++) {                             /* the best city (ties: the lower id - the original's qsort is unstable) */
			const Province *pv = &w->prov[p];
			long s;
			if (!cot_land_owned(w, p) || pv->tp || pv->population < 1000 || cot_is_home(w, p)) continue;
			s = cot_site(e, p) / 100;
			if (best < 0 || s > bs) { bs = s; best = p; }
		}
		if (best > 0) cot_found(e, best);
	}
	cot_member_all(e);
}

/* events (0x454720 / 0x454780) and new settlements (0x437ba5 / 0x437ceb) */
static void cot_event(void *u, int op, int p)
{
	Economy *e = (Economy *)u;
	World *w = e->w;
	int k;
	if (p <= 0 || p >= w->nprov) return;
	if (op == 0) { if (!cot_is_home(w, p)) cot_found(e, p); cot_year(e); }
	else if (op == 1) {
		k = e->prov_cot[p];
		if (k >= 0 && k < w->ncot) { cot_remove(e, k); set_member(e, p, -1); cot_member_all(e); }
	} else if (op == 2) cot_member_one(e, p);
}

static long trade_eff_of(const Economy *e, const Country *co)
{
	int sr = religion_of(e, co->religion);
	return tech_eff(e, e->trade_eff, R_TRADE, co->trade_tech, sr >= 0 ? (int)e->rel_trade[sr] : 0, -dpol(co, 0) - dpol(co, 5));
}

void econ_goods(const Economy *e, int c, int g, EconGoods *o)
{
	const World *w = e->w;
	static long units[MAX_COUNTRY];
	long best = 0;
	int i, k;
	memset(o, 0, sizeof *o);
	o->leader = -1;
	if (g < 0 || g >= MAX_NAMES) return;
	o->gold = g == e->gold_goods;
	o->base = e->goods_value[g];
	o->price_m = e->price_ok ? e->price_m[g] : o->base * 1000;
	o->prod_eff = econ_prod_eff(e, c); o->trade_eff = econ_trade_eff(e, c);
	memset(units, 0, sizeof units);
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		long mult;
		if (p->goods != g || p->owner < 0 || !has_city(p)) continue;
		mult = is_colony(p) ? 500 : city_mult_m(p->population);
		o->nprov++;
		if (p->owner == p->controller && p->owner < MAX_COUNTRY) units[p->owner] += mult;
		if (p->owner == c && p->controller == c)              /* production: units x efficiency x price (yearly) */
			o->prod_income_m += o->gold ? p->mine_value * mult : o->price_m / 10 * (mult / 100) / 1000 * o->prod_eff;   /* x1000 a year (econ_month's monthly x 12) */
		if (e->prov_cot[i] >= 0) {                            /* trade: our merchants' part of what passes the CoT (yearly) */
			const Cot *ct = &w->cot[e->prov_cot[i]];
			long total = 0, V = mult * (o->price_m / 1000);
			for (k = 0; k < ct->n; k++) total += ct->level[k];
			for (k = 0; k < ct->n; k++) {
				long n;
				if (ct->tag[k] != c) continue;
				n = ct->level[k] == 6 ? 26 - total : ct->level[k];
				if (n <= 0) continue;
				o->trade_units += total > 0 ? mult * ct->level[k] / total : 0;
				o->trade_income_m += o->trade_eff * V / 1000 * n * 50 / 1000;   /* x1000 a year (trade_month's monthly x 12) */
			}
		}
	}
	o->units = units[c];
	for (i = 0; i < w->ncountry && i < MAX_COUNTRY; i++) if (units[i] > best) { best = units[i]; o->leader = i; }
}

/* §5.3: every producing province pays its CoT's merchants: income = tradeEff x V x n x 0.05 */
/* the province's monthly trade value x1000 (0x480e30): F x the market price / 12, less half a ducat per pirate warship
 * off the coast (SPEC_PIRATES 4.2), at least 0 */
long econ_trade_value_m(const Economy *e, int i)
{
	const World *w = e->w;
	const Province *p;
	long F, V;
	if (i <= 0 || i >= w->nprov) return 0;
	p = &w->prov[i];
	if (p->owner < 0 || !has_city(p) || e->prov_cot[i] < 0 || p->goods < 0) return 0;
	F = is_colony(p) ? 500 : city_mult_m(p->population);        /* colony: size x 0.1 + 0.4 (size unknown: 1) */
	V = F * e->price_m[p->goods] / 12000 - 500L * p->pirates;    /* x1000 ducats per month at the market price (9.1) */
	return V < 0 ? 0 : V;
}

static void trade_month(Economy *e)
{
	World *w = e->w;
	int i, k;
	long eff[MAX_COUNTRY];
	for (i = 0; i < w->ncountry; i++) { e->trade_acc[i] = 0; e->c[i].grain_tr_m = 0; eff[i] = trade_eff_of(e, &w->country[i]); }
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		const Cot *ct;
		long V, total = 0;
		if (p->owner < 0 || !has_city(p) || e->prov_cot[i] < 0 || p->goods < 0) continue;
		ct = &w->cot[e->prov_cot[i]];
		if (!ct->n) continue;
		V = econ_trade_value_m(e, i);
		for (k = 0; k < ct->n; k++) total += ct->level[k];
		for (k = 0; k < ct->n; k++) {
			long n = ct->level[k] == 6 ? 26 - total : ct->level[k];
			if (n <= 0) continue;
			{	int tc = ct->tag[k], cap = w->country[tc].capital;
				long inc = eff[tc] * V / 1000 * n * 50 / 1000;
				if (cap > 0 && cap < w->nprov && w->prov[ct->prov].continent != w->prov[cap].continent) inc = inc * overseas_m(e, tc) / 1000;   /* §5.3 step 4 */
				e->trade_acc[tc] += inc;
				if (p->goods == e->grain_goods) e->c[tc].grain_tr_m += (is_colony(p) ? 500 : city_mult_m(p->population)) * n / 20;   /* 0x480f7a */
			}
		}
	}
}


/* -------------------------------------------------------------- loans */
/* §7.2 / §7.3 (partial). Loan size 200 (the country default), 5 years, at most 5 loans. The bank
 * interest rate is NIEZNANE in the spec: 10 %/year assumed. The AI extends a due loan it cannot pay
 * (the player will get the dialog). Bankruptcy when the treasury is negative and no loan is left. */
#define LOAN_SIZE_M (co->loansize > 0 ? 1000L * co->loansize : 200000L)   /* needs co in scope */
/* 0x45bb00, the bank's rate for a new loan (percent a year): 10 - stability + 3 x extensions - last month's income / 100
 * + 0.2 x bank loans (+ country+0xea0, only ever reset: 0), truncated; -3 for ENG and HOL (the exe's indices 1 and 4);
 * +25 within 5 years of a bankruptcy; at least 5; halved with a bank (event "bank") */
static int bank_rate(const Economy *e, int c)
{
	const Country *co = &e->w->country[c];
	const CEcon *ce = &e->c[c];
	long r = (10L - co->stability + 3L * ce->ext_count) * 1000 - ce->last_income_m / 1000 * 10 + 200L * ce->nloans;   /* x1000 */
	long today = cal_day(e->w->now.year, e->w->now.month, e->w->now.day);
	int rr = (int)(r / 1000);
	if (!strcmp(co->tag, "ENG") || !strcmp(co->tag, "HOL")) rr -= 3;   /* exe indices 1 / 4 = country.csv rows 0 / 3 */
	if (co->bankrupt_day > 0 && today < co->bankrupt_day + 5 * 360) rr += 25;
	if (rr < 5) rr = 5;
	if (co->bank) rr /= 2;
	return rr;
}
#define LOAN_RATE(ce, i) ((ce)->loan_rate[i] > 0 ? (ce)->loan_rate[i] : 10)
static void loans_month(Economy *e, int c)
{
	World *w = e->w;
	Country *co = &w->country[c];
	CEcon *ce = &e->c[c];
	long today = cal_day(w->now.year, w->now.month, w->now.day);
	int i;
	for (i = 0; i < ce->nloans; i++) { co->treasury_m -= ce->loan_m[i] * LOAN_RATE(ce, i) / 1200; world_book_exp(w, c, EXP_INTEREST, ce->loan_m[i] * LOAN_RATE(ce, i) / 1200); }   /* interest, expense 14 */
	for (i = 0; i < ce->nloans; ) {
		if (today < ce->loan_due[i]) { i++; continue; }
		if (co->human) {                                     /* the player is asked: pay or extend (0x522de0, BPAYLOAN / BEXTENDLOAN) */
			world_note(w, NOTE_LOANDUE, c, i, (int)(ce->loan_m[i] / 1000));
			i++; continue;
		}
		if (co->treasury_m >= ce->loan_m[i]) {               /* repay, expense 13 */
			co->treasury_m -= ce->loan_m[i]; world_book_exp(w, c, EXP_LOAN, ce->loan_m[i]);
			ce->loan_m[i] = ce->loan_m[ce->nloans - 1]; ce->loan_due[i] = ce->loan_due[ce->nloans - 1]; ce->loan_rate[i] = ce->loan_rate[ce->nloans - 1]; ce->nloans--;
		} else { ce->loan_due[i] = today + 5 * 360; ce->ext_count++; if (co->stability > -3) co->stability--; i++; }   /* extend (0x449210: +0xebc, stability -1) */
	}
	if (co->treasury_m >= 0) return;
	if (ce->nloans < 5 && econ_can_take_loan(e, c)) {
		ce->loan_rate[ce->nloans] = (short)bank_rate(e, c);
		ce->loan_m[ce->nloans] = LOAN_SIZE_M; ce->loan_due[ce->nloans] = today + 5 * 360; ce->nloans++;
		co->treasury_m += LOAN_SIZE_M;                      /* loan income goes straight to the treasury */
		world_note(w, NOTE_AUTOLOAN, c, -1, -1);           /* AUTOLOAN for the player */
		if (co->treasury_m >= 0) return;
	}
	/* bankruptcy */
	{
		short s = (short)(co->stability - 3);
		co->stability = s < -3 ? -3 : s;
	}
	for (i = 0; i < 4; i++) { ce->progress[i] -= 1000000L; if (ce->progress[i] < 0) ce->progress[i] = 0; }
	ce->stab_progress = 0;
	co->inflation_u += 10000L * ce->nloans + 50000L;
	ce->nloans = 0;
	co->treasury_m = 0;
	ce->bankruptcies++;
	co->bankrupt_day = cal_day(w->now.year, w->now.month, w->now.day);
	if (co->bankrupt_day == 0) co->bankrupt_day = 1;
	world_note(w, NOTE_BANKRUPT, c, -1, -1);
}

/* ------------------------------------------------------------- monthly */
static void post_expense(Country *co, CEcon *ce, long x)   /* AddExpense for maintenance (§6.1) */
{
	if (x > co->treasury_m && ce->rep.pool > 0) {
		long over = x - (co->treasury_m > 0 ? co->treasury_m : 0), take = over < ce->rep.pool ? over : ce->rep.pool;
		ce->rep.pool -= take; x -= take;
	}
	co->treasury_m -= x;
}

static long research_cost_m(const Economy *e, const Country *co, int track, int level, int year)
{
	long base, cost;
	int n = co->nprov_owned, g;
	long S, P, A = 1000, Gf;
	static const short gtab[5] = { 1000, 900, 800, 600, 100 };
	const char *tg;
	/* level L -> L+1 costs row L+1 (the spec's worked example: land 1->2 = row 2 = 6237; its formula text
	 * says row L - the example wins, NIEPEWNE); the ahead-of-time year is the row before that */
	if (level + 1 >= e->nlev[track] || !(base = e->cost[track][level + 1])) return 0;      /* maxed */
	tg = world_name(e->w, co->techgroup);
	g = script_ieq(tg, "latin") ? 0 : script_ieq(tg, "orthodox") ? 1 : script_ieq(tg, "muslim") ? 2 : script_ieq(tg, "china") ? 3 : 4;
	Gf = 2000 - gtab[g];
	if (n <= 1) S = 500; else if (n == 2) S = 600; else if (n == 3) S = 700; else if (n == 4) S = 750;
	else if (n == 5) S = 800; else if (n == 6) S = 900; else if (n == 7) S = 950;
	else { S = n * 1000 / 8; if (S > 1000) S = 1000 + (S - 1000) / 10; }
	{ int r = religion_of(e, co->religion); if (r >= 0) S -= e->rel_techspeed[r]; }
	S -= 10L * world_hre_n(e->w, (int)(co - e->w->country));   /* the emperor's electors (0x434b7e) */
	if (S < 500) S = 500;
	if (S > 2000) S = 2000;
	P = 1000 - 20 * dpol(co, 1) - 30 * dpol(co, 2);
	if (level > 0 && e->year[track][level] > year + 10)
		A = 1500 + 100L * (e->year[track][level] - year);
	cost = base * (1000 + co->inflation_u / 1000) / 1000;
	if (cost < 1) cost = 1;
	cost = cost * Gf / 1000 * S / 1000 * P / 1000 * A / 1000;
	return cost * 1000;                                  /* x1000 */
}

static long stability_cost_m(const Economy *e, const Country *co, int c)
{
	const World *w = e->w;
	long sum = 0, cost;
	int i, sr = religion_of(e, co->religion);
	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) {
		const Province *p = &w->prov[i];
		long k;
		int pr;
		if (p->owner != c || !has_city(p)) continue;
		if (is_colony(p)) { sum += 5; continue; }
		pr = religion_of(e, p->religion);
		if (pr == sr || sr < 0 || pr < 0) k = 25;
		else if (sr <= 4) k = pr <= 4 ? 50 : 100;
		else k = 60;
		if (sr >= 0) k -= e->rel_stab[sr] / 1000;
		if (k < 5) k = 5;
		sum += k;
	}
	cost = sum * (1000 + co->inflation_u / 1000) / 1000;
	cost = cost * (1000 + 50 * dpol(co, 2) - 100 * dpol(co, 7)) / 1000;
	cost = cost * (1000 - 10L * world_hre_n(w, c)) / 1000;          /* the emperor (0x4317b9) */
	{	/* badboy factor */
		int y, m, d;
		long lim, bb = co->badboy_m / 1000, f;
		cal_date(cal_day(w->now.year, w->now.month, w->now.day), &y, &m, &d);
		lim = 3 + 30 + (y - 1419) / 20 + (y > 1790 ? y - 1790 : 0);
		f = bb * 2 <= lim ? bb * 20 : bb <= lim ? bb * 40 : bb * 80;   /* x1000 */
		if (f > 3000) f = 3000;
		cost = cost * (1000 + f) / 1000;
	}
	return cost * 1000;
}

/* the AI's budget sliders, decoded (SPEC_MILITARY_AI A.5, 0x544b60); shares x1000. I = last month's income, D = the monthly
 * deficit not covered by tax (maintenance + interest - tax). Then the inflation cheat (A.5.7) and the money cheat (A.5.8).
 * "trade minded" = world_trade_minded (0x43a9e0); 0x43a9c0 = STE / BOS (exe indices 0x36 / 0x88); rolls stateless (day, country) */
/* the capital's land (+0x498 land mass, or the continent): the recount's "naval power" (+0x1f84: own ports there) and the
 * blockade share (0x43d200: blockaded / all of those ports), x1000; X = 2 x (domestic slider +0x1c - 5), read as the
 * mercantilism slider (NIEPEWNE) */
static int on_cap_land(const World *w, int c, int p)
{
	int k = w->country[c].capital;
	if (k <= 0 || k >= w->nprov) return 0;
	return (w->prov[p].lgroup >= 0 && w->prov[p].lgroup == w->prov[k].lgroup) || w->prov[p].continent == w->prov[k].continent;
}
static int has_port(const World *w, int p) { int z = w->prov[p].port_sea; return z > 0 && z < w->nprov && w->prov[z].terrain == 5; }
static long naval_power(const Economy *e, int c)
{
	long n = 0;
	int i;
	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) if (has_port(e->w, i) && on_cap_land(e->w, c, i)) n++;
	return n;
}
static long blockade_share_m(const Economy *e, int c)
{
	long n = 0, b = 0;
	int i;
	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) if (has_port(e->w, i) && on_cap_land(e->w, c, i)) { n++; if (e->w->prov[i].blockaded) b++; }
	return n ? b * 1000 / n : 0;
}
static long overseas_m(const Economy *e, int c)              /* 0x43d1c0: 0 without a naval power, else 1 - share - 0.01 X */
{
	long v;
	if (!naval_power(e, c)) return 0;
	v = 1000 - blockade_share_m(e, c) - 20L * (e->w->country[c].policy[3] - 5);
	return v < 0 ? 0 : v;
}

static long academies(const Economy *e, int c)              /* country+0xef2: Fine Arts Academies (manufactory type 1) */
{
	long n = 0;
	int i;
	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) if (e->w->prov[i].manu == 1) n++;
	return n;
}

static void ai_budget(Economy *e, int c)
{
	World *w = e->w;
	Country *co = &w->country[c];
	CEcon *ce = &e->c[c];
	int i, k, war = 0, own = 0, coast = 0, ships = 0, yard = 0, D5 = w->opt_diff + 2;
	long I = ce->last_income_m, Dm, interest = 0, s, Rm, tr, X, tech, trade, prod, n, sh[B_N], sum = 0, diff = 0;
	long today = cal_day(w->now.year, w->now.month, w->now.day);
	unsigned long roll = (unsigned long)today * 7919UL + (unsigned long)c * 104729UL;
	for (i = 0; i < w->nwar && !war; i++) {
		for (k = 0; k < w->war[i].natt; k++) if (w->war[i].att[k] == c) war = 1;
		for (k = 0; k < w->war[i].ndef; k++) if (w->war[i].def[k] == c) war = 1;
	}
	for (i = 0; i < ce->nloans; i++) interest += ce->loan_m[i] * LOAN_RATE(ce, i) / 1200;
	Dm = ce->rep.maint_land + ce->rep.maint_naval + interest - ce->rep.tax;   /* last month's (rep is still last month's here) */
	if (Dm < 0) Dm = 0;
	/* 1. stability share */
	if (I <= 0) { s = co->stability < 0 ? 200 : 0; }
	else {
		long bonus = 5000L * academies(e, c) + 2000L * (co->m_adm ? co->m_adm : 3) - co->we_m;   /* 0x431a40: 5 x academies + 2 x ADM - WE */
		s = bonus < 0 ? -bonus * 1000 / I : 0;
		if (s > 1000) s = 1000;
		if (co->stability < 2 && s < 50) s = 50;
		if (co->stability >= 0) s = s * 5 / 4; else s = co->stability < -2 ? 1000 : 750;
		if (s > 1000) s = 1000;
	}
	Rm = 1000 - s;
	/* 2. treasury share */
	tr = Rm;
	if (I - Dm > 0 && I > 0 && co->treasury_m < 1000000L) tr = Dm * 1000 / I;
	if (war) { if (co->treasury_m < 100000L) tr += 500; else if (co->treasury_m < 300000L) tr += 250; }
	/* 3. investments */
	X = 1000 - tr; if (X < 0) X = 0;
	if (co->treasury_m > 2000000L) { X = 1000; tr = 0; }
	/* 4. tech / trade / production */
	tech = X * 6 / 10; trade = X * 2 / 10; prod = X * 2 / 10;
	if (world_trade_minded(w, c)) { trade = X * 3 / 10; prod = X / 10; }
	if (co->trade_tech > 3) { tech = X * 8 / 10; trade = 10; }          /* 0x4333f0 level > 3 (NIEPEWNE: the trade track) */
	/* 5. naval share: coast share x 0.25, x2 with a shipyard, x1.5 for ENG / PRU / MIN / GEN, 0 without ships */
	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) { own++; if (w->prov[i].kind == 1) coast++; if (w->prov[i].bflags & PB_SHIPYARD) yard = 1; }
	for (i = 0; i < w->nnaval && !ships; i++) if (w->naval[i].owner == c && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports > 0) ships = 1;
	n = own ? coast * 250L / own : 0;
	if (yard) n *= 2;
	if (!strcmp(co->tag, "ENG") || !strcmp(co->tag, "PRU") || !strcmp(co->tag, "MIN") || !strcmp(co->tag, "GEN")) n = n * 3 / 2;
	if (!ships) n = 0;
	if (n > 1000) n = 1000;
	sh[B_NAVAL] = n * tech / 1000; sh[B_LAND] = tech - sh[B_NAVAL];
	sh[B_STAB] = s; sh[B_TRADE] = trade; sh[B_INFRA] = prod; sh[B_TREAS] = tr;
	/* 9. floored, normalised, applied when they moved by more than 0.02 in all */
	for (k = 0; k < B_N; k++) { if (sh[k] < 0) sh[k] = 0; sum += sh[k]; }
	if (sum <= 0) { sh[B_TREAS] = 1000; sum = 1000; }
	{	long acc = 0, ns[B_N];
		for (k = 0; k < B_N; k++) { ns[k] = sh[k] * 1000 / sum; acc += ns[k]; }
		ns[B_TREAS] += 1000 - acc;
		for (k = 0; k < B_N; k++) diff += ns[k] > ce->slider[k] ? ns[k] - ce->slider[k] : ce->slider[k] - ns[k];
		if (diff > 20) for (k = 0; k < B_N; k++) ce->slider[k] = (short)ns[k];
	}
	/* 7. the inflation cheat: above max(the human's inflation, (year - 1419) x 0.0025 (x (10 g + 1.05) with gold)), 10 %: -0.005 */
	{
		long L = w->now.year > 1419 ? (long)(w->now.year - 1419) * 2500 : 0, lim = 0, tot = ce->ytd_tax + ce->ytd_prod + ce->ytd_gold + ce->ytd_tolls;
		int h;
		if (tot > 0 && ce->ytd_gold > 0) L = L * (10 * (ce->ytd_gold * 1000 / tot) + 1050) / 1000;
		for (h = 0; h < w->ncountry; h++) if (w->country[h].human && w->country[h].inflation_u > lim) lim = w->country[h].inflation_u;
		if (L > lim) lim = L;
		if (D5 == 4) lim /= 3; else if (D5 == 3) lim /= 2;
		if (co->inflation_u > lim && (roll >> 3) % 100 < 10 && strcmp(co->tag, "STE") && strcmp(co->tag, "BOS")) {   /* not the 0x43a9c0 tags */ co->inflation_u -= 5000; if (co->inflation_u < 0) co->inflation_u = 0; }
	}
	/* 8. the money cheat: broke, in deficit and no credit - GAINMONEY (category 7) */
	if (strcmp(co->tag, "STE") && strcmp(co->tag, "BOS")) {   /* 0x545275..0x545467 */
		int broke = co->treasury_m < 50000L && co->treasury_m < Dm && !econ_can_take_loan(e, c);
		long g = 0;
		if (war) { if (broke) g = 12 * Dm + 10000L; }            /* at war: 12 D + 10, else nothing */
		else if (broke) g = 20 * Dm + 10000L;                    /* at peace: 20 D + 10 */
		else if (co->bankrupt_day > 0 && (today - co->bankrupt_day) / 30 < 50) g = Dm + 10000L;   /* else within 50 months of a bankruptcy: D + 10 */
		if (g) { co->treasury_m += g; world_book_inc(w, c, INC_EVENT, g); }
	}
}

/* war exhaustion, SPEC_TIME_ECONOMY 8.4 / SPEC_WAR_DIPLOMACY 14: cap = 10 + 0.4 (d1 + d2); the AI's cap and
 * step are scaled by the difficulty D (0: x1, f 1; 1-2: x0.5, f 1; 3: x0.5, f 2; 4: x1/3, f 3). At peace
 * -0.25 f a month; at war +1/36 (Defender of the Faith 1/45) / f when a battle of one of its wars began
 * less than 35 days ago, else -0.0125 f. */
static void war_exhaustion(Economy *e, int c)
{
	World *w = e->w;
	Country *co = &w->country[c];
	long cap = 10000L + 400L * (dpol(co, 1) + dpol(co, 2)), today = cal_day(w->now.year, w->now.month, w->now.day);
	int f = 1, D = w->opt_diff + 2, i, k, atwar = 0, recent = 0;
	if (!co->human) {
		if (D == 1 || D == 2) cap /= 2;
		else if (D == 3) { cap /= 2; f = 2; }
		else if (D >= 4) { cap /= 3; f = 3; }
	}
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		int in = 0;
		for (k = 0; k < wr->natt && !in; k++) if (wr->att[k] == c) in = 1;
		for (k = 0; k < wr->ndef && !in; k++) if (wr->def[k] == c) in = 1;
		if (!in) continue;
		atwar = 1;
		if (wr->last_combat > 0 && today - wr->last_combat < 35) recent = 1;
	}
	if (!atwar) co->we_m -= 250L * f;
	else if (recent) co->we_m += (co->defender ? 1000L / 45 : 1000L / 36) / f;
	else co->we_m -= 125L * f / 10;
	if (co->we_m < 0) co->we_m = 0;
	if (cap < 0) cap = 0;
	if (co->we_m > cap) co->we_m = cap;
}

int econ_boost(Economy *e, int c, int row);
static void country_month(Economy *e, int c)
{
	World *w = e->w;
	Country *co = &w->country[c];
	CEcon *ce = &e->c[c];
	int i, sr = religion_of(e, co->religion);
	long tax = 0, prod = 0, gold = 0, tolls = 0, maint = 0, mp_sum = 0, manu_inc = 0;
	long infra_eff, trade_eff;
	int coastal = 0;

	if (!co->human) {                                      /* the player sets the sliders in the budget window */
		ai_budget(e, c);
		if (co->stability < 0) econ_boost(e, c, ECON_SL_STAB);   /* the BOOST_STABILITY plan (SPEC_MILITARY_AI, 0x554a50; NIEPEWNE: taken whenever allowed) */
	}
	/* 1. badboy decay (4.1 step 1): k x DIP x 0.2 / 12, k = 0.25; difficulty 3 -> 1/6, 4 -> 1/8 (NIEPEWNE: the
	 * exe applies it outside multiplayer, for every country) */
	{
		int D = w->opt_diff + 2, dip = co->m_dip ? co->m_dip : 3;
		long dec = D == 4 ? dip * 25L / 12 : D == 3 ? dip * 100L / 36 : dip * 50L / 12;   /* x1000: k x DIP x 200 / 12 */
		co->badboy_m -= dec;
		if (co->badboy_m < 0) co->badboy_m = 0;
	}
	/* 2. war exhaustion (8.4) */
	war_exhaustion(e, c);

	infra_eff = tech_eff(e, e->infra_eff, R_INFRA, co->infra_tech, sr >= 0 ? (int)e->rel_prod[sr] : 0,
	                     dpol(co, 5) + 2 * dpol(co, 1) - 2 * dpol(co, 7) - dpol(co, 0));
	trade_eff = tech_eff(e, e->trade_eff, R_TRADE, co->trade_tech, sr >= 0 ? (int)e->rel_trade[sr] : 0, -dpol(co, 0) - dpol(co, 5));

	for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) {
		Province *p = &w->prov[i];
		long mult;
		if (p->owner != c) continue;
		if (p->kind == 1) coastal++;
		if (!has_city(p) || p->controller != c) continue;
		mult = city_mult_m(p->population);
		/* 9. tax (cities only, not colonies; none while looted, 0x457a2a) */
		if (!is_colony(p) && !p->looted) tax += yearly_tax_m(e, p, co, c) / 12;
		/* production / gold (§5.2), not from a blockaded city */
		if (p->blockaded || p->looted) ;
		else if (p->goods == e->gold_goods && e->gold_goods >= 0) gold += p->mine_value * mult / 12;
		else if (p->goods >= 0) prod += e->price_m[p->goods] / 10 * (mult / 100) / 1000 * infra_eff / 12;   /* the market price (9.1) */
		if (!is_colony(p)) {                                 /* §3.2: tax collector / governor +1 a year; §5.6 a manufactory 6 a year (12 on its own goods: NIEPEWNE which) */
			if (p->bflags & PB_TAXCOL) prod += 1000 / 12;
			if (p->bflags & PB_GOVERNOR) prod += 1000 / 12;
			if (p->manu) { prod += 6000 / 12; manu_inc += 6000 / 12; }
		}
		/* 11. tolls (§5.4) */
		if (!is_colony(p)) tolls += trade_eff * (p->population >= 5000 ? city_size(p->population) + 10 : settlement_size(p->population)) / 12;
		/* manpower (§8.2), yearly per province, x1000 */
		mp_sum += world_prov_manpower_m(w, i);                    /* 0x47cc80 (with the land connection and the 10-year rule) */
		/* population growth (§8.1, base terms): g = stability + 4 per mille per year */
		{
			long g = co->stability + 4 + ((p->bflags & PB_GOVERNOR) ? 1 : 0) + (p->manu ? 2 : 0), inc;   /* governor +1, manufactory +2 */
			int ci;
			for (ci = 0; ci < w->ncot; ci++) if (w->cot[ci].prov == i) g += 5;          /* a centre of trade here */
			g += 2L * p->cot_nb;                                                     /* per neighbouring CoT */
			if (is_colony(p) && !(e->europe >= 0 && p->continent == e->europe)) g += 5;        /* a colony (not on continent 2) */
			if (p->hostile) g -= 5;                                                  /* enemy troops present */
			if (p->controller != p->owner) g -= 3;                                   /* occupied */
			if (p->besieged || p->blockaded) g -= 5;                                 /* siege or blockade */
			if (p->looted) g -= 5;                                                   /* looted */
			{	/* the location term (0x47e940): off the capital's continent, a city below 5000 (fantasy off) */
				long L = 0, G;
				int cap = co->capital;
				if (cap > 0 && cap < w->nprov && w->prov[cap].continent != p->continent && p->population < 5000) {
					int big = p->terrain >= 5 || !p->island;
					const char *cn = p->continent >= 0 ? world_name(w, p->continent) : "";
					if (p->climate == 4 || p->climate == 6) L = big ? -8000 : -1000;
					if (!strcmp(cn, "Asia")) L -= big ? 4000 : 1000;                    /* continent 3 */
					else if (!strcmp(cn, "Africa")) L -= big ? 12000 : 2000;            /* continent 5 */
					if (big) L -= 500L * p->col_diff;
					if (p->population > 3500) L /= 4;
					else if (p->population > 2500) L /= 3;
					else if (p->population > 2000) L /= 2;
					else if (p->population > 1000) L = L * 2 / 3;
					if (L < -14000) L = -14000;
				}
				G = g * 1000 + L;                                                    /* per mille x1000 */
				inc = p->population / 1000 * G / 12 + p->population % 1000 * G / 12000;   /* x1000 people */
			}
			long cap = (p->income_m / 1000 + 200) * 1000;
			if (inc > cap) inc = cap;
			p->pop_frac_m += inc;
			p->population += p->pop_frac_m / 1000;
			p->pop_frac_m %= 1000;
			if (!is_colony(p) && p->population < 1000) p->population = 1000;
			if (p->population > 999999) p->population = 999999;
		}
	}
	if (co->war_tax > 0) co->war_tax--;                     /* step 9 (0x451943): after this month's tax */
	ce->rep.tax = tax; ce->rep.production = prod; ce->rep.gold = gold; ce->rep.tolls = tolls;
	ce->ytd_tax += tax; ce->ytd_prod += prod; ce->ytd_gold += gold; ce->ytd_tolls += tolls;
	ce->rep.trade = e->trade_acc[c];
	{	/* a vassal pays its overlord half its tax (0x451943 step 9: income 5 "Vassals") */
		int ov = world_overlord(w, c);
		if (ov >= 0 && ov != c && tax > 0) {
			long half = tax / 2;
			tax -= half; ce->rep.tax = tax;
			w->country[ov].treasury_m += half; world_book_inc(w, ov, INC_VASSAL, half);   /* NIEPEWNE: straight into the overlord's treasury */
		}
	}
	world_book_inc(w, c, INC_TAX, tax); world_book_inc(w, c, INC_TRADE, ce->rep.trade); world_book_inc(w, c, INC_PROD, prod - manu_inc);
	world_book_inc(w, c, INC_MANU, manu_inc); world_book_inc(w, c, INC_GOLD, gold); world_book_inc(w, c, INC_TOLLS, tolls);
	ce->rep.pool = tax + prod + gold + tolls + ce->rep.trade;

	/* 15. maintenance (§6.2), no over-limit surcharge yet */
	{
		long inf = 0, cav = 0, art = 0, ws = 0, gal = 0, tr = 0;
		long infl = 1000 + co->inflation_u / 1000;
		for (i = 0; i < w->nland; i++) if (w->land[i].owner == c) { inf += w->land[i].infantry; cav += w->land[i].cavalry; art += w->land[i].artillery; }
		for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == c) { ws += w->naval[i].warships; gal += w->naval[i].galleys; tr += w->naval[i].transports; }
#define UC(b) ((long)(b) * infl / 1000 < 1 ? 1L : (long)(b) * infl / 1000)
		{	/* x landSlider / navalSlider (§6.2): the maintenance levels of the land / naval windows */
			long ml = UC(9) * inf / 120 + UC(21) * cav / 120 + UC(30) * art * 1000 / 1200;
			long mn = UC(50) * ws * 1000 / 480 + UC(10) * gal * 1000 / 480 + UC(15) * tr * 1000 / 480;
			ml = ml * (1000 - ce->maint_cut[0]) / 1000; mn = mn * (1000 - ce->maint_cut[1]) / 1000;
			if (co->human || w->opt_diff + 2 < 1) {      /* the over-limit surcharge (§6.2): humans (or all when D < 1) */
				long troops = inf + cav + 100 * art, over = troops - econ_support_limit(e, c);
				if (over > 0 && troops > 0) {               /* unitCost x slider x (class / troops) x excess / 24, excess = over / 1000 */
					long sl = 1000 - ce->maint_cut[0], s = 0;
					s += UC(9) * sl / 1000 * (inf / 100) / (troops / 100 ? troops / 100 : 1) * (over / 24);
					s += UC(21) * sl / 1000 * (cav / 100) / (troops / 100 ? troops / 100 : 1) * (over / 24);
					s += UC(30) * sl / 1000 * art / (troops / 100 ? troops / 100 : 1) * (over / 24);   /* artillery as 100 men a piece (NIEPEWNE) */
					ml += s;
				}
			}
			ce->rep.maint_land = ml; ce->rep.maint_naval = mn;
			world_book_exp(w, c, EXP_ARMYM, ml); world_book_exp(w, c, EXP_FLEETM, mn);
			maint = ml + mn;
		}
#undef UC
		ce->rep.maintenance = maint;
		post_expense(co, ce, maint);
	}
	if (ce->rep.pool < 0) ce->rep.pool = 0;
	ce->last_income_m = ce->rep.pool;
	co->inc_m = ce->rep.pool; co->maint_m = ce->rep.maintenance;   /* for the AI (ai.c) */
	{	/* and for its strength indices (0x43c010, SPEC_AI_STRENGTH) */
		int k;
		co->maintl_m = ce->rep.maint_land; co->maintn_m = ce->rep.maint_naval;
		co->te_m = trade_eff_of(e, co); co->pe_m = econ_prod_eff(e, c);
		co->hports = (short)naval_power(e, c);
		co->loanint = 0; co->nloans_b = (short)ce->nloans;
		for (k = 0; k < ce->nloans && k < 5; k++) co->loanint += ce->loan_m[k] / 1000 * LOAN_RATE(ce, k);
	}

	/* 18. budget split (§6.3) */
	{
		long P = ce->rep.pool, part;
		static const int track_of[4] = { R_NAVAL, R_LAND, R_TRADE, R_INFRA };
		short *lvl[4];
		int k, y, m, d;
		lvl[0] = &co->naval_tech; lvl[1] = &co->land_tech; lvl[2] = &co->trade_tech; lvl[3] = &co->infra_tech;
		cal_date(cal_day(w->now.year, w->now.month, w->now.day), &y, &m, &d);
		for (k = 0; k < 4; k++) {
			int tr = track_of[k];
			long cost = research_cost_m(e, co, tr, *lvl[k], y);
			part = P * ce->slider[k] / 1000;
			if (!cost) { co->treasury_m += part; ce->slider[k] = 0; continue; }
			world_book_exp(w, c, k <= 1 ? EXP_TECH : EXP_TRINF, part);   /* naval / land: Technology, trade / infra: Trade & Infrastructure */
			ce->progress[tr] += part + (k == 0 || k == 1 ? 1000L * (co->m_mil ? co->m_mil : 3) : 0);   /* + MIL for land/naval */
			if (ce->progress[tr] >= cost) { ce->progress[tr] -= cost; (*lvl[k])++; }
		}
		part = P * ce->slider[B_STAB] / 1000;
		if (co->stability < 3) {
			long cost = stability_cost_m(e, co, c);
			world_book_exp(w, c, EXP_STAB, part);
			ce->stab_progress += part + 5000L * academies(e, c) + 2000L * (co->m_adm ? co->m_adm : 3) - co->we_m;   /* + 0x431a40: 5 x academies + 2 x ADM - war exhaustion */
			if (cost > 0 && ce->stab_progress >= cost) { ce->stab_progress -= cost; co->stability++; }
		} else co->treasury_m += part;
		co->treasury_m += P * ce->slider[B_TREAS] / 1000;
		ce->rep.pool = 0;
	}
	/* 19. inflation (§7.1): treasury slider / 1200, AI year cap */
	{
		long delta = (long)ce->slider[B_TREAS] * 1000 / 1200;          /* x1e6 (the AI's cheat is in ai_budget, A.5.7) */
		co->inflation_u += delta;
		if (co->inflation_u < 0) co->inflation_u = 0;
	}
	/* manpower (§8.2) */
	{
		long factor = 1000 + 50 * dpol(co, 5) - 50 * dpol(co, 6), rate = mp_sum * factor / 1000, minr = (co->land_tech * 5 / 60 + 2) * 1000, acc;
		rate += 1000L * world_hre_n(w, c);                    /* the emperor: + N a year (0x43cf11) */
		if (rate < minr) rate = minr;
		if (w->tutorial && co->human) rate *= 12;              /* 0x43cfeb: the player's yearly rate x12 in a tutorial */
		ce->manpower_rate_m = rate;
		acc = mp_sum / 12 * factor / 1000;
		if (acc < 1000) acc = 1000;
		if (w->tutorial && co->human) acc *= 12;
		if (co->manpower_m < 2 * rate) co->manpower_m += acc;
	}
	/* 21. envoys (§8.3), monthly = yearly / 12, stock cap 6 */
	{
		long col = 0, dip, miss, mer;
		if (coastal && sr >= 0) {
			long c5 = -400 * dpol(co, 5);
			if (c5 < 0) c5 /= 2;
			col = e->rel_col[sr] + 1000 - 400 * dpol(co, 2) - 200 * dpol(co, 3) + c5;
			if (col < 0) col = 0;
		}
		dip = (sr >= 0 ? e->rel_dip[sr] : 0) + 1000;
		miss = (sr >= 0 ? e->rel_miss[sr] : 0) - 400 * dpol(co, 2);
		if (miss < 0) miss = 0;
		{
			long st1 = co->stability + 1;
			if (st1 > 3) st1 = 3;
			if (st1 < -3) st1 = -3;
			mer = co->trade_tech / 5 * 1000 + (coastal ? (coastal / 25 + 1) * 1000 : 0) - 400 * dpol(co, 3) + st1 * 1000;
			if (mer < 0) mer = 0;
			if (mer > 12000) mer = 12000;
		}
		ce->rep.rate_mer = mer; ce->rep.rate_col = col; ce->rep.rate_dip = dip; ce->rep.rate_miss = miss;
		co->col_rate_m = col; co->mer_rate_m = mer;
#define ACC(x, r) do { x += (r) / 12; if (x > 6000) x = 6000; if (x < 0) x = 0; } while (0)
		ACC(co->colonists_m, col); ACC(co->diplomats_m, dip); ACC(co->missionaries_m, miss); ACC(co->merchants_m, mer);
#undef ACC
	}
}

static void build_owner_lists(Economy *e)
{
	World *w = e->w;
	int i;
	for (i = 0; i < MAX_COUNTRY; i++) e->first_prov[i] = -1;
	for (i = w->nprov - 1; i >= 1; i--) {               /* pushed in reverse: lists come out ascending */
		int o = w->prov[i].owner;
		if (o < 0 || o >= MAX_COUNTRY) continue;
		e->next_prov[i] = e->first_prov[o];
		e->first_prov[o] = (short)i;
	}
}

/* The market (docs/SPEC_MARKET.md; 0x41b620, monthly and on GAME_STARTED): supply = 0.5 + 0.01 per province with a
 * settlement or a trading post producing the goods (0.5..1.5); demand by market id from the world-wide counts over the
 * settlements (colonies included): S shipyards, B conscription centres, G governors, J chief judges, M manufactories,
 * m2..m5 per manufactory type, n1 CLO settlements, w plantations (COT, SUG, TOB: 1 under a Christian state religion,
 * else 0.1); d = clamp(formula, 0.01, 2), then at most the case's cap; price = base x (2 - supply) x demand. */
static void market_month(Economy *e)
{
	World *w = e->w;
	static long sup[MAX_NAMES];
	long B = 0, S = 0, J = 0, G = 0, M = 0, m[6] = { 0 }, n1 = 0, wsum = 0;
	int i;
	for (i = 0; i < w->nnames; i++) sup[i] = 500;
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		int g;
		if (!has_city(p) && !p->tp) continue;                 /* 0x47f6c0: a settlement or a trading post */
		if (p->goods < 0 || p->goods >= MAX_NAMES) g = -1;
		else { sup[p->goods] += 10; g = e->gid[p->goods]; }
		if (!has_city(p)) continue;                           /* the counters: settlements only */
		if (g == 1) n1++;
		if (g == 3 || g == 15 || g == 16) {
			int r = p->owner >= 0 ? religion_of(e, w->country[p->owner].religion) : -1;
			wsum += r >= 0 && r <= 4 ? 1000 : 100;
		}
		if (p->bflags & PB_CONSCRIPT) B++;
		if (p->bflags & PB_SHIPYARD) S++;
		if (p->bflags & PB_JUDGE) J++;
		if (p->bflags & PB_GOVERNOR) G++;
		if (p->manu) { M++; if (p->manu >= 1 && p->manu <= 5) m[p->manu]++; }
	}
	for (i = 0; i < w->nnames; i++) {
		long d, cap = 1500, s2 = sup[i];
		if (!e->is_goods[i]) continue;
		if (s2 > 1500) s2 = 1500;
		switch (e->gid[i]) {
		case 1: d = 500 + 10 * (m[5] + B); break;                                     /* CLO */
		case 2: d = 1000; break;                                                      /* GOLD */
		case 3: d = 500 + 10 * (n1 + m[5]); break;                                    /* COT */
		case 8: d = 500 + 10 * (m[2] + B); break;                                     /* METAL */
		case 9: d = 500 + 10 * (m[2] + S); break;                                     /* MINERAL */
		case 11: d = 500 + 10 * (m[3] + S); break;                                    /* NAVS */
		case 15: d = 1000 + 10 * m[4]; break;                                         /* SUG */
		case 4: case 6: case 12: case 17: case 18: d = 250 + 10 * (S + B + M); cap = 1000; break;            /* FISH GRAI SALT WINE WOOL */
		case 5: case 7: case 10: case 14: case 16: case 19: case 20: d = 750 + 10 * (J + G + S + B + M); cap = 2000; break;   /* FURS IVOR ORIENT SPIC TOB TEA COFFEE */
		case 13: d = wsum / 10; cap = 2000; break;                                    /* SLAV: 0.1 w */
		default: d = 1000; break;                                                     /* NOTHING: base 0, price 0 (its demand NIEZNANE) */
		}
		if (d > 2000) d = 2000;
		if (d < 10) d = 10;
		if (d > cap) d = cap;
		e->price_m[i] = e->goods_value[i] * (2000 - s2) * d / 1000;   /* x1000 */
	}
	e->price_ok = 1;
}

long econ_price_m(Economy *e, int goods) { if (!e->price_ok) market_month(e); return goods >= 0 && goods < MAX_NAMES ? e->price_m[goods] : 0; }

/* VP of the month (docs/SPEC_MISSIONS 2.1; 0x55f130, 0x41bcd0, 0x499e92): stability / 72, -0.1 per own
 * province the rebels hold, +1.1/12 diplomacy at peace; +1/24 to the greatest in manufactories, land
 * forces (100 art + cav + inf), ships and each of the four techs (ties all, value > 0); +1/12 to the
 * leading producer of each goods; +0.16 per centre of trade where a country has 6 merchants. */
static void score_month(Economy *e)
{
	World *w = e->w;
	static long manu[MAX_COUNTRY], land[MAX_COUNTRY], navy[MAX_COUNTRY], reb[MAX_COUNTRY], prod[MAX_COUNTRY];
	static unsigned char war[MAX_COUNTRY];
	long best[7];
	int c, i, k;
	if (w->opt_novp) return;
	memset(manu, 0, sizeof manu); memset(land, 0, sizeof land); memset(navy, 0, sizeof navy); memset(reb, 0, sizeof reb); memset(war, 0, sizeof war);
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		if (p->owner < 0) continue;
		if (p->manu) manu[p->owner]++;
		if (w->reb >= 0 && p->controller == w->reb) reb[p->owner]++;
	}
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].owner >= 0) land[w->land[i].owner] += 100 * w->land[i].artillery + w->land[i].cavalry + w->land[i].infantry;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner >= 0) navy[w->naval[i].owner] += w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports;
	for (i = 0; i < w->nwar; i++) { for (k = 0; k < w->war[i].natt; k++) war[w->war[i].att[k]] = 1; for (k = 0; k < w->war[i].ndef; k++) war[w->war[i].def[k]] = 1; }
	for (k = 0; k < 7; k++) best[k] = 0;
	for (c = 0; c < w->ncountry; c++) {
		const Country *co = &w->country[c];
		long v[7];
		if (co->nprov_owned <= 0) continue;
		v[0] = manu[c]; v[1] = land[c]; v[2] = navy[c]; v[3] = co->land_tech; v[4] = co->naval_tech; v[5] = co->infra_tech; v[6] = co->trade_tech;
		for (k = 0; k < 7; k++) if (v[k] > best[k]) best[k] = v[k];
	}
	for (c = 0; c < w->ncountry; c++) {
		const Country *co = &w->country[c];
		long v[7];
		if (co->nprov_owned <= 0) continue;
		world_vp(w, c, VP_ECO, (long)co->stability * 1000 / 72);
		if (reb[c]) world_vp(w, c, VP_ECO, -100L * reb[c]);
		if (!war[c]) world_vp(w, c, VP_DIP, 1100 / 12);
		v[0] = manu[c]; v[1] = land[c]; v[2] = navy[c]; v[3] = co->land_tech; v[4] = co->naval_tech; v[5] = co->infra_tech; v[6] = co->trade_tech;
		for (k = 0; k < 7; k++) if (v[k] > 0 && v[k] >= best[k]) world_vp(w, c, VP_ECO, 1000 / 24);
	}
	for (i = 0; i < w->nnames; i++) {                             /* the leading producer of each goods (integer threshold quirk) */
		long t = 0;
		int lead = -1;
		if (!e->is_goods[i]) continue;                            /* every goods, NOTHING and GOLD included (0x41bcd0) */
		memset(prod, 0, sizeof prod);
		for (k = 1; k < w->nprov; k++) {                           /* country+0xf06: summed per owner, the controller not tested */
			const Province *p = &w->prov[k];
			if (p->goods != i || p->owner < 0 || (!has_city(p) && !p->tp)) continue;
			prod[p->owner] += has_city(p) && !is_colony(p) ? city_mult_m(p->population) : 500;   /* NIEPEWNE: colony / trading post vfunc+0x18 = 0.5 */
		}
		for (c = 0; c < w->ncountry; c++) if (prod[c] > t * 1000) { lead = c; t = prod[c] / 1000; }
		if (lead >= 0) world_vp(w, lead, VP_ECO, 1000 / 12);
	}
	for (i = 0; i < w->ncot; i++)                                  /* monopolies */
		for (k = 0; k < w->cot[i].n; k++) if (w->cot[i].level[k] >= 6) world_vp(w, w->cot[i].tag[k], VP_ECO, 160);
}

void econ_month(Economy *e)
{
	int i;
	build_owner_lists(e);
	market_month(e);
	trade_month(e);
	cot_month(e);
	for (i = 0; i < e->w->ncountry; i++) if (e->w->country[i].nprov_owned > 0) { country_month(e, i); loans_month(e, i); }
	score_month(e);
}

void econ_year(Economy *e)
{
	World *w = e->w;
	int c, i;
	build_owner_lists(e);
	world_books_year(w);                                     /* the Ledger: this year becomes last year */
	for (c = 0; c < w->ncountry; c++) {
		Country *co = &w->country[c];
		CEcon *ce = &e->c[c];
		long census = 0, total;
		co->treasury_m += 2000L * world_hre_n(w, c);           /* the emperor: 2 N ducats a year (0x4512b9, income 5) */
		world_book_inc(w, c, INC_VASSAL, 2000L * world_hre_n(w, c));
		if (co->nprov_owned <= 0) continue;
		/* gold inflation (§7.2) */
		total = ce->ytd_tax + ce->ytd_prod + ce->ytd_gold + ce->ytd_tolls;
		if (total > 0 && ce->ytd_gold * 10 > total * 4) co->inflation_u += ce->ytd_gold * 2500 / total;
		ce->ytd_tax = ce->ytd_prod = ce->ytd_gold = ce->ytd_tolls = 0;
		if (co->inflation_u < 0) co->inflation_u = 0;
		/* census tax (§5.1) */
		for (i = e->first_prov[c]; i > 0; i = e->next_prov[i]) {
			const Province *p = &w->prov[i];
			long share;
			if (p->owner != c || p->controller != c || !has_city(p) || is_colony(p)) continue;
			share = (is_core(p, c) ? 250 : 0) + co->policy[1] * 25;
			if (share > 1000) share = 1000;
			census += yearly_tax_m(e, p, co, c) * share / 1000;
		}
		co->treasury_m += census; world_book_inc(w, c, INC_CENSUS, census);
		ce->rep.census = census;
	}
	cot_year(e);                                             /* the trade manager's yearly event 0x17 */
}

/* ---------------------------------------------------------- budget window (SPEC_UI §4.1) */
static const int ui_to_b[ECON_SL_N] = { B_LAND, B_NAVAL, B_STAB, B_TRADE, B_INFRA, B_TREAS };

void econ_budget(const Economy *e, int c, EconBudget *b)
{
	const CEcon *ce = &e->c[c];
	const Country *co = &e->w->country[c];
	static const int track[4] = { R_LAND, R_NAVAL, R_TRADE, R_INFRA };
	const short *lvl[4];
	int k;
	lvl[0] = &co->land_tech; lvl[1] = &co->naval_tech; lvl[2] = &co->trade_tech; lvl[3] = &co->infra_tech;
	for (k = 0; k < ECON_SL_N; k++) b->slider[k] = ce->slider[ui_to_b[k]];
	b->lock = ce->lock;
	for (k = 0; k < 4; k++) {
		int row = k < 2 ? k : k + 1;                     /* rows: land naval stab trade production(infra) treasury */
		b->cost_m[row] = research_cost_m(e, co, track[k], *lvl[k], e->w->now.year);
		b->progress_m[row] = ce->progress[track[k]];
	}
	b->cost_m[2] = co->stability < 3 ? stability_cost_m(e, co, c) : 0;
	b->progress_m[2] = ce->stab_progress;
	b->cost_m[5] = b->progress_m[5] = 0;
	b->income_m = ce->last_income_m;
}

/* sets one slider (0..1000) and scales the others so the six still sum to 1000 */
/* the budget window (0x51c630 from 0x51ca04): the moved row takes v; the difference R0 = 1000 - sum is shared
 * equally among the rows that are neither moved nor locked (R0 / n each, clamped 0..1000, what a clamped row
 * cannot take goes round again - the exe leaves it for the next event). A locked row does not move; with
 * every other row locked the moved one cannot move either. */
void econ_set_slider(Economy *e, int c, int row, int v)
{
	CEcon *ce = &e->c[c];
	int k, pass, rest, n, free_[ECON_SL_N];
	if (row < 0 || row >= ECON_SL_N || (ce->lock >> row & 1)) return;
	if (v < 0) v = 0;
	if (v > 1000) v = 1000;
	for (k = 0, n = 0; k < ECON_SL_N; k++) { free_[k] = k != row && !(ce->lock >> k & 1); n += free_[k]; }
	if (!n) return;
	ce->slider[ui_to_b[row]] = (short)v;
	for (pass = 0; pass < ECON_SL_N; pass++) {
		int share, extra;
		for (k = 0, rest = 1000; k < ECON_SL_N; k++) rest -= ce->slider[ui_to_b[k]];
		for (k = 0, n = 0; k < ECON_SL_N; k++) n += free_[k];
		if (!rest || !n) break;
		share = rest / n; extra = rest - share * n;              /* the remainder to the first free row */
		for (k = 0; k < ECON_SL_N; k++) {
			int b = ui_to_b[k], want;
			if (!free_[k]) continue;
			want = ce->slider[b] + share + extra; extra = 0;
			if (want < 0) { want = 0; free_[k] = 0; }
			if (want > 1000) { want = 1000; free_[k] = 0; }
			ce->slider[b] = (short)want;
		}
	}
	for (k = 0, rest = 1000; k < ECON_SL_N; k++) rest -= ce->slider[ui_to_b[k]];
	ce->slider[ui_to_b[row]] = (short)(ce->slider[ui_to_b[row]] + rest);   /* what nobody could take stays with the moved row */
}

int econ_slider_lock(Economy *e, int c, int row)
{
	if (row < 0 || row >= ECON_SL_N) return 0;
	e->c[c].lock ^= (unsigned char)(1 << row);
	return e->c[c].lock >> row & 1;
}

/* ------------------------------------------------------------- save game */
void econ_save(const Economy *e, FILE *f)
{
	const World *w = e->w;
	int c, k;
	fprintf(f, "amieu2_economy = {\n");
	for (c = 0; c < w->ncountry; c++) {
		const CEcon *ce = &e->c[c];
		const Country *co = &w->country[c];
		if (!w->country[c].present && w->country[c].nprov_owned <= 0) continue;
		fprintf(f, "\tc = { tag = %s p = { %ld %ld %ld %ld } stab = %ld sl = {", w->country[c].tag,
		        ce->progress[0], ce->progress[1], ce->progress[2], ce->progress[3], ce->stab_progress);
		for (k = 0; k < B_N; k++) fprintf(f, " %d", ce->slider[k]);
		fprintf(f, " } gt = %ld mpr = %ld inc = %ld ytd = { %ld %ld %ld %ld } bk = %ld lk = %d mc = { %d %d } loans = {", ce->grain_tr_m, ce->manpower_rate_m, ce->last_income_m,
		        ce->ytd_tax, ce->ytd_prod, ce->ytd_gold, ce->ytd_tolls, ce->bankruptcies, ce->lock, ce->maint_cut[0], ce->maint_cut[1]);
		for (k = 0; k < ce->nloans && k < 5; k++) fprintf(f, " %ld %ld", ce->loan_m[k], ce->loan_due[k]);
		fprintf(f, " } lr = {");
		for (k = 0; k < ce->nloans && k < 5; k++) fprintf(f, " %d", ce->loan_rate[k]);
		fprintf(f, " } ext = %ld rp = { %ld %ld %ld } bu = {", ce->ext_count, ce->rep.tax, ce->rep.maint_land, ce->rep.maint_naval);   /* rp: last month's, read by the AI budget */
		fprintf(f, " %ld %ld %ld %ld %ld } fx = { %ld %ld %ld %ld %ld %d %d } }\n", ce->boost_until[0], ce->boost_until[1], ce->boost_until[2], ce->boost_until[3], ce->boost_until[4],
		        co->te_m, co->pe_m, co->maintl_m, co->maintn_m, co->loanint, co->hports, co->nloans_b);   /* the month's strength inputs (ai.c) */
	}
	if (e->price_ok) {                                         /* the month's market prices (read mid-month: trade values, the pirates) */
		fprintf(f, "\tprices = {");
		for (k = 0; k < MAX_NAMES; k++) if (e->price_m[k] && world_name(w, (short)k)) fprintf(f, " %s %ld", world_name(w, (short)k), e->price_m[k]);
		fprintf(f, " }\n");
	}
	fprintf(f, "}\n");
}

static int ints(const SNode *list, long *out, int max)          /* bare numbers of a { } list */
{
	const SNode *q;
	int n = 0;
	for (q = list ? list->child : NULL; q && n < max; q = q->next) if (!q->key && q->val) out[n++] = parse_int(q->val);
	return n;
}

void econ_restore(Economy *e, const SNode *root)
{
	const SNode *blk = script_get(root, "amieu2_economy"), *n;
	if (blk && (n = script_get(blk, "prices")) != NULL) {
		const SNode *q;
		memset(e->price_m, 0, sizeof e->price_m);
		for (q = n->child; q && q->next; q = q->next->next)
			if (!q->key && q->val && q->next->val) { short g = world_intern(e->w, q->val); if (g >= 0 && g < MAX_NAMES) e->price_m[g] = parse_int(q->next->val); }
		e->price_ok = 1;
	}
	for (n = blk ? blk->child : NULL; n; n = n->next) {
		long v[16];
		int c, k, m;
		CEcon *ce;
		if (!n->key || !script_ieq(n->key, "c") || (c = world_country(e->w, script_str(n, "tag", ""))) < 0) continue;
		ce = &e->c[c];
		if (ints(script_get(n, "p"), v, 4) == 4) for (k = 0; k < 4; k++) ce->progress[k] = v[k];
		ce->stab_progress = script_int(n, "stab", 0);
		if (ints(script_get(n, "sl"), v, B_N) == B_N) for (k = 0; k < B_N; k++) ce->slider[k] = (short)v[k];
		ce->manpower_rate_m = script_int(n, "mpr", 0);
		ce->grain_tr_m = script_int(n, "gt", 0);
		ce->last_income_m = script_int(n, "inc", 0);
		if (ints(script_get(n, "ytd"), v, 4) == 4) { ce->ytd_tax = v[0]; ce->ytd_prod = v[1]; ce->ytd_gold = v[2]; ce->ytd_tolls = v[3]; }
		ce->bankruptcies = script_int(n, "bk", 0);
		ce->lock = (unsigned char)(script_int(n, "lk", 0) & 63);
		ce->maint_cut[0] = ce->maint_cut[1] = 0;
		if (ints(script_get(n, "mc"), v, 2) == 2) for (k = 0; k < 2; k++) ce->maint_cut[k] = (short)(v[k] < 0 ? 0 : v[k] > 500 ? 500 : v[k]);
		m = ints(script_get(n, "loans"), v, 10);
		for (k = 0, ce->nloans = 0; k + 1 < m; k += 2) { ce->loan_m[ce->nloans] = v[k]; ce->loan_due[ce->nloans] = v[k + 1]; ce->loan_rate[ce->nloans] = 10; ce->nloans++; }
		m = ints(script_get(n, "lr"), v, 5);
		for (k = 0; k < m && k < ce->nloans; k++) ce->loan_rate[k] = (short)v[k];
		ce->ext_count = script_int(n, "ext", 0);
		if (ints(script_get(n, "rp"), v, 3) == 3) { ce->rep.tax = v[0]; ce->rep.maint_land = v[1]; ce->rep.maint_naval = v[2]; }
		memset(ce->boost_until, 0, sizeof ce->boost_until);
		if (ints(script_get(n, "bu"), v, 5) == 5) for (k = 0; k < 5; k++) ce->boost_until[k] = v[k];
		if (ints(script_get(n, "fx"), v, 7) == 7) {
			Country *co = &e->w->country[c];
			co->te_m = v[0]; co->pe_m = v[1]; co->maintl_m = v[2]; co->maintn_m = v[3]; co->loanint = v[4]; co->hports = (short)v[5]; co->nloans_b = (short)v[6];
		}
	}
}

/* the yearly manpower of a country with the §8.2 terms of country_month (no policy factor) */
static long yearly_manpower(const Economy *e, int c)
{
	const World *w = e->w;
	const Country *co = &w->country[c];
	int i, sr = religion_of(e, co->religion);
	long sum = 0;
	for (i = 1; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		long m;
		if (p->owner != c || is_colony(p)) continue;
		m = p->manpower_m / 4 * (city_size(p->population >= 5000 ? p->population : 0) / 3 + 1);
		if (!world_culture_ok(e->w, c, p->culture)) m -= 1000;
		if (!is_core(p, c)) {
			int pr = religion_of(e, p->religion);
			if (pr >= 0 && sr >= 0 && rel_norm(pr) != rel_norm(sr) && pr != 7) m -= 1000;
		}
		if (m > 0) sum += m;
	}
	return sum;
}

/* a scenario gives no manpower pool: start with one year's worth (NIEPEWNE: the original's start
 * value was not read). Called for a new game only: a save game brings its own pool. */
void econ_new_game(Economy *e)
{
	int c;
	for (c = 0; c < e->w->ncountry; c++) {
		Country *co = &e->w->country[c];
		if (co->nprov_owned > 0 && co->manpower_m == 0) co->manpower_m = yearly_manpower(e, c);
	}
}

const short *econ_prov_cots(const Economy *e) { return e ? e->prov_cot : NULL; }

int  econ_maint_level(const Economy *e, int c, int naval) { return e && c >= 0 ? 1000 - e->c[c].maint_cut[naval ? 1 : 0] : 1000; }
void econ_set_maint_level(Economy *e, int c, int naval, int level_m)
{
	if (!e || c < 0) return;
	if (level_m < 500) level_m = 500;
	if (level_m > 1000) level_m = 1000;
	e->c[c].maint_cut[naval ? 1 : 0] = (short)(1000 - level_m);
}

/* trade / production efficiency of a country, x1000 (the same figures the monthly economy uses) */
long econ_trade_eff(const Economy *e, int c) { return e && c >= 0 ? trade_eff_of(e, &e->w->country[c]) : 0; }
long econ_prod_eff(const Economy *e, int c)
{
	const Country *co;
	int sr;
	if (!e || c < 0) return 0;
	co = &e->w->country[c]; sr = religion_of(e, co->religion);
	return tech_eff(e, e->infra_eff, R_INFRA, co->infra_tech, sr >= 0 ? (int)e->rel_prod[sr] : 0,
	                dpol(co, 5) + 2 * dpol(co, 1) - 2 * dpol(co, 7) - dpol(co, 0));
}
int econ_religion(const Economy *e, int name_id) { return e ? religion_of(e, name_id) : -1; }

/* the player's loans from the bank (PIW bank, BUTTON_LOANS): the same loan as the automatic one */
int econ_take_loan(Economy *e, int c, long today)
{
	CEcon *ce;
	if (!e || c < 0) return -1;
	ce = &e->c[c];
	if (ce->nloans >= 5 || !econ_can_take_loan(e, c)) return -1;
	{ const Country *co = &e->w->country[c];
	  ce->loan_rate[ce->nloans] = (short)bank_rate(e, c);
	  ce->loan_m[ce->nloans] = LOAN_SIZE_M; ce->loan_due[ce->nloans] = today + 5 * 360; ce->nloans++;
	  e->w->country[c].treasury_m += LOAN_SIZE_M; }
	return 0;
}
/* the player's answer to a due bank loan: pay it back (when the treasury can), else extend it 5 years */
int econ_loan_answer(Economy *e, int c, int i, int pay)
{
	CEcon *ce;
	Country *co;
	long today;
	if (!e || c < 0) return -1;
	ce = &e->c[c]; co = &e->w->country[c];
	if (i < 0 || i >= ce->nloans) return -1;
	today = cal_day(e->w->now.year, e->w->now.month, e->w->now.day);
	if (today < ce->loan_due[i]) return -1;
	if (pay && co->treasury_m >= ce->loan_m[i]) {
		co->treasury_m -= ce->loan_m[i]; world_book_exp(e->w, c, EXP_LOAN, ce->loan_m[i]);
		ce->loan_m[i] = ce->loan_m[ce->nloans - 1]; ce->loan_due[i] = ce->loan_due[ce->nloans - 1]; ce->loan_rate[i] = ce->loan_rate[ce->nloans - 1]; ce->nloans--;
		return 1;
	}
	ce->loan_due[i] = today + 5 * 360;
	ce->ext_count++; if (co->stability > -3) co->stability--;    /* 0x449210 */
	return 0;
}

int econ_loans(const Economy *e, int c, long *amount_m, long *due, int max)
{
	int i, n;
	if (!e || c < 0) return 0;
	n = e->c[c].nloans < max ? e->c[c].nloans : max;
	for (i = 0; i < n; i++) { amount_m[i] = e->c[c].loan_m[i]; due[i] = e->c[c].loan_due[i]; }
	return n;
}
long econ_loan_size_of(const World *w, int c) { const Country *co = &w->country[c]; return LOAN_SIZE_M; }
long econ_loan_size(void) { return 200000L; }

/* war taxes (SPEC_SMALLRULES 1): the button test 0x45d4e0 -> 0 = can raise, else the tooltip reason
 * 3 TWR_WARTAX3 (stability -3), 1 TWR_WARTAX1 (already running), 2 TWR_WARTAX2 (not at war) */
int econ_war_tax_state(const World *w, int c)
{
	const Country *co;
	int i, j, war = 0;
	if (!w || c < 0) return 2;
	co = &w->country[c];
	for (i = 0; i < w->nwar && !war; i++) {
		for (j = 0; j < w->war[i].natt; j++) if (w->war[i].att[j] == c) war = 1;
		for (j = 0; j < w->war[i].ndef; j++) if (w->war[i].def[j] == c) war = 1;
	}
	if (co->stability <= -3) return 3;
	if (co->war_tax > 0) return 1;
	return war ? 0 : 2;
}
/* command 0x56 EXCEPTIONAL_TAXES (0x448eb0): 6 months, war exhaustion +1, inflation +1 %; the caller
 * then makes the immediate revolt roll */
int econ_raise_war_tax(World *w, int c)
{
	Country *co;
	if (econ_war_tax_state(w, c)) return -1;
	co = &w->country[c];
	co->war_tax = 6;
	co->we_m += 1000;
	co->inflation_u += 10000;
	return 0;
}
/* 0x45bc00: in debt with fewer than 3 bank loans, or the loans' monthly interest with a new one at the current rate within
 * 0.75 x last month's income (a loaded game reads e+0x1cc instead: NIEPEWNE, not done); ours keeps the 5-loan cap too */
int econ_can_take_loan(const Economy *e, int c)
{
	const CEcon *ce;
	const Country *co;
	long I = 0;
	int i;
	if (!e || c < 0) return 0;
	ce = &e->c[c]; co = &e->w->country[c];
	if (ce->nloans >= 5) return 0;
	if (co->treasury_m < 0 && ce->nloans < 3) return 1;
	for (i = 0; i < ce->nloans; i++) I += ce->loan_m[i] * LOAN_RATE(ce, i) / 1200;
	I += LOAN_SIZE_M * bank_rate(e, c) / 1200;
	return I <= ce->last_income_m * 3 / 4;
}
int econ_bank_rate(const Economy *e, int c) { return e && c >= 0 ? bank_rate(e, c) : 10; }
int econ_loan_rates(const Economy *e, int c, short *rate, int max)
{
	int i, n;
	if (!e || c < 0) return 0;
	n = e->c[c].nloans < max ? e->c[c].nloans : max;
	for (i = 0; i < n; i++) rate[i] = (short)LOAN_RATE(&e->c[c], i);
	return n;
}

/* the one-time increase (command 0x83 -> 0x84, country 0x450420; SPEC_BOOST): enabled with 200 ducats and the
 * row's year lock over (0x45cc00.., no max-level test); a flat 200 out, +100 progress (none at the top level
 * or at stability +3), at most one level at once, then the row is locked for a year (0x429410) */
int econ_boost_ok(const Economy *e, int c, int row)
{
	const World *w;
	if (!e || c < 0 || row < 0 || row >= ECON_SL_TREAS) return 0;
	w = e->w;
	return w->country[c].treasury_m >= 200000L && cal_day(w->now.year, w->now.month, w->now.day) >= e->c[c].boost_until[row];
}
int econ_boost(Economy *e, int c, int row)
{
	static const int track[ECON_SL_N] = { R_LAND, R_NAVAL, -1, R_TRADE, R_INFRA, -1 };
	World *w = e->w;
	Country *co;
	CEcon *ce;
	if (!econ_boost_ok(e, c, row)) return -1;
	co = &w->country[c]; ce = &e->c[c];
	co->treasury_m -= 200000L;
	world_book_exp(w, c, row == ECON_SL_STAB ? EXP_STAB : row == ECON_SL_TRADE || row == ECON_SL_PROD ? EXP_TRINF : EXP_TECH, 200000L);   /* NIEPEWNE category */
	if (row == ECON_SL_STAB) {
		if (co->stability < 3) {
			long cost = stability_cost_m(e, co, c);
			ce->stab_progress += 100000L;
			if (cost > 0 && ce->stab_progress >= cost) { ce->stab_progress -= cost; co->stability++; }
		}
	} else {
		short *lvl = row == ECON_SL_LAND ? &co->land_tech : row == ECON_SL_NAVAL ? &co->naval_tech : row == ECON_SL_TRADE ? &co->trade_tech : &co->infra_tech;
		long cost = research_cost_m(e, co, track[row], *lvl, w->now.year);
		if (cost > 0) {
			ce->progress[track[row]] += 100000L;
			if (ce->progress[track[row]] >= cost) { ce->progress[track[row]] -= cost; (*lvl)++; }
		}
	}
	ce->boost_until[row] = cal_day(w->now.year, w->now.month, w->now.day) + 360;
	return 0;
}

