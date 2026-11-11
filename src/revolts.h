/* revolts.h - revolt risk and rebels (docs/SPEC_WAR_DIPLOMACY.md §13). The rebels are the country REB,
 * at war with everyone (military.c), so their armies fight and besiege with the normal rules; beaten
 * rebels have nowhere to retreat and are destroyed ("never retreat"). Portable C. */
#ifndef EU2_REVOLTS_H
#define EU2_REVOLTS_H
#include <stdio.h>
#include "world.h"
#include "military.h"
#include "script.h"
#include "pak.h"

int  rev_risk(const World *w, const Military *m, int prov);   /* §13.1, 0 for colonies and empty land */
int  rev_month(World *w, Military *m);                         /* the monthly rolls; returns new revolts */
int  rev_roll_country(World *w, Military *m, int c, int bonus);
struct Peace;
struct Diplo;
void rev_set_peace(struct Peace *p);                           /* for COUNTRY_BROKEN (leaving the wars) */
void rev_set_diplo(struct Diplo *d);                           /* a new revolter declares war through it */
int  rev_load(World *w, Pak *t);                               /* db/revolt.txt (SPEC_REVOLTERS 0); 0 = ok */
void rev_day(World *w, long today);
int  rev_spawn(World *w, Military *m, int prov);               /* rebels rise in prov (events revolt / religiousrevolt / colonialrevolt) */
int  rev_release(World *w, int tag, int from);
void rev_swap(int a, int b);                                    /* after world_swap_identity */                  /* events independence: tag released with its cores */                            /* keeps Province.ctl_day (the last change of controller) */
void rev_seed(unsigned long s);
void rev_save(FILE *f);                                        /* amieu2_revolts: the RNG */
void rev_restore(const SNode *root);
#endif
