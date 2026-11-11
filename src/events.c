/* events.c - see events.h. Section numbers refer to docs/SPEC_EVENTS.md. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "events.h"
#include "script.h"
#include "calendar.h"

/* ------------------------------------------------------------------ RNG */
/* MSVC rand(): 15-bit LCG. Bit-exactness with the PC is not a goal, the draw ORDER is (§12). */
static unsigned long g_seed = 1;
static void ev_srand(unsigned long s) { g_seed = s; }
static int ev_rand(void) { g_seed = (g_seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((g_seed >> 16) & 0x7FFF); }

/* ------------------------------------------------------------- triggers */
enum {
	T_AND, T_OR, T_NOT, T_OWNED, T_CONTROL, T_CORE, T_EXISTS, T_RELIGION, T_CONTINENT, T_EVENT, T_YEAR,
	T_STABILITY, T_LAND, T_NAVAL, T_INFRA, T_TRADE, T_DOMESTIC, T_COUNTRYSIZE, T_ATWAR, T_WAR, T_ALLIANCE,
	T_DYNASTIC, T_VASSAL, T_RELATION, T_NEIGHBOUR, T_LEADER, T_MONARCH, T_DISCOVERED, T_COT,
	T_PROVRELIGION, T_PROVCULTURE, T_BADBOY, T_AI, T_FLAG, T_EMPEROR, T_ELECTOR, T_NTYPES
};
static const char *tnames[T_NTYPES] = {
	"and", "or", "not", "owned", "control", "core", "exists", "religion", "continent", "event", "year",
	"stability", "land", "naval", "infra", "trade", "domestic", "countrysize", "atwar", "war", "alliance",
	"dynastic", "vassal", "relation", "neighbour", "leader", "monarch", "discovered", "cot",
	"provincereligion", "provinceculture", "badboy", "ai", "flag", "emperor", "elector"
};

typedef struct { unsigned char op, pad; unsigned short size; long a, b; } TNode;   /* size = nodes in subtree */

/* ------------------------------------------------------------- commands */
enum {
	C_REVOLT = 0x00, C_RELREVOLT, C_COLREVOLT, C_HERETIC, C_CONVERSION, C_PROVRELIGION, C_ADM, C_DIP, C_MIL,
	C_DOMESTIC, C_RELIGION, C_REVOLTRISK, C_INFLATION, C_DYNASTIC, C_VASSAL, C_BREAKDYN, C_BREAKVASSAL,
	C_ALLIANCE, C_INHERIT, C_TREASURY, C_MANPOWER, C_LAND, C_NAVAL, C_INFRA, C_TRADE, C_STABILITY, C_DIPLOMATS,
	C_COLONISTS, C_MERCHANTS, C_MISSIONARIES, C_CONQUISTADOR, C_EXPLORER, C_LEADER, C_MONARCH, C_COUNTRY,
	C_RELATION, C_GAINMANU, C_LOSEMANU, C_GAINBUILD, C_LOSEBUILD, C_DESERTION, C_FORTRESS, C_POPULATION,
	C_INF, C_CAV, C_ART, C_WARSHIPS, C_GALLEYS, C_TRANSPORTS, C_PROVTAX, C_PROVMANPOWER, C_MINE, C_FLAG,
	C_TRIGGER, C_VP, C_CASUSBELLI, C_CAPITAL, C_NATIVES, C_BANK, C_STOCK, C_PROVCULTURE, C_ADDCORE,
	C_REMOVECORE, C_COT, C_SECEDE, C_PROVREVOLTRISK, C_SLEEPMONARCH, C_SLEEPLEADER, C_ADDCULTURE,
	C_REMCULTURE, C_LOANSIZE, C_SLEEPEVENT, C_MISSION, C_WAR, C_TECHNOLOGY, C_INDEPENDENCE, C_FLAGNAME,
	C_BADBOY, C_AI, C_SETFLAG, C_CLRFLAG, C_REMOVECOT, C_NCODES, C_NOP = 0xFF
};
static const struct { const char *k; unsigned char c; } ckeys[] = {
	{ "revolt", C_REVOLT }, { "religiousrevolt", C_RELREVOLT }, { "colonialrevolt", C_COLREVOLT },
	{ "heretic", C_HERETIC }, { "conversion", C_CONVERSION }, { "provincereligion", C_PROVRELIGION },
	{ "adm", C_ADM }, { "dip", C_DIP }, { "mil", C_MIL }, { "domestic", C_DOMESTIC }, { "religion", C_RELIGION },
	{ "revoltrisk", C_REVOLTRISK }, { "inflation", C_INFLATION }, { "dynastic", C_DYNASTIC }, { "vassal", C_VASSAL },
	{ "breakdynastic", C_BREAKDYN }, { "breakvassal", C_BREAKVASSAL }, { "alliance", C_ALLIANCE },
	{ "inherit", C_INHERIT }, { "treasury", C_TREASURY }, { "cash", C_TREASURY }, { "manpower", C_MANPOWER },
	{ "land", C_LAND }, { "naval", C_NAVAL }, { "infra", C_INFRA }, { "trade", C_TRADE }, { "stability", C_STABILITY },
	{ "diplomats", C_DIPLOMATS }, { "colonists", C_COLONISTS }, { "merchants", C_MERCHANTS },
	{ "missionaries", C_MISSIONARIES }, { "conquistador", C_CONQUISTADOR }, { "explorer", C_EXPLORER },
	{ "leader", C_LEADER }, { "wakeleader", C_LEADER }, { "monarch", C_MONARCH }, { "wakemonarch", C_MONARCH },
	{ "country", C_COUNTRY }, { "relation", C_RELATION }, { "gainmanufactory", C_GAINMANU },
	{ "losemanufactory", C_LOSEMANU }, { "gainbuilding", C_GAINBUILD }, { "losebuilding", C_LOSEBUILD },
	{ "desertion", C_DESERTION }, { "fortress", C_FORTRESS }, { "population", C_POPULATION },
	{ "inf", C_INF }, { "infantry", C_INF }, { "cav", C_CAV }, { "cavalry", C_CAV }, { "art", C_ART },
	{ "artillery", C_ART }, { "warships", C_WARSHIPS }, { "galleys", C_GALLEYS }, { "transports", C_TRANSPORTS },
	{ "provincetax", C_PROVTAX }, { "provincemanpower", C_PROVMANPOWER }, { "mine", C_MINE }, { "flag", C_FLAG },
	{ "trigger", C_TRIGGER }, { "vp", C_VP }, { "casusbelli", C_CASUSBELLI }, { "capital", C_CAPITAL },
	{ "natives", C_NATIVES }, { "bank", C_BANK }, { "stock", C_STOCK }, { "provinceculture", C_PROVCULTURE },
	{ "culture", C_PROVCULTURE }, { "addcore", C_ADDCORE }, { "removecore", C_REMOVECORE }, { "cot", C_COT },
	{ "secedeprovince", C_SECEDE }, { "province_revoltrisk", C_PROVREVOLTRISK },
	{ "sleepmonarch", C_SLEEPMONARCH }, { "sleepleader", C_SLEEPLEADER },
	{ "add_countryculture", C_ADDCULTURE }, { "remove_countryculture", C_REMCULTURE }, { "loansize", C_LOANSIZE },
	{ "sleepevent", C_SLEEPEVENT }, { "mission", C_MISSION }, { "war", C_WAR }, { "technology", C_TECHNOLOGY },
	{ "independence", C_INDEPENDENCE }, { "flagname", C_FLAGNAME }, { "badboy", C_BADBOY }, { "ai", C_AI },
	{ "setflag", C_SETFLAG }, { "clrflag", C_CLRFLAG }, { "removecot", C_REMOVECOT }
};

typedef struct { unsigned char code; long which, value; const char *str; } Cmd;

typedef struct {
	long id;
	unsigned char random, happened, pending;
	short country, province;
	long date, deathdate, offset, expiry;     /* day numbers; date already shifted by the offset */
	int trig;                                 /* index of the root AND node, -1 none */
	int act[4], nact[4];                      /* command ranges */
	const char *name, *desc, *aname[4];       /* text.csv keys (aname NULL = no such action) */
} Event;

typedef struct { long day; int ev; short country; } Msg;

struct Events {
	int kinds;                 /* bit 0 random, bit 1 historical events (the GAME EVENTS option) */
	EventUnitFn unitfn; void *unituser;
	EventCmdFn cmdfn; void *cmduser;
	World *w;
	Event *ev; int nev, cap;
	TNode *tn; int ntn, tcap;
	Cmd *cmd; int ncmd, ccap;
	int *pool; int npool;                     /* random event indices */
	Msg *heap; int nheap, hcap;
	char *strs; unsigned long sused, ssize;   /* event names etc. (own copies: file buffers are freed) */
	unsigned long flags;                      /* numbered global flags (§9 code 0x34) */
	long today;
	long last_random_pick;                    /* the shared "-3" value (§7.2) */
	EventLogFn log; void *loguser;
	EventChoiceFn choice; void *chuser;
	long cs_day; unsigned long cs_epoch; short cs[MAX_COUNTRY];   /* countrysize cache (per day) */
	int *byid; /* event indices sorted by id */
	EventStats st;
};

/* ------------------------------------------------------------- storage */
static void *grow(void *p, int *cap, int need, size_t el)
{
	if (need <= *cap) return p;
	*cap = need * 2 + 64;
	return realloc(p, (size_t)*cap * el);
}

static const char *keep(Events *e, const char *s)
{
	unsigned long n = strlen(s) + 1;
	if (e->sused + n > e->ssize) {
		/* names are only for logs: a simple chain of blocks */
		unsigned long sz = 32768;
		char *b = malloc(sz + sizeof(char *));
		if (!b) return "";
		*(char **)b = e->strs; e->strs = b; e->sused = sizeof(char *); e->ssize = sz + sizeof(char *);
	}
	memcpy(e->strs + e->sused, s, n);
	e->sused += n;
	return e->strs + e->sused - n;
}

/* ------------------------------------------------------- symbol helpers */
static long country_of(World *w, const char *s)
{
	short c;
	if (!s) return -1;
	if ((s[0] >= '0' && s[0] <= '9') || s[0] == '-') return parse_int(s);   /* -1 etc. */
	c = world_country(w, s);
	return c;
}

static const char *sliders[8] = { "aristocracy", "centralization", "innovative", "mercantilism", "offensive", "land", "quality", "serfdom" };
static int slider_of(const char *s)
{
	int i;
	if (!s) return -1;
	for (i = 0; i < 8; i++) if (script_ieq(s, sliders[i])) return i;
	if (script_ieq(s, "centralisation")) return 1;
	return (s[0] >= '0' && s[0] <= '9') ? (int)parse_int(s) : -1;
}

/* ------------------------------------------------------- trigger build */
/* keyword lookups through small case-insensitive hash tables, built once (load-time speed on 68k) */
static unsigned kw_hash(const char *s)
{
	unsigned h = 5381;
	for (; *s; s++) { unsigned char c = (unsigned char)*s; if (c >= 'A' && c <= 'Z') c += 32; h = h * 33 + c; }
	return h & 255;
}
static short g_tkw[256], g_ckw[256];      /* index + 1 */
static int g_kw_ready;
static void kw_init(void)
{
	int i;
	unsigned h;
	for (i = 0; i < T_NTYPES; i++) { for (h = kw_hash(tnames[i]); g_tkw[h]; h = (h + 1) & 255) ; g_tkw[h] = (short)(i + 1); }
	for (i = 0; i < (int)(sizeof ckeys / sizeof ckeys[0]); i++) { for (h = kw_hash(ckeys[i].k); g_ckw[h]; h = (h + 1) & 255) ; g_ckw[h] = (short)(i + 1); }
	g_kw_ready = 1;
}
static int trig_type(const char *s)
{
	unsigned h;
	if (!g_kw_ready) kw_init();
	for (h = kw_hash(s); g_tkw[h]; h = (h + 1) & 255) if (script_ieq(tnames[g_tkw[h] - 1], s)) return g_tkw[h] - 1;
	return -1;
}
static int cmd_code(const char *s)
{
	unsigned h;
	if (!g_kw_ready) kw_init();
	for (h = kw_hash(s); g_ckw[h]; h = (h + 1) & 255) if (script_ieq(ckeys[g_ckw[h] - 1].k, s)) return ckeys[g_ckw[h] - 1].c;
	return -1;
}

static int build_trigger(Events *e, const SNode *n, int op);

static int add_node(Events *e, int op, long a, long b)
{
	e->tn = grow(e->tn, &e->tcap, e->ntn + 1, sizeof *e->tn);
	e->tn[e->ntn].op = (unsigned char)op; e->tn[e->ntn].size = 1; e->tn[e->ntn].a = a; e->tn[e->ntn].b = b;
	return e->ntn++;
}

static int build_leaf(Events *e, const SNode *n)
{
	World *w = e->w;
	int op;
	op = trig_type(n->key);
	if (op < 0) return -1;                                 /* "Unknown Trigger Type": skipped (§6) */
	switch (op) {
	case T_AND: case T_OR: case T_NOT: return build_trigger(e, n, op);
	case T_OWNED: case T_CONTROL: case T_CORE: case T_PROVRELIGION: case T_PROVCULTURE: {
		const char *d = script_str(n, "data", NULL);
		long b = -1;
		if (op == T_PROVRELIGION || op == T_PROVCULTURE) b = d ? world_intern(w, d) : -1;
		else if (d) { b = country_of(w, d); if (b == 0 && d[0] == '0') b = -1; }
		return add_node(e, op, script_int(n, "province", -1), b);
	}
	case T_EXISTS: case T_NEIGHBOUR: return add_node(e, op, country_of(w, n->val), 0);
	case T_RELIGION: case T_CONTINENT: case T_FLAG: return add_node(e, op, n->val ? world_intern(w, n->val) : -1, 0);
	case T_ATWAR: case T_AI: case T_EMPEROR: case T_ELECTOR: return add_node(e, op, parse_yes(n->val), 0);
	case T_DOMESTIC: return add_node(e, op, slider_of(script_str(n, "type", NULL)), script_int(n, "value", 0));
	case T_WAR: case T_ALLIANCE: case T_DYNASTIC: case T_VASSAL: {
		const SNode *c1 = script_get(n, "country"), *c2 = c1 ? script_next(c1, "country") : NULL;
		return add_node(e, op, c1 ? country_of(w, c1->val) : -1, c2 ? country_of(w, c2->val) : -1);
	}
	case T_RELATION: return add_node(e, op, country_of(w, script_str(n, "country", NULL)), script_int(n, "data", 0));
	default: return add_node(e, op, n->val ? parse_int(n->val) : 0, 0);
	}
}

/* builds an AND/OR/NOT node over the children of n; returns its index */
static int build_trigger(Events *e, const SNode *n, int op)
{
	int root = add_node(e, op, 0, 0);
	const SNode *c;
	for (c = n->child; c; c = c->next) if (c->key) build_leaf(e, c);
	e->tn[root].size = (unsigned short)(e->ntn - root);
	return root;
}

/* ------------------------------------------------------ trigger eval */
static int has_core(const Province *p, int c) { int k; for (k = 0; k < 8; k++) if (p->cores[k] == c) return 1; return 0; }


static int who_of(long v, int c) { return v == -1 ? c : (int)v; }   /* -1 in a trigger = this country (NIEPEWNE) */

/* b < 0: is a at war with anyone; else are a and b on opposite sides of some war */
static int war_side(const World *w, int a, int b)
{
	int i, k;
	if (a < 0) { a = b; b = -1; }
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		int sa = 0, sb = 0;
		for (k = 0; k < wr->natt; k++) { if (wr->att[k] == a) sa = 1; if (wr->att[k] == b) sb = 1; }
		for (k = 0; k < wr->ndef; k++) { if (wr->def[k] == a) sa = 2; if (wr->def[k] == b) sb = 2; }
		if (sa && (b < 0 || (sb && sb != sa))) return 1;
	}
	return 0;
}

static int exists(const World *w, long c) { return c >= 0 && c < w->ncountry && w->country[c].nprov_owned > 0; }

/* countrysize for every country at once, cached for the day: the trigger is evaluated thousands of
 * times a day by the random-event picker (683 uses in the data). Any executed command drops it. */
static int countrysize_c(Events *e, int c)
{
	const World *w = e->w;
	if (e->cs_day != e->today || e->cs_epoch != w->owner_epoch) {
		int i;
		memset(e->cs, 0, sizeof e->cs);
		for (i = 1; i < w->nprov; i++)
			if (w->prov[i].owner >= 0 && w->prov[i].population >= 1000) e->cs[w->prov[i].owner]++;
		e->cs_day = e->today; e->cs_epoch = w->owner_epoch;
	}
	return e->cs[c];
}

static int event_happened(const Events *e, long id)
{
	int lo = 0, hi = e->nev - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		long v = e->ev[e->byid[mid]].id;
		if (v == id) return e->ev[e->byid[mid]].happened;
		if (v < id) lo = mid + 1; else hi = mid - 1;
	}
	return 0;
}

static int eval(Events *e, int i, int c)
{
	const TNode *t = &e->tn[i];
	World *w = e->w;
	const Country *co = &w->country[c];
	int y, m, d;
	switch (t->op) {
	case T_AND: case T_OR: case T_NOT: {
		int k = i + 1, end = i + t->size, any = 0;
		while (k < end) {
			int r = eval(e, k, c);
			if (t->op == T_AND && !r) return 0;
			if (r) any = 1;
			if (t->op == T_OR && r) return 1;
			if (t->op == T_NOT && r) return 0;
			k += e->tn[k].size;
		}
		return t->op == T_AND ? 1 : t->op == T_OR ? any : 1;
	}
	case T_OWNED: case T_CONTROL: case T_CORE: {
		long p = t->a, who = t->b <= 0 ? c : t->b;
		if (p <= 0 || p >= w->nprov) return 0;
		if (t->op == T_OWNED) return w->prov[p].owner >= 0 && w->prov[p].owner == who;
		if (t->op == T_CONTROL) return w->prov[p].controller == who;
		return w->prov[p].owner >= 0 && has_core(&w->prov[p], (int)who);
	}
	case T_PROVRELIGION: return t->a > 0 && t->a < w->nprov && w->prov[t->a].religion == t->b;
	case T_PROVCULTURE: return t->a > 0 && t->a < w->nprov && w->prov[t->a].culture == t->b;
	case T_EXISTS: return exists(w, t->a);
	case T_RELIGION: return co->religion == t->a;
	case T_CONTINENT: return co->capital > 0 && co->capital < w->nprov && w->prov[co->capital].continent == t->a;
	case T_EVENT: return event_happened(e, t->a);
	case T_YEAR: cal_date(e->today, &y, &m, &d); return y >= t->a;
	case T_STABILITY: return co->stability >= t->a;
	case T_LAND: return co->land_tech >= t->a;
	case T_NAVAL: return co->naval_tech >= t->a;
	case T_INFRA: return co->infra_tech >= t->a;
	case T_TRADE: return co->trade_tech >= t->a;
	case T_DOMESTIC: return t->a >= 0 && t->a < 8 && co->policy[t->a] >= t->b;
	case T_COUNTRYSIZE: return countrysize_c(e, c) >= t->a;
	case T_BADBOY: return co->badboy_m >= t->a * 1000;
	case T_AI: return (!co->human) == t->a;
	case T_ATWAR: return war_side(w, -1, c) == t->a;
	case T_WAR: { int a = who_of(t->a, c), b = who_of(t->b, c); return a >= 0 && b >= 0 && war_side(w, a, b); }
	case T_ALLIANCE: return world_allied(w, who_of(t->a, c), who_of(t->b, c), ALLY_MILITARY);
	case T_DYNASTIC: return world_allied(w, who_of(t->a, c), who_of(t->b, c), ALLY_DYNASTIC);
	case T_VASSAL: { int b = who_of(t->b, c); return b >= 0 && world_overlord(w, b) == who_of(t->a, c); }   /* first = overlord (NIEPEWNE) */
	case T_RELATION: return t->a >= 0 && world_rel(w, c, (int)t->a) >= t->b * 1000;
	case T_FLAG: return 0;                   /* named flags: setflag / clrflag are unused in the data */
	case T_EMPEROR: return (c == w->emperor) == (t->a != 0);   /* 0x59ba40 */
	case T_ELECTOR: return (co->elector > 0) == (t->a != 0);   /* 0x59ba70: weight > 0 */
	case T_MONARCH: return e->cmdfn && e->cmdfn(e->cmduser, EVC_IS_MONARCH, c, t->a, 0);
	case T_LEADER: return e->cmdfn && e->cmdfn(e->cmduser, EVC_IS_LEADER, c, t->a, 0);
	case T_NEIGHBOUR: return t->a >= 0 && e->cmdfn && e->cmdfn(e->cmduser, EVC_NEIGHBOUR, c, t->a, 0);
	case T_DISCOVERED: return t->a > 0 && t->a < w->nprov && WORLD_KNOWN(w, c, t->a);
	default: return 0;                       /* cot / emperor / elector: unused in the data */
	}
}

/* ----------------------------------------------------------- loading */
static void parse_commands(Events *e, const SNode *act, int slot, Event *ev)
{
	const SNode *c;
	ev->act[slot] = e->ncmd; ev->nact[slot] = 0;
	if (!act) return;
	for (c = act->child; c; c = c->next) {
		const char *ty, *wh, *va;
		Cmd *k;
		if (!c->key || !script_ieq(c->key, "command")) continue;
		e->cmd = grow(e->cmd, &e->ccap, e->ncmd + 1, sizeof *e->cmd);
		k = &e->cmd[e->ncmd++]; ev->nact[slot]++;
		ty = script_str(c, "type", NULL); wh = script_str(c, "which", NULL); va = script_str(c, "value", NULL);
		k->code = C_NOP; k->which = 0; k->value = 0; k->str = NULL;
		if (!ty) continue;                                  /* command = { } : valid no-op */
		k->code = C_TREASURY;                               /* unknown keyword -> treasury (§9) */
		{ int cc = cmd_code(ty); if (cc >= 0) k->code = (unsigned char)cc; }
		switch (k->code) {
		case C_DYNASTIC: case C_VASSAL: case C_BREAKDYN: case C_BREAKVASSAL: case C_ALLIANCE: case C_INHERIT:
		case C_RELATION: case C_CASUSBELLI: case C_COUNTRY: case C_WAR: case C_INDEPENDENCE: case C_SECEDE:
			k->which = country_of(e->w, wh); k->value = va ? parse_int(va) : 0; break;
		case C_DOMESTIC: k->which = slider_of(wh); k->value = va ? parse_int(va) : 0; break;
		case C_RELIGION: k->which = wh ? world_intern(e->w, wh) : -1; break;
		case C_PROVRELIGION: case C_PROVCULTURE:
			k->which = wh ? parse_int(wh) : 0;
			k->value = va ? ((va[0] == '-' || (va[0] >= '0' && va[0] <= '9')) ? parse_int(va) : world_intern(e->w, va)) : -1;
			break;
		case C_ADDCULTURE: case C_REMCULTURE: k->which = wh ? world_intern(e->w, wh) : -1; break;
		case C_GAINBUILD: case C_LOSEBUILD: {              /* value = the building by name */
			static const char *const bn[5] = { "shipyard", "barrack", "bailiff", "cityrights", "courthouse" };
			int q;
			k->which = wh ? parse_int(wh) : 0; k->value = va ? parse_int(va) : 0;
			for (q = 0; va && q < 5; q++) if (script_ieq(va, bn[q])) k->value = q;
			break; }
		case C_ADDCORE: case C_REMOVECORE:                 /* which = a province, or a tag with the province in value (PER 529) */
			if (wh && !(wh[0] == '-' || (wh[0] >= '0' && wh[0] <= '9'))) { k->which = va ? parse_int(va) : 0; k->value = country_of(e->w, wh) + 1; }
			else { k->which = wh ? parse_int(wh) : 0; k->value = 0; }
			break;
		case C_TECHNOLOGY:                                 /* which = the tech group by name (muslim, exotic, torthodox ...) */
			if (wh && !(wh[0] == '-' || (wh[0] >= '0' && wh[0] <= '9'))) { k->str = keep(e, wh); k->which = -1; }
			else k->which = wh ? parse_int(wh) : 0;
			break;
		case C_GAINMANU: {                                 /* value = the manufactory by name (city+0x1c 1..5: luxury weapons naval refinery goods) */
			static const char *const mn[5] = { "luxury", "weapons", "navalequipment", "refinery", "goods" };
			int q;
			k->which = wh ? parse_int(wh) : 0; k->value = va ? parse_int(va) : 0;
			for (q = 0; va && q < 5; q++) if (script_ieq(va, mn[q])) k->value = q + 1;
			break; }
		case C_FLAGNAME: case C_AI: case C_SETFLAG: case C_CLRFLAG: k->str = keep(e, wh ? wh : ""); break;
		default: k->which = wh ? parse_int(wh) : 0; k->value = va ? parse_int(va) : 0; break;
		}
	}
}

static long date_of(const SNode *b)
{
	static const char *mn[12] = { "january", "february", "march", "april", "may", "june", "july", "august",
	                              "september", "october", "november", "december" };
	const char *m;
	int k, mo = 1;
	if (!b) return 0;
	m = script_str(b, "month", "january");
	for (k = 0; k < 12; k++) if (script_ieq(m, mn[k])) mo = k + 1;
	return cal_day((int)script_int(b, "year", 0), mo, (int)script_int(b, "day", 0));
}

static void load_event(Events *e, const SNode *b)
{
	Event *ev;
	const SNode *n;
	int i;
	e->ev = grow(e->ev, &e->cap, e->nev + 1, sizeof *e->ev);
	ev = &e->ev[e->nev++];
	memset(ev, 0, sizeof *ev);
	ev->id = script_int(b, "id", 0);
	ev->random = (unsigned char)parse_yes(script_str(b, "random", "no"));
	{ const char *c = script_str(b, "country", NULL); ev->country = c ? (short)world_country(e->w, c) : -1; }
	ev->province = (short)script_int(b, "province", -1);
	ev->date = script_get(b, "date") ? date_of(script_get(b, "date")) : 0;
	ev->deathdate = script_get(b, "deathdate") ? date_of(script_get(b, "deathdate")) : 0;
	ev->offset = script_int(b, "offset", 0);
	ev->name = keep(e, script_str(b, "name", ""));
	ev->desc = keep(e, script_str(b, "desc", ""));
	ev->trig = -1;
	for (n = b->child; n; n = n->next)                     /* every trigger block appends to one AND (§3) */
		if (n->key && script_ieq(n->key, "trigger")) {
			if (ev->trig < 0) ev->trig = build_trigger(e, n, T_AND);
			else {
				const SNode *c;
				for (c = n->child; c; c = c->next) if (c->key) build_leaf(e, c);
				e->tn[ev->trig].size = (unsigned short)(e->ntn - ev->trig);
			}
		}
	for (i = 0; i < 4; i++) {
		static const char *an[4] = { "action_a", "action_b", "action_c", "action_d" };
		const SNode *ab = script_get(b, an[i]);
		ev->aname[i] = ab ? keep(e, script_str(ab, "name", "OK")) : NULL;
		parse_commands(e, ab, i, ev);
	}
}

static int load_file(Events *e, Pak *t, const char *path, int depth, char *err, int errlen)
{
	char key[160], *buf;
	unsigned long sz;
	int i;
	Arena *a;
	SNode *root, *n;
	for (i = 0; path[i] && i < (int)sizeof key - 1; i++) {
		char c = path[i] == '\\' ? '/' : path[i];
		key[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	key[i] = 0;
	if (depth > 4 || !(buf = (char *)pak_load(t, key, &sz))) return 0;    /* the original logs and goes on */
	a = arena_new(65536);
	root = script_parse(buf, sz, a, err, errlen);
	if (root) {
		e->st.files++;
		for (n = root->child; n; n = n->next) {
			if (!n->key) continue;
			if (script_ieq(n->key, "event") && n->child) load_event(e, n);
			else if (script_ieq(n->key, "event") && n->val && depth == 0) load_file(e, t, n->val, depth + 1, err, errlen);
			else if (script_ieq(n->key, "include") && n->val) load_file(e, t, n->val, depth + 1, err, errlen);
		}
	}
	arena_free(a);
	free(buf);
	return root ? 0 : -1;
}

/* ------------------------------------------------------------- queue */
static void push(Events *e, long day, int ev, int country)
{
	int i;
	e->heap = grow(e->heap, &e->hcap, e->nheap + 1, sizeof *e->heap);
	i = e->nheap++;
	while (i > 0 && e->heap[(i - 1) / 2].day > day) { e->heap[i] = e->heap[(i - 1) / 2]; i = (i - 1) / 2; }
	e->heap[i].day = day; e->heap[i].ev = ev; e->heap[i].country = (short)country;
}

static Msg pop(Events *e)
{
	Msg top = e->heap[0], last = e->heap[--e->nheap];
	int i = 0;
	for (;;) {
		int c = 2 * i + 1;
		if (c >= e->nheap) break;
		if (c + 1 < e->nheap && e->heap[c + 1].day < e->heap[c].day) c++;
		if (last.day <= e->heap[c].day) break;
		e->heap[i] = e->heap[c]; i = c;
	}
	if (e->nheap) e->heap[i] = last;
	return top;
}

/* ------------------------------------------------------ random chain */
static void schedule_random(Events *e, int c)
{
	int n = e->w->country[c].nprov_owned, base, tries, cap = e->npool * 4 + 16;
	long delay;
	if (!e->npool) return;
	/* pick (§5.1) with an attempt cap (§12): the original spins forever */
	for (tries = 0; tries < cap; tries++) {
		int k = e->pool[ev_rand() % e->npool];
		Event *ev = &e->ev[k];
		if (ev->pending) continue;
		if (ev->country >= 0 && ev->country != c) continue;
		if (ev->province > 0 && (ev->province >= e->w->nprov || e->w->prov[ev->province].owner != c)) continue;
		if (ev->trig >= 0 && !eval(e, ev->trig, c)) continue;
		base = (25 - n) * 20; if (base < 100) base = 100;
		delay = ev_rand() % 100 + 100 + ev_rand() % 100 + base + ev_rand() % base;
		push(e, e->today + delay, k, c);
		return;
	}
	e->st.cap_hits++;
	base = (25 - n) * 20; if (base < 100) base = 100;      /* nothing qualified: re-arm the chain */
	push(e, e->today + 200 + base, -1, c);
}

/* ------------------------------------------------------------ commands */
static int pick_province(Events *e, int c, int code)
{
	World *w = e->w;
	int i, n = 0, pickn;
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == c && w->prov[i].population > 0) {
		if ((code == C_RELREVOLT || code == C_CONVERSION) && w->prov[i].religion == w->country[c].religion) continue;
		n++;
	}
	if (!n) return -1;
	pickn = ev_rand() % n;
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner == c && w->prov[i].population > 0) {
		if ((code == C_RELREVOLT || code == C_CONVERSION) && w->prov[i].religion == w->country[c].religion) continue;
		if (pickn-- == 0) return i;
	}
	return -1;
}

/* SPEC_EVENTS 7.2: a random eligible country - existing, not us, its capital known to us, plus the rule of
 * the command (dynastic / vassal / relation / casusbelli: a neighbour - the exe's "or near" float is
 * NIEPEWNE; breakdynastic: married to us; breakvassal: our vassal; alliance: not allied; inherit: smaller
 * and married to us). -1 when nobody qualifies (the command is dropped). */
static int pick_country(Events *e, int c, int code)
{
	World *w = e->w;
	int i, n = 0, pass, pickn = 0, got = -1;
	for (pass = 0; pass < 2 && got < 0; pass++) {
		if (pass) { if (!n) return -1; pickn = ev_rand() % n; }
		for (i = 0; i < w->ncountry; i++) {
			int cap = w->country[i].capital, ok;
			if (i == c || !exists(w, i) || i == w->reb || cap <= 0 || cap >= w->nprov || !WORLD_KNOWN(w, c, cap)) continue;
			switch (code) {
			case C_DYNASTIC: case C_VASSAL: case C_RELATION: case C_CASUSBELLI:
				ok = e->cmdfn && e->cmdfn(e->cmduser, EVC_NEIGHBOUR, c, i, 0); break;
			case C_BREAKDYN: ok = world_allied(w, c, i, ALLY_DYNASTIC); break;
			case C_BREAKVASSAL: ok = world_overlord(w, i) == c; break;
			case C_ALLIANCE: ok = !world_allied(w, c, i, ALLY_MILITARY); break;
			case C_INHERIT: ok = w->country[i].nprov_owned < w->country[c].nprov_owned && world_allied(w, c, i, ALLY_DYNASTIC); break;
			default: ok = 0; break;
			}
			if (!ok) continue;
			if (!pass) n++;
			else if (pickn-- == 0) { got = i; break; }
		}
	}
	return got;
}

static void ally_remove(World *w, int i) { w->ally[i] = w->ally[--w->nally]; }
static void ally_add2(World *w, int type, int a, int b, int years)
{
	Alliance *al;
	if (w->nally >= MAX_ALLY) return;
	al = &w->ally[w->nally++];
	memset(al, 0, sizeof *al);
	al->type = (unsigned char)type; al->member[0] = (short)a; al->member[1] = (short)b; al->n = 2;
	al->expiry = w->now; al->expiry.year = (short)(w->now.year + years);
}

static int province_cmd(int code)
{
	switch (code) {
	case C_REVOLT: case C_RELREVOLT: case C_COLREVOLT: case C_HERETIC: case C_CONVERSION: case C_PROVRELIGION:
	case C_GAINMANU: case C_LOSEMANU: case C_POPULATION: case C_PROVTAX: case C_PROVMANPOWER: case C_MINE:
	case C_PROVCULTURE: case C_FORTRESS: case C_PROVREVOLTRISK: case C_CAPITAL: case C_INF: case C_CAV: case C_ART:
	case C_WARSHIPS: case C_GALLEYS: case C_TRANSPORTS: case C_EXPLORER: case C_CONQUISTADOR: case C_GAINBUILD: case C_LOSEBUILD:
	case C_DESERTION: case C_ADDCORE: case C_REMOVECORE: case C_COT: case C_REMOVECOT: return 1;
	default: return 0;
	}
}

/* resolves which (§7.2) and checks validity (§8, the part our model can judge); 0 = invalid */
static int resolve(Events *e, Cmd *k, int c, long *which)
{
	World *w = e->w;
	*which = k->which;
	if (k->code == C_NOP) return 1;
	if (k->code == C_MISSION) return 0;
	if (province_cmd(k->code)) {
		if (*which == -1 || *which == -4) { *which = pick_province(e, c, k->code); e->last_random_pick = *which; }
		else if (*which == -2) *which = w->country[c].capital;
		else if (*which == -3) *which = e->last_random_pick;
		if (*which <= 0 || *which >= w->nprov) return 0;
		if (k->code != C_ADDCORE && k->code != C_REMOVECORE && k->code != C_COT && w->prov[*which].owner != c) return 0;
		return 1;
	}
	switch (k->code) {
	case C_DYNASTIC: case C_VASSAL: case C_ALLIANCE: case C_INHERIT: case C_BREAKDYN: case C_BREAKVASSAL:
	case C_RELATION: case C_CASUSBELLI: case C_WAR:
		if (*which == -1) { *which = pick_country(e, c, k->code); e->last_random_pick = *which; }
		else if (*which == -3) *which = e->last_random_pick;
		return exists(w, *which) && *which != c;
	case C_SECEDE: return exists(w, *which) && k->value > 0 && k->value < w->nprov && w->prov[k->value].owner == c;
	case C_COUNTRY: case C_INDEPENDENCE: return *which >= 0 && !exists(w, *which);
	default: return 1;
	}
}

static void clamp(short *v, int lo, int hi) { if (*v < lo) *v = (short)lo; if (*v > hi) *v = (short)hi; }

static void execute(Events *e, Cmd *k, int c, long which)
{
	World *w = e->w;
	Country *co = &w->country[c];
	Province *p = (which > 0 && which < w->nprov) ? &w->prov[which] : NULL;
	e->st.executed_cmds++;
	e->cs_day = -1;                                         /* state changed: drop the countrysize cache */
	switch (k->code) {
	case C_NOP: break;
	case C_BANK: co->bank = 1; break;                                       /* country vtable +0x354: country+0xfdc */
	case C_STOCK: co->stock = 1; break;                                     /* +0x350: +0xfdb (its use NIEZNANE) */
	case C_TREASURY: co->treasury_m += k->value * 1000;
		if (k->value > 0) world_book_inc(w, c, INC_EVENT, k->value * 1000); else world_book_exp(w, c, EXP_EVENT, -k->value * 1000);
		break;
	case C_INFLATION: co->inflation_u += k->value * 10000; if (co->inflation_u < 0) co->inflation_u = 0; break;   /* value in percent (§9: x 0.01); never below 0 (SPEC_TIME_ECONOMY §7) */
	case C_STABILITY: {                                   /* applied as |n| single steps, clamped -3..+3 (§9) */
		long s;
		for (s = k->value < 0 ? -k->value : k->value; s > 0; s--) { short v = (short)(co->stability + (k->value < 0 ? -1 : 1)); clamp(&v, -3, 3); co->stability = v; }
		break;
	}
	case C_DOMESTIC:
		if (which >= 0 && which < 8) { short v = (short)(co->policy[which] + k->value); clamp(&v, 0, 10); co->policy[which] = (signed char)v; }
		break;
	case C_RELIGION: if (which >= 0) co->religion = (short)which; break;
	case C_MANPOWER: co->manpower_m += k->value * 1000; break;
	case C_LAND: co->inv_land_m += k->value * 1000; break;
	case C_NAVAL: co->inv_naval_m += k->value * 1000; break;
	case C_INFRA: co->inv_infra_m += k->value * 1000; break;
	case C_TRADE: co->inv_trade_m += k->value * 1000; break;
	case C_DIPLOMATS: co->diplomats_m += k->value * 1000; break;
	case C_COLONISTS: co->colonists_m += k->value * 1000; break;
	case C_MERCHANTS: co->merchants_m += k->value * 1000; break;
	case C_MISSIONARIES: co->missionaries_m += k->value * 1000; break;
	case C_VP: world_vp(e->w, (int)(co - e->w->country), VP_MISS, 1000L * k->value); break;   /* event 0x17b: category 6 */
	case C_INF: case C_CAV: case C_ART: case C_WARSHIPS: case C_GALLEYS: case C_TRANSPORTS: case C_CONQUISTADOR: case C_EXPLORER:
		if (!e->unitfn || !p) { e->st.executed_cmds--; e->st.unimplemented_cmds++; break; }
		e->unitfn(e->unituser, c, (int)which, k->code == C_INF ? 0 : k->code == C_CAV ? 1 : k->code == C_ART ? 2 : k->code == C_WARSHIPS ? 3 :
		          k->code == C_GALLEYS ? 4 : k->code == C_TRANSPORTS ? 5 : k->code == C_CONQUISTADOR ? 6 : 7, k->value);
		break;
	case C_BADBOY: co->badboy_m += k->value * 1000; if (co->badboy_m < 0) co->badboy_m = 0; break;
	case C_CONVERSION: if (p) p->religion = co->religion; break;
	case C_PROVRELIGION: if (p && k->value >= 0) p->religion = (short)k->value; break;
	case C_PROVCULTURE: if (p) p->culture = (short)(k->value < 0 ? co->culture : k->value); break;
	case C_PROVTAX: if (p) { p->income_m += k->value * 1000; if (p->income_m < 1000) p->income_m = 1000; } break;
	case C_PROVMANPOWER: if (p) { p->manpower_m += k->value * 1000; if (p->manpower_m < 1000) p->manpower_m = 1000; } break;
	case C_POPULATION:
		if (p) { long np = p->population + k->value, floor = p->population < 1000 ? p->population : 1000; p->population = np > floor ? np : floor; }
		break;
	case C_FORTRESS: if (p) { int f = p->fort + (int)k->value; p->fort = (unsigned char)(f < 0 ? 0 : f > 6 ? 6 : f); } break;
	case C_PROVREVOLTRISK: if (p) p->revoltrisk_m += k->value * 1000; break;
	case C_CAPITAL: if (p) co->capital = (short)which; break;
	case C_ADDCORE: world_add_core(w, (int)which, (short)(k->value > 0 ? k->value - 1 : c)); break;
	case C_REMOVECORE: world_remove_core(w, (int)which, (short)(k->value > 0 ? k->value - 1 : c)); break;
	case C_SECEDE:
		w->prov[k->value].owner = w->prov[k->value].controller = (short)which;
		co->nprov_owned--; w->country[which].nprov_owned++;
		w->owner_epoch++;
		break;
	case C_FLAG: if (which >= 0 && which < 32) e->flags |= 1UL << which; if (which >= 0 && which < 8) w->gflag[which] = 1; break;   /* 0x14f SETEVENTFLAG: only ever set */
	case C_SLEEPEVENT: {
		int i;
		for (i = 0; i < e->nev; i++) if (e->ev[i].id == which) e->ev[i].happened = 1;
		break;
	}
	case C_RELATION: world_rel_add(w, c, (int)which, k->value * 1000); break;
	case C_CASUSBELLI:                                                      /* for value months (0: no end, NIEPEWNE) */
		if (k->value > 0) world_give_cb(w, c, (int)which, e->today + 30L * k->value); else w->dipflag[c][which] |= DF_CB_PERM;
		break;
	case C_DYNASTIC: if (!world_allied(w, c, (int)which, ALLY_DYNASTIC)) ally_add2(w, ALLY_DYNASTIC, c, (int)which, 25); break;
	case C_VASSAL: if (world_overlord(w, (int)which) < 0) ally_add2(w, ALLY_VASSAL, c, (int)which, 100); break;
	case C_BREAKDYN: case C_BREAKVASSAL: {
		int i, j, a2, b2;
		for (i = w->nally - 1; i >= 0; i--) {
			const Alliance *al = &w->ally[i];
			if (al->type != (k->code == C_BREAKDYN ? ALLY_DYNASTIC : ALLY_VASSAL)) continue;
			for (a2 = b2 = 0, j = 0; j < al->n; j++) { if (al->member[j] == c) a2 = 1; if (al->member[j] == which) b2 = 1; }
			if (a2 && b2) ally_remove(w, i);
		}
		break;
	}
	case C_ALLIANCE: {                                      /* add C to our alliance; create one if none */
		int ai = world_ally_of(w, c, ALLY_MILITARY);
		if (world_allied(w, c, (int)which, ALLY_MILITARY)) break;
		if (ai >= 0 && w->ally[ai].n < 24) w->ally[ai].member[w->ally[ai].n++] = (short)which;
		else if (ai < 0) ally_add2(w, ALLY_MILITARY, c, (int)which, 10);
		break;
	}
	case C_GAINMANU: if (p) p->manu = (unsigned char)k->value; break;   /* 0x15d -> 0x4962f2 / 0x496308: city+0x1c = value as is; -1 (3 events)
	                                                                      is kept as 255, a manufactory of no type (counted, no icon, no type effect) */
	case C_LOSEMANU: if (p) p->manu = 0; break;
	case C_GAINBUILD: case C_LOSEBUILD: {                   /* shipyard 0, barrack 1, bailiff 2, cityrights 3, courthouse 4 */
		static const unsigned char bf[5] = { 0x01, 0x02, 0x04, 0x08, 0x10 };   /* buildings.h PB_* */
		if (p && k->value >= 0 && k->value < 5) { if (k->code == C_GAINBUILD) p->bflags |= bf[k->value]; else p->bflags &= (unsigned char)~bf[k->value]; }
		break;
	}
	case C_DESERTION: {                                     /* up to value men from our armies in P, army by army */
		int i;
		long left = k->value;
		for (i = 0; i < w->nland && left > 0 && p; i++) {
			LandUnit *u = &w->land[i];
			long t;
			if (u->dead || u->owner != c || u->location != which) continue;
			t = u->infantry < left ? u->infantry : left; u->infantry -= t; left -= t;
			t = u->cavalry < left ? u->cavalry : left; u->cavalry -= t; left -= t;
			if (u->infantry + u->cavalry + u->artillery <= 0) u->dead = 1;
		}
		break;
	}
	case C_MINE:
		if (p && p->goods >= 0 && (script_ieq(world_name(w, p->goods), "gold") || script_ieq(world_name(w, p->goods), "metal")))
			{ p->mine_value += k->value; if (p->mine_value < 0) p->mine_value = 0; }
		break;
	case C_HERETIC: if (p) {                                /* 0x42f900: the "heretic" counterpart */
		static const char *const hn[11] = { "reformed", "protestant", "protestant", "protestant", "catholic", "shiite", "sunni", "pagan", "buddhism", "hinduism", "hinduism" };
		static const char *const rn[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite", "pagan", "confucianism", "buddhism", "hinduism" };
		int r;
		for (r = 0; r < 11; r++) if (script_ieq(world_name(w, p->religion), rn[r])) { p->religion = world_intern(w, hn[r]); w->owner_epoch++; break; }
		break; }
	case C_NATIVES: if (p) { p->natives += k->value; if (p->natives < 0) p->natives = 0; } break;
	case C_COT: if (p && w->cot_ev) { w->cot_ev(w->cot_evu, 0, which); break; }   /* 0x454720: a CoT founded at P, then the yearly routine */
		if (p) {                                                /* (no economy: the old rule) */
		int i, found = -1;
		for (i = 0; i < w->ncot; i++) if (w->cot[i].prov == which) found = -2;
		for (i = 0; i < w->ncot && found == -1; i++) if (w->prov[w->cot[i].prov].region == p->region) found = i;
		if (found >= 0) w->cot[found].prov = (short)which;
		else if (found == -1 && w->ncot < MAX_COT) { memset(&w->cot[w->ncot], 0, sizeof w->cot[0]); w->cot[w->ncot++].prov = (short)which; }
		w->owner_epoch++;
		break; }
	case C_REMOVECOT: {
		int i;
		if (w->cot_ev) { w->cot_ev(w->cot_evu, 1, which); break; }   /* 0x454780 */
		for (i = 0; i < w->ncot; i++) if (w->cot[i].prov == which) { w->cot[i] = w->cot[--w->ncot]; w->owner_epoch++; break; }
		break; }
	case C_TECHNOLOGY: {
		static const char *const gn[5] = { "latin", "orthodox", "muslim", "china", "exotic" };
		if (k->str && k->str[0]) co->techgroup = world_intern(w, k->str);
		else if (which >= 0 && which < 5) co->techgroup = world_intern(w, gn[which]);
		break; }
	case C_ADM: case C_DIP: case C_MIL: {                   /* which = the change, value = months; no ruler: ignored (main applies it) */
		int s = k->code == C_ADM ? 0 : k->code == C_DIP ? 1 : 2;
		co->ev_stat[s] = (signed char)which; co->ev_stat_until[s] = e->today + 30L * k->value;
		break; }
	case C_FLAGNAME: snprintf(co->flagname, sizeof co->flagname, "%s", k->str ? k->str : ""); break;   /* REV / UK flag graphics */
	case C_LOANSIZE: if (which > 0) co->loansize = (short)which; break;   /* all 21 uses put the size in which (NIEPEWNE) */
	case C_ADDCULTURE:
		if (which >= 0 && !world_culture_ok(w, c, (short)which) && co->nacc < 8) co->acc[co->nacc++] = (short)which;
		break;
	case C_REMCULTURE: {
		int k2;
		for (k2 = 0; k2 < co->nacc; k2++) if (co->acc[k2] == which) { co->acc[k2] = co->acc[--co->nacc]; break; }
		break; }
	case C_REVOLTRISK: co->ev_rr = (short)k->value; co->ev_rr_until = e->today + 30L * which; break;   /* which = months, value = points */
	case C_REVOLT: case C_RELREVOLT: case C_COLREVOLT: case C_INHERIT: case C_WAR:
	case C_MONARCH: case C_SLEEPMONARCH: case C_LEADER: case C_SLEEPLEADER: case C_INDEPENDENCE: case C_COUNTRY: {
		int code = k->code == C_REVOLT ? EVC_REVOLT : k->code == C_RELREVOLT ? EVC_RELREVOLT : k->code == C_COLREVOLT ? EVC_COLREVOLT :
		           k->code == C_INHERIT ? EVC_INHERIT : k->code == C_WAR ? EVC_WAR : k->code == C_MONARCH ? EVC_MONARCH :
		           k->code == C_SLEEPMONARCH ? EVC_SLEEPMONARCH : k->code == C_LEADER ? EVC_LEADER : k->code == C_SLEEPLEADER ? EVC_SLEEPLEADER :
		           k->code == C_INDEPENDENCE ? EVC_INDEPENDENCE : EVC_COUNTRY;
		if (!e->cmdfn || !e->cmdfn(e->cmduser, code, c, which, k->value)) { e->st.executed_cmds--; e->st.unimplemented_cmds++; }
		break; }
	case C_TRIGGER: {
		int i;
		for (i = 0; i < e->nev; i++) if (e->ev[i].id == which) {
			int tc = e->ev[i].country >= 0 ? e->ev[i].country : c;
			push(e, e->today, i, tc);                    /* fires now, through the full handler (§9) */
			break;
		}
		break;
	}
	default: e->st.executed_cmds--; e->st.unimplemented_cmds++; break;
	}
}

/* ------------------------------------------------------------- firing */
static void fire(Events *e, int k, int c)
{
	Event *ev = &e->ev[k];
	long which[64];
	int a, i, n[4], m, r, chosen;
	ev->pending = 1;
	/* resolve + validate all actions (§10.1, §8) */
	for (a = 0; a < 4; a++) {
		n[a] = 0;
		for (i = 0; i < ev->nact[a]; i++) {
			Cmd *cmd = &e->cmd[ev->act[a] + i];
			long wv;
			if (resolve(e, cmd, c, &wv)) n[a]++;
			else if (ev->random) { e->st.dropped_cmds++; ev->pending = 0; return; }   /* random: whole event cancelled */
			else e->st.dropped_cmds++;
		}
	}
	/* AI choice (§10.4). The RNG is drawn for the player too, so a game with and without the dialog
	 * keeps the same random sequence. */
	m = 85; if (n[1]) m = 90; if (n[2]) m = 95; if (n[3]) m = 100;
	r = ev_rand() % m;
	chosen = r < 85 ? 0 : r < 90 ? 1 : r < 95 ? 2 : 3;
	if (e->choice && c >= 0 && e->w->country[c].human) {
		EventInfo info;
		int q;
		info.id = ev->id; info.name = ev->name; info.desc = ev->desc; info.province = ev->province;
		for (q = 0; q < 4; q++) info.action[q] = ev->aname[q];
		q = e->choice(e->chuser, e->today, c, &info);
		if (q >= 0 && q < 4 && ev->aname[q]) chosen = q;
	}
	/* execute: re-resolve each command in file order (validation runs again at execution, §8) */
	for (i = 0; i < ev->nact[chosen] && i < 64; i++) {
		Cmd *cmd = &e->cmd[ev->act[chosen] + i];
		if (resolve(e, cmd, c, &which[i])) execute(e, cmd, c, which[i]);
	}
	ev->pending = 0;
	e->st.fired++;
	if (ev->random) e->st.fired_random++;
	if (e->log) e->log(e->loguser, e->today, c, ev->id, chosen, ev->name);
}

static void retry(Events *e, int k, int c)
{
	Event *ev = &e->ev[k];
	if (ev->deathdate && e->today < ev->deathdate) {
		push(e, e->today + 16 + ev_rand() % 44 + ev_rand() % 16, k, c);
		e->st.retries++;
	}
}

static void handle(Events *e, Msg m)
{
	Event *ev;
	int c = m.country;
	if (m.ev < 0) { if (exists(e->w, c)) schedule_random(e, c); return; }   /* re-armed chain */
	ev = &e->ev[m.ev];
	if (c < 0 && ev->province > 0 && ev->province < e->w->nprov) c = e->w->prov[ev->province].owner;
	if (c < 0 || !exists(e->w, c)) {                           /* no target (§4.2.3) */
		if (!ev->random) retry(e, m.ev, c);
		return;
	}
	if (!ev->random && ev->happened) return;                   /* one-shot (§4.2.4) */
	if (ev->trig < 0 || eval(e, ev->trig, c)) {
		if (!(e->kinds & (ev->random ? 1 : 2))) {          /* GAME EVENTS option: this kind is off */
			if (ev->random) schedule_random(e, c); else ev->happened = 1;
			return;
		}
		if (!ev->random) ev->happened = 1;
		fire(e, m.ev, c);
		if (ev->random) schedule_random(e, c);
	} else {
		if (ev->random) schedule_random(e, c);
		else retry(e, m.ev, c);
	}
}

/* ------------------------------------------------------------- public */
static const Event *g_sort_ev;
static int byid_cmp(const void *a, const void *b)
{
	long x = g_sort_ev[*(const int *)a].id, y = g_sort_ev[*(const int *)b].id;
	return x < y ? -1 : x > y;
}

Events *events_load(World *w, Pak *text, long today, unsigned long seed, char *err, int errlen)
{
	Events *e = calloc(1, sizeof *e);
	int i;
	if (!e) { snprintf(err, errlen, "out of memory"); return NULL; }
	e->w = w; e->today = today; e->last_random_pick = -1; e->kinds = 3;
	ev_srand(seed);
	if (w->has_events && load_file(e, text, "db/events.txt", 0, err, errlen)) { events_free(e); return NULL; }   /* only when the scenario includes it (0x58f8a0) */
	e->cs_day = -1;
	e->byid = malloc(sizeof(int) * (e->nev + 1));
	for (i = 0; i < e->nev; i++) e->byid[i] = i;
	g_sort_ev = e->ev;
	qsort(e->byid, e->nev, sizeof(int), byid_cmp);
	e->pool = malloc(sizeof(int) * (e->nev + 1));
	for (i = 0; i < e->nev; i++) {
		Event *ev = &e->ev[i];
		if (ev->random) { e->pool[e->npool++] = i; continue; }
		/* §4.1: offset once, expiry, schedule (events without a date never schedule themselves) */
		if (!ev->date) continue;
		if (ev->offset > 0) ev->date += ev_rand() % ev->offset;
		ev->expiry = ev->deathdate > ev->date ? ev->deathdate : ev->date + ev->offset;
		if (today >= ev->expiry) continue;
		if (ev->country >= 0) push(e, ev->date, i, ev->country);
		else if (ev->province > 0) push(e, ev->date, i, -1);
		else { int c; for (c = 0; c < w->ncountry; c++) if (exists(w, c)) push(e, ev->date, i, c); }
		e->st.dated++;
	}
	e->st.events = e->nev; e->st.random = e->npool;
	for (i = 0; i < w->ncountry; i++) if (exists(w, i)) schedule_random(e, i);   /* §5: one chain per country */
	return e;
}

void events_free(Events *e)
{
	char *p;
	if (!e) return;
	p = e->strs;
	while (p) { char *n = *(char **)p; free(p); p = n; }
	free(e->ev); free(e->tn); free(e->cmd); free(e->pool); free(e->heap); free(e->byid); free(e);
}

void events_day(Events *e, long today)
{
	e->today = today;
	while (e->nheap && e->heap[0].day <= today) handle(e, pop(e));
}

void events_set_log(Events *e, EventLogFn fn, void *user) { e->log = fn; e->loguser = user; }
void events_set_choice(Events *e, EventChoiceFn fn, void *user) { e->choice = fn; e->chuser = user; }
void events_stats(const Events *e, EventStats *s) { *s = e->st; }
static int index_of_id(const Events *e, long id);
int events_action_effects(const Events *e, long id, int action, EventEffect *out, int max)
{
	int k = index_of_id(e, id), i, n = 0;
	const Event *ev;
	if (k < 0 || action < 0 || action > 3) return 0;
	ev = &e->ev[k];
	for (i = 0; i < ev->nact[action] && n < max; i++) {
		const Cmd *c = &e->cmd[ev->act[action] + i];
		unsigned j;
		out[n].kw = NULL;
		for (j = 0; j < sizeof ckeys / sizeof ckeys[0]; j++) if (ckeys[j].c == c->code) { out[n].kw = ckeys[j].k; break; }
		if (!out[n].kw) continue;
		out[n].which = c->which; out[n].value = c->value; out[n].str = c->str;
		n++;
	}
	return n;
}
const char *events_name_of(const Events *e, long id) { int k = index_of_id(e, id); return k >= 0 ? e->ev[k].name : NULL; }
void events_set_kinds(Events *e, int kinds) { e->kinds = kinds; }
void events_set_units(Events *e, EventUnitFn fn, void *user) { e->unitfn = fn; e->unituser = user; }
void events_set_cmds(Events *e, EventCmdFn fn, void *user) { e->cmdfn = fn; e->cmduser = user; }

static long sw(long v, int a, int b) { return v == a ? b : v == b ? a : v; }
void events_swap_countries(Events *e, int a, int b)
{
	int i;
	for (i = 0; i < e->nev; i++) e->ev[i].country = (short)sw(e->ev[i].country, a, b);
	for (i = 0; i < e->ncmd; i++) {
		Cmd *k = &e->cmd[i];
		switch (k->code) {
		case C_RELATION: case C_CASUSBELLI: case C_COUNTRY: case C_WAR: case C_INDEPENDENCE: case C_SECEDE:
		case C_DYNASTIC: case C_VASSAL: case C_BREAKDYN: case C_BREAKVASSAL: case C_ALLIANCE: case C_INHERIT:
			if (k->which >= 0) k->which = sw(k->which, a, b);
			break;
		default: break;
		}
	}
	for (i = 0; i < e->ntn; i++) {
		TNode *t = &e->tn[i];
		switch (t->op) {
		case T_OWNED: case T_CONTROL: case T_CORE: if (t->b > 0) t->b = sw(t->b, a, b); break;
		case T_EXISTS: case T_NEIGHBOUR: case T_RELATION: if (t->a >= 0) t->a = sw(t->a, a, b); break;
		case T_WAR: case T_ALLIANCE: case T_DYNASTIC: case T_VASSAL:
			if (t->a >= 0) t->a = sw(t->a, a, b);
			if (t->b >= 0) t->b = sw(t->b, a, b);
			break;
		default: break;
		}
	}
	for (i = 0; i < e->nheap; i++) e->heap[i].country = (short)sw(e->heap[i].country, a, b);
}

/* ------------------------------------------------------------- save game */
static int index_of_id(const Events *e, long id)
{
	int lo = 0, hi = e->nev - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		long v = e->ev[e->byid[mid]].id;
		if (v == id) return e->byid[mid];
		if (v < id) lo = mid + 1; else hi = mid - 1;
	}
	return -1;
}

void events_save(const Events *e, FILE *f)
{
	int i, n = 0;
	/* 32-bit values as two 16-bit halves: a long is 32 bits on the 68k and the parser reads signed */
	fprintf(f, "amieu2_events = {\n\tflags = { %ld %ld } lastpick = %ld seed = { %ld %ld }\n\thappened = {", (long)(e->flags >> 16),
	        (long)(e->flags & 0xFFFF), e->last_random_pick, (long)(g_seed >> 16), (long)(g_seed & 0xFFFF));
	for (i = 0; i < e->nev; i++) if (e->ev[i].happened) fprintf(f, "%s%ld", n++ % 16 ? " " : "\n\t\t", e->ev[i].id);
	fprintf(f, " }\n\tqueue = {");                      /* day, event id (-1 = random chain), country tag */
	for (i = 0; i < e->nheap; i++) {
		const Msg *m = &e->heap[i];
		fprintf(f, "%s%ld %ld %s", i % 8 ? "  " : "\n\t\t", m->day, m->ev < 0 ? -1L : e->ev[m->ev].id,
		        m->country >= 0 ? e->w->country[m->country].tag : "---");
	}
	fprintf(f, " }\n}\n");
}

void events_restore(Events *e, const SNode *root)
{
	const SNode *blk = script_get(root, "amieu2_events"), *q;
	int i;
	if (!blk) return;
	{
		const SNode *h;
		long v[2]; int n = 0;
		for (h = script_get(blk, "flags"), h = h ? h->child : NULL; h && n < 2; h = h->next) if (!h->key && h->val) v[n++] = parse_int(h->val);
		if (n == 2) { e->flags = (unsigned long)v[0] << 16 | (unsigned long)v[1]; for (i = 0; i < 8; i++) if (e->flags >> i & 1) e->w->gflag[i] = 1; }
		n = 0;
		for (h = script_get(blk, "seed"), h = h ? h->child : NULL; h && n < 2; h = h->next) if (!h->key && h->val) v[n++] = parse_int(h->val);
		if (n == 2) g_seed = (unsigned long)v[0] << 16 | (unsigned long)v[1];
	}
	e->last_random_pick = script_int(blk, "lastpick", 0);
	for (i = 0; i < e->nev; i++) e->ev[i].happened = 0;
	for (q = script_get(blk, "happened"), q = q ? q->child : NULL; q; q = q->next)
		if (!q->key && q->val) { int k = index_of_id(e, parse_int(q->val)); if (k >= 0) e->ev[k].happened = 1; }
	e->nheap = 0;                                        /* the saved queue replaces the one built at load */
	for (q = script_get(blk, "queue"), q = q ? q->child : NULL; q; ) {
		const char *t[3];
		int got = 0;
		for (; q && got < 3; q = q->next) if (!q->key && q->val) t[got++] = q->val;
		if (got == 3) {
			long id = parse_int(t[1]);
			int k = id < 0 ? -1 : index_of_id(e, id);
			int c = t[2][0] == '-' ? -1 : world_country(e->w, t[2]);
			if (id < 0 || k >= 0) push(e, parse_int(t[0]), k, c);
		}
	}
}
