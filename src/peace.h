/* peace.h - war score and peace (docs/SPEC_WAR_DIPLOMACY.md §5), truces, peace badboy.
 * The war score follows the exe (§5.2/§5.3). What the AI offers and accepts is NIEZNANE in the exe;
 * the rules here are our own and marked so in peace.c. Wars with a human member end only by the
 * automatic white peace (§5.4) until the peace window exists. Portable C, no floats. */
#ifndef EU2_PEACE_H
#define EU2_PEACE_H
#include "world.h"
#include "script.h"
#include <stdio.h>

typedef struct Peace Peace;

Peace *peace_new(World *w, long today, unsigned long seed);
void   peace_free(Peace *p);
void   peace_month(Peace *p, long today);               /* AI peace decisions, automatic white peace */

int  peace_prov_value(const World *w, int prov);         /* §5.3, war-score percent */
int  peace_war_score(const World *w, int war);           /* attackers' view, -100..+100 (§5.2) */
int  peace_side(const World *w, int war, int country);   /* 1 attacker, 2 defender, 0 not in it */
int  peace_truce(const Peace *p, int a, int b, long today);   /* 1 = truce in force */
long peace_last(const Peace *p, int a, int b);                /* day of the last peace between a and b (country+0x8ee), 0 none in 37 years */
void peace_truces_of(const Peace *p, int c, long today, unsigned char *flags);   /* flags[x] = 1 for c's truces */

/* Ends war `war` in favour of side `winner` (1 attackers, 2 defenders, 0 white peace): the provinces
 * in provs[] change owner, gold_m moves from the losing leader to the winning one. */
void peace_make(Peace *p, int war, int winner, const short *provs, int nprovs, long gold_m, long today);

typedef void (*PeaceLogFn)(void *user, long today, const char *text);
void peace_set_log(Peace *p, PeaceLogFn fn, void *user);

/* the terms a winner (side 1/2) gets for `score` percent: provinces into take[] (count returned),
 * cash in *gold_m (25 ducats per percent not covered by provinces, at most half the loser's treasury) */
int  peace_terms(Peace *p, int war, int winner, int score, short *take, int max, long *gold_m);
int  peace_find_war(const World *w, int a, int b);
void peace_leave_all(Peace *p, int c, long today);       /* COUNTRY_BROKEN: c leaves all its wars */       /* war with a and b on opposite sides, or -1 */

/* a winning AI leader offers the human leader these terms; return 1 to accept */
typedef int (*PeaceOfferFn)(void *user, int war, int sender, int winner, const short *provs, int nprovs, long gold_m);   /* sender: who offers */
void peace_set_offer(Peace *p, PeaceOfferFn fn, void *user);

enum { PEACE_WHITE, PEACE_DEMAND, PEACE_SURRENDER };
enum { PEACE_ACCEPTED = 1, PEACE_REFUSED = 0, PEACE_NOWAR = -1, PEACE_NOTLEADER = -2 };
int  peace_player_offer(Peace *p, int war, int player, int kind, long today);

/* the peace window (iw_peace, SPEC_UI §4.12): the player builds an offer. mode PO_DEMAND = we receive,
 * PO_OFFER = we give. Provinces from peace_candidates; money in ducats x1000; the three demands only
 * when demanding. The value is in war-score percent: provinces by §5.3, 25 ducats a percent, the
 * demands vassal 40 / religion 20 / access 5 (ours, NIEPEWNE). The answer (ours, NIEPEWNE): a demand
 * is accepted when it does not exceed our war score, an offer when the score plus the offer is at
 * least -10 (the white-peace margin); No Tribute is the white peace. */
enum { PO_DEMAND = 1, PO_OFFER = 2 };
#define PO_MAXPROV 32
typedef struct { int mode; short prov[PO_MAXPROV]; int nprov; long gold_m; int vassal, religion, access;
                 int sep_with; } PeaceOffer;   /* sep_with = country + 1: a separate peace between the player and it (0 none) */
int  peace_candidates(Peace *p, int war, int player, int mode, short *out, int max);   /* offerable provinces */
int  peace_offer_value(const World *w, const PeaceOffer *o);
int  peace_player_propose(Peace *p, int war, int player, const PeaceOffer *o, long today);   /* PEACE_* */
/* separate peace (the peace window's shields, windows3 4.2): a member who is not its side's leader makes
 * peace for itself, or the leader settles with one enemy member alone. Only the two countries exchange
 * provinces and money; the one that is not a leader leaves the war (a side left empty ends it). The
 * score used is the war's (NIEPEWNE: the exe recomputes it for the pair). */
int  peace_separate_ok(const World *w, int war, int player, int them);   /* the pair may settle apart */
int  peace_candidates_sep(Peace *p, int war, int player, int them, int mode, short *out, int max);
int  peace_can_annex(Peace *p, int war, int player);    /* page 2: crushing victory over a leader we hold the capital of */
int  peace_player_annex(Peace *p, int war, int player, long today);
/* the AI at war (0x53f240): does country u offer peace this month? 1 = an offer went out */
int  peace_ai_think(Peace *p, int u, long today);
struct Military;
void peace_set_military(Peace *p, struct Military *m);
/* what the AI part needs from diplo.c: ferocity per country, the strategic mode, neighbours, badboy limit */
void peace_set_strength(Peace *p, long (*fn)(void *, int, int), void *user);   /* FS / FN from ai.c */
void peace_set_ai(Peace *p, const unsigned char *fero, int (*mode)(void *, int), int (*nb)(void *, int, int), int (*over)(void *, int), void *user);

void peace_save(const Peace *p, FILE *f);               /* amieu2_peace: truces, offer clock, RNG */
void peace_restore(Peace *p, const SNode *root);
#endif
