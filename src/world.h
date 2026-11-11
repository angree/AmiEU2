/* world.h - the game state: provinces, countries, units, date. Portable C, fixed point
 * (values marked _m are x1000). Filled from DB/<x>.csv and a scenario (.eug + includes); a save
 * game has the same format as a scenario. */
#ifndef EU2_WORLD_H
#define EU2_WORLD_H
#include "pak.h"

#define MAX_PROV     2100
#define MAX_COUNTRY   420
#define MAX_LAND     3000
#define MAX_NAVAL    1500
#define MAX_NAMES    1024     /* interned short names: religions, cultures, goods, areas ... */
#define MAX_COT       280
#define MAX_WAR        96            /* ours (a full table refuses a declaration); the soak peaks near 50 */
#define MAX_ALLY      384
#define MAX_BUILD     1024          /* ours: one shared queue (the exe keeps one per country); 128 throttled the whole world's recruiting */

typedef struct { short year; unsigned char month, day; } Date;   /* month 1..12, day 1..30 (EU2: 30-day months? NIEPEWNE) */

/* scenario `alliance` blocks: military alliance, vassalisation (member[0] = overlord, one vassal
 * per object), dynastic alliance = royal marriage (two members) */
enum { ALLY_MILITARY = 1, ALLY_VASSAL = 2, ALLY_DYNASTIC = 3 };
typedef struct { short member[24]; unsigned char n, type; Date expiry; } Alliance;

/* a land unit (or ships) being raised: appears in prov on day done (men; artillery in pieces) */
typedef struct { short country, prov, ship; long inf, cav, art, done; } Build;   /* ship 1..3 = warships/galleys/transports, count in inf */

/* per ordered country pair a -> b (dipflag[a][b]) */
#define DF_CB_PERM   1    /* a holds a permanent casus belli on b (SPEC_WAR_DIPLOMACY §1.2 +0x3f) */
#define DF_ACCESS    2    /* a grants b military access */
#define DF_NOTRADE   4    /* a refuses trade with b (embargo) */
#define DF_WARNED    8    /* a warned b (NIEPEWNE use) */
#define DF_TRADEAGR 16    /* a trade agreement between a and b (set both ways); merchants never compete */
#define DF_CB_TMP   32    /* a holds a casus belli on b until a day (World tcb; GIVECB 0xad with an expiry) */
#define DF_CB       (DF_CB_PERM | DF_CB_TMP)
typedef struct {
	short att[24], def[24]; unsigned char natt, ndef;   /* att[0] / def[0] lead the sides */
	Date began;                           /* scenario war date (year 0 = unknown) */
	long start_day, last_combat;          /* calendar day numbers; 0 = not set (peace_new fills it) */
	unsigned char bp_att[24][24];         /* battle points of att[i] against def[j], capped 95 (SPEC §5.1) */
	unsigned char bp_def[24][24];         /* battle points of def[j] against att[i]: [j][i] */
} War;
#define COT_MERCH      24

typedef struct {
	short prov;                           /* home province */
	short n;                              /* merchant entries */
	short tag[COT_MERCH];                 /* country index */
	unsigned char level[COT_MERCH];       /* merchants of that country (1..6) */
	long fail;                            /* the failure counter (+0x14) in 1/12000: 100.0 = 1200000 (save: failure) */
} Cot;


typedef struct {
	short id;
	const char *name;
	unsigned char kind;        /* PIW name column: 0 inland, 1 coastal, 2 sea, 3 other (NIEPEWNE) */
	unsigned char terrain, climate, hre, ice, storm;   /* ice / storm: the Ice? / Storm? columns (+0x9e / +0xa6) */
	unsigned char wind, storm_now, winter;    /* the month's weather (0x4825b0): wind 0..9 (+0xa2), storm (+0x248), winter (+0x3e2) */
	unsigned char tot;         /* province.csv ToT-SPA (1) / ToT-POR (2): the halves of the Treaty of Tordesillas */
	short religion, culture, goods, area, region, continent;   /* interned name ids, -1 none */
	long income_m, manpower_m, size_m;
	short city_x, city_y, army_x, army_y, port_x, port_y, manu_x, manu_y;
	short port_sea;            /* adjacent sea province for the port */
	short pirates;             /* idle pirate warships in port_sea (+0x84, SPEC_PIRATES 4.1): refreshed by the military every day */
	unsigned char blockaded;   /* +0x489: idle fleets hostile to the controller in port_sea, warships + galleys / 2 >= 5 (SPEC_PIRATES 4.4) */
	unsigned char besieged;    /* a siege is running (the PIW's siege picture); both refreshed with pirates */
	unsigned char hostile;     /* land units at war with the owner stand here (population growth -5); refreshed with pirates */
	unsigned char cot_nb;      /* neighbouring centres of trade (population growth +2 each) */
	unsigned char looted;      /* +0x3cc: plundered by an army out of supply, for a year (no tax / production) */
	short lgroup;              /* +0x498: its land mass (land-connected provinces), set by the military at load; -1 sea / unknown */
	unsigned char island;      /* every neighbour is sea or river (0x47e940's test), set by the military at load */
	long loot_until;           /* the day the loot ends (command 0x5e a year later) */
	/* scenario state */
	short owner, controller;   /* country index, -1 none */
	long population;
	long pop_frac_m;           /* fraction of a person, 1/1000 (growth must not freeze small cities) */
	long mine_value;           /* province.csv MineValue */
	unsigned char fort, capital, natives_present;
	short cores[8];            /* countries with a core here, -1 = free slot */
	long revoltrisk_m;         /* permanent province modifier (events) */
	long natives;
	unsigned char col_diff, ferocity;   /* province.csv "Difficulty for Colonization", natives' "Ferocity" x10 (+0x134, SPEC_NATIVES 1.1) */
	unsigned char tolerance, conq_done; /* natives' "Tolerance" 0..9 (+0x13c, the UI's aggressiveness); the conquistador test ran (+0x88) */
	short maxnatives;                   /* the natives' cap (+0x128), hundreds; `natives` is in hundreds too */
	unsigned char negot, tp;            /* "Negotiation Value for Trading Posts"; trading post level 1..6 (0 none) */
	signed char cot_hist;               /* province.csv "CoT Historical Modifier" (+0xb2), -35 when its CoT dissolves (saved: ch) */
	short tcot;                         /* home province of the CoT it trades in (+0xaa; 0 none, -1 not loaded; saved: tc) */
	unsigned char bflags, manu;         /* improvements (buildings.h PB_*), manufactory type 1..5 (0 none) */
	short acq_year, last_owner;         /* year the owner got it (P+0x3be; 0 = before the game), owner at the last monthly look */
	long ctl_day;                       /* day of the last change of controller (P+0x3c0; revolts.c rev_day keeps it) */
	short last_ctl;                     /* the controller rev_day saw last (saved as lc + 1, 0 = the controller) */
	short tsx[4], tsy[4]; unsigned char tsv[4];   /* landscape sprites: map position and variant (province.csv "Terrain x/Y/variant", x < 0 none) */
} Province;

typedef struct {
	char tag[4];
	short color, fantasia_color;          /* index into the colour scale names */
	short techgroup;
	char army_spr[4], navy_spr[4];        /* nationality sprite sets (country.csv ArmyNationalitySprite, ShipNationalitySprite) */
	char city_nat[4];                     /* country.csv City Nationality (province window building style) */
	signed char policy[8];                /* Aristocracy Centralisation Innovative Mercantilism Offensive Land Quality Serfdom (country.csv defaults) */
	unsigned char elector;
	/* scenario state */
	unsigned char present;                /* has a country block in the scenario */
	unsigned char major, ai_controlled;
	short religion, culture, capital;
	long treasury_m, merchants_m, colonists_m, diplomats_m;
	long inflation_u;                     /* inflation as a fraction x 1e6 (0.01 = 1 % = 10000) */
	short stability;
	short land_tech, naval_tech, infra_tech, trade_tech;
	short nprov_owned;
	long badboy_m, vp, manpower_m;      /* vp = int(the VP total) */
	long vpc_m[7], vpt_m;                 /* VP per category x1000 (VP_*), the total x1000 (country+0x20E6.., +0x20DE) */
	long inc_m, maint_m, col_rate_m;      /* this month (economy.c; saved for the AI): budget pool (+0xe80), maintenance, colonists a year */
	long mer_rate_m;                      /* merchants a year (0x458b50), this month, saved */
	/* for the AI's strength indices (economy.c monthly, read by ai.c in the same month step; not saved) */
	long te_m, pe_m, maintl_m, maintn_m, loanint;   /* trade / production efficiency x1000, land / naval maintenance, sum of loan ducats x rate % */
	short hports, nloans_b;               /* home ports (+0x1f84), bank loans */
	long bankrupt_day;                  /* day of the last bankruptcy (0 never; country+0xe9c) */
	short war_tax;                        /* months of war taxes left (country+0xe78, SPEC_SMALLRULES 1) */
	char flagname[4];                     /* event flagname (REV, UK): the shield shield_<tag><name>.bmp, saved */
	unsigned char tracked;                /* the scenario header has a block for it (country+0x11e, NIEPEWNE): missions */
	short loansize;
	unsigned char bank, stock;            /* event commands "bank" (+0xfdc: the bank's interest halved) and "stock" (+0xfdb) */
	long yinc_m[12], yexp_m[16];          /* the Ledger's income / expense categories this year (e+0x154 / +0x184, x1000) */
	long linc_m[12], lexp_m[16];          /* and last year (-0x70) */                       /* bank loan size in ducats (country+0xfde, scenario key loansize; 0 = 200) */
	short acc[8]; unsigned char nacc;     /* accepted cultures (scenario culture = { type = .. type = .. }, country+0x382) besides `culture` */
	signed char ev_stat[3];               /* event ADM / DIP / MIL changes (SPEC_EVENTS 9 codes 6-8) ... */
	long ev_stat_until[3];                /* ... until these days */
	short ev_rr; long ev_rr_until;        /* event country revolt risk (code 0x0b) until that day */
	long we_m;                            /* war exhaustion x1000, 0..cap (SPEC_TIME_ECONOMY 8.4; save key warexhaustion) */
	long inv_land_m, inv_naval_m, inv_infra_m, inv_trade_m;   /* research investment (ducats) */
	long missionaries_m;
	unsigned char human;                  /* 1 = the player */
	char ai_file[20];                     /* scenario `ai = "peaceful.ai"` (empty = default.ai) */
	/* .ai colonial preferences (diplo_load_ai; SPEC_AI_ENVOYS 0): traders, tradingpost, expansion, neighbour,
	   enemies, and the area / region / continent lists as interned name ids (-1 = end) */
	unsigned char ai_traders, ai_tpost, ai_exp;
	short ai_nb, ai_en;
	short ai_area[20], ai_region[8], ai_cont[6];
	long policy_day;                      /* day of the last domestic-policy change (0 = never) */
	short tol[11];                        /* tolerance toward religions 0..10 (religion.h), x10, -100..100 */
	unsigned char defender;               /* Sole Defender of its faith */
	unsigned char tol_set;                /* tol[] came from a save (else the defaults of the state religion) */
	long conv_day;                        /* day of the last change of state religion (0 never) */
	const char *m_name;                   /* the monarch on the throne (monarchs.c, from the date; NULL none) */
	unsigned char m_adm, m_dip, m_mil;    /* his skills; 0 until set = 3 (eu2.exe's value without a monarch) */
} Country;

#define LAND_ABOARD 2
typedef struct {
	long id;
	const char *name;
	const char *lname;                    /* a scenario's own leader (unit block "leader = { name ... }"), else NULL */
	short owner, location;
	long infantry, cavalry, artillery;
	long morale_m;                        /* current morale x1000 (0 = not yet set: starts at the maximum) */
	short path[40]; short npath, pathpos; /* move order: provinces to walk through */
	long arrive_day;                      /* day the next step completes */
	short from;                           /* province it came from (crossing modifiers) */
	unsigned char dead;                   /* 1 destroyed; LAND_ABOARD = carried by fleet `aboard` (skipped like the dead) */
	short aboard;
	short l_id;                           /* its general (leaders.c id + 1, 0 none) and his rank / maneuver / fire / shock / siege */
	unsigned char l_rank, l_move, l_fire, l_shock, l_siege;
	unsigned char l_type;                 /* its leader's category: 0 general, 2 conquistador, 3 explorer (docs/SPEC_EXPLORERS.md) */
} LandUnit;

typedef struct {
	long id;
	const char *name;
	const char *lname;                    /* a scenario's own leader (unit block "leader = { name ... }"), else NULL */
	short owner, location;
	short warships, galleys, transports;
	short path[40]; short npath, pathpos; /* sail order: zones (and the port) to pass through */
	long arrive_day;                      /* day the next step completes */
	long morale_m;                        /* naval morale x1000 (0 = not yet set: starts at the maximum, SPEC_WAR_DIPLOMACY 8.2) */
	short l_id;                           /* its leader (leaders.c id + 1, 0 none): explorers for now */
	unsigned char l_move, l_type, l_fire, l_shock;   /* explorer or admiral (type 1) */
	long sea_since;                       /* year x 12 + month it last lay in a port (+0xf9 / +0xfa): months at sea */
	long att_m;                           /* attrition owed in ships x 1000 (the exe's ship counts are floats) */
} NavalUnit;

/* things the player is told about (main.c shows them as the game's message types); not saved */
enum { NOTE_MERCLOSS = 1, NOTE_MISSION_OK, NOTE_MISSION_FAIL, NOTE_REVOLT, NOTE_BATTLE_WIN, NOTE_BATTLE_LOSS, NOTE_SIEGE_WIN, NOTE_PROV_LOST,
       NOTE_MISSION_DONE, NOTE_MISSION_LOST,   /* these two: prov = mission row, other = VP */
       NOTE_WHITEPEACE,                         /* other = the leader of the other side */
       NOTE_LOAN_REPAID, NOTE_LOAN_DEFAULT,     /* to the lender: other = the borrower, prov = ducats */
       NOTE_NAVAL_WIN, NOTE_NAVAL_LOSS,         /* other = the enemy */
       NOTE_ARMY_ARRIVE, NOTE_FLEET_ARRIVE,     /* other = the unit index */
       NOTE_LIBERATED,                          /* other = the country that held it */
       NOTE_AUTOLOAN, NOTE_BANKRUPT,
       NOTE_DIPMSG,
       NOTE_EXPLORESPEC,
       NOTE_JOINWAR,
       NOTE_BROKEN,
       NOTE_RELIGION,
       NOTE_NEWNATION,                          /* news: country declared independence from other (prov = its capital) */
       NOTE_DEFECT_US, NOTE_DEFECT_THEM,         /* to the player: prov defected to other / from other to us */
       NOTE_DEFECT,
       NOTE_ENVOY,
       NOTE_LOANDUE,
       NOTE_OTHER,
       NOTE_EMPEROR,
       NOTE_NATIVES, NOTE_NATIVESKILL, NOTE_PIRATES };       /* to the player: natives damaged / destroyed our settlement in prov */                          /* news to the player: country is the new emperor (EMPEROR, if its capital is known) */                            /* news: country did OM_* (prov) with other (SPEC_SMALLRULES 5.2) */
enum { OM_ROYMAR, OM_ANNEX, OM_TRADEAG, OM_BRKTRADE, OM_ACCMIL, OM_CANMIL, OM_CLAIM, OM_DEFENDER, OM_NEWMON, OM_MONOPOLY, OM_PEACE, OM_N };                          /* a bank loan of the player is due: prov = its index, other = ducats */                            /* an envoy arrived: prov = the province, other = EA_* (envoys.h) */                           /* news: prov defected to country */                         /* news: country converted to religion other (OTHERRELIGION) */                           /* COUNTRY_BROKEN: BROKENUS / BROKENOTHER */                          /* news: country joined the side of prov (its leader) against other */                      /* a first-discovery special: prov = the province, other = the special 1..17 */                          /* prov = DM_* (diplo.h), country = the actor, other = a third country */
typedef struct { short type, country, prov, other; } Note;

typedef struct World {
	Province prov[MAX_PROV];
	short nprov;                          /* highest id + 1 */
	Country country[MAX_COUNTRY];
	short ncountry;
	LandUnit land[MAX_LAND];   int nland;
	NavalUnit naval[MAX_NAVAL]; int nnaval;
	const char *names[MAX_NAMES]; short nnames;
	Date start, end, now;
	char *pool; unsigned long pool_used, pool_size;   /* string storage */
	char scenario_name[64];
	unsigned long owner_epoch;            /* bumped whenever a province changes owner (map recolour) */
	Cot cot[MAX_COT]; int ncot;
	void (*cot_ev)(void *u, int op, int prov); void *cot_evu;
	long (*ai_resv)(const struct World *w, int c);   /* money the AI's waiting envoy plans hold (envoys.c; NULL none) */   /* the economy's CoT events: 0 cot =, 1 removecot =, 2 a settlement founded */
	War war[MAX_WAR]; int nwar;
	Alliance ally[MAX_ALLY]; int nally;
	Build build[MAX_BUILD]; int nbuild;     /* land units being raised (military.c mil_recruit) */
	/* relation a <-> b, x1000, -200000..+200000, one value per unordered pair (SPEC §2.1: the engine
	 * applies every change both ways); index with world_rel / world_rel_add */
	long rel_m[MAX_COUNTRY * (MAX_COUNTRY - 1) / 2];
	unsigned char dipflag[MAX_COUNTRY][MAX_COUNTRY];   /* DF_* */
	struct { short a, b; long until; } tcb[384];      /* the timed casus belli (DF_CB_TMP): a on b until the day */
	int ntcb;
	short name_hash[2048], tag_hash[1024];  /* index + 1 (0 = empty), open addressing, case-insensitive */
	unsigned long repairs;                /* data quirks tolerated */
	Note note[16]; int nnote;             /* notifications waiting for main.c */
	Note news[32]; int nnews;             /* the same of the other countries (the OTHER messages) */
	const char *sfx[16]; short sfxprov[16]; int nsfx;   /* sfxprov: -1, or where it happens (heard near the screen centre only) */        /* sound effects asked for by the simulation (Config/Sfx.txt keys), played and cleared by main; not saved */
	short reb, nat, pir;                            /* the rebels' country (REB), -1 none */
	unsigned char opt_novp, opt_nomis;   /* the frontend options BASE VICTORY POINTS / DYNAMIC MISSIONS off (not saved) */
	signed char opt_diff;                /* DIFFICULTY - 2 (Normal 0): added to the human's unit prices (windows4 1.4; not saved) */
	unsigned char gflag[8];               /* the global flags 0..6 (0x8a230c +0xa): 0 Tordesillas, 1 Reformation, 2 Calvin,
	                                       * 3 Trent, 4 Edict of Tolerance, 5/6 flag graphics; set by events and `flags` */
	short emperor;                        /* the Holy Roman Emperor's country (0x8d0e18), -1 none (SPEC_HRE) */
	short swaps[64]; int nswaps;          /* event `country` (tag changes): pairs of slots whose identities were swapped */
	long leadev_id[64], leadev_day[64]; unsigned char leadev_kind[64]; int nleadev;   /* events: wake (1) / sleep (0) a leader */
	long monev_id[64], monev_day[64]; short monev_c[64]; unsigned char monev_kind[64]; int nmonev;   /* events: wake (1) / sleep (0) a monarch */
	unsigned char lead_killed[750];       /* leaders (id - 1, leaders.c) killed in battle: never dealt again (6000 bits) */
	unsigned char has_events;             /* the scenario includes db\events.txt (the tutorials do not: no events there) */
	unsigned long spec_used;              /* first-discovery specials already claimed by anyone, bit k (0x8ff638 +4+k) */
	unsigned char tutorial, optmode;      /* header: tutorial = yes; optionmode = 1 forces set_* (difficulty, AI aggressiveness, speed, base VP, missions) */
	signed char set_opt[5];
	char tutfile[64];                     /* header: tutorialfile (the script) */
	unsigned char vp_first[8];            /* 0x418e80: first colony in America / Africa / Siberia / Oceania, trading post in India / China / Japan (1..7) taken */
	unsigned char known[MAX_COUNTRY][(MAX_PROV + 7) / 8];   /* discovered provinces (scenario knownprovinces + units' travels) */
} World;

/* loads DB/province.csv and DB/country.csv. 0 = ok, else message in err */
int  world_load_db(World *w, Pak *text, char *err, int errlen);
/* applies a scenario .eug (pak name, e.g. "scenarios/the grand campaign.eug") and its includes */
int  world_load_scenario(World *w, Pak *text, const char *eug, char *err, int errlen);
void world_free(World *w);

short world_country(const World *w, const char *tag);   /* index or -1 */
short world_intern(World *w, const char *name);          /* case-insensitive */
int   world_flag(const World *w, int n);
void  world_swap_identity(World *w, int a, int b);
void  world_hre_elect(World *w, long today);             /* SPEC_HRE 2: the election (0x5b20a0) */
void  world_hre_month(World *w, long today);             /* the monthly check: no emperor country -> an election */
int   world_hre_n(const World *w, int c);                /* 0x460390: for the emperor, the other electors friendly to him (rel >= 100) */        /* event `country`: slot a takes tag b's identity (CAS -> SPA) and back */
int   world_culture_ok(const World *w, int c, short culture);   /* c's primary or an accepted culture */
const char *world_special(int k, int *vp);
void  world_sfx(World *w, int country, const char *key);   /* queue a sound for the player: country = whose (only the human hears it), -1 everyone */
void world_sfx_at(World *w, int country, int prov, const char *key);   /* the same, positional (SPEC_SOUND 4) */
void  world_news(World *w, int type, int country, int prov, int other);   /* an OTHER message for everyone (main.c filters) */                 /* the special's text key (SPEC_*) and VP, k = 1..17 */                 /* 0x4d16a0: raw for n >= 1, Tordesillas only while it applies */
int   world_religion_index(const World *w, short name);   /* 0 protestant .. 10 hinduism (religion.h order), -1 other */
void  world_default_tol(World *w, int c);                 /* the exe's tolerance defaults for c's state religion (0x430c70) */
void  world_add_core(World *w, int prov, short country);
void  world_remove_core(World *w, int prov, short country);
const char *world_name(const World *w, short id);
char *world_read_file(const char *path, unsigned long *sz);   /* whole file + NUL, malloc; NULL on error */

long  world_rel(const World *w, int a, int b);              /* x1000; 0 for a == b */
void  world_rel_set(World *w, int a, int b, long v_m);      /* clamped to +-200 */
void  world_rel_add(World *w, int a, int b, long d_m);
void  world_note(World *w, int type, int country, int prov, int other);   /* queued for the player's messages */
/* victory points (docs/SPEC_MISSIONS.md 2.1, 0x55f030): nothing when BASE VP is off */
enum { VP_ECO, VP_PEACE, VP_BATTLE, VP_DIP, VP_EST, VP_DISC, VP_MISS };
void  world_vp(World *w, int c, int cat, long amount_m);
void  world_vp_establish(World *w, int c, int prov, int tp);   /* a new colony (+0.8) / trading post (+0.4), +5 the first of its class */
void  world_vp_discover(World *w, int c, int prov, int sea);   /* before marking prov known to c: +2 / +0.3 (0x48cdd0) */
/* the province "income value" 0x47d970 (x1000; ours: base tax), less half a ducat per pirate warship off an unfortified coast */
long world_income_value_m(const World *w, int p);
/* the Ledger's books (SPEC_LEDGER pages 8 / 9): income category 0..11, expense category 0..15, x1000; no treasury change */
enum { INC_TAX, INC_TRADE, INC_PROD, INC_MANU, INC_GOLD, INC_VASSAL, INC_LOAN, INC_EVENT, INC_PEACE, INC_CENSUS, INC_TOLLS, INC_TARIFF };
enum { EXP_ARMYM, EXP_FLEETM, EXP_ARMY, EXP_FLEET, EXP_FORT, EXP_COL, EXP_TP, EXP_MIS, EXP_MER, EXP_TECH, EXP_STAB, EXP_TRINF, EXP_MANU,
       EXP_LOAN, EXP_INTEREST, EXP_EVENT };
void world_book_inc(World *w, int c, int cat, long amount_m);
void world_book_exp(World *w, int c, int cat, long amount_m);
void world_books_year(World *w);       /* this year becomes last year */
void world_weather_month(World *w);   /* every province's weather of the new month (0x4825b0, from the province monthly) */
/* a casus belli of a on b until the calendar day (the later one if a has one already); expired ones go in world_cb_day */
void world_give_cb(World *w, int a, int b, long until);
void world_cb_day(World *w, long today);
#define WORLD_KNOWN(w, c, p) (((w)->known[c][(p) >> 3] >> ((p) & 7)) & 1)
#define WORLD_DISCOVER(w, c, p) ((w)->known[c][(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))
int   world_ally_of(const World *w, int c, int type);       /* alliance index of that type holding c, or -1 */
int   world_allied(const World *w, int a, int b, int type); /* same alliance object of that type */
int   world_overlord(const World *w, int c);                /* country index or -1 */
int world_trade_minded(const World *w, int country);     /* 0x43a9e0 */
long world_prov_manpower_m(const World *w, int prov);     /* 0x47cc80, yearly x1000 */

#endif
