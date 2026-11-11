/* events.h - the EU2 event system, implemented from docs/SPEC_EVENTS.md. Portable C, no floats.
 * Dated events go through a day-sorted queue (offset applied once at load, retries every 16-74
 * days until deathdate); every existing country runs one random-event chain; the AI picks actions
 * 85/5/5/5. Commands that touch state the world model does not have yet are counted, not applied. */
#ifndef EU2_EVENTS_H
#define EU2_EVENTS_H
#include "pak.h"
#include "world.h"
#include "script.h"
#include <stdio.h>

typedef struct Events Events;

/* loads db/events.txt and the files it lists; today = current day number (calendar.h) */
Events *events_load(World *w, Pak *text, long today, unsigned long seed, char *err, int errlen);
void    events_free(Events *e);
/* runs everything due up to and including 'today' */
void    events_day(Events *e, long today);

/* fired-event callback for logs/UI: country index, event id, action chosen (0..3), name key */
typedef void (*EventLogFn)(void *user, long today, int country, long id, int action, const char *name);
void    events_set_log(Events *e, EventLogFn fn, void *user);

/* the player's events: called (modally) when an event fires for a human country; returns the
 * chosen action 0..3 (an action whose name is NULL does not exist; other values = the AI pick) */
typedef struct { long id; const char *name, *desc, *action[4]; short province; } EventInfo;   /* text.csv keys */
typedef int (*EventChoiceFn)(void *user, long today, int country, const EventInfo *ev);
void    events_set_choice(Events *e, EventChoiceFn fn, void *user);

/* save game: an amieu2_events block (happened events, flags, the message queue, the RNG) */
void    events_save(const Events *e, FILE *f);
void    events_restore(Events *e, const SNode *root);

typedef struct {
	int events, random, dated, files;
	long fired, fired_random, retries, dropped_cmds, unimplemented_cmds, executed_cmds, cap_hits;
} EventStats;
void    events_stats(const Events *e, EventStats *s);
void    events_set_kinds(Events *e, int kinds);   /* bit 0 random, bit 1 historical (default both) */
/* the unit commands (SPEC_EVENTS 2b-30, 1e/1f): kind 0 infantry, 1 cavalry, 2 artillery (men / guns), 3 warships,
 * 4 galleys, 5 transports (ships), 6 conquistador, 7 explorer; the game creates them (without it they count as
 * not implemented, as in the host tests) */
typedef void (*EventUnitFn)(void *user, int country, int prov, int kind, long value);
void    events_set_units(Events *e, EventUnitFn fn, void *user);
/* commands that need other modules (SPEC_EVENTS 9): the game applies them; without a hook they count as
 * not implemented. EVC_NEIGHBOUR is a question (1 = a and b border each other), the others act. */
enum { EVC_REVOLT, EVC_RELREVOLT, EVC_COLREVOLT, EVC_INHERIT, EVC_WAR, EVC_NEIGHBOUR, EVC_MONARCH, EVC_SLEEPMONARCH,
       EVC_LEADER, EVC_SLEEPLEADER, EVC_INDEPENDENCE, EVC_COUNTRY,
       EVC_IS_MONARCH, EVC_IS_LEADER };     /* trigger questions: monarch `which` reigns in country / leader `which` is active */
typedef int (*EventCmdFn)(void *user, int code, int country, long which, long value);
void    events_set_cmds(Events *e, EventCmdFn fn, void *user);
void    events_swap_countries(Events *e, int a, int b);   /* after world_swap_identity */
/* an action's commands for the choice popup's effect list (windows4 3.2): the event-file keyword, which, value */
typedef struct { const char *kw; long which, value; const char *str; } EventEffect;
int     events_action_effects(const Events *e, long id, int action, EventEffect *out, int max);
const char *events_name_of(const Events *e, long id);   /* the name key of event id, NULL none */
#endif
