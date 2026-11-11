/* buildings.c - see buildings.h */
#include <string.h>
#include "buildings.h"

static long infl(const Country *co, long v) { long i = co->inflation_u / 1000; return v + v / 1000 * i + v % 1000 * i / 1000; }

static int count_flag(const World *w, int c, int flag)
{
	int p, n = 0;
	for (p = 1; p < w->nprov; p++) if (w->prov[p].owner == c && (w->prov[p].bflags & flag)) n++;
	return n;
}

int bld_manu_count(const World *w, int c)
{
	int p, n = 0;
	for (p = 1; p < w->nprov; p++) if (w->prov[p].owner == c && w->prov[p].manu) n++;
	return n;
}

long bld_cost(const World *w, int c, int p, int kind)
{
	const Country *co = &w->country[c];
	long v;
	(void)p;
	switch (kind) {
	case BK_TAXCOL: v = 50; break;
	case BK_JUDGE: case BK_GOVERNOR: v = 100; break;
	case BK_SHIPYARD: v = 1000 + 100L * count_flag(w, c, PB_SHIPYARD); break;
	case BK_CONSCRIPT: v = 1000 + 250L * count_flag(w, c, PB_CONSCRIPT); break;
	default: {                                           /* manufactory: grows with the count and 20 - ADM, x 50 (NIEPEWNE) */
		int adm = co->m_adm ? co->m_adm : 3, n = bld_manu_count(w, c);
		v = 50L * (20 - adm) * (2 + n / 3) / 2;
		break; }
	}
	return infl(co, v * 1000);
}

int bld_months(int kind)
{
	switch (kind) {
	case BK_TAXCOL: return 12;
	case BK_SHIPYARD: return 36;
	default: return 24;
	}
}

int bld_can(const World *w, const Military *m, int c, int p, int kind)
{
	const Province *pv;
	const Country *co = &w->country[c];
	int i;
	if (p <= 0 || p >= w->nprov) return BLD_BADPROV;
	pv = &w->prov[p];
	if (pv->owner != c || pv->controller != c || pv->population < 1000 || pv->terrain >= 5) return BLD_BADPROV;   /* own city */
	if (m && mil_besieger(m, p) >= 0) return BLD_BADPROV;
	for (i = 0; i < w->nbuild; i++) if (w->build[i].prov == p && w->build[i].ship >= BK_TAXCOL) return BLD_BUSY;   /* PIW_ISIMPROVING */
	switch (kind) {
	case BK_TAXCOL: if (pv->bflags & PB_TAXCOL) return BLD_HAVE; if (co->infra_tech < 1) return BLD_TECH; break;
	case BK_JUDGE: if (pv->bflags & PB_JUDGE) return BLD_HAVE; if (co->infra_tech < 4 || !(pv->bflags & PB_TAXCOL)) return BLD_TECH; break;
	case BK_GOVERNOR: if (pv->bflags & PB_GOVERNOR) return BLD_HAVE; if (co->infra_tech < 5) return BLD_TECH; break;
	case BK_SHIPYARD: if (pv->bflags & PB_SHIPYARD) return BLD_HAVE; if (co->naval_tech < 11 || pv->kind != 1 || pv->income_m <= 5000) return BLD_TECH; break;
	case BK_CONSCRIPT: {
		int K = co->capital;
		if (pv->bflags & PB_CONSCRIPT) return BLD_HAVE;
		if (co->land_tech < 41) return BLD_TECH;
		if (K > 0 && w->prov[K].continent != pv->continent && w->prov[K].region != pv->region) return BLD_TECH;
		break; }
	default:
		if (kind < BK_MANU1 || kind > BK_MANU5) return BLD_BADPROV;
		if (pv->manu) return BLD_HAVE;
		{	static const int need_infra[5] = { 2, 0, 0, 3, 6 };
			int t = kind - BK_MANU1;
			if ((t == 1 && co->land_tech < 18) || (t == 2 && co->naval_tech < 16) || (need_infra[t] && co->infra_tech < need_infra[t])) return BLD_TECH; }
		break;
	}
	if (w->nbuild >= MAX_BUILD) return BLD_FULL;
	if (bld_cost(w, c, p, kind) > co->treasury_m) return BLD_MONEY;
	return BLD_OK;
}

int bld_start(World *w, const Military *m, int c, int p, int kind, long today)
{
	Build *b;
	int ok = bld_can(w, m, c, p, kind);
	if (ok != BLD_OK) return ok;
	if (kind >= BK_MANU1 && kind <= BK_MANU5) world_book_exp(w, c, EXP_MANU, bld_cost(w, c, p, kind));   /* the officials / shipyard: no category found (NIEPEWNE) */
	w->country[c].treasury_m -= bld_cost(w, c, p, kind);
	b = &w->build[w->nbuild++];
	memset(b, 0, sizeof *b);
	b->country = (short)c; b->prov = (short)p; b->ship = (short)kind; b->done = today + 30L * bld_months(kind);
	return BLD_OK;
}

void bld_done(World *w, const Build *b)
{
	Province *pv = &w->prov[b->prov];
	if (pv->owner != b->country) return;                      /* lost meanwhile */
	world_vp(w, b->country, VP_EST, 1000);                    /* EXEC_BUILD_FACILITY: +1 */
	switch (b->ship) {
	case BK_TAXCOL: pv->bflags |= PB_TAXCOL; break;
	case BK_JUDGE: pv->bflags |= PB_JUDGE; break;
	case BK_GOVERNOR: pv->bflags |= PB_GOVERNOR; break;
	case BK_SHIPYARD: pv->bflags |= PB_SHIPYARD; break;
	case BK_CONSCRIPT: pv->bflags |= PB_CONSCRIPT; break;
	default: if (b->ship >= BK_MANU1 && b->ship <= BK_MANU5) pv->manu = (unsigned char)(b->ship - BK_MANU1 + 1); break;
	}
}

/* the AI (ours, NIEPEWNE: the original has a decision routine per item): once a month at most one
 * improvement per country, the first one it can afford three times over, officials first */
void bld_ai_month(World *w, const Military *m, long today)
{
	static const int order[7] = { BK_TAXCOL, BK_GOVERNOR, BK_JUDGE, BK_MANU1 + 4, BK_MANU1, BK_MANU1 + 3, BK_SHIPYARD };
	static short first[MAX_COUNTRY], next[MAX_PROV];
	static unsigned char busy[MAX_PROV];
	int c, p, k;
	for (c = 0; c < w->ncountry; c++) first[c] = -1;
	memset(busy, 0, sizeof busy);
	for (k = 0; k < w->nbuild; k++) if (w->build[k].ship >= BK_TAXCOL && w->build[k].prov > 0 && w->build[k].prov < MAX_PROV) busy[w->build[k].prov] = 1;
	for (p = w->nprov - 1; p > 0; p--) {                 /* own cities only, as lists per owner */
		int o = w->prov[p].owner;
		next[p] = -1;
		if (o >= 0 && o < w->ncountry && w->prov[p].population >= 1000 && !busy[p]) { next[p] = first[o]; first[o] = (short)p; }
	}
	for (c = 0; c < w->ncountry; c++) {
		const Country *co = &w->country[c];
		int done = 0;
		long cash = co->treasury_m - (w->ai_resv ? w->ai_resv(w, c) : 0);   /* less the envoy plans' money (military.c AI_CASH) */
		if (co->human || first[c] < 0 || cash < 150000) continue;
		for (k = 0; k < 7 && !done; k++) {
			long cost = -1;
			for (p = first[c]; p > 0 && !done; p = next[p]) {
				if (cost < 0) cost = bld_cost(w, c, p, order[k]);   /* the same for every city of the country */
				if (cost * 3 > cash) break;
				if (bld_can(w, m, c, p, order[k]) == BLD_OK) done = bld_start(w, m, c, p, order[k], today) == BLD_OK;
			}
		}
	}
}
