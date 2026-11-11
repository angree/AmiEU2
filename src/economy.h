/* economy.h - monthly and yearly economy, implemented from docs/SPEC_TIME_ECONOMY.md (v1).
 * Fixed point: ducats x1000 (_m), fractions x1000 (_m) unless noted, inflation x1e6.
 * In v1: tax (§5.1), production + gold (§5.2), tolls (§5.4), census tax (§5.1), maintenance (§6.2,
 * no over-limit surcharge), budget split with research and stability costs (§6.3, §6.4), monthly and
 * AI-capped inflation (§7.1), gold inflation (§7.2), population growth (§8.1, base terms), manpower
 * (§8.2), envoys (§8.3), badboy decay (§4.1), centre-of-trade income (§5.3; membership = nearest CoT
 * on the same continent, NIEPEWNE: the real attractiveness rule is not decoded). Not yet: revolt risk, loans,
 * bankruptcy, market prices (base values are used), war exhaustion (no wars yet). */
#ifndef EU2_ECONOMY_H
#define EU2_ECONOMY_H
#include "pak.h"
#include "world.h"
#include "script.h"
#include <stdio.h>

typedef struct Economy Economy;

Economy *econ_load(World *w, Pak *text, char *err, int errlen);   /* tables from the DB csv files */
void     econ_free(Economy *e);
void     econ_month(Economy *e);          /* call on every month change */
void     econ_year(Economy *e);           /* call on every year change, after econ_month */

/* per-country figures of the last month (ducats x1000) for the UI and tests */
typedef struct {
	long tax, production, gold, tolls, census, maintenance, pool, trade;
	long maint_land, maint_naval;         /* the two parts of maintenance */
	long rate_mer, rate_col, rate_dip, rate_miss;   /* envoys per year x1000 (§8.3) */
	long manpower_rate_m;      /* thousands of men per year x1000 */
	int loans; long bankruptcies;
} EconReport;
void     econ_report(const Economy *e, int country, EconReport *r);
const short *econ_prov_cots(const Economy *e);   /* CoT index (world.cot) per province, -1 none */
long     econ_trade_eff(const Economy *e, int country);   /* x1000 */
long     econ_trade_value_m(const Economy *e, int prov);
typedef int (*EconRiskFn)(void *user, int prov);
void     econ_set_riskfn(Economy *e, EconRiskFn fn, void *user);   /* the revolt risk for the tax modifier (rev_risk) */  /* the province's monthly trade value x1000 (0x480e30, less the pirates) */
/* the trade-good window (iw_resource, windows4 4): one good from one country's side. The market is not
 * simulated (prices are the base values), so supply and demand are 100 % (NIEPEWNE) */
typedef struct { int gold; long base, price_m, units, prod_eff, prod_income_m, trade_units, trade_eff, trade_income_m; int leader, nprov; } EconGoods;
void     econ_goods(const Economy *e, int country, int goods, EconGoods *g);
long     econ_price_m(Economy *e, int goods);              /* the market price x1000 (9.1) */
long     econ_prod_eff(const Economy *e, int country);    /* x1000 */
int      econ_religion(const Economy *e, int name_id);    /* religion.csv row of an interned name, -1 */
int      econ_take_loan(Economy *e, int country, long today);   /* a bank loan; -1 at the limit of 5 */
int      econ_can_take_loan(const Economy *e, int country);
int      econ_loan_answer(Economy *e, int country, int idx, int pay);   /* a due bank loan: 1 paid, 0 extended, -1 not due */
long     econ_support_limit(const Economy *e, int country);     /* land support limit, men (§6.2) */
int      econ_boost_ok(const Economy *e, int country, int row);   /* the one-time increase of a budget row (ECON_SL_*, not TREAS) */
int      econ_boost(Economy *e, int country, int row);            /* 0 done: 200 ducats for 100 of investment (BUTTON_0045..49) */
int      econ_war_tax_state(const World *w, int country);      /* 0 = can raise; 1 running, 2 not at war, 3 stability -3 */
int      econ_raise_war_tax(World *w, int country);            /* 0 done, -1 not allowed */
int      econ_loans(const Economy *e, int country, long *amount_m, long *due, int max);
long     econ_loan_size(void);                                  /* ducats x1000 (the default) */
long     econ_loan_size_of(const World *w, int country);        /* the country's (loansize) */
int      econ_bank_rate(const Economy *e, int country);        /* a new bank loan's interest, percent a year (0x45bb00) */
int      econ_loan_rates(const Economy *e, int country, short *rate, int max);   /* each loan's own rate */
/* maintenance level of the land / naval window (x1000, 500..1000 = 50..100 %, §6.2 landSlider) */
int      econ_maint_level(const Economy *e, int country, int naval);
void     econ_set_maint_level(Economy *e, int country, int naval, int level_m);

/* the budget window (SPEC_UI §4.1), rows in its order: land tech, naval tech, stability, trade,
 * production (infra), treasury. Sliders are fractions x1000 summing to 1000. */
enum { ECON_SL_LAND, ECON_SL_NAVAL, ECON_SL_STAB, ECON_SL_TRADE, ECON_SL_PROD, ECON_SL_TREAS, ECON_SL_N };
typedef struct {
	short slider[ECON_SL_N];
	long progress_m[ECON_SL_N], cost_m[ECON_SL_N];   /* research/stability progress and the next step's cost */
	long income_m;                                   /* last month's budget pool */
} EconBudget;
void     econ_budget(const Economy *e, int country, EconBudget *b);
void     econ_set_slider(Economy *e, int country, int row, int value_m);

/* save game: an amieu2_economy block; restore reads it from the parsed save file (script.h) */
void     econ_save(const Economy *e, FILE *f);
void     econ_restore(Economy *e, const SNode *root);
void     econ_new_game(Economy *e);      /* new game only: the starting manpower pools */
#endif
