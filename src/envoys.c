/* envoys.c - see envoys.h. Sections refer to docs/SPEC_ENVOYS.md. */
#include <string.h>
#include "envoys.h"
#include "script.h"
#include "calendar.h"

/* envoys on their way (SPEC_SMALLRULES 3.7): everything is rolled when sent and applied on arrival */
#define MAXPEND 256
typedef struct { short c, p, kind, ok; long due; } Pend;   /* kind 1 colonist, 2 trader, 3 merchant (p = the CoT) */
static Pend g_pend[MAXPEND]; static int g_npend;
static long today_of(const World *w) { return cal_day(w->now.year, w->now.month, w->now.day); }
static int tp_arrive(World *w, int c, int p, int ok);
static int col_arrive(World *w, int c, int p, int ok);
static Military *g_envmil;                                 /* for the natives' attack after a failed envoy */
static long ai_resv(const World *w, int c);
void env_set_military(Military *m) { g_envmil = m; }
static int rnd100(void);
static void native_later(const World *w, int c, int p)     /* dated today + 2 + rand % 4 days */
{
	if (g_npend >= MAXPEND) return;
	g_pend[g_npend].c = (short)c; g_pend[g_npend].p = (short)p; g_pend[g_npend].kind = 4; g_pend[g_npend].ok = 0;
	g_pend[g_npend].due = cal_day(w->now.year, w->now.month, w->now.day) + 2 + rnd100() % 4; g_npend++;
}
static int pend_add(const World *w, int c, int p, int kind, int ok, int months)
{
	if (g_npend >= MAXPEND) return 0;
	g_pend[g_npend].c = (short)c; g_pend[g_npend].p = (short)p; g_pend[g_npend].kind = (short)kind; g_pend[g_npend].ok = (short)ok;
	g_pend[g_npend].due = today_of(w) + 30L * months; g_npend++;
	return 1;
}

#define ADM (g_adm)                              /* the sender's monarch (3 without one), set per call */
static int g_adm = 3;
static unsigned long g_seed = 12345;
static void set_adm(const World *w, int c) { g_adm = c >= 0 && w->country[c].m_adm ? w->country[c].m_adm : 3; }
static void ai_reset(void);
void env_seed(unsigned long s) { g_seed = s ? s : 1; ai_reset(); }
static int rnd100(void) { g_seed = (g_seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((g_seed >> 16) % 100); }
static long clampl(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }
/* v x (1 + inflation) without a 32-bit overflow on the 68k (long is 32 bits there, 64 on the host:
 * v x 1000000 overflowed and a 208 ducat mission cost 1) */
static long infl(const Country *co, long v) { long i = co->inflation_u / 1000; return v + v / 1000 * i + v % 1000 * i / 1000; }

static int capital(const World *w, int c) { int k = w->country[c].capital; return k > 0 && k < w->nprov ? k : -1; }

static const Economy *g_eco;                 /* for the efficiencies of the travel speed (env_set_econ) */
void env_set_econ(const Economy *e) { g_eco = e; }

static long isqrt(long v) { long r = 0, b = 1L << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

/* 0x436940: the distance from the capital, the x distance wrapped at 0x2500 / 0x4A00 (SPEC_SMALLRULES 3.1).
 * The original measures between the centres of the provinces' bounding boxes; ours between their army
 * positions (NIEPEWNE: close to the centre, the box is not in our data) */
static long cap_dist(const World *w, int c, int p)
{
	int k = capital(w, c);
	long dx, dy;
	if (k < 0 || k == p) return 0;
	dx = w->prov[k].army_x - w->prov[p].army_x; if (dx < 0) dx = -dx; if (dx > 0x2500) dx = 0x4A00 - dx;
	dy = w->prov[k].army_y - w->prov[p].army_y;
	return isqrt(dx * dx + dy * dy);
}

/* d: (difficulty - 2) for a human sender, else 0 (0x4366f0) */
static int diff_d(const World *w, int c) { return w->country[c].human ? w->opt_diff : 0; }

/* days -> whole months of the duration, at least 2 (the colonist / trader minimum) */
static int months_min2(long days) { int m = (int)(days / 30); return m < 2 ? 2 : m; }

/* 0x4366f0 the colonist (SPEC_SMALLRULES 3.2): speed = 5 (E + 1) + post/colony/city bonus - d, at least 1;
 * days = dist x 0.3 / int(speed) */
static int travel_months(const World *w, int c, int p)
{
	const Province *pv = &w->prov[p];
	long E = g_eco ? econ_prod_eff(g_eco, c) : 1000, sp;
	if (E < 10) E = 10;
	sp = 5 * (E + 1000);
	if (pv->tp) sp += 1000;
	else if (pv->owner >= 0 && pv->population > 0 && pv->population < 1000) sp += 2000;
	else if (pv->population >= 1000) sp += 3000;
	sp -= 1000L * diff_d(w, c);
	sp /= 1000; if (sp < 1) sp = 1;
	return months_min2(cap_dist(w, c, p) * 3 / 10 / sp);
}

/* 0x436eb0 the trader (3.3): speed = 10 Et + 4 (+ 0.5 x the post level) - d */
int env_trader_months(const World *w, int c, int p)
{
	long Et = g_eco ? econ_trade_eff(g_eco, c) : 1000, sp;
	if (Et < 10) Et = 10;
	sp = 10 * Et + 4000 + (w->prov[p].tp ? 500L * w->prov[p].tp : 0) - 1000L * diff_d(w, c);
	sp /= 1000; if (sp < 1) sp = 1;
	return months_min2(cap_dist(w, c, p) * 3 / 10 / sp);
}
int env_colonist_months(const World *w, int c, int p) { return travel_months(w, c, p); }

/* ---------------------------------------------------------------- merchants (§1) */
int env_merchant_count(const World *w, int c, int cot)
{
	const Cot *t = &w->cot[cot];
	int k;
	for (k = 0; k < t->n; k++) if (t->tag[k] == c) return t->level[k];
	return 0;
}

int env_merchant_total(const World *w, int cot)
{
	const Cot *t = &w->cot[cot];
	int k, s = 0;
	for (k = 0; k < t->n; k++) s += t->level[k];
	return s;
}

static int someone_has6(const World *w, int cot, int except)
{
	const Cot *t = &w->cot[cot];
	int k;
	for (k = 0; k < t->n; k++) if (t->level[k] >= 6 && t->tag[k] != except) return t->tag[k];
	return -1;
}

/* §1.2 0x4378d0: (10 - ADM) + distance terms, /3, at least 2; x mercantilism, x inflation */
long env_merchant_cost(const World *w, const Economy *e, int c, int cot)
{
	set_adm(w, c);
	const Country *co = &w->country[c];
	int P = w->cot[cot].prov, K = capital(w, c);
	long a = 10 - ADM, b = 0, base;
	if (K > 0) {
		b += w->prov[K].continent != w->prov[P].continent ? 30 : 5;
		b += w->prov[K].region != w->prov[P].region ? 5 : 2;
	} else b += 35;
	if (w->prov[P].owner != c) {
		const short *pc = e ? econ_prov_cots(e) : NULL;
		if (pc && K > 0 && pc[K] == cot) b = b / 3 + 1;                  /* the capital's own CoT */
		else b += 10;
	}
	base = (a + b) * 1000 / 3;
	if (base < 2000) base = 2000;
	base = base * (1000 - (co->policy[3] - 5) * 100) / 1000;              /* mercantilism x1.5 .. x0.5 */
	return infl(co, base);
}

/* §1.3: clamp(50 trade efficiency + 5 stability + 3 ADM + 35, 1, 99) */
int env_merchant_chance(const World *w, const Economy *e, int c)
{
	set_adm(w, c);
	long te = e ? econ_trade_eff(e, c) : 0;
	return (int)clampl(te * 50 / 1000 + 5 * w->country[c].stability + 3 * ADM + 35, 1, 99);
}

static int has_port(const World *w, int c)
{
	int i;
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == c && w->prov[i].kind == 1) return 1;
	return 0;
}

/* §1.1 0x45c600 / 0x45c490, reasons in the tooltip order */
int env_merchant_can(const World *w, const Economy *e, int c, int cot)
{
	const Country *co = &w->country[c];
	int P, K, n, o;
	if (cot < 0 || cot >= w->ncot) return ENV_BADPROV;
	P = w->cot[cot].prov; K = capital(w, c); n = env_merchant_count(w, c, cot); o = w->prov[P].owner;
	if (co->human && !WORLD_KNOWN(w, c, P)) return ENV_REACH;               /* COTIWSENDERR8: discover it first (the AI: not kept in saves, so not checked) */
	if (co->trade_tech < 1) return ENV_TECH;
	if (o != c && K > 0 && w->prov[K].continent != w->prov[P].continent && !has_port(w, c)) return ENV_REACH;
	if (n > 4 && co->trade_tech < 3) return ENV_TECH;
	if (n > 5) return ENV_MONOPOLY;
	if (co->merchants_m < 1000) return ENV_NOSTOCK;
	if (o >= 0 && o != c && (w->dipflag[o][c] & DF_NOTRADE)) return ENV_REFUSED;
	if (env_merchant_cost(w, e, c, cot) > co->treasury_m) return ENV_MONEY;
	return ENV_OK;
}

static void cot_add(World *w, int cot, int c, int d)
{
	Cot *t = &w->cot[cot];
	int k;
	for (k = 0; k < t->n; k++) if (t->tag[k] == c) break;
	if (k == t->n) { if (d <= 0 || t->n >= COT_MERCH) return; t->tag[t->n] = (short)c; t->level[t->n] = 0; t->n++; }
	t->level[k] = (unsigned char)clampl(t->level[k] + d, 0, 6);
	if (!t->level[k]) { t->tag[k] = t->tag[t->n - 1]; t->level[k] = t->level[t->n - 1]; t->n--; }
}

static int same_group(const Economy *e, int ra, int rb)        /* 2 (counter-reformed) and 3 (catholic) count as one */
{
	(void)e;
	if ((ra == 2 || ra == 3) && (rb == 2 || rb == 3)) return 1;
	return ra == rb;
}

/* §1.3 arrival: uncontested = +1 on success; contested (full or a monopolist) = pick the weakest rival
 * by the score of 0x446ebb and win a second roll to push one of its merchants out */
static int merch_arrive(World *w, const Economy *e, int c, int cot, int success);
int env_merchant_send(World *w, const Economy *e, int c, int cot)
{
	set_adm(w, c);
	Country *co = &w->country[c];
	int ok = env_merchant_can(w, e, c, cot), success;
	if (ok != ENV_OK) return ok;
	world_book_exp(w, c, EXP_MER, env_merchant_cost(w, e, c, cot));
	co->treasury_m -= env_merchant_cost(w, e, c, cot);
	co->merchants_m -= 1000;
	success = w->tutorial || rnd100() <= env_merchant_chance(w, e, c);   /* 0x43f6bc: always a success in a tutorial */
	if (pend_add(w, c, w->cot[cot].prov, 5, success, 1)) return ENV_SENT;    /* the merchant always arrives a month later */
	return merch_arrive(w, e, c, cot, success);
}

static int merch_arrive(World *w, const Economy *e, int c, int cot, int success)
{
	Country *co = &w->country[c];
	int total, mono;
	set_adm(w, c);
	total = env_merchant_total(w, cot);
	mono = someone_has6(w, cot, c);
	if (total < 20 && mono < 0) {
		if (!success) return ENV_FAILED;
		world_vp(w, c, VP_ECO, 50);                           /* 0x446df9: an uncontested arrival */
		if (env_merchant_count(w, c, cot) >= 6) { co->merchants_m += 1000; return ENV_SUCCESS; }   /* refunded */
		cot_add(w, cot, c, 1);
		if (env_merchant_count(w, c, cot) > 5) world_news(w, NOTE_OTHER, c, OM_MONOPOLY, w->cot[cot].prov);   /* MONOPOLYOTHER (0x446da7) */
		return ENV_SUCCESS;
	}
	{
		const Cot *t = &w->cot[cot];
		int k, j = -1, rc = econ_religion(e, co->religion);
		long best = -1, te = e ? econ_trade_eff(e, c) : 0;
		if (total >= 20 && mono >= 0) j = mono;
		else for (k = 0; k < t->n; k++) {
			int o = t->tag[k];
			long s;
			if (o == c) continue;
			s = rnd100() % 25 + 50 + 3 * t->level[k] + (10 - ADM);
			if (w->dipflag[o][c] & DF_NOTRADE) s += 20;
			if (w->dipflag[c][o] & DF_NOTRADE) s += 30;
			if (!world_allied(w, c, o, ALLY_DYNASTIC) && !world_allied(w, c, o, ALLY_MILITARY)) s += 5;
			if (!same_group(e, rc, econ_religion(e, w->country[o].religion))) s += 20;
			s += 20 * (te - (e ? econ_trade_eff(e, o) : 0)) / 1000;
			s += (10 - co->policy[3]) - w->country[o].policy[3];
			if (s < 1) s = 1;
			if (w->dipflag[c][o] & DF_TRADEAGR) s = 0;                     /* a trade agreement: never pushed out */
			if (s > best && s > 0) { best = s; j = o; }
		}
		if (j < 0 || !success) return ENV_FAILED;
		{
			long ch = 60 + 50 * ((te - (e ? econ_trade_eff(e, j) : 0)) / 1000) + (co->stability - w->country[j].stability) - co->policy[3] + w->country[j].policy[3];
			if (w->prov[w->cot[cot].prov].controller != c) ch -= co->badboy_m / 5000;
			if (w->dipflag[c][j] & DF_NOTRADE) ch += 15;
			if (!w->tutorial && rnd100() > ch) return ENV_FAILED;   /* 0x4471f0: a tutorial always wins the second roll */
		}
		cot_add(w, cot, j, -1);
		world_rel_add(w, j, c, -5000L);
		world_note(w, NOTE_MERCLOSS, j, w->cot[cot].prov, c);            /* MERCLOSS for the loser */
		if (env_merchant_total(w, cot) < 20) cot_add(w, cot, c, 1);    /* NIEPEWNE: the original may only free the slot */
		return ENV_SUCCESS;
	}
}

/* ---------------------------------------------------------------- colonists (§2) */
static int land_prov(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].id >= 0 && w->prov[p].terrain < 5; }

static int next_to_ours(const World *w, const Military *m, int c, int p, int *city)
{
	short nb[64];
	int n = m ? mil_adjacent(m, p, nb, 64) : 0, k, any = 0;
	if (city) *city = 0;
	for (k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < w->nprov && w->prov[nb[k]].owner == c) {
		any = 1;
		if (city && w->prov[nb[k]].population >= 1000) *city = 1;
	}
	return any;
}

/* 0x436670: the best maneuver of our conquistadors / explorers standing in p (0 none; SPEC_EXPLORERS) */
static int conq_skill(const World *w, int c, int p)
{
	int i, best = 0;
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		if (u->owner == c && !u->dead && u->location == p && u->l_id && (u->l_type == 2 || u->l_type == 3) && u->l_move > best) best = u->l_move;
	}
	return best;
}

long env_colonist_cost(const World *w, const Military *m, int c, int p)
{
	const Province *pv = &w->prov[p];
	long B = pv->owner == c ? pv->population / 100 : 0, v, pp, n = 15;
	int city;
	v = 4 * pv->col_diff - conq_skill(w, c, p) + (travel_months(w, c, p) - B);
	if (v < 1) v = 1;
	pp = 10 * v + 100 + (pv->terrain == 4 ? 10 : 0) + (pv->kind == 1 ? 0 : 10);
	if (next_to_ours(w, m, c, p, &city)) n = city ? 0 : 5;
	return infl(&w->country[c], clampl((n + pp) * 1000 / 6, 20000, 0x7FFFFFFL));
}

static int g_always = -1;                    /* the tutorial's AlwaysSuccess: this country's colonists always succeed (0x567f30) */
void env_always_success(int c) { g_always = c; }

int env_colonist_chance(const World *w, const Economy *e, int c, int p)
{ if (c == g_always) return 100;
	set_adm(w, c);
	const Province *pv = &w->prov[p];
	const Country *co = &w->country[c];
	long ch = 90 - 4 * pv->col_diff;
	int K = capital(w, c);
	if (pv->natives > 0) ch -= 4 * pv->tolerance;               /* aggressiveness = Tolerance +0x13c (SPEC_NATIVES 6) */
	ch += 5 * conq_skill(w, c, p);                             /* a conquistador / explorer there */
	if (pv->terrain == 4 && (K < 0 || w->prov[K].terrain != 4)) ch -= 10;
	if (pv->owner == c) ch += 4 * (pv->population / 100);
	if (pv->religion == co->religion) ch += 5;
	if (world_culture_ok(w, c, pv->culture)) ch += 8;
	if (pv->owner < 0) { long y = w->now.year - 1419; if (y < 0) y = 0; ch += y / 8 - 25 < 0 ? y / 8 - 25 : 0; }
	ch += 10 * (e ? econ_prod_eff(e, c) : 0) / 1000 - 3;
	return (int)clampl(ch, 1, 95);
}

int env_colonist_can(const World *w, const Military *m, int c, int p)
{
	const Province *pv;
	if (!land_prov(w, p)) return ENV_BADPROV;
	pv = &w->prov[p];
	/* 0x45c110: known, not besieged, free or ours and controlled, no city; coastal, or next to an own city, or in Siberia next to own land */
	if (!(pv->owner < 0 || (pv->owner == c && pv->controller == c && pv->population < 1000)) || pv->besieged) return ENV_BADPROV;
	if (!WORLD_KNOWN(w, c, p)) return ENV_REACH;
	if (w->country[c].colonists_m < 1000) return ENV_NOSTOCK;
	if (pv->kind != 1) {
		int city, nb = next_to_ours(w, m, c, p, &city);
		const char *rn = pv->region >= 0 ? world_name(w, pv->region) : NULL;
		if (!(nb && (city || (rn && !strcmp(rn, "Siberia"))))) return ENV_REACH;
	}
	if (env_colonist_cost(w, m, c, p) > w->country[c].treasury_m) return ENV_MONEY;
	return ENV_OK;
}

/* §2.7: a colonist makes a colony of 100 settlers or adds 100; at 1000 it is a city and the remaining
 * natives join (help texts) */
static int col_arrive(World *w, int c, int p, int ok);
int env_colonist_send(World *w, const Economy *e, const Military *m, int c, int p)
{
	Country *co = &w->country[c];
	int ok = env_colonist_can(w, m, c, p);
	if (ok != ENV_OK) return ok;
	world_book_exp(w, c, EXP_COL, env_colonist_cost(w, m, c, p));
	co->treasury_m -= env_colonist_cost(w, m, c, p);
	co->colonists_m -= 1000;
	ok = w->tutorial || rnd100() < env_colonist_chance(w, e, c, p);       /* 0x43f0aa: never fails in a tutorial */
	if (pend_add(w, c, p, 1, ok, travel_months(w, c, p))) return ENV_SENT;
	return col_arrive(w, c, p, ok);
}

static int col_arrive(World *w, int c, int p, int ok)
{
	Country *co = &w->country[c];
	Province *pv = &w->prov[p];
	if (!ok || !(pv->owner < 0 || (pv->owner == c && pv->population < 1000))) return ENV_FAILED;   /* taken meanwhile: lost */
	if (pv->owner < 0) {
		pv->owner = pv->controller = (short)c; pv->population = 100;
		co->nprov_owned++;
		w->owner_epoch++;
		world_vp_establish(w, c, p, 0);                         /* COLONISE: a new colony */
	} else if (pv->tp) { pv->population = 10L * pv->tp + 100; pv->tp = 0; }    /* a trading post becomes a colony (0x47f630) */
	else pv->population += 100;
	/* the natives join at > 900 in the monthly step (revolts.c, SPEC_NATIVES 2.2) */
	if (w->cot_ev) w->cot_ev(w->cot_evu, 2, p);
	return ENV_SUCCESS;
}

/* ---------------------------------------------------------------- missionaries (§3) */
int env_missionary_can(const World *w, int c, int p)
{
	const Province *pv;
	if (!land_prov(w, p)) return ENV_BADPROV;
	pv = &w->prov[p];
	if (pv->owner != c || pv->population <= 0 || pv->religion == w->country[c].religion) return ENV_BADPROV;
	if (w->country[c].missionaries_m < 1000) return ENV_NOSTOCK;
	if (env_missionary_cost(w, c, p) > w->country[c].treasury_m) return ENV_MONEY;
	return ENV_OK;
}

long env_missionary_cost(const World *w, int c, int p)
{
	const Province *pv = &w->prov[p];
	int K = capital(w, c);
	long cost = pv->population * 4 + 100000L + cap_dist(w, c, p) * 3 / 10 / 30 * 1000 / 3 + 4 * pv->income_m;   /* travel 0x439bf0(.., 30) in days */
	if (K > 0 && w->prov[K].continent != pv->continent) cost = cost * 7 / 4;
	if (pv->population < 1000) cost /= 5;
	return infl(&w->country[c], cost);
}

/* 0x45c710 (SPEC_SMALLRULES 3.5): travel dist x 0.3 days, + (10 - ADM) YEARS, + 24 months */
int env_missionary_months(const World *w, int c, int p) { set_adm(w, c); return (int)(cap_dist(w, c, p) * 3 / 10 / 30) + 12 * (10 - ADM) + 24; }

int env_missionary_chance(const World *w, const Economy *e, int c, int p)
{
	set_adm(w, c);
	const Province *pv = &w->prov[p];
	int r = econ_religion(e, w->country[c].religion), pr = econ_religion(e, pv->religion);
	long ch;
	ch = (r == 1 || r == 6) ? 27 : r == 7 ? 50 : r == 8 ? 20 : r == 10 ? 35 : (r == 0 || r == 2 || r == 3 || r == 4 || r == 5 || r == 9) ? 30 : 10;
	ch += -pv->income_m / 3000 + 4 * ADM - (pv->population / 5000 > 20 ? 20 : pv->population / 5000);
	if (!world_culture_ok(w, c, pv->culture) && pr != 7) ch /= 2;
	return (int)clampl(ch, ADM, 99);
}

/* the outcome is rolled now (§3.6); the province changes faith when the time is up (build kind 5) */
int env_missionary_send(World *w, const Economy *e, int c, int p, long today)
{
	Country *co = &w->country[c];
	int ok = env_missionary_can(w, c, p), success;
	Build *b;
	if (ok != ENV_OK) return ok;
	if (w->nbuild >= MAX_BUILD) return ENV_FULL;
	world_book_exp(w, c, EXP_MIS, env_missionary_cost(w, c, p));
	co->treasury_m -= env_missionary_cost(w, c, p);
	co->missionaries_m -= 1000;
	success = rnd100() < env_missionary_chance(w, e, c, p);
	b = &w->build[w->nbuild++];
	memset(b, 0, sizeof *b);
	b->country = (short)c; b->prov = (short)p; b->ship = 5; b->inf = success;
	b->done = today + 30L * env_missionary_months(w, c, p);
	return success ? ENV_SUCCESS : ENV_FAILED;
}

/* 0x5566e0: the AI's Spain colonises only the ToT-SPA half, Portugal only the ToT-POR half (no flag test) */
static int tot_half(const World *w, int c, int p)
{
	int spa = world_country(w, "SPA"), por = world_country(w, "POR");   /* not cached: CAS becomes SPA by a tag swap */
	return !(c == spa && !(w->prov[p].tot & 1)) && !(c == por && !(w->prov[p].tot & 2));
}

/* ---------------------------------------------------------------- the AI's envoys (docs/SPEC_AI_ENVOYS.md)
 * Merchants: one agent per AI country thinking every 30-59 days, at most one merchant plan (roulette
 * over the CoTs, §1.3). Colonists / traders: one agent thinking every 20-39 days, at most one plan
 * (§2.2-2.5). Missionaries: a monthly roll (§3). The King's resource pools are not modelled: a plan is
 * granted on the country's resource tick (20-39 days at peace, 5-9 at war) when the treasury and the
 * stock cover it (NIEPEWNE: the pools also serve the other plans by score). The trade agreement offers
 * of the merchant agent (§1.5) are not here (ai.c). */
typedef struct {
	long mnext, knext, cnext;      /* next merchant think, King resource tick, colonial think (0 = not started) */
	long mneed, cneed;             /* money a plan waits for, ducats (trunc(cost + 1)) */
	long cstart, ctimer, clast;    /* colonial plan: start, next timer; last colonist / trader sent (U+0xe52) */
	short mcot, cprov;             /* merchant plan's CoT (its home province, the plan's +0x80; -1 none), colonial plan's province (0 none) */
	signed char ckind;             /* 1 colonist, 2 trader */
} AiEnv;
static AiEnv g_ai[MAX_COUNTRY];
static int g_ai_init;
#define MAX_TA 256
static struct { short c, o; long day; } g_ta[MAX_TA];   /* agent +0x56[o]: the last trade agreement plan of c toward o */
static int g_nta;
static int (*g_offer)(void *, int, int); static void *g_offer_user;
void env_set_trade_offer(int (*fn)(void *, int, int), void *user) { g_offer = fn; g_offer_user = user; }

static void ai_reset(void)
{
	int c;
	memset(g_ai, 0, sizeof g_ai);
	for (c = 0; c < MAX_COUNTRY; c++) g_ai[c].mcot = -1;
	g_nta = 0;
	g_ai_init = 1;
}
static long rnd_n(long n)                    /* rand % n for n up to 2^30 (two steps of the envoy generator) */
{
	unsigned long a, b;
	if (n <= 1) return 0;
	g_seed = (g_seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; a = (g_seed >> 16) & 0x7FFF;
	g_seed = (g_seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; b = (g_seed >> 16) & 0x7FFF;
	return (long)(((a << 15) | b) % (unsigned long)n);
}
static int ai_at_war(const World *w, int c)
{
	int i, j;
	for (i = 0; i < w->nwar; i++) {
		for (j = 0; j < w->war[i].natt; j++) if (w->war[i].att[j] == c) return 1;
		for (j = 0; j < w->war[i].ndef; j++) if (w->war[i].def[j] == c) return 1;
	}
	return 0;
}
static int in_list(const short *l, int n, short id) { int i; for (i = 0; i < n && l[i] >= 0; i++) if (l[i] == id) return 1; return 0; }
static int nm_is(const World *w, short id, const char *s) { const char *n = id >= 0 ? world_name(w, id) : NULL; return n && script_ieq(n, s); }

/* ---- merchants (§1.3, §1.4) */
static long cot_value_m(const World *w, const Economy *e, int k)   /* C+0x24, the CoT's yearly value x1000 (NIEPEWNE: 12 x the monthly sum) */
{
	const short *pc = econ_prov_cots(e);
	long v = 0;
	int i;
	for (i = 1; i < w->nprov; i++) if (pc[i] == k) v += econ_trade_value_m(e, i);
	return 12 * v;
}

static long cot_weight(const World *w, const Economy *e, int c, int k)   /* 0x53e2d0, -1 excluded */
{
	const Country *co = &w->country[c];
	int P = w->cot[k].prov, o, K = capital(w, c), n;
	long v = 0, cost;
	if (P <= 0 || P >= w->nprov) return -1;
	o = w->prov[P].owner;
	/* 0x45c490 reachability: known, owned, overseas only with a harbour, not refused, monopoly rule */
	if (!WORLD_KNOWN(w, c, P) || o < 0) return -1;
	if (o != c && K > 0 && w->prov[K].continent != w->prov[P].continent && !has_port(w, c)) return -1;
	if (o != c && (w->dipflag[o][c] & DF_NOTRADE)) return -1;
	if (env_merchant_count(w, c, k) > 4 && co->trade_tech < 3) return -1;
	v = cot_value_m(w, e, k);
	cost = env_merchant_cost(w, e, c, k);
	if (cost > 1000) v = v / cost * 1000 + v % cost * 1000 / cost;
	n = env_merchant_total(w, k) - 2 * env_merchant_count(w, c, k);
	if (n < 6) { v = v * 3 / 2; if (n < 2) v = v * 3 / 2; }
	if (o == c) v *= 5;
	else if (w->dipflag[c][o] & DF_TRADEAGR) v = v * 3 / 2;
	else { long d = cap_dist(w, c, P); if (d > 1) v /= d; }      /* 0x43a820 map distance (NIEPEWNE: ours = the army positions) */
	return v / 1000;
}

static void merch_roll(World *w, const Economy *e, int c)          /* 0x53de80 */
{
	static short cand[MAX_COT + 1]; static long wt[MAX_COT + 1];
	const short *pc = econ_prov_cots(e);
	int n = 0, k, K = capital(w, c);
	long sum = 0, r;
	g_ai[c].mcot = -1;
	if (rnd100() > w->country[c].ai_traders) return;
	for (k = 0; k < w->ncot; k++) {
		long v = cot_weight(w, e, c, k);
		if (v >= 1) { cand[n] = (short)k; wt[n++] = v; sum += v; }
	}
	if (K > 0 && pc[K] >= 0) { cand[n] = pc[K]; wt[n++] = 1; sum++; }   /* the capital's CoT once more */
	if (sum <= 0) return;
	r = rnd_n(sum);
	for (k = 0; k < n; k++) if ((r -= wt[k]) < 0) break;
	if (k >= n) return;
	g_ai[c].mcot = w->cot[cand[k]].prov;
	g_ai[c].mneed = env_merchant_cost(w, e, c, cand[k]) / 1000 + 1;
}

/* the plan's update (0x53da70): granted = the treasury covers the money and a merchant is in stock */
static void merch_update(World *w, const Economy *e, int c, int think)
{
	AiEnv *a = &g_ai[c];
	const Country *co = &w->country[c];
	long cost;
	int k;
	for (k = 0; k < w->ncot && w->cot[k].prov != a->mcot; k++) ;
	if (a->mcot < 0 || k >= w->ncot) { merch_roll(w, e, c); return; }   /* the CoT dissolved: cancelled, roll again */
	if (co->treasury_m < a->mneed * 1000 || co->merchants_m < 1000) {
		if (think) merch_roll(w, e, c);                         /* not granted by the next think: cancel, roll again */
		return;
	}
	cost = env_merchant_cost(w, e, c, k) / 1000 + 1;
	if (cost > a->mneed) { a->mneed = cost; return; }
	if (env_merchant_can(w, e, c, k) == ENV_OK) env_merchant_send(w, e, c, k);
	merch_roll(w, e, c);                                        /* executed or cancelled: roll again */
}

/* ---- colonists and traders (§2) */
static int own_nb(const World *w, int c, int p, int *city, int *other, int *war)
{
	short nb[64];
	int n = g_envmil ? mil_adjacent(g_envmil, p, nb, 64) : 0, k, own = 0;
	if (city) *city = 0;
	if (other) *other = 0;
	if (war) *war = 0;
	for (k = 0; k < n; k++) {
		int q = nb[k], o;
		if (q <= 0 || q >= w->nprov || (o = w->prov[q].owner) < 0) continue;
		if (o == c) { own = 1; if (city && w->prov[q].population >= 1000 && !w->prov[q].tp) *city = 1; }
		else { if (other) *other = 1; if (war && mil_at_war(w, o, c)) *war = 1; }
	}
	return own;
}

/* the AI filters of 0x5566e0 / 0x5568b0 (§2.6 steps 2-5; tag 0x52 is NIEZNANE and not applied; Russia's
   byte U+0xfec is NIEZNANE, taken as unset) */
static int ai_filter(const World *w, int c, int p)
{
	const Province *pv = &w->prov[p];
	int r, rus = world_country(w, "RUS");
	if (!tot_half(w, c, p)) return 0;
	if (c == rus && !(nm_is(w, pv->region, "Siberia") && own_nb(w, c, p, NULL, NULL, NULL))) return 0;
	r = g_eco ? econ_religion(g_eco, w->country[c].religion) : -1;
	if ((r == 5 || r == 6) && !nm_is(w, pv->continent, "Asia") && !nm_is(w, pv->continent, "Africa")) return 0;
	return 1;
}
static int may_col(const World *w, int c, int p)                   /* 0x5566e0 -> 0x45c110 */
{
	const Province *pv = &w->prov[p];
	int city;
	if (!land_prov(w, p)) return 0;
	if (pv->owner == c) return 1;
	if (!ai_filter(w, c, p)) return 0;
	if (!WORLD_KNOWN(w, c, p) || pv->besieged) return 0;
	if (pv->owner >= 0 || pv->tp) return 0;                     /* (own provinces returned above) */
	if (pv->kind == 1) return 1;
	if (own_nb(w, c, p, &city, NULL, NULL) && (city || nm_is(w, pv->region, "Siberia"))) return 1;
	return 0;
}
static int may_tp(const World *w, int c, int p)                    /* 0x5568b0 -> 0x45c2e0 */
{
	const Province *pv = &w->prov[p];
	if (!land_prov(w, p) || !ai_filter(w, c, p)) return 0;
	if (!WORLD_KNOWN(w, c, p) || pv->besieged) return 0;
	if (pv->owner >= 0 && !(pv->owner == c && pv->tp)) return 0;     /* a settlement, or someone else's */
	if (pv->tp >= 6) return 0;
	if (pv->owner == c) return pv->controller == c;
	return pv->kind == 1 || own_nb(w, c, p, NULL, NULL, NULL);
}

static long site_value(const World *w, int c, int p)               /* 0x53cee0 */
{
	const Province *pv = &w->prov[p];
	const Country *co = &w->country[c];
	long v = (2 * (pv->goods >= 0 && g_eco ? econ_price_m((Economy *)g_eco, pv->goods) : 0) + 2 * pv->income_m + 250000L) / 1000;
	int other, war, own = own_nb(w, c, p, NULL, &other, &war);
	if (war) return 0;
	if (own) v += co->ai_nb; else if (co->ai_nb > 90) v -= co->ai_nb;
	if (other) v += co->ai_en;
	v -= 3 * pv->col_diff;
	if (pv->natives > 0) v /= (long)pv->tolerance * pv->natives / 80 + 1;
	return 2 * v;
}

static int pref(const World *w, int c, int p)                       /* 0x55dd10 */
{
	const Country *co = &w->country[c];
	const Province *pv = &w->prov[p];
	return (in_list(co->ai_area, 20, pv->area) ? 50 : 0) + (in_list(co->ai_region, 8, pv->region) ? 25 : 0) + (in_list(co->ai_cont, 6, pv->continent) ? 10 : 0);
}

static int colonial_power(const World *w, int c)                   /* 0x43a970 */
{
	int i;
	if (w->country[c].col_rate_m >= 990) return 1;
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].owner == c && w->land[i].l_id && (w->land[i].l_type == 2 || w->land[i].l_type == 3)) return 1;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == c && w->naval[i].l_id && w->naval[i].l_type == 3) return 1;
	return 0;
}

/* 0x53d060: true = colonist */
static int prefer_colonist(const World *w, int c, int p)
{
	const Country *co = &w->country[c];
	const Province *pv = &w->prov[p];
	if (co->trade_tech < 2) return 1;
	if (pv->owner >= 0 && !pv->tp) return 1;
	if (rnd100() < co->ai_tpost) return 0;
	if (env_colonist_chance(w, g_eco, c, p) < env_trader_chance(w, g_eco, c, p)) return 0;
	if (5 * env_trader_cost(w, c, p) < env_colonist_cost(w, g_envmil, c, p)) return 0;
	return !ai_at_war(w, c);
}

enum { L_NONE, L_7A, L_C2, L_B0, L_9E };
static void col_plan_new(World *w, int c, long today)               /* 0x53c9f0 */
{
	static unsigned char lt[MAX_PROV];
	const Country *co = &w->country[c];
	AiEnv *a = &g_ai[c];
	int n[5] = { 0, 0, 0, 0, 0 }, p, k, E, T, best = 0, kind = 0, which, pass;
	long bs;
	for (p = 1; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		lt[p] = L_NONE;
		if (!land_prov(w, p) || !WORLD_KNOWN(w, c, p)) continue;
		if (pv->owner < 0) lt[p] = pref(w, c, p) > 0 ? L_7A : L_C2;
		else if (pv->owner == c) { if (pv->tp) lt[p] = pv->tp < 5 ? L_9E : L_B0; else if (pv->population < 1000) lt[p] = L_B0; }
		n[lt[p]]++;
	}
	k = co->ai_exp / 2; if (k < 1) k = 1;
	T = n[L_B0] + n[L_9E] + n[L_7A] + n[L_C2];
	if (T <= 0) return;
	E = n[L_B0] + n[L_9E];
	for (pass = (E > k + rnd_n(k)) ? 1 : 0; pass < 2 && !best; pass++) {
		if (pass == 0) {                                      /* a new site */
			if (n[L_7A] + n[L_C2] < 1) continue;
			which = n[L_7A] ? L_7A : L_C2;
		} else {                                              /* expand a colony / trading post */
			if (E <= 0) break;
			which = rnd_n(E) <= n[L_B0] ? L_B0 : L_9E;
		}
		for (bs = 0, p = 1; p < w->nprov; p++) {
			long s;
			if (lt[p] != which) continue;
			if (which == L_B0) {
				if (!may_col(w, c, p)) continue;
				s = site_value(w, c, p) - env_colonist_cost(w, g_envmil, c, p) / 1000;
				if (w->prov[p].owner == c && !w->prov[p].tp) s += w->prov[p].population;
			} else if (which == L_9E) {
				if (!may_tp(w, c, p)) continue;
				s = site_value(w, c, p) - env_trader_cost(w, c, p) / 1000;
			} else {
				if (!((co->trade_tech >= 2 && may_tp(w, c, p)) || may_col(w, c, p))) continue;
				s = site_value(w, c, p) - env_trader_cost(w, c, p) / 1000;   /* the trader cost even for a colonist (literal) */
			}
			if (s > bs) { bs = s; best = p; }
		}
		if (best) kind = which == L_B0 ? 1 : which == L_9E ? 2 : prefer_colonist(w, c, best) ? 1 : 2;
	}
	if (!best) return;
	if (kind == 2 && co->trade_tech < 2) kind = 1;                /* the trader creator falls back */
	a->cprov = (short)best; a->ckind = (signed char)kind; a->cstart = today; a->ctimer = today + 32 + rnd_n(10);
	a->cneed = (kind == 1 ? env_colonist_cost(w, g_envmil, c, best) : env_trader_cost(w, c, best)) / 1000 + 1;
}

static void col_update(World *w, int c)                              /* 0x550090 / 0x5504d0 */
{
	AiEnv *a = &g_ai[c];
	const Country *co = &w->country[c];
	long cost;
	int p = a->cprov;
	if (p <= 0 || p >= w->nprov) { a->cprov = 0; return; }
	if (co->treasury_m < a->cneed * 1000 || co->colonists_m < 1000) return;    /* waits (no cancel) */
	cost = (a->ckind == 1 ? env_colonist_cost(w, g_envmil, c, p) : env_trader_cost(w, c, p)) / 1000 + 1;
	if (cost > a->cneed) { a->cneed = cost; return; }
	if (a->ckind == 1 ? may_col(w, c, p) : may_tp(w, c, p)) {
		int r = a->ckind == 1 ? env_colonist_send(w, g_eco, g_envmil, c, p) : env_trader_send(w, g_eco, c, p);
		if (r == ENV_SENT || r == ENV_SUCCESS || r == ENV_FAILED) a->clast = cal_day(w->now.year, w->now.month, w->now.day);
	}
	a->cprov = 0;                                                  /* executed or cancelled */
}

static void col_timer(World *w, int c, long today)                  /* 0x5501a0 / 0x5505e0 */
{
	AiEnv *a = &g_ai[c];
	Country *co = &w->country[c];
	const Province *pv = &w->prov[a->cprov];
	int y0, m0, d0;
	a->ctimer = today + 30 + rnd_n(20);
	if (pv->owner >= 0 && pv->controller != c) { a->cprov = 0; return; }
	if (a->ckind != 2) return;                                     /* the colonist's month-only tests never fire */
	cal_date(a->cstart, &y0, &m0, &d0);
	if (w->now.year - y0 > 1) { a->cprov = 0; return; }
	if (w->now.month - m0 > 6 && co->col_rate_m > 1500 && co->nprov_owned > 5 && co->treasury_m < a->cneed * 1000 && co->colonists_m >= 990) {
		long g = a->cneed * 1000 + 1010 - (co->treasury_m > 0 ? co->treasury_m : 0);   /* GAINMONEY needed + 1.01 - granted */
		co->treasury_m += g;                                        /* NIEPEWNE: granted = what the treasury holds */
	}
}

/* §1.5, 0x53e0e0: a trade agreement offer to the owner of the best CoT, at most once per owner in 6 years */
static void trade_offer(World *w, const Economy *e, int c, long today)
{
	const Country *co = &w->country[c];
	int k, o, i, n, best = -1, emb = 0;
	long bv = 0;
	if (!g_offer || ai_at_war(w, c) || co->mer_rate_m < 5000) return;
	for (o = 0; o < w->ncountry; o++) if (o != c && (w->dipflag[c][o] & DF_NOTRADE)) emb++;
	if (emb > 0) return;
	for (i = n = 0; i < g_nta; i++) if (today - g_ta[i].day < 6L * 360) g_ta[n++] = g_ta[i];   /* older entries no longer block */
	g_nta = n;
	for (k = 0; k < w->ncot; k++) {
		long v, s;
		int P = w->cot[k].prov, seen = 0;
		if (P <= 0 || P >= w->nprov || cot_weight(w, e, c, k) < 1) continue;
		if ((o = w->prov[P].owner) < 0 || o == c) continue;
		for (i = 0; i < g_nta; i++) if (g_ta[i].c == c && g_ta[i].o == o) seen = 1;
		if (seen) continue;
		if ((w->dipflag[c][o] & DF_TRADEAGR) || (w->dipflag[o][c] & DF_NOTRADE) || (w->dipflag[c][o] & DF_NOTRADE)) s = -1000;   /* 0x552ec0 */
		else {
			int q, na = 0;
			for (q = 0; q < w->ncountry; q++) if (q != o && (w->dipflag[o][q] & DF_TRADEAGR)) na++;
			s = -5000L * na + (world_allied(w, c, o, ALLY_DYNASTIC) ? 1000 : 0) + (world_allied(w, c, o, ALLY_MILITARY) ? 5000 : 0) + world_rel(w, c, o) / 10;
			s = s / 1000 * co->mer_rate_m + s % 1000 * co->mer_rate_m / 1000;
			if (s <= 10) s = -1000;
		}
		v = s + cot_value_m(w, e, k);                                   /* the -1 is added, not excluding (literal) */
		if (v > bv) { bv = v; best = o; }
	}
	if (best < 0) return;
	if (g_nta < MAX_TA) { g_ta[g_nta].c = (short)c; g_ta[g_nta].o = (short)best; g_ta[g_nta].day = today; g_nta++; }
	g_offer(g_offer_user, c, best);                                  /* NIEPEWNE: at once (the plan waits for a King pool +0x82 grant) */
}

static void ai_env_country(World *w, int c, long today)
{
	AiEnv *a = &g_ai[c];
	const Country *co = &w->country[c];
	int col;
	if (!a->mnext) { merch_roll(w, g_eco, c); a->mnext = today + 30 + rnd_n(30); }
	if (!a->knext) a->knext = today + 20 + rnd_n(20);
	if (!a->cnext) {
		a->cnext = today + 20 + rnd_n(20);
		if (colonial_power(w, c) && co->ai_exp > 0) { col_plan_new(w, c, today); if (g_envmil) mil_ai_colonial(g_envmil, c, 1); }
	}
	if (today >= a->knext) {                                       /* the King's resource tick */
		a->knext = today + (ai_at_war(w, c) ? 5 + rnd_n(5) : 20 + rnd_n(20));
		if (a->mcot >= 0) merch_update(w, g_eco, c, 0);
		if (a->cprov) col_update(w, c);
	}
	if (today >= a->mnext) {                                       /* 0x53dcf0 */
		a->mnext = today + 30 + rnd_n(30);
		if (a->mcot < 0) merch_roll(w, g_eco, c); else merch_update(w, g_eco, c, 1);
		trade_offer(w, g_eco, c, today);
	}
	if (a->cprov && today >= a->ctimer) col_timer(w, c, today);
	if (today >= a->cnext) {                                       /* 0x53bbc0 */
		a->cnext = today + 20 + rnd_n(20);
		if (co->ai_exp == 0) return;
		if (a->cprov) col_update(w, c);
		col = colonial_power(w, c) || co->colonists_m + co->col_rate_m > 1000;
		if (!col) return;
		if (!a->cprov) col_plan_new(w, c, today);
		if (g_envmil) mil_ai_colonial(g_envmil, c, 0);              /* explorer / conquistador / genocider plans */
	}
}

void env_attach(World *w) { w->ai_resv = ai_resv; }

/* the money the waiting merchant / colonist plans hold (military.c and buildings.c spend only above it) */
static long ai_resv(const World *w, int c)
{
	const AiEnv *a;
	(void)w;
	if (c < 0 || c >= MAX_COUNTRY) return 0;
	a = &g_ai[c];
	return (a->mcot >= 0 ? a->mneed * 1000 : 0) + (a->cprov ? a->cneed * 1000 : 0);
}

static void ai_env_day(World *w, long today)
{
	int c;
	if (!g_ai_init) ai_reset();
	if (!g_eco || !g_envmil) return;
	for (c = 0; c < w->ncountry; c++) {
		const Country *co = &w->country[c];
		if (co->human || co->nprov_owned <= 0 || capital(w, c) < 0) { g_ai[c].mcot = -1; g_ai[c].cprov = 0; continue; }
		ai_env_country(w, c, today);
	}
}

/* §3, 0x557ab0: once a month, a missionary into the best own heretic city with probability sum / 2500 */
void env_ai_month(World *w, const Economy *e, const Military *m, long today)
{
	int c, p;
	short eur = -1;
	(void)m;
	for (p = 1; p < w->nprov && eur < 0; p++) if (nm_is(w, w->prov[p].continent, "Europe")) eur = w->prov[p].continent;
	for (c = 0; c < w->ncountry; c++) {
		Country *co = &w->country[c];
		int rc, best = 0, bs = 0;
		long sum = 0;
		if (co->human || co->nprov_owned <= 0 || !e) continue;
		rc = econ_religion(e, co->religion);
		if (rc < 0 || rc > 6 || co->missionaries_m <= 999) continue;
		for (p = 1; p < w->nprov; p++) {
			const Province *pv = &w->prov[p];
			int s, rp, ch;
			if (pv->owner != c) continue;
			rp = econ_religion(e, pv->religion);
			if (same_group(e, rp, rc)) continue;
			if (pv->tp || pv->population < 1000) s = -1;
			else {
				ch = env_missionary_chance(w, e, c, p);
				s = 1 + (ch > 30) + (ch > 50) + (pv->continent != eur ? 4 : 0) + (rp == 7 ? 2 : 0);
			}
			if (s > bs) { bs = s; best = p; }
			sum += s;
		}
		if (ai_at_war(w, c)) sum = 0;
		if (rnd_n(2500) < sum && best) {
			co->treasury_m += env_missionary_cost(w, c, best) + 1000;     /* GAINMONEY cost + 1 first: free for the AI */
			env_missionary_send(w, e, c, best, today);
		}
	}
}

void env_day(World *w, long today)
{
	int i, n = 0;
	ai_env_day(w, today);
	for (i = 0; i < g_npend; i++) {
		Pend q = g_pend[i];
		int r, ea, pp;
		if (q.due > today) { g_pend[n++] = q; continue; }
		if (q.c < 0 || q.c >= w->ncountry) continue;
		if (q.kind == 4) {                                   /* a native attack after a failed colonist / trader */
			if (g_envmil && q.p > 0 && q.p < w->nprov) mil_native_attack(g_envmil, q.p);
			continue;
		}
		if (q.kind == 5) {                                   /* by the CoT's province: gone meanwhile = lost */
			int k;
			for (k = 0; k < w->ncot && w->cot[k].prov != q.p; k++) ;
			if (k >= w->ncot) continue;
			q.p = (short)k; q.kind = 3;
		}
		if (q.kind == 3) {
			if (q.p < 0 || q.p >= w->ncot) continue;
			r = merch_arrive(w, g_eco, q.c, q.p, q.ok); ea = r == ENV_SUCCESS ? EA_MERCSUCC : EA_MERCFAIL; pp = w->cot[q.p].prov;
		} else if (q.kind == 1) {
			int had = q.p > 0 && q.p < w->nprov && w->prov[q.p].owner == q.c;
			if (q.p <= 0 || q.p >= w->nprov) continue;
			r = col_arrive(w, q.c, q.p, q.ok); ea = r != ENV_SUCCESS ? EA_COLFAIL : had ? EA_COLEXPAND : EA_COLNEW; pp = q.p;
			if (r != ENV_SUCCESS && w->prov[q.p].natives > 0 && rnd100() < 10 * w->prov[q.p].tolerance) native_later(w, q.c, q.p);
		} else {
			int had = q.p > 0 && q.p < w->nprov && w->prov[q.p].tp > 0;
			if (q.p <= 0 || q.p >= w->nprov) continue;
			r = tp_arrive(w, q.c, q.p, q.ok); ea = r != ENV_SUCCESS ? EA_TPFAIL : had ? EA_TPEXPAND : EA_TPNEW; pp = q.p;
			if (r != ENV_SUCCESS && w->prov[q.p].natives > 0 && rnd100() < 10 * w->prov[q.p].tolerance) native_later(w, q.c, q.p);
		}
		world_note(w, NOTE_ENVOY, q.c, pp, ea);
	}
	g_npend = n;
}

void env_save(FILE *f)
{
	int i;
	fprintf(f, "amieu2_envoys = { seed = %lu pend = {", g_seed);
	for (i = 0; i < g_npend; i++) fprintf(f, " %d %d %d %d %ld", g_pend[i].c, g_pend[i].p, g_pend[i].kind, g_pend[i].ok, g_pend[i].due);
	fprintf(f, " }\n\tai = {");
	for (i = 0; i < MAX_COUNTRY; i++) {
		const AiEnv *a = &g_ai[i];
		if (!a->mnext && !a->knext && !a->cnext) continue;
		fprintf(f, " %d %ld %ld %ld %ld %ld %ld %ld %ld %d %d %d", i, a->mnext, a->knext, a->cnext, a->mneed, a->cneed, a->cstart, a->ctimer, a->clast, a->mcot, a->cprov, a->ckind);
	}
	fprintf(f, " }\n\tta = {");
	for (i = 0; i < g_nta; i++) fprintf(f, " %d %d %ld", g_ta[i].c, g_ta[i].o, g_ta[i].day);
	fprintf(f, " } }\n");
}
void env_restore(const SNode *root)
{
	const SNode *b = script_get(root, "amieu2_envoys"), *q;
	g_npend = 0;
	ai_reset();
	if (!b) return;
	g_seed = (unsigned long)script_int(b, "seed", 12345);
	q = script_get(b, "pend"); q = q ? q->child : NULL;
	while (q && g_npend < MAXPEND) {
		long v[5]; int k = 0;
		for (; q && k < 5; q = q->next) if (!q->key && q->val) v[k++] = parse_int(q->val);
		if (k < 5) break;
		g_pend[g_npend].c = (short)v[0]; g_pend[g_npend].p = (short)v[1]; g_pend[g_npend].kind = (short)v[2]; g_pend[g_npend].ok = (short)v[3]; g_pend[g_npend].due = v[4];
		g_npend++;
	}
	q = script_get(b, "ai"); q = q ? q->child : NULL;
	while (q) {
		long v[12]; int k = 0;
		for (; q && k < 12; q = q->next) if (!q->key && q->val) v[k++] = parse_int(q->val);
		if (k < 12) break;
		if (v[0] >= 0 && v[0] < MAX_COUNTRY) {
			AiEnv *a = &g_ai[v[0]];
			a->mnext = v[1]; a->knext = v[2]; a->cnext = v[3]; a->mneed = v[4]; a->cneed = v[5]; a->cstart = v[6];
			a->ctimer = v[7]; a->clast = v[8]; a->mcot = (short)v[9]; a->cprov = (short)v[10]; a->ckind = (signed char)v[11];
		}
	}
	q = script_get(b, "ta"); q = q ? q->child : NULL;
	while (q && g_nta < MAX_TA) {
		long v[3]; int k = 0;
		for (; q && k < 3; q = q->next) if (!q->key && q->val) v[k++] = parse_int(q->val);
		if (k < 3) break;
		g_ta[g_nta].c = (short)v[0]; g_ta[g_nta].o = (short)v[1]; g_ta[g_nta].day = v[2]; g_nta++;
	}
}

/* ---------------------------------------------------------------- traders (§2.6) */
long env_trader_cost(const World *w, int c, int p)
{
	const Province *pv = &w->prov[p];
	long f = 3L * pv->negot - conq_skill(w, c, p) + 20 + (pv->religion == w->country[c].religion ? 5 : 0), v;
	v = pv->tp ? f * 200 - 2000L * pv->tp : f * 1000 / 3;                  /* x1000: f x 0.2 - 2 level, or f / 3 */
	if (v < 5000) v = 5000;
	return infl(&w->country[c], v);
}

int env_trader_chance(const World *w, const Economy *e, int c, int p)
{
	const Province *pv = &w->prov[p];
	long ch = pv->natives > 0 ? 5L * (16 - pv->negot) : 60;
	set_adm(w, c);
	if (pv->terrain == 4) ch -= 10;
	ch += 50 * ((e ? econ_trade_eff(e, c) : 0) / 1000);                     /* int(trade efficiency): 0 below 100 % */
	if (pv->tp) ch += 5 * pv->tp; else ch -= pv->col_diff;
	if (pv->owner < 0) { long y = w->now.year - 1419; if (y < 0) y = 0; ch += y / 8 - 10 < 0 ? y / 8 - 10 : 0; }
	return (int)clampl(ch, 1, 95);
}

int env_trader_can(const World *w, int c, int p)
{
	const Province *pv;
	if (!land_prov(w, p)) return ENV_BADPROV;
	pv = &w->prov[p];
	/* 0x45c2e0: known, no settlement, not besieged; our own trading post (controlled, below 6), or free land on the coast or next to ours */
	if (!WORLD_KNOWN(w, c, p)) return ENV_REACH;
	if (w->country[c].trade_tech < 2) return ENV_TECH;                       /* CCWTPLEVEL */
	if (pv->owner >= 0 && !(pv->owner == c && pv->tp)) return ENV_BADPROV;   /* a colony / city / someone else's */
	if (pv->besieged || (pv->owner == c && pv->controller != c)) return ENV_BADPROV;
	if (pv->tp >= 6) return ENV_FULL;                                        /* CCWTPMAXXX */
	if (pv->owner < 0 && pv->kind != 1 && !next_to_ours(w, g_envmil, c, p, NULL)) return ENV_REACH;
	if (w->country[c].colonists_m < 1000) return ENV_NOSTOCK;                /* traders use the colonist stock */
	if (env_trader_cost(w, c, p) > w->country[c].treasury_m) return ENV_MONEY;
	return ENV_OK;
}

/* a trader makes a level-1 trading post (10 people) or adds a level (+10) */
static int tp_arrive(World *w, int c, int p, int ok);
int env_trader_send(World *w, const Economy *e, int c, int p)
{
	Country *co = &w->country[c];
	int ok = env_trader_can(w, c, p);
	if (ok != ENV_OK) return ok;
	world_book_exp(w, c, EXP_TP, env_trader_cost(w, c, p));
	co->treasury_m -= env_trader_cost(w, c, p);
	co->colonists_m -= 1000;
	ok = w->tutorial || rnd100() < env_trader_chance(w, e, c, p);         /* 0x43f322 */
	if (pend_add(w, c, p, 2, ok, env_trader_months(w, c, p))) return ENV_SENT;
	return tp_arrive(w, c, p, ok);
}

static int tp_arrive(World *w, int c, int p, int ok)
{
	Country *co = &w->country[c];
	Province *pv = &w->prov[p];
	if (!ok || !(pv->owner < 0 || (pv->owner == c && pv->tp && pv->tp < 6))) return ENV_FAILED;
	if (pv->owner < 0) { pv->owner = pv->controller = (short)c; pv->population = 0; co->nprov_owned++; w->owner_epoch++; }
	if (!pv->tp) world_vp_establish(w, c, p, 1);              /* ESTABLISH_TRADEPOST: a new one */
	pv->tp++;
	pv->population += 10;
	if (w->cot_ev) w->cot_ev(w->cot_evu, 2, p);
	return ENV_SUCCESS;
}
