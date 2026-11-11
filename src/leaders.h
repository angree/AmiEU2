/* leaders.h - historical generals (DB/leaders/leaders.<tag>, category general). A leader serves from
 * his startdate to his deathdate. In the original the player puts leaders on armies; here every
 * country's active generals, best rank first, lead its largest armies (NIEPEWNE: ours, the AI's own
 * rule was not read). The stats are copied into the army (LandUnit l_*), which the battle reads
 * (SPEC_WAR_DIPLOMACY 7.5) and the save keeps. Portable C. */
#ifndef EU2_LEADERS_H
#define EU2_LEADERS_H
#include "world.h"
#include "pak.h"

/* type: the DB category (docs/SPEC_EXPLORERS.md 1.2): 0 general, 2 conquistador, 3 explorer (privateers read
 * as explorers, as the exe does); admirals and monarchs are not loaded yet */
enum { LT_GENERAL = 0, LT_ADMIRAL = 1, LT_CONQ = 2, LT_EXPLORER = 3 };
typedef struct { const char *name; Date start, death; unsigned char rank, move, fire, shock, siege, type, dormant; short country, location; long id; } Leader;   /* dormant: only an event wakes him */
typedef struct Leaders Leaders;

Leaders *lead_load(const World *w, Pak *text);
void     lead_free(Leaders *l);
void     lead_swap(Leaders *l, int a, int b);
int      lead_active(const Leaders *l, const World *w, long id);             /* after world_swap_identity */
void     lead_assign(const Leaders *l, World *w);        /* monthly: generals onto the largest armies */
const Leader *lead_get(const Leaders *l, int id);        /* id = LandUnit.l_id - 1 */
/* conquistadors and explorers arrive with a unit of their own (SPEC_EXPLORERS 2.2): a new army of 1000 /
 * a new fleet of 3 ships at their location or the capital. newgame: everyone active now; otherwise
 * those whose start date fell in the month that just ended (stateless, so a loaded game agrees). */
struct Military;
void     lead_spawn(const Leaders *l, World *w, struct Military *m, int newgame);
/* a randomly generated leader (SPEC_EXPLORERS 1.5) has no record: his stats live in the unit, l_id = LEAD_RANDOM */
#define LEAD_RANDOM 30000
#endif
