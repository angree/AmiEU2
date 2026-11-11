/* religion.c - see religion.h */
#include <string.h>
#include "religion.h"
#include "script.h"

static const char *const rn[REL_N] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite",
                                       "pagan", "confucianism", "buddhism", "hinduism" };

int rel_index(const World *w, short name) { return world_religion_index(w, name); }

short rel_name(const World *w, int r)
{
	int i;
	if (r < 0 || r >= REL_N) return -1;
	for (i = 0; i < w->nnames; i++) if (script_ieq(w->names[i], rn[r])) return (short)i;
	return -1;
}

/* 0x4310f0 (SPEC_SMALLRULES 2.3 A): protestant needs flag 1, reformed flag 2, counter-reform flag 3 and not 4 */
int rel_exists(const World *w, int r)
{
	if (r == REL_PROTESTANT) return world_flag(w, 1);
	if (r == REL_REFORMED) return world_flag(w, 2);
	if (r == REL_COUNTER) return world_flag(w, 3) && !world_flag(w, 4);
	return 1;
}

int rel_targets(const World *w, int s, int *out)
{
	int n = 0;
	switch (s) {
	case REL_PROTESTANT: out[n++] = REL_CATHOLIC; if (rel_exists(w, REL_REFORMED)) out[n++] = REL_REFORMED; break;
	case REL_REFORMED: out[n++] = REL_PROTESTANT; break;
	case REL_COUNTER: out[n++] = REL_CATHOLIC; break;
	case REL_CATHOLIC: if (rel_exists(w, REL_PROTESTANT)) out[n++] = REL_PROTESTANT; if (rel_exists(w, REL_COUNTER)) out[n++] = REL_COUNTER; break;
	case REL_ORTHODOX: out[n++] = REL_CATHOLIC; break;
	case REL_SUNNI: out[n++] = REL_SHIITE; break;
	case REL_SHIITE: out[n++] = REL_SUNNI; break;
	default: break;                                   /* pagan and the eastern faiths: only in fantasy mode */
	}
	return n;
}

int rel_shown(const World *w, int s, int *out)
{
	int r, n = 0;
	for (r = 0; r < REL_N; r++) {
		if (r == s || r == REL_COUNTER || r == REL_PAGAN || !rel_exists(w, r)) continue;
		if ((s == REL_SUNNI || s == REL_SHIITE) && (r == REL_REFORMED || r >= REL_CONFUCIAN)) continue;
		if (s >= REL_PROTESTANT && s <= REL_ORTHODOX && (r == REL_SHIITE || r >= REL_CONFUCIAN)) continue;
		if (s >= REL_PAGAN && (r == REL_PROTESTANT || r == REL_REFORMED || r == REL_ORTHODOX || r == REL_SHIITE)) continue;
		out[n++] = r;
	}
	return n;
}

const char *rel_label(int s, int r)
{
	static const char *const key[REL_N] = { "PROTESTANT", "REFORMED", "Counter-Reformed", "CATHOLIC", "ORTHODOX", "SUNNI", "SHIITE",
	                                        "PAGAN", "Confucian", "BUDDHIST", "HINDU" };   /* text.csv keys; two have none (literal names) */
	if (r == REL_SUNNI && s != REL_SUNNI && s != REL_SHIITE) return "MOSLEM";
	if (r == REL_CATHOLIC && s >= REL_CONFUCIAN) return "CHRISTIAN";
	return r >= 0 && r < REL_N ? key[r] : "";
}

void rel_set_tol(World *w, int c, int r, int v)
{
	Country *co = &w->country[c];
	int s = rel_index(w, co->religion), others[REL_N], n, k, d, rest;
	if (r < 0 || r >= REL_N || s < 0) return;
	if (v < -100) v = -100;
	if (v > 100) v = 100;
	d = v - co->tol[r];
	co->tol[r] = (short)v;
	n = rel_shown(w, s, others);                     /* the group: the state slider + the shown others, sum 0 */
	others[n++] = s;
	rest = 0;
	for (k = 0; k < n; k++) if (others[k] != r) rest++;
	if (rest > 0) {                                  /* -d spread evenly, the remainder one by one */
		int base = -d / rest, rem = -d - base * rest, sgn = rem < 0 ? -1 : 1;
		if (rem < 0) rem = -rem;
		for (k = 0; k < n; k++) {
			int o = others[k], nv;
			if (o == r) continue;
			nv = co->tol[o] + base + (rem > 0 ? sgn : 0);
			if (rem > 0) rem--;
			co->tol[o] = (short)(nv < -100 ? -100 : nv > 100 ? 100 : nv);
		}
	}
}

int rel_defender(const World *w, int r)
{
	int c;
	for (c = 0; c < w->ncountry; c++) if (w->country[c].defender && w->country[c].nprov_owned > 0 && rel_index(w, w->country[c].religion) == r) return c;
	return -1;
}

int rel_can_defend(const World *w, int c)
{
	int s = rel_index(w, w->country[c].religion);
	if (s < 0 || s >= REL_PAGAN || w->country[c].defender) return 0;
	if (rel_defender(w, s) >= 0) return 0;
	return w->country[c].treasury_m >= 1000000L;
}

int rel_claim_defender(World *w, int c)
{
	int k, r;
	if (!rel_can_defend(w, c)) return 0;
	w->country[c].treasury_m -= 1000000L;
	w->country[c].defender = 1;
	world_news(w, NOTE_OTHER, c, OM_DEFENDER, rel_index(w, w->country[c].religion));   /* OTHERDEFENDER */
	r = rel_index(w, w->country[c].religion);
	for (k = 0; k < w->ncountry; k++)                         /* 0x455660: co-religionists +50 if friends, else -50 */
		if (k != c && w->country[k].nprov_owned > 0 && rel_index(w, w->country[k].religion) == r)
			world_rel_add(w, c, k, world_rel(w, c, k) > 0 ? 50000 : -50000);
	return 1;
}

/* a stateless roll 0..n-1 from the day, the country and a salt: keeps the saves exact without an RNG state */
static int roll(long today, int c, int salt, int n)
{
	unsigned long h = (unsigned long)today * 2654435761UL ^ (unsigned long)(c + 1) * 40503UL ^ (unsigned long)salt * 0x9E3779B9UL;
	h ^= h >> 15; h *= 2246822519UL; h ^= h >> 13; h &= 0xFFFFFFFFUL;
	return (int)((h >> 8) % (unsigned long)n);
}

static int at_war(const World *w, int c)
{
	int i, j;
	for (i = 0; i < w->nwar; i++) {
		for (j = 0; j < w->war[i].natt; j++) if (w->war[i].att[j] == c) return 1;
		for (j = 0; j < w->war[i].ndef; j++) if (w->war[i].def[j] == c) return 1;
	}
	return 0;
}

/* SPEC_SMALLRULES 2.3 F (0x556480) and G (0x557b3a), for every AI country once a month (the trigger
 * frequency of F is NIEZNANE: monthly here) */
void rel_ai_month(World *w, long today)
{
	int c, p, r;
	for (c = 0; c < w->ncountry; c++) {
		Country *co = &w->country[c];
		int s, best, adm;
		long wt[REL_N], bv;
		if (co->human || co->nprov_owned <= 0 || c == w->reb) continue;
		s = rel_index(w, co->religion);
		/* G: Defender of the Faith, only before the Edict of Tolerance */
		if (!world_flag(w, 4) && co->treasury_m > 1000000L && s >= 0 && s < REL_PAGAN && rel_defender(w, s) < 0 && !co->defender
		    && roll(today, c, 7, 100) < co->nprov_owned - 10)
			rel_claim_defender(w, c);
		/* F: the state religion */
		if (co->conv_day > 0 && today - co->conv_day < 6L * 360) continue;
		if (at_war(w, c)) continue;
		adm = co->m_adm ? co->m_adm : 3;
		if (roll(today, c, 1, 10) > adm) continue;
		if (s < REL_PROTESTANT || s > REL_CATHOLIC) continue;
		memset(wt, 0, sizeof wt);
		for (p = 1; p < w->nprov; p++) {
			const Province *pv = &w->prov[p];
			int pr, k, core = 0;
			long v;
			if (pv->owner != c) continue;
			pr = rel_index(w, pv->religion);
			if (pr == REL_PROTESTANT && !world_flag(w, 1)) pr = REL_CATHOLIC;   /* the effective religion (2.3 B) */
			if (pr == REL_REFORMED && !world_flag(w, 2)) pr = REL_CATHOLIC;
			if (pr < 0) continue;
			v = pv->income_m / 1000;                            /* the province's income value 0x47d970 (NIEPEWNE: base tax) */
			for (k = 0; k < 8; k++) if (pv->cores[k] == c) core = 1;
			wt[pr] += core ? 2 * v + 50 : v + (world_culture_ok(w, c, pv->culture) ? 10 : 0);
		}
		best = REL_CATHOLIC; bv = 0;
		for (r = 0; r < REL_N; r++) if (bv < wt[r]) { best = r; bv = wt[r]; }
		if (best != REL_CATHOLIC) {
			int n = 0, k;
			for (k = 0; k < w->ncountry; k++) if (w->country[k].nprov_owned > 0 && rel_index(w, w->country[k].religion) == best) n++;
			if (n < 4 && roll(today, c, 2, 10) > n) continue;
		}
		if (world_flag(w, 4) && s == REL_COUNTER) best = REL_CATHOLIC;   /* back to Catholic after the Edict */
		else if (best == REL_REFORMED && s == REL_CATHOLIC) best = REL_PROTESTANT;
		else if (s == REL_REFORMED && best == REL_CATHOLIC) best = REL_PROTESTANT;
		if (best == s || !rel_exists(w, best)) continue;
		if (roll(today, c, 3, 100) < 5 && rel_convert(w, c, best, today))
			world_news(w, NOTE_RELIGION, c, -1, best);
	}
}

int rel_convert(World *w, int c, int r, long today)
{
	Country *co = &w->country[c];
	int s = rel_index(w, co->religion), t[4], n, k;
	short nm;
	if (s < 0) return 0;
	n = rel_targets(w, s, t);
	for (k = 0; k < n && t[k] != r; k++) ;
	if (k == n || (nm = rel_name(w, r)) < 0) return 0;
	{ int i2; for (i2 = 0; i2 < w->nwar; i2++) { int j2; for (j2 = 0; j2 < w->war[i2].natt; j2++) if (w->war[i2].att[j2] == c) return 0; for (j2 = 0; j2 < w->war[i2].ndef; j2++) if (w->war[i2].def[j2] == c) return 0; } }   /* not while at war */
	if (s != REL_COUNTER && r != REL_COUNTER) {               /* 0x4536c0: the penalties (not to / from counter-reform) */
		int x, i, j;
		co->stability = (short)(co->stability - 5 < -3 ? -3 : co->stability - 5);
		co->badboy_m += 1000;
		for (x = 0; x < w->ncountry; x++) {
			int xr;
			if (x == c || w->country[x].nprov_owned <= 0) continue;
			xr = rel_index(w, w->country[x].religion);
			if (xr == s) world_rel_add(w, c, x, -100000);
			else if (xr == r) world_rel_add(w, c, x, 50000);
		}
		co->defender = 0;
		for (i = w->nally - 1; i >= 0; i--) {                 /* leave the alliances with members of the old faith */
			int in = 0, old = 0;
			if (w->ally[i].type == ALLY_VASSAL) continue;
			for (j = 0; j < w->ally[i].n; j++) {
				if (w->ally[i].member[j] == c) in = 1;
				else if (rel_index(w, w->country[w->ally[i].member[j]].religion) == s) old = 1;
			}
			if (in && old) w->ally[i] = w->ally[--w->nally];
		}
	}
	co->religion = nm;
	world_default_tol(w, c);                                  /* the new faith's defaults */
	co->conv_day = today;
	return 1;
}
