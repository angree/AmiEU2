/* monarchs.h - historical monarchs (DB/monarchs/monarchs.<tag>): the ruler on the throne at a date
 * and his ADM / DIP / MIL. The reign runs from a monarch's startdate to the next one's (the files'
 * deathdate is the same day). A country without a monarch file or before its first monarch gets 3/3/3,
 * the value eu2.exe uses without a monarch. Derived from the date, so nothing is saved. Portable C. */
#ifndef EU2_MONARCHS_H
#define EU2_MONARCHS_H
#include "world.h"
#include "pak.h"

typedef struct { const char *name; Date start; unsigned char adm, dip, mil, dormant; long id; } Monarch;   /* dormant = yes: only an event wakes him */
typedef struct Monarchs Monarchs;

Monarchs *mon_load(const World *w, Pak *text);          /* every country's file it finds */
void      mon_free(Monarchs *m);
void      mon_swap(Monarchs *m, int a, int b);           /* after world_swap_identity */
const Monarch *mon_current(const Monarchs *m, int country, Date now);   /* NULL = none (file data only) */
/* with the events' wakemonarch / sleepmonarch of World.monev: a woken monarch reigns from that day, a
 * slept one never (SPEC_EVENTS 9 code 0x21 / 0x42) */
const Monarch *mon_current_w(const Monarchs *m, const World *w, int country, Date now);
int       mon_list(const Monarchs *m, int country, const Monarch **out, int max);   /* the whole line, in order */
#endif
