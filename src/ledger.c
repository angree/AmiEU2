/* ledger.c - see ledger.h. Page numbers and columns follow docs/SPEC_LEDGER.md. */
#include <stdio.h>
#include <string.h>
#include "ledger.h"
#include "text.h"
#include "envoys.h"
#include "calendar.h"
#include "script.h"
#include "monarchs.h"
#include "leaders.h"
#include "missions.h"

static const Missions *g_lmis;
void ledger_set_missions(const struct Missions *m) { g_lmis = (const Missions *)m; }
static const Monarchs *g_lmon;
static const Leaders *g_llead;
void ledger_set_sources(const struct Monarchs *m, const struct Leaders *l) { g_lmon = (const Monarchs *)m; g_llead = (const Leaders *)l; }

static const char *mname[12] = { "January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December" };
static char g_log[40][120]; static int g_nlog, g_logpos;

void ledger_log(const World *w, const char *text)
{
	int m = w->now.month >= 1 && w->now.month <= 12 ? w->now.month - 1 : 0;
	snprintf(g_log[g_logpos], sizeof g_log[0], "%s %d: %s", mname[m], w->now.year, text);
	g_logpos = (g_logpos + 1) % 40;
	if (g_nlog < 40) g_nlog++;
}

int ledger_log_recent(const char **out, int max)
{
	int i, n = g_nlog < max ? g_nlog : max;
	for (i = 0; i < n; i++) out[i] = g_log[(g_logpos - n + i + 40) % 40];
	return n;
}

/* page ids per chapter (SPEC_LEDGER 2): Monarch 0-3, Economy 4-15, Diplomacy 16-19, Technology 20-21,
 * Military 22-28, Establishments 29-33, History 34 */
static const int ch_first[7] = { 0, 4, 16, 20, 22, 29, 34 }, ch_last[7] = { 3, 15, 19, 21, 28, 33, 34 };
int ledger_chapter(int page) { int c; for (c = 6; c > 0 && page < ch_first[c]; c--) ; return c; }
int ledger_first_page(int c) { return ch_first[c < 0 ? 0 : c > 6 ? 6 : c]; }
int ledger_last_page(int c) { return ch_last[c < 0 ? 0 : c > 6 ? 6 : c]; }

/* ------------------------------------------------------------------ history */
void ledger_hist_init(LedgerHist *h, const World *w, int player)
{
	int c;
	memset(h, 0, sizeof *h);
	if (player >= 0) h->track[h->ntrack++] = (short)player;
	while (h->ntrack < LG_TRACK) {                       /* then the largest countries (the scenarios mark few as major) */
		int best = -1, k;
		for (c = 0; c < w->ncountry; c++) {
			if (c == w->reb || w->country[c].nprov_owned <= 0) continue;
			for (k = 0; k < h->ntrack && h->track[k] != c; k++) ;
			if (k < h->ntrack) continue;
			if (best < 0 || w->country[c].nprov_owned > w->country[best].nprov_owned) best = c;
		}
		if (best < 0) break;
		h->track[h->ntrack++] = (short)best;
	}
}

static long series_value(int s, const World *w, const Economy *e, int c)
{
	const Country *co = &w->country[c];
	long v = 0;
	int i;
	switch (s) {
	case LS_VP: return co->vp;
	case LS_WEALTH:                                                      /* 0x4e74b0: the incomes - army / fleet maintenance - interest (the closed year) */
		for (i = 0; i < 12; i++) v += co->linc_m[i];
		(void)e;
		return (v - co->lexp_m[EXP_ARMYM] - co->lexp_m[EXP_FLEETM] - co->lexp_m[EXP_INTEREST]) / 1000;
	case LS_INFRA: return co->infra_tech;
	case LS_TRADE: return co->trade_tech;
	case LS_INFL: return co->inflation_u / 10000;                       /* percent */
	case LS_LAND: return co->land_tech;
	case LS_NAVAL: return co->naval_tech;
	case LS_ARMY:                                                        /* cav + 1000 art + inf, as the original adds them */
		for (i = 0; i < w->nland; i++) if (w->land[i].owner == c && w->land[i].dead != 1) v += w->land[i].infantry + w->land[i].cavalry + 1000 * w->land[i].artillery;
		return v;
	case LS_NAVY:
		for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == c) v += w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports;
		return v;
	case LS_TERR: return co->nprov_owned;
	default: return 0;
	}
}

void ledger_hist_sample(LedgerHist *h, const World *w, const Economy *e)
{
	int s, k;
	if (h->n >= LG_SAMPLES) return;
	for (s = 0; s < LS_N; s++) for (k = 0; k < h->ntrack; k++) {
		long v = series_value(s, w, e, h->track[k]);
		h->v[s][k][h->n] = v < 0 ? 0 : v > 1000000L ? 1000000L : v;   /* clamped 0..1,000,000 like the original */
	}
	if (h->ntrack > 0) {                                   /* 0x4e8210 / 0x4e8440: the player's categories, each its own series */
		const Country *pc = &w->country[h->track[0]];
		int j;
		for (j = 0; j < LG_NCAT; j++) {
			long v = j < 12 ? pc->linc_m[j] : pc->lexp_m[j - 12];   /* the year just closed (the exe records e+0x154 / +0x184 at a time NIEZNANE) */
			v /= 1000;
			h->cat[j][h->n] = v < 0 ? 0 : v > 1000000L ? 1000000L : v;
		}
	}
	h->year[h->n++] = w->now.year;
}

/* ------------------------------------------------------------------ tables */
static void col(UiLedger *L, const char *key, int w, int right) { if (L->ncol < LG_MAXCOL) { L->hdr[L->ncol] = text_get(key); L->w[L->ncol] = (short)w; L->right[L->ncol] = (unsigned char)right; L->ncol++; } }
static char *cell(UiLedger *L, int r, int c) { return L->cell[r][c]; }
static void money(char *s, long m) { long a = m < 0 ? -m : m; snprintf(s, 28, "%s%ld.%02ld $", m < 0 ? "-" : "", a / 1000, a % 1000 / 10); }
static void tenth(char *s, long m) { long a = m < 0 ? -m : m; snprintf(s, 28, "%s%ld.%ld", m < 0 ? "-" : "", a / 1000, a % 1000 / 100); }
static const char *yesno(int v) { return text_get(v ? "EYRYES" : "EYRNOPES"); }
static void dayfmt(char *s, long day) { int y, m, d; cal_date(day, &y, &m, &d); snprintf(s, 28, "%s %d", mname[m >= 1 && m <= 12 ? m - 1 : 0], y); }
static const char *pname(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].name ? w->prov[p].name : "-"; }

static void title(UiLedger *L, const char *key) { snprintf(L->title, sizeof L->title, "%s", text_get(key)); }

static void graph(UiLedger *L, const LedgerHist *h, int s)
{
	int k, i;
	L->kind = LG_GRAPH; L->nser = h->ntrack; L->npts = h->n;
	L->year0 = h->n ? h->year[0] : 0; L->year1 = h->n ? h->year[h->n - 1] : 0; L->maxv = 1;
	for (k = 0; k < h->ntrack; k++) {
		L->ser_country[k] = h->track[k]; L->ser[k] = h->v[s][k];
		for (i = 0; i < h->n; i++) if (h->v[s][k][i] > L->maxv) L->maxv = h->v[s][k][i];
	}
}

void ledger_fill(UiLedger *L, int page, const World *w, const Economy *e, const Military *m, const LedgerHist *h, int player)
{
	const Country *me = player >= 0 ? &w->country[player] : NULL;
	int i, k, r = 0;
	memset(L, 0, sizeof *L);
	L->page = page; L->chapter = ledger_chapter(page); L->kind = LG_TABLE; L->body_h = 300;
	if (!me) return;
	switch (page) {
	case 0: title(L, "EYRVP"); graph(L, h, LS_VP); break;
	case 1: title(L, "EYRHISTVP"); col(L, "EYRVPINPROGRESS", 300, 0); col(L, "EYRVPEARNED", 80, 1);   /* the current missions and their VP, then the completed count */
		if (g_lmis) {
			MisCur cur[8];
			int n = mis_current(g_lmis, cur, 8), j;
			for (j = 0; j < n && r < LG_MAXROW - 1; j++) {
				const MisRow *mr = mis_row(g_lmis, cur[j].row);
				mis_title(g_lmis, w, cur[j].row, cell(L, r, 0), 28, 0);
				snprintf(cell(L, r, 1), 28, "%d", mr ? mis_vp(mr->code) : 0);
				r++;
			}
			snprintf(cell(L, r, 0), 28, "%s", text_get("EYRVPCOMPLETED")); snprintf(cell(L, r, 1), 28, "%d", mis_done(g_lmis)); r++;
		}
		L->nrow = r; break;
	case 2: title(L, "EYRBASEVP"); L->body_h = 140; col(L, "EYRVPSOURCE", 200, 0); col(L, "EYRVPEARNED", 100, 1);
		{	static const char *const src[7] = { "BVPECO", "BVPPEACE", "BVPBATTLE", "BVPDIP", "BVPEST", "BVPDISC", "BVPMISS" };   /* 0x55f070 */
			int r;
			for (r = 0; r < 7; r++) {
				long v = me->vpc_m[r], a = v < 0 ? -v : v;
				snprintf(cell(L, r, 0), 28, "%s", text_get(src[r]));
				snprintf(cell(L, r, 1), 28, "%s%ld.%ld", v < 0 ? "-" : "", a / 1000, a % 1000 / 100);   /* "%.1f" */
			}
			snprintf(cell(L, 7, 0), 28, "%s", text_get("EYRTOTAL")); snprintf(cell(L, 7, 1), 28, "%ld", me->vp);
			L->nrow = 8;
		}
		break;
	case 3: title(L, "EYRPREVMON"); col(L, "EYRMONNAME", 120, 0); col(L, "EYRMONADM", 80, 0); col(L, "EYRMONMIL", 80, 0);
		col(L, "EYRMONDIP", 80, 0); col(L, "EYRMONSTART", 60, 1); col(L, "EYRMONEND", 60, 1); col(L, "EYRMONVPS", 40, 1);
		/* the rows: main_amiga.c ledger_window (the rating words) */
		break;
	case 4: title(L, "EYRWEALTH"); graph(L, h, LS_WEALTH); break;
	case 5: title(L, "EYRPE"); graph(L, h, LS_INFRA); break;
	case 6: title(L, "EYRTE"); graph(L, h, LS_TRADE); break;
	case 7: title(L, "EYRINFLATION"); graph(L, h, LS_INFL); break;
	case 8: case 9: {                                                               /* income / expenses: the year's books (0x4e77b0 / 0x4e7cb0) */
		static const char *ik[12] = { "INCOMETAX", "INCOMETRADE", "INCOMEPRODUCTION", "INCOMEPRODFAC", "INCOMEGOLD", "INCOMEVASSAL", "INCOMELOAN",
		                              "INCOMEGIFT", "INCOMESOW", "INCOMEDTAX", "INCOMEEXPORTDUTIES", "INCOMEHARBORFEES" };
		static const char *ek[16] = { "EXPENSEAM", "EXPENSEFM", "EXPENSEARMY", "EXPENSEFLEET", "EXPENSEFORT", "EXPENSECOL", "EXPENSETRA", "EXPENSEMIS",
		                              "EXPENSEMER", "EXPENSERSR", "EXPENSESTAB", "EXPENSEPE", "EXPENSEMAN", "EXPENSELOAN", "EXPENSEINTEREST", "EXPENSEGIFT" };
		const Country *pc = &w->country[player];
		const long *ty = page == 8 ? pc->yinc_m : pc->yexp_m, *ly = page == 8 ? pc->linc_m : pc->lexp_m;
		long tot = 0;
		int n = page == 8 ? 12 : 16;
		(void)e;
		title(L, page == 8 ? "EYRINCOME" : "EYREXPENSE"); L->body_h = page == 8 ? 240 : 320;
		col(L, "EYRSOURCE", 128, 0); col(L, "EYRLASTYEAR", 0, 1); col(L, "EYRTHISYEAR", 0, 1); col(L, page == 8 ? "EYRPERCENTAGE" : "EYRPERCENTAGEE", 0, 1);
		for (i = 0; i < n; i++) tot += ty[i];
		for (i = 0; i < n && i < LG_MAXROW; i++) {
			snprintf(cell(L, i, 0), 28, "%s", text_get(page == 8 ? ik[i] : ek[i]));
			money(cell(L, i, 1), ly[i]);
			money(cell(L, i, 2), ty[i]);
			{ long p = tot > 0 ? ty[i] * 100 / (tot / 1000 > 0 ? tot / 1000 : 1) : 0; snprintf(cell(L, i, 3), 28, "%ld.%ld%%", p / 1000, p % 1000 / 100); }   /* total <= 0: 1 */
		}
		L->nrow = n;
		break; }
	case 10: case 11: {                                                             /* category charts: line graphs, one line per category (0x4e02c0) */
		static const char *ik[12] = { "INCOMETAX", "INCOMETRADE", "INCOMEPRODUCTION", "INCOMEPRODFAC", "INCOMEGOLD", "INCOMEVASSAL", "INCOMELOAN",
		                              "INCOMEGIFT", "INCOMESOW", "INCOMEDTAX", "INCOMEEXPORTDUTIES", "INCOMEHARBORFEES" };
		static const char *ek[16] = { "EXPENSEAM", "EXPENSEFM", "EXPENSEARMY", "EXPENSEFLEET", "EXPENSEFORT", "EXPENSECOL", "EXPENSETRA", "EXPENSEMIS",
		                              "EXPENSEMER", "EXPENSERSR", "EXPENSESTAB", "EXPENSEPE", "EXPENSEMAN", "EXPENSELOAN", "EXPENSEINTEREST", "EXPENSEGIFT" };
		int n = page == 10 ? 12 : 16, base = page == 10 ? 0 : 12, j;
		title(L, page == 10 ? "EYRINCOMES" : "EYREXPENSES");
		L->kind = LG_GRAPH; L->ser_cat = page == 10 ? 1 : 2; L->nser = n; L->npts = h->n;
		L->year0 = h->n ? h->year[0] : 0; L->year1 = h->n ? h->year[h->n - 1] : 0; L->maxv = 1;
		for (j = 0; j < n; j++) {
			L->ser_country[j] = -1; L->ser[j] = h->cat[base + j]; L->ser_label[j] = text_get(page == 10 ? ik[j] : ek[j]);
			for (i = 0; i < h->n; i++) if (h->cat[base + j][i] > L->maxv) L->maxv = h->cat[base + j][i];
		}
		break; }
	case 12: title(L, "EYRPROVINCEVALUE"); col(L, "EYRPVPROVINCE", 80, 0); col(L, "EYRPVTAX", 0, 1); col(L, "EYRPVMP", 0, 1); col(L, "EYRPVPRODUCTION", 0, 0);
		for (i = 1; i < w->nprov && r < LG_MAXROW; i++) if (w->prov[i].owner == player) {
			snprintf(cell(L, r, 0), 28, "%s", pname(w, i)); money(cell(L, r, 1), w->prov[i].income_m); tenth(cell(L, r, 2), w->prov[i].manpower_m);
			snprintf(cell(L, r, 3), 28, "%s", world_name(w, w->prov[i].goods) ? world_name(w, w->prov[i].goods) : "-"); r++;
		}
		L->nrow = r; break;
	case 13: title(L, "EYRTRADE"); col(L, "EYRCOT", 0, 0); col(L, "EYRTRADEOUR", 0, 1); col(L, "EYRTRADETOTAL", 0, 1); col(L, "EYRTCOST", 0, 1);
		for (k = 0; k < w->ncot && r < LG_MAXROW; k++) {
			int n = env_merchant_count(w, player, k), t = env_merchant_total(w, k);
			if (!n) continue;
			snprintf(cell(L, r, 0), 28, "%s", pname(w, w->cot[k].prov));
			snprintf(cell(L, r, 1), 28, "%d.0%%", n * 100 / 20); snprintf(cell(L, r, 2), 28, "%d.0%%", t * 100 / 20);
			money(cell(L, r, 3), env_merchant_cost(w, e, player, k)); r++;
		}
		L->nrow = r; break;
	case 14: {
		long am[5], du[5];
		short rt[5];
		int n = e ? econ_loans(e, player, am, du, 5) : 0;
		if (e) econ_loan_rates(e, player, rt, 5);
		title(L, "EYRLOANS"); col(L, "EYRLENDER", 120, 0); col(L, "EYRBORROWER", 120, 0); col(L, "EYRDUCATS", 0, 1);
		col(L, "EYRINTERESTPERC", 0, 1); col(L, "EYRINTERESTCASH", 0, 1); col(L, "EYRDUE", 120, 0);
		for (i = 0; i < n; i++) {
			snprintf(cell(L, i, 0), 28, "%s", text_get("EYRBANK")); snprintf(cell(L, i, 1), 28, "%s", text_get(me->tag));
			money(cell(L, i, 2), am[i]); snprintf(cell(L, i, 3), 28, "%d.0%%", rt[i]);
			money(cell(L, i, 4), am[i] / 100 * rt[i]);
			dayfmt(cell(L, i, 5), du[i]);
		}
		L->nrow = n;
		break; }
	case 15: {
		long n[6] = { 0, 0, 0, 0, 0, 0 };
		static const char *nk[6] = { "EYRRECINF", "EYRRECCAV", "EYRRECART", "EYRRECWARSHIPS", "EYRRECGALLEYS", "EYRRECTRANSPORTS" };
		static const long uc[6] = { 9, 21, 30, 50, 10, 15 };
		title(L, "EYRMAINTENANCE"); L->body_h = 140; col(L, "EYRCATEGORY", 0, 0); col(L, "EYRCURRENTNUM", 0, 1); col(L, "EYRCOST", 0, 1);
		for (i = 0; i < w->nland; i++) if (w->land[i].owner == player && w->land[i].dead != 1) { n[0] += w->land[i].infantry; n[1] += w->land[i].cavalry; n[2] += w->land[i].artillery; }
		for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == player) { n[3] += w->naval[i].warships; n[4] += w->naval[i].galleys; n[5] += w->naval[i].transports; }
		for (i = 0; i < 6; i++) {                                                 /* SPEC_TIME_ECONOMY 6.2 at the 100 % level */
			long c = i < 2 ? uc[i] * n[i] / 120 : i == 2 ? uc[i] * n[i] * 1000 / 1200 : uc[i] * n[i] * 1000 / 480;
			snprintf(cell(L, i, 0), 28, "%s", text_get(nk[i])); snprintf(cell(L, i, 1), 28, "%ld", n[i]); tenth(cell(L, i, 2), c);
		}
		L->nrow = 6; break; }
	case 16: case 17: {
		char t[64];
		snprintf(t, sizeof t, text_get(page == 16 ? "EYRFRIEND" : "EYRNMY"), text_get(me->tag));
		snprintf(L->title, sizeof L->title, "%s", t);
		if (page == 16) { col(L, "EYRFRNATION", 80, 0); col(L, "EYRFRVAS", 32, 0); col(L, "EYRFRMIL", 32, 0); col(L, "EYRFRRM", 32, 0);
		                  col(L, "EYRFRTR", 0, 0); col(L, "EYRFRMA", 0, 0); col(L, "EYRFRCB", 80, 0); }
		else { col(L, "EYRENNATION", 80, 0); col(L, "EYRENWAR", 32, 0); col(L, "EYRENTRADE", 0, 0); col(L, "EYRENCB", 80, 0); }
		for (k = 0; k < w->ncountry && r < LG_MAXROW; k++) {
			int vas, mil, rm, emb, acc, cb, war, ref;
			if (k == player || w->country[k].nprov_owned <= 0) continue;
			vas = world_overlord(w, k) == player || world_overlord(w, player) == k;
			mil = world_allied(w, player, k, ALLY_MILITARY); rm = world_allied(w, player, k, ALLY_DYNASTIC);
			emb = (w->dipflag[player][k] & DF_NOTRADE) != 0; acc = (w->dipflag[k][player] & DF_ACCESS) || (w->dipflag[player][k] & DF_ACCESS);
			cb = (w->dipflag[player][k] & DF_CB_PERM) != 0; war = mil_at_war(w, player, k); ref = (w->dipflag[k][player] & DF_NOTRADE) != 0;
			if (page == 16 ? !(vas || mil || rm || emb || acc || cb) : !(war || ref || (w->dipflag[k][player] & DF_CB_PERM))) continue;
			snprintf(cell(L, r, 0), 28, "%s", text_get(w->country[k].tag));
			if (page == 16) {
				snprintf(cell(L, r, 1), 28, "%s", yesno(vas)); snprintf(cell(L, r, 2), 28, "%s", yesno(mil)); snprintf(cell(L, r, 3), 28, "%s", yesno(rm));
				snprintf(cell(L, r, 4), 28, "%s", yesno(emb)); snprintf(cell(L, r, 5), 28, "%s", yesno(acc));
				snprintf(cell(L, r, 6), 28, "%s", cb ? text_get("EYRPERMANENT") : text_get("EYRNOPES"));
			} else {
				snprintf(cell(L, r, 1), 28, "%s", yesno(war)); snprintf(cell(L, r, 2), 28, "%s", yesno(ref));
				snprintf(cell(L, r, 3), 28, "%s", (w->dipflag[k][player] & DF_CB_PERM) ? text_get("EYRPERMANENT") : text_get("EYRNOPES"));
			}
			r++;
		}
		L->nrow = r; break; }
	case 18: title(L, "EYRALLIANCES"); L->kind = LG_TEXT;
		for (i = 0; i < w->nally && L->nlines < 32; i++) if (w->ally[i].type == ALLY_MILITARY) {
			char *s = L->lines[L->nlines++];
			size_t n;
			snprintf(s, 120, "%s:", text_get("EYRMILALLIANCE"));
			for (k = 0; k < w->ally[i].n; k++) { n = strlen(s); snprintf(s + n, 120 - n, "%s %s", k ? "," : "", text_get(w->country[w->ally[i].member[k]].tag)); }
		}
		break;
	case 19: title(L, "EYRWARS"); L->kind = LG_TEXT;
		for (i = 0; i < w->nwar && L->nlines < 32; i++) {
			char *s = L->lines[L->nlines++];
			size_t n;
			snprintf(s, 120, "%s", text_get("EYRWAR"));
			for (k = 0; k < w->war[i].natt; k++) { n = strlen(s); snprintf(s + n, 120 - n, "%s %s", k ? "," : "", text_get(w->country[w->war[i].att[k]].tag)); }
			n = strlen(s); snprintf(s + n, 120 - n, " /");
			for (k = 0; k < w->war[i].ndef; k++) { n = strlen(s); snprintf(s + n, 120 - n, "%s %s", k ? "," : "", text_get(w->country[w->war[i].def[k]].tag)); }
		}
		break;
	case 20: title(L, "EYRLANDTECH"); graph(L, h, LS_LAND); break;
	case 21: title(L, "EYRNAVALTECH"); graph(L, h, LS_NAVAL); break;
	case 22: title(L, "EYRLEADERS"); col(L, "EYRLNAME", 120, 0); col(L, "EYRLTYPE", 40, 0); col(L, "EYRLRANK", 0, 1); col(L, "EYRLCOMMISION", 130, 0);
		{	/* our leaders in command: the unit they lead is the commission */
			static const char *const tk[4] = { "GENERAL", "ADMIRAL", "CONQUISTADOR", "EXPLORER" };
			for (i = 0; i < w->nland + w->nnaval && r < LG_MAXROW; i++) {
				int nav = i >= w->nland, id, ty, rk;
				const char *un, *ln = NULL;
				const Leader *x;
				if (!nav) { const LandUnit *u = &w->land[i]; if (u->dead || u->owner != player || !u->l_id) continue; id = u->l_id; ty = u->l_type; rk = u->l_rank; un = u->name; }
				else { const NavalUnit *u = &w->naval[i - w->nland]; if (u->owner != player || !u->l_id || u->warships + u->galleys + u->transports <= 0) continue; id = u->l_id; ty = u->l_type; rk = 0; un = u->name; }
				x = id > 0 && id != 30000 && g_llead ? lead_get(g_llead, id - 1) : NULL;
				if (x) { ln = x->name; rk = x->rank; ty = x->type; }
				snprintf(cell(L, r, 0), 28, "%s", ln ? ln : "-");
				snprintf(cell(L, r, 1), 28, "%s", ty >= 0 && ty < 4 ? text_get(tk[ty]) : "-");
				snprintf(cell(L, r, 2), 28, "%d", rk);
				snprintf(cell(L, r, 3), 28, "%s", un ? un : "-");
				r++;
			}
			L->nrow = r;
		}
		break;
	case 23: title(L, "EYRARMYSIZE"); graph(L, h, LS_ARMY); break;
	case 24: title(L, "NAVYSIZE"); graph(L, h, LS_NAVY); break;
	case 25: case 26:
		title(L, page == 25 ? "EYRARMIES" : "EYRFLEETS");
		col(L, page == 25 ? "EYRANAME" : "EYRFNAME", 160, 0); col(L, page == 25 ? "EYRAPROV" : "EYRFPROV", 100, 0);
		col(L, page == 25 ? "EYRAREGION" : "EYRFREGION", 100, 0);
		col(L, page == 25 ? "EYRRECINF" : "EYRRECWARSHIPS", 64, 1); col(L, page == 25 ? "EYRRECCAV" : "EYRRECGALLEYS", 64, 1); col(L, page == 25 ? "EYRRECART" : "EYRRECTRANSPORTS", 48, 1);
		if (page == 25) for (i = 0; i < w->nland && r < LG_MAXROW; i++) {
			const LandUnit *u = &w->land[i];
			if (u->owner != player || u->dead == 1) continue;
			snprintf(cell(L, r, 0), 28, "%s", u->name ? u->name : ""); snprintf(cell(L, r, 1), 28, "%s", pname(w, u->location));
			snprintf(cell(L, r, 2), 28, "%s", u->location > 0 && world_name(w, w->prov[u->location].region) ? world_name(w, w->prov[u->location].region) : "-");
			snprintf(cell(L, r, 3), 28, "%ld", u->infantry); snprintf(cell(L, r, 4), 28, "%ld", u->cavalry); snprintf(cell(L, r, 5), 28, "%ld", u->artillery); r++;
		} else for (i = 0; i < w->nnaval && r < LG_MAXROW; i++) {
			const NavalUnit *u = &w->naval[i];
			if (u->owner != player || u->warships + u->galleys + u->transports <= 0) continue;
			snprintf(cell(L, r, 0), 28, "%s", u->name ? u->name : ""); snprintf(cell(L, r, 1), 28, "%s", pname(w, u->location));
			snprintf(cell(L, r, 2), 28, "%s", u->location > 0 && world_name(w, w->prov[u->location].region) ? world_name(w, w->prov[u->location].region) : "-");
			snprintf(cell(L, r, 3), 28, "%d", u->warships); snprintf(cell(L, r, 4), 28, "%d", u->galleys); snprintf(cell(L, r, 5), 28, "%d", u->transports); r++;
		}
		L->nrow = r; break;
	case 27: title(L, "EYRRECRUITS"); col(L, "EYRRECLOCATION", 80, 0); col(L, "EYRRECINF", 0, 1); col(L, "EYRRECCAV", 0, 1); col(L, "EYRRECART", 0, 1);
		col(L, "EYRRECWARSHIPS", 0, 1); col(L, "EYRRECREADY", 80, 0);
		for (i = 0; i < w->nbuild && r < LG_MAXROW; i++) {
			const Build *b = &w->build[i];
			if (b->country != player || b->ship > 3) continue;
			snprintf(cell(L, r, 0), 28, "%s", pname(w, b->prov));
			snprintf(cell(L, r, 1), 28, "%ld", b->ship ? 0 : b->inf); snprintf(cell(L, r, 2), 28, "%ld", b->ship ? 0 : b->cav);
			snprintf(cell(L, r, 3), 28, "%ld", b->ship ? 0 : b->art); snprintf(cell(L, r, 4), 28, "%ld", b->ship ? b->inf : 0);
			dayfmt(cell(L, r, 5), b->done);
			r++;
		}
		L->nrow = r; break;
	case 28: title(L, "EYRLOSSES"); L->body_h = 120; col(L, "EYRLOTYPE", 64, 0); col(L, "EYRLOTOTAL", 100, 1); col(L, "EYRLOCOMBAT", 0, 1); col(L, "EYRLOATT", 0, 1); break;   /* losses: not tracked yet */
	case 29: title(L, "EYRTSIZE"); graph(L, h, LS_TERR); break;
	case 30: case 31:
		title(L, page == 30 ? "EYRTSPROVINCES" : "EYRCOLONIES");
		col(L, "EYRTSPROVINCE", 100, 0); col(L, "EYRTSREGION", 80, 0); col(L, "EYRTSPOP", 0, 1); col(L, "EYRTSMP", 0, 1); col(L, "EYRTSFORTLEVEL", 0, 0); col(L, "EYRTSCONTROL", 0, 0);
		for (i = 1; i < w->nprov && r < LG_MAXROW; i++) {
			const Province *p = &w->prov[i];
			char fk[12];
			if (p->owner != player || (page == 30) != (p->population >= 1000)) continue;
			snprintf(cell(L, r, 0), 28, "%s", pname(w, i));
			snprintf(cell(L, r, 1), 28, "%s", world_name(w, p->region) ? world_name(w, p->region) : "-");
			snprintf(cell(L, r, 2), 28, "%ld", p->population); tenth(cell(L, r, 3), p->manpower_m);
			snprintf(fk, sizeof fk, "EYRFORT%d", p->fort >= 1 && p->fort <= 6 ? p->fort : 0); snprintf(cell(L, r, 4), 28, "%s", text_get(fk));
			snprintf(cell(L, r, 5), 28, "%s", p->controller >= 0 ? text_get(w->country[p->controller].tag) : "-"); r++;
		}
		L->nrow = r; break;
	case 32: title(L, "EYRSTRUCTURES"); col(L, "EYRSTRUPROV", 0, 0); col(L, "EYRSTRUMANU", 0, 0); col(L, "EYRSTRUSHIPYARD", 0, 0); col(L, "EYRTSFORTLEVEL", 0, 0);
		for (i = 1; i < w->nprov && r < LG_MAXROW; i++) if (w->prov[i].owner == player) {
			char fk[12];
			snprintf(cell(L, r, 0), 28, "%s", pname(w, i)); snprintf(cell(L, r, 1), 28, "%s", yesno(0)); snprintf(cell(L, r, 2), 28, "%s", yesno(0));
			snprintf(fk, sizeof fk, "EYRFORT%d", w->prov[i].fort >= 1 && w->prov[i].fort <= 6 ? w->prov[i].fort : 0); snprintf(cell(L, r, 3), 28, "%s", text_get(fk)); r++;
		}
		L->nrow = r; break;
	case 33: title(L, "EYRCOMPARISON"); col(L, "EYRFRNATION", 120, 0); col(L, "EYRINFLATION", 0, 1); col(L, "EYRTE", 0, 1); col(L, "EYRPE", 0, 1);
		col(L, "EYRLANDTECH", 0, 1); col(L, "EYRNAVALTECH", 0, 1);
		for (k = 0; k < h->ntrack && r < LG_MAXROW; k++) {
			int c = h->track[k];
			const Country *co = &w->country[c];
			snprintf(cell(L, r, 0), 28, "%s", text_get(co->tag));
			{ long u = co->inflation_u / 1000; snprintf(cell(L, r, 1), 28, "%ld.%ld", u / 10, u % 10); }
			tenth(cell(L, r, 2), e ? econ_trade_eff(e, c) * 100 : 0); tenth(cell(L, r, 3), e ? econ_prod_eff(e, c) * 100 : 0);
			snprintf(cell(L, r, 4), 28, "%d", co->land_tech); snprintf(cell(L, r, 5), 28, "%d", co->naval_tech); r++;
		}
		L->nrow = r; break;
	case 34: title(L, "EYRHISTORY"); L->kind = LG_TEXT;
		for (i = 0; i < g_nlog && L->nlines < 32; i++) snprintf(L->lines[L->nlines++], 120, "%s", g_log[(g_logpos - 1 - i + 40) % 40]);
		break;
	default: break;
	}
	(void)m;
}

void ledger_hist_save(const LedgerHist *h, const World *w, FILE *f)
{
	int s, k, i;
	fprintf(f, "amieu2_ledger = { track = {");
	for (k = 0; k < h->ntrack; k++) fprintf(f, " %s", w->country[h->track[k]].tag);
	fprintf(f, " } years = {");
	for (i = 0; i < h->n; i++) fprintf(f, " %d", h->year[i]);
	fprintf(f, " }");
	for (s = 0; s < LS_N; s++)
		for (k = 0; k < h->ntrack; k++) {
			fprintf(f, "\n\ts = {");
			for (i = 0; i < h->n; i++) fprintf(f, " %ld", h->v[s][k][i]);
			fprintf(f, " }");
		}
	for (s = 0; s < LG_NCAT; s++) {
		fprintf(f, "\n\tc = {");
		for (i = 0; i < h->n; i++) fprintf(f, " %ld", h->cat[s][i]);
		fprintf(f, " }");
	}
	fprintf(f, " }\n");
}

void ledger_hist_restore(LedgerHist *h, const World *w, const SNode *root)
{
	const SNode *b = script_get(root, "amieu2_ledger"), *q, *t;
	int k = 0, s = 0, i;
	if (!b) return;
	memset(h, 0, sizeof *h);
	for (t = script_get(b, "track"), q = t ? t->child : NULL; q && h->ntrack < LG_TRACK; q = q->next)
		if (!q->key && q->val) { int c = world_country(w, q->val); if (c >= 0) h->track[h->ntrack++] = (short)c; }
	for (t = script_get(b, "years"), q = t ? t->child : NULL; q && h->n < LG_SAMPLES; q = q->next) if (!q->key && q->val) h->year[h->n++] = (short)parse_int(q->val);
	for (q = b->child; q && s < LS_N; q = q->next) {
		const SNode *x;
		if (!q->key || !script_ieq(q->key, "s")) continue;
		for (i = 0, x = q->child; x && i < h->n; x = x->next) if (!x->key && x->val) h->v[s][k][i++] = parse_int(x->val);
		if (++k >= h->ntrack) { k = 0; s++; }
	}
	for (q = b->child, s = 0; q && s < LG_NCAT; q = q->next) {
		const SNode *x;
		if (!q->key || !script_ieq(q->key, "c")) continue;
		for (i = 0, x = q->child; x && i < h->n; x = x->next) if (!x->key && x->val) h->cat[s][i++] = parse_int(x->val);
		s++;
	}
}
