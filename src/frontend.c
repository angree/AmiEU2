/* frontend.c - see frontend.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend.h"
#include "script.h"

static int read_header(Pak *text, const char *path, FrontScen *s)
{
	unsigned long sz;
	char *buf = (char *)pak_load(text, path, &sz), err[80];
	Arena *a;
	SNode *root, *h, *sel, *t;
	if (!buf) return -1;
	if (!(a = arena_new(16384))) { free(buf); return -1; }
	if (!(root = script_parse(buf, sz, a, err, sizeof err)) || !(h = script_get(root, "header"))) { arena_free(a); free(buf); return -1; }
	snprintf(s->path, sizeof s->path, "%s", path);
	snprintf(s->name, sizeof s->name, "%s", script_str(h, "name", path));
	s->startyear = (int)script_int(h, "startyear", 0);
	s->nsel = 0;
	/* the header's own country blocks (FRA = { countrydifficulty = ... }) are the scenario's featured
	 * powers: they come first, then the rest of `selectable` */
	for (t = h->child; t && s->nsel < FRONT_MAXSEL; t = t->next)
		if (t->key && strlen(t->key) == 3 && t->child && script_get(t, "countrydifficulty")) {
			int k, j;
			for (k = 0; k < 3; k++) s->sel[s->nsel][k] = (char)(t->key[k] >= 'a' && t->key[k] <= 'z' ? t->key[k] - 32 : t->key[k]);
			s->sel[s->nsel][3] = 0;
			for (j = 0; j < s->nsel && strcmp(s->sel[j], s->sel[s->nsel]); j++) ;
			if (j == s->nsel) {
				if (s->nsel < FRONT_MAXFEAT) {
					FrontCountry *fc = &s->feat[s->nsel];
					const SNode *q;
					memset(fc, 0, sizeof *fc);
					for (q = t->child; q; q = q->next) {
						if (!q->key || !q->val) continue;
						if (script_ieq(q->key, "desc") || script_ieq(q->key, "scenariotactics")) snprintf(fc->hist, sizeof fc->hist, "%s", q->val);
						else if (script_ieq(q->key, "countrytactics")) snprintf(fc->strat, sizeof fc->strat, "%s", q->val);
						else if (script_ieq(q->key, "missiontactics")) snprintf(fc->mtact, sizeof fc->mtact, "%s", q->val);
						else if (script_ieq(q->key, "missioncountry")) snprintf(fc->mcountry, sizeof fc->mcountry, "%s", q->val);
						else if (script_ieq(q->key, "picture")) snprintf(fc->picture, sizeof fc->picture, "%s", q->val);
						else if (script_ieq(q->key, "countrydifficulty")) fc->diff = (signed char)parse_int(q->val);
						else if (script_ieq(q->key, "countryeconomy")) fc->eco = (signed char)parse_int(q->val);
						else if (script_ieq(q->key, "countrymilitary")) fc->mil = (signed char)parse_int(q->val);
						else if (script_ieq(q->key, "countrydiplomacy")) fc->dip = (signed char)parse_int(q->val);
					}
				}
				s->nsel++;
			}
		}
	s->nfeat = s->nsel;
	for (sel = script_get(h, "selectable"), t = sel ? sel->child : NULL; t && s->nsel < FRONT_MAXSEL; t = t->next)
		if (!t->key && t->val && strlen(t->val) == 3) {
			int j;
			for (j = 0; j < s->nsel && strcmp(s->sel[j], t->val); j++) ;
			if (j == s->nsel) { memcpy(s->sel[s->nsel], t->val, 4); s->nsel++; }
		}
	arena_free(a);
	free(buf);
	return 0;
}

int front_scenarios(Pak *text, FrontScen *out, int max)
{
	unsigned long i;
	int n = 0, a, b;
	for (i = 0; i < text->count && n < max; i++) {
		const char *nm = text->ent[i].name;
		size_t l = strlen(nm);
		if (strncmp(nm, "scenarios/", 10) || l < 5 || strcmp(nm + l - 4, ".eug") || strchr(nm + 10, '/')) continue;
		if (!read_header(text, nm, &out[n]) && out[n].nsel > 0) n++;
	}
	for (a = 1; a < n; a++)                              /* by start year (insertion sort, n is small) */
		for (b = a; b > 0 && out[b].startyear < out[b - 1].startyear; b--) {
			FrontScen t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
		}
	return n;
}
