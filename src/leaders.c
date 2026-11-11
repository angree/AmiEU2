/* leaders.c - see leaders.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaders.h"
#include "script.h"
#include "military.h"
#include "calendar.h"

#define MAX_LEAD 6000
struct Leaders {
	Leader l[MAX_LEAD];
	int n;
	short first[MAX_COUNTRY], count[MAX_COUNTRY];
	char *names; unsigned long nused, nsize;
};

static int month_of(const char *s)
{
	static const char *mn[12] = { "january", "february", "march", "april", "may", "june", "july",
	                              "august", "september", "october", "november", "december" };
	int i;
	if (!s) return 1;
	for (i = 0; i < 12; i++) if (script_ieq(s, mn[i])) return i + 1;
	i = (int)parse_int(s);
	return i >= 1 && i <= 12 ? i : 1;
}

static void rdate(const SNode *b, Date *d)
{
	memset(d, 0, sizeof *d);
	if (!b) return;
	d->year = (short)script_int(b, "year", 0);
	d->month = (unsigned char)month_of(script_str(b, "month", "january"));
	d->day = (unsigned char)script_int(b, "day", 1);
}

static long key(Date d) { return (long)d.year * 400 + d.month * 32 + d.day; }

Leaders *lead_load(const World *w, Pak *text)
{
	Leaders *L = (Leaders *)calloc(1, sizeof *L);
	int c;
	if (!L) return NULL;
	L->nsize = 160000; L->names = (char *)malloc(L->nsize);
	if (!L->names) { free(L); return NULL; }
	for (c = 0; c < w->ncountry; c++) {
		char path[48], tag[4], err[64];
		unsigned long sz;
		char *buf;
		Arena *a = NULL;
		SNode *root, *n;
		int i;
		L->first[c] = (short)L->n; L->count[c] = 0;
		for (i = 0; i < 3 && w->country[c].tag[i]; i++) tag[i] = (char)(w->country[c].tag[i] >= 'A' && w->country[c].tag[i] <= 'Z' ? w->country[c].tag[i] + 32 : w->country[c].tag[i]);
		tag[i] = 0;
		snprintf(path, sizeof path, "db/leaders/leaders.%s", tag);
		if (!(buf = (char *)pak_load(text, path, &sz))) continue;
		if ((a = arena_new(65536)) != NULL && (root = script_parse(buf, sz, a, err, sizeof err)) != NULL)
			for (n = root->child; n && L->n < MAX_LEAD; n = n->next) {
				Leader *x;
				const char *nm;
				size_t len;
				const char *cat;
				int ty;
				if (!n->key || !script_ieq(n->key, "historicalleader")) continue;
				cat = script_str(n, "category", "");
				ty = script_ieq(cat, "general") ? LT_GENERAL : script_ieq(cat, "admiral") ? LT_ADMIRAL : script_ieq(cat, "conquistador") ? LT_CONQ :
				     script_ieq(cat, "explorer") || script_ieq(cat, "privateer") ? LT_EXPLORER : -1;
				if (ty < 0) continue;                        /* monarchs: monarchs.c */
				x = &L->l[L->n];
				memset(x, 0, sizeof *x);
				x->type = (unsigned char)ty; x->location = (short)script_int(n, "location", 0);
				nm = script_str(n, "name", ""); len = strlen(nm) + 1;
				if (L->nused + len <= L->nsize) { memcpy(L->names + L->nused, nm, len); x->name = L->names + L->nused; L->nused += len; } else x->name = "";
				rdate(script_get(n, "startdate"), &x->start); rdate(script_get(n, "deathdate"), &x->death);
				x->rank = (unsigned char)script_int(n, "rank", 3); x->move = (unsigned char)script_int(n, "movement", 0);
				x->fire = (unsigned char)script_int(n, "fire", 0); x->shock = (unsigned char)script_int(n, "shock", 0);
				x->siege = (unsigned char)script_int(n, "siege", 0); x->country = (short)c;
				x->dormant = (unsigned char)script_ieq(script_str(n, "dormant", "no"), "yes");
				x->id = script_int(script_get(n, "id"), "id", 0);
				L->n++; L->count[c]++;
			}
		if (a) arena_free(a);
		free(buf);
	}
	return L;
}

void lead_free(Leaders *l) { if (l) { free(l->names); free(l); } }
static long lstart(const World *w, const Leader *x, long now);
/* events' `leader = id` trigger: the leader exists, is not dormant, is within his dates and did not fall */
int lead_active(const Leaders *L, const World *w, long id)
{
	long now;
	int i;
	if (!L) return 0;
	now = key(w->now);
	for (i = 0; i < L->n; i++) if (L->l[i].id == id) {
		const Leader *x = &L->l[i];
		long st = lstart(w, x, now);
		if (st < 0 || st > now || (x->death.year && key(x->death) <= now)) return 0;
		return !(i < 6000 && (w->lead_killed[i >> 3] >> (i & 7) & 1));
	}
	return 0;
}

void lead_swap(Leaders *L, int a, int b)
{
	short t;
	int i;
	if (!L || a < 0 || b < 0 || a >= MAX_COUNTRY || b >= MAX_COUNTRY) return;
	t = L->first[a]; L->first[a] = L->first[b]; L->first[b] = t;
	t = L->count[a]; L->count[a] = L->count[b]; L->count[b] = t;
	for (i = 0; i < L->n; i++) L->l[i].country = (short)(L->l[i].country == a ? b : L->l[i].country == b ? a : L->l[i].country);
}

/* the leader's start (date key) with the events' wakeleader / sleepleader (World.leadev, SPEC_EVENTS 9 codes
 * 0x20 / 0x43): -1 = dormant now; a woken leader starts on the day he was woken */
static long lstart(const World *w, const Leader *x, long now)
{
	int k, dorm = x->dormant;
	long st = key(x->start);
	for (k = 0; k < w->nleadev; k++) if (w->leadev_id[k] == x->id && x->id) {
		int y, mo, d;
		long kk;
		cal_date(w->leadev_day[k], &y, &mo, &d);
		kk = (long)y * 400 + mo * 32 + d;
		if (kk > now) continue;
		if (w->leadev_kind[k]) { dorm = 0; if (st > kk) st = kk; } else dorm = 1;
	}
	return dorm ? -1 : st;
}
const Leader *lead_get(const Leaders *l, int id) { return l && id >= 0 && id < l->n ? &l->l[id] : NULL; }

void lead_assign(const Leaders *L, World *w)
{
	static short gen[64], arm[64];
	int c, i, j;
	long now = key(w->now);
	if (!L) return;
	for (i = 0; i < w->nland; i++) {                  /* generals are dealt anew; a conquistador or explorer stays with his unit while he lives */
		LandUnit *u = &w->land[i];
		if (u->l_id == LEAD_RANDOM && !u->dead) continue;   /* an event's conquistador / explorer (lifetime not kept: NIEPEWNE) */
		if (u->l_id > 0 && u->l_type != LT_GENERAL && u->l_id <= L->n && !u->dead) {
			const Leader *x = &L->l[u->l_id - 1];
			if (!(x->death.year && key(x->death) <= now)) continue;
		}
		u->l_id = 0; u->l_type = 0; u->l_rank = u->l_move = u->l_fire = u->l_shock = u->l_siege = 0;
	}
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *f = &w->naval[i];
		if (f->l_id == LEAD_RANDOM && f->warships + f->galleys + f->transports > 0) continue;
		if (f->l_id > 0 && f->l_id <= L->n && f->l_type != LT_ADMIRAL && f->warships + f->galleys + f->transports > 0) {
			const Leader *x = &L->l[f->l_id - 1];
			if (!(x->death.year && key(x->death) <= now)) continue;
		}
		f->l_id = 0; f->l_move = f->l_type = f->l_fire = f->l_shock = 0;
	}
	for (c = 0; c < w->ncountry; c++) {
		int ng = 0, na = 0;
		if (!L->count[c] || w->country[c].nprov_owned <= 0) continue;
		for (i = L->first[c]; i < L->first[c] + L->count[c] && ng < 64; i++) {
			const Leader *x = &L->l[i];
			if (x->type != LT_GENERAL || lstart(w, x, now) < 0 || lstart(w, x, now) > now || (x->death.year && key(x->death) <= now)) continue;
			if (i < 6000 && (w->lead_killed[i >> 3] >> (i & 7) & 1)) continue;   /* fell in battle (KILL_LEADER) */
			for (j = ng; j > 0 && (L->l[gen[j - 1]].rank > x->rank || (L->l[gen[j - 1]].rank == x->rank && L->l[gen[j - 1]].fire + L->l[gen[j - 1]].shock < x->fire + x->shock)); j--) gen[j] = gen[j - 1];
			gen[j] = (short)i; ng++;
		}
		if (!ng) continue;
		for (i = 0; i < w->nland && na < 64; i++) {
			const LandUnit *u = &w->land[i];
			long men;
			if (u->owner != c || u->dead || u->l_id) continue;   /* (a conquistador's army keeps him) */
			men = u->infantry + u->cavalry + 100 * u->artillery;
			for (j = na; j > 0 && w->land[arm[j - 1]].infantry + w->land[arm[j - 1]].cavalry + 100 * w->land[arm[j - 1]].artillery < men; j--) arm[j] = arm[j - 1];
			arm[j] = (short)i; na++;
		}
		for (i = 0; i < ng && i < na; i++) {
			LandUnit *u = &w->land[arm[i]];
			const Leader *x = &L->l[gen[i]];
			u->l_id = (short)(gen[i] + 1); u->l_type = LT_GENERAL; u->l_rank = x->rank; u->l_move = x->move; u->l_fire = x->fire; u->l_shock = x->shock; u->l_siege = x->siege;
		}
	}
	for (c = 0; c < w->ncountry; c++) {                  /* admirals onto the largest fleets, the same way */
		int ng = 0, na = 0;
		if (!L->count[c] || w->country[c].nprov_owned <= 0) continue;
		for (i = L->first[c]; i < L->first[c] + L->count[c] && ng < 64; i++) {
			const Leader *x = &L->l[i];
			if (x->type != LT_ADMIRAL || lstart(w, x, now) < 0 || lstart(w, x, now) > now || (x->death.year && key(x->death) <= now)) continue;
			for (j = ng; j > 0 && (L->l[gen[j - 1]].rank > x->rank || (L->l[gen[j - 1]].rank == x->rank && L->l[gen[j - 1]].fire + L->l[gen[j - 1]].shock < x->fire + x->shock)); j--) gen[j] = gen[j - 1];
			gen[j] = (short)i; ng++;
		}
		if (!ng) continue;
		for (i = 0; i < w->nnaval && na < 64; i++) {
			const NavalUnit *f = &w->naval[i];
			int sz = 3 * f->warships + f->galleys + f->transports;
			if (f->owner != c || sz <= 0 || f->l_id) continue;
			for (j = na; j > 0 && 3 * w->naval[arm[j - 1]].warships + w->naval[arm[j - 1]].galleys + w->naval[arm[j - 1]].transports < sz; j--) arm[j] = arm[j - 1];
			arm[j] = (short)i; na++;
		}
		for (i = 0; i < ng && i < na; i++) {
			NavalUnit *f = &w->naval[arm[i]];
			const Leader *x = &L->l[gen[i]];
			f->l_id = (short)(gen[i] + 1); f->l_type = LT_ADMIRAL; f->l_move = x->move; f->l_fire = x->fire; f->l_shock = x->shock;
		}
	}
}

/* the place (SPEC_EXPLORERS 2.2, simplified): the leader's location when the country controls it and it
 * has a city, else the capital */
static int place_of(const World *w, const Leader *x)
{
	int c = x->country, p = x->location;
	if (p > 0 && p < w->nprov && w->prov[p].controller == c && w->prov[p].population > 0) return p;
	p = w->country[c].capital;
	return p > 0 && p < w->nprov ? p : -1;
}

void lead_spawn(const Leaders *L, World *w, struct Military *m, int newgame)
{
	long now, prev;
	int i;
	Date pd;
	if (!L || !m) return;
	now = key(w->now);
	pd = w->now;
	if (pd.month > 1) pd.month--; else { pd.month = 12; pd.year--; }
	prev = key(pd);
	for (i = 0; i < L->n; i++) {
		const Leader *x = &L->l[i];
		long st;
		int c = x->country, p, k = -1;
		if (x->type != LT_CONQ && x->type != LT_EXPLORER) continue;
		st = lstart(w, x, now);
		if (st < 0 || st > now || (x->death.year && key(x->death) <= now)) continue;
		if (!newgame && st <= prev) continue;
		if (c < 0 || c >= w->ncountry || w->country[c].nprov_owned <= 0 || (p = place_of(w, x)) < 0) continue;
		if (x->type == LT_EXPLORER) {                 /* a fleet of 3 in a port: here, else any own one */
			int q = mil_is_port(w, p) && w->prov[p].controller == c ? p : -1;
			for (k = 1; q < 0 && k < w->nprov; k++) if (w->prov[k].owner == c && w->prov[k].controller == c && mil_is_port(w, k)) q = k;
			k = -1;
			if (q > 0 && (k = mil_new_fleet(m, c, q, 3, x->name)) >= 0) {
				NavalUnit *f = &w->naval[k];
				f->l_id = (short)(i + 1); f->l_move = x->move; f->l_type = x->type;
				continue;
			}
		}
		if ((k = mil_new_army(m, c, p, 1000, x->name)) >= 0) {   /* a conquistador (or an explorer without a port) */
			LandUnit *u = &w->land[k];
			u->l_id = (short)(i + 1); u->l_type = x->type; u->l_rank = x->rank; u->l_move = x->move;
			u->l_fire = x->fire; u->l_shock = x->shock; u->l_siege = x->siege;
		}
	}
}
