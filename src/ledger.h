/* ledger.h - the Ledger (EYR, F6): 35 pages in 7 chapters, layout in docs/SPEC_LEDGER.md. This
 * module fills a page (table, graph or text list) from the simulation; ui.c draws it. The graph
 * history is sampled at the start and every new year for the tracked countries (the player and the
 * major powers); the original saves it in the save game - here it is kept in memory only
 * (NIEPEWNE). Portable C. */
#ifndef EU2_LEDGER_H
#define EU2_LEDGER_H
#include "world.h"
#include "economy.h"
struct Monarchs; struct Leaders;
void ledger_set_sources(const struct Monarchs *m, const struct Leaders *l);   /* the monarchs / leaders pages */
struct Missions;
void ledger_set_missions(const struct Missions *m);                          /* the missions page */
#include "military.h"

#define LG_PAGES   35
#define LG_MAXCOL  10
#define LG_MAXROW  48
#define LG_TRACK   12
#define LG_SER     16                 /* series on one graph: tracked countries or the expense categories */
#define LG_NCAT    28                 /* the Ledger's 12 income + 16 expense categories (pages 10 / 11) */
#define LG_SAMPLES 301
enum { LG_TABLE, LG_GRAPH, LG_TEXT };
enum { LS_VP, LS_WEALTH, LS_INFRA, LS_TRADE, LS_INFL, LS_LAND, LS_NAVAL, LS_ARMY, LS_NAVY, LS_TERR, LS_N };

typedef struct {
	int page, chapter, kind;
	char title[64];
	int ncol; const char *hdr[LG_MAXCOL]; short w[LG_MAXCOL]; unsigned char right[LG_MAXCOL];
	int nrow, body_h; char cell[LG_MAXROW][LG_MAXCOL][28];
	int nser, npts, year0, year1; short ser_country[LG_SER], ser_pen[LG_SER]; const long *ser[LG_SER]; long maxv;
	int ser_cat; const char *ser_label[LG_SER];   /* category charts: 1 income / 2 expense, labels; colours from gfx/palette/<kind>.bmp */
	int nlines; char lines[32][120];
} UiLedger;

typedef struct {
	int ntrack; short track[LG_TRACK];
	int n; short year[LG_SAMPLES];
	long v[LS_N][LG_TRACK][LG_SAMPLES];
	long cat[LG_NCAT][LG_SAMPLES];        /* the player's category books at each sample (pages 10 / 11) */
} LedgerHist;

void ledger_hist_init(LedgerHist *h, const World *w, int player);
void ledger_hist_sample(LedgerHist *h, const World *w, const Economy *e);   /* once a year (and at the start) */
void ledger_log(const World *w, const char *text);                          /* the History page (34) */
int  ledger_log_recent(const char **out, int max);                         /* the newest entries, oldest first */
int  ledger_chapter(int page);                                              /* 0..6 */
int  ledger_first_page(int chapter);
int  ledger_last_page(int chapter);
/* the history in the save game (amieu2_ledger): tracked countries by tag, then every series' samples */
void ledger_hist_save(const LedgerHist *h, const World *w, FILE *f);
void ledger_hist_restore(LedgerHist *h, const World *w, const SNode *root);
void ledger_fill(UiLedger *L, int page, const World *w, const Economy *e, const Military *m, const LedgerHist *h, int player);
#endif
