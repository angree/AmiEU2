/* diplo.c - see diplo.h. Section numbers refer to docs/SPEC_WAR_DIPLOMACY.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "diplo.h"
#include "script.h"

#include "diplo_int.h"
#include "revolts.h"

void diplo_link_peace(Diplo *d);

int drand(Diplo *d) { d->seed = (d->seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((d->seed >> 16) & 0x7FFF); }

void logd(Diplo *d, const char *fmt, ...)
{
	char b[220];
	va_list a;
	if (!d->log) return;
	va_start(a, fmt);
	vsnprintf(b, sizeof b, fmt, a);
	va_end(a);
	d->log(d->loguser, d->today, b);
}

void diplo_set_log(Diplo *d, DiploLogFn fn, void *user) { d->log = fn; d->loguser = user; }
const char *diplo_msg_type(int dm)
{
	static const char *const t[DM_N] = { "INSUTOUS", "WARNTOUS", "TRADEREFUSAL", "TRADEALLOW", "CANCMILUS", "MILALLDISHONOR", "VASALLBREAK", "CLAIMTOUS", "GUARATOUS", "WEMILALLDISHONOR" };
	return dm >= 0 && dm < DM_N ? t[dm] : "";
}
void diplo_set_ask(Diplo *d, DiploAskFn fn, void *user) { d->ask = fn; d->ask_user = user; }
void diplo_set_aggr(Diplo *d, int aggr) { d->opt_aggr = aggr < 0 ? 0 : aggr > 4 ? 4 : aggr; }

int bit(const unsigned char *m, int a, int b) { long i = (long)a * MAX_COUNTRY + b; return (m[i >> 3] >> (i & 7)) & 1; }
void setbit(unsigned char *m, int a, int b) { long i = (long)a * MAX_COUNTRY + b; m[i >> 3] |= (unsigned char)(1 << (i & 7)); }
int neighbours(const Diplo *d, int a, int b) { return bit(d->nb, a, b); }

static int ulcmp(const void *x, const void *y) { unsigned long a = *(const unsigned long *)x, b = *(const unsigned long *)y; return a < b ? -1 : a > b; }
/* 0x43aa90..0x43b0ac (the country recount): for every province c owns, every neighbour owned by another country x adds 1 to
 * x's count; the counts divided by their total are country+0x14de (the spec's "grievance": a border share) */
int diplo_gshare(const Diplo *dc, int c, int x)
{
	Diplo *d = (Diplo *)dc;                                  /* a cache */
	World *w = d->w;
	int k;
	if (!d->m || c < 0 || x < 0) return 0;
	if (d->gs_epoch != (long)w->owner_epoch + 1) {         /* one pass over the provinces: (owner, neighbour's owner) pairs, sorted */
		static unsigned long pr[16384];
		long np = 0, i2, j2;
		int p;
		memset(d->gn, 0, sizeof d->gn);
		for (p = 1; p < w->nprov; p++) {
			short nb[32];
			int nn, q, cc = w->prov[p].owner;
			if (cc < 0) continue;
			nn = mil_adjacent(d->m, p, nb, 32);
			for (q = 0; q < nn && np < 16384; q++) {
				int o = nb[q] > 0 && nb[q] < w->nprov ? w->prov[nb[q]].owner : -1;
				if (o >= 0 && o != cc) pr[np++] = (unsigned long)cc << 16 | (unsigned long)o;
			}
		}
		qsort(pr, (size_t)np, sizeof pr[0], ulcmp);
		for (i2 = 0; i2 < np; i2 = j2) {                       /* one country's run: its total, then each neighbour's count */
			int cc = (int)(pr[i2] >> 16), n = 0;
			long tot;
			for (j2 = i2; j2 < np && (int)(pr[j2] >> 16) == cc; j2++) ;
			tot = j2 - i2;
			{	long k2 = i2;
				while (k2 < j2 && n < 24) {
					long k3 = k2;
					while (k3 < j2 && pr[k3] == pr[k2]) k3++;
					d->gx[cc][n] = (short)(pr[k2] & 0xFFFF); d->gv[cc][n] = (short)((k3 - k2) * 1000L / tot); n++;
					k2 = k3;
				}
			}
			d->gn[cc] = (unsigned char)n;
		}
		d->gs_epoch = (long)w->owner_epoch + 1;
	}
	for (k = 0; k < d->gn[c]; k++) if (d->gx[c][k] == x) return d->gv[c][k];
	return 0;
}

void refresh(Diplo *d)
{
	World *w = d->w;
	static const char *rn[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite",
	                              "pagan", "confucianism", "buddhism", "hinduism" };
	int i, k;
	for (i = 0; i < w->ncountry; i++) {
		const char *r = w->country[i].religion >= 0 ? world_name(w, w->country[i].religion) : "";
		d->rel_idx[i] = -1;
		for (k = 0; k < 11; k++) if (script_ieq(r, rn[k])) d->rel_idx[i] = (signed char)k;
	}
	memset(d->atwar, 0, sizeof d->atwar);
	for (i = 0; i < w->nwar; i++) {
		for (k = 0; k < w->war[i].natt; k++) d->atwar[w->war[i].att[k]] = 1;
		for (k = 0; k < w->war[i].ndef; k++) d->atwar[w->war[i].def[k]] = 1;
	}
	memset(d->men, 0, sizeof d->men);
	for (i = 0; i < w->nland; i++)
		if (!w->land[i].dead && w->land[i].owner >= 0) d->men[w->land[i].owner] += w->land[i].infantry + w->land[i].cavalry + 100 * w->land[i].artillery;
	if (d->m) mil_neighbour_bits(d->m, d->nb);
	refresh_ties(d);
}

/* royal marriages and military alliances (the part of refresh that diplomacy changes) */
void refresh_ties(Diplo *d)
{
	World *w = d->w;
	int i, k;
	memset(d->dyn, 0, sizeof d->dyn);
	for (i = 0; i < w->ncountry; i++) d->milally[i] = -1;
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		int j;
		for (k = 0; k < al->n; k++) {
			if (al->type == ALLY_MILITARY && d->milally[al->member[k]] < 0) d->milally[al->member[k]] = (short)i;
			if (al->type == ALLY_DYNASTIC) for (j = 0; j < al->n; j++) if (j != k) setbit(d->dyn, al->member[k], al->member[j]);
		}
	}
}

static int dp_mil_query(void *u, int c, int what)        /* MilAiQuery: 0 the strategic mode, 1 the war target */
{
	const Diplo *d = (const Diplo *)u;
	if (!d || c < 0 || c >= MAX_COUNTRY) return what ? -1 : 0;
	return what ? ai_enemy(d, c) : ai_mode(d, c);
}

Diplo *diplo_new(World *w, Military *m, Peace *p, unsigned long seed)
{
	Diplo *d = (Diplo *)calloc(1, sizeof *d);
	if (!d) return NULL;
	d->w = w; d->m = m; d->p = p; d->seed = seed ? seed : 1;
	memset(d->aiwar, 50, sizeof d->aiwar);
	memset(d->aiexp, 1, sizeof d->aiexp); memset(d->aimono, 5, sizeof d->aimono);
	d->opt_aggr = 2;
	{ int i2; for (i2 = 1; i2 < w->nprov && !d->eur_cont; i2++) { const char *cn = world_name(w, w->prov[i2].continent); if (cn && script_ieq(cn, "Europe")) d->eur_cont = (short)(w->prov[i2].continent + 1); } }
	if ((d->ai = ai_new()) == NULL) { free(d); return NULL; }
	if (m) mil_set_ai_query(m, dp_mil_query, d);          /* the military AI reads the strategic mode and the war target */
	diplo_link_peace(d);
	refresh(d);
	return d;
}

void diplo_free(Diplo *d) { if (d) ai_free(d->ai); free(d); }

/* AI/<file>.ai (ai_Readme.txt): `war = 0..100` warmonger scale and `combat = { TAGS }` historical
 * targets; the army weights (conquer / garrison blocks, base, front) go to the military AI (mil_set_ai_weights). */
/* an .ai name list (area 0 / region 1 / continent 2) as the interned ids the provinces carry; names are
   matched without regard to case ("carribean", "west africa"); unknown names are dropped */
static void ai_names(const World *w, const SNode *l, int kind, short *out, int max)
{
	const SNode *t;
	int n = 0, p;
	for (t = l ? l->child : NULL; t && n < max; t = t->next) {
		short id = -1;
		if (t->key || !t->val) continue;
		for (p = 1; p < w->nprov && id < 0; p++) {
			short x = kind == 0 ? w->prov[p].area : kind == 1 ? w->prov[p].region : w->prov[p].continent;
			const char *nm = x >= 0 ? world_name(w, x) : NULL;
			if (nm && script_ieq(nm, t->val)) id = x;
		}
		if (id >= 0) out[n++] = id;
	}
}

int diplo_load_ai(Diplo *d, Pak *text)
{
	World *w = d->w;
	static char done[32][20]; static unsigned char dwar[32], dexp[32], dmono[32], dfero[32]; static short dtag[32][32]; static unsigned char dn[32];
	static long dw[32][AIW_N];
	static unsigned char dtr[32], dtp[32]; static short dnb[32], den[32], dar[32][20], drg[32][8], dct[32][6];
	static const char *const cqk[6] = { "enemy", "supply", "distance", "owner", "notsupply", "base" }, *const gak[5] = { "fortress", "strategic", "size", "supply", "war" };
	static const long wdef[AIW_N] = { 5000, 100, 500, 2000, 1000, 1000, 1000, 3000, 1000, 2000, 5000, 100, 5000 };   /* default.ai */
	int nd = 0, c, i, loaded = 0;
	for (c = 0; c < w->ncountry; c++) {
		const char *fn = w->country[c].ai_file[0] ? w->country[c].ai_file : "default.ai";
		int k = -1;
		for (i = 0; i < nd; i++) if (script_ieq(done[i], fn)) k = i;
		if (k < 0 && nd < 32) {                          /* first use of this file: parse it */
			char path[48], err[80];
			unsigned long sz;
			char *buf;
			k = nd++;
			snprintf(done[k], sizeof done[k], "%s", fn);
			dwar[k] = 50; dn[k] = 0; dexp[k] = 1; dmono[k] = 5; dfero[k] = 0;
			dtr[k] = 100; dtp[k] = 20; dnb[k] = 50; den[k] = -50;          /* default.ai */
			for (i = 0; i < 20; i++) dar[k][i] = -1;
			for (i = 0; i < 8; i++) drg[k][i] = -1;
			for (i = 0; i < 6; i++) dct[k][i] = -1;
			memcpy(dw[k], wdef, sizeof dw[k]);
			snprintf(path, sizeof path, "ai/%s", fn);
			for (i = 0; path[i]; i++) if (path[i] >= 'A' && path[i] <= 'Z') path[i] += 32;
			if ((buf = (char *)pak_load(text, path, &sz)) != NULL) {
				Arena *a = arena_new(16384);
				SNode *root = a ? script_parse(buf, sz, a, err, sizeof err) : NULL, *cb, *t;
				if (root) {
					long v = script_int(root, "war", 50);
					dwar[k] = (unsigned char)(v < 0 ? 0 : v > 100 ? 100 : v);
					v = script_int(root, "expansion", 1); dexp[k] = (unsigned char)(v < 0 ? 0 : v > 100 ? 100 : v);
					v = script_int(root, "monopoly", 5); dmono[k] = (unsigned char)(v < 0 ? 0 : v > 100 ? 100 : v);
					dfero[k] = (unsigned char)script_ieq(script_str(root, "ferocity", "no"), "yes");
					v = script_int(root, "traders", 100); dtr[k] = (unsigned char)(v < 0 ? 0 : v > 100 ? 100 : v);
					v = script_int(root, "tradingpost", 20); dtp[k] = (unsigned char)(v < 0 ? 0 : v > 100 ? 100 : v);
					dnb[k] = (short)script_int(root, "neighbour", 50); den[k] = (short)script_int(root, "enemies", -50);
					ai_names(w, script_get(root, "area"), 0, dar[k], 20);
					ai_names(w, script_get(root, "region"), 1, drg[k], 8);
					ai_names(w, script_get(root, "continent"), 2, dct[k], 6);
					{	const SNode *cq = script_get(root, "conquer"), *ga = script_get(root, "garrison");
						int j;
						for (j = 0; j < 6 && cq; j++) dw[k][AIW_ENEMY + j] = script_milli(cq, cqk[j], wdef[AIW_ENEMY + j]);
						for (j = 0; j < 5 && ga; j++) dw[k][AIW_GFORT + j] = script_milli(ga, gak[j], wdef[AIW_GFORT + j]);
						dw[k][AIW_RBASE] = script_milli(root, "base", wdef[AIW_RBASE]); dw[k][AIW_RFRONT] = script_milli(root, "front", wdef[AIW_RFRONT]);
					}
					for (cb = script_get(root, "combat"), t = cb ? cb->child : NULL; t && dn[k] < 32; t = t->next) {
						short o = t->val ? world_country(w, t->val) : -1;
						if (o >= 0) dtag[k][dn[k]++] = o;
					}
					loaded++;
				}
				if (a) arena_free(a);
				free(buf);
			}
		}
		if (k < 0) continue;
		w->country[c].ai_traders = dtr[k]; w->country[c].ai_tpost = dtp[k]; w->country[c].ai_exp = dexp[k];
		w->country[c].ai_nb = dnb[k]; w->country[c].ai_en = den[k];
		memcpy(w->country[c].ai_area, dar[k], sizeof dar[k]); memcpy(w->country[c].ai_region, drg[k], sizeof drg[k]); memcpy(w->country[c].ai_cont, dct[k], sizeof dct[k]);
		d->aiwar[c] = dwar[k]; d->aiexp[c] = dexp[k]; d->aimono[c] = dmono[k]; d->aifero[c] = dfero[k]; d->ncombat[c] = dn[k];
		if (d->m) mil_set_ai_weights(d->m, c, dw[k]);
		for (i = 0; i < dn[k]; i++) setbit(d->aicombat, c, dtag[k][i]);
	}
	return loaded;
}

int same_religion(int ra, int rb) { return ra == rb || ((ra == R_CATH || ra == R_COUNTER) && (rb == R_CATH || rb == R_COUNTER)); }

long diplo_ceiling(const Diplo *d, int a, int b)
{
	const World *w = d->w;
	int ra = d->rel_idx[a], rb = d->rel_idx[b];
	long c = same_religion(ra, rb) ? 125000L : 25000L;
	if (CHRISTIAN(ra) != CHRISTIAN(rb)) c -= 25000L;
	if (w->dipflag[b][a] & DF_CB) c -= 100000L;
	if (w->dipflag[a][b] & DF_CB) c -= 100000L;
	c -= 250L * diplo_gshare(d, a, b);                      /* - 250 x a's border share with b (country+0x14de) */
	return c > 200000L ? 200000L : c < -200000L ? -200000L : c;
}

static int g_edict;                                    /* world flag 4 (Edict of Tolerance) at this month's drift */
static int hostile_religions(int a, int b)            /* §2.3, 0x45d050; the Edict ends only Protestant-Catholic (SPEC_SMALLRULES 2.3 D) */
{
	if (a < 0 || b < 0) return 0;
	if (!g_edict && (((a == R_PROT || a == R_REF) && (b == R_CATH || b == R_COUNTER)) || ((b == R_PROT || b == R_REF) && (a == R_CATH || a == R_COUNTER)))) return 1;
	if ((MUSLIM(a) && CHRISTIAN(b)) || (MUSLIM(b) && CHRISTIAN(a))) return 1;
	if ((a == R_PAGAN) != (b == R_PAGAN)) return 1;
	if ((a == R_SUNNI && b == R_SHIITE) || (a == R_SHIITE && b == R_SUNNI)) return 1;
	if (CHRISTIAN(a) != CHRISTIAN(b)) return 1;
	return 0;
}

/* §2.3: the monthly delta a sees toward b, x1000 */
#ifdef DIPLO_REF
static long drift_one(const Diplo *d, int a, int b, long ceil, long rel)
{
	const World *w = d->w;
	const Country *A = &w->country[a], *B = &w->country[b];
	long dd = 0;
	int nb = neighbours(d, a, b);
	if (ceil < 0 && (A->badboy_m > 1000 || B->badboy_m > 1000)) dd = -50;
	if (ceil > 0) dd += 50;
	if (d->milally[a] >= 0 && d->milally[a] == d->milally[b]) dd += 250;
	if (bit(d->dyn, a, b)) dd += 250;
	if (w->dipflag[a][b] & DF_NOTRADE) dd -= 100;          /* record +0x39: we embargo them (SPEC_DIP_ACTIONS 2.8) */
	if (mil_at_war(w, a, b)) dd -= 100;                    /* record +0x36: at war */
	if (w->dipflag[a][b] & DF_TRADEAGR) dd += 50;          /* +0x40: trade agreement */
	if (w->dipflag[b][a] & DF_ACCESS) dd += 25;            /* +0x41: we have military access there */
	if (diplo_has_pact(d, a, b, PACT_GUARANTEE)) dd += 80; /* +0x2a: our guarantee of them, 240 months */
	if (B->badboy_m > 10000) dd -= 500;
	if (B->badboy_m > 15000) dd -= 500;
	if (B->badboy_m > 20000) dd -= 500;
	if (nb || B->nprov_owned > 200) {
		int cnt = (B->land_tech - A->land_tech > 2) + (B->naval_tech - A->naval_tech > 2) + (B->trade_tech - A->trade_tech > 2) + (A->infra_tech > B->infra_tech);
		dd += (long)cnt * 1000 / 12;
	}
	{	/* + 0.025 x our tolerance toward their religion (0x43c970), not toward pagans */
		int br = world_religion_index(w, B->religion);
		if (br >= 0 && br != 7) dd += (long)A->tol[br] * 5 / 2;
	}
	if (hostile_religions(d->rel_idx[a], d->rel_idx[b])) dd -= 100;
	if (nb && !d->atwar[a] && !d->atwar[b]) dd += 50;
	if (rel > ceil) dd -= 20;
	if (dd > 416) dd = 416;
	if (dd < -416) dd = -416;
	if (rel > ceil && dd > 0) dd = 0;
	return dd;
}
#endif

/* The same rules as drift_one, restructured for the 68k (drift_one stays as the readable reference;
 * test_diplo proves both give identical results): every term that is symmetric in (a, b) is computed
 * once per pair, religion terms come from 12x12 tables, per-country values are gathered first. */
#ifdef DIPLO_REF
static void drift(Diplo *d)                           /* reference loop: host builds with -DDIPLO_REF */
{
	World *w = d->w;
	static short live[MAX_COUNTRY];
	int n = 0, i, j;
	for (i = 0; i < w->ncountry; i++) if (w->country[i].nprov_owned > 0) live[n++] = (short)i;
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++) {
			int a = live[i], b = live[j];
			long rel = world_rel(w, a, b);
			long dab = drift_one(d, a, b, diplo_ceiling(d, a, b), rel), dba = drift_one(d, b, a, diplo_ceiling(d, b, a), rel);
			if (dab + dba) world_rel_add(w, a, b, (dab + dba) / 2);
		}
}
#else
static long fin(long dd, long rel, long ceil)
{
	if (dd > 416) dd = 416;
	if (dd < -416) dd = -416;
	if (rel > ceil && dd > 0) dd = 0;
	return dd;
}

static void drift(Diplo *d)
{
	World *w = d->w;
	static short live[MAX_COUNTRY];
	static long ceil0[12][12], tri[MAX_COUNTRY], bpen[MAX_COUNTRY];
	static signed char host[12][12];
	static unsigned char rx[MAX_COUNTRY], bbig[MAX_COUNTRY], big[MAX_COUNTRY];
	static signed char ti[MAX_COUNTRY];                  /* religion index 0..10 for the tolerance term */
	static short tt[MAX_COUNTRY][12];                   /* 0.025 x tolerance toward faith r, x1000 (index 11: none) */
	static short gsb[MAX_COUNTRY];
	static unsigned char gua[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];   /* active guarantees a -> b this month */
	int n = 0, i, j;
	g_edict = world_flag(w, 4);
	memset(gua, 0, sizeof gua);
	for (i = 0; i < d->npact; i++) if (d->pact[i].kind == PACT_GUARANTEE && d->pact[i].until > d->today) setbit(gua, d->pact[i].a, d->pact[i].b);
	for (i = 0; i < 12; i++)
		for (j = 0; j < 12; j++) {
			int ra = i - 1, rb = j - 1;
			long c = same_religion(ra, rb) ? 125000L : 25000L;
			if (CHRISTIAN(ra) != CHRISTIAN(rb)) c -= 25000L;
			ceil0[i][j] = c;
			host[i][j] = (signed char)hostile_religions(ra, rb);
		}
	for (i = 0; i < w->ncountry; i++) {
		const Country *C = &w->country[i];
		tri[i] = (long)i * (i - 1) / 2;
		if (C->nprov_owned <= 0) continue;
		live[n++] = (short)i;
		rx[i] = (unsigned char)(d->rel_idx[i] + 1);
		bbig[i] = C->badboy_m > 1000;
		bpen[i] = (C->badboy_m > 10000 ? -500 : 0) + (C->badboy_m > 15000 ? -500 : 0) + (C->badboy_m > 20000 ? -500 : 0);
		big[i] = C->nprov_owned > 200;
		ti[i] = (signed char)world_religion_index(w, C->religion);
		if (ti[i] < 0 || ti[i] == 7) ti[i] = 11;          /* pagans and others: no term */
		{ int r; for (r = 0; r < 11; r++) tt[i][r] = (short)(C->tol[r] * 5 / 2); tt[i][11] = 0; }
	}
	diplo_gshare(d, 0, 0);                                 /* refresh the border-share cache */
	for (j = 1; j < n; j++) {
		int b = live[j];
		const Country *B = &w->country[b];
		for (i = 0; i < d->gn[b]; i++) gsb[d->gx[b][i]] = d->gv[b][i];   /* b's border shares toward each a */
		for (i = 0; i < j; i++) {
			int a = live[i], nb = neighbours(d, a, b);
			const Country *A = &w->country[a];
			long *rp = &w->rel_m[tri[b] + a], rel = *rp, ceil = ceil0[rx[a]][rx[b]], cab, cba, com = 0, dab, dba;
			unsigned char fab = w->dipflag[a][b], fba = w->dipflag[b][a];
			if (fab & DF_CB) ceil -= 100000L;
			if (fba & DF_CB) ceil -= 100000L;
			cab = cba = ceil;                             /* - 250 x the border share, per direction (diplo_ceiling) */
			if (gsb[a]) { int k; cba -= 250L * gsb[a]; for (k = 0; k < d->gn[a]; k++) if (d->gx[a][k] == b) { cab -= 250L * d->gv[a][k]; break; } }
			cab = cab > 200000L ? 200000L : cab < -200000L ? -200000L : cab;
			cba = cba > 200000L ? 200000L : cba < -200000L ? -200000L : cba;
			if (d->milally[a] >= 0 && d->milally[a] == d->milally[b]) com += 250;
			if (bit(d->dyn, a, b)) com += 250;
			if (mil_at_war(w, a, b)) com -= 100;            /* record +0x36: at war */
			if (host[rx[a]][rx[b]]) com -= 100;
			if (nb && !d->atwar[a] && !d->atwar[b]) com += 50;
			dab = com + bpen[b]; dba = com + bpen[a];
			if (cab < 0 && (bbig[a] || bbig[b])) dab -= 50;
			if (cba < 0 && (bbig[a] || bbig[b])) dba -= 50;
			if (cab > 0) dab += 50;
			if (cba > 0) dba += 50;
			if (rel > cab) dab -= 20;
			if (rel > cba) dba -= 20;
			if (fab & DF_NOTRADE) dab -= 100;               /* +0x39: we embargo them */
			if (fab & DF_TRADEAGR) dab += 50;               /* +0x40: trade agreement (per record: a scenario may set one side) */
			if (fba & DF_TRADEAGR) dba += 50;
			if (fba & DF_NOTRADE) dba -= 100;
			if (fba & DF_ACCESS) dab += 25;                 /* +0x41: our military access there */
			if (fab & DF_ACCESS) dba += 25;
			if (bit(gua, a, b)) dab += 80;                  /* +0x2a: our guarantee of them */
			if (bit(gua, b, a)) dba += 80;
			dab += tt[a][ti[b]]; dba += tt[b][ti[a]];         /* + 0.025 x tolerance toward their faith (0x43c970) */
			if (nb || big[b]) dab += (long)((B->land_tech - A->land_tech > 2) + (B->naval_tech - A->naval_tech > 2) + (B->trade_tech - A->trade_tech > 2) + (A->infra_tech > B->infra_tech)) * 1000 / 12;
			if (nb || big[a]) dba += (long)((A->land_tech - B->land_tech > 2) + (A->naval_tech - B->naval_tech > 2) + (A->trade_tech - B->trade_tech > 2) + (B->infra_tech > A->infra_tech)) * 1000 / 12;
			dab = fin(dab, rel, cab) + fin(dba, rel, cba);
			if (dab) {                                    /* both directions write the one shared value: averaged (NIEPEWNE) */
				rel += dab / 2;
				*rp = rel > 200000L ? 200000L : rel < -200000L ? -200000L : rel;
			}
		}
		for (i = 0; i < d->gn[b]; i++) gsb[d->gx[b][i]] = 0;
	}
}

#endif

static int date_before(const Date *x, const Date *y) { return x->year != y->year ? x->year < y->year : x->month != y->month ? x->month < y->month : x->day < y->day; }

static void expire_alliances(Diplo *d)
{
	World *w = d->w;
	int i;
	for (i = w->nally - 1; i >= 0; i--)
		if (w->ally[i].expiry.year > 0 && date_before(&w->ally[i].expiry, &w->now)) {
			logd(d, "alliance (type %d) of %s and %d others expires", w->ally[i].type, w->country[w->ally[i].member[0]].tag, w->ally[i].n - 1);
			memmove(&w->ally[i], &w->ally[i + 1], (size_t)(w->nally - i - 1) * sizeof w->ally[0]);
			w->nally--;
		}
}

static void stab(Country *c, int d) { int s = c->stability + d; c->stability = (short)(s < -3 ? -3 : s > 3 ? 3 : s); }

static int in_war(const War *wr, int c)
{
	int i;
	for (i = 0; i < wr->natt; i++) if (wr->att[i] == c) return 1;
	for (i = 0; i < wr->ndef; i++) if (wr->def[i] == c) return 2;
	return 0;
}

/* JOIN_WAR (0x44bcb0, SPEC_SMALLRULES 4.4): a truce with anyone of the other side is broken when c joins the
 * defenders, or the war is at least 2 months old: stability -5 (revolt roll with the old stability's bonus),
 * relations -50 with the neighbours, VP -25. Everyone who knows c's capital hears of it (JOINWAROTHER). */
static void join(Diplo *d, War *wr, int side, int c, int leader, const char *why)
{
	World *w = d->w;
	int i, n, truce = 0;
	const short *en;
	if (c < 0 || in_war(wr, c) || w->country[c].nprov_owned <= 0) return;
	en = side == 1 ? wr->def : wr->att; n = side == 1 ? wr->ndef : wr->natt;
	for (i = 0; i < n && d->p; i++) if (en[i] != c && peace_truce(d->p, c, en[i], d->today)) truce = 1;
	if (side == 1 && wr->natt < 24) wr->att[wr->natt++] = (short)c;
	else if (side == 2 && wr->ndef < 24) wr->def[wr->ndef++] = (short)c;
	else return;
	logd(d, "  %s joins %s (%s)", w->country[c].tag, w->country[leader].tag, why);
	if (truce && (side == 2 || (wr->start_day > 0 && d->today - wr->start_day >= 60))) {
		int s = w->country[c].stability - 2;
		stab(&w->country[c], -5);
		if (d->m) rev_roll_country(w, d->m, c, s < 0 ? -s : 0);
		for (i = 0; i < w->ncountry; i++) if (neighbours(d, c, i)) world_rel_add(w, c, i, -50000L);
		world_vp(w, c, VP_DIP, -25000);
		logd(d, "  %s breaks a truce by joining", w->country[c].tag);
	}
	world_news(w, NOTE_JOINWAR, c, leader, side == 1 ? wr->def[0] : wr->att[0]);
}

/* §3.4 call to arms, AI answer ours (NIEPEWNE): vassals always come; military allies come when
 * their relation with the caller is not negative and they are not allied to the enemy leader;
 * a refusal costs -1 stability. Human allies join for now (no dialog yet). */
static void call_to_arms(Diplo *d, War *wr, int side)
{
	World *w = d->w;
	int lead = side == 1 ? wr->att[0] : wr->def[0], enemy = side == 1 ? wr->def[0] : wr->att[0];
	int i, k, ov = world_overlord(w, lead);
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		if (al->type == ALLY_VASSAL && al->member[0] == lead) for (k = 1; k < al->n; k++) join(d, wr, side, al->member[k], lead, "vassal");
	}
	if (ov >= 0 && ov != enemy && side == 2 && !in_war(wr, ov)) join(d, wr, side, ov, lead, "overlord");
	if (!w->country[lead].human && !w->tutorial && d->rel_idx[lead] >= 0 && d->rel_idx[lead] <= 4) {   /* 0x557a20: a Christian AI calls its */
		int cap = w->country[enemy].capital;                                                     /* allies only against a European capital */
		if (!(cap > 0 && cap < w->nprov && d->eur_cont && w->prov[cap].continent == d->eur_cont - 1)) { logd(d, "  %s calls no allies (enemy outside Europe)", w->country[lead].tag); return; }
	}
	short refused[24];
	int nref = 0, j;
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		int has = 0;
		if (al->type != ALLY_MILITARY) continue;
		for (k = 0; k < al->n; k++) if (al->member[k] == lead) has = 1;
		if (!has) continue;
		w->ally[i].expiry = w->now; w->ally[i].expiry.year = (short)(w->now.year + 10);   /* §3.2: expires 10 years after its last war */
		for (k = 0; k < al->n; k++) {
			int c = al->member[k];
			if (c == lead || in_war(wr, c) || w->country[c].nprov_owned <= 0) continue;
			if (world_allied(w, c, enemy, ALLY_MILITARY) || c == enemy) continue;
			if (w->country[c].human ? d->ask && d->ask(d->ask_user, "WPINVPROP", lead, enemy, lead)   /* REQUEST_HONOR_ALLIANCE to the player */
			                        : ai_honour(d, c, lead, enemy, (int)(wr - w->war))) join(d, wr, side, c, lead, "ally");   /* 0x535f40 */
			else {   /* EXEC_DISHONOR_ALLIANCE 0x44c980: a 6-month casus belli for the caller (GIVECB, holder NIEPEWNE),
			          * the messages, stability -1, and the refuser leaves the alliance (0x441000) */
				int m;
				stab(&w->country[c], -1); logd(d, "  %s refuses the call of %s and leaves the alliance", w->country[c].tag, w->country[lead].tag);
				world_give_cb(w, lead, c, d->today + 183);
				if (w->country[c].human) world_note(w, NOTE_DIPMSG, lead, DM_WEDISHONOR, -1);          /* 0x56 to the refuser */
				for (m = 0; m < al->n; m++)                                                           /* 0x55 to a player in the alliance */
					if (al->member[m] != c && w->country[al->member[m]].human) world_note(w, NOTE_DIPMSG, c, DM_DISHONOR, -1);
				world_sfx(w, lead, "diplomat_breach"); world_sfx(w, c, "diplomat_breach");
				if (nref < 24) refused[nref++] = (short)c;
			}
		}
	}
	for (j = 0; j < nref; j++)                               /* out of the alliance it shares with the caller (after the loops) */
		for (i = 0; i < w->nally; i++) {
			int hl = 0, hc = 0;
			if (w->ally[i].type != ALLY_MILITARY) continue;
			for (k = 0; k < w->ally[i].n; k++) { if (w->ally[i].member[k] == lead) hl = 1; if (w->ally[i].member[k] == refused[j]) hc = 1; }
			if (hl && hc) { ally_drop_member(w, i, refused[j]); break; }
		}
	if (nref) refresh_ties(d);
}

static void add_pact(Diplo *d, int a, int b, int kind, long until)
{
	int i, slot = -1;
	for (i = 0; i < d->npact; i++) if (d->pact[i].until <= d->today || (d->pact[i].a == a && d->pact[i].b == b && d->pact[i].kind == kind)) { slot = i; break; }
	if (slot < 0) { if (d->npact >= 96) return; slot = d->npact++; }
	d->pact[slot].a = (short)a; d->pact[slot].b = (short)b; d->pact[slot].kind = (unsigned char)kind; d->pact[slot].until = until;
}

int diplo_has_pact(const Diplo *d, int a, int b, int kind)
{
	int i;
	for (i = 0; i < d->npact; i++) if (d->pact[i].a == a && d->pact[i].b == b && d->pact[i].kind == kind && d->pact[i].until > d->today) return 1;
	return 0;
}

/* the casus belli of a war of a on b (0x4ce3b0, SPEC_SMALLRULES 2.3 E), in its order: (1, the human over the
 * badboy limit at a high difficulty: not modelled) a core of a owned by b; a temporary / permanent CB; a
 * Counter-Reform country against Protestants and the Reformed before the Edict of Tolerance; a Catholic
 * country against the holder of Rome when the Papal State is gone; Spain against Muslims and pagans;
 * Sunni against Shiite and back */
int diplo_cb(const Diplo *d, int a, int b)
{
	const World *w = d->w;
	int ra, rb, p, k, pap;
	if (a < 0 || b < 0 || a == b) return 0;
	if (w->dipflag[a][b] & DF_CB) return 1;
	{	/* "b owns a core of a": a bit matrix rebuilt when the day or the owners change (the AI asks per pair) */
		static unsigned char core_of[MAX_COUNTRY][(MAX_COUNTRY + 7) / 8];
		static long day = -1, epoch = -1;
		static const World *ww;
		if (day != d->today || epoch != (long)w->owner_epoch || ww != w) {
			memset(core_of, 0, sizeof core_of);
			for (p = 1; p < w->nprov; p++) {
				int o = w->prov[p].owner;
				if (o < 0 || o >= MAX_COUNTRY) continue;
				for (k = 0; k < 8; k++) { int c = w->prov[p].cores[k]; if (c >= 0 && c < MAX_COUNTRY && c != o) core_of[c][o >> 3] |= (unsigned char)(1 << (o & 7)); }
			}
			day = d->today; epoch = (long)w->owner_epoch; ww = w;
		}
		if (core_of[a][b >> 3] >> (b & 7) & 1) return 1;
	}
	ra = d->rel_idx[a]; rb = d->rel_idx[b];
	if (ra == R_COUNTER && !world_flag(w, 4) && (rb == R_PROT || rb == R_REF)) return 1;
	pap = world_country(w, "PAP");
	if ((ra == R_CATH || ra == R_COUNTER) && (pap < 0 || w->country[pap].nprov_owned <= 0) && 399 < w->nprov && w->prov[399].owner == b) return 1;
	if (a == world_country(w, "SPA") && (rb == R_SUNNI || rb == R_SHIITE || rb == R_PAGAN)) return 1;
	if ((ra == R_SUNNI && rb == R_SHIITE) || (ra == R_SHIITE && rb == R_SUNNI)) return 1;
	return 0;
}

/* EXEC_LEAVE_ALLIANCE type 2 (0x44a100): the leaver L loses 3 stability (VP -5 booked by the caller), relation -50 with every
   country of P's religion, P gets a CB on L for a year */
static void leave_vassalage(Diplo *d, int L, int P, long today)
{
	World *w = d->w;
	int k;
	stab(&w->country[L], -3);
	for (k = 0; k < w->ncountry; k++) if (k != L && k != P && w->country[k].nprov_owned > 0 && same_religion(d->rel_idx[k], d->rel_idx[P])) world_rel_add(w, L, k, -50000L);
	world_give_cb(w, P, L, today + 360);
}

int diplo_declare_war(Diplo *d, int a, int b, long today)
{
	World *w = d->w;
	Country *A, *B;
	War *wr;
	long rel;
	int cb, i, k;
	d->today = today;
	if (a < 0 || b < 0 || a == b || w->nwar >= MAX_WAR || mil_at_war(w, a, b)) return -1;
	A = &w->country[a]; B = &w->country[b];
	if (A->stability <= -3 || world_allied(w, a, b, ALLY_MILITARY) || (w->dipflag[b][a] & DF_ACCESS)) return -1;   /* §4.1 */
	rel = world_rel(w, a, b);
	cb = diplo_cb(d, a, b);
	logd(d, "%s declares war on %s (relation %ld, %s)", A->tag, B->tag, rel / 1000, cb ? "casus belli" : "no casus belli");
	world_sfx(w, b, "diplomat_war");                         /* THEY_DECLARE_WAR to the attacked player (declaring is silent) */
	/* §4.2 in order */
	if (d->p && peace_truce(d->p, a, b, today)) {
		stab(A, -5); world_vp(w, a, VP_DIP, -25000);
		for (i = 0; i < w->ncountry; i++) if (neighbours(d, a, i)) world_rel_add(w, a, i, -50000L);
		logd(d, "  %s breaks a truce", A->tag);
	}
	{	/* WE_DECLARE_WAR 0x44b2b6: whoever warned a (and is not at war with it) gets a CB on a for 2 years; THEY_DECLARE_WAR
		 * 0x44ba17: b's guarantors get one on a for 6 months; 0x4cc150: b's overlord for a year */
		int ov = world_overlord(w, b);
		for (i = 0; i < d->npact; i++) {
			int g = d->pact[i].a;
			if (d->pact[i].until <= today) continue;
			if (d->pact[i].kind == PACT_GUARANTEE && d->pact[i].b == b && g != a && g != b && !mil_at_war(w, g, b)) world_give_cb(w, g, a, today + 180);
			if (d->pact[i].kind == PACT_WARNING && d->pact[i].b == a && g != b && g != a && !mil_at_war(w, g, a)) world_give_cb(w, g, a, today + 720);
		}
		if (ov >= 0 && ov != a) world_give_cb(w, ov, a, today + 360);
	}
	{	/* 0x4cbfb0: an alliance object holding both is left by the attacker first - a shared royal marriage breaks (VP -5,
		 * stability -1), a shared vassalage too (EXEC_LEAVE_ALLIANCE type 2); the diplomat of the declaration */
		int di = dyn_index(w, a, b), vi = vassal_index(w, a, b) >= 0 ? vassal_index(w, a, b) : vassal_index(w, b, a);
		if (di >= 0) { ally_remove(w, di); stab(A, -1); world_vp(w, a, VP_DIP, -5000); }
		if (vi >= 0) { ally_remove(w, vi); world_vp(w, a, VP_DIP, -5000); leave_vassalage(d, a, b, today); }
		A->diplomats_m -= 1000; if (A->diplomats_m < 0) A->diplomats_m = 0;
	}
	if (rel > 150000L) stab(A, -1);
	if (rel > 100000L) stab(A, -1);
	world_rel_add(w, a, b, -100000L);
	{ int ov = world_overlord(w, b); if (ov >= 0 && ov != a) world_rel_add(w, a, ov, -100000L); }   /* 0x44b4f1: -100 with the target's overlord only */
	(void)k;
	{
		long bb;
		int pagan_target = d->rel_idx[b] == R_PAGAN, europe = A->capital > 0 && A->capital < w->nprov &&
		    script_ieq(world_name(w, w->prov[A->capital].continent), "europe");          /* "continent 2" = Europe, NIEPEWNE */
		if (!cb) { stab(A, -2); world_vp(w, a, VP_DIP, -5000); bb = rel < 0 ? 4000 : 2000; if (B->human) bb /= 3; }
		else { world_vp(w, a, VP_DIP, -3000); bb = B->human ? 0 : 1000; }
		if (!(europe && pagan_target)) A->badboy_m += bb;
	}
	{	/* same religion; from 1615 two different Christian religions no longer count (0x45fbb0) */
		int ra = d->rel_idx[a], rb = d->rel_idx[b];
		if (ra >= 0 && (ra == rb || (w->now.year < 1615 && CHRISTIAN(ra) && CHRISTIAN(rb)))) stab(A, -1);
	}
	if (world_overlord(w, b) == a) stab(A, -3);
	w->dipflag[a][b] &= (unsigned char)~DF_ACCESS;
	wr = &w->war[w->nwar++];
	memset(wr, 0, sizeof *wr);
	wr->att[0] = (short)a; wr->natt = 1;
	wr->def[0] = (short)b; wr->ndef = 1;
	wr->began = w->now; wr->start_day = today; wr->last_combat = today;
	call_to_arms(d, wr, 2);                                /* defenders are asked first (NIEPEWNE order) */
	call_to_arms(d, wr, 1);
	d->atwar[a] = d->atwar[b] = 1;
	return w->nwar - 1;
}

int marriage_ok(int ra, int rb)
{
	if (ra < 0 || rb < 0) return 0;
	if (ra == R_ORTH && CHRISTIAN(rb)) return 1;
	if (rb == R_ORTH && CHRISTIAN(ra)) return 1;
	return same_religion(ra, rb);
}

static void loans_month(Diplo *d)
{
	World *w = d->w;
	int i;
	for (i = 0; i < d->nloan; ) {
		Country *L = &w->country[d->loan[i].lender], *B = &w->country[d->loan[i].borrower];
		long back;
		int pay;
		if (d->today < d->loan[i].due) { i++; continue; }
		back = d->loan[i].amount_m + d->loan[i].amount_m / 1200 * d->loan[i].rate * d->loan[i].months;
		pay = B->nprov_owned > 0 && B->treasury_m >= back;
		if (B->nprov_owned > 0 && !B->human) {                         /* 0x556d10: the AI renounces or pays (given what it lacks) */
			pay = !ai_loan_cancel(d, d->loan[i].borrower, d->loan[i].lender, d->loan[i].rate);
			if (pay && B->treasury_m < back) B->treasury_m = back;
		}
		if (pay) {
			B->treasury_m -= back; L->treasury_m += back;
			world_note(w, NOTE_LOAN_REPAID, d->loan[i].lender, (int)(back / 1000), d->loan[i].borrower);
			logd(d, "%s repays %ld ducats to %s", B->tag, back / 1000, L->tag);
		} else {
			w->dipflag[d->loan[i].lender][d->loan[i].borrower] |= DF_CB_PERM;
			world_rel_add(w, d->loan[i].lender, d->loan[i].borrower, -50000);
			world_note(w, NOTE_LOAN_DEFAULT, d->loan[i].lender, (int)(d->loan[i].amount_m / 1000), d->loan[i].borrower);
			logd(d, "%s defaults on a loan of %ld ducats from %s", B->tag, d->loan[i].amount_m / 1000, L->tag);
		}
		d->loan[i] = d->loan[--d->nloan];
	}
}


static int core_in(const Province *p, int t) { int k; for (k = 0; k < 8; k++) if (p->cores[k] == t) return 1; return 0; }

int diplo_vassal_candidates(const Diplo *d, int c, short *out, int max)
{
	const World *w = d->w;
	int t, p, n = 0;
	for (t = 0; t < w->ncountry && n < max; t++) {
		if (t == c || t == w->reb || w->country[t].nprov_owned > 0) continue;
		for (p = 1; p < w->nprov; p++)
			if (w->prov[p].owner == c && w->prov[p].controller == c && p != w->country[c].capital && core_in(&w->prov[p], t)) { out[n++] = (short)t; break; }
	}
	return n;
}

int diplo_release(Diplo *d, int c, int t, long today)
{
	World *w = d->w;
	int p, n = 0;
	(void)today;
	if (t < 0 || t >= w->ncountry || t == c || w->country[t].nprov_owned > 0 || w->nally >= MAX_ALLY) return 0;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		if (pv->owner != c || pv->controller != c || p == w->country[c].capital || !core_in(pv, t)) continue;
		pv->owner = pv->controller = (short)t;
		w->country[c].nprov_owned--; w->country[t].nprov_owned++;
		if (w->country[t].capital <= 0 || w->prov[w->country[t].capital].owner != t) w->country[t].capital = (short)p;
		n++;
	}
	if (!n) return 0;
	{	int i, k;                                          /* a country that did not exist keeps no old alliances */
		for (i = w->nally - 1; i >= 0; i--) for (k = 0; k < w->ally[i].n; k++) if (w->ally[i].member[k] == t) { ally_remove(w, i); break; }
	}
	if (w->country[t].religion < 0) w->country[t].religion = w->country[c].religion;
	if (w->country[t].culture < 0) w->country[t].culture = w->country[c].culture;
	{	Alliance *al = &w->ally[w->nally++];
		memset(al, 0, sizeof *al);
		al->type = ALLY_VASSAL; al->member[0] = (short)c; al->member[1] = (short)t; al->n = 2;
		al->expiry = w->now; al->expiry.year = (short)(w->now.year + 100); }
	w->owner_epoch++;
	logd(d, "%s grants independence to %s (%d provinces), now its vassal", w->country[c].tag, w->country[t].tag, n);
	return n;
}

int diplo_offer_loan(Diplo *d, int a, int b, long amount_m, int rate, int months, long today)
{
	World *w = d->w;
	Country *A = &w->country[a], *B;
	int i, owed = 0;
	long rel;
	if (a == b || b < 0 || b >= w->ncountry || amount_m <= 0 || months < 1 || A->treasury_m < amount_m || d->nloan >= 32) return DR_FAIL;
	B = &w->country[b];
	if (B->nprov_owned <= 0) return DR_FAIL;
	for (i = 0; i < d->nloan; i++) if (d->loan[i].borrower == b) owed++;
	rel = world_rel(w, b, a) / 1000;
	if (B->human ? (owed >= 2 || !((B->treasury_m < 2 * amount_m && rel >= -50) || (rate <= 3 && rel >= 0)))
	             : !ai_accept_loan(d, b, a, amount_m, rate, months)) return DR_REFUSED;   /* 0x535da0 */
	A->treasury_m -= amount_m; B->treasury_m += amount_m;
	d->loan[d->nloan].lender = (short)a; d->loan[d->nloan].borrower = (short)b; d->loan[d->nloan].rate = (short)rate;
	d->loan[d->nloan].months = (short)months; d->loan[d->nloan].amount_m = amount_m; d->loan[d->nloan].due = today + 30L * months;
	d->nloan++;
	world_rel_add(w, b, a, 10000);                                    /* (ours) */
	logd(d, "%s lends %s %ld ducats at %d %% for %d months", A->tag, B->tag, amount_m / 1000, rate, months);
	return DR_ACCEPTED;
}

/* VP of alliances (EXEC_JOIN_ALLIANCE 0x105 / LEAVE 0x106): joining a royal marriage +2, a vassalage +20,
 * a military alliance +5 (both partners: NIEPEWNE); leaving one -5 */
void diplo_vp_join(Diplo *d, int a, int b, int type)
{
	long v = type == ALLY_DYNASTIC ? 2000 : type == ALLY_VASSAL ? 20000 : 5000;
	world_vp(d->w, a, VP_DIP, v);
	if (b >= 0) world_vp(d->w, b, VP_DIP, v);
}

void diplo_month(Diplo *d, long today)
{
	d->today = today;
	loans_month(d);
	refresh_ties(d);                                       /* the rest of the month data comes from the AI's monthly rebuild (ai_month) */
	expire_alliances(d);
	drift(d);
	{ int i; d->nmilally = 0; for (i = 0; i < d->w->nally; i++) if (d->w->ally[i].type == ALLY_MILITARY) d->nmilally++; }
	/* the AI's month data is built by the first diplo_day of the month, after the day's moves */
	ai_mission_vp(d);
}

void diplo_day(Diplo *d, long today) { world_cb_day(d->w, today); ai_day(d, today); }

/* save game: the RNG (relations, alliances and flags live in the World and are saved with it) */
void diplo_save(const Diplo *d, FILE *f)
{
	int i;
	fprintf(f, "amieu2_diplo = { seed = { %ld %ld } loans = {", (long)(d->seed >> 16), (long)(d->seed & 0xFFFF));
	for (i = 0; i < d->nloan; i++)
		fprintf(f, " %s %s %ld %d %d %ld", d->w->country[d->loan[i].lender].tag, d->w->country[d->loan[i].borrower].tag,
		        d->loan[i].amount_m, d->loan[i].rate, d->loan[i].months, d->loan[i].due);
	fprintf(f, " } vpfirst = { %d %d %d %d %d %d %d %d } pacts = {", d->w->vp_first[0], d->w->vp_first[1], d->w->vp_first[2], d->w->vp_first[3],
	        d->w->vp_first[4], d->w->vp_first[5], d->w->vp_first[6], d->w->vp_first[7]);
	for (i = 0; i < d->npact; i++) if (d->pact[i].until > d->today)
		fprintf(f, " %s %s %d %ld", d->w->country[d->pact[i].a].tag, d->w->country[d->pact[i].b].tag, d->pact[i].kind, d->pact[i].until);
	fprintf(f, " } }\n");
	ai_save(d, f);
}
void diplo_restore(Diplo *d, const SNode *root)
{
	const SNode *q = script_get(script_get(root, "amieu2_diplo"), "seed");
	long v[2]; int n = 0;
	for (q = q ? q->child : NULL; q && n < 2; q = q->next) if (!q->key && q->val) v[n++] = parse_int(q->val);
	if (n == 2) d->seed = (unsigned long)v[0] << 16 | (unsigned long)v[1];
	d->nloan = 0;
	for (q = script_get(script_get(root, "amieu2_diplo"), "loans"), q = q ? q->child : NULL; q && d->nloan < 32; ) {
		const char *t[6];
		int got = 0;
		for (; q && got < 6; q = q->next) if (!q->key && q->val) t[got++] = q->val;
		if (got == 6) {
			int lc = world_country(d->w, t[0]), bc = world_country(d->w, t[1]);
			if (lc >= 0 && bc >= 0) {
				d->loan[d->nloan].lender = (short)lc; d->loan[d->nloan].borrower = (short)bc; d->loan[d->nloan].amount_m = parse_int(t[2]);
				d->loan[d->nloan].rate = (short)parse_int(t[3]); d->loan[d->nloan].months = (short)parse_int(t[4]); d->loan[d->nloan].due = parse_int(t[5]);
				d->nloan++;
			}
		}
	}
	{	const SNode *vf = script_get(script_get(root, "amieu2_diplo"), "vpfirst"), *x;
		int r = 0;
		for (x = vf ? vf->child : NULL; x && r < 8; x = x->next) if (!x->key && x->val) d->w->vp_first[r++] = (unsigned char)parse_int(x->val);
	}
	d->npact = 0;
	for (q = script_get(script_get(root, "amieu2_diplo"), "pacts"), q = q ? q->child : NULL; q && d->npact < 96; ) {
		const char *t[4];
		int got = 0;
		for (; q && got < 4; q = q->next) if (!q->key && q->val) t[got++] = q->val;
		if (got == 4) {
			int ac = world_country(d->w, t[0]), bc = world_country(d->w, t[1]);
			if (ac >= 0 && bc >= 0) { d->pact[d->npact].a = (short)ac; d->pact[d->npact].b = (short)bc; d->pact[d->npact].kind = (unsigned char)parse_int(t[2]); d->pact[d->npact].until = parse_int(t[3]); d->npact++; }
		}
	}
	ai_restore(d, root);
}

/* ------------------------------------------------ the player's diplomatic actions (§3.2, SPEC_UI 4.8)
 * Which actions exist, when they are shown and what they cost follow the decoded action panel; an AI
 * target answers with the exe's rules (ai.c, docs/SPEC_AI.md); a human target (hot seat, not used yet)
 * with our old thresholds. */
static int player_ok(const Diplo *d, int a, int b, long today)         /* 0x45c060: a diplomat, a valid target, no cooldown */
{
	const World *w = d->w;
	if (a < 0 || b < 0 || a == b || b >= w->ncountry || w->country[b].nprov_owned <= 0) return 0;
	if (w->country[a].diplomats_m < 990) return 0;
	if (d->pcd[b] && today - d->pcd[b] < 30) return 0;                   /* one diplomat per target per month (§3.1) */
	return 1;
}

int dyn_index(const World *w, int a, int b)
{
	int i, k, ha, hb;
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		if (al->type != ALLY_DYNASTIC) continue;
		for (ha = hb = 0, k = 0; k < al->n; k++) { if (al->member[k] == a) ha = 1; if (al->member[k] == b) hb = 1; }
		if (ha && hb) return i;
	}
	return -1;
}

void ally_remove(World *w, int i) { w->ally[i] = w->ally[--w->nally]; }

void ally_drop_member(World *w, int i, int c)
{
	Alliance *al = &w->ally[i];
	int k;
	for (k = 0; k < al->n; k++) if (al->member[k] == c) { memmove(&al->member[k], &al->member[k + 1], (size_t)(al->n - k - 1) * sizeof al->member[0]); al->n--; break; }
	if (al->n < 2) ally_remove(w, i);
}

/* §3.2 vassalisation: royal marriage, the same alliance, relation >= 190, the same religion, the target
 * at peace and not larger (fewer provinces) */
static int vassal_ok(const Diplo *d, int a, int b)
{
	const World *w = d->w;
	return dyn_index(w, a, b) >= 0 && d->milally[a] >= 0 && d->milally[a] == d->milally[b] && world_rel(w, a, b) >= 190000L &&
	       same_religion(d->rel_idx[a], d->rel_idx[b]) && !d->atwar[b] && w->country[b].nprov_owned <= w->country[a].nprov_owned &&
	       world_overlord(w, b) < 0;
}

/* §3.2 annexation: our vassal, relation >= 190, at peace, a land border, the same religion (the 10 years
 * of vassalage are not tracked: NIEPEWNE) */
static int annex_ok(const Diplo *d, int a, int b)
{
	const World *w = d->w;
	return world_overlord(w, b) == a && world_rel(w, a, b) >= 190000L && !d->atwar[a] && !d->atwar[b] &&
	       neighbours(d, a, b) && same_religion(d->rel_idx[a], d->rel_idx[b]);
}

int vassal_ok_ai(const Diplo *d, int a, int b) { return vassal_ok(d, a, b); }
int annex_ok_ai(const Diplo *d, int a, int b) { return annex_ok(d, a, b); }

int vassal_index(const World *w, int lord, int v)
{
	int i;
	for (i = 0; i < w->nally; i++)
		if (w->ally[i].type == ALLY_VASSAL && w->ally[i].n >= 2 && w->ally[i].member[0] == lord && w->ally[i].member[1] == v) return i;
	return -1;
}

/* annexation: provinces and armies move over; centralisation -1 */
int diplo_neighbours(const Diplo *d, int a, int b) { return d && a >= 0 && b >= 0 && neighbours(d, a, b); }
void diplo_annex_now(Diplo *d, int lord, int v) { diplo_annex(d, lord, v); }
void diplo_annex(Diplo *d, int a, int b)
{
	World *w = d->w;
	Country *A = &w->country[a];
	int i;
	world_news(w, NOTE_OTHER, a, OM_ANNEX, b);                 /* ANNEXACCEPTOTHER */
	world_vp(w, a, VP_DIP, 20000);                              /* EXEC_SET_ANNEXATION: +20 */
	Country *B = &w->country[b];
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == b) {
		w->prov[i].owner = (short)a; if (w->prov[i].controller == b) w->prov[i].controller = (short)a;
		A->nprov_owned++; B->nprov_owned--;
	}
	for (i = 0; i < w->nland; i++) if (w->land[i].owner == b) w->land[i].owner = (short)a;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == b) w->naval[i].owner = (short)a;
	if ((i = vassal_index(w, a, b)) >= 0) ally_remove(w, i);
	if (B->stability > A->stability) A->stability = B->stability;
	if (A->policy[1] > 0) A->policy[1]--;
	B->capital = -1;
	w->owner_epoch++;
	refresh(d);
}

long diplo_action_cost(const Diplo *d, int a, int b, int act)
{
	const World *w = d->w;
	long k, s1, s2, f, base;
	if (a < 0 || b < 0) return 0;
	switch (act) {
	case DA_GIFT_STATE: case DA_CLAIM: k = 250; break;
	case DA_GIFT_PERS: case DA_WARN: case DA_GUARANTEE: k = 100; break;
	case DA_INTRO: k = 25; break;
	case DA_INSULT: k = 10; break;
	default: return 0;
	}
	/* §2.4 / SPEC_UI 4.8: f = clamp(s1 s2 (1 + 0.1 dDIP), 0.33, 3) */
	s1 = (long)w->country[a].nprov_owned * 4;                              /* x1000: provinces x 0.004 */
	s1 = s1 < 200 ? 200 : s1 > 2000 ? 2000 : s1;
	if (w->country[a].nprov_owned > 1) {
		s2 = (long)w->country[b].nprov_owned * 1000 / w->country[a].nprov_owned;
		s2 = s2 < 200 ? 200 : s2 > 2000 ? 2000 : s2;
	} else s2 = s1;
	f = s1 * s2 / 1000;
	{	/* x (1 + 0.1 dDIP), dDIP = the target's monarch DIP - ours (NIEPEWNE which is a: a better diplomat pays less) */
		int da = w->country[a].m_dip ? w->country[a].m_dip : 3, db = w->country[b].m_dip ? w->country[b].m_dip : 3;
		f = f * (10 + db - da) / 10;
	}
	f = f < 330 ? 330 : f > 3000 ? 3000 : f;
	{	/* f x (1 + inflation); f x 1000000 overflows a 32-bit long (68k) */
		long i = w->country[a].inflation_u / 1000;
		base = f + f / 1000 * i + f % 1000 * i / 1000;                         /* x1000 */
	}
	return k * base;                                                        /* ducats x1000 */
}

int diplo_action_state(const Diplo *d, int a, int b, int act, long today)
{
	const World *w = d->w;
	int ok, war, dyn, vas_ab, vas_ba;
	if (a < 0 || b < 0 || a == b) return DS_HIDDEN;
	ok = player_ok(d, a, b, today);
	war = mil_at_war(w, a, b);
	dyn = dyn_index(w, a, b) >= 0;
	vas_ab = world_overlord(w, b) == a;
	vas_ba = world_overlord(w, a) == b;
	switch (act) {
	case DA_WAR: return war ? DS_HIDDEN : w->country[a].stability > -3 && !world_allied(w, a, b, ALLY_MILITARY) && !vas_ab && !vas_ba && !(w->dipflag[b][a] & DF_ACCESS) ? DS_ON : DS_OFF;   /* §4.1 */
	case DA_PEACE: return !war ? DS_HIDDEN : DS_ON;
	case DA_MARRY: return dyn ? DS_HIDDEN : ok && !war && !vas_ab && !vas_ba && marriage_ok(d->rel_idx[a], d->rel_idx[b]) ? DS_ON : DS_OFF;
	case DA_BREAKDYN: return !dyn ? DS_HIDDEN : ok ? DS_ON : DS_OFF;
	case DA_OFFVAS: return vas_ab ? DS_HIDDEN : ok && vassal_ok(d, a, b) ? DS_ON : DS_OFF;   /* §3.2 */
	case DA_BRKVAS: return vas_ab ? (ok ? DS_ON : DS_OFF) : DS_HIDDEN;
	case DA_CANVAS: return vas_ba ? (ok && !d->atwar[a] ? DS_ON : DS_OFF) : DS_HIDDEN;
	case DA_ANNEX: return ok && vas_ab && annex_ok(d, a, b) ? DS_ON : DS_OFF;
	case DA_ASKACC: return (w->dipflag[b][a] & DF_ACCESS) ? DS_HIDDEN : ok && !war ? DS_ON : DS_OFF;
	case DA_CANACC: {                                       /* 0x45b5d0: and no unit of ours stands in their land */
		int i2, here = 0;
		if (!(w->dipflag[b][a] & DF_ACCESS)) return DS_HIDDEN;
		for (i2 = 0; i2 < w->nland && !here; i2++) { const LandUnit *u = &w->land[i2]; if (!u->dead && u->owner == a && u->location > 0 && u->location < w->nprov && w->prov[u->location].owner == b) here = 1; }
		return ok && !here ? DS_ON : DS_OFF; }
	case DA_REVACC: return (w->dipflag[a][b] & DF_ACCESS) ? (ok ? DS_ON : DS_OFF) : DS_HIDDEN;
	case DA_CREATE: return ok && !war && d->milally[a] < 0 && d->milally[b] < 0 && world_overlord(w, b) < 0 ? DS_ON : DS_OFF;
	case DA_LEAVE: return d->milally[a] >= 0 && w->ally[d->milally[a]].member[0] != a && w->ally[d->milally[a]].member[0] == b && !d->atwar[a] ? DS_ON : DS_OFF;   /* 0x45cdc0: the target is its leader */
	case DA_BAN: return d->milally[a] >= 0 && d->milally[a] == d->milally[b] && w->ally[d->milally[a]].member[0] == a && (w->dipflag[a][b] & DF_CB) ? DS_ON : DS_OFF;
	case DA_INVITE: return ok && !war && d->milally[a] >= 0 && w->ally[d->milally[a]].member[0] == a && d->milally[b] < 0 && w->ally[d->milally[a]].n < 24 ? DS_ON : DS_OFF;
	case DA_JOIN: return ok && !war && d->milally[a] < 0 && d->milally[b] >= 0 ? DS_ON : DS_OFF;
	case DA_EMBARGO: {                                      /* 0x45f620: trade tech 4, no agreement, the last peace 61 months back, a diplomat */
		long lp = d->p ? peace_last(d->p, a, b) : 0;
		if (w->dipflag[a][b] & DF_NOTRADE) return DS_HIDDEN;
		return ok && w->country[a].trade_tech >= 4 && !(w->dipflag[a][b] & DF_TRADEAGR) && !(lp > 0 && today - lp < 61L * 30) ? DS_ON : DS_OFF; }
	case DA_ALLOWTRADE: return (w->dipflag[a][b] & DF_NOTRADE) ? (ok ? DS_ON : DS_OFF) : DS_HIDDEN;
	case DA_OFFTRADE: return (w->dipflag[a][b] & DF_TRADEAGR) ? DS_HIDDEN : ok && !war ? DS_ON : DS_OFF;
	case DA_CANTRADE: return (w->dipflag[a][b] & DF_TRADEAGR) ? (ok ? DS_ON : DS_OFF) : DS_HIDDEN;
	case DA_LOAN: return w->country[a].treasury_m >= 1000 && w->country[b].nprov_owned > 0 ? DS_ON : DS_OFF;   /* window 0x13 */
	case DA_EXCDIS: return DS_OFF;
	case DA_CLAIM: return !dyn ? DS_HIDDEN : ok && !war && w->country[a].treasury_m >= diplo_action_cost(d, a, b, act) ? DS_ON : DS_OFF;   /* DIPROL_CLAIM */
	case DA_GIFT_STATE: case DA_GIFT_PERS: case DA_INTRO:
		return ok && !war && w->country[a].treasury_m >= diplo_action_cost(d, a, b, act) ? DS_ON : DS_OFF;
	case DA_WARN: return ok && !war && !diplo_has_pact(d, a, b, PACT_WARNING) && w->country[a].treasury_m >= diplo_action_cost(d, a, b, act) ? DS_ON : DS_OFF;
	case DA_INSULT: return ok && !war && w->country[a].treasury_m >= diplo_action_cost(d, a, b, act) ? DS_ON : DS_OFF;   /* 0x45b2c0: and the money */
	case DA_GUARANTEE: return ok && !war && !diplo_has_pact(d, a, b, PACT_GUARANTEE) && w->country[a].treasury_m >= diplo_action_cost(d, a, b, act) ? DS_ON : DS_OFF;   /* DIPROL_GUARANTEE */
	default: return DS_HIDDEN;
	}
}

/* runs an action of country a (the player) toward b; the result is DR_* */
int diplo_action(Diplo *d, int a, int b, int act, long today)
{
	World *w = d->w;
	Country *A = &w->country[a];
	long r;
	int res = DR_DONE;
	d->today = today;
	if (diplo_action_state(d, a, b, act, today) != DS_ON) return DR_FAIL;
	r = world_rel(w, a, b);
	switch (act) {
	case DA_WAR:
		return diplo_declare_war(d, a, b, today) >= 0 ? DR_DONE : DR_FAIL;
	case DA_MARRY:                                                         /* 0x5377e0 */
		if ((w->country[b].human ? r > 50000L : ai_accept_marriage(d, b, a)) && w->nally < MAX_ALLY) {
			Alliance *al = &w->ally[w->nally++];
			memset(al, 0, sizeof *al);
			al->type = ALLY_DYNASTIC; al->member[0] = (short)a; al->member[1] = (short)b; al->n = 2;
			al->expiry = w->now; al->expiry.year = (short)(w->now.year + 25);   /* 0x44abf0: 25 */
			res = DR_ACCEPTED;
		} else res = DR_REFUSED;
		break;
	case DA_BREAKDYN:
		{ int i = dyn_index(w, a, b); if (i >= 0) ally_remove(w, i); }
		if (A->stability > -3) A->stability--;
		break;
	case DA_OFFVAS:
		if (w->nally < MAX_ALLY && (w->country[b].human || ai_accept_vassal(d, b, a))) {   /* 0x537ae0 */
			Alliance *al = &w->ally[w->nally++];
			memset(al, 0, sizeof *al);
			al->type = ALLY_VASSAL; al->member[0] = (short)a; al->member[1] = (short)b; al->n = 2;
			al->expiry = w->now; al->expiry.year = (short)(w->now.year + 100);
			res = DR_ACCEPTED;
		} else res = DR_REFUSED;
		break;
	case DA_BRKVAS: case DA_CANVAS: {                                      /* we release them / we break free: EXEC_LEAVE_ALLIANCE type 2 */
		int lord = act == DA_BRKVAS ? a : b, v = act == DA_BRKVAS ? b : a, i = vassal_index(w, lord, v);
		if (i >= 0) ally_remove(w, i);
		leave_vassalage(d, a, b, today);
		break; }
	case DA_ANNEX:                                                         /* 0x536430: the vassal's answer */
		if (!w->country[b].human && !ai_accept_annex(d, b, a)) { res = DR_REFUSED; break; }
		diplo_annex(d, a, b);
		res = DR_ACCEPTED;
		break;
	case DA_OFFTRADE:
		if (w->country[b].human ? r >= 100000L : ai_accept_trade(d, b, a)) { w->dipflag[a][b] |= DF_TRADEAGR; w->dipflag[b][a] |= DF_TRADEAGR; res = DR_ACCEPTED; world_news(w, NOTE_OTHER, a, OM_TRADEAG, b); } else res = DR_REFUSED;   /* 0x5380c0 */
		break;
	case DA_CANTRADE:
		w->dipflag[a][b] &= (unsigned char)~DF_TRADEAGR; w->dipflag[b][a] &= (unsigned char)~DF_TRADEAGR;
		world_news(w, NOTE_OTHER, a, OM_BRKTRADE, b);          /* BRKTRADEOTH */
		world_give_cb(w, b, a, today + 360);                                /* 0x455c50: their CB for a year, relation -20, no stability */
		world_rel_add(w, a, b, -20000L);
		break;
	case DA_ASKACC:
		if (w->country[b].human ? r >= 50000L : ai_accept_access(d, b, a)) { w->dipflag[b][a] |= DF_ACCESS; res = DR_ACCEPTED; } else res = DR_REFUSED;   /* 0x537ed0 */
		break;
	case DA_CANACC:
		w->dipflag[b][a] &= (unsigned char)~DF_ACCESS;
		if (A->stability > -3) A->stability--;
		break;
	case DA_REVACC:
		w->dipflag[a][b] &= (unsigned char)~DF_ACCESS;
		world_give_cb(w, b, a, today + 360);                                /* 0x4561a0: their CB for a year */
		if (A->stability > -3) A->stability--;
		break;
	case DA_CREATE:                                                        /* 0x5372b0 */
		if ((w->country[b].human ? r >= 100000L : ai_accept_alliance(d, b, a, -1)) && w->nally < MAX_ALLY) {
			Alliance *al = &w->ally[w->nally++];
			memset(al, 0, sizeof *al);
			al->type = ALLY_MILITARY; al->member[0] = (short)a; al->member[1] = (short)b; al->n = 2;
			al->expiry = w->now; al->expiry.year = (short)(w->now.year + 10);
			res = DR_ACCEPTED;
		} else res = DR_REFUSED;
		break;
	case DA_INVITE:
		if (w->country[b].human ? r >= 100000L : ai_accept_alliance(d, b, a, d->milally[a])) { Alliance *al = &w->ally[d->milally[a]]; al->member[al->n++] = (short)b; res = DR_ACCEPTED; }
		else res = DR_REFUSED;
		break;
	case DA_JOIN:
		{
			Alliance *al = &w->ally[d->milally[b]];
			int lead = al->member[0], okm = al->n < 24 && (w->country[lead].human ? world_rel(w, a, lead) >= 100000L : ai_allow_join(d, lead, a, d->milally[b]));   /* 0x536300 */
			if (okm) { al->member[al->n++] = (short)a; res = DR_ACCEPTED; } else res = DR_REFUSED;
		}
		break;
	case DA_LEAVE:
		{
			int i = d->milally[a], k;
			for (k = 0; k < w->ally[i].n; k++) if (w->ally[i].member[k] != a) world_give_cb(w, w->ally[i].member[k], a, d->today + 360);   /* 12 months (the text) */
			ally_drop_member(w, i, a);
			if (A->stability > -3) A->stability--;
		}
		break;
	case DA_BAN: {                                         /* 0x44a680: no CB for the banned (the text says so, the code not) */
		int i2 = d->milally[a], k2;
		world_rel_add(w, a, b, -50000L);
		ally_drop_member(w, i2, b);
		if (i2 >= 0) for (k2 = 0; k2 < w->ally[i2].n; k2++) if (w->ally[i2].member[k2] != a) world_rel_add(w, a, w->ally[i2].member[k2], -10000L);
		for (k2 = 0; k2 < w->ntcb; k2++) if (w->tcb[k2].a == a && w->tcb[k2].b == b) w->tcb[k2].until = 0;   /* our timed CBs on them are used up */
		w->dipflag[a][b] &= (unsigned char)~DF_CB_TMP;
		break; }
	case DA_EMBARGO: {                                     /* 0x447c70: -25 with them, -10 with their co-religionists (no stability, no CB in the code) */
		int k2;
		w->dipflag[a][b] |= DF_NOTRADE;
		world_rel_add(w, a, b, -25000L);
		for (k2 = 0; k2 < w->ncountry; k2++) if (k2 != a && k2 != b && w->country[k2].nprov_owned > 0 && same_religion(d->rel_idx[k2], d->rel_idx[b])) world_rel_add(w, a, k2, -10000L);
		break; }
	case DA_ALLOWTRADE:                                                    /* 0x447e40: +15 */
		w->dipflag[a][b] &= (unsigned char)~DF_NOTRADE;
		world_rel_add(w, a, b, 15000L);
		break;
	case DA_GIFT_STATE: case DA_GIFT_PERS: case DA_INTRO:
		{	/* 0x4d3d81 / 0x4548b0: R100 x (DIP + R3 - 1) x k x 0.0033 (k 100 / 50 / 10; R3 drawn first), not the cost; the
			 * cost is booked by us and paid to them (income 7); they learn our capital */
			long cost = diplo_action_cost(d, a, b, act), k = act == DA_GIFT_STATE ? 100 : act == DA_GIFT_PERS ? 50 : 10, r3, r100;
			r3 = drand(d) % 3; r100 = drand(d) % 100;
			A->treasury_m -= cost; world_book_exp(w, a, EXP_EVENT, cost);
			w->country[b].treasury_m += cost; world_book_inc(w, b, INC_EVENT, cost);
			if (A->capital > 0 && A->capital < w->nprov) WORLD_DISCOVER(w, b, A->capital);
			world_rel_add(w, a, b, r100 * ((A->m_dip ? A->m_dip : 3) + r3 - 1) * k * 33 / 10);
		}
		break;
	case DA_WARN:                                                          /* DIPROL_WARNING: 60 months */
		w->dipflag[a][b] |= DF_WARNED;
		add_pact(d, a, b, PACT_WARNING, today + 240L * 30);                 /* 0x4cded0: active 240 months (the text says 60) */
		{ long cst = diplo_action_cost(d, a, b, act); A->treasury_m -= cst; world_book_exp(w, a, EXP_EVENT, cst); }   /* no relation change */
		if (w->country[b].human) world_note(w, NOTE_DIPMSG, a, DM_WARN, -1);
		break;
	case DA_GUARANTEE:                                                     /* DIPROL_GUARANTEE: 5 years */
		add_pact(d, a, b, PACT_GUARANTEE, today + 240L * 30);               /* 0x4cdf10: 240 months (the text says 5 years) */
		{ long cst = diplo_action_cost(d, a, b, act); A->treasury_m -= cst; world_book_exp(w, a, EXP_EVENT, cst); }
		if (w->country[b].human) world_note(w, NOTE_DIPMSG, a, DM_GUARANTEE, -1);
		break;
	case DA_CLAIM: {                                                       /* DIPROL_CLAIM: a CB on them, our marriages suffer */
		int k2;
		{ long cst = diplo_action_cost(d, a, b, act); A->treasury_m -= cst; world_book_exp(w, a, EXP_EVENT, cst); }
		world_give_cb(w, a, b, today + 3L * 360);                           /* 0x4559e0: a CB for 3 years */
		for (k2 = 0; k2 < w->ncountry; k2++) if (k2 != a && dyn_index(w, a, k2) >= 0) world_rel_add(w, a, k2, -100000L);   /* -100 with every royal-marriage partner (them too) */
		if (A->stability > -3) A->stability--;                              /* 0x436490(1) */
		if (d->m) rev_roll_country(w, d->m, b, 0);                           /* one revolt roll for them (host) */
		if (w->country[b].human) world_note(w, NOTE_DIPMSG, a, DM_CLAIM, -1);
		world_news(w, NOTE_OTHER, a, OM_CLAIM, b);             /* CLAIMTOOTH */
		break; }
	case DA_INSULT:                                                        /* 0x455830: -50, their CB for a year */
		{ long cst = diplo_action_cost(d, a, b, act); A->treasury_m -= cst; world_book_exp(w, a, EXP_EVENT, cst); }
		world_rel_add(w, a, b, -50000L);
		if (w->country[b].human) world_note(w, NOTE_DIPMSG, a, DM_INSULT, -1);
		world_give_cb(w, b, a, today + 360);                                /* a 1-year CB (the text) */
		break;
	default:
		return DR_FAIL;
	}
	if (res == DR_ACCEPTED && (act == DA_MARRY || act == DA_OFFVAS || act == DA_CREATE || act == DA_INVITE || act == DA_JOIN))
		diplo_vp_join(d, a, b, act == DA_MARRY ? ALLY_DYNASTIC : act == DA_OFFVAS ? ALLY_VASSAL : ALLY_MILITARY);
	/* SPEC_SOUND 3: EXEC_JOIN_ALLIANCE / EXEC_SET_ANNEXATION -> diplomat_success for a party that is the player; the declined
	   annexation / alliance offer / alliance join -> diplomat_failure to the asker; EXEC_LEAVE_ALLIANCE -> diplomat_breach */
	if (res == DR_ACCEPTED && (act == DA_MARRY || act == DA_OFFVAS || act == DA_CREATE || act == DA_INVITE || act == DA_JOIN || act == DA_ANNEX)) {
		world_sfx(w, a, "diplomat_success"); world_sfx(w, b, "diplomat_success");
	}
	if (res == DR_REFUSED && (act == DA_ANNEX || act == DA_CREATE || act == DA_INVITE || act == DA_JOIN)) world_sfx(w, a, "diplomat_failure");
	if (act == DA_BREAKDYN || act == DA_BRKVAS || act == DA_CANVAS || act == DA_LEAVE) { world_sfx(w, a, "diplomat_breach"); world_sfx(w, b, "diplomat_breach"); }
	if (act == DA_BREAKDYN || act == DA_BRKVAS || act == DA_CANVAS || act == DA_LEAVE) world_vp(w, a, VP_DIP, -5000);   /* EXEC_LEAVE_ALLIANCE: -5 */
	if (act != DA_WAR) { A->diplomats_m -= 1000; if (A->diplomats_m < 0) A->diplomats_m = 0; }
	d->pcd[b] = today;
	refresh(d);
	logd(d, "%s -> %s: action %d, result %d", A->tag, w->country[b].tag, act, res);
	return res;
}

int diplo_milally(const Diplo *d, int c) { return c >= 0 ? d->milally[c] : -1; }

/* the merchant agent's trade agreement plan executes (0x552fe0 can-execute = the DIPOFFTRADE rule, 0x552e80) */
int diplo_ai_offer_trade(void *dd, int a, int b)
{
	Diplo *d = (Diplo *)dd;
	if (diplo_action_state(d, a, b, DA_OFFTRADE, d->today) != DS_ON) return 0;
	return diplo_ai_action(d, a, b, DA_OFFTRADE);
}

int diplo_ai_action(Diplo *d, int a, int b, int act)
{
	long keep = d->pcd[b];
	int r;
	d->pcd[b] = 0;
	r = diplo_action(d, a, b, act, d->today);
	d->pcd[b] = keep;
	return r != DR_FAIL;
}

/* what peace.c's AI part needs */
static int cb_mode(void *u, int c) { return ai_mode((const Diplo *)u, c); }
static int cb_nb(void *u, int a, int b) { return neighbours((const Diplo *)u, a, b); }
static int cb_over(void *u, int c) { const Diplo *d = (const Diplo *)u; return d->w->country[c].badboy_m > ai_badboy_limit(d->w, c) * 1000L; }
static long cb_str(void *u, int c, int naval) { return ai_strength((Diplo *)u, c, naval); }
void diplo_link_peace(Diplo *d) { if (d->p) { peace_set_ai(d->p, d->aifero, cb_mode, cb_nb, cb_over, d); peace_set_military(d->p, d->m); peace_set_strength(d->p, cb_str, d); } }
