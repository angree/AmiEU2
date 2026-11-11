/* ai.h - the diplomatic AI decoded from eu2.exe (docs/SPEC_AI.md): strategic mode, worst enemy, the
 * monthly diplomat (wars, marriages, vassals, alliances, embargoes) and the answers an AI country gives
 * to proposals. Used through diplo.c; the state lives in Diplo (diplo_int.h). Portable C. */
#ifndef EU2_AI_H
#define EU2_AI_H
#include <stdio.h>
#include "script.h"
#include "world.h"

typedef struct AiState AiState;
struct Diplo;

enum { AIP_WAR, AIP_PEACE, AIP_MARRY, AIP_VASSAL, AIP_ANNEX, AIP_CANVAS, AIP_INVITE, AIP_NEWALLY, AIP_ASKJOIN, AIP_LEAVE,
       AIP_TRADE, AIP_N };

AiState *ai_new(void);
void ai_free(AiState *a);
void ai_month(struct Diplo *d);                     /* monthly bookkeeping */
long ai_strength(struct Diplo *d, int c, int naval);  /* the strength index FS / FN x1000 (0x43c010), computed now */
void ai_mission_vp(struct Diplo *d);                /* the AI's random mission VP (monthly) */
void ai_day(struct Diplo *d, long today);           /* the diplomats whose tick came (0x53fd00) */
int  ai_war_ok(struct Diplo *d, int u, int t);      /* 0x540fc0 */
int  ai_badboy_limit(const World *w, int c);        /* 0x45af40 */

/* answers of AI country u to a proposal from o (1 = accept) */
int  ai_accept_alliance(struct Diplo *d, int u, int o, int al);   /* al = existing alliance invited to, -1 new */
int  ai_accept_marriage(struct Diplo *d, int u, int o);
int  ai_accept_vassal(struct Diplo *d, int u, int o);             /* o would be the overlord */
int  ai_accept_access(struct Diplo *d, int u, int o);
int  ai_accept_trade(struct Diplo *d, int u, int o);
int  ai_accept_loan(struct Diplo *d, int u, int lender, long amount_m, int rate, int months);
int  ai_loan_cancel(struct Diplo *d, int u, int lender, int rate); /* at the due date: 1 = renounce the debt */
int  ai_honour(struct Diplo *d, int u, int ally, int enemy, int war);
int  ai_allow_join(struct Diplo *d, int u, int o, int al);        /* u leads al */
int  ai_accept_annex(struct Diplo *d, int u, int o);              /* o is u's overlord */

int  ai_mode(const struct Diplo *d, int c);         /* 0 dominance, 1 buildup, 2 colonise */
int  ai_enemy(const struct Diplo *d, int c);        /* worst enemy, -1 */
long ai_war_months(const struct Diplo *d, int c);   /* months at war without a day of peace (country+0x38a) */
void ai_save(const struct Diplo *d, FILE *f);
void ai_restore(struct Diplo *d, const SNode *root);
#endif
