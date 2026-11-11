/* monarchs.c - see monarchs.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monarchs.h"
#include "script.h"
#include "calendar.h"

#define MAX_MON 4000
struct Monarchs {
	Monarch m[MAX_MON];
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

static const char *keep(Monarchs *m, const char *s)
{
	size_t l = strlen(s) + 1;
	char *p;
	if (m->nused + l > m->nsize) return "";
	p = m->names + m->nused;
	memcpy(p, s, l);
	m->nused += l;
	return p;
}

static long date_key(Date d) { return (long)d.year * 400 + d.month * 32 + d.day; }

Monarchs *mon_load(const World *w, Pak *text)
{
	Monarchs *m = (Monarchs *)calloc(1, sizeof *m);
	int c;
	if (!m) return NULL;
	m->nsize = 120000; m->names = (char *)malloc(m->nsize);
	if (!m->names) { free(m); return NULL; }
	for (c = 0; c < w->ncountry; c++) {
		char path[48], tag[4], err[64];
		unsigned long sz;
		char *buf;
		Arena *a;
		SNode *root, *n;
		int i;
		m->first[c] = (short)m->n; m->count[c] = 0;
		for (i = 0; i < 3 && w->country[c].tag[i]; i++) tag[i] = (char)(w->country[c].tag[i] >= 'A' && w->country[c].tag[i] <= 'Z' ? w->country[c].tag[i] + 32 : w->country[c].tag[i]);
		tag[i] = 0;
		snprintf(path, sizeof path, "db/monarchs/monarchs.%s", tag);
		if (!(buf = (char *)pak_load(text, path, &sz))) continue;
		if ((a = arena_new(65536)) != NULL && (root = script_parse(buf, sz, a, err, sizeof err)) != NULL) {
			for (n = root->child; n && m->n < MAX_MON; n = n->next) {
				const SNode *sd;
				Monarch *x;
				if (!n->key || !script_ieq(n->key, "historicalmonarch")) continue;
				x = &m->m[m->n];
				memset(x, 0, sizeof *x);
				x->name = keep(m, script_str(n, "name", ""));
				if ((sd = script_get(n, "startdate")) != NULL) {
					x->start.year = (short)script_int(sd, "year", 0);
					x->start.month = (unsigned char)month_of(script_str(sd, "month", "january"));
					x->start.day = (unsigned char)script_int(sd, "day", 1);
				}
				x->adm = (unsigned char)script_int(n, "ADM", 3); x->dip = (unsigned char)script_int(n, "DIP", 3); x->mil = (unsigned char)script_int(n, "MIL", 3);
				x->dormant = (unsigned char)script_ieq(script_str(n, "dormant", "no"), "yes");
				x->id = script_int(script_get(n, "id"), "id", 0);
				m->n++; m->count[c]++;
			}
			{	/* in order of the start date (insertion sort: a few dozen per country) */
				int lo = m->first[c], hi = lo + m->count[c], j, k;
				for (j = lo + 1; j < hi; j++) {
					Monarch t = m->m[j];
					for (k = j; k > lo && date_key(m->m[k - 1].start) > date_key(t.start); k--) m->m[k] = m->m[k - 1];
					m->m[k] = t;
				}
			}
		}
		if (a) arena_free(a);
		free(buf);
	}
	return m;
}

void mon_free(Monarchs *m) { if (m) { free(m->names); free(m); } }
void mon_swap(Monarchs *m, int a, int b)
{
	short t;
	if (!m || a < 0 || b < 0 || a >= MAX_COUNTRY || b >= MAX_COUNTRY) return;
	t = m->first[a]; m->first[a] = m->first[b]; m->first[b] = t;
	t = m->count[a]; m->count[a] = m->count[b]; m->count[b] = t;
}

const Monarch *mon_current(const Monarchs *m, int c, Date now)
{
	const Monarch *best = NULL;
	int i;
	if (!m || c < 0 || c >= MAX_COUNTRY) return NULL;
	for (i = m->first[c]; i < m->first[c] + m->count[c]; i++)
		if (!m->m[i].dormant && date_key(m->m[i].start) <= date_key(now)) best = &m->m[i];
	return best;
}

const Monarch *mon_current_w(const Monarchs *m, const World *w, int c, Date now)
{
	const Monarch *best = NULL;
	long bk = -1, nowd = cal_day(now.year, now.month, now.day);
	int i, k, any = 0;
	if (!m || c < 0 || c >= MAX_COUNTRY) return NULL;
	for (k = 0; k < w->nmonev; k++) if (w->monev_c[k] == c) any = 1;
	if (!any) return mon_current(m, c, now);
	for (i = m->first[c]; i < m->first[c] + m->count[c]; i++) {
		const Monarch *x = &m->m[i];
		int dorm = x->dormant;
		long st = cal_day(x->start.year, x->start.month, x->start.day);
		for (k = 0; k < w->nmonev; k++) if (w->monev_c[k] == c && w->monev_id[k] == x->id && w->monev_day[k] <= nowd) {
			if (w->monev_kind[k]) { dorm = 0; st = w->monev_day[k]; } else dorm = 1;
		}
		if (dorm || st > nowd) continue;
		if (st >= bk) { bk = st; best = x; }
	}
	return best;
}

int mon_list(const Monarchs *m, int c, const Monarch **out, int max)
{
	int i, n = 0;
	if (!m || c < 0 || c >= MAX_COUNTRY) return 0;
	for (i = m->first[c]; i < m->first[c] + m->count[c] && n < max; i++) out[n++] = &m->m[i];
	return n;
}
