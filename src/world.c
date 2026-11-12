/* world.c - see world.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "world.h"
#include "envoys.h"
#include "script.h"

/* ------------------------------------------------------------ strings */
static const char *pool_str(World *w, const char *s)
{
	unsigned long n = strlen(s) + 1;
	char *r;
	if (w->pool_used + n > w->pool_size) {
		/* pools are never moved (pointers into them are kept): chain a new one */
		unsigned long sz = n > 65536 ? n : 65536;
		char *np = malloc(sz + sizeof(char *));
		if (!np) return "";
		*(char **)np = w->pool;              /* link to the previous pool for world_free */
		w->pool = np;
		w->pool_used = sizeof(char *);
		w->pool_size = sz + sizeof(char *);
	}
	r = w->pool + w->pool_used;
	memcpy(r, s, n);
	w->pool_used += n;
	return r;
}

static unsigned hash_ci(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) { unsigned char c = (unsigned char)*s; if (c >= 'a' && c <= 'z') c -= 32; h = (h ^ c) * 16777619u; }
	return h;
}

short world_intern(World *w, const char *name)
{
	unsigned h;
	if (!name || !*name || !strcmp(name, "-") || !strcmp(name, "none")) return -1;
	for (h = hash_ci(name) & 2047; w->name_hash[h]; h = (h + 1) & 2047)
		if (script_ieq(w->names[w->name_hash[h] - 1], name)) return (short)(w->name_hash[h] - 1);
	if (w->nnames >= MAX_NAMES) return -1;
	w->names[w->nnames] = pool_str(w, name);
	w->name_hash[h] = (short)(w->nnames + 1);
	return w->nnames++;
}

static void tag_index_add(World *w, short ci)
{
	unsigned h;
	for (h = hash_ci(w->country[ci].tag) & 1023; w->tag_hash[h]; h = (h + 1) & 1023)
		if (script_ieq(w->country[w->tag_hash[h] - 1].tag, w->country[ci].tag)) return;   /* first one wins, as the linear search did */
	w->tag_hash[h] = (short)(ci + 1);
}

short world_country(const World *w, const char *tag)
{
	unsigned h;
	if (!tag) return -1;
	for (h = hash_ci(tag) & 1023; w->tag_hash[h]; h = (h + 1) & 1023)
		if (script_ieq(w->country[w->tag_hash[h] - 1].tag, tag)) return (short)(w->tag_hash[h] - 1);
	return -1;
}

const char *world_name(const World *w, short id) { return id >= 0 && id < w->nnames ? w->names[id] : "-"; }

/* ------------------------------------------------------------ diplomacy state */
static long rel_index(int a, int b)                  /* unordered pair, a != b */
{
	if (a < b) { int t = a; a = b; b = t; }
	return (long)a * (a - 1) / 2 + b;
}

long world_rel(const World *w, int a, int b)
{
	if (a == b || a < 0 || b < 0 || a >= MAX_COUNTRY || b >= MAX_COUNTRY) return 0;
	return w->rel_m[rel_index(a, b)];
}

void world_rel_set(World *w, int a, int b, long v)
{
	if (a == b || a < 0 || b < 0 || a >= MAX_COUNTRY || b >= MAX_COUNTRY) return;
	w->rel_m[rel_index(a, b)] = v > 200000L ? 200000L : v < -200000L ? -200000L : v;
}

void world_rel_add(World *w, int a, int b, long d) { world_rel_set(w, a, b, world_rel(w, a, b) + d); }

int world_ally_of(const World *w, int c, int type)
{
	int i, k;
	for (i = 0; i < w->nally; i++)
		if (w->ally[i].type == type)
			for (k = 0; k < w->ally[i].n; k++) if (w->ally[i].member[k] == c) return i;
	return -1;
}

int world_allied(const World *w, int a, int b, int type)
{
	int i, k, ina, inb;
	for (i = 0; i < w->nally; i++) {
		if (w->ally[i].type != type) continue;
		ina = inb = 0;
		for (k = 0; k < w->ally[i].n; k++) { if (w->ally[i].member[k] == a) ina = 1; if (w->ally[i].member[k] == b) inb = 1; }
		if (ina && inb) return 1;
	}
	return 0;
}

int world_overlord(const World *w, int c)
{
	int i, k;
	for (i = 0; i < w->nally; i++)
		if (w->ally[i].type == ALLY_VASSAL)
			for (k = 1; k < w->ally[i].n; k++) if (w->ally[i].member[k] == c) return w->ally[i].member[0];
	return -1;
}

void world_free(World *w)
{
	char *p = w->pool;
	while (p) { char *n = *(char **)p; free(p); p = n; }
	w->pool = NULL;
}

/* ------------------------------------------------------------- DB csv */
static char *load_text(Pak *t, const char *name, unsigned long *sz)
{
	return (char *)pak_load(t, name, sz);
}

char *world_read_file(const char *path, unsigned long *sz)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n < 0 || !(b = (char *)malloc((size_t)n + 1))) { fclose(f); return NULL; }
	n = (long)fread(b, 1, (size_t)n, f);
	b[n] = 0;
	fclose(f);
	*sz = (unsigned long)n;
	return b;
}

int world_load_db(World *w, Pak *t, char *err, int errlen)
{
	char *buf, *cur, *line, *f[80];
	unsigned long sz;
	int n, first = 1;
	short i;
	memset(w, 0, sizeof *w);
	for (i = 0; i < MAX_PROV; i++) {
		int k;
		w->prov[i].id = -1; w->prov[i].owner = w->prov[i].controller = -1; w->prov[i].last_ctl = -32768; w->prov[i].last_owner = -32768; w->prov[i].tcot = -1;   /* not loaded */
		for (k = 0; k < 8; k++) w->prov[i].cores[k] = -1;
	}

	if (!(buf = load_text(t, "db/province.csv", &sz))) { snprintf(err, errlen, "db/province.csv missing"); return -1; }
	cur = buf;
	while ((line = csv_line(&cur)) != NULL) {
		Province *p;
		long id;
		if (first) { first = 0; continue; }                  /* header */
		n = csv_split(line, f, 80);
		if (n < 52 || !f[0][0] || f[0][0] < '0' || f[0][0] > '9') continue;
		id = parse_int(f[0]);
		if (id < 0 || id >= MAX_PROV) continue;
		p = &w->prov[id];
		p->id = (short)id;
		p->name = pool_str(w, f[1]);
		p->kind = script_ieq(f[2], "inland") ? 0 : script_ieq(f[2], "coastal") ? 1 : script_ieq(f[2], "sea") ? 2 : 3;
		p->religion = world_intern(w, f[3]);
		p->culture = world_intern(w, f[4]);
		p->size_m = parse_milli(f[5]);
		p->climate = (unsigned char)parse_int(f[7]);
		p->ice = (unsigned char)parse_int(f[8]);
		p->storm = (unsigned char)parse_int(f[9]);
		p->manpower_m = parse_milli(f[11]);
		p->income_m = parse_milli(f[12]);
		p->terrain = (unsigned char)parse_int(f[13]);
		p->hre = (unsigned char)parse_int(f[16]);
		p->tot = (unsigned char)((parse_int(f[14]) ? 1 : 0) | (parse_int(f[15]) ? 2 : 0));
		p->goods = world_intern(w, f[19]);
		p->mine_value = parse_int(f[18]);
		p->city_x = (short)parse_int(f[28]); p->city_y = (short)parse_int(f[29]);
		p->army_x = (short)parse_int(f[30]); p->army_y = (short)parse_int(f[31]);
		p->port_x = (short)parse_int(f[32]); p->port_y = (short)parse_int(f[33]);
		p->manu_x = (short)parse_int(f[34]); p->manu_y = (short)parse_int(f[35]);
		p->port_sea = (short)parse_int(f[36]);
		p->col_diff = (unsigned char)parse_int(f[22]);
		p->cot_hist = (signed char)parse_int(f[21]);
		p->ferocity = (unsigned char)(parse_milli(f[24]) / 100);          /* x10: the values are multiples of 0.5 */
		p->natives = parse_int(f[23]); p->maxnatives = (short)p->natives;   /* "Native Combat Strength", hundreds */
		p->tolerance = (unsigned char)parse_int(f[27]);
		p->negot = (unsigned char)parse_int(f[26]);
		{ int k; for (k = 0; k < 4; k++) { p->tsx[k] = (short)parse_int(f[37 + 3 * k]); p->tsy[k] = (short)parse_int(f[38 + 3 * k]); p->tsv[k] = (unsigned char)parse_int(f[39 + 3 * k]); } }
		p->area = world_intern(w, f[49]);
		p->region = world_intern(w, f[50]);
		p->continent = world_intern(w, f[51]);
		if (id + 1 > w->nprov) w->nprov = (short)(id + 1);
	}
	free(buf);

	if (!(buf = load_text(t, "db/country.csv", &sz))) { snprintf(err, errlen, "db/country.csv missing"); return -1; }
	cur = buf; first = 1;
	while ((line = csv_line(&cur)) != NULL) {
		Country *c;
		int k;
		if (first) { first = 0; continue; }
		n = csv_split(line, f, 40);
		if (n < 17 || strlen(f[0]) != 3 || w->ncountry >= MAX_COUNTRY) continue;
		c = &w->country[w->ncountry++];
		memcpy(c->tag, f[0], 3); c->tag[3] = 0;
		tag_index_add(w, (short)(w->ncountry - 1));
		c->color = world_intern(w, f[1]);
		c->fantasia_color = world_intern(w, f[2]);
		c->techgroup = world_intern(w, f[3]);
		strncpy(c->army_spr, f[5], 3); strncpy(c->navy_spr, f[6], 3); strncpy(c->city_nat, f[7], 3);
		for (k = 0; k < 8; k++) c->policy[k] = (signed char)parse_int(f[8 + k]);
		c->elector = (unsigned char)parse_int(f[16]);
		c->religion = c->culture = c->capital = -1;
	}
	free(buf);
	memset(w->known, 0xFF, sizeof w->known);           /* until a scenario says otherwise */
	w->reb = world_country(w, "REB");
	w->nat = world_country(w, "NAT");
	w->pir = world_country(w, "PIR");
	return 0;
}

/* ---------------------------------------------------------- scenario */
static int month_of(const char *m)
{
	static const char *mn[12] = { "january", "february", "march", "april", "may", "june", "july",
	                              "august", "september", "october", "november", "december" };
	int i;
	if (!m) return 1;
	for (i = 0; i < 12; i++) if (script_ieq(m, mn[i])) return i + 1;
	return (int)parse_int(m);
}

static void read_date(const SNode *b, Date *d)
{
	if (!b) return;
	d->year = (short)script_int(b, "year", d->year);
	d->month = (unsigned char)month_of(script_str(b, "month", "january"));
	d->day = (unsigned char)script_int(b, "day", 0);
}

static const char *type_of(const SNode *b, const char *key)       /* religion = { type = sunni } */
{
	SNode *n = script_get(b, key);
	if (!n) return NULL;
	if (n->val) return n->val;
	return script_str(n, "type", NULL);
}

static void apply_province(World *w, const SNode *b)
{
	long id = script_int(b, "id", -1);
	Province *p;
	const char *s;
	if (id < 0 || id >= MAX_PROV) { w->repairs++; return; }
	p = &w->prov[id];
	if ((s = type_of(b, "religion")) != NULL) p->religion = world_intern(w, s);
	if ((s = type_of(b, "culture")) != NULL) p->culture = world_intern(w, s);
	if (script_get(b, "natives")) { p->natives = script_int(b, "natives", 0); p->natives_present = p->natives > 0; }
	{	/* save game: exact values (savegame.c) */
		const SNode *x = script_get(b, "amieu2");
		if (x) {
			p->income_m = script_int(x, "income", p->income_m);
			p->manpower_m = script_int(x, "manpower", p->manpower_m);
			p->revoltrisk_m = script_int(x, "revolt", p->revoltrisk_m);
			p->population = script_int(x, "population", p->population);
			p->pop_frac_m = script_int(x, "popfrac", p->pop_frac_m);
			p->fort = (unsigned char)script_int(x, "fort", p->fort);
			p->tp = (unsigned char)script_int(x, "tp", p->tp);
			p->bflags = (unsigned char)script_int(x, "bf", p->bflags);
			p->manu = (unsigned char)script_int(x, "manu", p->manu);
			p->acq_year = (short)script_int(x, "acq", 0);
			p->ctl_day = script_int(x, "ctl", 0);
			p->maxnatives = (short)script_int(x, "nmax", p->maxnatives);
			p->conq_done = (unsigned char)script_int(x, "cq", 0);
			p->loot_until = script_int(x, "lt", 0); p->looted = (unsigned char)(p->loot_until > 0);
			{ long lc = script_int(x, "lc", 0); if (lc) { p->last_ctl = (short)(lc - 2); } }
			{ long lo = script_int(x, "lo", 0); if (lo) p->last_owner = (short)(lo - 2); }   /* kept by the reset below */
			p->cot_hist = (signed char)script_int(x, "ch", p->cot_hist);
			p->tcot = (short)script_int(x, "tc", p->tcot);
			{ long wx = script_int(x, "wx", 0); p->wind = (unsigned char)(wx & 15); p->storm_now = (unsigned char)((wx >> 4) & 1); p->winter = (unsigned char)((wx >> 5) & 1); }
		}
	}
}

void world_add_core(World *w, int p, short ci)
{
	int k;
	if (p < 0 || p >= MAX_PROV) return;
	for (k = 0; k < 8; k++) if (w->prov[p].cores[k] == ci) return;
	for (k = 0; k < 8; k++) if (w->prov[p].cores[k] < 0) { w->prov[p].cores[k] = ci; return; }
	w->repairs++;                                        /* more than 8 cores: dropped (NIEPEWNE limit) */
}

void world_remove_core(World *w, int p, short ci)
{
	int k;
	if (p < 0 || p >= MAX_PROV) return;
	for (k = 0; k < 8; k++) if (w->prov[p].cores[k] == ci) w->prov[p].cores[k] = -1;
}

static void list_provinces(World *w, const SNode *list, short ci, int owned)
{
	SNode *n;
	if (!list) return;
	for (n = list->child; n; n = n->next) {
		long id;
		if (n->key || !n->val) continue;
		id = parse_int(n->val);
		if (id < 0 || id >= MAX_PROV) { w->repairs++; continue; }
		if (owned == 2) { world_add_core(w, (int)id, ci); continue; }
		if (owned) { w->prov[id].owner = ci; if (w->prov[id].controller < 0) w->prov[id].controller = ci; }
		else w->prov[id].controller = ci;
	}
}

static short g_save_idx[MAX_LAND]; static int g_have_idx;   /* save game: each land unit's index in the saved game */
static short g_save_nidx[MAX_NAVAL]; static int g_have_nidx;  /* the same for fleets */
/* a unit's own leader in a scenario (the tutorials: "leader = { name category rank fire shock siege movement }"): kept in
   the unit like an event's random leader (LEAD_RANDOM 30000, leaders.h), category general 0 / admiral 1 / conquistador 2 /
   explorer 3 (privateer read as explorer) */
static void scen_leader(World *w, const SNode *l, short *id, unsigned char *type, unsigned char *rank, unsigned char *move,
                        unsigned char *fire, unsigned char *shock, unsigned char *siege, const char **name)
{
	const char *c;
	if (!l) return;
	c = script_str(l, "category", "general");
	*id = 30000;
	*type = (unsigned char)(script_ieq(c, "conquistador") ? 2 : script_ieq(c, "explorer") || script_ieq(c, "privateer") ? 3 : script_ieq(c, "admiral") ? 1 : 0);
	*rank = (unsigned char)script_int(l, "rank", 1); *move = (unsigned char)script_int(l, "movement", 0);
	*fire = (unsigned char)script_int(l, "fire", 0); *shock = (unsigned char)script_int(l, "shock", 0); *siege = (unsigned char)script_int(l, "siege", 0);
	*name = pool_str(w, script_str(l, "name", ""));
}

static void apply_country(World *w, const SNode *b)
{
	const char *tag = script_str(b, "tag", NULL), *s;
	short ci;
	Country *c;
	SNode *n, *tech;
	if (!tag) { w->repairs++; return; }
	ci = world_country(w, tag);
	if (ci < 0) {                                        /* a tag the DB does not list: add it */
		if (w->ncountry >= MAX_COUNTRY) return;
		ci = w->ncountry++;
		memset(&w->country[ci], 0, sizeof w->country[ci]);
		strncpy(w->country[ci].tag, tag, 3);
		tag_index_add(w, ci);
		w->country[ci].color = w->country[ci].religion = w->country[ci].culture = -1;
	}
	c = &w->country[ci];
	c->present = (unsigned char)!parse_yes(script_str(b, "amieu2absent", "no"));   /* ours: saved only for what it holds */
	c->major = (unsigned char)parse_yes(script_str(b, "major", "no"));
	c->treasury_m = script_milli(b, "treasury", 0);
	c->inflation_u = script_milli(b, "inflation", 0) * 1000;
	c->merchants_m = script_milli(b, "merchants", 0);
	c->colonists_m = script_milli(b, "colonists", 0);
	c->diplomats_m = script_milli(b, "diplomats", 0);
	c->stability = (short)script_int(b, "stability", 0);
	c->badboy_m = script_milli(b, "badboy", 0);
	c->we_m = script_milli(b, "warexhaustion", 0);
	c->manpower_m = script_milli(b, "manpower", 0);
	c->loansize = (short)script_int(b, "loansize", c->loansize);
	snprintf(c->flagname, sizeof c->flagname, "%s", script_str(b, "flagname", c->flagname));              /* the scenario pool, thousands of men (0: econ_new_game computes one) */
	if ((n = script_get(b, "policy")) != NULL) {
		static const char *pk[8] = { "aristocracy", "centralization", "innovative", "mercantilism", "offensive", "land", "quality", "serfdom" };
		int k;
		for (k = 0; k < 8; k++) if (script_get(n, pk[k])) c->policy[k] = (signed char)script_int(n, pk[k], 0);
	}
	if ((s = type_of(b, "religion")) != NULL) c->religion = world_intern(w, s);
	if ((s = type_of(b, "culture")) != NULL) c->culture = world_intern(w, s);
	{	/* every `type` of the culture block: the first is the primary, the rest are accepted */
		const SNode *cb = script_get(b, "culture"), *q;
		int first = 1;
		if (cb && !cb->val) {
			c->nacc = 0;
			for (q = cb->child; q; q = q->next) if (q->key && script_ieq(q->key, "type") && q->val) {
				if (first) { first = 0; continue; }
				if (c->nacc < 8) c->acc[c->nacc++] = world_intern(w, q->val);
			}
		}
	}
	c->capital = (short)script_int(b, "capital", -1);
	if ((s = script_str(b, "ai", NULL)) != NULL) snprintf(c->ai_file, sizeof c->ai_file, "%s", s);
	if ((n = script_get(b, "amieu2")) != NULL) {         /* save game: exact values (savegame.c) */
		c->human = (unsigned char)script_int(n, "human", 0);
		c->vp = script_int(n, "vp", 0);
		c->vpt_m = script_int(n, "vpt", c->vp * 1000L);
		{	const SNode *vq = script_get(n, "vpc"), *q2;
			int r2 = 0;
			for (q2 = vq ? vq->child : NULL; q2 && r2 < 7; q2 = q2->next) if (!q2->key && q2->val) c->vpc_m[r2++] = parse_int(q2->val);
		}
		c->manpower_m = script_int(n, "manpower", 0);
		c->missionaries_m = script_int(n, "missionaries", 0);
		c->inflation_u = script_int(n, "inflation", c->inflation_u);
		c->policy_day = script_int(n, "policyday", 0);
		c->inv_land_m = script_int(n, "invland", 0); c->inv_naval_m = script_int(n, "invnaval", 0);
		c->inv_infra_m = script_int(n, "invinfra", 0); c->inv_trade_m = script_int(n, "invtrade", 0);
		c->defender = (unsigned char)script_int(n, "defender", 0); c->conv_day = script_int(n, "convday", 0);
		c->bankrupt_day = script_int(n, "bankrupt", 0);
		c->war_tax = (short)script_int(n, "wartax", 0);
		c->bank = (unsigned char)script_int(n, "bank", 0); c->stock = (unsigned char)script_int(n, "stock", 0);
		{	/* the Ledger's books: yi / ye / li / le lists */
			static const char *bk[4] = { "yi", "ye", "li", "le" };
			int b2;
			for (b2 = 0; b2 < 4; b2++) {
				const SNode *q = script_get(b, bk[b2]), *x;
				long *dst = b2 == 0 ? c->yinc_m : b2 == 1 ? c->yexp_m : b2 == 2 ? c->linc_m : c->lexp_m;
				int k = 0, mx = (b2 & 1) ? 16 : 12;
				for (x = q ? q->child : NULL; x && k < mx; x = x->next) if (!x->key && x->val) dst[k++] = parse_int(x->val);
			}
		}
		if (script_get(n, "trk")) c->tracked = (unsigned char)script_int(n, "trk", 0);
		{	const SNode *q2 = script_get(n, "evst"), *x;
			long v[6]; int k2 = 0;
			for (x = q2 ? q2->child : NULL; x && k2 < 6; x = x->next) if (!x->key && x->val) v[k2++] = parse_int(x->val);
			if (k2 == 6) { int s; for (s = 0; s < 3; s++) { c->ev_stat[s] = (signed char)v[s]; c->ev_stat_until[s] = v[3 + s]; } }
			q2 = script_get(n, "evrr"); k2 = 0;
			for (x = q2 ? q2->child : NULL; x && k2 < 2; x = x->next) if (!x->key && x->val) v[k2++] = parse_int(x->val);
			if (k2 == 2) { c->ev_rr = (short)v[0]; c->ev_rr_until = v[1]; }
		}
		c->inc_m = script_int(n, "inc", 0); c->maint_m = script_int(n, "maint", 0); c->col_rate_m = script_int(n, "colrate", 0); c->mer_rate_m = script_int(n, "merrate", 0);
		{	const SNode *tl = script_get(n, "tol"), *q;
			int r = 0;
			for (q = tl ? tl->child : NULL; q && r < 11; q = q->next) if (!q->key && q->val) c->tol[r++] = (short)parse_int(q->val);
			if (r == 11) c->tol_set = 1;
		}
	}
	if ((n = script_get(b, "diplomacy")) != NULL) {      /* relation = { tag = ENG value = -100 casusbelli = {...} access = yes refusetrade = no } */
		const SNode *r;
		for (r = n->child; r; r = r->next) {
			const char *t;
			short o;
			if (!r->key || !script_ieq(r->key, "relation") || !(t = script_str(r, "tag", NULL)) || (o = world_country(w, t)) < 0 || o == ci) continue;
			world_rel_set(w, ci, o, script_milli(r, "value", 0));          /* both directions: the later file wins (NIEPEWNE) */
			if (script_get(r, "casusbelli")) w->dipflag[ci][o] |= DF_CB_PERM;   /* expiry ignored for now */
			if (parse_yes(script_str(r, "access", "no"))) w->dipflag[ci][o] |= DF_ACCESS;   /* NIEPEWNE direction */
			if (parse_yes(script_str(r, "refusetrade", "no"))) w->dipflag[ci][o] |= DF_NOTRADE;
			if (parse_yes(script_str(r, "amieu2_warned", "no"))) w->dipflag[ci][o] |= DF_WARNED;
			if (parse_yes(script_str(r, "tradeagreement", "no"))) w->dipflag[ci][o] |= DF_TRADEAGR;
		}
	}
	if ((tech = script_get(b, "technology")) != NULL) {
		c->land_tech  = (short)script_int(script_get(tech, "land"), "level", 0);
		c->naval_tech = (short)script_int(script_get(tech, "naval"), "level", 0);
		c->infra_tech = (short)script_int(script_get(tech, "infra"), "level", 0);
		c->trade_tech = (short)script_int(script_get(tech, "trade"), "level", 0);
	}
	{	/* knownprovinces: a country without the list knows the whole map (NIEPEWNE: the original's default) */
		const SNode *kl = script_get(b, "knownprovinces"), *q;
		if (kl) {
			memset(w->known[ci], 0, sizeof w->known[ci]);
			for (q = kl->child; q; q = q->next) if (!q->key && q->val) { long p = parse_int(q->val); if (p > 0 && p < MAX_PROV) WORLD_DISCOVER(w, ci, p); }
		}
	}
	{	/* our saves: the AI's map knowledge as a bitmap "k<hex>" (the specials, defection and exploring depend on it) */
		const SNode *am = script_get(b, "amieu2");
		const char *hx = am ? script_str(am, "known", NULL) : NULL;
		if (hx && hx[0] == 'k') {
			int i, n = (int)sizeof w->known[ci];
			memset(w->known[ci], 0, sizeof w->known[ci]);
			for (i = 0, hx++; i < n && hx[0] && hx[1]; i++, hx += 2) {
				int hi = hx[0] <= '9' ? hx[0] - '0' : hx[0] - 'a' + 10, lo = hx[1] <= '9' ? hx[1] - '0' : hx[1] - 'a' + 10;
				w->known[ci][i] = (unsigned char)(hi * 16 + lo);
			}
		}
	}
	list_provinces(w, script_get(b, "ownedprovinces"), ci, 1);
	list_provinces(w, script_get(b, "controlledprovinces"), ci, 0);
	list_provinces(w, script_get(b, "nationalprovinces"), ci, 2);
	for (n = b->child; n; n = n->next) {
		if (!n->key) continue;
		if (script_ieq(n->key, "city")) {
			long loc = script_int(n, "location", -1);
			if (loc < 0 || loc >= MAX_PROV) { w->repairs++; continue; }
			w->prov[loc].population = script_int(n, "population", 0);
			w->prov[loc].fort = (unsigned char)script_int(script_get(n, "fortress"), "level", 0);
			w->prov[loc].capital = (unsigned char)parse_yes(script_str(n, "capital", "no"));
			if (w->prov[loc].capital) c->capital = (short)loc;          /* city { capital = yes } */
		} else if (script_ieq(n->key, "landunit") && w->nland < MAX_LAND) {
			LandUnit *u = &w->land[w->nland++];
			u->id = script_int(script_get(n, "id"), "id", 0);
			u->name = pool_str(w, script_str(n, "name", ""));
			u->owner = ci;
			u->location = (short)script_int(n, "location", -1);
			u->infantry = script_int(n, "infantry", 0);
			u->cavalry = script_int(n, "cavalry", 0);
			u->artillery = script_int(n, "artillery", 0);
			scen_leader(w, script_get(n, "leader"), &u->l_id, &u->l_type, &u->l_rank, &u->l_move, &u->l_fire, &u->l_shock, &u->l_siege, &u->lname);
			{	/* save game: morale and the march in progress */
				const SNode *x = script_get(n, "amieu2"), *pt, *q;
				if (x) {
					u->morale_m = script_int(x, "morale", 0);
					u->dead = (unsigned char)script_int(x, "dead", 0);
					u->aboard = (short)script_int(x, "aboard", 0);
					{	/* its general: id rank move fire shock siege */
						const SNode *lq = script_get(x, "lead"), *q2;
						long v[7];
						int nv = 0;
						for (q2 = lq ? lq->child : NULL; q2 && nv < 7; q2 = q2->next) if (!q2->key && q2->val) v[nv++] = parse_int(q2->val);
						if (nv >= 6) { u->l_id = (short)v[0]; u->l_rank = (unsigned char)v[1]; u->l_move = (unsigned char)v[2]; u->l_fire = (unsigned char)v[3]; u->l_shock = (unsigned char)v[4]; u->l_siege = (unsigned char)v[5]; }
						if (nv == 7) u->l_type = (unsigned char)v[6];
					}
					g_save_idx[w->nland - 1] = (short)script_int(x, "idx", -1);
					g_have_idx = 1;
					u->from = (short)script_int(x, "from", 0);
					u->pathpos = (short)script_int(x, "pathpos", 0);
					u->arrive_day = script_int(x, "arrive", 0);
					for (pt = script_get(x, "path"), q = pt ? pt->child : NULL; q && u->npath < 40; q = q->next)
						if (!q->key && q->val) u->path[u->npath++] = (short)parse_int(q->val);
				}
			}
		} else if (script_ieq(n->key, "navalunit") && w->nnaval < MAX_NAVAL) {
			NavalUnit *u = &w->naval[w->nnaval++];
			u->id = script_int(script_get(n, "id"), "id", 0);
			u->name = pool_str(w, script_str(n, "name", ""));
			u->owner = ci;
			u->location = (short)script_int(n, "location", -1);
			u->warships = (short)script_int(n, "warships", 0);
			u->galleys = (short)script_int(n, "galleys", 0);
			u->transports = (short)script_int(n, "transports", 0);
			{ unsigned char rk, sg; scen_leader(w, script_get(n, "leader"), &u->l_id, &u->l_type, &rk, &u->l_move, &u->l_fire, &u->l_shock, &sg, &u->lname); }
			u->sea_since = w->now.year * 12L + w->now.month;   /* a scenario fleet: at sea from the start (a save game has amieu2 sea) */
			{	/* save game: the voyage in progress */
				const SNode *x = script_get(n, "amieu2"), *pt, *q;
				if (x) {
					u->pathpos = (short)script_int(x, "pathpos", 0);
					u->morale_m = script_int(x, "morale", 0);
					u->sea_since = script_int(x, "sea", 0); u->att_m = script_int(x, "att", 0);
					{	/* its explorer: id move type */
						const SNode *lq = script_get(x, "lead"), *q2;
						long v[5];
						int nv = 0;
						for (q2 = lq ? lq->child : NULL; q2 && nv < 5; q2 = q2->next) if (!q2->key && q2->val) v[nv++] = parse_int(q2->val);
						if (nv >= 3) { u->l_id = (short)v[0]; u->l_move = (unsigned char)v[1]; u->l_type = (unsigned char)v[2]; }
						if (nv == 5) { u->l_fire = (unsigned char)v[3]; u->l_shock = (unsigned char)v[4]; }
					}
					g_save_nidx[w->nnaval - 1] = (short)script_int(x, "idx", -1);
					g_have_nidx = 1;
					u->arrive_day = script_int(x, "arrive", 0);
					for (pt = script_get(x, "path"), q = pt ? pt->child : NULL; q && u->npath < 40; q = q->next)
						if (!q->key && q->val) u->path[u->npath++] = (short)parse_int(q->val);
				}
			}
		}
	}
}

static void pak_path(const char *in, char *out, int n)
{
	int i;
	for (i = 0; in[i] && i < n - 1; i++) {
		char c = in[i] == '\\' ? '/' : in[i];
		out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	out[i] = 0;
}

static int load_file(World *w, Pak *t, const char *name, int depth, char *err, int errlen)
{
	char key[160], *buf;
	unsigned long sz;
	Arena *a;
	SNode *root, *n;
	if (depth > 8) { snprintf(err, errlen, "include nesting too deep at %s", name); return -1; }
	pak_path(name, key, sizeof key);
	if (!strncmp(key, "db/events", 9)) { w->has_events = 1; return 0; }   /* events: loaded by the event module later, if included */
	if (strchr(name, ':') || name[0] == '/') {           /* a save game on disk, not a pack entry */
		if (!(buf = world_read_file(name, &sz))) { snprintf(err, errlen, "cannot read %s", name); return -1; }
	} else if (!(buf = load_text(t, key, &sz))) { w->repairs++; return 0; }   /* the original skips missing includes (NIEPEWNE) */
	a = arena_new(65536);
	root = script_parse(buf, sz, a, err, errlen);
	if (!root) { arena_free(a); free(buf); return -1; }
	for (n = root->child; n; n = n->next) {
		if (!n->key) continue;
		if (script_ieq(n->key, "include") && n->val) {
			if (load_file(w, t, n->val, depth + 1, err, errlen)) { arena_free(a); free(buf); return -1; }
		} else if (script_ieq(n->key, "globaldata")) {
			read_date(script_get(n, "startdate"), &w->start);
			read_date(script_get(n, "enddate"), &w->end);
			w->spec_used = (unsigned long)script_int(n, "amieu2spec", (long)w->spec_used);
			if (script_get(n, "emperor")) { const char *t = script_str(n, "emperor", ""); w->emperor = world_country(w, t); }
			{	/* the timed casus belli: a b until */
				const SNode *q = script_get(n, "amieu2cb"), *x;
				long v[3]; int k = 0;
				for (x = q ? q->child : NULL; x; x = x->next) {
					if (x->key || !x->val) continue;
					v[k++] = parse_int(x->val);
					if (k == 3) { world_give_cb(w, (int)v[0], (int)v[1], v[2]); k = 0; }
				}
			}
			{	/* tag changes first: the country blocks below find their slots by the swapped tags */
				const SNode *q = script_get(n, "amieu2swaps"), *x;
				long v[2]; int k = 0;
				for (x = q ? q->child : NULL; x; x = x->next) {
					if (x->key || !x->val) continue;
					v[k++] = parse_int(x->val);
					if (k == 2) { world_swap_identity(w, (int)v[0], (int)v[1]); k = 0; }
				}
			}
			{	const SNode *q = script_get(n, "amieu2leadev"), *x;
				long v[3]; int k = 0;
				for (x = q ? q->child : NULL; x && w->nleadev < 64; x = x->next) {
					if (x->key || !x->val) continue;
					v[k++] = parse_int(x->val);
					if (k == 3) { w->leadev_id[w->nleadev] = v[0]; w->leadev_day[w->nleadev] = v[1]; w->leadev_kind[w->nleadev] = (unsigned char)v[2]; w->nleadev++; k = 0; }
				}
			}
			{	const SNode *q = script_get(n, "amieu2monev"), *x;
				long v[4]; int k = 0;
				for (x = q ? q->child : NULL; x && w->nmonev < 64; x = x->next) {
					if (x->key || !x->val) continue;
					v[k++] = parse_int(x->val);
					if (k == 4) { w->monev_c[w->nmonev] = (short)v[0]; w->monev_id[w->nmonev] = v[1]; w->monev_day[w->nmonev] = v[2]; w->monev_kind[w->nmonev] = (unsigned char)v[3]; w->nmonev++; k = 0; }
				}
			}
			{	const char *hx = script_str(n, "amieu2killed", NULL);   /* leaders fallen in battle: "k<hex>" */
				int i;
				if (hx && hx[0] == 'k') for (i = 0, hx++; i < (int)sizeof w->lead_killed && hx[0] && hx[1]; i++, hx += 2)
					w->lead_killed[i] = (unsigned char)(((hx[0] <= '9' ? hx[0] - '0' : hx[0] - 'a' + 10) << 4) | (hx[1] <= '9' ? hx[1] - '0' : hx[1] - 'a' + 10));
			}
			{	/* flags = { 0 1 2 3 } (0x5b1d3a): the global flags already set */
				const SNode *fl = script_get(n, "flags"), *q;
				for (q = fl ? fl->child : NULL; q; q = q->next) if (!q->key && q->val) { long k = parse_int(q->val); if (k >= 0 && k < 8) w->gflag[k] = 1; }
			}
			{	/* wars running at the start: war = { attackers = { participant = { A B } } defenders = ... } */
				const SNode *wn;
				for (wn = n->child; wn; wn = wn->next) {
					War *wr;
					int side;
					if (wn->key && script_ieq(wn->key, "alliance") && w->nally < MAX_ALLY) {   /* alliance = { type = militaryalliance expirydate = {} participant = { A B } } */
						Alliance *al = &w->ally[w->nally];
						const char *ty = script_str(wn, "type", "");
						const SNode *p = script_get(wn, "participant"), *t;
						memset(al, 0, sizeof *al);
						al->type = (unsigned char)(script_ieq(ty, "militaryalliance") ? ALLY_MILITARY : script_ieq(ty, "vassalization") ? ALLY_VASSAL :
						                           script_ieq(ty, "dynasticalliance") ? ALLY_DYNASTIC : 0);
						read_date(script_get(wn, "expirydate"), &al->expiry);
						for (t = p ? p->child : NULL; t; t = t->next) {
							short ci = t->val ? world_country(w, t->val) : -1;
							if (ci >= 0 && al->n < 24) al->member[al->n++] = ci;
						}
						if (al->type && al->n >= 2) w->nally++; else w->repairs++;
						continue;
					}
					if (!wn->key || !script_ieq(wn->key, "war") || w->nwar >= MAX_WAR) continue;
					wr = &w->war[w->nwar++];
					memset(wr, 0, sizeof *wr);
					read_date(script_get(wn, "date"), &wr->began);
					for (side = 0; side < 2; side++) {
						const SNode *p = script_get(script_get(wn, side ? "defenders" : "attackers"), "participant"), *t;
						for (t = p ? p->child : NULL; t; t = t->next) {
							short ci = t->val ? world_country(w, t->val) : -1;
							if (ci < 0) continue;
							if (side && wr->ndef < 24) wr->def[wr->ndef++] = ci;
							if (!side && wr->natt < 24) wr->att[wr->natt++] = ci;
						}
					}
					{	/* save game: war clock and battle points (savegame.c) */
						const SNode *x = script_get(wn, "amieu2"), *l, *q;
						int m;
						if (x) {
							wr->start_day = script_int(x, "start", 0);
							wr->last_combat = script_int(x, "last", 0);
							for (m = 0; m < 2; m++)
								for (l = script_get(x, m ? "bpd" : "bpa"), q = l ? l->child : NULL; q; ) {
									int i = 0, j = 0, v = 0, got = 0;
									for (; q && got < 3; q = q->next) { if (q->key || !q->val) continue; if (got == 0) i = parse_int(q->val); else if (got == 1) j = parse_int(q->val); else v = parse_int(q->val); got++; }
									if (got == 3 && i >= 0 && i < 24 && j >= 0 && j < 24) { if (m) wr->bp_def[i][j] = (unsigned char)v; else wr->bp_att[i][j] = (unsigned char)v; }
								}
						}
					}
				}
			}
		} else if (script_ieq(n->key, "province")) {
			apply_province(w, n);
		} else if (script_ieq(n->key, "country")) {
			apply_country(w, n);
		} else if (script_ieq(n->key, "cot") && w->ncot < MAX_COT) {
			Cot *ct = &w->cot[w->ncot];
			const SNode *mm;
			ct->prov = (short)script_int(n, "location", -1);
			ct->n = 0;
			ct->fail = script_int(n, "amieu2fail", script_milli(n, "failure", 0) * 12);
			for (mm = n->child; mm; mm = mm->next)
				if (mm->key && script_ieq(mm->key, "merchant") && ct->n < COT_MERCH) {
					short ci = world_country(w, script_str(mm, "tag", ""));
					if (ci < 0) { w->repairs++; continue; }
					ct->tag[ct->n] = ci; ct->level[ct->n] = (unsigned char)script_int(mm, "level", 1); ct->n++;
				}
			if (ct->prov > 0) w->ncot++;
		} else if (script_ieq(n->key, "amieu2_buildq")) {   /* save game: the build queue in order (savegame.c) */
			const SNode *q = n->child;
			while (q && w->nbuild < MAX_BUILD) {
				const char *t[7];
				int got = 0;
				for (; q && got < 7; q = q->next) if (!q->key && q->val) t[got++] = q->val;
				if (got == 7) {
					Build *b = &w->build[w->nbuild];
					b->country = world_country(w, t[0]); b->prov = (short)parse_int(t[1]); b->ship = (short)parse_int(t[2]);
					b->inf = parse_int(t[3]); b->cav = parse_int(t[4]); b->art = parse_int(t[5]); b->done = parse_int(t[6]);
					if (b->ship < 0 || (b->ship > 5 && b->ship < 10) || b->ship > 19) continue;   /* 10..19: improvements (buildings.h) */
					if (b->country >= 0 && b->prov > 0 && b->prov < MAX_PROV) w->nbuild++;
				}
			}
		} else if (script_ieq(n->key, "amieu2_builds") || script_ieq(n->key, "amieu2_shipbuilds")) {   /* older saves: recruits / ships in two blocks */
			int ships = script_ieq(n->key, "amieu2_shipbuilds");
			const SNode *q = n->child;
			while (q && w->nbuild < MAX_BUILD) {
				const char *t[6];
				int got = 0;
				for (; q && got < 6; q = q->next) if (!q->key && q->val) t[got++] = q->val;
				if (got == 6) {
					Build *b = &w->build[w->nbuild];
					b->country = world_country(w, t[0]); b->prov = (short)parse_int(t[1]);
					if (ships) { b->ship = (short)parse_int(t[2]); b->inf = parse_int(t[3]); b->cav = b->art = 0; }
					else { b->ship = 0; b->inf = parse_int(t[2]); b->cav = parse_int(t[3]); b->art = parse_int(t[4]); }
					b->done = parse_int(t[5]);
					if (ships && (b->ship < 1 || b->ship > 5)) continue;   /* 4 = fortress works, 5 = a missionary */
					if (b->country >= 0 && b->prov > 0 && b->prov < MAX_PROV) w->nbuild++;
				}
			}
		} else if (script_ieq(n->key, "header")) {
			snprintf(w->scenario_name, sizeof w->scenario_name, "%s", script_str(n, "name", ""));
			w->tutorial = (unsigned char)parse_yes(script_str(n, "tutorial", "no"));
			snprintf(w->tutfile, sizeof w->tutfile, "%s", script_str(n, "tutorialfile", ""));
			w->optmode = (unsigned char)(script_int(n, "optionmode", 0) == 1);
			w->set_opt[0] = (signed char)script_int(n, "set_difficulty", 2);
			w->set_opt[1] = (signed char)script_int(n, "set_ai_aggresive", 1);
			w->set_opt[2] = (signed char)script_int(n, "set_gamespeed", 2);
			w->set_opt[3] = (signed char)script_int(n, "set_basevp", 1);
			w->set_opt[4] = (signed char)script_int(n, "set_missions", 1);
			{	/* the featured countries (FRA = { countrydifficulty = .. missiontype = .. }) */
				const SNode *t;
				for (t = n->child; t; t = t->next) if (t->key && strlen(t->key) == 3 && t->child && script_get(t, "countrydifficulty")) {
					int c = world_country(w, t->key);
					if (c >= 0) w->country[c].tracked = 1;
				}
			}
		}
	}
	arena_free(a);
	free(buf);
	return 0;
}

void world_give_cb(World *w, int a, int b, long until)
{
	int i, k = -1;
	if (a < 0 || b < 0 || a >= w->ncountry || b >= w->ncountry || a == b) return;
	for (i = 0; i < w->ntcb; i++) if (w->tcb[i].a == a && w->tcb[i].b == b) { if (w->tcb[i].until < until) w->tcb[i].until = until; w->dipflag[a][b] |= DF_CB_TMP; return; }
	if (w->ntcb < (int)(sizeof w->tcb / sizeof w->tcb[0])) k = w->ntcb++;
	else for (i = 0, k = 0; i < w->ntcb; i++) if (w->tcb[i].until < w->tcb[k].until) k = i;   /* full: the soonest to expire makes room */
	if (k < w->ntcb - 1 || w->ntcb == (int)(sizeof w->tcb / sizeof w->tcb[0])) w->dipflag[w->tcb[k].a][w->tcb[k].b] &= (unsigned char)~DF_CB_TMP;
	w->tcb[k].a = (short)a; w->tcb[k].b = (short)b; w->tcb[k].until = until;
	w->dipflag[a][b] |= DF_CB_TMP;
}

void world_cb_day(World *w, long today)
{
	int i = 0;
	while (i < w->ntcb)
		if (today > w->tcb[i].until) { w->dipflag[w->tcb[i].a][w->tcb[i].b] &= (unsigned char)~DF_CB_TMP; w->tcb[i] = w->tcb[--w->ntcb]; }
		else i++;
}

/* 0x4825b0: a deterministic value from the date, the province, its owner and last month's weather drives the storm (Storm?
 * column and v % 100 < 30), the wind ((v / 2) % 7 + 1, +5 in a storm; the exe also takes 5 off in two regions, +0x10e = 0x2c / 0x2d,
 * NIEZNANE which: not done) and winter by climate and month. Ours: the province index for +0x58 and our country index for the
 * owner's +0x6 (NIEPEWNE), so the rolls are the exe's in kind, not in value */
void world_weather_month(World *w)
{
	int i, mo = w->now.month > 0 ? w->now.month - 1 : 0;      /* 0-based month as in the exe */
	for (i = 1; i < w->nprov; i++) {
		Province *p = &w->prov[i];
		long v = w->now.day + mo + (w->now.year * 12L + mo) * 30 + i + p->winter + (p->owner >= 0 ? p->owner : 0) + p->wind / 2, e;
		int wd, win = 0;
		if (v < 0) v = -v;
		p->storm_now = (unsigned char)(p->storm && v % 100 < 30);
		e = v / 2;
		wd = (int)(e % 7) + 1;
		if (p->storm_now) wd += 5;
		p->wind = (unsigned char)(wd > 9 ? 9 : wd);
		switch (p->climate) {
		case 0:                                                   /* Nov..Mar winter, Apr / May half, Oct three in four */
			if (mo <= 2 || mo >= 10) win = 1; else if (mo == 3 || mo == 4) win = e % 2 == 0; else if (mo == 9) win = e % 4 != 0;
			break;
		case 7: if (mo <= 1 || mo == 11) win = 1; else if (mo == 2) win = e % 2 == 0; else if (mo == 10) win = e % 10 < 4; break;
		case 5: if (mo == 0) win = e % 5 == 0; else if (mo == 1) win = e % 20 == 0; else if (mo == 11) win = e % 10 == 0; break;
		case 8: if (mo == 5) win = e % 5 == 0; else if (mo == 6) win = e % 4 == 0; else if (mo == 7) win = e % 10 == 0; break;   /* the south */
		}
		p->winter = (unsigned char)win;
	}
}

void world_book_inc(World *w, int c, int cat, long a) { if (c >= 0 && c < w->ncountry && cat >= 0 && cat < 12) w->country[c].yinc_m[cat] += a; }
void world_book_exp(World *w, int c, int cat, long a) { if (c >= 0 && c < w->ncountry && cat >= 0 && cat < 16) w->country[c].yexp_m[cat] += a; }
void world_books_year(World *w)
{
	int c;
	for (c = 0; c < w->ncountry; c++) {
		Country *co = &w->country[c];
		memcpy(co->linc_m, co->yinc_m, sizeof co->linc_m); memcpy(co->lexp_m, co->yexp_m, sizeof co->lexp_m);
		memset(co->yinc_m, 0, sizeof co->yinc_m); memset(co->yexp_m, 0, sizeof co->yexp_m);
	}
}

long world_income_value_m(const World *w, int p)
{
	const Province *pv = &w->prov[p];
	long v = pv->income_m;
	if (!pv->fort && pv->pirates > 0) { v -= 500L * pv->pirates; if (v < 0) v = 0; }   /* 0x47db28: only without a fortress */
	return v;
}

int world_religion_index(const World *w, short name)
{
	static const char *const rn[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite",
	                                    "pagan", "confucianism", "buddhism", "hinduism" };
	static signed char cache[MAX_NAMES];                  /* per interned name, rebuilt when the name table changes */
	static const World *cw;
	static int cn = -1;
	if (name < 0 || name >= w->nnames) return -1;
	if (cw != w || cn != w->nnames) {
		int i, k;
		for (i = 0; i < w->nnames; i++) {
			cache[i] = -1;
			for (k = 0; k < 11; k++) if (w->names[i] && script_ieq(w->names[i], rn[k])) { cache[i] = (signed char)k; break; }
		}
		cw = w; cn = w->nnames;
	}
	return cache[name];
}

/* SPEC_EVENTS 9 code 0x22 `country`: the country of slot a changes its tag to that of slot b (which does not
 * exist). Ours: the two slots swap their country.csv identity (tag, colours, tech group, sprites, elector), so
 * every index-keyed state stays with the polity; recorded for the saves (amieu2swaps, replayed first) */
void world_swap_identity(World *w, int a, int b)
{
	Country *A, *B;
	char t4[4];
	short s;
	unsigned char e;
	int i;
	if (a < 0 || b < 0 || a == b || a >= w->ncountry || b >= w->ncountry) return;
	A = &w->country[a]; B = &w->country[b];
	memcpy(t4, A->tag, 4); memcpy(A->tag, B->tag, 4); memcpy(B->tag, t4, 4);
	s = A->color; A->color = B->color; B->color = s;
	s = A->fantasia_color; A->fantasia_color = B->fantasia_color; B->fantasia_color = s;
	s = A->techgroup; A->techgroup = B->techgroup; B->techgroup = s;
	memcpy(t4, A->army_spr, 4); memcpy(A->army_spr, B->army_spr, 4); memcpy(B->army_spr, t4, 4);
	memcpy(t4, A->navy_spr, 4); memcpy(A->navy_spr, B->navy_spr, 4); memcpy(B->navy_spr, t4, 4);
	memcpy(t4, A->city_nat, 4); memcpy(A->city_nat, B->city_nat, 4); memcpy(B->city_nat, t4, 4);
	e = A->elector; A->elector = B->elector; B->elector = e;
	memset(w->tag_hash, 0, sizeof w->tag_hash);
	for (i = 0; i < w->ncountry; i++) tag_index_add(w, (short)i);
	if (w->nswaps < 32) { w->swaps[2 * w->nswaps] = (short)a; w->swaps[2 * w->nswaps + 1] = (short)b; w->nswaps++; }
	w->owner_epoch++;
}

/* SPEC_HRE 2 (0x5b20a0): candidates exist, are Catholic / Counter-Reformed or electors, have a monarch and
 * their capital in Europe; score = rand % 10 + sum over the living electors E of (trunc(rel K-E) + 100) x
 * weight(E) + 200 if K is an elector + 25 for HAB and SPA. The best strictly above 0 wins (HAB otherwise).
 * The roll is stateless (the day, the candidate) so the saves stay exact. The winner: +50 VP, news. */
void world_hre_elect(World *w, long today)
{
	int k, e, best = world_country(w, "HAB"), hab = best, spa = world_country(w, "SPA");
	long bs = 0;
	for (k = 0; k < w->ncountry; k++) {
		const Country *K = &w->country[k];
		int r = world_religion_index(w, K->religion), cap = K->capital;
		long s;
		unsigned long h;
		if (K->nprov_owned <= 0 || k == w->reb) continue;
		if (!(r == 2 || r == 3 || K->elector)) continue;
		if (!K->m_name || cap <= 0 || cap >= w->nprov || !script_ieq(world_name(w, w->prov[cap].continent), "Europe")) continue;
		h = (unsigned long)today * 2654435761UL ^ (unsigned long)(k + 7) * 40503UL; h ^= h >> 13; h &= 0xFFFFFFFFUL;
		s = (long)((h >> 8) % 10);
		for (e = 0; e < w->ncountry; e++) {
			long rl;
			if (!w->country[e].elector || w->country[e].nprov_owned <= 0 || e == k) continue;
			rl = world_rel(w, k, e) / 1000;
			s += (rl + 100) * w->country[e].elector;
		}
		if (K->elector) s += 200;
		if (k == hab || k == spa) s += 25;
		if (s > bs) { bs = s; best = k; }
	}
	if (best < 0) return;
	w->emperor = (short)best;
	world_vp(w, best, VP_DIP, 50000L);                       /* NEW_EMPEROR 0x150 (0x455530) */
	world_news(w, NOTE_EMPEROR, best, -1, -1);
}

void world_hre_month(World *w, long today)
{
	if (w->tutorial) return;                                  /* 0x5b23e7: the month step skips it when 0x419700 (the header's tutorial flag, +0x2766) */
	if (w->emperor < 0 || w->emperor >= w->ncountry || w->country[w->emperor].nprov_owned <= 0) world_hre_elect(w, today);
}

int world_hre_n(const World *w, int c)
{
	int e, n = 0;
	if (c < 0 || c != w->emperor) return 0;
	for (e = 0; e < w->ncountry; e++)
		if (e != c && w->country[e].elector && w->country[e].nprov_owned > 0 && world_rel(w, e, c) >= 100000L) n++;
	return n;
}

int world_culture_ok(const World *w, int c, short cul)
{
	const Country *co;
	int k;
	if (c < 0 || c >= w->ncountry || cul < 0) return 0;
	co = &w->country[c];
	if (cul == co->culture) return 1;
	for (k = 0; k < co->nacc; k++) if (co->acc[k] == cul) return 1;
	return 0;
}

/* 0x4d16a0 (SPEC_SMALLRULES 2.1): flag 0 (Tordesillas) counts only while the Edict of Tolerance (4) is off
 * and Spain and Portugal both exist and are Catholic or Counter-Reformed */
int world_flag(const World *w, int n)
{
	int spa, por, rs, rp;
	if (n < 0 || n >= 8) return 0;
	if (n) return w->gflag[n] != 0;
	if (!w->gflag[0] || w->gflag[4]) return 0;
	spa = world_country(w, "SPA"); por = world_country(w, "POR");
	if (spa < 0 || por < 0 || w->country[spa].nprov_owned <= 0 || w->country[por].nprov_owned <= 0) return 0;
	rs = world_religion_index(w, w->country[spa].religion); rp = world_religion_index(w, w->country[por].religion);
	return (rs == 2 || rs == 3) && (rp == 2 || rp == 3);
}

/* docs revolt_religion 5 (0x430c70): tolerance defaults by state religion and the reformation flags 1 / 2, x10 */
void world_default_tol(World *w, int c)
{
	static const signed char tab[14][7] = {                 /* P0 R1 C3 O4 Su5 Sh6, state */
		{ 6, 0, 0, 0, -6, -6, 0 }, { 6, 2, -2, 0, -6, -6, 0 },          /* protestant: flag2 off / on */
		{ 3, 5, -2, 0, -6, -6, 1 },                                       /* reformed */
		{ -5, -5, 10, 3, -3, -3, 2 },                                     /* counter-reform */
		{ 0, 0, 10, 0, 0, -10, 3 }, { -4, 0, 10, 2, -7, -7, 3 }, { -4, -2, 10, 2, -6, -6, 3 },   /* catholic: none / flag1 / flag2 */
		{ 0, 0, -4, 10, -6, -6, 4 }, { -2, 0, -2, 10, -4, -4, 4 }, { -3, -3, 0, 10, -4, -4, 4 }, /* orthodox */
		{ 0, 0, -3, -1, 10, -6, 5 }, { -2, -2, -2, 0, 10, -6, 5 },       /* sunni: flag1 off / on */
		{ 0, 0, -3, -1, -6, 10, 6 }, { -2, -2, -2, 0, -6, 10, 6 } };     /* shiite */
	static const int col[6] = { 0, 1, 3, 4, 5, 6 };
	Country *co = &w->country[c];
	int s = world_religion_index(w, co->religion), f1 = world_flag(w, 1), f2 = world_flag(w, 2), k, row = -1;
	memset(co->tol, 0, sizeof co->tol);
	switch (s) {
	case 0: row = f2 ? 1 : 0; break;
	case 1: row = 2; break;
	case 2: row = 3; break;
	case 3: row = f2 ? 6 : f1 ? 5 : 4; break;
	case 4: row = f2 ? 9 : f1 ? 8 : 7; break;
	case 5: row = f1 ? 11 : 10; break;
	case 6: row = f1 ? 13 : 12; break;
	default: return;                                         /* pagan and the eastern faiths: all 0 */
	}
	for (k = 0; k < 6; k++) co->tol[col[k]] = (short)(tab[row][k] * 10);
	co->tol[2] = co->tol[3];                                 /* counter-reform always equals catholic */
}

int world_load_scenario(World *w, Pak *t, const char *eug, char *err, int errlen)
{
	w->emperor = world_country(w, "HAB");                   /* the default before the scenario (0x5b186f) */
	short i;
	int rc;
	g_have_idx = 0; g_have_nidx = 0;
	rc = load_file(w, t, eug, 0, err, errlen);
	if (rc) return rc;
	if (g_have_idx) {                                    /* save game: put the land units back in their saved order */
		int k, ok = 1;
		static unsigned char seen[MAX_LAND];
		memset(seen, 0, sizeof seen);
		for (k = 0; k < w->nland && ok; k++) { int x = g_save_idx[k]; if (x < 0 || x >= w->nland || seen[x]) ok = 0; else seen[x] = 1; }
		for (k = 0; ok && k < w->nland; k++)                 /* cycle sort on the permutation */
			while (g_save_idx[k] != k) {
				int x = g_save_idx[k];
				LandUnit tu = w->land[x]; short ti = g_save_idx[x];
				w->land[x] = w->land[k]; g_save_idx[x] = g_save_idx[k];
				w->land[k] = tu; g_save_idx[k] = ti;
			}
		if (!ok) w->repairs++;
	}
	if (g_have_nidx) {                                   /* and the fleets (naval battles go by index) */
		int k, ok = 1;
		static unsigned char seen[MAX_NAVAL];
		memset(seen, 0, sizeof seen);
		for (k = 0; k < w->nnaval && ok; k++) { int x = g_save_nidx[k]; if (x < 0 || x >= w->nnaval || seen[x]) ok = 0; else seen[x] = 1; }
		for (k = 0; ok && k < w->nnaval; k++)
			while (g_save_nidx[k] != k) {
				int x = g_save_nidx[k];
				NavalUnit tu = w->naval[x]; short ti = g_save_nidx[x];
				w->naval[x] = w->naval[k]; g_save_nidx[x] = g_save_nidx[k];
				w->naval[k] = tu; g_save_nidx[k] = ti;
			}
		if (!ok) w->repairs++;
	}
	w->now = w->start;
	for (i = 0; i < w->ncountry; i++) w->country[i].nprov_owned = 0;
	for (i = 0; i < w->nprov; i++) {
		if (w->prov[i].owner >= 0) w->country[w->prov[i].owner].nprov_owned++;
		if (w->prov[i].last_owner == -32768) w->prov[i].last_owner = w->prov[i].owner;   /* revolts.c tracks changes of hands from here */
	}
	{	/* tolerance: the defaults wherever a save did not give them */
		int c2;
		for (c2 = 0; c2 < w->ncountry; c2++) if (!w->country[c2].tol_set) world_default_tol(w, c2);
	}
	{	/* everyone knows their own provinces */
		int p2;
		for (p2 = 1; p2 < w->nprov; p2++) if (w->prov[p2].owner >= 0) WORLD_DISCOVER(w, w->prov[p2].owner, p2);
	}
	return 0;
}

void world_vp(World *w, int c, int cat, long amount_m)
{
	Country *co;
	if (w->opt_novp || c < 0 || c >= w->ncountry || cat < 0 || cat > 6) return;
	co = &w->country[c];
	co->vpc_m[cat] += amount_m;
	co->vpt_m += amount_m;
	co->vp = co->vpt_m / 1000;
}

static int name_is(const World *w, short id, const char *s) { return id >= 0 && !strcmp(world_name(w, id), s); }

void world_vp_establish(World *w, int c, int prov, int tp)
{
	const Province *p = &w->prov[prov];
	int cls = 0;
	world_vp(w, c, VP_EST, tp ? 400 : 800);
	if (!tp) {
		if (name_is(w, p->continent, "America")) cls = 1;
		else if (name_is(w, p->continent, "Africa")) cls = 2;
		else if (name_is(w, p->region, "Siberia")) cls = 3;
		else if (name_is(w, p->continent, "Oceania")) cls = 4;
	} else {
		if (name_is(w, p->region, "India")) cls = 5;
		else if (name_is(w, p->region, "China")) cls = 6;
		else if (name_is(w, p->region, "Japan")) cls = 7;
	}
	if (cls && !w->vp_first[cls]) { w->vp_first[cls] = 1; world_vp(w, c, VP_EST, 5000); }
}

/* the first-discovery specials 0x418420 (SPEC_SMALLRULES 4.3), in their order; the first match that nobody
 * claimed yet wins. Areas / regions / continents by name (the exe's AREA_ / REG_ indices) */
static const struct { const char *key; short vp; } g_spec[18] = { { "", 0 },
	{ "SPEC_AMERICA", 50 }, { "SPEC_ASIA", 25 }, { "SPEC_QUEBEC", 5 }, { "SPEC_GREATLAKE", 10 }, { "SPEC_CAPEHORN", 10 },
	{ "SPEC_GOODHOPE", 10 }, { "SPEC_HUDSONBAY", 10 }, { "SPEC_ALASKA", 5 }, { "SPEC_PANAMA", 5 }, { "SPEC_INLANDCHINA", 20 },
	{ "SPEC_AMAZONEZ", 10 }, { "SPEC_NORTHAM", 20 }, { "SPEC_INDIA", 25 }, { "SPEC_KAMTCHATSKA", 10 }, { "SPEC_CALIFORNIA", 10 },
	{ "SPEC_PACIFIC", 10 }, { "SPEC_AUSTRALIA", 5 } };
const char *world_special(int k, int *vp) { if (k < 1 || k > 17) { if (vp) *vp = 0; return ""; } if (vp) *vp = g_spec[k].vp; return g_spec[k].key; }

static int name_is(const World *w, short id, const char *s);
static int special_match(const World *w, int c, int p, int k)
{
	const Province *pv = &w->prov[p];
	int land = pv->terrain < 5, q;
	switch (k) {
	case 1: return land && name_is(w, pv->continent, "America");
	case 2: return land && name_is(w, pv->continent, "Asia");
	case 3: return land && name_is(w, pv->area, "Quebec");
	case 4: return name_is(w, pv->area, "Grand Lacs");
	case 5: return p == 1240;
	case 6: return p == 1252;
	case 7: return p == 829;
	case 8: return name_is(w, pv->area, "Alaska");
	case 9: return p == 39;
	case 10:                                              /* Manchuria, and all of Siberia already known (as found) */
		if (!name_is(w, pv->area, "Manchuria")) return 0;
		for (q = 1; q < w->nprov; q++) if (name_is(w, w->prov[q].region, "Siberia") && !WORLD_KNOWN(w, c, q)) return 0;
		return 1;
	case 11: return land && name_is(w, pv->area, "Amazonia");
	case 12: return 0;                                    /* NORTHAM: its case never matches */
	case 13: return land && name_is(w, pv->region, "India");
	case 14: return land && name_is(w, pv->area, "Kamchatka");
	case 15: return land && name_is(w, pv->area, "California");
	case 16: return !land && name_is(w, pv->region, "Pacific");
	case 17: return p >= 720 && p <= 726;
	}
	return 0;
}

void world_vp_discover(World *w, int c, int prov, int sea)
{
	int k, anyone = 0;
	if (c >= 0 && c < w->ncountry && prov > 0 && prov < w->nprov && !WORLD_KNOWN(w, c, prov) && c != w->reb) {
		for (k = 1; k <= 17; k++) {
			if (w->spec_used >> k & 1) continue;
			if (!special_match(w, c, prov, k)) continue;
			w->spec_used |= 1UL << k;                       /* used up even with the base VP off */
			world_vp(w, c, VP_DISC, 1000L * g_spec[k].vp);
			world_note(w, NOTE_EXPLORESPEC, c, prov, k);
			break;
		}
	}
	/* SPEC_VP row 7 (0x48ce18): every country; "nobody had discovered it" (province byte +0x254) read as no other country knowing it
	 * (NIEPEWNE: whether the scenario's known lists set that byte) */
	if (c < 0 || prov <= 0 || prov >= w->nprov || WORLD_KNOWN(w, c, prov)) return;
	for (k = 0; k < w->ncountry && !anyone; k++) if (k != c && WORLD_KNOWN(w, k, prov)) anyone = 1;
	if (!sea) world_vp(w, c, VP_DISC, anyone ? 300 : 2000);
	else if (!anyone) world_vp(w, c, VP_DISC, 300);
}

void world_sfx_at(World *w, int country, int prov, const char *key)
{
	int i;
	if (country >= 0 && (country >= w->ncountry || !w->country[country].human)) return;
	for (i = 0; i < w->nsfx; i++) if (w->sfx[i] == key && w->sfxprov[i] == prov) return;   /* one of each per frame */
	if (w->nsfx < 16) { w->sfxprov[w->nsfx] = (short)prov; w->sfx[w->nsfx++] = key; }
}
void world_sfx(World *w, int country, const char *key) { world_sfx_at(w, country, -1, key); }

void world_news(World *w, int type, int country, int prov, int other)
{
	Note *n;
	if (w->nnews >= 32) return;
	n = &w->news[w->nnews++];
	n->type = (short)type; n->country = (short)country; n->prov = (short)prov; n->other = (short)other;
}

void world_note(World *w, int type, int country, int prov, int other)
{
	Note *n;
	if (country < 0 || country >= w->ncountry) return;
	if (!w->country[country].human && type != NOTE_DIPMSG) {   /* another country: kept for its OTHER message (a DIPMSG names
	                                                        * the sender; its callers already checked the player is the receiver) */
		if ((type == NOTE_BATTLE_WIN || type == NOTE_SIEGE_WIN || type == NOTE_REVOLT || type == NOTE_BANKRUPT) && w->nnews < 32) {
			n = &w->news[w->nnews++];
			n->type = (short)type; n->country = (short)country; n->prov = (short)prov; n->other = (short)other;
		}
		return;
	}
	switch (type) {                                      /* the sounds of these messages (SPEC_SOUND 3) */
	case NOTE_BATTLE_WIN: if (other >= 0) world_sfx(w, country, "army_victory"); break;   /* PROVBATTLEWON, land */
	case NOTE_BATTLE_LOSS: world_sfx(w, country, "army_loss"); break;
	case NOTE_NAVAL_WIN: world_sfx(w, country, "navy_victory"); break;
	case NOTE_NAVAL_LOSS: world_sfx(w, country, "navy_loss"); break;
	case NOTE_ENVOY:                                     /* COLONISE / ESTABLISH_TRADEPOST, or their FAILED / BOUNCED */
		if (other == EA_COLNEW || other == EA_COLEXPAND || other == EA_TPNEW || other == EA_TPEXPAND) world_sfx(w, country, "colony_success");
		else if (other == EA_COLFAIL || other == EA_TPFAIL) world_sfx(w, country, "colony_failure");
		break;   /* merchant results are silent */
	}
	if (w->nnote >= 16) return;                          /* only the player is told */
	n = &w->note[w->nnote++];
	n->type = (short)type; n->country = (short)country; n->prov = (short)prov; n->other = (short)other;
}

/* 0x43a9e0 "trade minded": owns a centre of trade (+0xfc6, counted in the recount 0x43aed3), or domestic
   slider 3 below 5 (0x42bbf0(3) < 4, or 0x42b8c0 = -0.4 (s3 - 5) > 0); the fantasy flag 0x8d0d7a is never set here */
int world_trade_minded(const World *w, int c)
{
	int k;
	if (c < 0 || c >= w->ncountry) return 0;
	for (k = 0; k < w->ncot; k++) if (w->cot[k].prov > 0 && w->prov[w->cot[k].prov].owner == c) return 1;
	return w->country[c].policy[3] < 5;
}

/* 0x47cc80 (SPEC_TIME_ECONOMY 8.2): a province's yearly manpower value, x1000 - a city that is not a colony, land-connected to
   the capital or on its continent; manpower column x 0.25 x (city size div 3 + 1); a conscription centre (flag 69) 2 m + 1;
   -1 for a culture the owner does not accept; not a core: -1 for another religion (the pre-Reformation reading, pagans
   excepted) or when gained less than 10 years ago; at least 0. Also the V of the peace war value (0x47ca60). */
long world_prov_manpower_m(const World *w, int prov)
{
	static const long lim[6] = { 5000, 10000, 20000, 40000, 80000, 200000 };
	const Province *p;
	int c, cap, k, core = 0, size = 6, rp, rc;
	long m;
	if (prov <= 0 || prov >= w->nprov) return 0;
	p = &w->prov[prov];
	c = p->owner;
	if (c < 0 || c >= w->ncountry || p->population < 1000 || p->tp) return 0;
	cap = w->country[c].capital;
	if (cap <= 0 || cap >= w->nprov || !((p->lgroup >= 0 && p->lgroup == w->prov[cap].lgroup) || p->continent == w->prov[cap].continent)) return 0;
	if (p->population < 5000) size = 0;
	else for (k = 0; k < 6; k++) if (p->population <= lim[k]) { size = k; break; }
	m = p->manpower_m / 4 * (size / 3 + 1);
	if (p->bflags & 0x02) m = 2 * m + 1000;                   /* PB_CONSCRIPT */
	if (!world_culture_ok(w, c, p->culture)) m -= 1000;
	for (k = 0; k < 8; k++) if (p->cores[k] == c) core = 1;
	if (!core) {
		rp = world_religion_index(w, p->religion); rc = world_religion_index(w, w->country[c].religion);
		if (rp == 2 || (rp == 0 && !world_flag(w, 1)) || (rp == 1 && !world_flag(w, 2))) rp = 3;   /* 0x47ca10 */
		if (rc == 2 || (rc == 0 && !world_flag(w, 1)) || (rc == 1 && !world_flag(w, 2))) rc = 3;
		if ((rp >= 0 && rc >= 0 && rp != rc && rp != 7) || (p->acq_year > 0 && w->now.year - p->acq_year < 10)) m -= 1000;
	}
	return m > 0 ? m : 0;
}
