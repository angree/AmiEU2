/* diplo.h - relations and war declarations (docs/SPEC_WAR_DIPLOMACY.md §2, §4): monthly relation
 * drift toward the ceiling, alliance expiry, declaring war with its stability/badboy/VP/relation
 * effects, the call to arms, and AI war declarations (the AI rules are ours, NIEPEWNE: the exe's
 * AI was not read). Portable C, relations x1000. */
#ifndef EU2_DIPLO_H
#define EU2_DIPLO_H
#include "world.h"
#include "military.h"
#include "peace.h"

typedef struct Diplo Diplo;

Diplo *diplo_new(World *w, Military *m, Peace *p, unsigned long seed);
void   diplo_free(Diplo *d);
int    diplo_load_ai(Diplo *d, Pak *text);       /* the AI folder's .ai files: war (warmonger 0..100), combat targets; returns files read */
void   diplo_month(Diplo *d, long today);            /* drift, expiry, the AI's monthly bookkeeping */
void   diplo_day(Diplo *d, long today);              /* the AI diplomats whose day came */
long   diplo_ceiling(const Diplo *d, int a, int b);  /* §2.2, x1000 */
/* a declares war on b: -1 if not allowed (§4.1), else the new war index */
int    diplo_declare_war(Diplo *d, int a, int b, long today);
int    diplo_neighbours(const Diplo *d, int a, int b);
void   diplo_annex_now(Diplo *d, int lord, int v);   /* events inherit: v's provinces and armies go to lord */

/* the player's actions, numbered as the original's action ids (SPEC_UI 4.8) */
enum { DA_MARRY = 0, DA_BREAKDYN = 1, DA_OFFVAS = 2, DA_BRKVAS = 3, DA_CANVAS = 4, DA_ANNEX = 5, DA_WAR = 6, DA_PEACE = 7,
       DA_INVITE = 8, DA_CREATE = 9, DA_JOIN = 10, DA_BAN = 11, DA_LEAVE = 12, DA_EXCDIS = 13, DA_ALLOWTRADE = 15,
       DA_EMBARGO = 16, DA_LOAN = 17, DA_GIFT_STATE = 18, DA_GIFT_PERS = 19, DA_INTRO = 20, DA_WARN = 21, DA_INSULT = 22,
       DA_CLAIM = 23, DA_GUARANTEE = 24, DA_ASKACC = 25, DA_REVACC = 26, DA_CANACC = 27, DA_CANTRADE = 28, DA_OFFTRADE = 29 };
enum { DS_HIDDEN, DS_OFF, DS_ON };                     /* not listed / greyed / clickable */
enum { DR_FAIL, DR_DONE, DR_ACCEPTED, DR_REFUSED };
int    diplo_ai_offer_trade(void *d, int a, int b);   /* for env_set_trade_offer */
int    diplo_action_state(const Diplo *d, int a, int b, int act, long today);
long   diplo_action_cost(const Diplo *d, int a, int b, int act);   /* gifts, warning ...: ducats x1000 */
int    diplo_action(Diplo *d, int a, int b, int act, long today);
/* Offer Loan (window 0x13, windows4 6.1): amount in ducats x1000, interest percent, length in months. The
 * answer and the repayment are ours (NIEPEWNE): a country short of money or friendly to us takes it; at
 * the due date it pays back amount x (1 + rate x months / 1200), or defaults - the lender then gets a
 * casus belli (LOANSNOREPAY says 12 months; ours has no expiry yet) */
int    diplo_offer_loan(Diplo *d, int a, int b, long amount_m, int rate, int months, long today);   /* DR_* */
int    diplo_offer_marriage(Diplo *d, int u, int o);   /* the tutorial: u proposes a royal marriage to o now (1 = married) */
/* Grant Independence (iw_Vassal, windows4 6.2): countries that do not exist and have a core in one of our
 * provinces (the exe's filter 0x43d8c0: NIEPEWNE); releasing one gives it those provinces (never our capital)
 * and makes it our vassal. Returns the provinces handed over (0 none). */
int    diplo_vassal_candidates(const Diplo *d, int c, short *out, int max);
int    diplo_release(Diplo *d, int c, int t, long today);
int    diplo_milally(const Diplo *d, int c);           /* military alliance index of c, -1 */

/* diplomatic news for the player (NOTE_DIPMSG, prov = one of these; diplo_msg_type gives the message type) */
enum { DM_INSULT, DM_WARN, DM_EMBARGO, DM_ALLOWTRADE, DM_CANCACC, DM_DISHONOR, DM_VASBREAK, DM_CLAIM, DM_GUARANTEE, DM_WEDISHONOR, DM_N };
const char *diplo_msg_type(int dm);
typedef void (*DiploLogFn)(void *user, long today, const char *text);
/* a proposal of the AI to the human player, as the game's message type (ROYMARPROP, MILALLPROP, MILALLINVPROP,
 * MILALLJOINPROP, WPINVPROP): return 1 to accept. Without it the player declines. */
typedef int (*DiploAskFn)(void *user, const char *type, int from, int against, int third);
void   diplo_set_ask(Diplo *d, DiploAskFn fn, void *user);
void   diplo_set_aggr(Diplo *d, int aggr);              /* the AI-aggressiveness option 0..4 ([[0x8c0bfc]+0x2c6], NIEPEWNE) */
void   diplo_set_log(Diplo *d, DiploLogFn fn, void *user);
void   diplo_save(const Diplo *d, FILE *f);           /* amieu2_diplo: the RNG */
void   diplo_restore(Diplo *d, const SNode *root);
#endif
