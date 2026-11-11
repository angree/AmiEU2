/* military.h - land movement, battles and sieges (docs/SPEC_WAR_DIPLOMACY.md §7-§11), v1:
 * no leaders, no attrition, no naval combat, no supply; wars come from the scenario. Portable C,
 * fixed point (men are integers, morale x1000, strength x1000). */
#ifndef EU2_MILITARY_H
#define EU2_MILITARY_H
#include "pak.h"
#include "world.h"
#include "script.h"
#include <stdio.h>

typedef struct Military Military;

Military *mil_load(World *w, Pak *text, Pak *map, unsigned long seed, char *err, int errlen);
void      mil_free(Military *m);
void      mil_day(Military *m, long today);            /* movement, battles, sieges */
void      mil_month(Military *m);                      /* morale recovery */
int       mil_at_war(const World *w, int a, int b);
/* orders the land unit to walk to dest; returns the travel time in days or -1 (no path) */
long      mil_order_move(Military *m, int unit, int dest, long today);
void      mil_ai_colonial(Military *m, int country, int start);   /* the colonial agent's explore plans (SPEC_AI_EXPLORE 2.1) */
long      mil_order_append(Military *m, int unit, int dest, long today);   /* Shift + right click: a waypoint after the current order */
int       mil_land_unit_at(const World *w, int prov, int owner);   /* first live unit, or -1 */
void      mil_neighbour_bits(const Military *m, unsigned char *bits);  /* (MAX_COUNTRY^2+7)/8 bytes */
void      mil_prof(long ms[6]);   /* time spent: move, build+battles, rebuild, sieges, AI (ms, all instances) */
void      mil_save(const Military *m, FILE *f);           /* amieu2_military: the RNG */
void      mil_restore(Military *m, const SNode *root);

/* recruitment: kind 0 = 1000 infantry, 1 = 1000 cavalry, 2 = 10 artillery, in an own, controlled city */
enum { MIL_RC_OK = 0, MIL_RC_BADPROV = -1, MIL_RC_MONEY = -2, MIL_RC_MANPOWER = -3, MIL_RC_FULL = -4 };
long      mil_recruit_cost(const Military *m, int country, int kind);     /* ducats x1000 */
/* what the purchase window offers in a province (windows4 1.3): army - men (artillery: cannons) that can still
 * be raised there: tax x 1000, x2 with a conscription centre, halved unless a core, capped by the manpower
 * pool, less what is queued; navy - ships (tax x 1000, x10 with a shipyard, at least 5000, per 5000) */
long      mil_recruit_avail(const Military *m, int country, int prov, int kind);
long      mil_max_morale(const Military *m, int country);                 /* x1000 (§8) */
/* the land / naval level values of the country (x1000 of the csv / 100): land[6] = inf fire, cav fire, art fire, inf shock,
   cav shock, art shock; nav[4] = warship fire, warship shock, galley fire, galley shock; mor[2] = land / naval maximum morale */
void      mil_strength_inputs(const Military *m, int country, long *land, long *nav, long *mor);
/* fleets: sail order to a sea zone or a port (returns days, -1 no route); first fleet of owner there */
long      mil_order_fleet(Military *m, int fleet, int dest, long today);
int       mil_fleet_at(const World *w, int prov, int owner);
int       mil_recruit(Military *m, int country, int prov, int kind, long today);
/* ships in an own port: kind 0 warship, 1 galley, 2 transport (one per order); MIL_RC_* */
long      mil_ship_cost(const Military *m, int country, int kind);        /* ducats x1000 */
long      mil_ship_days(const Military *m, int country, int kind);   /* 0x438298: warships 9 + Low Wind months, galleys 5, transports 6 */
int       mil_build_ship(Military *m, int country, int prov, int kind, long today);
int       mil_is_port(const World *w, int prov);
int       mil_besieger(const Military *m, int prov);           /* the country besieging prov, -1 none */
long      mil_army_weight(const LandUnit *u);                 /* x1000 (sea transport) */
long      mil_fleet_capacity(const NavalUnit *f);             /* x1000: warship 1, galley 0.5, transport 2 */
long      mil_fleet_load(const World *w, int fleet);          /* x1000 carried now */
int       mil_embark(Military *m, int army, int fleet);       /* MIL_RC_*: the army boards the fleet next to it */
int       mil_disembark(Military *m, int fleet, int prov);    /* carried armies land in prov next to the fleet: count */
long      mil_supply(const Military *m, int prov, int country);      /* supply limit (§10.1) */
long      mil_attrition(const Military *m, int prov, int country);   /* maximum attrition, percent (§10.2) */
int       mil_adjacent(const Military *m, int prov, short *out, int max);   /* neighbours (ADJACENT.TBL) */
/* fortress (SPEC_WAR_DIPLOMACY §9.5): 100 ducats x (1 + inflation), level capped by land tech;
 * the work takes FORT_DAYS (ours, NIEPEWNE). mil_fort_max = the cap for the country now. */
#define FORT_DAYS 360
long      mil_fort_cost(const Military *m, int country);
int       mil_fort_max(const World *w, int country);
int       mil_build_fort(Military *m, int country, int prov, long today);   /* MIL_RC_*, MIL_RC_TECH when capped */
enum { MIL_RC_TECH = -5 };

/* what the battle window shows (iw_battle, SPEC_UI §4.11): side 0 = the attacker (left), 1 = the defender */
typedef struct {
	int kind;                 /* MBV_NONE / MBV_LAND / MBV_SIEGE */
	int country[2];           /* leading country of each side, -1 none */
	long n[2][3];             /* infantry, cavalry, artillery (siege: the defender's units in the city) */
	long morale[2], maxmor;   /* x1000; maxmor = the highest maximum of the two */
	long power[2];            /* fire (fire phase) or shock strength x1 */
	int phase;                /* 0 fire, 1 shock */
	int roll[2];              /* the die + modifiers each side rolls this phase */
	int cav_adv[2];           /* the side has the cavalry advantage */
	long attr[2];             /* attrition, percent */
	int terrain;
	int fort, distress, breaches, rating, hold;   /* siege: fort level, siege state, breaches, artillery rating 0..3, fort - starvation - distress */
	int nstack[2];            /* armies on each side (the window draws up to 3 figures) */
} MilBattleView;
enum { MBV_NONE = 0, MBV_LAND = 1, MBV_SIEGE = 2, MBV_NAVAL = 3 };   /* naval: n[s] = warships, galleys, transports */
int       mil_battle_view(const Military *m, int prov, MilBattleView *v);
enum { MSB_NAVAL = 1, MSB_SHOCK = 2, MSB_ASSAULT = 4 };
int       mil_sound_battles(const Military *m, int c, short *prov, unsigned char *info, int max);   /* SPEC_SOUND 4 */
/* natives (docs/SPEC_NATIVES.md): all of p's natives become a NAT army there (4.1); 1 = an army came out */
int       mil_native_attack(Military *m, int prov);
int       mil_native_style(const World *w, int prov);   /* 2.1: 0..9 */   /* kind; MBV_NONE when nothing goes on there */

/* the army / fleet window's commands (SPEC_UI 4.10: SPLIT_ARMY, MERGE, DISBAND). Split: a second unit with
 * half of each arm, in the same place, no leader; merge: every other own unit standing in the same place
 * joins this one (morale weighted by men); disband: the unit is gone. Not while it fights or is aboard.
 * The return is the new / kept unit, or -1. Ours where the exe's rules were not read (NIEPEWNE: the
 * split ratio, who keeps the leader, what disbanding gives back - nothing here). */
/* the siege window's buttons (SPEC_UI_WINDOWS3 2): detach a force big enough to go on besieging (5000 x the
 * fort level) or to cover the fortress (1000 x), as a new unit in the place (-1 when the army is not bigger);
 * assault (SPEC_WAR 9.4, the rule ours - NIEPEWNE: a garrison of 1000 x the fort level, d10 + artillery
 * rating + the general's siege - the fort level without a breach, taken on 6+; losses from the garrison
 * size; success takes the province and lowers the fort one level, never below 1) */
enum { MIL_AS_OK = 1, MIL_AS_FAILED = 0, MIL_AS_TECH = -1, MIL_AS_NOINF = -2, MIL_AS_NOSIEGE = -3, MIL_AS_MORALE = -4 };   /* OK: the assault battle has started */
int       mil_detach(Military *m, int army, int cover);
int       mil_assault(Military *m, int army);
int       mil_split(Military *m, int army);
int       mil_merge(Military *m, int army);            /* units merged in */
int       mil_merge_count(const Military *m, int army); /* other own units that could join */
void      mil_disband(Military *m, int army);
int       mil_split_fleet(Military *m, int fleet);
int       mil_merge_fleet(Military *m, int fleet);
int       mil_merge_fleet_count(const Military *m, int fleet);
void      mil_disband_fleet(Military *m, int fleet);

/* a new unit (a leader's arrival, SPEC_EXPLORERS 2.2): in a free slot; index or -1 */
/* mercenaries (docs battle_merc 4): one market for everyone; a company a month while there are fewer than
 * 6 (before 1500) / 5 (to 1699) / 1 (from 1700) + the wars going on; 3000-8000 infantry, 500-2000 cavalry,
 * from 1500 a 40 % chance of 5 / 10 / 15 guns; price (0.02 inf + 0.04 cav + 6.5 art) x (1 + inflation);
 * only for a country whose capital is in Europe, raised in a province on the capital's continent. The AI
 * never hires. */
int       mil_merc_offer(const Military *m, long *inf, long *cav, long *art);   /* the first company, 0 none */
long      mil_merc_price(const Military *m, int country);                      /* ducats x1000, 0 none */
int       mil_merc_access(const Military *m, int country, int prov);
int       mil_hire_merc(Military *m, int country, int prov);                   /* army index or MIL_RC_* (< 0) */
int       mil_new_army(Military *m, int country, int prov, long infantry, const char *name);
int       mil_new_fleet(Military *m, int country, int prov, int warships, const char *name);

typedef void (*MilLogFn)(void *user, long today, const char *text);
void      mil_set_log(Military *m, MilLogFn fn, void *user);
/* the province's monthly trade value x1000 (0x480e30, the economy's), for the pirates' zone score; NULL = 0 */
typedef long (*MilTradeFn)(void *user, int prov);
/* "Commission Privateers" in sea zone z (SPEC_PIRATES 5): 0 possible, 1 too expensive, 2 pirates already there,
 * 3 not a naval power; cost x1000 into *cost_m */
int       mil_privateer_state(const Military *m, int c, int z, long *cost_m);
int       mil_privateers(Military *m, int c, int z);            /* the new pirate fleet, -1 none */
long      mil_naval_rate(const Military *m, int fleet);         /* the fleet's monthly attrition in percent of its ships (0x489470) */
void      mil_usage(const Military *m, int *battles, int *plans, int *generals);   /* tests: how full our fixed tables are */
void      mil_set_tradefn(Military *m, MilTradeFn fn, void *user);
void      mil_set_supportfn(Military *m, long (*fn)(void *, int), void *user);   /* the land support limit 0x45ab50 (econ_support_limit) */

/* the military AI (docs/SPEC_MILITARY_AI.md). The .ai file's army weights, x1000, set by diplo_load_ai
 * (default.ai's values until then): conquer { enemy supply distance owner notsupply base }, garrison
 * { fortress strategic size supply war }, the top-level base / front (gathering). */
enum { AIW_ENEMY, AIW_SUPPLY, AIW_DIST, AIW_OWNER, AIW_NOTSUP, AIW_BASE,
       AIW_GFORT, AIW_GSTRAT, AIW_GSIZE, AIW_GSUPPLY, AIW_GWAR, AIW_RBASE, AIW_RFRONT, AIW_N };
void      mil_set_ai_weights(Military *m, int c, const long *w);
/* what the diplomatic AI knows (it is saved there): what = 0 strategic mode (0 DOMINANCE, 1 BUILDUP,
 * 2 COLONISE), 1 the war target (worst enemy) or -1 */
typedef int (*MilAiQuery)(void *user, int c, int what);
void      mil_set_ai_query(Military *m, MilAiQuery fn, void *user);
#endif
