/* missions.c - see missions.h */
#include <stdlib.h>
#include <string.h>
#include "missions.h"
#include "text.h"

#define MAX_ROWS 700
typedef struct {
	short offer[MIS_TYPES];                /* row per type, -1 none */
	MisCur cur[MIS_TYPES]; int ncur;
	int ndone;                             /* missions won (MISSION victory) */
} MisState;
struct Missions {
	MisRow r[MAX_ROWS]; int n;
	MisState *st;                          /* per country (SPEC_MISSIONS 1.3: every tracked country gets offers) */
	int player;
	unsigned long seed;
};
#define ME(m) (&(m)->st[(m)->player >= 0 ? (m)->player : 0])

static const int g_vp[MIS_TYPES] = { 30, 20, 20, 30, 15, 50, 5, 5, 5, 10, 2 };
int mis_done(const Missions *m) { return m && m->st ? ME(m)->ndone : 0; }
int mis_vp(int code) { return code >= 0 && code < MIS_TYPES ? g_vp[code] : 0; }
static int rnd(Missions *m, int n) { m->seed = (m->seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return n > 0 ? (int)((m->seed >> 16) % (unsigned long)n) : 0; }

Missions *mis_load(const World *w, Pak *text)
{
	Missions *m = (Missions *)calloc(1, sizeof *m);
	unsigned long sz;
	if (m && !(m->st = (MisState *)calloc(MAX_COUNTRY, sizeof *m->st))) { free(m); return NULL; }
	char *buf, *cur, *line, *f[10];
	if (!m) return NULL;
	mis_reset(m);
	if (!(buf = (char *)pak_load(text, "db/missions.csv", &sz))) return m;
	cur = buf;
	while ((line = csv_line(&cur)) != NULL && m->n < MAX_ROWS) {
		int k = csv_split(line, f, 10), code;
		MisRow *r = &m->r[m->n];
		if (k < 5 || !f[1][0] || f[1][0] < '0' || f[1][0] > '9') continue;
		code = (int)parse_int(f[1]);
		if (code < 0 || code >= MIS_TYPES) continue;
		r->tag = world_country(w, f[0]); r->code = (short)code;
		r->start = (short)parse_int(f[2]); r->end = (short)parse_int(f[3]);
		if (code == 2 || code == 3 || code == 5 || code == 9 || code == 10) r->x = world_country(w, f[4]);
		else r->x = (short)parse_int(f[4]);
		r->area = k > 5 && f[5][0] ? world_intern((World *)w, f[5]) : -1;   /* the area names are interned by the provinces already */
		if (r->tag < 0 || r->x < 0) continue;
		m->n++;
	}
	free(buf);
	return m;
}

void mis_free(Missions *m) { if (m) { free(m->st); free(m); } }
void mis_reset(Missions *m)
{
	int c, i;
	for (c = 0; c < MAX_COUNTRY && m->st; c++) { for (i = 0; i < MIS_TYPES; i++) m->st[c].offer[i] = -1; m->st[c].ncur = 0; m->st[c].ndone = 0; }
	m->seed = 31337; m->player = -1;
}
const MisRow *mis_row(const Missions *m, int row) { return m && row >= 0 && row < m->n ? &m->r[row] : NULL; }

static int area_free_of(const World *w, int x, int area)
{
	int p;
	for (p = 1; p < w->nprov; p++) if (w->prov[p].owner == x && w->prov[p].area == area) return 0;
	return 1;
}

/* 1 done, -1 impossible, 0 open (deadline judgements: at == 1) */
static int judge(const World *w, const MisRow *r, int c, int at)
{
	const Province *p = r->x > 0 && r->x < w->nprov ? &w->prov[r->x] : NULL;
	switch (r->code) {
	case 0: case 1: if (!p) return -1; return at ? (p->owner == c && p->controller == c ? 1 : -1) : 0;
	case 2: if (w->country[r->x].nprov_owned <= 0) return -1; return world_overlord(w, r->x) == c ? 1 : 0;
	case 3: return w->country[r->x].nprov_owned <= 0 ? 1 : 0;
	case 4: if (!p || p->population <= 0) return -1; return (p->bflags & 0x01) ? 1 : 0;   /* a shipyard there (PB_SHIPYARD); no settlement fails */
	case 5: return at ? (area_free_of(w, r->x, r->area) ? 1 : -1) : 0;
	case 6: if (!p) return -1; return WORLD_KNOWN(w, c, r->x) ? 1 : 0;
	case 7: if (!p) return -1; if (p->owner == c && !p->tp) return 1; return p->owner >= 0 && p->owner != c ? -1 : 0;
	case 8: if (!p) return -1; if (p->owner == c && p->tp) return 1; return p->owner >= 0 && p->owner != c ? -1 : 0;
	case 9: if (w->country[r->x].nprov_owned <= 0) return -1; return world_allied(w, c, r->x, ALLY_MILITARY) ? 1 : 0;
	case 10: if (w->country[r->x].nprov_owned <= 0) return -1; return at ? (world_allied(w, c, r->x, ALLY_DYNASTIC) ? 1 : -1) : 0;
	default: return -1;
	}
}

static int already_ok(const World *w, const MisRow *r, int c)              /* offered only if not done before it starts */
{
	const Province *p = r->x > 0 && r->x < w->nprov ? &w->prov[r->x] : NULL;
	if ((r->code == 0) && p && p->owner == c) return 1;
	if ((r->code == 5) && area_free_of(w, r->x, r->area)) return 1;
	if (r->code == 10 && world_allied(w, c, r->x, ALLY_DYNASTIC)) return 1;
	return judge(w, r, c, 0) != 0;
}

static void month_one(Missions *m, World *w, int c, long today)
{
	MisState *S = &m->st[c];
	int t, i;
	for (i = 0; i < S->ncur; ) {                    /* judgements */
		const MisRow *r = &m->r[S->cur[i].row];
		int res = judge(w, r, c, today >= S->cur[i].deadline);
		if (!res && today >= S->cur[i].deadline) res = -1;
		if (!res) { i++; continue; }
		world_vp(w, c, VP_MISS, res > 0 ? 1000L * g_vp[r->code] : -(1000L * g_vp[r->code] * 66 / 100));   /* SPEC_MISSIONS 2.1: BASE VP off adds nothing */
		if (res > 0) S->ndone++;
		world_note(w, res > 0 ? NOTE_MISSION_DONE : NOTE_MISSION_LOST, c, S->cur[i].row, res > 0 ? g_vp[r->code] : g_vp[r->code] * 66 / 100);
		S->cur[i] = S->cur[--S->ncur];
	}
	for (t = 0; t < MIS_TYPES; t++) {               /* offers: one per type, replaced when impossible or out of date */
		int busy = 0, k, cand = 0, pick;
		if (S->offer[t] >= 0) {
			const MisRow *r = &m->r[S->offer[t]];
			if (w->now.year > r->end || judge(w, r, c, 0) != 0) S->offer[t] = -1;
		}
		for (k = 0; k < S->ncur; k++) if (m->r[S->cur[k].row].code == t) busy = 1;
		if (busy || S->offer[t] >= 0 || w->opt_nomis) continue;   /* no new offers with DYNAMIC MISSIONS off (SPEC_MISSIONS) */
		for (k = 0; k < m->n; k++) { const MisRow *r = &m->r[k]; if (r->tag == c && r->code == t && r->start <= w->now.year && w->now.year <= r->end && !already_ok(w, r, c)) cand++; }
		if (!cand) continue;
		pick = rnd(m, cand);
		for (k = 0; k < m->n; k++) {
			const MisRow *r = &m->r[k];
			if (r->tag == c && r->code == t && r->start <= w->now.year && w->now.year <= r->end && !already_ok(w, r, c) && pick-- == 0) { S->offer[t] = (short)k; break; }
		}
	}
	if (c != m->player && !w->country[c].human) {    /* 0x451300: the AI accepts its best offer itself (score: the VP, NIEPEWNE; one a month) */
		int best = -1, bv = 0;
		for (t = 0; t < MIS_TYPES; t++) if (S->offer[t] >= 0 && g_vp[t] > bv) { bv = g_vp[t]; best = S->offer[t]; }
		if (best >= 0 && S->ncur < MIS_TYPES) {
			S->cur[S->ncur].row = (short)best; S->cur[S->ncur].deadline = today + 5L * 360; S->ncur++;
			S->offer[m->r[best].code] = -1;
		}
	}
}

/* every tracked country (SPEC_MISSIONS 1.3: country+0x11e; ours: the player and the scenario header's featured countries, NIEPEWNE) */
void mis_month(Missions *m, World *w, int c, long today)
{
	int k;
	if (!m || !m->st) return;
	m->player = c;
	for (k = 0; k < w->ncountry && k < MAX_COUNTRY; k++)
		if ((k == c && !w->tutorial) || ((w->country[k].tracked || w->country[k].major) && w->country[k].nprov_owned > 0 && !w->country[k].human)) month_one(m, w, k, today);   /* 0x451680: no missions for the player in a tutorial */
}

int mis_offers(const Missions *m, short *rows, int max) { int t, n = 0; for (t = 0; t < MIS_TYPES && n < max; t++) if (ME(m)->offer[t] >= 0) rows[n++] = ME(m)->offer[t]; return n; }
int mis_current(const Missions *m, MisCur *cur, int max) { int i; for (i = 0; i < ME(m)->ncur && i < max; i++) cur[i] = ME(m)->cur[i]; return i; }

int mis_accept(Missions *m, int row, long today)
{
	int t;
	MisState *S = ME(m);
	if (row < 0 || row >= m->n || S->ncur >= MIS_TYPES) return -1;
	t = m->r[row].code;
	if (S->offer[t] != row) return -1;
	S->cur[S->ncur].row = (short)row; S->cur[S->ncur].deadline = today + 5L * 360; S->ncur++;
	S->offer[t] = -1;
	return 0;
}

void mis_title(const Missions *m, const World *w, int row, char *out, int n, int desc)
{
	static const char letter[MIS_TYPES] = { 'F', 'G', 'B', 'C', 'H', 'A', 'I', 'J', 'K', 'D', 'E' };
	const MisRow *r = mis_row(m, row);
	char key[24];
	const char *a1 = "", *a2 = "";
	if (!r) { out[0] = 0; return; }
	snprintf(key, sizeof key, "MISSION_%s_%c", desc ? "DESC" : "TITLE", letter[r->code]);
	if (r->code == 2 || r->code == 3 || r->code == 9 || r->code == 10 || r->code == 5) a1 = text_get(w->country[r->x].tag);
	else a1 = r->x > 0 && r->x < w->nprov && w->prov[r->x].name ? w->prov[r->x].name : "?";
	if (r->code == 5) a2 = r->area >= 0 && world_name(w, r->area) ? world_name(w, r->area) : "?";
	else if (r->code == 0 && r->x > 0 && r->x < w->nprov && w->prov[r->x].owner >= 0) a2 = text_get(w->country[w->prov[r->x].owner].tag);
	snprintf(out, (size_t)n, text_get(key), a1, a2);
}

void mis_save(const Missions *m, const World *w, FILE *f)
{
	int t, i, c;
	fprintf(f, "amieu2_missions = { seed = %lu player = %d\n", m->seed, m->player);
	for (c = 0; c < w->ncountry && c < MAX_COUNTRY; c++) {
		const MisState *S = &m->st[c];
		int any = S->ncur > 0 || S->ndone > 0;
		for (t = 0; t < MIS_TYPES && !any; t++) if (S->offer[t] >= 0) any = 1;
		if (!any) continue;
		fprintf(f, "\tc = { tag = %s done = %d offers = {", w->country[c].tag, S->ndone);
		for (t = 0; t < MIS_TYPES; t++) fprintf(f, " %d", S->offer[t]);
		fprintf(f, " } current = {");
		for (i = 0; i < S->ncur; i++) fprintf(f, " %d %ld", S->cur[i].row, S->cur[i].deadline);
		fprintf(f, " } }\n");
	}
	fprintf(f, "}\n");
}

void mis_restore(Missions *m, const World *w, const SNode *root)
{
	const SNode *b = script_get(root, "amieu2_missions"), *q, *l;
	long v[2 * MIS_TYPES];
	int n = 0, t = 0;
	(void)w;
	const SNode *cn;
	if (!m || !b) return;
	mis_reset(m);
	m->seed = (unsigned long)script_int(b, "seed", 31337);
	m->player = (int)script_int(b, "player", -1);
	for (cn = b->child; cn; cn = cn->next) {        /* one block per country (older saves: the player's at the top level) */
		const SNode *blk = cn->key && script_ieq(cn->key, "c") ? cn : NULL;
		int c = blk ? world_country(w, script_str(blk, "tag", "")) : -1;
		MisState *S;
		if (c < 0 || c >= MAX_COUNTRY) continue;
		S = &m->st[c]; n = 0; t = 0;
		S->ndone = (int)script_int(blk, "done", 0);
		for (l = script_get(blk, "offers"), q = l ? l->child : NULL; q && t < MIS_TYPES; q = q->next) if (!q->key && q->val) { int r = (int)parse_int(q->val); S->offer[t++] = (short)(r < m->n ? r : -1); }
		for (l = script_get(blk, "current"), q = l ? l->child : NULL; q && n < 2 * MIS_TYPES; q = q->next) if (!q->key && q->val) v[n++] = parse_int(q->val);
		for (t = 0; t + 1 < n; t += 2) if (v[t] >= 0 && v[t] < m->n) { S->cur[S->ncur].row = (short)v[t]; S->cur[S->ncur].deadline = v[t + 1]; S->ncur++; }
	}
}
