/* ai.c - the diplomatic AI as decoded from eu2.exe v1.09 (docs/SPEC_AI.md; addresses in the comments).
 * Floats of the exe are fixed point here: values marked _m are x1000. What the exe computes from
 * fields this port does not have is approximated and marked NIEPEWNE. Portable C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diplo_int.h"
#include "ai.h"
#include "buildings.h"
#include "script.h"
#include "calendar.h"
/* the exe compares country indices (country.csv row + 1); ours are the rows, so the tags are compared */
#define TAGIS(w, c, t) (!strcmp((w)->country[c].tag, (t)))
#ifdef AI_PROF
#include <time.h>
long ai_prof[8];
static long nowus(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000L + t.tv_nsec / 1000; }
#define PT0 long pt0 = nowus()
#define PT(i) do { long t1 = nowus(); ai_prof[i] += t1 - pt0; pt0 = t1; } while (0)
#else
/* a section clock the bench can set (main_amiga.c): ai_prof[] in its units */
long ai_prof[8];
unsigned long (*ai_clock)(void);
#define PT0 unsigned long pt0 = ai_clock ? ai_clock() : 0
#define PT(i) do { if (ai_clock) { unsigned long t1 = ai_clock(); ai_prof[i] += (long)(t1 - pt0); pt0 = t1; } } while (0)
#endif

enum { AIM_DOMINANCE = 0, AIM_BUILDUP = 1, AIM_COLONISE = 2 };
#define AI_NLAST 4096
#define AI_NDM 240   /* controller+0x40 (0x5421a0) */

struct AiState {
	signed char mode[MAX_COUNTRY];
	long mode_day[MAX_COUNTRY];             /* controller+0x44: day the mode was chosen */
	short enemy[MAX_COUNTRY];               /* controller+0x40 of the diplomat: worst enemy, -1 none */
	long peace_day[MAX_COUNTRY];            /* country+0x38a: last day at peace (0 = not set) */
	long war_day[MAX_COUNTRY];              /* country+0xe3e: last day at war (0 = not set) */
	short lu[AI_NLAST], lt[AI_NLAST];       /* R+0x18 per (country + 1, target): last diplomatic action */
	long lday[AI_NLAST];
	/* this month (not saved) */
	unsigned char coast[MAX_COUNTRY];
	short ships[MAX_COUNTRY], rank[MAX_COUNTRY], rebels[MAX_COUNTRY], cont[MAX_COUNTRY];
	long fs[MAX_COUNTRY], fn[MAX_COUNTRY], fe[MAX_COUNTRY];   /* +0x364 / +0x368 / +0x360, x1000 of the exe's units */
	short core_head[MAX_COUNTRY];
	short core_next[MAX_PROV * 8], core_prov[MAX_PROV * 8];
	short held[MAX_COUNTRY];                /* scratch: cores of the thinking country held by each country */
	short lord[MAX_COUNTRY];                /* overlord of each country, -1 (rebuilt with the ties) */
	short cur;                              /* the country thinking now (its flags below), -1 */
	short live[MAX_COUNTRY]; int nlive;     /* this month: countries with land and a valid tag (0x45bce0), in index order */
	short lidx[MAX_COUNTRY];                /* country -> index in live, -1 */
	short dcap[MAX_COUNTRY];                /* the capitals the distance table was built for */
	short dm[AI_NDM][AI_NDM];               /* capital distances x10 between live countries (0x43a8a5), -1 not built */
	int ndm;
	long next_think[MAX_COUNTRY];           /* the diplomat's next tick (0x5402fb), 0 = now */
	int cache_month;                        /* month the per-month data above was built for (0 none) */
	unsigned char trf[MAX_COUNTRY], foe[MAX_COUNTRY];   /* cur: truce with / at war with */
	long relu[MAX_COUNTRY];                 /* cur: its relation toward each country (x1000) */
};

static long today_of(const Diplo *d) { return d->today; }
#define BITM(m, a, b) (((m)[((long)(a) * MAX_COUNTRY + (b)) >> 3] >> (((long)(a) * MAX_COUNTRY + (b)) & 7)) & 1)
#define NB(d, a, b) BITM((d)->nb, a, b)
#define ALIVE(w, c) ((w)->country[c].nprov_owned > 0)

AiState *ai_new(void)
{
	AiState *a = (AiState *)calloc(1, sizeof *a);
	int i;
	if (!a) return NULL;
	for (i = 0; i < MAX_COUNTRY; i++) { a->enemy[i] = -1; a->mode[i] = AIM_DOMINANCE; a->lord[i] = -1; a->lidx[i] = -1; a->core_head[i] = -1; }
	a->cur = -1;
	return a;
}
void ai_free(AiState *a) { free(a); }

/* 0x45bce0: not the exe's empty slot 0 nor its special countries 0x14f..0x153 (REB / PIR / NAT ...); ours are indexed by the
 * country.csv row, so country 0 is ENG and the specials are w->reb / nat / pir */
static int valid_tag_w(const World *w, int i) { return i >= 0 && i < w->ncountry && i != w->reb && i != w->nat && i != w->pir; }
#define valid_tag(i) valid_tag_w(w, (i))
static int alive(const World *w, int c) { return c >= 0 && c < w->ncountry && w->country[c].nprov_owned > 0; }

/* integer square root */
static long isqrt(long v) { long r = 0, b = 1L << 30; if (v <= 0) return 0; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

/* 0x43a8a5: distance of the capitals, sqrt(dx^2 + dy^2) x 0.01, x wrapping at 0x4a00; 10000 without a capital.
 * Returned x10 (0.1 units). */
static long dist10_raw(const World *w, int a, int b)
{
	int ca = w->country[a].capital, cb = w->country[b].capital;
	long dx, dy;
	if (ca <= 0 || cb <= 0 || ca >= w->nprov || cb >= w->nprov) return 100000L;
	if (ca == cb) return 0;
	dx = w->prov[ca].city_x - w->prov[cb].city_x; if (dx < 0) dx = -dx;
	if (dx > 0x2500) dx = 0x4a00 - dx;
	dy = w->prov[ca].city_y - w->prov[cb].city_y;
	return isqrt(dx * dx + dy * dy) / 10;
}

static AiState *g_dm;                                      /* the table dist10 reads (set by ai_month) */
static long dist10(const World *w, int a, int b)
{
	if (g_dm && g_dm->lidx[a] >= 0 && g_dm->lidx[b] >= 0 && g_dm->lidx[a] < g_dm->ndm && g_dm->lidx[b] < g_dm->ndm) return g_dm->dm[g_dm->lidx[a]][g_dm->lidx[b]];
	return dist10_raw(w, a, b);
}

static int truce(const Diplo *d, int a, int b)               /* 0x45f5d0: last peace < 61 months */
{
	if (d->ai->cur == a) return d->ai->trf[b];
	if (d->ai->cur == b) return d->ai->trf[a];
	return d->p && peace_truce(d->p, a, b, today_of(d));
}
#define REL(d, a, b) ((d)->ai->cur == (a) ? (d)->ai->relu[b] : (d)->ai->cur == (b) ? (d)->ai->relu[a] : world_rel((d)->w, a, b))
static int war_with(const Diplo *d, int a, int b)
{
	if (d->ai->cur == a) return d->ai->foe[b];
	if (d->ai->cur == b) return d->ai->foe[a];
	return mil_at_war(d->w, a, b);
}
#define has_cb(w, a, b) diplo_cb(d, (a), (b))                /* 0x4ce3b0 (diplo.c); every caller has its Diplo d */
static int allied(const Diplo *d, int a, int b) { return d->milally[a] >= 0 && d->milally[a] == d->milally[b]; }
static AiState *g_cur;                                      /* the state the helpers below read (set by ai_month) */
static int lord_of(const World *w, int c) { return g_cur ? g_cur->lord[c] : world_overlord(w, c); }
static int vassal_of(const World *w, int v, int lord) { return lord_of(w, v) == lord; }
static int year_now(const World *w) { return w->now.year; }
static int dip_of(const Country *c) { return c->m_dip ? c->m_dip : 3; }
static int mil_of(const Country *c) { return c->m_mil ? c->m_mil : 3; }

/* 0x45af40: badboy over the limit DIP + 30 + (year - 1419)/20 (+ year - 1790 after 1790) */
int ai_badboy_limit(const World *w, int c)
{
	int y = year_now(w);
	long lim = dip_of(&w->country[c]) + 30 + (y - 1419) / 20;
	if (y > 1790) lim += y - 1790;
	return (int)lim;
}
static int over_limit(const World *w, int c) { return w->country[c].badboy_m > ai_badboy_limit(w, c) * 1000L; }

static int known_cap(const Diplo *d, int u, int x)   /* the exe: x's capital province is discovered by u */
{
	const World *w = d->w;
	int cap = w->country[x].capital;
	if (cap <= 0 || cap >= w->nprov) return 0;
	/* the AI's discoveries are not saved (savegame.c), so they cannot decide here: a neighbour or the same
	 * continent counts as known (NIEPEWNE) */
	return NB(d, u, x) || d->ai->cont[u] == d->ai->cont[x];
}

/* ---------------------------------------------------------------- monthly bookkeeping */
static long mul_k(long stat, long n) { return stat * (n / 1000) + stat * (n % 1000) / 1000; }   /* stat x n / 1000 without overflow */
/* 0x43c010 (docs/SPEC_AI_STRENGTH.md): FS / FN / FE, monthly for every country, x1000 of the original's units */
static void strength(Diplo *d, int c)
{
	World *w = d->w;
	AiState *a = d->ai;
	const Country *co = &w->country[c];
	long ls[6] = { 0, 0, 0, 0, 0, 0 }, ns[4] = { 0, 0, 0, 0 }, mor[2] = { 0, 0 };
	long inc = co->inc_m / 1000, money = 12 * inc + co->treasury_m / 1000, ninf = 0, nart = 0, nwar = 0, ngal = 0, gl1 = 0, gl2 = 0;
	long ci = 1, cc = 1, ca = 1, infp, cavp, artp, troop, ships, rl, rn, fs, fn, net, fe, li = co->loanint;
	int i, nl = co->nloans_b, months, adm = co->m_adm ? co->m_adm : 3;
	if (d->m) {
		mil_strength_inputs(d->m, c, ls, ns, mor);
		ci = mil_recruit_cost(d->m, c, 0) / 1000; cc = mil_recruit_cost(d->m, c, 1) / 1000; ca = mil_recruit_cost(d->m, c, 2) / 1000;
		if (ci < 1) ci = 1;
		if (cc < 1) cc = 1;
		if (ca < 1) ca = 1;
	}
	for (i = 0; i < w->nland; i++) {                         /* owned (vf 0x388 / 0x390) + the leaders of the armies (rank < 5) */
		const LandUnit *u = &w->land[i];
		if (u->dead || u->owner != c) continue;
		ninf += u->infantry; nart += u->artillery;
		if (u->l_id && u->l_rank < 5) {
			long f = u->l_fire, s = u->l_shock;
			if (u->l_type == 2 && u->location > 0 && u->location < w->nprov && d->eur_cont && w->prov[u->location].continent == d->eur_cont - 1) f = s = 0;   /* a conquistador in Europe */
			gl1 += f + s - 5; gl2 += u->l_siege;
		}
	}
	for (i = 0; i < w->nbuild; i++) if (w->build[i].country == c && !w->build[i].ship) { ninf += w->build[i].inf; nart += w->build[i].art; }   /* + queued */
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == c) { nwar += w->naval[i].warships; ngal += w->naval[i].galleys; }
	/* fleets: their leaders count only if the unit list +0x124c holds fleets too (NIEPEWNE: SPEC_BOOST has +0x1260 for them) - not counted */
	infp = ninf + 500 * money / ci;
	cavp = ninf + 400 * money / cc;                          /* the infantry count again (literal) */
	artp = nart + 10 * money / ca;
	troop = mul_k(ls[0] + ls[3], infp) + mul_k(ls[1] + ls[4], cavp) + mul_k(ls[2] + 10 * ls[5], artp);   /* 0.001 x stat x n, x1000 = stat(x1000) x n / 1000 */
	ships = nwar * (ns[0] + ns[1]) + 33 * ngal * (ns[2] + ns[3]) / 100;
	rl = inc >= 1 ? co->maintl_m / inc : co->maintl_m;       /* the ratio x1000, or the raw sum below 1 ducat */
	rn = inc >= 1 ? co->maintn_m / inc : co->maintn_m;
	fs = 1000L * co->land_tech + 20 * mor[0] + troop + 1000 * gl1 - 5 * rl / 2 + 2000L * mil_of(co) + 1000 * gl2;
	{	int yards = 0, p;
		for (p = 1; p < w->nprov; p++) if (w->prov[p].owner == c && (w->prov[p].bflags & PB_SHIPYARD)) yards++;
		fn = 5 * mor[1] + 1000L * (co->hports + yards) + rn + ships;
	}
	for (i = 0; i < d->nloan; i++) if (d->loan[i].borrower == c) { li += d->loan[i].amount_m / 1000 * d->loan[i].rate; nl++; }
	net = 120 * inc - 120 * ((co->maintl_m + co->maintn_m) / 1000) - li / 10;
	months = co->bankrupt_day > 0 ? (int)((d->today - co->bankrupt_day) / 30) : 9999;
	fe = (net + 10 * money + 100 * co->te_m + 100 * co->pe_m) * (adm + 50) / 100 + (months < 12 ? -10000 : months < 60 ? -5000 : 0) + 1000L * nl;
	a->fs[c] = fs < 1000 ? 1000 : fs;
	a->fn[c] = fn < 1000 ? 1000 : fn;
	a->fe[c] = fe < 1000 ? 1000 : fe;
}

long ai_strength(Diplo *d, int c, int naval)      /* for peace.c: FS / FN of c, this month's (+0x364 / +0x368, set by ai_month) */
{
	if (!d || !d->ai || c < 0 || c >= d->w->ncountry) return 1000;
	return naval ? d->ai->fn[c] : d->ai->fs[c];
}

static void ai_refresh(Diplo *d)
{
	World *w = d->w;
	AiState *a = d->ai;
	int i, k;
	long today = today_of(d);
	memset(a->coast, 0, sizeof a->coast); memset(a->ships, 0, sizeof a->ships); memset(a->rebels, 0, sizeof a->rebels);
	for (i = 0; i < w->ncountry; i++) a->core_head[i] = -1;
	{	int n = 0;
		for (i = 1; i < w->nprov; i++) {
			const Province *p = &w->prov[i];
			if (p->owner >= 0) {
				if (p->kind == 1) a->coast[p->owner] = 1;
				if (w->reb >= 0 && p->controller == w->reb) a->rebels[p->owner]++;
			}
			for (k = 0; k < 8; k++) if (p->cores[k] >= 0 && p->cores[k] < w->ncountry && n < MAX_PROV * 8) {
				a->core_prov[n] = (short)i; a->core_next[n] = a->core_head[p->cores[k]]; a->core_head[p->cores[k]] = (short)n; n++;
			}
		}
	}
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner >= 0) a->ships[w->naval[i].owner] += w->naval[i].warships + w->naval[i].galleys;
	for (i = 0; i < w->ncountry; i++) {
		const Country *c = &w->country[i];
		int cap = c->capital;
		strength(d, i);
		a->cont[i] = (short)(cap > 0 && cap < w->nprov ? w->prov[cap].continent : -1);
		if (c->nprov_owned > 0) { if (!d->atwar[i]) a->peace_day[i] = today; else a->war_day[i] = today; }   /* 0x450ad0 */
	}
	a->nlive = 0;
	for (i = 0; i < w->ncountry; i++) a->lidx[i] = -1;
	for (i = 0; i < w->ncountry; i++) if (w->country[i].nprov_owned > 0 && valid_tag(i)) { a->lidx[i] = (short)a->nlive; a->live[a->nlive++] = (short)i; }
	{	/* the distance table: rebuilt when the live set or a capital changed */
		int n = a->nlive < AI_NDM ? a->nlive : AI_NDM, same = n == a->ndm;
		for (i = 0; i < n && same; i++) if (a->dcap[i] != w->country[a->live[i]].capital || a->dm[i][i] != (short)a->live[i]) same = 0;
		if (!same) {
			int j;
			for (i = 0; i < n; i++) {
				a->dcap[i] = w->country[a->live[i]].capital;
				for (j = 0; j < i; j++) { long v = dist10_raw(w, a->live[i], a->live[j]); a->dm[i][j] = a->dm[j][i] = (short)(v > 32000 ? 32000 : v); }
			}
			for (i = 0; i < n; i++) a->dm[i][i] = (short)a->live[i];    /* the diagonal tags the slot (distance to itself is never asked) */
			a->ndm = n;
		}
	}
	for (i = 0; i < w->ncountry; i++) a->rank[i] = 999;     /* country+0x370: 1 + countries with more VP (NIEPEWNE score) */
	for (i = 0; i < a->nlive; i++) {
		int r = 1, j;
		long v = w->country[a->live[i]].vp;
		for (j = 0; j < a->nlive; j++) if (w->country[a->live[j]].vp > v) r++;
		a->rank[a->live[i]] = (short)r;
	}
}

static int months_since(long today, long day) { return day > 0 ? (int)((today - day) / 30) : 0; }

/* ---------------------------------------------------------------- scores */
/* H, 0x535810: how much U likes X (low = enemy), x1000 */
/* A, 0x535600: how good X is as an ally of U against E (E < 0 none), x1000 */
static long score_A(Diplo *d, int u, int x, int e)
{
	World *w = d->w;
	AiState *a = d->ai;
	long s = REL(d, u, x) / 1000 * ((a->fs[x] + a->fn[x]) / 1000) * 100;   /* rel x strength x 0.1 */
	if (s > 300000L) s = 300000L; else if (s < -300000L) s = -300000L;
	if (neighbours(d, u, x)) s += 30000;
	if (e >= 0 && neighbours(d, x, e)) s += 40000;
	if (CHRISTIAN(d->rel_idx[u]) && !CHRISTIAN(d->rel_idx[x])) s -= 200000L;   /* unless world flag 4 (0x4d16a0, NIEZNANE) */
	if (e >= 0) s = s / 1000 * (diplo_gshare(d, x, e) + 100);   /* x (X's border share with E (+0x14de) + 0.1) */
	if (e >= 0 && neighbours(d, u, e) && (!a->coast[u] || !a->coast[x])) return -1000;
	if (!neighbours(d, u, x) && !(e >= 0 && neighbours(d, x, e))) s -= 300000L;
	if (w->country[u].elector && w->country[x].elector) s += 100000L;   /* two electors (0x5356ce) */
	return s;
}

/* ---------------------------------------------------------------- the war predicate, 0x540fc0 */
static int land_access_route(const Diplo *d, int u, int t)   /* a country bordering both that gives us access */
{
	const World *w = d->w;
	int k;
	for (k = 0; k < w->ncountry; k++)
		if (alive(w, k) && neighbours(d, u, k) && neighbours(d, k, t) && (w->dipflag[k][u] & DF_ACCESS)) return 1;
	return 0;
}

/* 0x484320: a neighbour of p is owned or controlled by c */
static int borders_c(const Diplo *d, int p, int c)
{
	const World *w = d->w;
	short nb[64];
	int n = d->m ? mil_adjacent(d->m, p, nb, 64) : 0, k;
	for (k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < w->nprov && (w->prov[nb[k]].owner == c || w->prov[nb[k]].controller == c)) return 1;
	return 0;
}
/* 0x541570, the army balance with a neighbour: all armies and those next to the other's land (strength = men + 100 per gun,
   NIEPEWNE: the unit's vfunc 0x84 scale) */
static int border_ok(const Diplo *d, int u, int t)
{
	const World *w = d->w;
	long ut = 0, ub = 0, tt = 0, tb = 0;
	int i;
	if (!neighbours(d, u, t)) return 1;
	for (i = 0; i < w->nland; i++) {
		const LandUnit *x = &w->land[i];
		long s;
		if (x->dead || (x->owner != u && x->owner != t) || x->location <= 0 || x->location >= w->nprov) continue;
		s = x->infantry + x->cavalry + 100L * x->artillery;
		if (x->owner == u) { ut += s; if (borders_c(d, x->location, t)) ub += s; }
		else { tt += s; if (borders_c(d, x->location, u)) tb += s; }
	}
	if (ut <= 0) return 0;                                    /* < 0.05 */
	if (ut > 4 * tt) return 1;                                /* 0.25 x ours > theirs */
	return 100 * ub >= 15 * tb && 10 * ub > ut;               /* at the border: >= 0.15 x theirs and > 0.1 x all ours */
}

static int economy_ok(const Diplo *d, int u)                /* 0x5413c0: money, stability and manpower for a war */
{
	const World *w = d->w;
	const Country *U = &w->country[u];
	long v = U->treasury_m / 1000 + 150L * U->stability + 5L * (U->manpower_m / 1000) + U->inc_m / 1000 - 12L * (U->maint_m / 1000)
	         + U->nprov_owned - 50L * d->ai->rebels[u];
	{	/* every owned non-core European province gained less than 30 years ago: -50 x (3 - decades) (0x5414d9) */
		int p2, yy = w->now.year;
		for (p2 = 1; p2 < w->nprov; p2++) {
			const Province *pv = &w->prov[p2];
			int k2, core = 0, dd;
			if (pv->owner != u || pv->acq_year <= 0 || !d->eur_cont || pv->continent != d->eur_cont - 1) continue;
			for (k2 = 0; k2 < 8; k2++) if (pv->cores[k2] == u) core = 1;
			if (core) continue;
			dd = (yy - pv->acq_year) / 10;
			if (dd < 3) v -= 50L * (3 - dd);
		}
	}
	return v > 0;
}

/* the strength index +0x364 of c, or of its whole military alliance (lead: only when c leads it) */
static long side_power(const Diplo *d, int c, int lead)
{
	const World *w = d->w;
	long s = 0;
	int k, al = d->milally[c];
	if (al >= 0 && (!lead || w->ally[al].member[0] == c)) { for (k = 0; k < w->ally[al].n; k++) s += d->ai->fs[w->ally[al].member[k]] / 1000; }
	else s = d->ai->fs[c] / 1000;
	return s;
}
/* 0x540cb0: 2 x ours (our alliance when we lead it) > theirs (their alliance, led by anyone); the troop sums it also
   computes (vfuncs 0x388 / 0x38c) are not used */
static int alliance_ok(const Diplo *d, int u, int t) { return 2 * side_power(d, u, 1) > side_power(d, t, 0); }

int ai_war_ok(Diplo *d, int u, int t)
{
	World *w = d->w;
	AiState *a = d->ai;
	const Country *U = &w->country[u], *T;
	int mode = a->mode[u], cb;
	if (!alive(w, t) || t == u || d->atwar[u] || truce(d, u, t)) return 0;        /* 0x540ea0 */
	T = &w->country[t];
	if (U->land_tech < T->land_tech - 2 && !(U->nprov_owned / 3 > T->nprov_owned)) return 0;
	if (a->fs[t] / 10 * 8 > a->fs[u]) return 0;               /* their land strength x 0.8 above ours */
	if (U->we_m >= 1000) return 0;
	if (REL(d, u, t) > (140L + d->aiwar[u] / 30) * 1000) return 0;
	if (U->bankrupt_day > 0 && today_of(d) - U->bankrupt_day < 5L * 360) return 0;
	{ int k = U->nprov_owned / 5; if (k > 4) k = 4; if (a->rebels[u] > k) return 0; }
	if (mode == AIM_BUILDUP && T->badboy_m < 10000) return 0;
	if (mode == AIM_COLONISE && d->rel_idx[t] != R_PAGAN) return 0;
	if (!neighbours(d, u, t)) {
		if (a->ships[u] > 0 && a->coast[u]) { if (!(a->ships[u] > a->ships[t] / 2)) return 0; }
		else if (!land_access_route(d, u, t)) return 0;
	}
	cb = has_cb(w, u, t);
	if (T->badboy_m <= 20000) {
		if (!border_ok(d, u, t) && !economy_ok(d, u)) return 0;
		if (d->atwar[t] || cb || a->fs[t] < a->fs[u] || U->nprov_owned > T->nprov_owned / 3) return 1;
	}
	if (mode == AIM_COLONISE) return 1;
	if (border_ok(d, u, t) && economy_ok(d, u) && d->atwar[t]) return 1;
	if (border_ok(d, u, t) && cb && alliance_ok(d, u, t)) return 1;
	if (border_ok(d, u, t) && economy_ok(d, u) && alliance_ok(d, u, t)) return 1;
	return 0;
}

/* ---------------------------------------------------------------- strategic mode, 0x5421a0 */
static void choose_mode(Diplo *d, int u)
{
	World *w = d->w;
	AiState *a = d->ai;
	const Country *U = &w->country[u];
	long B = 100, C = d->aiexp[u], D = d->aiwar[u], tot, r;
	int yrs = a->war_day[u] > 0 ? months_since(today_of(d), a->war_day[u]) / 12 : 99, ncore = 0, k;
	long col = U->col_rate_m;
	if (yrs < 3) B = 300;
	if (d->ncombat[u] > 0 || a->enemy[u] >= 0) D += 100;
	C = col > 990 ? C + 25 : 0;
	for (k = a->core_head[u]; k >= 0; k = a->core_next[k]) ncore++;
	if (ncore > U->nprov_owned) D += 10L * (ncore - U->nprov_owned);   /* 10 x (cores - owned) */
	if (d->aiexp[u] == 0) C = 0;
	{	/* +0x386 (the recount 0x43ac12): unowned land it knows; +0xfe8 / +0xfe4 explorers / conquistadors (ours: units led by one) */
		int p2, i2, nexp = 0;
		long unk = 0;
		for (p2 = 1; p2 < w->nprov; p2++) if (w->prov[p2].owner < 0 && w->prov[p2].terrain < 5 && w->prov[p2].id >= 0 && WORLD_KNOWN(w, u, p2)) unk++;
		for (i2 = 0; i2 < w->nland; i2++) if (!w->land[i2].dead && w->land[i2].owner == u && w->land[i2].l_id && (w->land[i2].l_type == 2 || w->land[i2].l_type == 3)) nexp++;
		for (i2 = 0; i2 < w->nnaval; i2++) if (w->naval[i2].owner == u && w->naval[i2].l_id && w->naval[i2].l_type == 3) nexp++;
		if (year_now(w) > 1480) {
			if (col > 990) { B /= 5; D /= 5; }
			if (col > 1200) { C += 200; B = 0; }
			C += unk;
		}
		C = (!unk && !nexp) ? 0 : C + 1;                     /* 0x542370 */
	}
	{	/* 0x542399: every owned non-core European province acquired less than 30 years ago: W -= 10 (3 - decades), B += 10 */
		int p2, yy = year_now(w);
		for (p2 = 1; p2 < w->nprov; p2++) {
			const Province *pv = &w->prov[p2];
			int k2, core = 0, dd;
			if (pv->owner != u || pv->acq_year <= 0) continue;
			for (k2 = 0; k2 < 8; k2++) if (pv->cores[k2] == u) core = 1;
			if (core || !d->eur_cont || pv->continent != d->eur_cont - 1) continue;
			dd = (yy - pv->acq_year) / 10;
			if (dd < 3) { D -= 10L * (3 - dd); B += 10; }
		}
	}
	if (D < 0) D = 0;
	tot = B + C + D;
	a->mode_day[u] = today_of(d);
	if (tot <= 0) { a->mode[u] = AIM_BUILDUP; return; }
	r = drand(d) % tot;
	if (r - B < 0) a->mode[u] = AIM_BUILDUP;
	else if (r - B - C < 0) a->mode[u] = AIM_COLONISE;
	else a->mode[u] = AIM_DOMINANCE;
	logd(d, "%s is now in %s MODE", U->tag, a->mode[u] == AIM_BUILDUP ? "BUILDUP" : a->mode[u] == AIM_COLONISE ? "COLONISE" : "DOMINANCE");
}

/* ---------------------------------------------------------------- candidates */
/* 0x540310: the worst enemy = the lowest H (0x535810, how much U likes X), only negative ones; H inline
 * here (the hot loop of the AI): s = 0.1 rel; -5 neighbour; +300 truce; -4 if X ranks higher; with a casus
 * belli -5, -m per own core X holds (m 10 neighbour, else 1), neighbour + provinces(X) - provinces(U);
 * -0.05 min(80, years at peace) - 20; - badboy (not a neighbour: /2 /3 /4 by distance < 10 / < 20 /
 * more); a human X: badboy > 20 -> -1000 b (neighbour) or -25 b (distance < 30), else -b again;
 * COLONISE: other continent -20 (Christian U, pagan X) or -5, x2 neighbour, same continent 20 - b;
 * other modes: other continent and not a neighbour +30; not a neighbour + 4 distance; +100 allied,
 * +30 royal marriage, +50 own vassal; + rand%5 - rand%5; H = s / 2 (grievance not kept). Then in this
 * search: COLONISE and a pagan neighbour -99; a .ai combat target x5. */
#define LCG(d) ((d)->seed = ((d)->seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL, (int)(((d)->seed >> 16) & 0x7FFF))   /* = drand */
static int worst_enemy(Diplo *d, int u)
{
	World *w = d->w;
	AiState *a = d->ai;
	const Country *U = &w->country[u];
	int x, best = -1, k, li, yrs, mode = a->mode[u], cont_u = a->cont[u], rank_u = a->rank[u], al_u = d->milally[u];
	int col_chr = CHRISTIAN(d->rel_idx[u]);
	long bs = 0, base;
	const unsigned char *cbrow = w->dipflag[u];
	memset(a->held, 0, sizeof a->held);
	for (k = a->core_head[u]; k >= 0; k = a->core_next[k]) { int o = w->prov[a->core_prov[k]].owner; if (o >= 0) a->held[o]++; }
	yrs = a->war_day[u] > 0 ? months_since(today_of(d), a->war_day[u]) / 12 : 80;
	if (yrs > 80) yrs = 80;
	base = -(50L * yrs + 20000);
	for (li = 0; li < a->nlive; li++) {
		const Country *X;
		long s, b, dd;
		int nb, lx;
		x = a->live[li];
		if (x == u) continue;
		nb = NB(d, u, x);
		if (!nb && a->cont[x] != cont_u) continue;           /* known_cap */
		X = &w->country[x];
		lx = a->lidx[x];
		dd = a->lidx[u] >= 0 && lx >= 0 && a->lidx[u] < a->ndm && lx < a->ndm ? a->dm[a->lidx[u]][lx] : dist10_raw(w, u, x);
		s = a->relu[x] / 10 + base;
		if (nb) s -= 5000;
		if (a->trf[x]) s += 300000L;
		if (a->rank[x] < rank_u) s -= 4000;
		if (cbrow[x] & DF_CB) {
			s -= 5000 + (nb ? 10000L : 1000L) * a->held[x];
			if (nb) s += 1000L * (X->nprov_owned - U->nprov_owned);
		}
		b = X->badboy_m;
		if (!nb) b = dd < 100 ? b / 2 : dd < 200 ? b / 3 : b / 4;
		s -= b;
		if (X->human) {
			if (b > 20000) { if (nb) s -= b * 1000; else if (dd < 300) s -= 25 * b; }
			else s -= b;
		}
		if (mode == AIM_COLONISE) {
			if (a->cont[x] != cont_u) { s -= col_chr && d->rel_idx[x] == R_PAGAN ? 20000 : 5000; if (nb) s *= 2; }
			else s = 20000 - b;
		} else if (a->cont[x] != cont_u && !nb) s += 30000;
		if (!nb) s += 400L * dd;
		if (al_u >= 0 && d->milally[x] == al_u) s += 100000L;
		if (BITM(d->dyn, u, x)) s += 30000;
		if (a->lord[x] == u) s += 50000;
		if (U->elector && X->elector) s += 15000;              /* two electors (0x535aaf) */
		s += (LCG(d) % 5) * 1000L;
		s -= (LCG(d) % 5) * 1000L;
		s = s / 1000 * (diplo_gshare(d, x, u) + 1000) / 2;     /* H = s x (X's border share with U (+0x14de) + 1) x 0.5 */
		if (mode == AIM_COLONISE && nb && d->rel_idx[x] == R_PAGAN) s -= 99000L;
		if (BITM(d->aicombat, u, x)) s *= 5;
		if (s < bs) { bs = s; best = x; }
	}
	if (best != a->enemy[u] && best >= 0) logd(d, "%s determines that the worst enemy to them is %s", w->country[u].tag, w->country[best].tag);
	a->enemy[u] = (short)best;
	return best;
}

static int marriage_candidate(Diplo *d, int u)              /* 0x540460 */
{
	World *w = d->w;
	int x, best = -1, li;
	long bs = 0;
	for (li = 0; li < d->ai->nlive; li++) {
		long s;
		int ru, rx;
		x = d->ai->live[li]; ru = d->rel_idx[u]; rx = d->rel_idx[x];
		if (x == u || BITM(d->dyn, u, x)) continue;
		s = REL(d, u, x);
		if (neighbours(d, u, x)) s *= 2;
		if ((CHRISTIAN(rx) && MUSLIM(ru)) || (CHRISTIAN(ru) && MUSLIM(rx))) s -= 250000L;
		if (s > bs) { bs = s; best = x; }
	}
	return best;
}

static int vassal_candidate(Diplo *d, int u)                /* 0x540570 (0x45bf60 gate: NIEZNANE, taken as true) */
{
	World *w = d->w;
	int x, best = -1, li;
	long bs = 0;
	for (li = 0; li < d->ai->nlive; li++) {
		long s;
		x = d->ai->live[li];
		if (x == u) continue;
		s = REL(d, u, x);
		if (NB(d, u, x)) s *= 2;
		if (s > bs) { bs = s; best = x; }
	}
	return best;
}

static int alliance_candidate(Diplo *d, int u, int e)       /* 0x540600 */
{
	World *w = d->w;
	int x, best = -1, li;
	long bs = 0;
	for (li = 0; li < d->ai->nlive; li++) {
		long s;
		x = d->ai->live[li];
		if (x == u || d->milally[x] >= 0 || !known_cap(d, u, x)) continue;
		s = score_A(d, u, x, e);
		if (vassal_of(w, x, u)) s += 100000000L;
		if (s > bs) { bs = s; best = x; }
	}
	return best;
}

/* ---------------------------------------------------------------- answers (docs/SPEC_AI.md §3) */
static int pct(Diplo *d, long p_m) { return (long)(drand(d) % 100) * 1000 < p_m; }   /* rand%100 < trunc(100 p): p x1000 as percent x1000 */

/* 0x5372b0: military alliance offered by o (alliance al, -1 = a new one) */
int ai_accept_alliance(Diplo *d, int u, int o, int al)
{
	World *w = d->w;
	AiState *a = d->ai;
	long p, r = REL(d, u, o);
	int y = year_now(w), nb = neighbours(d, u, o), members = al >= 0 ? w->ally[al].n : 1, lim, e = a->enemy[u];
	if (!alive(w, o) || r < -100000L) return 0;
	p = same_religion(d->rel_idx[u], d->rel_idx[o]) ? 525 : 425;          /* x1000 */
	if (!nb) {
		if (!w->country[o].human) {
			long k = (y - 1420) / 25 + 5;
			if (a->cont[u] != a->cont[o] && w->country[o].nprov_owned < 200 && a->ships[o] < 5) return 0;
			if (dist10(w, u, o) / 5 > k * 10) return 0;                      /* 0.02 x distance > k */
		} else {
			int k, any = 0;
			if (a->cont[u] != a->cont[o] && a->ships[o] < 5) p /= 2;
			for (k = 0; k < w->ncountry && !any; k++) if (alive(w, k) && neighbours(d, u, k) && neighbours(d, o, k)) any = 1;
			if (!any) p /= 20;
		}
	}
	if (bit(d->dyn, u, o) || allied(d, u, o) || vassal_of(w, u, o) || vassal_of(w, o, u)) p *= 2;
	if (d->atwar[o]) p /= 2;
	p = p * (800 + 100L * (6 - w->country[u].stability)) / 1000;
	if (e >= 0 && al >= 0) {
		int k, in = 0;
		for (k = 0; k < w->ally[al].n; k++) if (w->ally[al].member[k] == e) in = 1;
		if (in) {
			long sa = 0, se = (a->fs[e] + a->fn[e]) / 1000;
			for (k = 0; k < w->ally[al].n; k++) sa += (a->fs[w->ally[al].member[k]] + a->fn[w->ally[al].member[k]]) / 1000;
			if (se > 0) p = p * sa / se;
		}
	}
	lim = 4 + d->opt_aggr;                                    /* + [[0x8c0bfc]+0x2c6] when the human is not in it (NIEPEWNE option) */
	if (al >= 0) { int k; for (k = 0; k < w->ally[al].n; k++) if (w->country[w->ally[al].member[k]].human) lim = 4; }
	if (members + 1 > lim) p -= 50000L * (members + 1 - lim);
	if ((CHRISTIAN(d->rel_idx[u]) && MUSLIM(d->rel_idx[o])) || (CHRISTIAN(d->rel_idx[o]) && MUSLIM(d->rel_idx[u]))) {
		p /= 4;
		if (y < 1520) p = p / 20 - 150000L;
		if (y < 1600) p /= 5;
	}
	if (vassal_of(w, u, o)) p += 750;
	if (o == w->emperor && w->country[u].elector) p *= 2;     /* the emperor asking an elector (0x537679) */
	p = p * (1000 + REL(d, o, u) / 400) / 1000;           /* x (1 + 0.0025 rel) */
	return pct(d, p * 100);
}

/* 0x552090: the score of a royal marriage plan of u toward o (also the base of the answer) */
static long marriage_score(Diplo *d, int u, int o)
{
	World *w = d->w;
	AiState *a = d->ai;
	long s = has_cb(w, u, o) ? 0 : 10000, t;
	int k;
	if (!w->country[o].tracked) s += 10000;                  /* O+0x11e == 0: not a featured country of the scenario header (scenario+0x2768 -> 0x4a2ea8) */
	if (same_religion(d->rel_idx[u], d->rel_idx[o])) s += 20000;
	if (!d->atwar[o]) s += 2000;
	if (neighbours(d, u, o)) s += 5000;
	if (a->fs[o] > 0 && a->fs[u] > a->fs[o]) s += 5000L * (a->fs[u] - a->fs[o]) / a->fs[o];
	if (a->fn[o] > 0 && a->fn[u] > a->fn[o]) s += 5000L * (a->fn[u] - a->fn[o]) / a->fn[o];
	if (a->fe[o] > 0 && a->fe[u] > a->fe[o]) s += 10000L * (a->fe[u] - a->fe[o]) / a->fe[o];   /* F3 = the economic index +0x360 */
	s = s / 1000 * (REL(d, u, o) / 4 + 500);            /* x (0.25 rel + 0.5) */
	t = 0;
	for (k = 0; k < w->ncountry; k++) if (k != o && bit(d->dyn, o, k)) t += 5000;
	if (!same_religion(d->rel_idx[u], d->rel_idx[o])) t += 20000;
	return s - t;
}

int ai_accept_marriage(Diplo *d, int u, int o)                /* 0x5377e0 */
{
	World *w = d->w;
	long p;
	int i, k;
	if (!alive(w, o) || TAGIS(w, u, "PAP") || TAGIS(w, u, "KNI") || REL(d, u, o) < -150000L) return 0;   /* exe indices 0x2e / 0x16 = country.csv rows 45 / 21 */
	p = marriage_score(d, u, o);
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		int ho = 0, hu = 0;
		for (k = 0; k < al->n; k++) { if (al->member[k] == o) ho = 1; if (al->member[k] == u) hu = 1; }
		if (al->type == ALLY_DYNASTIC) { if (ho) p -= 15000; if (hu) p -= 5000; }
		else if (al->type == ALLY_VASSAL) { if (ho) p -= 25000; if (hu) p -= 25000; }
	}
	if (o == w->emperor && w->country[u].elector) p *= 2;     /* 0x5379cf */
	p = p / 1000 * (1000 + REL(d, o, u) / 4);           /* x (1 + 0.25 rel) */
	return pct(d, p);
}

int ai_accept_vassal(Diplo *d, int u, int o)                  /* 0x537ae0: o offers to make u its vassal */
{
	World *w = d->w;
	const Country *U = &w->country[u], *O = &w->country[o];
	long p;
	int k, ncore = 0;
	AiState *a = d->ai;
	if (!alive(w, o)) return 0;
	p = 5L * dip_of(O) + (O->nprov_owned - U->nprov_owned) / 4;
	p += neighbours(d, u, o) ? 20 : -10;
	p -= 5L * U->nprov_owned;
	for (k = a->core_head[u]; k >= 0; k = a->core_next[k]) ncore++;
	if (U->nprov_owned > ncore) p -= 10;
	if (O->nprov_owned < U->nprov_owned) p -= 85; else if (!O->human && !U->human) p += 50;
	if (U->elector && !O->elector) p -= 30;                   /* an elector does not bow to a non-elector (0x537c13) */
	p = p - 5L * U->stability + 5L * O->stability;
	for (k = 0; k < w->ncountry; k++) if (vassal_of(w, k, u)) return 0;
	return (long)(drand(d) % 100) < p;
}

int ai_accept_access(Diplo *d, int u, int o)                  /* 0x537ed0 */
{
	World *w = d->w;
	AiState *a = d->ai;
	long r = REL(d, u, o) / 1000, p;
	if (!alive(w, o) || (w->dipflag[u][o] & DF_NOTRADE)) return 0;
	if (r > 150) p = r - 150;
	else if (w->country[o].nprov_owned / 2 > w->country[u].nprov_owned || neighbours(d, u, o)) {
		long base = 1;
		if (a->fs[o] / 2 > a->fs[u]) { base = 26; if (r < -150) base = 36; }
		p = base + (r - 50) / 3;
	} else p = 0;
	if (d->atwar[o]) p -= 15;
	if (d->milally[u] >= 0) { int k; const Alliance *al = &w->ally[d->milally[u]]; for (k = 0; k < al->n; k++) if (w->dipflag[o][al->member[k]] & DF_NOTRADE) { p -= 100; break; } }
	return (long)(drand(d) % 100) < p * 100;
}

int ai_accept_trade(Diplo *d, int u, int o)                   /* 0x5380c0 */
{
	World *w = d->w;
	long p = 100 - d->aimono[u], r = REL(d, u, o) / 1000;
	int k;
	if (r >= -150 && drand(d) % 100 < (r + 100) / 4) p += 25;
	if (world_trade_minded(w, u)) p += 25;                      /* 0x43a9e0 */
	for (k = 0; k < w->ncountry; k++) if (w->dipflag[u][k] & DF_NOTRADE) return 0;
	for (k = 0; k < w->ncountry; k++) if (k != u && (w->dipflag[u][k] & DF_TRADEAGR)) p -= 10;
	return (long)(drand(d) % 100) < p;
}

/* 0x535da0: loan offer of amount_m at rate % due in months */
int ai_accept_loan(Diplo *d, int u, int lender, long amount_m, int rate, int months)
{
	World *w = d->w;
	const Country *U = &w->country[u];
	long A = amount_m / 1000, inc = U->inc_m / 1000, owed = 0;
	int i;
	if (rate < 1) return 1;
	if (rate > 25) return 0;
	for (i = 0; i < d->nloan; i++) if (d->loan[i].borrower == u || d->loan[i].lender == u) owed += d->loan[i].amount_m / 1000;
	if (A + owed > 250 * inc + U->treasury_m / 1000) return 0;
	if (rate * A > 1200 * inc) return 0;
	if (U->treasury_m / 1000 > 24 * inc) return 0;
	if ((long)months * rate > 100) return 0;
	{	/* the lender's yearly interest income above 8 ducats (ledger income cat. 6, NIEPEWNE): from its loans */
		long earned = 0;
		for (i = 0; i < d->nloan; i++) if (d->loan[i].lender == lender) earned += d->loan[i].amount_m / 1000 * d->loan[i].rate / 100;
		if (earned > 8) return 0;
	}
	return 1;
}

/* 0x556d10: an AI borrower at the due date; 1 = it renounces the debt (cancel), 0 = it pays */
int ai_loan_cancel(Diplo *d, int u, int lender, int rate)
{
	World *w = d->w;
	long p = 95, r;
	if (lender < 0) return 0;
	if (!alive(w, lender)) return (drand(d) % 100) < p;
	if (w->dipflag[u][lender] & DF_TRADEAGR) p = 90;
	if (vassal_of(w, lender, u)) p -= 80;
	if (vassal_of(w, u, lender)) p -= 80;
	r = REL(d, lender, u);
	if (r > 170000L) p -= 50; else if (r > 120000L) p -= 30; else if (r > 70000L) p -= 10;
	if (allied(d, u, lender)) p -= 25;
	if (bit(d->dyn, u, lender)) p -= 10;
	p += 5L * rate;
	return (drand(d) % 100) < p;
}

/* 0x535f40: u is asked by ally al_c to honour the alliance in war `war` against e */
int ai_honour(Diplo *d, int u, int ally, int e, int war)
{
	World *w = d->w;
	AiState *a = d->ai;
	long r = REL(d, ally, u), p, sf = 0, se = 0;
	int k;
	const War *wr = &w->war[war];
	if (!alive(w, ally) || !alive(w, e)) return 0;
	if (r > 180000L) return 1;
	if (!neighbours(d, e, u) && !neighbours(d, e, ally) && (!a->coast[e] || !a->coast[u])) return 0;
	p = 80000L - 10000L * w->country[u].stability - 15L * w->country[u].we_m;
	for (k = 0; k < wr->natt; k++) { long f = (a->fs[wr->att[k]] + a->fn[wr->att[k]]) / 1000; if (peace_side(w, war, e) == 1) se += f; else sf += f; }
	for (k = 0; k < wr->ndef; k++) { long f = (a->fs[wr->def[k]] + a->fn[wr->def[k]]) / 1000; if (peace_side(w, war, e) == 2) se += f; else sf += f; }
	if (se > 0) p = p * sf / se;
	p = p / 1000 * (1000 + r / 4);                            /* x (1 + 0.25 rel) */
	if (r > 170000L) p += 25000;
	if (r > 160000L) p += 25000;
	if (r > 150000L) p += 25000;
	if (ally == w->emperor && w->country[u].elector) p *= 2;  /* 0x5361d2 */
	return (long)(drand(d) % 100) * 1000 < p;
}

/* 0x536300: o asks to join u's alliance al */
int ai_allow_join(Diplo *d, int u, int o, int al)
{
	World *w = d->w;
	long p = 50000L + REL(d, o, u) / 4;
	int lim = 4 + d->opt_aggr, k;
	for (k = 0; k < w->ally[al].n; k++) if (w->country[w->ally[al].member[k]].human) lim = 4;
	if (w->ally[al].n > lim) p -= 33000L * (w->ally[al].n - lim);
	if (o == w->emperor && w->country[u].elector) p *= 2;     /* 0x5363e1 */
	return (long)(drand(d) % 100) * 1000 < p;
}

/* 0x536430: overlord o offers to annex its vassal u */
int ai_accept_annex(Diplo *d, int u, int o)
{
	World *w = d->w;
	const Country *U = &w->country[u], *O = &w->country[o];
	long p;
	int i;
	if (REL(d, u, o) < -150000L) return 0;
	p = 5;
	if (U->stability < 0) p = 10;
	if (U->stability < -1) p += 5;
	if (U->stability < -2) p += 10;
	i = vassal_index(w, o, u);
	if (i >= 0) p += year_now(w) - w->ally[i].expiry.year + 100;   /* years of vassalage: the expiry is set 100 years ahead (diplo.c) */
	p -= 10;
	if (O->nprov_owned < U->nprov_owned) return 0;
	p = p - U->nprov_owned / 4 + O->nprov_owned / 8;
	if (dip_of(O) > p) p = dip_of(U);
	if (o == w->emperor && U->elector) p *= 2;                /* 0x5365c1 */
	return (drand(d) % 100) < p;
}

/* ---------------------------------------------------------------- plans (docs/SPEC_AI.md §5)
 * The exe queues plans and runs them in the same think; each needs the human menu's rule for that
 * action, a diplomat, and the record's cooldown (0x535520: last action toward the target + 12 months,
 * 1 month at war; a 50 % roll in the second interval). */
static long *last_slot(AiState *a, int u, int t, int create)
{
	unsigned long k = ((unsigned long)u * MAX_COUNTRY + (unsigned long)t) * 2654435761UL;
	int i, h = (int)((k >> 20) & (AI_NLAST - 1));
	for (i = 0; i < AI_NLAST; i++, h = (h + 1) & (AI_NLAST - 1)) {
		if (a->lu[h] == u + 1 && a->lt[h] == t) return &a->lday[h];
		if (!a->lu[h]) { if (!create) return NULL; a->lu[h] = (short)(u + 1); a->lt[h] = (short)t; a->lday[h] = 0; return &a->lday[h]; }
	}
	return NULL;
}

static int cooldown_ok(Diplo *d, int u, int t)
{
	long *l = last_slot(d->ai, u, t, 0), n = war_with(d, u, t) ? 30 : 360, dt;
	if (!alive(d->w, t)) return 0;
	if (!l || *l <= 0) return 1;
	dt = today_of(d) - *l;
	if (dt < n) return 0;
	if (dt >= 2 * n) return 1;
	return drand(d) % 100 < 50;
}

static void acted(Diplo *d, int u, int t) { long *l = last_slot(d->ai, u, t, 1); if (l) *l = today_of(d); }

static int envoy(Diplo *d, int u)                           /* 0x45c060: a diplomat goes (as the player's, §3.1) */
{
	Country *U = &d->w->country[u];
	if (U->diplomats_m < 990) return 0;
	U->diplomats_m -= 1000;
	if (U->diplomats_m < 0) U->diplomats_m = 0;
	return 1;
}

static int ndyn(const Diplo *d, int c)
{
	const World *w = d->w;
	int i, k, n = 0;
	for (i = 0; i < w->nally; i++) if (w->ally[i].type == ALLY_DYNASTIC) for (k = 0; k < w->ally[i].n; k++) if (w->ally[i].member[k] == c) { n += w->ally[i].n - 1; break; }
	return n;
}

static int marry(Diplo *d, int u, int o);
static void send_marriage(Diplo *d, int u, int o)           /* 0x5388b0 / 0x552030 */
{
	World *w = d->w;
	if (!marriage_ok(d->rel_idx[u], d->rel_idx[o]) || war_with(d, u, o) || vassal_of(w, o, u) || vassal_of(w, u, o)) return;   /* DIPOFFDYN */
	if (!envoy(d, u)) return;
	acted(d, u, o);
	marry(d, u, o);
}

/* the tutorial's OfferRoyalMarriage (0x441140(1, B)): u proposes to o now, no envoy, no cooldown */
int diplo_offer_marriage(Diplo *d, int u, int o)
{
	if (u < 0 || o < 0 || u == o || war_with(d, u, o) || vassal_of(d->w, o, u) || vassal_of(d->w, u, o)) return 0;
	return marry(d, u, o);
}

static int marry(Diplo *d, int u, int o)                    /* the offer, the answer, the alliance */
{
	World *w = d->w;
	logd(d, "%s sends a 'royalmarriage' to %s", w->country[u].tag, w->country[o].tag);
	if (!(w->country[o].human ? d->ask && d->ask(d->ask_user, "ROYMARPROP", u, -1, -1) : ai_accept_marriage(d, o, u)) || w->nally >= MAX_ALLY) { logd(d, "%s declines alliance with %s", w->country[o].tag, w->country[u].tag); return 0; }
	{	Alliance *al = &w->ally[w->nally++];
		memset(al, 0, sizeof *al);
		al->type = ALLY_DYNASTIC; al->member[0] = (short)u; al->member[1] = (short)o; al->n = 2;
		al->expiry = w->now; al->expiry.year = (short)(w->now.year + 25);   /* 0x44abf0: type 1 -> 25 */
		setbit(d->dyn, u, o); setbit(d->dyn, o, u);
		diplo_vp_join(d, u, o, ALLY_DYNASTIC);
		world_news(w, NOTE_OTHER, u, OM_ROYMAR, o);            /* ROYMARACCEPTOTHER */
		logd(d, "%s accepts alliance with %s", w->country[o].tag, w->country[u].tag);
	}
	return 1;
}

static void send_vassal(Diplo *d, int u, int o)             /* 0x538d90 / 0x552290 */
{
	World *w = d->w;
	if (w->country[o].human || !vassal_ok_ai(d, u, o) || !envoy(d, u)) return;   /* DIPOFFVAS */
	acted(d, u, o);
	logd(d, "%s sends a 'vassalization' to %s", w->country[u].tag, w->country[o].tag);
	if (!ai_accept_vassal(d, o, u) || w->nally >= MAX_ALLY) { logd(d, "%s declines alliance with %s", w->country[o].tag, w->country[u].tag); return; }
	{	Alliance *al = &w->ally[w->nally++];
		memset(al, 0, sizeof *al);
		al->type = ALLY_VASSAL; al->member[0] = (short)u; al->member[1] = (short)o; al->n = 2;
		al->expiry = w->now; al->expiry.year = (short)(w->now.year + 100);
		diplo_vp_join(d, u, o, ALLY_VASSAL);
		logd(d, "%s accepts alliance with %s", w->country[o].tag, w->country[u].tag);
	}
}

static void send_alliance(Diplo *d, int u, int o, int invite)   /* 0x538a10 / 0x538be0 */
{
	World *w = d->w;
	int al = d->milally[u];
	if (d->milally[o] >= 0 || war_with(d, u, o) || lord_of(w, o) >= 0) return;   /* DIPCNALL / invite rule */
	if (invite && (al < 0 || w->ally[al].member[0] != u)) return;
	if (!invite && al >= 0) return;
	if (!envoy(d, u)) return;
	acted(d, u, o);
	logd(d, "%s sends a '%s' to %s", w->country[u].tag, invite ? "invite-to-existing-alliance" : "offer-new-alliance", w->country[o].tag);
	if (!(w->country[o].human ? d->ask && d->ask(d->ask_user, invite ? "MILALLINVPROP" : "MILALLPROP", u, d->ai->enemy[u], -1) : ai_accept_alliance(d, o, u, invite ? al : -1))) {
		logd(d, "%s declines alliance with %s", w->country[o].tag, w->country[u].tag); return;
	}
	if (invite && w->ally[al].n < 24) { w->ally[al].member[w->ally[al].n++] = (short)o; d->milally[o] = (short)al; }
	else if (!invite && w->nally < MAX_ALLY) {
		Alliance *na = &w->ally[w->nally];
		memset(na, 0, sizeof *na);
		na->type = ALLY_MILITARY; na->member[0] = (short)u; na->member[1] = (short)o; na->n = 2;
		na->expiry = w->now; na->expiry.year = (short)(w->now.year + 10);   /* 0x44abf0: other types -> 10 */
		d->milally[u] = d->milally[o] = (short)w->nally;
		w->nally++;
	} else return;
	diplo_vp_join(d, u, o, ALLY_MILITARY);
	logd(d, "%s accepts alliance with %s", w->country[o].tag, w->country[u].tag);
}

static void send_askjoin(Diplo *d, int u, int al)           /* 0x538c90 / 0x552a70 */
{
	World *w = d->w;
	int lead = w->ally[al].member[0];
	if (d->milally[u] >= 0 || w->ally[al].n > 5 || war_with(d, u, lead) || !cooldown_ok(d, u, lead) || !envoy(d, u)) return;
	acted(d, u, lead);
	logd(d, "%s sends a 'ask-to-join-their-alliance' to %s", w->country[u].tag, w->country[lead].tag);
	if (!(w->country[lead].human ? d->ask && d->ask(d->ask_user, "MILALLJOINPROP", u, d->ai->enemy[u], -1) : ai_allow_join(d, lead, u, al)) || w->ally[al].n >= 24) { logd(d, "%s disallows %s to join", w->country[lead].tag, w->country[u].tag); return; }
	w->ally[al].member[w->ally[al].n++] = (short)u; d->milally[u] = (short)al;
	diplo_vp_join(d, u, lead, ALLY_MILITARY);
	logd(d, "%s allows %s to join", w->country[lead].tag, w->country[u].tag);
}

static void send_annex(Diplo *d, int u, int v)              /* 0x538e40 / 0x5523b0 */
{
	World *w = d->w;
	if (w->country[v].human || w->country[v].nprov_owned > 250 || !annex_ok_ai(d, u, v) || !envoy(d, u)) return;   /* DIPANNEX */
	acted(d, u, v);
	logd(d, "%s sends a 'annexation' to %s", w->country[u].tag, w->country[v].tag);
	if (!ai_accept_annex(d, v, u)) { logd(d, "%s declines an annexation from %s", w->country[v].tag, w->country[u].tag); return; }
	logd(d, "%s accepts an annexation from %s", w->country[v].tag, w->country[u].tag);
	diplo_annex(d, u, v);
}

static void ties(Diplo *d);
static void cancel_vassalage(Diplo *d, int u, int o)        /* 0x538f10: u is o's vassal */
{
	World *w = d->w;
	long p = (200 - REL(d, u, o) / 1000) / 5;
	int i = vassal_index(w, o, u);
	if (!cooldown_ok(d, u, o)) return;
	if (i >= 0) p -= 2L * (year_now(w) - (w->ally[i].expiry.year - 100));   /* years of vassalage (expiry is set 100 years on) */
	if (drand(d) % 100 > p) return;
	if (allied(d, u, o)) return;
	if (drand(d) % 10 < dip_of(&w->country[o])) return;
	if (drand(d) % 10 > dip_of(&w->country[u])) return;
	{	/* U+0x8ee[O]: at least 10 + 3 DIP(O) years since the last peace with o */
		long lp = peace_last(d->p, u, o);
		if (lp > 0 && d->today - lp < (10L + 3L * dip_of(&w->country[o])) * 360) return;
	}
	if (neighbours(d, u, o) && d->men[u] + w->country[u].manpower_m < (d->men[o] + w->country[o].manpower_m) / 3) return;
	if (w->country[u].human || !envoy(d, u)) return;
	acted(d, u, o);
	logd(d, "%s sends a 'cancel-vassalization' to %s", w->country[u].tag, w->country[o].tag);
	if (w->country[o].human) world_note(w, NOTE_DIPMSG, u, DM_VASBREAK, o);
	if (i >= 0) ally_remove(w, i);
	world_vp(w, u, VP_DIP, -5000);
	if (w->country[u].stability > -3) w->country[u].stability--;   /* the human Cancel path (§3.2): -1 stability */
	w->dipflag[o][u] |= DF_CB_PERM;                                /* the lord gets a CB (readme) */
	ties(d);
}

/* ---------------------------------------------------------------- 0x5409a0 alliances */
static void think_alliances(Diplo *d, int u)
{
	World *w = d->w;
	AiState *a = d->ai;
	int e = a->enemy[u], al = d->milally[u];
	if (lord_of(w, u) >= 0) return;
	if (al >= 0) {
		/* a member that dislikes its leader would leave (0x538ac0), but the exe's plan never gets its alliance
		 * id and is cancelled: the AI never leaves an alliance this way (docs/SPEC_AI.md §5) */
		if (w->ally[al].member[0] == u && w->ally[al].n < 4 + d->opt_aggr) {
			int x = alliance_candidate(d, u, e);
			if (x >= 0 && cooldown_ok(d, u, x) && (long)(drand(d) % 100) * 1000 < score_A(d, u, x, e)) send_alliance(d, u, x, 1);
		}
		return;
	}
	{	int x = alliance_candidate(d, u, e), q;
		if (x >= 0) {
			q = w->country[u].nprov_owned + 15 + (w->country[u].tracked ? 40 : -10);   /* +40 instead of -10 for a featured country (U+0x11e) */
			if (drand(d) % 100 < q && cooldown_ok(d, u, x)) { send_alliance(d, u, x, 0); return; }
		}
	}
	if (drand(d) % 100 <= d->nmilally + 10) {
		int i, best = -1;
		long bs = 20000;
		for (i = 0; i < w->nally; i++) {
			const Alliance *x = &w->ally[i];
			long s;
			if (x->type != ALLY_MILITARY || x->n <= 0 || x->n >= 4 + d->opt_aggr || x->member[0] == u) continue;
			s = score_A(d, u, x->member[0], e);
			if (s > bs) { bs = s; best = i; }
		}
		if (best >= 0 && (long)(drand(d) % 100) * 1000 < bs) send_askjoin(d, u, best);
	}
}

/* ---------------------------------------------------------------- 0x53f5f0 pressure on the worst enemy */
static void pressure(Diplo *d, int u, int e)
{
	World *w = d->w;
	const Country *U = &w->country[u];
	long best = 0, s, inc12 = U->inc_m * 12 / 1000;
	int what = 0, tgt = -1, k, li;
	if (e < 0 || !alive(w, e)) return;
	if (w->dipflag[e][u] & DF_ACCESS) {                        /* cancel the access E gives us (score 295) */
		if (!U->human && cooldown_ok(d, u, e) && envoy(d, u)) { acted(d, u, e); w->dipflag[e][u] &= (unsigned char)~DF_ACCESS; if (w->country[e].human) world_note(w, NOTE_DIPMSG, u, DM_CANCACC, -1); world_news(w, NOTE_OTHER, u, OM_CANMIL, e); if (w->country[u].stability > -3) w->country[u].stability--; logd(d, "%s cancels the military access of %s", U->tag, w->country[e].tag); }
		return;
	}
	if (!has_cb(w, u, e)) {
		long r = REL(d, u, e) / 1000;
		int small = w->country[e].nprov_owned < U->nprov_owned / 2;
		s = r >= 20 ? r / 5 : -1;                              /* insult */
		if (allied(d, u, e) || !small || d->men[u] <= 0) s = -1;
		if (s > best) { best = s; what = 1; tgt = e; }
		s = 2 * (w->country[e].badboy_m / 1000) + 10;          /* warning */
		if (!small || (w->dipflag[u][e] & DF_WARNED)) s = -1;
		if (s > best) { best = s; what = 2; tgt = e; }
		if (BITM(d->dyn, u, e)) {                              /* claim throne (25): income >= 500 a year */
			s = inc12 >= 500 ? 25 : -1;
			if (!small || diplo_has_pact(d, u, e, PACT_GUARANTEE) || allied(d, u, e)) s = -1;
			if (s > best) { best = s; what = 4; tgt = e; }
		}
	}
	if (lord_of(w, u) < 0)
		for (li = 0; li < d->ai->nlive; li++) {                /* ask military access next to E (score 35) */
			long *l;
			k = d->ai->live[li];
			if (k == e || k == u || !NB(d, k, e) || !known_cap(d, u, k)) continue;
			l = last_slot(d->ai, u, k, 0);
			if (l && *l > 0 && today_of(d) - *l < 36L * 30) continue;
			if (U->nprov_owned / 3 > w->country[k].nprov_owned && inc12 > 500 && !diplo_has_pact(d, u, k, PACT_GUARANTEE) &&
			    w->country[k].nprov_owned < U->nprov_owned / 2 && 25 > best) { best = 25; what = 5; tgt = k; }   /* guarantee */
			if (!neighbours(d, u, e) && neighbours(d, u, k) && w->country[k].nprov_owned < U->nprov_owned / 2 && !(w->dipflag[k][u] & DF_ACCESS) && 35 > best) { best = 35; what = 3; tgt = k; }
		}
	if (!what || tgt < 0 || !cooldown_ok(d, u, tgt)) return;
	if (what != 3) {                                          /* money plans: the cost + 1 must be in the treasury */
		static const int act[6] = { 0, DA_INSULT, DA_WARN, 0, DA_CLAIM, DA_GUARANTEE };
		static const char *const nm[6] = { "", "insult", "warning", "", "claim-throne", "guarantee" };
		long cost = diplo_action_cost(d, u, tgt, act[what]) + 1000;
		if (w->country[u].treasury_m < cost) return;
		if (diplo_ai_action(d, u, tgt, act[what])) {
			acted(d, u, tgt); logd(d, "%s sends a '%s' to %s", U->tag, nm[what], w->country[tgt].tag); }
		return;
	}
	if (w->country[tgt].human || !envoy(d, u)) return;
	acted(d, u, tgt);
	logd(d, "%s asks %s for military access", U->tag, w->country[tgt].tag);
	if (ai_accept_access(d, tgt, u)) { w->dipflag[tgt][u] |= DF_ACCESS; world_news(w, NOTE_OTHER, tgt, OM_ACCMIL, u); }   /* ACCMILOTHER */
}

/* ---------------------------------------------------------------- trade (0x5391b0 / 0x539360) */
static void lift_embargoes(Diplo *d, int u)
{
	World *w = d->w;
	int k, li;
	for (li = 0; li < d->ai->nlive; li++) {
		long p, r;
		k = d->ai->live[li];
		if (!(w->dipflag[u][k] & DF_NOTRADE) || war_with(d, u, k) || !cooldown_ok(d, u, k)) continue;
		p = 100 - d->aimono[u];
		r = REL(d, u, k) / 1000;
		if (r >= -150 && drand(d) % 100 < (r + 100) / 4) p *= 2; else p /= 2;
		if (w->country[k].human) p /= 2;
		if (drand(d) % 1000 >= p) continue;
		acted(d, u, k);
		w->dipflag[u][k] &= (unsigned char)~DF_NOTRADE; w->dipflag[k][u] &= (unsigned char)~DF_CB_PERM;
		if (w->country[k].human) world_note(w, NOTE_DIPMSG, u, DM_ALLOWTRADE, -1);
		logd(d, "%s sends a 'allowtrade' to %s", w->country[u].tag, w->country[k].tag);
	}
}

static void embargo_enemies(Diplo *d, int u)                /* at war: p = monopoly + 999 + 25 x badboy, out of 1000 */
{
	World *w = d->w;
	int k, n = 0, li;
	for (k = 0; k < w->ncountry; k++) if (k != u && (w->dipflag[u][k] & DF_NOTRADE)) n++;
	for (li = 0; li < d->ai->nlive && n <= 3; li++) {
		k = d->ai->live[li];
		if ((w->dipflag[u][k] & DF_NOTRADE) || !d->ai->foe[k] || !cooldown_ok(d, u, k)) continue;
		if (drand(d) % 1000 >= 25L * (w->country[k].badboy_m / 1000) + d->aimono[u] + 999) continue;
		acted(d, u, k);
		w->dipflag[u][k] |= DF_NOTRADE; w->dipflag[k][u] |= DF_CB_PERM;
		if (w->country[k].human) world_note(w, NOTE_DIPMSG, u, DM_EMBARGO, -1);
		n++;
		logd(d, "%s sends a 'refusetrade' to %s", w->country[u].tag, w->country[k].tag);
	}
}

/* ---------------------------------------------------------------- 0x53fd00 the diplomat's month */
static void declare(Diplo *d, int u, int t)                 /* 0x538200 / 0x551e00 (DIPWAR rule, a diplomat) */
{
	World *w = d->w;
	if (d->no_wars || war_with(d, u, t)) return;          /* [0x8d0d79] */
	if (diplo_action_state(d, u, t, DA_WAR, today_of(d)) != DS_ON || !envoy(d, u)) return;
	acted(d, u, t);
	logd(d, "%s sends a 'declarewar' to %s", w->country[u].tag, w->country[t].tag);
	diplo_declare_war(d, u, t, today_of(d));
}

static void think(Diplo *d, int u)
{
	World *w = d->w;
	AiState *a = d->ai;
	Country *U = &w->country[u];
	long today = today_of(d);
	int D = w->opt_diff + 2, e, k;
	PT0;
	{	/* A.3: at the start; after a war of ours ended (at peace again); else after 7 years with a 4 % roll per think */
		int yy, mm, dd, y0, m0, d0;
		cal_date(today, &yy, &mm, &dd); cal_date(a->mode_day[u], &y0, &m0, &d0);
		if (a->mode_day[u] == 0 || (!d->atwar[u] && a->war_day[u] > a->mode_day[u]) || (yy - y0 > 7 && drand(d) % 100 > 95)) choose_mode(d, u);
	}
	PT(0);
	if (!d->atwar[u] && D >= 4) {                             /* very hard: the player over the badboy limit */
		int li;
		for (li = 0; li < a->nlive; li++) {
			const Country *P;
			k = a->live[li]; P = &w->country[k];
			if (!P->human || !over_limit(w, k) || truce(d, u, k) || !known_cap(d, u, k)) continue;
			if (neighbours(d, u, k) || (a->ships[u] > 0 && a->coast[k] && a->cont[u] == a->cont[k])) { declare(d, u, k); return; }
		}
	}
	e = worst_enemy(d, u);
	PT(1);
	lift_embargoes(d, u);
	PT(2);
	if (d->atwar[u]) {
		if (d->p && peace_ai_think(d->p, u, today)) { PT(3); return; }   /* 0x53f240 */
		PT(3);
		embargo_enemies(d, u);
		think_alliances(d, u);
		PT(4);
		return;
	}
	if (lord_of(w, u) >= 0) cancel_vassalage(d, u, lord_of(w, u));
	else if (!TAGIS(w, u, "STE") && !TAGIS(w, u, "BOS")) {   /* 0x43a9c0: exe indices 0x36 / 0x88 = country.csv rows 53 / 135 */
		int x;
		if (ndyn(d, u) <= dip_of(U) / 2 && (x = marriage_candidate(d, u)) >= 0 && !TAGIS(w, u, "PAP") && !TAGIS(w, u, "KNI") && cooldown_ok(d, u, x) &&
		    drand(d) % 100 < 50 - 15 * ndyn(d, x) - 5 * ndyn(d, u)) send_marriage(d, u, x);
		/* the vassalisation rule needs a royal marriage and the same alliance: without them no search (the plan would fail) */
		if (d->milally[u] >= 0 && ndyn(d, u) > 0 && (x = vassal_candidate(d, u)) >= 0 && lord_of(w, x) < 0 && cooldown_ok(d, u, x)) send_vassal(d, u, x);
		for (k = 0; k < a->nlive; k++)
			if (vassal_of(w, a->live[k], u) && NB(d, u, a->live[k]) && cooldown_ok(d, u, a->live[k])) { send_annex(d, u, a->live[k]); break; }
		think_alliances(d, u);
	}
	PT(5);
	if (!d->atwar[u]) pressure(d, u, e);
	PT(6);
	if (e >= 0 && ai_war_ok(d, u, e)) declare(d, u, e);
	PT(7);
}

static void ties(Diplo *d)                                   /* alliances, marriages, overlords after a change */
{
	World *w = d->w;
	int i;
	refresh_ties(d);
	memset(d->atwar, 0, sizeof d->atwar);
	for (i = 0; i < w->nwar; i++) { int k; for (k = 0; k < w->war[i].natt; k++) d->atwar[w->war[i].att[k]] = 1; for (k = 0; k < w->war[i].ndef; k++) d->atwar[w->war[i].def[k]] = 1; }
	for (i = 0; i < w->ncountry; i++) d->ai->lord[i] = -1;
	for (i = 0; i < w->nally; i++) if (w->ally[i].type == ALLY_VASSAL && w->ally[i].n >= 2) { int k; for (k = 1; k < w->ally[i].n; k++) d->ai->lord[w->ally[i].member[k]] = w->ally[i].member[0]; }
}

static void set_cur(Diplo *d, int u)
{
	World *w = d->w;
	AiState *a = d->ai;
	int i, k;
	a->cur = -1;
	memset(a->foe, 0, sizeof a->foe);
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		int s = 0;
		for (k = 0; k < wr->natt; k++) if (wr->att[k] == u) s = 1;
		for (k = 0; k < wr->ndef; k++) if (wr->def[k] == u) s = 2;
		if (s == 1) for (k = 0; k < wr->ndef; k++) a->foe[wr->def[k]] = 1;
		if (s == 2) for (k = 0; k < wr->natt; k++) a->foe[wr->att[k]] = 1;
	}
	memset(a->trf, 0, sizeof a->trf);
	if (d->p) peace_truces_of(d->p, u, today_of(d), a->trf);
	for (i = 0; i < w->ncountry; i++) a->relu[i] = a->lidx[i] >= 0 || i == u ? world_rel(w, u, i) : 0;   /* the live ones (no other is asked) */
	a->cur = (short)u;
}

/* 0x557ace: an AI country's random mission VP, monthly, with dynamic missions on */
void ai_mission_vp(Diplo *d)
{
	if (!d->w->opt_nomis && !d->w->opt_novp) {               /* 0x557ace: AI only, dynamic missions on */
		static const int mvp[11] = { 30, 20, 20, 30, 15, 50, 5, 5, 5, 10, 2 };
		World *w = d->w;
		int i, D = w->opt_diff + 2;
		for (i = 0; i < d->w->ncountry; i++) {
			int c = i, r;
			if (w->country[c].human || w->country[c].nprov_owned <= 0 || !valid_tag(c)) continue;
			r = drand(d);
			if (r % 1000 < d->opt_aggr + D / 2 + 2) world_vp(w, c, VP_MISS, 1000L * mvp[r % 11]);
		}
	}
}

void ai_month(Diplo *d)
{
	PT0;
	g_cur = d->ai; g_dm = d->ai;
	{	/* the action clock table: when 3/4 full, drop actions older than two years (rebuilt in slot order) */
		AiState *a = d->ai;
		int i, n = 0;
		for (i = 0; i < AI_NLAST; i++) n += a->lu[i] != 0;
		if (n > AI_NLAST * 3 / 4) {
			static short ou[AI_NLAST], ot[AI_NLAST]; static long od[AI_NLAST];
			memcpy(ou, a->lu, sizeof ou); memcpy(ot, a->lt, sizeof ot); memcpy(od, a->lday, sizeof od);
			memset(a->lu, 0, sizeof a->lu);
			for (i = 0; i < AI_NLAST; i++) if (ou[i] && od[i] > today_of(d) - 720) { long *l = last_slot(a, ou[i] - 1, ot[i], 1); if (l) *l = od[i]; }
		}
	}
	refresh(d);
	ties(d);
	ai_refresh(d);
	{ int i; d->nmilally = 0; for (i = 0; i < d->w->nally; i++) if (d->w->ally[i].type == ALLY_MILITARY) d->nmilally++; }
	d->ai->cache_month = d->w->now.year * 12 + d->w->now.month;
	PT(6);
	g_cur = NULL; g_dm = NULL;
}

/* the diplomats whose tick has come (each country keeps its own date, as the exe's AI_TICK events) */
void ai_day(Diplo *d, long today)
{
	World *w = d->w;
	int u;
	d->today = today;
	if (d->ai->cache_month != w->now.year * 12 + w->now.month) ai_month(d);   /* first day of a month: after the day's moves */
	g_cur = d->ai; g_dm = d->ai;
	for (u = 0; u < w->ncountry; u++) {                       /* (the exe starts at 1: its slot 0 is empty; ours is ENG) */
		int nw, na;
		if (!alive(w, u) || w->country[u].human || !valid_tag(u)) continue;
		if (d->ai->next_think[u] > today_of(d)) continue;
		set_cur(d, u);
		nw = w->nwar; na = w->nally;
		think(d, u);
		{	/* 0x5402fb: next tick in rand%(8e) + 4 e (1 + rand%3) + 5 days, e = 6 - AI aggressiveness; at war a fifth.
			 * Ticks are taken at month steps here, so a war tick comes every month (NIEPEWNE: the exe ~3 a month) */
			long e = 6 - d->opt_aggr, b, dl;
			if (e < 1) e = 1;
			b = (drand(d) % 3 + 1) * e;
			dl = drand(d) % (8 * e) + 4 * b + 5;
			if (d->atwar[u]) dl /= 5;
			if (dl < 30) dl = 30;                              /* NIEPEWNE floor (CPU): the exe's war ticks come every 4-17 days */
			d->ai->next_think[u] = today_of(d) + dl;
		}
		d->ai->cur = -1;
		if (w->nwar != nw || w->nally != na) ties(d);
	}
	g_cur = NULL; g_dm = NULL;
}

/* ---------------------------------------------------------------- save game */
void ai_save(const Diplo *d, FILE *f)
{
	const World *w = d->w;
	const AiState *a = d->ai;
	int i;
	ai_month((Diplo *)d);                                     /* the month data from now, as a load rebuilds it: a game going on
	                                                             after a save runs as the loaded one */
	fprintf(f, "amieu2_ai = {");
	for (i = 0; i < w->ncountry; i++) {
		if (!a->mode_day[i] && !a->peace_day[i] && !a->war_day[i] && !a->next_think[i]) continue;
		fprintf(f, " %s = { %d %ld %d %ld %ld %ld }", w->country[i].tag, a->mode[i], a->mode_day[i], a->enemy[i] >= 0 ? a->enemy[i] + 1 : 0, a->peace_day[i], a->war_day[i], a->next_think[i]);
	}
	fprintf(f, " last = {");
	for (i = 0; i < AI_NLAST; i++) if (a->lu[i]) fprintf(f, " %d %s %s %ld", i, w->country[a->lu[i] - 1].tag, w->country[a->lt[i]].tag, a->lday[i]);   /* by slot: the hash stays the same */
	fprintf(f, " } }\n");
}

void ai_restore(Diplo *d, const SNode *root)
{
	World *w = d->w;
	AiState *a = d->ai;
	const SNode *n = script_get(root, "amieu2_ai"), *c, *v;
	if (!n) return;
	memset(a->lu, 0, sizeof a->lu);
	for (c = n->child; c; c = c->next) {
		int i, k = 0;
		long x[6];
		if (!c->key) continue;
		if (!strcmp(c->key, "last")) {
			const char *t[4];
			for (v = c->child; v; ) {
				int got = 0;
				for (; v && got < 4; v = v->next) if (!v->key && v->val) t[got++] = v->val;
				if (got == 4) {
					long sl = parse_int(t[0]);
					int uu = world_country(w, t[1]), tt = world_country(w, t[2]);
					if (sl >= 0 && sl < AI_NLAST && uu >= 0 && tt >= 0) { a->lu[sl] = (short)(uu + 1); a->lt[sl] = (short)tt; a->lday[sl] = parse_int(t[3]); }
				}
			}
			continue;
		}
		if ((i = world_country(w, c->key)) < 0) continue;
		for (v = c->child; v && k < 6; v = v->next) if (!v->key && v->val) x[k++] = parse_int(v->val);
		if (k < 6) continue;
		a->mode[i] = (signed char)x[0]; a->mode_day[i] = x[1]; a->enemy[i] = (short)(x[2] - 1); a->peace_day[i] = x[3]; a->war_day[i] = x[4]; a->next_think[i] = x[5];
	}
	d->today = cal_day(w->now.year, w->now.month, w->now.day);
	ai_month(d);
}

int  ai_mode(const Diplo *d, int c) { return d->ai->mode[c]; }
int  ai_enemy(const Diplo *d, int c) { return d->ai->enemy[c]; }
long ai_war_months(const Diplo *d, int c) { return d->ai->peace_day[c] > 0 ? (d->today - d->ai->peace_day[c]) / 30 : 0; }
