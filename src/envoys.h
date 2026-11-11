/* envoys.h - merchants, colonists and missionaries sent by a country (the player's buttons), rules read
 * out of eu2.exe (docs/SPEC_ENVOYS.md). The monarch is not ported: ADM is 3, the original's value
 * without a monarch. The original rolls the outcome when the envoy leaves; merchants and colonists
 * arrive at once here (their travel time is NIEPEWNE), a missionary's conversion completes after its
 * time through the build queue (kind 5). Portable C, ducats x1000. */
#ifndef EU2_ENVOYS_H
#define EU2_ENVOYS_H
#include "world.h"
#include "economy.h"
#include "military.h"
#include "script.h"
#include <stdio.h>

enum { ENV_OK = 0, ENV_NOSTOCK = -1, ENV_MONEY = -2, ENV_TECH = -3, ENV_FULL = -4, ENV_REFUSED = -5,
       ENV_BADPROV = -6, ENV_MONOPOLY = -7, ENV_REACH = -8 };
enum { ENV_FAILED = 0, ENV_SUCCESS = 1, ENV_SENT = 2 };   /* SENT: on its way, the outcome (rolled now) comes on arrival */
/* the outcomes on arrival (NOTE_ENVOY other): */
enum { EA_MERCSUCC, EA_MERCFAIL, EA_COLNEW, EA_COLEXPAND, EA_COLFAIL, EA_TPNEW, EA_TPEXPAND, EA_TPFAIL };
void env_day(World *w, long today);       /* colonists, traders and merchants that arrive today (SPEC_SMALLRULES 3.7) */

long env_merchant_cost(const World *w, const Economy *e, int c, int cot);
int  env_merchant_chance(const World *w, const Economy *e, int c);
int  env_merchant_can(const World *w, const Economy *e, int c, int cot);        /* ENV_OK or the first reason */
int  env_merchant_count(const World *w, int c, int cot);
int  env_merchant_total(const World *w, int cot);
int  env_merchant_send(World *w, const Economy *e, int c, int cot);             /* ENV_SUCCESS / ENV_FAILED / ENV_* */

long env_colonist_cost(const World *w, const Military *m, int c, int prov);
int  env_colonist_chance(const World *w, const Economy *e, int c, int prov);
void env_always_success(int c);   /* the tutorial: country c (-1 none) colonises with 100 % */
int  env_colonist_can(const World *w, const Military *m, int c, int prov);
int  env_colonist_send(World *w, const Economy *e, const Military *m, int c, int prov);
long env_trader_cost(const World *w, int c, int prov);                 /* SPEC_ENVOYS 2.6 */
void env_set_econ(const Economy *e);
void env_attach(World *w);   /* the world's hook for the AI envoy plans' money (call after loading) */
void env_set_trade_offer(int (*fn)(void *, int, int), void *user);   /* the AI's trade agreement offers (diplo_ai_offer_trade) */
void env_set_military(Military *m);                                   /* natives attacking after a failed envoy */                                  /* the efficiencies for the travel times */
int  env_colonist_months(const World *w, int c, int prov);            /* SPEC_SMALLRULES 3.2, at least 2 */
int  env_trader_months(const World *w, int c, int prov);              /* 3.3, at least 2 */
int  env_trader_chance(const World *w, const Economy *e, int c, int prov);
int  env_trader_can(const World *w, int c, int prov);                  /* ENV_TECH below trade level 2 */
int  env_trader_send(World *w, const Economy *e, int c, int prov);     /* uses a colonist (SPEC_ENVOYS 2.3) */

long env_missionary_cost(const World *w, int c, int prov);
int  env_missionary_chance(const World *w, const Economy *e, int c, int prov);
int  env_missionary_months(const World *w, int c, int prov);
int  env_missionary_can(const World *w, int c, int prov);
int  env_missionary_send(World *w, const Economy *e, int c, int prov, long today);   /* queued: build kind 5 */
void env_seed(unsigned long s);
void env_ai_month(World *w, const Economy *e, const Military *m, long today);   /* AI countries use their envoys */
void env_save(FILE *f);                    /* amieu2_envoys: the RNG */
void env_restore(const SNode *root);
#endif
