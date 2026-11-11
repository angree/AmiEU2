/* missions.h - the player's missions (DB/Missions.csv, docs/SPEC_MISSIONS.md): offers of up to one row
 * per type for the current year, accepted with a five-year limit, judged at once or at the deadline,
 * VP on success, int(0.66 x VP) off on failure. All eleven types are offered. AI countries get no missions here (NIEPEWNE:
 * the original offers them to every tracked country). Portable C. */
#ifndef EU2_MISSIONS_H
#define EU2_MISSIONS_H
#include <stdio.h>
#include "world.h"
#include "pak.h"
#include "script.h"

#define MIS_TYPES 11
typedef struct { short tag, code, start, end, x; short area; } MisRow;   /* x: province id or country index; area: interned name, -1 */
typedef struct { short row; long deadline; } MisCur;
typedef struct Missions Missions;

Missions *mis_load(const World *w, Pak *text);
void      mis_free(Missions *m);
void      mis_month(Missions *m, World *w, int player, long today);   /* offers, judgements, VP; notes for the player */
int       mis_offers(const Missions *m, short *rows, int max);           /* available rows */
int       mis_current(const Missions *m, MisCur *cur, int max);
const MisRow *mis_row(const Missions *m, int row);
int       mis_done(const Missions *m);                                  /* missions completed so far */
int       mis_vp(int code);                                             /* base VP of a type (penalty = 66 % of it) */
int       mis_accept(Missions *m, int row, long today);                  /* 0 ok */
void      mis_title(const Missions *m, const World *w, int row, char *out, int n, int desc);   /* MISSION_TITLE_x / DESC_x */
void      mis_save(const Missions *m, const World *w, FILE *f);
void      mis_restore(Missions *m, const World *w, const SNode *root);
void      mis_reset(Missions *m);                                         /* a new game */
#endif
