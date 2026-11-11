/* religion.h - the religion window's rules (docs windows4 §2): religion ids 0..10, which conversions
 * are allowed, which tolerance sliders a state religion shows, the linked sliders (their sum stays 0),
 * Defender of the Faith. Tolerance is Country.tol[] x10 (-100..100). Portable C. */
#ifndef EU2_RELIGION_H
#define EU2_RELIGION_H
#include "world.h"

enum { REL_PROTESTANT, REL_REFORMED, REL_COUNTER, REL_CATHOLIC, REL_ORTHODOX, REL_SUNNI, REL_SHIITE, REL_PAGAN,
       REL_CONFUCIAN, REL_BUDDHIST, REL_HINDU, REL_N };

int  rel_index(const World *w, short name);               /* 0..10, -1 not one of the eleven */
short rel_name(const World *w, int r);                    /* the interned name of religion r, -1 none */
int  rel_exists(const World *w, int r);                   /* 0x4310f0: the reformation religions by the world flags 1 / 2 / 3 (and not 4) */
int  rel_targets(const World *w, int s, int *out);        /* allowed conversions from state religion s (0x45fc40, fantasy off) */
int  rel_shown(const World *w, int s, int *out);          /* the other tolerance sliders shown, in order (0x51a8e7) */
const char *rel_label(int s, int r);                      /* text key: the religion, or MOSLEM / CHRISTIAN for a group */
void rel_set_tol(World *w, int c, int r, int v);          /* set one slider (x10) and spread the difference over the shown others */
int  rel_defender(const World *w, int r);                 /* the country defending faith r, -1 none */
int  rel_can_defend(const World *w, int c);               /* 0x460340: a Christian / Muslim faith without a defender, 1000 ducats */
int  rel_claim_defender(World *w, int c);                 /* 1 done */
int  rel_convert(World *w, int c, int r, long today);     /* 1 done (0x4536c0): stability -5, badboy +1, relations, alliances, tolerance reset; not at war */
void rel_ai_month(World *w, long today);                  /* the AI's conversion (0x556480) and Defender of the Faith (0x557b3a) */
#endif
