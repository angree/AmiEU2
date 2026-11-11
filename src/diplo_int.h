/* diplo_int.h - the inside of Diplo, shared by diplo.c and ai.c (the AI, docs/SPEC_AI.md). Not for
 * other modules. */
#ifndef EU2_DIPLO_INT_H
#define EU2_DIPLO_INT_H
#include "diplo.h"
#include "ai.h"

/* religion.csv row order: 0 protestant 1 reformed 2 counterreform 3 catholic 4 orthodox (Christian
 * 0..4), 5 sunni 6 shiite, 7 pagan, 8 confucianism 9 buddhism 10 hinduism */
enum { R_PROT, R_REF, R_COUNTER, R_CATH, R_ORTH, R_SUNNI, R_SHIITE, R_PAGAN, R_CONF, R_BUDD, R_HIND };
enum { PACT_GUARANTEE = 1, PACT_WARNING = 2 };
int diplo_cb(const Diplo *d, int a, int b);              /* 0x4ce3b0: the casus belli of a war of a on b */
#define CHRISTIAN(r) ((r) >= 0 && (r) <= R_ORTH)
#define MUSLIM(r)    ((r) == R_SUNNI || (r) == R_SHIITE)

struct Diplo {
	struct AiState *ai;                     /* ai.c: per-country AI state (mode, worst enemy, plan clocks) */
	World *w; Military *m; Peace *p;
	unsigned long seed;
	DiploLogFn log; void *loguser;
	long today;
	signed char rel_idx[MAX_COUNTRY];       /* religion row per country, -1 unknown */
	unsigned char atwar[MAX_COUNTRY];       /* at war with anyone (this month) */
	long men[MAX_COUNTRY];                  /* live land troops (this month) */
	unsigned char nb[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];
	unsigned char dyn[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];   /* royal marriage pairs (this month) */
	short milally[MAX_COUNTRY];              /* military alliance index, -1 (one per country, §3.2) */
	short eur_cont;                          /* the continent id "Europe" + 1 (0 not found) */
	long gs_epoch;                           /* country+0x14de, the share of a country's border contacts with each neighbour */
	unsigned char gn[MAX_COUNTRY];           /* (0x43ae2c): per country up to 24 neighbours (gx) and shares x1000 (gv), */
	short gx[MAX_COUNTRY][24], gv[MAX_COUNTRY][24];   /* recomputed when the owners change (owner_epoch) */
	unsigned char aiwar[MAX_COUNTRY];        /* .ai `war` 0..100 (warmonger), 50 when unknown */
	unsigned char aicombat[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];   /* .ai `combat = { tags }` targets */
	unsigned char aiexp[MAX_COUNTRY], aimono[MAX_COUNTRY], aifero[MAX_COUNTRY];   /* .ai expansion, monopoly, ferocity */
	unsigned char ncombat[MAX_COUNTRY];      /* number of .ai combat targets */
	int opt_aggr;                            /* the AI-aggressiveness option ([[0x8c0bfc]+0x2c6], NIEPEWNE) */
	int nmilally;                            /* military alliances in the world ([0x8ff60a], NIEPEWNE) */
	int no_wars;                             /* [0x8d0d79]: the AI declares no wars (tests) */
	DiploAskFn ask; void *ask_user;          /* proposals to the human player */
	long pcd[MAX_COUNTRY];                   /* day of the player's last diplomat to each country (not saved, NIEPEWNE) */
	struct { short lender, borrower, rate, months; long amount_m, due; } loan[32];   /* loans between countries (saved) */
	struct { short a, b; unsigned char kind; long until; } pact[96];   /* PACT_*: a guarantees / warned b until the day (saved) */
	int npact;
	int nloan;
};


/* diplo.c helpers (prefixed so ai.c can use them) */
#define drand dp_rand
#define logd dp_log
#define bit dp_bit
#define setbit dp_setbit
#define neighbours dp_nb
#define refresh dp_refresh
#define same_religion dp_same_rel
#define marriage_ok dp_marriage_ok
#define dyn_index dp_dyn_index
#define vassal_index dp_vassal_index
#define ally_remove dp_ally_remove
#define ally_drop_member dp_ally_drop
int  drand(Diplo *d);
void logd(Diplo *d, const char *fmt, ...);
int  bit(const unsigned char *m, int a, int b);
void setbit(unsigned char *m, int a, int b);
int  neighbours(const Diplo *d, int a, int b);
void refresh(Diplo *d);
void refresh_ties(Diplo *d);
int  same_religion(int ra, int rb);
int  marriage_ok(int ra, int rb);
int  dyn_index(const World *w, int a, int b);
int  vassal_index(const World *w, int lord, int v);
void ally_remove(World *w, int i);
void ally_drop_member(World *w, int i, int c);
int  vassal_ok_ai(const Diplo *d, int a, int b);     /* the vassalisation conditions of §3.2 */
int  annex_ok_ai(const Diplo *d, int a, int b);      /* the annexation conditions of §3.2 */
int  diplo_has_pact(const Diplo *d, int a, int b, int kind);
void diplo_annex(Diplo *d, int lord, int v);
void diplo_vp_join(Diplo *d, int a, int b, int type);   /* VP of a new tie (+2 / +20 / +5) */         /* v's provinces and units go to lord */
int  diplo_ai_action(Diplo *d, int a, int b, int act);   /* an AI country runs a player action (no cooldown of the player's) */
int diplo_gshare(const Diplo *d, int c, int x);   /* country+0x14de[x] of c, x1000 */
#endif
