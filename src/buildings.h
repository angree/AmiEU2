/* buildings.h - province improvements (docs/SPEC_BUILDINGS.md): the three officials (tax collector,
 * chief judge, governor), shipyard, conscription centre and the five manufactories. Costs, times and
 * requirements as decoded; the manufactory cost formula is only roughly known (NIEPEWNE: ours).
 * Built through the build queue (Build.ship = BK_*), one improvement per province at a time. The
 * effects live in economy.c / revolts.c (SPEC_TIME_ECONOMY 3.2). Portable C. */
#ifndef EU2_BUILDINGS_H
#define EU2_BUILDINGS_H
#include "world.h"
#include "military.h"

/* Province.bflags (the city flags +0x68..+0x6c) */
#define PB_SHIPYARD   0x01
#define PB_CONSCRIPT  0x02
#define PB_TAXCOL     0x04
#define PB_GOVERNOR   0x08
#define PB_JUDGE      0x10
enum { BK_TAXCOL = 10, BK_JUDGE, BK_GOVERNOR, BK_SHIPYARD, BK_CONSCRIPT, BK_MANU1, BK_MANU5 = BK_MANU1 + 4 };   /* build queue kinds */
enum { BLD_OK = 0, BLD_BADPROV = -1, BLD_MONEY = -2, BLD_TECH = -3, BLD_BUSY = -4, BLD_HAVE = -5, BLD_FULL = -6 };

long bld_cost(const World *w, int c, int prov, int kind);       /* ducats x1000 */
int  bld_months(int kind);
int  bld_can(const World *w, const Military *m, int c, int prov, int kind);
int  bld_start(World *w, const Military *m, int c, int prov, int kind, long today);
void bld_done(World *w, const Build *b);                       /* builds_day: the work is finished */
int  bld_manu_count(const World *w, int c);
void bld_ai_month(World *w, const Military *m, long today);   /* the AI builds */
#endif
