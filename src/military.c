/* military.c - see military.h. Section numbers refer to docs/SPEC_WAR_DIPLOMACY.md. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "military.h"
#include "buildings.h"
#include "script.h"

#define MAXLEV 64
#define MAXB   64

typedef struct { long vif, vcf, vaf, vis, vcs, vas, morale; int era; } LandLev;   /* x1000 */

typedef struct {
	short prov, attacker;        /* attacker = country whose unit arrived to start it */
	unsigned char phase, day, die[2], active;
	long men_day[2], mor_day[2]; /* losses per day taken BY side 0 / 1 (side 0 = attacker's coalition) */
	long started;
	long lost[2];               /* men lost so far by side 0 / 1 (battle points) */
	long men0[2];               /* men at the start of the battle (the decisive test, 0x46aaa0) */
	short lastc[2];             /* leading country of each side seen last (a wiped-out side has none) */
	long frac[2];               /* naval: ship losses not yet taken, x1000 (saved as bfrac) */
	unsigned char assault;      /* 1: the assault of a besieged fortress, side 1 = its garrison (docs/SPEC_ASSAULT.md) */
} Battle;
typedef struct { long wf, ws, gf, gs, morale, att; int era, lowwind; } NavLev;   /* NAVAL.csv, x1000 (fire / shock per ship, Attrition); Low Wind (int) */

typedef struct { long next_test, starve_m; short distress, breaches; short besieger; long g_inf, g_art, g_mor; } Siege;   /* g_*: the garrison (SPEC_ASSAULT 2) */

/* the military AI (docs/SPEC_MILITARY_AI.md): the King per country, a General per (country, land mass),
 * an Admiral per General with a port, expedition Generals, plans (LANDATTACK / CONQUER / GARRISON / NAVALATTACK / BLOCKADE /
 * BREAKBLOCKADE / INVASION / TRANSPORT), build orders. All of it is saved (mil_save "ai*" blocks). */
#define AIG_MAX 400                 /* Generals in the world */
#define AIP_MAX 800                 /* plans in the world */
#define AIO_MAX 4                   /* build orders per country (the King's BuildArmy + plan reinforcements) */
#define AI_BUILD_RESERVE 32         /* ours: the AI leaves this much of the shared build queue (MAX_BUILD) to the player */
#define AI_BUILDS_EACH 3            /* ours: land recruits one AI country may have waiting at a time */
enum { PL_FREE = 0, PL_LANDATTACK = 2, PL_NAVALATTACK = 3, PL_TRANSPORT = 6, PL_CONQUER = 11, PL_BREAKBLOCKADE = 14, PL_BLOCKADE = 15,
       PL_INVASION = 16, PL_GARRISON = 17, PL_EXSEA = 0x32, PL_EXLAND = 0x33, PL_KILLNAT = 0x34 };   /* the exe's plan type ids */
typedef struct { long next; unsigned char war; short bf[3]; } MilKing;        /* next think day, at war at the last think, BuildFleet still to build */
typedef struct { long inf, cav, art; short prov, plan, prio; } MilOrder;      /* a BuildArmy: men / men / pieces still to build, target province, parent plan (-1 the King) */
/* a General: country (-1 free slot), land mass, next think, next DEFEND (garrison layout); its Admiral (D.2, ours: one per
 * General): home port (0 none), next think; exp = an expedition General (D.6) on a land mass where we own nothing, against foe */
typedef struct { short c, grp; long next, dnext; short hport, foe; unsigned char exp; long anext; } MilGen;
typedef struct {
	unsigned char type, phase, engaged, asked;   /* PL_*, 0 gather / 1 attack (TRANSPORT: its state), engaged, a build was asked for */
	short c, gen, target, gather;                /* (TRANSPORT: target = where the army goes, gather = its embarkation province) */
	long score, need, own, next, power;          /* x1000 score; strength values (see ai_ustr); next think day; garrison power */
	short army, fleet, ezone, lzone, land, parent;   /* TRANSPORT (D.7): the army, its fleet, embarkation / unload zone, landing province, the plan served (-1 none) */
	long t0;                                     /* TRANSPORT: start day */
} MilPlan;

struct Military {
	World *w;
	LandLev lev[MAXLEV]; int nlev;
	NavLev nlv[MAXLEV]; int nnlv;
	signed char nfire[7][7], nshock[7][7];  /* naval fire / naval shock.csv: CRT column by (our era, their era) */
	signed char afire[7][7], ashock[7][7], adef[7][7];   /* assault fire / shock (the attacker), assault defender (the garrison) */
	long crt_cas[16][5], crt_mor[16][5];    /* x1000 */
	signed char fire[7][7], shock[7][7];    /* CRT letter 0..4 by [our era][their era] */
	long tmove[16];                         /* terrain movement factor x1000 */
	long rel_morale[16];                    /* religion.csv Morale /100, x1000, by religion row */
	short rel_of_name[MAX_NAMES];
	unsigned long *astart; unsigned long nadj; unsigned long *arec;   /* ADJACENT.TBL: neighbour, type pairs */
	Battle b[MAXB];
	Siege *siege;                           /* per province */
	short loc_head[MAX_PROV], loc_next[MAX_LAND];   /* live land units per province, rebuilt daily */
	unsigned long seed;
	short europe;                           /* name id of the continent "europe" (-1 none) */
	int explmiss;                           /* SPEC_EXPLORERS 4.2: misses of the explorers' coastal rolls (0x80b4fc, one for all fleets) */
	long pir_next;                          /* SPEC_PIRATES 1.1: the pirates' next spawner day (PIR+0x212e, 0 = at once) */
	short pir_last;                         /* the last spawn zone (+0x2132) */
	MilTradeFn tradefn; void *tradeu;
	long (*supfn)(void *, int); void *supu;   /* the land support limit (economy.c) */
	long sup_day[MAX_COUNTRY], sup_val[MAX_COUNTRY];   /* its value per country, taken once a day (day + 1, 0 none) */
	struct { long inf, cav, art; } merc[16]; int nmerc;   /* the mercenary market (saved) */
	MilLogFn log; void *loguser;
	long today;
	unsigned char warbits[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];   /* a at war with b: rebuilt at the start of every mil_day */
	unsigned char sidebits[(MAX_COUNTRY * MAX_COUNTRY + 7) / 8];  /* a and b on the same side of a war (dip record +0x3d, 0x4ce1d1) */
	unsigned char atwar_any[MAX_COUNTRY];
	unsigned char inbat[MAX_PROV];          /* 1 = a battle is running in that province */
	long eday[3][16][3];                    /* edge_days by speed class (x2 = 3/4/6), terrain, crossing type 0/1/2 */
	unsigned char realwar[MAX_COUNTRY];     /* in a war of w->war (atwar_any also counts rebels in the land) */
	/* the military AI: saved state */
	MilKing king[MAX_COUNTRY];
	MilOrder ord[MAX_COUNTRY][AIO_MAX];
	MilGen gen[AIG_MAX];
	MilPlan plan[AIP_MAX];
	unsigned short aplan[MAX_LAND];         /* plan index + 1 the army serves (0 none) */
	long astuck[MAX_LAND];                  /* ours: no plan offers to this army before that day (it found no way) */
	unsigned short fplan[MAX_NAVAL];        /* plan index + 1 the fleet serves (0 none) */
	/* the military AI: constant or rebuilt every AI day */
	long aiw[MAX_COUNTRY][AIW_N];           /* the .ai weights x1000 (mil_set_ai_weights) */
	unsigned char aiw_set[MAX_COUNTRY];
	MilAiQuery aiq; void *aiqu;             /* the diplomatic AI: strategic mode, war target */
	short lgrp[MAX_PROV];                   /* land mass (land-connected provinces) of each land province, -1 sea */
	short ncont[MAX_NAMES];                 /* land provinces per continent name */
	short own_head[MAX_COUNTRY], own_next[MAX_PROV];     /* provinces by owner */
	short arm_head[MAX_COUNTRY], arm_next[MAX_LAND];     /* live land units (not aboard) by owner */
	short ctl_head[MAX_COUNTRY], ctl_next[MAX_PROV];     /* provinces by controller */
	short cpl_head[MAX_COUNTRY], cpl_next[AIP_MAX];      /* plans by country */
	short cg_head[MAX_COUNTRY], cg_next[AIG_MAX];        /* Generals by country */
	short navw[MAX_COUNTRY], navg[MAX_COUNTRY], navt[MAX_COUNTRY];   /* warships, galleys, transports by owner */
	short malli[MAX_COUNTRY];                            /* first military alliance of each country, -1 */
	long qland[MAX_PROV], qsea[MAX_PROV];                /* men / ships queued in each province by its owner (mil_recruit_avail) */
	short lprov[MAX_PROV]; int nlprov;                   /* the land provinces (static) */
	long cdue[MAX_COUNTRY];                              /* the earliest think of a country's Generals and plans */
	short zp_head[MAX_PROV], zp_next[MAX_PROV];          /* (static) the ports opening onto each sea zone, ascending */
	short gp_head[MAX_PROV], gp_next[MAX_PROV];          /* (static) the land provinces of each land mass */
	unsigned char coast[MAX_PROV], gcoast[MAX_PROV];     /* (static) land province next to a sea zone; land mass with such a province */
	short fz_head[MAX_PROV], fz_next[MAX_NAVAL];         /* live fleets by location (rebuilt every AI day) */
	short fzused[MAX_NAVAL]; int nfz;                    /* the fz_head entries in use */
	short fo_head[MAX_COUNTRY], fo_next[MAX_NAVAL];      /* live fleets by owner */
	long fload[MAX_NAVAL];                               /* what each fleet carries (mil_fleet_load) */
	short *sadj; unsigned short sst[1617];                     /* (static) sea zone -> its sea neighbours, sadj[sst[z] .. sst[z + 1]) */
	short *thpool; int thused; short thoff[MAX_PROV], thlen[MAX_PROV];   /* (static, filled when first asked) the Admirals' theatres by home zone */
};

static long max_morale(const Military *m, int c);
static int port_prov(const World *w, int p);
static void pirates_day(Military *m);
static void naval_attrition(Military *m);
static void pirates_refresh(Military *m);
static void pirates_tech(Military *m);
static void ai_init(Military *m);
static void natives_arrival(Military *m, int p);
static void natives_damage(Military *m, int p);
static void natives_pacify(Military *m);
static int atw(const Military *m, int a, int b)       /* == mil_at_war for today, one bit test */
{
	long i;
	if (a < 0 || b < 0 || a == b) return 0;
	i = (long)a * MAX_COUNTRY + b;
	return (m->warbits[i >> 3] >> (i & 7)) & 1;
}

static void build_warbits(Military *m)
{
	const World *w = m->w;
	int i, j, k;
	memset(m->warbits, 0, sizeof m->warbits);
	memset(m->atwar_any, 0, sizeof m->atwar_any);
	memset(m->realwar, 0, sizeof m->realwar);
	if (w->reb >= 0)                                    /* the rebels fight everyone (§13) */
		for (i = 0; i < w->ncountry; i++) if (i != w->reb) {
			long x = (long)w->reb * MAX_COUNTRY + i, y = (long)i * MAX_COUNTRY + w->reb;
			m->warbits[x >> 3] |= (unsigned char)(1 << (x & 7));
			m->warbits[y >> 3] |= (unsigned char)(1 << (y & 7));
		}
	if (w->nat >= 0)                                    /* and so do the natives (NAT, country class 5) */
		for (i = 0; i < w->ncountry; i++) if (i != w->nat) {
			long x = (long)w->nat * MAX_COUNTRY + i, y = (long)i * MAX_COUNTRY + w->nat;
			m->warbits[x >> 3] |= (unsigned char)(1 << (x & 7));
			m->warbits[y >> 3] |= (unsigned char)(1 << (y & 7));
		}
	if (w->pir >= 0)                                    /* the pirates too (0x43b6e0: no declaration involved) */
		for (i = 0; i < w->ncountry; i++) if (i != w->pir) {
			long x = (long)w->pir * MAX_COUNTRY + i, y = (long)i * MAX_COUNTRY + w->pir;
			m->warbits[x >> 3] |= (unsigned char)(1 << (x & 7));
			m->warbits[y >> 3] |= (unsigned char)(1 << (y & 7));
		}
	if (w->reb >= 0) {                                  /* who has rebels to put down: their armies go out (AI) */
		m->atwar_any[w->reb] = 1;
		for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].owner == w->reb && w->land[i].location > 0) {
			int o = w->prov[w->land[i].location].owner;
			if (o >= 0) m->atwar_any[o] = 1;
		}
		for (i = 1; i < w->nprov; i++) if (w->prov[i].controller == w->reb && w->prov[i].owner >= 0) m->atwar_any[w->prov[i].owner] = 1;
	}
	memset(m->sidebits, 0, sizeof m->sidebits);
	for (i = 0; i < w->nwar; i++) {                     /* co-belligerents: every pair of one side */
		int sd;
		for (sd = 0; sd < 2; sd++) {
			const short *L = sd ? w->war[i].def : w->war[i].att;
			int n = sd ? w->war[i].ndef : w->war[i].natt;
			for (j = 0; j < n; j++) for (k = 0; k < n; k++) if (j != k) {
				long x = (long)L[j] * MAX_COUNTRY + L[k];
				m->sidebits[x >> 3] |= (unsigned char)(1 << (x & 7));
			}
		}
	}
	for (i = 0; i < w->nwar; i++)
		for (j = 0; j < w->war[i].natt; j++)
			for (k = 0; k < w->war[i].ndef; k++) {
				int a = w->war[i].att[j], b = w->war[i].def[k];
				long x = (long)a * MAX_COUNTRY + b, y = (long)b * MAX_COUNTRY + a;
				if (a == b) continue;
				m->atwar_any[a] = m->atwar_any[b] = 1;
				m->realwar[a] = m->realwar[b] = 1;
				m->warbits[x >> 3] |= (unsigned char)(1 << (x & 7));
				m->warbits[y >> 3] |= (unsigned char)(1 << (y & 7));
			}
}
static int mrand(Military *m) { m->seed = (m->seed * 214013UL + 2531011UL) & 0xFFFFFFFFUL; return (int)((m->seed >> 16) & 0x7FFF); }

static void logm(Military *m, const char *fmt, ...);
#include <stdarg.h>
static void logm(Military *m, const char *fmt, ...)
{
	char b[200];
	va_list a;
	if (!m->log) return;
	va_start(a, fmt);
	vsnprintf(b, sizeof b, fmt, a);
	va_end(a);
	m->log(m->loguser, m->today, b);
}

void mil_set_log(Military *m, MilLogFn fn, void *user) { m->log = fn; m->loguser = user; }
void mil_set_tradefn(Military *m, MilTradeFn fn, void *user) { if (m) { m->tradefn = fn; m->tradeu = user; } }

/* ------------------------------------------------------------- loading */
static long milli_of(const char *s) { return parse_milli(s); }

static void load_letters(Pak *t, const char *name, signed char tab[7][7])
{
	unsigned long sz;
	char *buf = (char *)pak_load(t, name, &sz), *cur, *line, *f[10];
	int r = -1, c;
	memset(tab, 0, 49);
	if (!buf) return;
	cur = buf;
	while ((line = csv_line(&cur)) != NULL && r < 7) {
		int n = csv_split(line, f, 10);
		if (r < 0) { r = 0; continue; }                     /* header */
		for (c = 0; c < 7 && c + 1 < n; c++) {
			char L = f[c + 1][0];
			tab[r][c] = (signed char)(L >= 'A' && L <= 'E' ? L - 'A' : -1);
		}
		r++;
	}
	free(buf);
}

Military *mil_load(World *w, Pak *text, Pak *map, unsigned long seed, char *err, int errlen)
{
	Military *m = calloc(1, sizeof *m);
	char *buf, *cur, *line, *f[16];
	unsigned long sz;
	int r, i;
	if (!m) { snprintf(err, errlen, "out of memory"); return NULL; }
	m->w = w; m->seed = seed;
	{ int i2; m->europe = -1; for (i2 = 0; i2 < w->nnames; i2++) if (script_ieq(world_name(w, (short)i2), "europe")) m->europe = (short)i2; }
	m->siege = calloc(MAX_PROV, sizeof *m->siege);
	if ((buf = (char *)pak_load(text, "db/land.csv", &sz)) != NULL) {     /* §15 */
		cur = buf; r = -1;
		while ((line = csv_line(&cur)) != NULL && m->nlev < MAXLEV) {
			int n = csv_split(line, f, 16);
			LandLev *L;
			if (r++ < 0) continue;
			if (n < 12 || f[0][0] < '0' || f[0][0] > '9') continue;
			L = &m->lev[m->nlev++];
			L->vif = parse_int(f[2]) * 10; L->vcf = parse_int(f[3]) * 10; L->vaf = parse_int(f[4]) * 10;
			L->vis = parse_int(f[5]) * 10; L->vcs = parse_int(f[6]) * 10; L->vas = parse_int(f[7]) * 10;
			L->morale = milli_of(f[9]); L->era = (int)parse_int(f[11]);
		}
		free(buf);
	}
	if ((buf = (char *)pak_load(text, "db/combat/crt.csv", &sz)) != NULL) {
		cur = buf; r = 0;
		while ((line = csv_line(&cur)) != NULL && r < 16) {
			int n = csv_split(line, f, 12), c;
			if (n < 10) continue;
			for (c = 0; c < 5; c++) { m->crt_cas[r][c] = parse_milli(f[2 * c]) / 100; m->crt_mor[r][c] = parse_milli(f[2 * c + 1]); }
			r++;
		}
		free(buf);
	}
	load_letters(text, "db/combat/landfire.csv", m->fire);
	load_letters(text, "db/combat/naval fire.csv", m->nfire);
	load_letters(text, "db/combat/assault fire.csv", m->afire);
	load_letters(text, "db/combat/assault shock.csv", m->ashock);
	load_letters(text, "db/combat/assault defender.csv", m->adef);
	load_letters(text, "db/combat/naval shock.csv", m->nshock);
	if ((buf = (char *)pak_load(text, "db/naval.csv", &sz)) != NULL) {     /* level;Cost;WarshipFire;WarshipShock;GalleyFire;GalleyShock;Attrition;Morale;Wind;Low Wind;Sprite;Era */
		cur = buf; r = -1;
		while ((line = csv_line(&cur)) != NULL && m->nnlv < MAXLEV) {
			int n = csv_split(line, f, 16);
			NavLev *N;
			if (r++ < 0) continue;
			if (n < 12 || f[0][0] < '0' || f[0][0] > '9') continue;
			N = &m->nlv[m->nnlv++];
			N->wf = parse_int(f[2]) * 10; N->ws = parse_int(f[3]) * 10; N->gf = parse_int(f[4]) * 10; N->gs = parse_int(f[5]) * 10;
			N->morale = milli_of(f[7]); N->era = (int)parse_int(f[11]); N->att = milli_of(f[6]); N->lowwind = (int)parse_int(f[9]);
		}
		free(buf);
	}
	load_letters(text, "db/combat/landshock.csv", m->shock);
	for (i = 0; i < 16; i++) m->tmove[i] = 1000;
	if ((buf = (char *)pak_load(text, "map/terrain types.csv", &sz)) != NULL) {
		cur = buf; r = -1;
		while ((line = csv_line(&cur)) != NULL) {
			int n = csv_split(line, f, 4);
			long id, v;
			if (r++ < 0 || n < 3) continue;
			id = parse_int(f[0]); v = parse_milli(f[2]);
			if (id >= 0 && id < 16) m->tmove[id] = v <= 0 ? 1000 : v;
		}
		free(buf);
	}
	for (i = 0; i < MAX_NAMES; i++) m->rel_of_name[i] = -1;
	if ((buf = (char *)pak_load(text, "db/religion.csv", &sz)) != NULL) {
		static const char *rn[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite",
		                              "pagan", "confucianism", "buddhism", "hinduism" };
		cur = buf; r = -1;
		while ((line = csv_line(&cur)) != NULL && r < 11) {
			int n = csv_split(line, f, 12);
			if (r++ < 0) continue;
			if (n > 6 && r - 1 < 16) m->rel_morale[r - 1] = parse_int(f[6]) * 10;
		}
		free(buf);
		for (i = 0; i < w->nnames; i++) {
			int k;
			for (k = 0; k < 11; k++) if (script_ieq(world_name(w, (short)i), rn[k])) m->rel_of_name[i] = (short)k;
		}
	}
	{	/* ADJACENT.TBL: u32 start[1616] then { u32 province, type, unused } (docs/MAP_FORMAT.md) */
		unsigned char *a = pak_load(map, "map/adjacent.tbl", &sz);
		unsigned long k, nst = 1616;
		if (!a || sz < nst * 4) { free(a); snprintf(err, errlen, "map/adjacent.tbl missing"); free(m->siege); free(m); return NULL; }
		m->astart = malloc((nst + 1) * sizeof(unsigned long));
		m->nadj = (sz - nst * 4) / 12;
		m->arec = malloc(m->nadj * 2 * sizeof(unsigned long) + 8);
		for (k = 0; k < nst; k++) m->astart[k] = a[4 * k] | a[4 * k + 1] << 8 | (unsigned long)a[4 * k + 2] << 16 | (unsigned long)a[4 * k + 3] << 24;
		m->astart[nst] = m->nadj;
		for (k = 0; k < m->nadj; k++) {
			const unsigned char *p = a + nst * 4 + k * 12;
			m->arec[2 * k] = p[0] | p[1] << 8 | (unsigned long)p[2] << 16;
			m->arec[2 * k + 1] = p[4];
		}
		free(a);
	}
	for (i = 0; i < w->nland; i++) if (!w->land[i].morale_m && !w->land[i].dead) w->land[i].morale_m = max_morale(m, w->land[i].owner);   /* armies start at full morale (a save game keeps its own; the dead stay as they are) */
	{	/* edge_days as a table for the AI search: same formula, no divisions in the inner loop */
		static const int spx2[3] = { 3, 4, 6 };
		int s, t, ty;
		for (s = 0; s < 3; s++)
			for (t = 0; t < 16; t++)
				for (ty = 0; ty < 3; ty++) {
					long cost = m->tmove[t] + (ty == 1 ? 500 : 0) + (ty == 2 ? 1000 : 0), d = cost * 30 * 2 / spx2[s] / 1000;
					m->eday[s][t][ty] = d < 1 ? 1 : d;
				}
	}
	ai_init(m);
	return m;
}

void mil_free(Military *m) { if (m) { free(m->astart); free(m->arec); free(m->siege); free(m->sadj); free(m->thpool); free(m); } }

/* ------------------------------------------------------------- helpers */
int mil_at_war(const World *w, int a, int b)
{
	int i, j, k;
	if (a < 0 || b < 0 || a == b) return 0;
	if (w->reb >= 0 && (a == w->reb || b == w->reb)) return 1;
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		int ina = 0, inb = 0;
		for (j = 0; j < wr->natt; j++) { if (wr->att[j] == a) ina = 1; if (wr->att[j] == b) inb = 1; }
		for (k = 0; k < wr->ndef; k++) { if (wr->def[k] == a) ina = 2; if (wr->def[k] == b) inb = 2; }
		if (ina && inb && ina != inb) return 1;
	}
	return 0;
}

static int land_prov(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].id >= 0 && w->prov[p].terrain < 5; }

static const LandLev *lev_of(const Military *m, int c)
{
	int L = m->w->country[c].land_tech;
	if (L >= m->nlev) L = m->nlev - 1;
	if (L < 0) L = 0;
	return &m->lev[L];
}

static long unit_men(const LandUnit *u) { return u->infantry + u->cavalry + 100 * u->artillery; }

/* §8.1 maximum land morale x1000 (maintenance 1.0, no monarch: MIL 3, no Defender of the Faith) */
static long max_morale(const Military *m, int c)
{
	const Country *co = &m->w->country[c];
	long M = lev_of(m, c)->morale + 500 - 125;
	int r = co->religion >= 0 && co->religion < MAX_NAMES ? m->rel_of_name[co->religion] : -1;
	if (r >= 0) M += m->rel_morale[r];
	M += 40 * (co->policy[4] - 5) + (co->policy[5] > 5 ? 100 * (co->policy[5] - 5) : 0) + 50 * (co->policy[6] - 5) - 50 * (co->policy[7] - 5);
	if (co->defender) M += 500;                                /* Defender of the Faith +0.5 (0x438690) */
	if (M < 750) M = 750;
	if (M > 7000) M = 7000;
	return M;
}

static const NavLev *nlev_of(const Military *m, int c)
{
	static const NavLev none = { 0, 500, 0, 1000, 1500, 1000, 0, 0 };
	int t = m->w->country[c].naval_tech;
	if (!m->nnlv) return &none;
	if (t < 0) t = 0;
	if (t >= m->nnlv) t = m->nnlv - 1;
	return &m->nlv[t];
}

/* §8.2: the same shape as the land one with NAVAL.csv Morale; the Land/Naval policy at its naval end
 * +0.1 a step, Defender of the Faith +0.15; 0.5 .. 7 (the maintenance term as on land: NIEPEWNE) */
static long nmax_morale(const Military *m, int c)
{
	const Country *co = &m->w->country[c];
	long M = nlev_of(m, c)->morale + 500 - 125;
	if (co->policy[5] < 5) M += 100 * (5 - co->policy[5]);
	if (co->defender) M += 150;
	if (M < 500) M = 500;
	if (M > 7000) M = 7000;
	return M;
}

static long isqrt_m(long v)          /* sqrt of a x1000 value, result x1000 */
{
	long x = 1000, k;
	if (v <= 0) return 0;
	for (k = 0; k < 12; k++) x = (x + v * 1000 / x) / 2;
	return x;
}

/* ------------------------------------------------------------- movement */
static int speed_x2(const LandUnit *u) { return u->artillery > 0 ? 3 : u->infantry > 0 ? 4 : 6; }   /* speed x2 (1.5 / 2 / 3) */

/* the Treaty of Tordesillas (0x45f840, SPEC_SMALLRULES 2.3 K): while world flag 0 applies, Spain in its half and
 * Portugal in its half may besiege and take a province of a Catholic / Counter-Reform owner that is not its
 * military ally without a war */
static int tot_take(const World *w, int c, int p)
{
	const Province *pv = &w->prov[p];
	int o = pv->controller, r;
	static int spa = -2, por = -2; static const World *ww;
	if (ww != w) { spa = world_country(w, "SPA"); por = world_country(w, "POR"); ww = w; }
	if (c < 0 || o < 0 || o == c || !((c == spa && (pv->tot & 1)) || (c == por && (pv->tot & 2)))) return 0;
	if (!world_flag(w, 0)) return 0;
	r = world_religion_index(w, w->country[o].religion);
	return (r == 2 || r == 3) && !world_allied(w, c, o, ALLY_MILITARY);
}

/* terra incognita (docs/SPEC_EXPLORERS.md 3): a province the country does not know may be entered when it
 * has an owner, with land tech 31 (fleets: naval tech 27), or under a conquistador / explorer. Every country
 * (SPEC_AI_EXPLORE 4: the AI's paths too); rebels, natives and pirates go anywhere (0x48c0e0: pirates explore). */
static int may_enter(const World *w, int c, int p, int naval, int ltype)
{
	if (c < 0 || c == w->reb || c == w->nat || c == w->pir || WORLD_KNOWN(w, c, p)) return 1;
	if (!naval && w->prov[p].owner >= 0) return 1;
	if (naval ? w->country[c].naval_tech >= 27 : w->country[c].land_tech >= 31) return 1;
	return ltype == 2 || ltype == 3;
}

/* 0x45f730 "C may enter P" (land): no owner, our own, controlled by us, at war with the owner (always for
 * REB / NAT / PIR), on the owner's side of a war (+0x3d), the emperor at war in the Empire (SPEC_HRE 3.4), the
 * owner our vassal, military access from the owner; Tordesillas: Spain / Portugal in their half where the owner is
 * Catholic and not their military ally (SPEC_SMALLRULES 2.3 K) */
static int may_pass(const Military *m, int c, int p)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	int o = pv->owner;
	long x;
	if (o < 0 || o == c || pv->controller == c || c < 0) return 1;
	if (c == w->reb || c == w->nat || o == w->reb || o == w->nat) return 1;
	if (pv->tot && world_flag(w, 0)) {
		int spa = world_country(w, "SPA"), por = world_country(w, "POR"), r = world_religion_index(w, w->country[o].religion);
		if (((c == spa && (pv->tot & 1)) || (c == por && (pv->tot & 2))) && (r == 2 || r == 3) && !world_allied(w, c, o, ALLY_MILITARY)) return 1;
	}
	if (atw(m, c, o)) return 1;
	x = (long)c * MAX_COUNTRY + o;
	if ((m->sidebits[x >> 3] >> (x & 7)) & 1) return 1;
	if (c == w->emperor && pv->hre && m->atwar_any[c]) return 1;
	if (world_overlord(w, o) == c) return 1;
	return (w->dipflag[o][c] & DF_ACCESS) != 0;
}

static long edge_days(const Military *m, const LandUnit *u, int to, int type)
{
	const Province *p = &m->w->prov[to];
	long cost = m->tmove[p->terrain < 16 ? p->terrain : 0] + (type == 1 ? 500 : 0) + (type == 2 ? 1000 : 0);
	long d = cost * 30 * 2 / speed_x2(u) / 1000;          /* trunc(cost x 30 / speed) */
	if (u->owner >= 0 && m->w->country[u->owner].human && !WORLD_KNOWN(m->w, u->owner, to)) {   /* 3.3: x (1.5 - 0.15 maneuver + K), K 5 / 2.2 */
		long uu = 150 - 15L * (u->l_id ? u->l_move : 0) + (u->l_id && (u->l_type == 2 || u->l_type == 3) ? 220 : 500);
		d = d * uu / 100;
	}
	return d < 1 ? 1 : d;
}

long mil_order_move(Military *m, int ui, int dest, long today)
{
	World *w = m->w;
	LandUnit *u = &w->land[ui];
	static long dist[MAX_PROV];
	static short prev[MAX_PROV];
	static unsigned char done[MAX_PROV];
	int i, src = u->location, steps = 0, c;
	short rev[64];
	if (!land_prov(w, dest) || !land_prov(w, src) || src == dest) return -1;
	for (i = 0; i < w->nprov; i++) { dist[i] = 0x7FFFFFFFL; prev[i] = -1; done[i] = 0; }
	dist[src] = 0;
	for (;;) {                                           /* Dijkstra, O(n^2): fine for single orders */
		long best = 0x7FFFFFFFL;
		unsigned long k;
		c = -1;
		for (i = 1; i < w->nprov; i++) if (!done[i] && dist[i] < best) { best = dist[i]; c = i; }
		if (c < 0 || c == dest) break;
		done[c] = 1;
		if (c >= 1616) continue;
		for (k = m->astart[c]; k < m->astart[c + 1]; k++) {
			int nb = (int)m->arec[2 * k], ty = (int)m->arec[2 * k + 1];
			long nd;
			if (!land_prov(w, nb) || done[nb] || !may_enter(w, u->owner, nb, 0, u->l_id ? u->l_type : 0) || !may_pass(m, u->owner, nb)) continue;
			nd = dist[c] + edge_days(m, u, nb, ty);
			if (nd < dist[nb]) { dist[nb] = nd; prev[nb] = (short)c; }
		}
	}
	if (dist[dest] == 0x7FFFFFFFL) return -1;
	for (c = dest; c != src && steps < 64; c = prev[c]) rev[steps++] = (short)c;
	if (steps > 40) steps = 40;                          /* long orders are cut (re-issued later) */
	for (i = 0; i < steps; i++) u->path[i] = rev[steps - 1 - i];
	u->npath = (short)steps; u->pathpos = 0;
	u->arrive_day = today + edge_days(m, u, u->path[0], 0);
	return dist[dest];
}

/* Shift + right click (0x4907a0 with append): the way from the order's last waypoint to dest is added
 * to the path the unit walks now; with no order it is a plain move. Days to the new end, -1 none. */
long mil_order_append(Military *m, int ui, int dest, long today)
{
	LandUnit *u = &m->w->land[ui];
	short keep[40];
	int nk = 0, i, loc = u->location;
	long arr = u->arrive_day, d;
	if (u->pathpos >= u->npath) return mil_order_move(m, ui, dest, today);
	for (i = u->pathpos; i < u->npath && nk < 40; i++) keep[nk++] = u->path[i];
	u->location = keep[nk - 1];                          /* plan from the last waypoint */
	d = mil_order_move(m, ui, dest, today);
	u->location = (short)loc;
	if (d < 0) {                                        /* refused: the old order stays */
		for (i = 0; i < nk; i++) u->path[i] = keep[i];
		u->npath = (short)nk; u->pathpos = 0; u->arrive_day = arr;
		return -1;
	}
	{	short add[40];
		int na = u->npath;
		for (i = 0; i < na; i++) add[i] = u->path[i];
		for (i = 0; i < nk; i++) u->path[i] = keep[i];
		for (i = 0; i < na && nk + i < 40; i++) u->path[nk + i] = add[i];
		u->npath = (short)(nk + i); u->pathpos = 0; u->arrive_day = arr;
	}
	return d;
}

int mil_land_unit_at(const World *w, int prov, int owner)
{
	int i;
	for (i = 0; i < w->nland; i++)
		if (!w->land[i].dead && w->land[i].location == prov && (owner < 0 || w->land[i].owner == owner)) return i;
	return -1;
}

static int adj_type(const Military *m, int a, int b)
{
	unsigned long k;
	if (a <= 0 || a >= 1616) return 0;
	for (k = m->astart[a]; k < m->astart[a + 1]; k++) if ((int)m->arec[2 * k] == b) return (int)m->arec[2 * k + 1];
	return 0;
}

static int hostile_in(const Military *m, int prov, int c)
{
	const World *w = m->w;
	int i;
	for (i = 0; i < w->nland; i++)
		if (!w->land[i].dead && w->land[i].location == prov && atw(m, w->land[i].owner, c)) return 1;
	return 0;
}

static void move_day(Military *m)
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		int next;
		if (u->dead || u->pathpos >= u->npath || m->today < u->arrive_day) continue;
		next = u->path[u->pathpos++];
		u->from = u->location;
		u->location = (short)next;
		if (u->owner >= 0) { world_vp_discover(w, u->owner, next, 0); WORLD_DISCOVER(w, u->owner, next); }   /* 4.2: a unit reveals the province it enters */
		if (w->prov[next].natives > 0 && u->owner != w->nat && u->owner != w->reb) natives_arrival(m, next);
		if (hostile_in(m, next, u->owner)) { u->npath = 0; logm(m, "%s army arrives in %s - enemy present", w->country[u->owner].tag, w->prov[next].name); continue; }
		if (u->pathpos < u->npath) u->arrive_day = m->today + edge_days(m, u, u->path[u->pathpos], adj_type(m, u->location, u->path[u->pathpos]));
		else { u->npath = 0; world_note(w, NOTE_ARMY_ARRIVE, u->owner, next, i); }
	}
}

/* -------------------------------------------------------------- battles */
static int side_of(const Military *m, const Battle *b, int c) { return c == b->attacker || !atw(m, c, b->attacker) ? 0 : 1; }

static void side_sum(const Military *m, const Battle *b, int s, long *men, long *fire, long *shock, long *cav, long *morale, int *country)
{
	const World *w = m->w;
	int i, t = w->prov[b->prov].terrain;
	long mm = 0;
	*men = *fire = *shock = *cav = 0; *country = -1;
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		const LandLev *L;
		long n;
		if (u->dead || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		L = lev_of(m, u->owner);
		n = unit_men(u);
		*men += n; *cav += u->cavalry; mm += n * (u->morale_m / 10);
		{	long f = (u->infantry * L->vif + u->cavalry * L->vcf + u->artillery * L->vaf * (t == 4 ? 1 : 2) / 2) / 1000;
			long sh = (u->infantry * L->vis + u->cavalry * L->vcs * (t == 1 || t == 2 || t == 4 ? 1 : 2) / 2 + u->artillery * L->vas) / 1000;
			if (u->owner == w->nat && w->nat >= 0) { int fe = w->prov[b->prov].ferocity; f = f * fe / 10; sh = sh * fe / 10; }   /* natives x Ferocity */
			*fire += f; *shock += sh;
		}
		if (*country < 0) *country = u->owner;
	}
	*morale = *men ? mm / *men * 10 : 0;
}

/* §7.5: the side's best-ranked general (lowest rank value); 0 / 0 without one */
static void side_leader(const Military *m, const Battle *b, int s, int *fire, int *shock)
{
	const World *w = m->w;
	int i, best = 99;
	*fire = *shock = 0;
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		if (u->dead || !u->l_id || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (u->l_rank < best) {
			int eu = u->l_type == 2 && m->europe >= 0 && w->prov[b->prov].continent == m->europe;   /* 1.4: a conquistador fights as 0 in Europe */
			best = u->l_rank; *fire = eu ? 0 : u->l_fire; *shock = eu ? 0 : u->l_shock;
		}
	}
}

/* naval battles (SPEC_WAR_DIPLOMACY §12): the land combat engine with ships - fire = warships x
 * WarshipFire x w + galleys x GalleyFire, shock likewise (w = 1: the wind of a zone is NIEZNANE),
 * transports do not fight; "men" are ships */
static int fleet_ships(const NavalUnit *u) { return u->warships + u->galleys + u->transports; }
static void nside_sum(const Military *m, const Battle *b, int s, long *ships, long *fire, long *shock, long *morale, int *country)
{
	const World *w = m->w;
	int i;
	long mm = 0;
	*ships = *fire = *shock = 0; *country = -1;
	for (i = 0; i < w->nnaval; i++) {
		const NavalUnit *u = &w->naval[i];
		const NavLev *N;
		long n = fleet_ships(u);
		if (n <= 0 || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		N = nlev_of(m, u->owner);
		*ships += n; mm += n * (u->morale_m / 10);
		*fire += (u->warships * N->wf + u->galleys * N->gf) / 1000;
		*shock += (u->warships * N->ws + u->galleys * N->gs) / 1000;
		if (*country < 0) *country = u->owner;
	}
	*morale = *ships ? mm / *ships * 10 : 0;
}

static int nside_fighting(const Military *m, const Battle *b, int s)      /* warships + galleys */
{
	const World *w = m->w;
	int i, n = 0;
	for (i = 0; i < w->nnaval; i++) {
		const NavalUnit *u = &w->naval[i];
		if (u->location == b->prov && fleet_ships(u) > 0 && side_of(m, b, u->owner) == s && (s == 0 || atw(m, u->owner, b->attacker))) n += u->warships + u->galleys;
	}
	return n;
}

static int nroll_mod(const Military *m, const Battle *b, int e)    /* the admirals' difference (§7.5) */
{
	const World *w = m->w;
	int i, k, v[2][2], d;
	for (k = 0; k < 2; k++) {
		v[k][0] = v[k][1] = 0;
		for (i = 0; i < w->nnaval; i++) {
			const NavalUnit *u = &w->naval[i];
			if (!u->l_id || u->location != b->prov || fleet_ships(u) <= 0 || side_of(m, b, u->owner) != k) continue;
			if (u->l_fire + u->l_shock > v[k][0] + v[k][1]) { v[k][0] = u->l_fire; v[k][1] = u->l_shock; }
		}
	}
	d = b->phase == 0 ? v[e][0] - v[1 - e][0] : v[e][1] - v[1 - e][1];
	return d > 0 ? d : 0;
}

static void nphase_losses(Military *m, Battle *b)
{
	World *w = m->w;
	long sh[2], fire[2], shock[2], mor[2];
	int c[2], s;
	for (s = 0; s < 2; s++) { nside_sum(m, b, s, &sh[s], &fire[s], &shock[s], &mor[s], &c[s]); b->die[s] = (unsigned char)(mrand(m) % 10); }
	for (s = 0; s < 2; s++) {
		int e = 1 - s, row, col;
		long str = b->phase == 0 ? fire[e] : shock[e];
		b->men_day[s] = b->mor_day[s] = 0;
		if (c[e] < 0 || c[s] < 0) continue;
		row = b->die[e] + nroll_mod(m, b, e);
		if (row > 15) row = 15;
		{ int ea = nlev_of(m, c[e])->era, eb = nlev_of(m, c[s])->era;
		  ea = ea < 0 ? 0 : ea > 6 ? 6 : ea; eb = eb < 0 ? 0 : eb > 6 ? 6 : eb;
		  col = b->phase == 0 ? m->nfire[ea][eb] : m->nshock[ea][eb]; }
		if (str > 0 && col >= 0 && mor[e] > 400) {
			long F = isqrt_m(nmax_morale(m, c[s]) / 2 < 1000 ? 1000 : nmax_morale(m, c[s]) / 2);
			b->men_day[s] = m->crt_cas[row][col] * str * 2 / 10;            /* ships x1000 a day */
			b->mor_day[s] = m->crt_mor[row][col] * 2 / 10 * F / 1000;
			logm(m, "  naval phase %s: %s takes row %d col %c: %ld.%03ld ships/day, morale %ld.%03ld/day", b->phase ? "shock" : "fire",
			     w->country[c[s]].tag, row, (char)(65 + col), b->men_day[s] / 1000, b->men_day[s] % 1000, b->mor_day[s] / 1000, b->mor_day[s] % 1000);
		}
	}
}

static void napply_losses(Military *m, Battle *b, int s)
{
	World *w = m->w;
	long sh, fire, shock, mor, whole;
	int c, i;
	nside_sum(m, b, s, &sh, &fire, &shock, &mor, &c);
	if (sh <= 0) return;
	b->frac[s] += b->men_day[s];
	whole = b->frac[s] / 1000; b->frac[s] %= 1000;
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *u = &w->naval[i];
		long n = fleet_ships(u), take;
		if (n <= 0 || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		take = whole * n / sh;
		if (take > 0) {                                      /* over the three kinds in proportion (as on land) */
			long tg = take * u->galleys / n, tt = take * u->transports / n, tw = take - tg - tt;
			if (tw > u->warships) { tg += tw - u->warships; tw = u->warships; }
			if (tg > u->galleys) { tt += tg - u->galleys; tg = u->galleys; }
			if (tt > u->transports) tt = u->transports;
			u->warships = (short)(u->warships - tw); u->galleys = (short)(u->galleys - tg); u->transports = (short)(u->transports - tt);
			b->lost[s] += tw + tg + tt;
		}
		u->morale_m -= b->mor_day[s];
		if (fleet_ships(u) <= 0) { u->npath = 0; logm(m, "%s fleet %s sunk in %s", w->country[u->owner].tag, u->name ? u->name : "?", w->prov[b->prov].name ? w->prov[b->prov].name : "?"); }
	}
}

static int sea_prov(const World *w, int p);
static int nearest_own_port(Military *m, int fi);
static void nretreat_side(Military *m, Battle *b, int s)            /* broken: to the nearest own port */
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *u = &w->naval[i];
		int port;
		if (fleet_ships(u) <= 0 || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		u->morale_m = 400;
		port = nearest_own_port(m, i);
		if (port > 0 && mil_order_fleet(m, i, port, m->today) >= 0 && u->pathpos < u->npath) {
			u->location = u->path[u->pathpos++];               /* it slips away at once (NIEPEWNE) */
			if (u->pathpos >= u->npath) u->npath = 0;
			logm(m, "%s fleet breaks off to %s", w->country[u->owner].tag, w->prov[port].name ? w->prov[port].name : "?");
		} else { u->warships = u->galleys = u->transports = 0; u->npath = 0; logm(m, "%s fleet, cut off, is lost", w->country[u->owner].tag); }
	}
}

/* ---- the assault (docs/SPEC_ASSAULT.md 3-6): the land engine with the besieger's armies (no cavalry,
 * assault fire / shock columns, die - fort level while there is no breach) against the garrison
 * (assault defender column in both phases); morale factor 1/3; no overrun, terrain or leaders */
static void as_attacker(const Military *m, const Battle *b, long *men, long *inf, long *art, long *fire, long *shock, long *mor, int *c)
{
	const World *w = m->w;
	int i, t = w->prov[b->prov].terrain;
	long mm = 0;
	*men = *inf = *art = *fire = *shock = 0; *c = b->attacker;
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		const LandLev *L;
		long n;
		if (u->dead || u->location != b->prov || u->owner != b->attacker) continue;
		L = lev_of(m, u->owner);
		n = u->infantry + 100 * u->artillery;
		*men += n; *inf += u->infantry; *art += u->artillery; mm += n * (u->morale_m / 10);
		*fire += (u->infantry * L->vif + u->artillery * L->vaf * (t == 4 ? 1 : 2) / 2) / 1000;
		*shock += (u->infantry * L->vis + u->artillery * L->vas) / 1000;
	}
	*mor = *men ? mm / *men * 10 : 0;
}

static void as_garrison(const Military *m, const Battle *b, long *men, long *fire, long *shock, int *c)
{
	const Siege *sg = &m->siege[b->prov];
	int ctl = m->w->prov[b->prov].controller;
	const LandLev *L = ctl >= 0 ? lev_of(m, ctl) : &m->lev[0];
	*c = ctl;
	*men = sg->g_inf + 100 * sg->g_art;
	*fire = (sg->g_inf * L->vif + sg->g_art * L->vaf) / 1000;
	*shock = (sg->g_inf * L->vis + sg->g_art * L->vas) / 1000;
}

static void as_phase_losses(Military *m, Battle *b)
{
	World *w = m->w;
	const Siege *sg = &m->siege[b->prov];
	long am, ai, aa, af, as, amor, gm, gf, gs;
	int ac, gc, s;
	as_attacker(m, b, &am, &ai, &aa, &af, &as, &amor, &ac);
	as_garrison(m, b, &gm, &gf, &gs, &gc);
	b->die[0] = (unsigned char)(mrand(m) % 10); b->die[1] = (unsigned char)(mrand(m) % 10);
	for (s = 0; s < 2; s++) {                                   /* s takes the losses from e = 1 - s */
		int e = 1 - s, row, col, ea, eb, ce = e ? gc : ac, cs = s ? gc : ac;
		long str = e == 0 ? (b->phase == 0 ? af : as) : (b->phase == 0 ? gf : gs), M;
		long emor = e == 0 ? amor : sg->g_mor;
		b->men_day[s] = b->mor_day[s] = 0;
		if (ce < 0 || cs < 0) continue;
		row = b->die[e] - (e == 0 && sg->breaches == 0 ? w->prov[b->prov].fort : 0);
		if (row < 0) row = 0;
		if (row > 15) row = 15;
		ea = lev_of(m, ce)->era; eb = lev_of(m, cs)->era;
		ea = ea < 0 ? 0 : ea > 6 ? 6 : ea; eb = eb < 0 ? 0 : eb > 6 ? 6 : eb;
		col = e == 0 ? (b->phase == 0 ? m->afire[ea][eb] : m->ashock[ea][eb]) : m->adef[ea][eb];
		if (str > 0 && col >= 0 && emor > 400) {
			M = max_morale(m, cs) / 3;
			b->men_day[s] = m->crt_cas[row][col] * str / 1000 * 2 / 10;
			b->mor_day[s] = m->crt_mor[row][col] * 2 / 10 * isqrt_m(M < 1000 ? 1000 : M) / 1000;
			logm(m, "  assault %s: %s takes row %d col %c: %ld men/day, morale %ld.%03ld/day", b->phase ? "shock" : "fire",
			     w->country[cs].tag, row, (char)(65 + col), b->men_day[s], b->mor_day[s] / 1000, b->mor_day[s] % 1000);
		}
	}
}

static void siege_won(Military *m, int p, int bes);
static void retreat_side(Military *m, Battle *b, int s);
/* one day of an assault; returns 1 when it is over */
static int as_day(Military *m, Battle *b)
{
	World *w = m->w;
	Siege *sg = &m->siege[b->prov];
	Province *pv = &w->prov[b->prov];
	long am, ai, aa, af, as, amor, gm, gf, gs, take;
	int ac, gc, i, L = pv->fort > 0 ? pv->fort : 1;
	as_attacker(m, b, &am, &ai, &aa, &af, &as, &amor, &ac);
	if (am > 0) {                                               /* the attacker's losses: infantry and artillery */
		take = b->men_day[0];
		for (i = 0; i < w->nland; i++) {
			LandUnit *u = &w->land[i];
			long n, ti, ta;
			if (u->dead || u->location != b->prov || u->owner != b->attacker) continue;
			n = u->infantry + 100 * u->artillery;
			if (n <= 0) continue;
			ti = take * n / am * u->infantry / n; ta = (take * n / am - ti) / 100;
			u->infantry -= ti; u->artillery -= ta;
			if (u->infantry < 0) u->infantry = 0;
			if (u->artillery < 0) u->artillery = 0;
			b->lost[0] += ti + 100 * ta;
			u->morale_m -= b->mor_day[0];
			if (unit_men(u) <= 0) u->dead = 1;
		}
	}
	as_garrison(m, b, &gm, &gf, &gs, &gc);
	if (gm > 0) {                                               /* the garrison's */
		long ti = b->men_day[1] * sg->g_inf / gm, ta = (b->men_day[1] - ti) / 100;
		sg->g_inf -= ti; sg->g_art -= ta;
		if (sg->g_inf < 0) sg->g_inf = 0;
		if (sg->g_art < 0) sg->g_art = 0;
		sg->g_mor -= b->mor_day[1];
		b->lost[1] += ti + 100 * ta;
	}
	as_attacker(m, b, &am, &ai, &aa, &af, &as, &amor, &ac);
	as_garrison(m, b, &gm, &gf, &gs, &gc);
	if (gm < 100 || sg->g_mor <= 0) {                           /* the garrison breaks: the fortress falls (§6) */
		logm(m, "assault on %s: the garrison breaks", pv->name ? pv->name : "?");
		siege_won(m, b->prov, b->attacker);
		return 1;
	}
	if (am <= 0 || ai <= 0 || amor <= 0 || (amor <= 400 && sg->g_mor <= 400)) {   /* the attacker breaks (both spent: it, NIEPEWNE) */
		long men = 0;
		for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].location == b->prov && w->land[i].owner == b->attacker) men += unit_men(&w->land[i]);
		logm(m, "assault on %s repulsed (%ld men left)", pv->name ? pv->name : "?", men);
		for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].location == b->prov && w->land[i].owner == b->attacker && w->land[i].morale_m < 400) w->land[i].morale_m = 400;
		if (men < 1000L * L) retreat_side(m, b, 0);           /* not even a cover force: it really retreats */
		return 1;
	}
	if (++b->day > 4) { b->day = 0; b->phase = (unsigned char)(b->phase == 0 ? 1 : (af > 0 || gf > 0 ? 0 : 1)); as_phase_losses(m, b); }
	return 0;
}

/* the modifier to side e's die against side 1 - e: mountains, cavalry, the leaders (§7.4, §7.5) */
static int roll_mod(const Military *m, const Battle *b, int e, const long cav[2], int t)
{
	int s = 1 - e, mod = 0;
	if (e == 0 && t == 2) mod -= 1;                                  /* attacker in mountains */
	if (b->phase == 1 && t != 1 && t != 2 && t != 4 && cav[e] > 2 * cav[s]) mod += 1;   /* cavalry superiority */
	{	/* §7.5: the leader difference, never below 0 */
		int fe, se, fs, ss, d;
		side_leader(m, b, e, &fe, &se); side_leader(m, b, s, &fs, &ss);
		d = b->phase == 0 ? fe - fs : se - ss;
		if (d > 0) mod += d;
	}
	return mod;
}

/* §7.4: losses side x takes for the whole phase, from enemy e */
static void phase_losses(Military *m, Battle *b)
{
	World *w = m->w;
	long men[2], fire[2], shock[2], cav[2], mor[2];
	int c[2], s, t = w->prov[b->prov].terrain;
	for (s = 0; s < 2; s++) { side_sum(m, b, s, &men[s], &fire[s], &shock[s], &cav[s], &mor[s], &c[s]); b->die[s] = (unsigned char)(mrand(m) % 10); }
	for (s = 0; s < 2; s++) {
		int e = 1 - s, mod = 0, row, col;
		long str = b->phase == 0 ? fire[e] : shock[e];
		const LandLev *Le, *Ls;
		b->men_day[s] = b->mor_day[s] = 0;
		if (c[e] < 0 || c[s] < 0) continue;
		Le = lev_of(m, c[e]); Ls = lev_of(m, c[s]);
		mod = roll_mod(m, b, e, cav, t);
		row = b->die[e] + mod;
		if (row < 0) row = 0;
		if (row > 15) row = 15;
		{ int ea = Le->era < 0 ? 0 : Le->era > 6 ? 6 : Le->era, eb = Ls->era < 0 ? 0 : Ls->era > 6 ? 6 : Ls->era;
		  col = b->phase == 0 ? m->fire[ea][eb] : m->shock[ea][eb]; }
		if (str > 0 && col >= 0 && mor[e] > 400) {
			long F = isqrt_m(max_morale(m, c[s]) / 2 < 1000 ? 1000 : max_morale(m, c[s]) / 2);
			b->men_day[s] = m->crt_cas[row][col] * str / 1000 * 2 / 10;
			b->mor_day[s] = m->crt_mor[row][col] * 2 / 10 * F / 1000;
			logm(m, "  phase %s: %s takes row %d col %c: %ld men/day, morale %ld.%03ld/day (enemy strength %ld)", b->phase ? "shock" : "fire",
			     w->country[c[s]].tag, row, (char)(65 + col), b->men_day[s], b->mor_day[s] / 1000, b->mor_day[s] % 1000, str);
		}
	}
}

static void apply_losses(Military *m, Battle *b, int s)
{
	World *w = m->w;
	long men[2], fire[2], shock[2], cav[2], mor[2], loss = b->men_day[s];
	int c[2], i;
	side_sum(m, b, s, &men[s], &fire[s], &shock[s], &cav[s], &mor[s], &c[s]);
	if (men[s] <= 0) return;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		long n, take;
		if (u->dead || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		n = unit_men(u);
		take = loss * n / men[s];                                       /* proportional (NIEPEWNE split) */
		if (n > 0) {
			long ti = take * u->infantry / n, tc = take * u->cavalry / n, ta = (take - ti - tc) / 100;
			u->infantry -= ti; u->cavalry -= tc; u->artillery -= ta;
			b->lost[s] += ti + tc + 100 * ta;
			if (u->infantry < 0) u->infantry = 0;
			if (u->cavalry < 0) u->cavalry = 0;
			if (u->artillery < 0) u->artillery = 0;
		}
		u->morale_m -= b->mor_day[s];
		if (unit_men(u) <= 0) { u->dead = 1; logm(m, "%s army destroyed in %s", w->country[u->owner].tag, w->prov[b->prov].name); }
	}
}

static void retreat_side(Military *m, Battle *b, int s)
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		unsigned long k;
		int to = -1;
		if (u->dead || u->location != b->prov || side_of(m, b, u->owner) != s) continue;
		if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
		if (b->prov < 1616)
			for (k = m->astart[b->prov]; k < m->astart[b->prov + 1] && to < 0; k++) {
				int nb = (int)m->arec[2 * k];
				if (land_prov(w, nb) && !hostile_in(m, nb, u->owner) && (w->prov[nb].controller == u->owner || !atw(m, w->prov[nb].controller, u->owner)))
					to = nb;
			}
		u->morale_m = 400;
		u->npath = 0;
		if (u->owner == w->reb) { u->dead = 1; logm(m, "%s army dissolves in %s", w->country[u->owner].tag, w->prov[b->prov].name); continue; }   /* rebels never retreat: they dissolve when their morale breaks (SPEC_WAR_DIPLOMACY 13) */
		if (to < 0) { u->dead = 1; logm(m, "%s army cut off and destroyed in %s", w->country[u->owner].tag, w->prov[b->prov].name); }
		else { u->from = u->location; u->location = (short)to; logm(m, "%s army retreats to %s", w->country[u->owner].tag, w->prov[to].name); }
	}
}

/* §5.1: the war object remembers the date of the last combat between its members */
static void mark_combat(World *w, int a, int b, long today)
{
	int i, j, k;
	for (i = 0; i < w->nwar; i++) {
		War *wr = &w->war[i];
		int sa = 0, sb = 0;
		for (j = 0; j < wr->natt; j++) { if (wr->att[j] == a) sa = 1; if (wr->att[j] == b) sb = 1; }
		for (k = 0; k < wr->ndef; k++) { if (wr->def[k] == a) sa = 2; if (wr->def[k] == b) sb = 2; }
		if (sa && sb && sa != sb) wr->last_combat = today;
	}
}

/* Battle points for the winner against the loser in every war where they are on opposite sides,
 * each matrix byte capped at 95 (§5.1). How many points a battle is worth is NIEZNANE in the exe;
 * ours (NIEPEWNE): 1 + one per 2000 men the loser lost, at most 10. */
/* the battle is over: s lost. Decisive (0x46aaa0): both sides had >= 10000 men, the loser lost >= 2/3 of
 * its men and the winner less than half as many as the loser. VP (PROVBATTLEWON / LOST 0x8f / 0x90):
 * +4 / +0.5 to the winner (nothing against pirates, natives, rebels), -3 / -0.5 to the loser; the
 * war-score byte of the pair +3 / +1 (0x4cfb70). */
static void award_points(Military *m, Battle *b, int win, int lose, long lost)
{
	World *w = m->w;
	int i, j, k, pts, s = side_of(m, b, lose), dec;
	long l0 = b->men0[s], w0 = b->men0[1 - s];
	if (win < 0 || lose < 0) return;
	dec = l0 >= 10000 && w0 >= 10000 && b->lost[s] >= 666L * l0 / 1000 && b->lost[1 - s] < b->lost[s] / 2;
	world_note(w, NOTE_BATTLE_WIN, win, b->prov, lose);           /* LBWIN / LBLOSS for the player */
	world_note(w, NOTE_BATTLE_LOSS, lose, b->prov, win);
	{	const char *lt = w->country[lose].tag;
		if (strcmp(lt, "PIR") && strcmp(lt, "NAT") && strcmp(lt, "REB")) world_vp(w, win, VP_BATTLE, dec ? 4000 : 500);
		world_vp(w, lose, VP_BATTLE, dec ? -3000 : -500);
	}
	pts = dec ? 3 : 1;
	(void)lost;
	for (i = 0; i < w->nwar; i++) {
		War *wr = &w->war[i];
		int ia = -1, id = -1, wa = -1, wd = -1;
		for (j = 0; j < wr->natt; j++) { if (wr->att[j] == win) wa = j; if (wr->att[j] == lose) ia = j; }
		for (k = 0; k < wr->ndef; k++) { if (wr->def[k] == win) wd = k; if (wr->def[k] == lose) id = k; }
		if (wa >= 0 && id >= 0) { int v = wr->bp_att[wa][id] + pts; wr->bp_att[wa][id] = (unsigned char)(v > 95 ? 95 : v); }
		else if (wd >= 0 && ia >= 0) { int v = wr->bp_def[wd][ia] + pts; wr->bp_def[wd][ia] = (unsigned char)(v > 95 ? 95 : v); }
		else continue;
		logm(m, "  %s wins against %s: +%d battle points (loser lost %ld men)", w->country[win].tag, w->country[lose].tag, pts, lost);
	}
}

/* PROVSIEGEWON / LOST (0x91 / 0x92): the taker + (city size 0..6 + 1), the loser - (fort level + 1) */
static void siege_vp(World *w, int p, int won, int lost)
{
	static const long lim[6] = { 5000, 10000, 20000, 40000, 80000, 200000 };
	const Province *pv = &w->prov[p];
	int sz = 0;
	while (sz < 6 && pv->population > lim[sz]) sz++;
	if (pv->population <= 0) sz = 0;
	world_vp(w, won, VP_BATTLE, 1000L * (sz + 1));
	if (lost >= 0) world_vp(w, lost, VP_BATTLE, -1000L * (pv->fort + 1));
}

static void build_lists(Military *m);
static void battles_day(Military *m)
{
	World *w = m->w;
	int i, k;
	/* start battles where enemies share a province */
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		int j;
		if (u->dead || u->location <= 0 || u->location >= MAX_PROV) continue;
		if (!u->morale_m) u->morale_m = max_morale(m, u->owner);
		if (m->inbat[u->location]) continue;
		for (j = m->loc_head[u->location]; j >= 0; j = m->loc_next[j])
			if (j != i && !w->land[j].dead && w->land[j].location == u->location && atw(m, u->owner, w->land[j].owner)) {
				for (k = 0; k < MAXB && m->b[k].active; k++) ;
				if (k == MAXB) break;
				memset(&m->b[k], 0, sizeof m->b[k]);
				m->b[k].active = 1; m->b[k].prov = u->location;
				m->inbat[u->location] = 1;
				m->b[k].attacker = u->from ? u->owner : w->land[j].owner;
				m->b[k].started = m->today;
				m->b[k].lastc[0] = m->b[k].lastc[1] = -1;
				mark_combat(w, u->owner, w->land[j].owner, m->today);
				{	/* first phase: fire if anyone has fire strength, else shock (§7.2) */
					long men, f0, f1, s, cav, mor;
					int c;
					side_sum(m, &m->b[k], 0, &men, &f0, &s, &cav, &mor, &c);
					side_sum(m, &m->b[k], 1, &men, &f1, &s, &cav, &mor, &c);
					m->b[k].phase = (unsigned char)(f0 > 0 || f1 > 0 ? 0 : 1);
				}
				phase_losses(m, &m->b[k]);
				logm(m, "battle of %s: %s attacks", w->prov[u->location].name, w->country[m->b[k].attacker].tag);
				break;
			}
	}
	for (i = 0; i < w->nnaval; i++) {                     /* naval battles: enemy fleets in one sea zone */
		NavalUnit *u = &w->naval[i];
		int j, z = u->location;
		if (fleet_ships(u) <= 0 || z <= 0 || z >= MAX_PROV || !sea_prov(w, z)) continue;
		if (!u->morale_m) u->morale_m = nmax_morale(m, u->owner);
		if (m->inbat[z]) continue;
		for (j = 0; j < w->nnaval; j++)
			if (j != i && w->naval[j].location == z && fleet_ships(&w->naval[j]) > 0 && atw(m, u->owner, w->naval[j].owner)) {
				for (k = 0; k < MAXB && m->b[k].active; k++) ;
				if (k == MAXB) break;
				memset(&m->b[k], 0, sizeof m->b[k]);
				m->b[k].active = 1; m->b[k].prov = (short)z; m->inbat[z] = 1;
				m->b[k].attacker = u->npath ? u->owner : w->naval[j].owner;   /* the one under way (NIEPEWNE) */
				m->b[k].started = m->today; m->b[k].lastc[0] = m->b[k].lastc[1] = -1;
				if (!w->naval[j].morale_m) w->naval[j].morale_m = nmax_morale(m, w->naval[j].owner);
				mark_combat(w, u->owner, w->naval[j].owner, m->today);
				{	long sh, f0, f1, s2, mo; int c2;
					nside_sum(m, &m->b[k], 0, &sh, &f0, &s2, &mo, &c2); nside_sum(m, &m->b[k], 1, &sh, &f1, &s2, &mo, &c2);
					m->b[k].phase = (unsigned char)(f0 > 0 || f1 > 0 ? 0 : 1);
				}
				nphase_losses(m, &m->b[k]);
				logm(m, "naval battle in %s: %s attacks", w->prov[z].name ? w->prov[z].name : "?", w->country[m->b[k].attacker].tag);
				break;
			}
	}
	for (k = 0; k < MAXB; k++) {
		Battle *b = &m->b[k];
		long men[2], fire[2], shock[2], cav[2], mor[2];
		int c[2], s, over = 0;
		if (!b->active) continue;
		if (b->assault) { if (as_day(m, b)) { b->active = 0; m->inbat[b->prov] = 0; } continue; }
		if (sea_prov(w, b->prov)) {                          /* the naval turn (same timeline, §7.2) */
			long fs[2];
			napply_losses(m, b, 0);
			napply_losses(m, b, 1);
			for (s = 0; s < 2; s++) { nside_sum(m, b, s, &men[s], &fire[s], &shock[s], &mor[s], &c[s]); fs[s] = nside_fighting(m, b, s); }
			for (s = 0; s < 2; s++) { if (c[s] >= 0) b->lastc[s] = (short)c[s]; if (!b->men0[s]) b->men0[s] = men[s] + b->lost[s]; }
			for (s = 0; s < 2 && !over; s++) {
				int win = c[1 - s] >= 0 ? c[1 - s] : b->lastc[1 - s], lose = c[s] >= 0 ? c[s] : b->lastc[s];
				if (men[s] <= 0 || fs[s] <= 0 || mor[s] <= 0 || (fs[1 - s] > 0 && fs[s] * 10 < fs[1 - s]) || (s == 0 && mor[0] <= 400 && mor[1] <= 400)) {
					if (win >= 0 && lose >= 0) {
						world_note(w, NOTE_NAVAL_WIN, win, b->prov, lose);
						world_note(w, NOTE_NAVAL_LOSS, lose, b->prov, win);
						world_vp(w, win, VP_BATTLE, 500); world_vp(w, lose, VP_BATTLE, -500);   /* as a land battle that is not decisive (NIEPEWNE) */
					}
					logm(m, "naval battle in %s: %s side %s", w->prov[b->prov].name ? w->prov[b->prov].name : "?", lose >= 0 ? w->country[lose].tag : "?",
					     fs[s] <= 0 ? "has no fighting ships left" : mor[s] <= 0 ? "breaks" : "is overrun");
					if (fs[s] <= 0 || (fs[1 - s] > 0 && fs[s] * 10 < fs[1 - s])) {   /* no fighting ships / overrun: all sunk (§7.2) */
						int i2;
						for (i2 = 0; i2 < w->nnaval; i2++)
							if (w->naval[i2].location == b->prov && side_of(m, b, w->naval[i2].owner) == s && (s == 0 || atw(m, w->naval[i2].owner, b->attacker)))
							{ w->naval[i2].warships = w->naval[i2].galleys = w->naval[i2].transports = 0; w->naval[i2].npath = 0; }
					} else nretreat_side(m, b, s);
					over = 1;
				}
			}
			if (over || men[0] <= 0 || men[1] <= 0) { b->active = 0; m->inbat[b->prov] = 0; continue; }
			if (++b->day > 4) {
				b->day = 0;
				b->phase = (unsigned char)(b->phase == 0 ? 1 : (fire[0] > 0 || fire[1] > 0 ? 0 : 1));
				nphase_losses(m, b);
			}
			continue;
		}
		apply_losses(m, b, 0);
		apply_losses(m, b, 1);
		for (s = 0; s < 2; s++) side_sum(m, b, s, &men[s], &fire[s], &shock[s], &cav[s], &mor[s], &c[s]);
		for (s = 0; s < 2; s++) { if (c[s] >= 0) b->lastc[s] = (short)c[s]; if (!b->men0[s]) b->men0[s] = men[s] + b->lost[s]; }
		for (s = 0; s < 2 && !over; s++) {
			if (men[s] <= 0) { award_points(m, b, c[1 - s], b->lastc[s], b->lost[s]); over = 1; continue; }
			if (mor[s] <= 0 || (men[1 - s] > 0 && men[s] * 10 < men[1 - s]) || (s == 0 && mor[0] <= 400 && mor[1] <= 400)) {   /* both spent: the attacker yields (NIEPEWNE) */
				logm(m, "battle of %s: %s side breaks (men %ld vs %ld)", w->prov[b->prov].name, c[s] >= 0 ? w->country[c[s]].tag : "?", men[s], men[1 - s]);
				if (men[s] * 10 < men[1 - s]) b->lost[s] += men[s];
				award_points(m, b, c[1 - s], c[s], b->lost[s]);
				if (men[s] * 10 < men[1 - s]) {          /* overrun: destroyed outright (§7.2) */
					int i;
					for (i = 0; i < w->nland; i++)
						if (!w->land[i].dead && w->land[i].location == b->prov && side_of(m, b, w->land[i].owner) == s) w->land[i].dead = 1;
				} else retreat_side(m, b, s);
				over = 1;
			}
		}
		if (over || men[0] <= 0 || men[1] <= 0) { b->active = 0; m->inbat[b->prov] = 0; continue; }
		if (++b->day > 4) {                               /* next phase after 5 days */
			b->day = 0;
			b->phase = (unsigned char)(b->phase == 0 ? 1 : (fire[0] > 0 || fire[1] > 0 ? 0 : 1));
			logm(m, "battle of %s: new phase, men %ld / %ld, morale %ld / %ld", w->prov[b->prov].name, men[0], men[1], mor[0], mor[1]);
			for (s = 0; s < 2; s++) {                     /* KILL_LEADER (0x46d2c0): 1 % a side, VP 25 + rank to the leader's own country
				                                           * (as found; NIEPEWNE: per phase here, the exe's trigger slot was not found) */
				int i2, li = -1;
				for (i2 = 0; i2 < w->nland; i2++) {
					const LandUnit *v = &w->land[i2];
					if (!v->dead && v->location == b->prov && v->l_id && side_of(m, b, v->owner) == s && (li < 0 || v->l_fire + v->l_shock > w->land[li].l_fire + w->land[li].l_shock)) li = i2;
				}
				if (li >= 0 && mrand(m) % 100 == 17) {
					LandUnit *v = &w->land[li];
					world_vp(w, v->owner, VP_BATTLE, 1000L * (25 + v->l_rank));
					logm(m, "battle of %s: the %s general falls", w->prov[b->prov].name, w->country[v->owner].tag);
					world_sfx(w, v->owner, "leader_killed");   /* KILL_LEADER 0x52f73f */
					if (v->l_id > 0 && v->l_id <= 6000) w->lead_killed[(v->l_id - 1) >> 3] |= (unsigned char)(1 << ((v->l_id - 1) & 7));
					v->l_id = 0; v->l_type = 0; v->l_rank = v->l_move = v->l_fire = v->l_shock = v->l_siege = 0;
				}
			}
			phase_losses(m, b);
		}
	}
}

/* -------------------------------------------------------------- sieges */
static void build_lists(Military *m)
{
	World *w = m->w;
	int i;
	for (i = 0; i < MAX_PROV; i++) m->loc_head[i] = -1;
	for (i = w->nland - 1; i >= 0; i--) {                 /* reverse push: lists keep unit order */
		const LandUnit *u = &w->land[i];
		if (u->dead || u->location <= 0 || u->location >= MAX_PROV) continue;
		m->loc_next[i] = m->loc_head[u->location];
		m->loc_head[u->location] = (short)i;
	}
}

static void sieges_day(Military *m)
{
	World *w = m->w;
	int p, i;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		Siege *sg = &m->siege[p];
		int bes = -1;
		long men = 0, art = 0;
		if (m->loc_head[p] < 0) { if (sg->besieger) memset(sg, 0, sizeof *sg); continue; }
		if (!land_prov(w, p) || pv->controller < 0) continue;
		if (m->inbat[p]) continue;                                    /* fighting first */
		for (i = m->loc_head[p]; i >= 0; i = m->loc_next[i]) {
			const LandUnit *u = &w->land[i];
			if (u->dead || u->location != p || u->npath || !(atw(m, u->owner, pv->controller) || tot_take(w, u->owner, p))) continue;
			if (bes < 0) bes = u->owner;
			if (u->owner == bes) { men += unit_men(u); art += u->artillery; }
		}
		if (bes < 0) { if (sg->besieger) memset(sg, 0, sizeof *sg); continue; }
		if (!pv->fort) {                                              /* no fortress: controlled at once (§9.5) */
			if (pv->controller >= 0 && bes == pv->owner) world_note(w, NOTE_LIBERATED, bes, p, pv->controller);   /* LIBPROV */
			else if (pv->controller >= 0) { world_note(w, NOTE_SIEGE_WIN, bes, p, pv->controller); world_note(w, NOTE_PROV_LOST, pv->controller, p, bes); siege_vp(w, p, bes, pv->controller); }
			if (bes == w->nat && w->nat >= 0) natives_damage(m, p);
			pv->controller = (short)bes;
			logm(m, "%s occupies %s", w->country[bes].tag, pv->name);
			continue;
		}
		if (sg->besieger != bes + 1) {                              /* a new siege: its garrison (SPEC_ASSAULT 2.2) */
			memset(sg, 0, sizeof *sg); sg->besieger = (short)(bes + 1); sg->next_test = m->today + 30;
			sg->g_inf = 5000L * pv->fort; sg->g_art = 10L * pv->fort;
			if (pv->population < 5000) { sg->g_inf /= 2; sg->g_art /= 2; }
			sg->g_mor = max_morale(m, pv->controller);
		}
		if (sg->g_inf + 100 * sg->g_art < 100) sg->distress = 15;   /* a garrison under 100 men gives up */
		if (men < 5000L * pv->fort || m->today < sg->next_test) continue;   /* cover only */
		{	int lt = w->country[bes].land_tech, iv = lt >= 51 ? 21 - 2 * (lt - 50) : 30;   /* era 6: shorter (SPEC_ASSAULT 1) */
			sg->next_test = m->today + (iv < 7 ? 7 : iv); }
		{	/* §9.3 */
			int roll = mrand(m) % 10 + 1, rating = art >= 40L * pv->fort ? 3 : art >= 25L * pv->fort ? 2 : art >= 10L * pv->fort ? 1 : 0;
			int total = roll + rating - (pv->fort - (int)(sg->starve_m / 1000) - sg->distress);
			world_sfx_at(w, bes, p, "siege_shot"); world_sfx_at(w, pv->controller, p, "siege_shot");   /* one cannon shot per siege test (0x4704a0) */
			sg->starve_m += port_prov(w, p) && !pv->blockaded ? -300 : 300;   /* fed by sea unless blockaded (+0x41c / +0x489) */
			if (sg->starve_m < 0) sg->starve_m = 0;
			if (total >= 14) { logm(m, "%s: the fortress of %s surrenders", w->country[bes].tag, pv->name); siege_won(m, p, bes); } else if (total >= 11) { sg->breaches++; sg->distress += 2; }
			else if (total >= 8) sg->distress += 3;
			else if (total >= 5) sg->distress += 1;
			if (sg->distress > 8) sg->distress = 8;
		}
	}
}

/* ------------------------------------------------------------ rebel armies, AI invasions
 * The countries' armies are run by the military AI of the exe (end of this file, docs/SPEC_MILITARY_AI.md).
 * What is left of our v1 rules (NIEPEWNE, ours): rebel armies (the exe has no rebel AI of its own; how
 * rebel stacks pick their way is NIEZNANE) look for the nearest province (by marching days, at most
 * AI_MAXDAYS) that someone else controls or where an enemy army stands, skipping places whose enemy
 * armies are stronger than 1.2 x theirs, once a week, staggered; AI fleets of a country without an
 * Admiral's home port (D.2) sail home to the nearest own port; and armies left aboard a fleet with no
 * TRANSPORT plan (D.7) land next to it. (The v1 sea invasions are replaced by INVASION / TRANSPORT.) */
#define AI_MAXDAYS 120

static long hostile_men(const Military *m, int prov, int c)
{
	const World *w = m->w;
	long n = 0;
	int i;
	for (i = m->loc_head[prov]; i >= 0; i = m->loc_next[i])
		if (!w->land[i].dead && atw(m, w->land[i].owner, c)) n += unit_men(&w->land[i]);
	return n;
}

/* Dijkstra with a binary heap (lazy deletion), stops at the first wanted province. dist/prev are
 * valid only where stamp == the current search (no clearing of 2100 entries per search). */
static int ai_nearest(Military *m, int ui, long *days)
{
	World *w = m->w;
	LandUnit *u = &w->land[ui];
	static long dist[MAX_PROV], hk[8192];
	static short prev[MAX_PROV], hp[8192];
	static unsigned short stamp[MAX_PROV], cur;
	int nh = 0, src = u->location, c = u->owner, sp = speed_x2(u) == 3 ? 0 : speed_x2(u) == 4 ? 1 : 2;
	long mine = unit_men(u);
	if (++cur == 0) { memset(stamp, 0, sizeof stamp); cur = 1; }
#define DIST(p) (stamp[p] == cur ? dist[p] : 0x7FFFFFFFL)
	stamp[src] = cur; dist[src] = 0; prev[src] = -1;
	hk[0] = 0; hp[0] = (short)src; nh = 1;
	while (nh > 0) {
		long d = hk[0];
		int p = hp[0], k, j;
		unsigned long e;
		nh--;                                            /* pop: move the last to the root, sift down */
		hk[0] = hk[nh]; hp[0] = hp[nh];
		for (k = 0; ; ) {
			int l = 2 * k + 1, r = l + 1, s = k;
			if (l < nh && hk[l] < hk[s]) s = l;
			if (r < nh && hk[r] < hk[s]) s = r;
			if (s == k) break;
			{ long tk = hk[k]; short tp = hp[k]; hk[k] = hk[s]; hp[k] = hp[s]; hk[s] = tk; hp[s] = tp; }
			k = s;
		}
		if (d != DIST(p)) continue;                      /* stale entry */
		if (p != src) {
			const Province *pv = &w->prov[p];
			long en = m->loc_head[p] >= 0 ? hostile_men(m, p, c) : 0;
			if (atw(m, c, pv->controller) || en > 0) {
				if (en * 5 <= mine * 6) {
					int steps = 0, q;
					short rev[64];
					for (q = p; q != src && steps < 64; q = prev[q]) rev[steps++] = (short)q;
					if (steps > 40) steps = 40;
					for (q = 0; q < steps; q++) u->path[q] = rev[steps - 1 - q];
					u->npath = (short)steps; u->pathpos = 0;
					u->arrive_day = m->today + edge_days(m, u, u->path[0], adj_type(m, src, u->path[0]));
					*days = d;
					return p;
				}
				continue;                                /* too strong: do not path through it */
			}
		}
		if (p >= 1616) continue;
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
			int nb = (int)m->arec[2 * e], ty = (int)m->arec[2 * e + 1];
			long nd;
			if (!land_prov(w, nb)) continue;
			nd = d + m->eday[sp][w->prov[nb].terrain < 16 ? w->prov[nb].terrain : 0][ty == 1 ? 1 : ty == 2 ? 2 : 0];
			if (nd >= DIST(nb) || nd > AI_MAXDAYS || nh >= 8192) continue;
			stamp[nb] = cur; dist[nb] = nd; prev[nb] = (short)p;
			for (j = nh++; j > 0 && hk[(j - 1) / 2] > nd; j = (j - 1) / 2) { hk[j] = hk[(j - 1) / 2]; hp[j] = hp[(j - 1) / 2]; }
			hk[j] = nd; hp[j] = (short)nb;
		}
	}
#undef DIST
	return -1;
}

#define AI_SEA_STEPS 8
#define SEA_DAYS 10
#define PORT_DAYS 5
static int sea_prov(const World *w, int p);
static int port_prov(const World *w, int p);
static int adjacent(const Military *m, int a, int b);

/* an idle, empty AI fleet at sea sails home: breadth first over sea zones (at most 2 x AI_SEA_STEPS) to
 * the nearest zone with a port its owner owns and controls, then into that port (ours, NIEPEWNE) */
static void ai_fleet_home(Military *m, int fi)
{
	World *w = m->w;
	NavalUnit *f = &w->naval[fi];
	static short q[MAX_PROV], par[MAX_PROV];
	static unsigned char dep[MAX_PROV];
	static unsigned short st[MAX_PROV], cur;
	int head = 0, tail = 0, port = -1, goal = -1, p;
	if (++cur == 0) { memset(st, 0, sizeof st); cur = 1; }
	q[tail++] = f->location; st[f->location] = cur; par[f->location] = -1; dep[f->location] = 0;
	while (head < tail && port < 0) {
		int z = q[head++];
		unsigned long k;
		for (p = m->zp_head[z]; p > 0; p = m->zp_next[p])                     /* the ports onto z, ascending */
			if (w->prov[p].owner == f->owner && w->prov[p].controller == f->owner) { port = p; goal = z; break; }
		if (port >= 0 || dep[z] >= 2 * AI_SEA_STEPS) continue;
		for (k = m->astart[z]; k < m->astart[z + 1]; k++) {
			int n2 = (int)m->arec[2 * k];
			if (n2 < 1616 && sea_prov(w, n2) && st[n2] != cur) { st[n2] = cur; par[n2] = (short)z; dep[n2] = (unsigned char)(dep[z] + 1); q[tail++] = (short)n2; }
		}
	}
	if (port < 0) return;
	{
		short rev[64];
		int steps = 0, c, i;
		rev[steps++] = (short)port;
		for (c = goal; c >= 0 && c != f->location && steps < 64; c = par[c]) rev[steps++] = (short)c;
		if (steps > 40) return;
		for (i = 0; i < steps; i++) f->path[i] = rev[steps - 1 - i];
		f->npath = (short)steps; f->pathpos = 0;
		f->arrive_day = m->today + (port_prov(w, f->path[0]) ? PORT_DAYS : SEA_DAYS);
		logm(m, "AI: %s fleet %s returns to %s", w->country[f->owner].tag, f->name ? f->name : "?", w->prov[port].name ? w->prov[port].name : "?");
	}
}

/* AI fleets that carry armies and have arrived: the armies land next to them, in a province an enemy
 * controls if there is one, else in one of their own */
static void ai_land_day(Military *m)
{
	World *w = m->w;
	static unsigned char adm[MAX_COUNTRY];
	int i;
	memset(adm, 0, sizeof adm);
	for (i = 0; i < AIG_MAX; i++) if (m->gen[i].c >= 0 && m->gen[i].c < MAX_COUNTRY && m->gen[i].hport > 0) adm[m->gen[i].c] = 1;   /* (saved state only: the day starts here) */
	for (i = 0; i < w->nnaval; i++) {                  /* weekly, staggered: empty fleets at sea go home (not those an Admiral runs) */
		const NavalUnit *f = &w->naval[i];
		if ((i + m->today) % 7 || f->npath || f->owner < 0 || f->owner >= MAX_COUNTRY || w->country[f->owner].human || !sea_prov(w, f->location) || f->location >= 1616) continue;
		if (m->fplan[i] || adm[f->owner]) continue;
		if (f->warships + f->galleys + f->transports <= 0 || mil_fleet_load(w, i) > 0) continue;
		ai_fleet_home(m, i);
	}
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		const NavalUnit *f;
		unsigned long k;
		int t = -1, alt = -1;
		if (u->dead != LAND_ABOARD || u->owner < 0 || w->country[u->owner].human || u->aboard < 0 || u->aboard >= w->nnaval) continue;
		if (m->aplan[i] && m->aplan[i] <= AIP_MAX && m->plan[m->aplan[i] - 1].type == PL_TRANSPORT) continue;   /* its TRANSPORT unloads it */
		f = &w->naval[u->aboard];
		if (f->npath || !sea_prov(w, f->location) || f->location >= 1616) continue;
		for (k = m->astart[f->location]; k < m->astart[f->location + 1] && t < 0; k++) {
			int p = (int)m->arec[2 * k];
			if (!land_prov(w, p)) continue;
			if (atw(m, u->owner, w->prov[p].controller)) t = p;
			else if (alt < 0 && w->prov[p].controller == u->owner) alt = p;
		}
		if (t < 0) t = alt;
		if (t > 0) mil_disembark(m, u->aboard, t);
	}
}

static void milai_day(Military *m);
static void ai_day(Military *m)
{
	World *w = m->w;
	int i;
	if (w->reb >= 0)
		for (i = 0; i < w->nland; i++) {                     /* rebel armies (v1, ours) */
			LandUnit *u = &w->land[i];
			const Province *here;
			long days;
			int t;
			if (u->dead || u->npath || (i + m->today) % 7 || u->owner != w->reb) continue;
			if (!land_prov(w, u->location)) continue;
			here = &w->prov[u->location];
			if (atw(m, u->owner, here->controller)) continue;                   /* besieging: stay */
			if (m->inbat[u->location]) continue;                                 /* fighting */
			if ((t = ai_nearest(m, i, &days)) > 0)
				logm(m, "AI: %s army %s marches to %s (%ld days)", w->country[u->owner].tag, u->name ? u->name : "?", w->prov[t].name, days);
		}
	milai_day(m);
}

#include <time.h>
static clock_t g_prof[6];
void mil_prof(long ms[6]) { int i; for (i = 0; i < 6; i++) ms[i] = (long)(g_prof[i] * 1000 / CLOCKS_PER_SEC); }

static void builds_day(Military *m);
static void fleets_day(Military *m);
static void cargo_day(Military *m);
void mil_day(Military *m, long today)
{
	clock_t c0 = clock(), c1, c2, c3, c4, c5;
	m->today = today;
	build_warbits(m);                                    /* wars change only outside mil_day */
	move_day(m);
	fleets_day(m);
	c1 = clock();                                        /* naval battles: in battles_day (the land engine) */
	cargo_day(m);
	ai_land_day(m);
	build_lists(m);
	battles_day(m);            c2 = clock();
	build_lists(m);                                      /* retreats moved units */
	c3 = clock();
	sieges_day(m);             c4 = clock();
	natives_pacify(m);
	ai_day(m);
	builds_day(m);
	pirates_day(m);
	pirates_refresh(m);        c5 = clock();
	g_prof[0] += c1 - c0; g_prof[1] += c2 - c1; g_prof[2] += c3 - c2; g_prof[3] += c4 - c3; g_prof[4] += c5 - c4;
}

/* ------------------------------------------------------------ attrition (§10) */
/* §10.1 supply value of province p for country c. The neutral/enemy split of 3 or 2 is NIEPEWNE:
 * 3 neutral, 2 at war. */
static long supply_value(const Military *m, int p, int c)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	int o = pv->owner, k = pv->controller, mult, sideO, sideK, flag = 0;
	long s, x;
	/* decoded 0x483570 (land): side = on c's side in a war (0x45fa70, dip +0x3d); 0x4cbe90 = o is c's vassal; 0x4cb880 = dip
	 * byte +0x41 (NIEPEWNE: read as a military alliance) */
	x = o >= 0 ? (long)o * MAX_COUNTRY + c : 0; sideO = o >= 0 && (o == c || ((m->sidebits[x >> 3] >> (x & 7)) & 1));
	x = k >= 0 ? (long)k * MAX_COUNTRY + c : 0; sideK = k >= 0 && (k == c || ((m->sidebits[x >> 3] >> (x & 7)) & 1));
	if (o < 0) mult = 1;
	else if ((sideO || world_overlord(w, o) == c) && sideK) { mult = 5; flag = 1; }
	else if (sideO) { mult = 4; flag = 1; }                                  /* ours (or a co-belligerent's), held by another */
	else if (world_allied(w, o, c, ALLY_MILITARY)) { mult = 4; flag = 1; }
	else mult = sideK ? 3 : 2;
	s = pv->income_m / 1000 * mult;
	if (pv->blockaded && k != c && port_prov(w, p)) s *= 2;              /* a blockaded port held by another */
	if (o == c || flag) { s += 10 * pv->fort; if (pv->bflags & PB_CONSCRIPT) s += 10; }   /* fort x 10, a conscription centre +10 */
	return s;
}

/* §10.2 maximum attrition (percent), decoded 0x4837f0 / 0x483b60 (terrain codes: Map/Terrain types.csv, 2 mountain, 4 marsh) */
/* 0x43a530: k is c, on c's side in a war (dip +0x3d), or c's overlord (0x4cbe90: a vassalage led by k holding c) */
static int sup_friend(const Military *m, int k, int c)
{
	long x;
	if (k == c) return 1;
	if (k < 0 || c < 0) return 0;
	x = (long)k * MAX_COUNTRY + c;
	if ((m->sidebits[x >> 3] >> (x & 7)) & 1) return 1;
	return world_overlord(m->w, c) == k;
}
/* 0x483b60: p supplies c - it has people (+0x80 > 0, NIEPEWNE), a controller, an owner that is c's friend or enemy (not a
 * neutral), and is held by a friend - or, under siege, besieged by one */
static int sup_base(const Military *m, int p, int c)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	int o = pv->owner, k = pv->controller;
	if (pv->population <= 0 || k < 0 || o < 0) return 0;
	if (!sup_friend(m, o, c) && !atw(m, o, c)) return 0;
	if (m->siege[p].besieger) return sup_friend(m, m->siege[p].besieger - 1, c);
	return sup_friend(m, k, c);
}
/* 0x4837f0: the maximum attrition of c in p; besieging (a unit in a siege) only the base, +10 without a port or blockaded */
static long max_attrition_b(const Military *m, int p, int c, int besieging)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	long a;
	int ok = 0;
	unsigned long k;
	if (pv->terrain == 5) return 15;                                      /* at sea */
	a = 10 - w->country[c].land_tech / 6;
	if (a < 1) a = 1;
	if (besieging) return port_prov(w, p) && !pv->blockaded ? a : a + 10;
	if (sup_base(m, p, c)) ok = 1;                                        /* out of supply: neither p nor a neighbour supplies */
	else if (p < 1616 && m->astart)
		for (k = m->astart[p]; k < m->astart[p + 1] && !ok; k++) { int n = (int)m->arec[2 * k]; if (n > 0 && n < w->nprov && sup_base(m, n, c)) ok = 1; }
	if (!ok) a += 10;
	if (pv->winter) a += 10;                                              /* winter (+0x3e2) */
	if (pv->climate == 4 || pv->climate == 6) a += 5;                     /* tropical */
	if (pv->looted) a += 5;                                               /* looted (+0x3cc) */
	if (pv->terrain == 4) a += 2;                                         /* marsh */
	if (pv->terrain == 2) a += 5;                                         /* mountains */
	if (pv->climate == 1) a += 8;                                         /* desert */
	return a;
}
static long max_attrition(const Military *m, int p, int c) { return max_attrition_b(m, p, c, 0); }

long mil_supply(const Military *m, int p, int c) { return p > 0 && p < m->w->nprov && c >= 0 ? supply_value(m, p, c) : 0; }
long mil_attrition(const Military *m, int p, int c) { return p > 0 && p < m->w->nprov && c >= 0 ? max_attrition(m, p, c) : 0; }
int  mil_adjacent(const Military *m, int p, short *out, int max)
{
	unsigned long k;
	int n = 0;
	if (p <= 0 || p >= m->w->nprov || !m->astart) return 0;
	for (k = m->astart[p]; k < m->astart[p + 1] && n < max; k++) out[n++] = (short)m->arec[2 * k];
	return n;
}

/* §10.3, applied once a month as a percentage of each unit's men (deterministic: the application
 * site was not read, NIEPEWNE whether the original randomises it) */
/* 0x48e930: the unit is supplied - land of its own, of its vassal (0x4cbe90) or of a friend (0x43b740 -> 0x4cb900: royal
 * marriage, the same military alliance, on its side in a war, vassalage), and in supply (0x483940) */
static int ai_in_supply(const Military *m, int c, int p);
static int loot_supplied(const Military *m, const LandUnit *u)
{
	const World *w = m->w;
	int o = w->prov[u->location].owner, c = u->owner;
	if (o < 0) return 0;
	if (o != c && world_overlord(w, o) != c && world_overlord(w, c) != o && !world_allied(w, o, c, ALLY_DYNASTIC) && !world_allied(w, o, c, ALLY_MILITARY)) {
		long x = (long)o * MAX_COUNTRY + c;
		if (!((m->sidebits[x >> 3] >> (x & 7)) & 1)) return 0;
	}
	return ai_in_supply(m, c, u->location);
}
/* 0x486210: a land province with a real city (not a colony), not looted already, ours or a hostile owner's */
static int may_loot(const Military *m, const LandUnit *u)
{
	const World *w = m->w;
	const Province *pv = &w->prov[u->location];
	if (u->dead || !land_prov(w, u->location) || pv->looted || pv->population < 1000 || pv->owner < 0) return 0;
	return pv->owner == u->owner || atw(m, pv->owner, u->owner);
}

static void attrition_month(Military *m)
{
	World *w = m->w;
	int i, j;
	build_lists(m);
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		const Province *pv;
		long a = 0, men, loss;
		if (u->dead || u->location <= 0 || u->location >= w->nprov || !land_prov(w, u->location)) continue;
		pv = &w->prov[u->location];
		for (j = m->loc_head[u->location]; j >= 0; j = m->loc_next[j]) {          /* everyone in the province */
			const LandUnit *v = &w->land[j];
			if (v->dead) continue;
			a += (v->infantry + 999) / 1000 + (v->cavalry + 999) / 1000 + (v->artillery + 9) / 10;
		}
		if (pv->winter) { a += 10; if (pv->owner != u->owner) a += 15; }   /* 0x486056: winter, +15 more abroad */
		if (pv->climate == 4 || pv->climate == 6) a += 5;
		a -= supply_value(m, u->location, u->owner);
		if (u->l_id) a -= 2L * u->l_move * (u->l_type == 2 ? 4 : 1);                 /* §10.3: the leader, x4 for a conquistador */
		{ long mx = max_attrition_b(m, u->location, u->owner, m->siege[u->location].besieger == u->owner + 1 && !u->npath); if (a > mx) a = mx; }   /* 0x4860ff: besieging (0x48df30) */
		men = unit_men(u);
		if (men <= 0) continue;
		if (!loot_supplied(m, u) && may_loot(m, u)) {                                /* 0x4861b1..0x4861c7 */
			Province *lp = &w->prov[u->location];
			w->country[u->owner].treasury_m += 2 * (lp->income_m < 0 ? 0 : lp->income_m); world_book_inc(w, u->owner, INC_TAX, 2 * (lp->income_m < 0 ? 0 : lp->income_m));   /* 0x5f (category +0x10 = 0): twice the base tax to the looter */
			lp->looted = 1; lp->loot_until = m->today + 360;                    /* 0x5d, undone by 0x5e a year later */
			if (lp->owner != u->owner && lp->manu && mrand(m) % 100 < 5) lp->manu = 0;   /* 0x4d97c0(p, 5): command 0x7d clears the manufactory */
			logm(m, "%s army %s loots %s", w->country[u->owner].tag, u->name ? u->name : "?", lp->name ? lp->name : "?");
		}
		if (a > 0 && a * men < 1000) a = (1000 + men - 1) / men;
		if (a < 1) a = u->npath || (u->l_id && u->l_type == 2) ? 0 : 1;            /* minimum 1 %, none on the march or under a conquistador */
		if (!a) continue;
		loss = men * a / 100;
		u->infantry -= u->infantry * a / 100; u->cavalry -= u->cavalry * a / 100; u->artillery -= u->artillery * a / 100;
		if (unit_men(u) <= 0 || (u->infantry < 50 && u->cavalry < 50 && u->artillery <= 0)) {
			u->dead = 1;
			logm(m, "%s army %s perishes from attrition in %s", w->country[u->owner].tag, u->name ? u->name : "?", pv->name ? pv->name : "?");
		} else if (a >= 5) logm(m, "%s army in %s loses %ld men to attrition (%ld %%)", w->country[u->owner].tag, pv->name ? pv->name : "?", loss, a);
	}
}


static void builds_day(Military *m);
/* discovery: every unit shows its owner its province and the neighbours (monthly; NIEPEWNE: the
 * original's explorers and conquistadors reveal more) */
static void discover_month(Military *m) __attribute__((unused));
static void discover_month(Military *m)
{
	World *w = m->w;
	int i;
	unsigned long k;
	for (i = 0; i < w->nland + w->nnaval; i++) {
		int land = i < w->nland, o, loc;
		if (land) { const LandUnit *u = &w->land[i]; if (u->dead == 1) continue; o = u->owner; loc = u->location; }
		else { const NavalUnit *u = &w->naval[i - w->nland]; if (u->warships + u->galleys + u->transports <= 0) continue; o = u->owner; loc = u->location; }
		if (o < 0 || o >= w->ncountry || loc <= 0 || loc >= w->nprov) continue;
		WORLD_DISCOVER(w, o, loc);
		if (m->astart) for (k = m->astart[loc]; k < m->astart[loc + 1]; k++) { int q = (int)m->arec[2 * k]; if (q > 0 && q < w->nprov) WORLD_DISCOVER(w, o, q); }
	}
}

static void merc_month(Military *m)
{
	const World *w = m->w;
	int y = w->now.year, cap = (y < 1500 ? 6 : y < 1700 ? 5 : 1) + w->nwar;
	if (m->nmerc >= cap || m->nmerc >= 16) return;
	m->merc[m->nmerc].inf = (3 + mrand(m) % 6) * 1000L;
	m->merc[m->nmerc].cav = (1 + mrand(m) % 4) * 500L;
	m->merc[m->nmerc].art = y >= 1500 && mrand(m) % 100 < 40 ? (1 + mrand(m) % 3) * 5L : 0;
	m->nmerc++;
}

int mil_merc_offer(const Military *m, long *inf, long *cav, long *art)
{
	if (m->nmerc <= 0) return 0;
	*inf = m->merc[0].inf; *cav = m->merc[0].cav; *art = m->merc[0].art;
	return 1;
}

long mil_merc_price(const Military *m, int c)
{
	long p, infl = m->w->country[c].inflation_u > 0 ? m->w->country[c].inflation_u : 0;
	if (m->nmerc <= 0) return 0;
	p = m->merc[0].inf * 20 + m->merc[0].cav * 40 + m->merc[0].art * 6500;         /* x1000 */
	return p / 1000 * (1000000L + infl) / 1000;                                       /* x (1 + inflation) */
}

int mil_merc_access(const Military *m, int c, int prov)
{
	const World *w = m->w;
	int cap = w->country[c].capital;
	if (cap <= 0 || cap >= w->nprov || prov <= 0 || prov >= w->nprov || m->europe < 0) return 0;
	return w->prov[cap].continent == m->europe && w->prov[prov].continent == w->prov[cap].continent;
}

int mil_hire_merc(Military *m, int c, int prov)
{
	World *w = m->w;
	long price = mil_merc_price(m, c);
	int k;
	if (m->nmerc <= 0) return MIL_RC_FULL;
	if (!mil_merc_access(m, c, prov) || w->prov[prov].owner != c || w->prov[prov].controller != c) return MIL_RC_BADPROV;
	if (w->country[c].treasury_m < price) return MIL_RC_MONEY;
	if ((k = mil_new_army(m, c, prov, m->merc[0].inf, "Mercenaries")) < 0) return MIL_RC_FULL;
	w->land[k].cavalry = m->merc[0].cav; w->land[k].artillery = m->merc[0].art;
	w->country[c].treasury_m -= price; world_book_exp(w, c, EXP_ARMY, price);
	memmove(&m->merc[0], &m->merc[1], sizeof m->merc[0] * (size_t)(m->nmerc - 1));
	m->nmerc--;
	logm(m, "%s hires mercenaries in %s for %ld ducats", w->country[c].tag, w->prov[prov].name ? w->prov[prov].name : "?", price / 1000);
	return k;
}

void mil_month(Military *m)
{
	build_warbits(m);                                    /* fresh at the month step (a loaded game has no day behind it) */
	merc_month(m);
	/* discover_month(m): replaced by discovery on arrival (SPEC_EXPLORERS 4.2) */
	World *w = m->w;
	int i;
	/* morale (the monthly upkeep 0x4de6c6 -> unit vfunc +0x98 = 0x487530 / 0x48a5e0): units not in a battle (a siege does not
	 * count) move 0.3 x their maximum towards it, from below or from above (rebels: their source country's maximum, NIEPEWNE:
	 * ours REB's) */
	{ static unsigned char inb[MAX_PROV]; int kb;
	memset(inb, 0, sizeof inb);
	for (kb = 0; kb < MAXB; kb++) if (m->b[kb].active && m->b[kb].prov > 0 && m->b[kb].prov < MAX_PROV) inb[m->b[kb].prov] = 1;   /* the battles now running */
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		long M;
		if (u->dead || (u->location > 0 && u->location < MAX_PROV && inb[u->location])) continue;
		M = max_morale(m, u->owner);
		if (u->morale_m < M) { u->morale_m += M * 3 / 10; if (u->morale_m > M) u->morale_m = M; }
		else if (u->morale_m > M) { u->morale_m -= M * 3 / 10; if (u->morale_m < M) u->morale_m = M; }
	}
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *u = &w->naval[i];
		long M;
		if (u->warships + u->galleys + u->transports <= 0 || !u->morale_m || (u->location > 0 && u->location < MAX_PROV && inb[u->location])) continue;
		M = nmax_morale(m, u->owner);
		if (u->morale_m < M) { u->morale_m += M * 3 / 10; if (u->morale_m > M) u->morale_m = M; }
		else if (u->morale_m > M) { u->morale_m -= M * 3 / 10; if (u->morale_m < M) u->morale_m = M; }
	}
	}
	attrition_month(m);
	naval_attrition(m);
	pirates_tech(m);
}

/* countries whose provinces touch by land (ADJACENT.TBL), as a bit matrix a * MAX_COUNTRY + b */
void mil_neighbour_bits(const Military *m, unsigned char *bits)
{
	const World *w = m->w;
	int p;
	memset(bits, 0, (MAX_COUNTRY * MAX_COUNTRY + 7) / 8);
	for (p = 1; p < w->nprov && p < 1616; p++) {
		unsigned long k;
		int a = w->prov[p].owner;
		if (a < 0) continue;
		for (k = m->astart[p]; k < m->astart[p + 1]; k++) {
			int nb = (int)m->arec[2 * k], b;
			long i;
			if (nb <= 0 || nb >= w->nprov || (b = w->prov[nb].owner) < 0 || b == a) continue;
			i = (long)a * MAX_COUNTRY + b; bits[i >> 3] |= (unsigned char)(1 << (i & 7));
			i = (long)b * MAX_COUNTRY + a; bits[i >> 3] |= (unsigned char)(1 << (i & 7));
		}
	}
}

/* save game: the RNG, battles in progress and siege progress, as integer lists */
void mil_save(const Military *m, FILE *f)
{
	int k, p;
	fprintf(f, "amieu2_military = {\n\tseed = { %ld %ld } explmiss = %d pirates = { %ld %d }\n\tmercs = {", (long)(m->seed >> 16), (long)(m->seed & 0xFFFF), m->explmiss, m->pir_next, m->pir_last);
	for (k = 0; k < m->nmerc; k++) fprintf(f, " %ld %ld %ld", m->merc[k].inf, m->merc[k].cav, m->merc[k].art);
	fprintf(f, " }\n\tblockades = {");                          /* as of the last mil_day (wars may have changed since) */
	for (p = 1; p < m->w->nprov; p++) if (m->w->prov[p].blockaded || m->w->prov[p].pirates || m->w->prov[p].hostile) fprintf(f, " %d %d %d", p, m->w->prov[p].blockaded | m->w->prov[p].hostile << 1, m->w->prov[p].pirates);
	fprintf(f, " }\n\tbattles = {");
	for (k = 0; k < MAXB; k++) {
		const Battle *b = &m->b[k];
		if (!b->active) continue;
		fprintf(f, "\n\t\t%d %d %d %d %d %d %ld %ld %ld %ld %ld %ld %ld %d %d", b->prov, b->attacker, b->phase, b->day, b->die[0], b->die[1],
		        b->men_day[0], b->men_day[1], b->mor_day[0], b->mor_day[1], b->started, b->lost[0], b->lost[1], b->lastc[0], b->lastc[1]);
	}
	fprintf(f, " }\n\tbslot = {");                     /* their slots: a new battle takes the lowest free one, the order is the processing order */
	for (k = 0; k < MAXB; k++) if (m->b[k].active) fprintf(f, " %d", k);
	fprintf(f, " }\n\tbmen0 = {");                     /* the battles' starting men, in the same order */
	for (k = 0; k < MAXB; k++) if (m->b[k].active) fprintf(f, " %ld %ld", m->b[k].men0[0], m->b[k].men0[1]);
	fprintf(f, " }\n\tbfrac = {");                     /* naval: ship losses not yet taken */
	for (k = 0; k < MAXB; k++) if (m->b[k].active) fprintf(f, " %ld %ld", m->b[k].frac[0], m->b[k].frac[1]);
	fprintf(f, " }\n\tbassault = {");
	for (k = 0; k < MAXB; k++) if (m->b[k].active) fprintf(f, " %d", m->b[k].assault);
	fprintf(f, " }\n\tsieges = {");
	for (p = 0; p < m->w->nprov && p < MAX_PROV; p++) {
		const Siege *s = &m->siege[p];
		if (!s->besieger && !s->next_test) continue;
		fprintf(f, "\n\t\t%d %ld %ld %d %d %d %ld %ld %ld", p, s->next_test, s->starve_m, s->distress, s->breaches, s->besieger, s->g_inf, s->g_art, s->g_mor);
	}
	/* the military AI: King, build orders, Generals, plans, armies' plans */
	fprintf(f, " }\n\taiking = {");
	for (k = 0; k < MAX_COUNTRY; k++) {
		const MilKing *K = &m->king[k];
		if (K->next || K->war || K->bf[0] || K->bf[1] || K->bf[2]) fprintf(f, "\n\t\t%d %ld %d %d %d %d", k, K->next, K->war, K->bf[0], K->bf[1], K->bf[2]);
	}
	fprintf(f, " }\n\taiord = {");
	for (k = 0; k < MAX_COUNTRY; k++)
		for (p = 0; p < AIO_MAX; p++) {
			const MilOrder *o = &m->ord[k][p];
			if (o->prov > 0) fprintf(f, "\n\t\t%d %d %ld %ld %ld %d %d %d", k, p, o->inf, o->cav, o->art, o->prov, o->plan, o->prio);
		}
	fprintf(f, " }\n\taigen = {");
	for (k = 0; k < AIG_MAX; k++) if (m->gen[k].c >= 0) fprintf(f, "\n\t\t%d %d %d %ld %ld", k, m->gen[k].c, m->gen[k].grp, m->gen[k].next, m->gen[k].dnext);
	fprintf(f, " }\n\taiplan = {");
	for (k = 0; k < AIP_MAX; k++) {
		const MilPlan *pl = &m->plan[k];
		if (pl->type == PL_FREE) continue;
		fprintf(f, "\n\t\t%d %d %d %d %d %d %d %d %d %ld %ld %ld %ld %ld", k, pl->type, pl->phase, pl->engaged, pl->asked, pl->c, pl->gen, pl->target, pl->gather,
		        pl->score, pl->need, pl->own, pl->next, pl->power);
	}
	fprintf(f, " }\n\taiarmy = {");
	for (k = 0; k < m->w->nland && k < MAX_LAND; k++) if (m->aplan[k] || m->astuck[k] > m->today) fprintf(f, " %d %d %ld", k, m->aplan[k], m->astuck[k] > m->today ? m->astuck[k] : 0L);
	fprintf(f, " }\n\taiadm = {");                    /* the Generals' Admirals and expeditions: k home port, next think, expedition, its enemy */
	for (k = 0; k < AIG_MAX; k++) if (m->gen[k].c >= 0) fprintf(f, "\n\t\t%d %d %ld %d %d", k, m->gen[k].hport, m->gen[k].anext, m->gen[k].exp, m->gen[k].foe);
	fprintf(f, " }\n\taiplanx = {");                  /* TRANSPORT plans: k army fleet ezone lzone land parent t0 */
	for (k = 0; k < AIP_MAX; k++) {
		const MilPlan *pl = &m->plan[k];
		if (pl->type == PL_TRANSPORT || pl->type == PL_EXSEA || pl->type == PL_EXLAND || pl->type == PL_KILLNAT) fprintf(f, "\n\t\t%d %d %d %d %d %d %d %ld", k, pl->army, pl->fleet, pl->ezone, pl->lzone, pl->land, pl->parent, pl->t0);
	}
	fprintf(f, " }\n\taifleet = {");                  /* fleet, plan + 1 */
	for (k = 0; k < m->w->nnaval && k < MAX_NAVAL; k++) if (m->fplan[k]) fprintf(f, " %d %d", k, m->fplan[k]);
	fprintf(f, " }\n}\n");
}

static int read_longs(const SNode **q, long *v, int n)        /* next n bare numbers of a list */
{
	int got = 0;
	for (; *q && got < n; *q = (*q)->next) if (!(*q)->key && (*q)->val) v[got++] = parse_int((*q)->val);
	return got;
}

void mil_restore(Military *m, const SNode *root)
{
	const SNode *blk = script_get(root, "amieu2_military"), *q;
	long v[16];
	int k = 0;
	if (!blk) return;
	q = script_get(blk, "seed"); q = q ? q->child : NULL;
	if (read_longs(&q, v, 2) == 2) m->seed = (unsigned long)v[0] << 16 | (unsigned long)v[1];
	m->explmiss = (int)script_int(blk, "explmiss", 0);
	q = script_get(blk, "pirates"); q = q ? q->child : NULL;            /* ours: saved (the exe runs the spawner again on a load, 1.4) */
	m->pir_next = 0; m->pir_last = 0;
	if (read_longs(&q, v, 2) == 2) { m->pir_next = v[0]; m->pir_last = (short)v[1]; }
	{	const SNode *mq = script_get(blk, "mercs");
		long mv[3];
		m->nmerc = 0;
		mq = mq ? mq->child : NULL;
		while (m->nmerc < 16 && read_longs(&mq, mv, 3) == 3) { m->merc[m->nmerc].inf = mv[0]; m->merc[m->nmerc].cav = mv[1]; m->merc[m->nmerc].art = mv[2]; m->nmerc++; }
	}
	memset(m->b, 0, sizeof m->b);
	memset(m->inbat, 0, sizeof m->inbat);
	q = script_get(blk, "battles"); q = q ? q->child : NULL;
	while (k < MAXB && read_longs(&q, v, 15) == 15) {
		Battle *b = &m->b[k++];
		b->active = 1; b->prov = (short)v[0]; b->attacker = (short)v[1]; b->phase = (unsigned char)v[2]; b->day = (unsigned char)v[3];
		b->die[0] = (unsigned char)v[4]; b->die[1] = (unsigned char)v[5]; b->men_day[0] = v[6]; b->men_day[1] = v[7];
		b->mor_day[0] = v[8]; b->mor_day[1] = v[9]; b->started = v[10]; b->lost[0] = v[11]; b->lost[1] = v[12];
		b->lastc[0] = (short)v[13]; b->lastc[1] = (short)v[14];
		if (b->prov > 0 && b->prov < MAX_PROV) m->inbat[b->prov] = 1;
	}
	{	const SNode *bq = script_get(blk, "bmen0");
		long mv[2];
		int kk = 0;
		bq = bq ? bq->child : NULL;
		while (kk < k && read_longs(&bq, mv, 2) == 2) { m->b[kk].men0[0] = mv[0]; m->b[kk].men0[1] = mv[1]; kk++; }
		bq = script_get(blk, "bfrac"); bq = bq ? bq->child : NULL; kk = 0;
		while (kk < k && read_longs(&bq, mv, 2) == 2) { m->b[kk].frac[0] = mv[0]; m->b[kk].frac[1] = mv[1]; kk++; }
		bq = script_get(blk, "bassault"); bq = bq ? bq->child : NULL; kk = 0;
		while (kk < k && read_longs(&bq, mv, 1) == 1) m->b[kk++].assault = (unsigned char)mv[0];
		bq = script_get(blk, "bslot"); bq = bq ? bq->child : NULL; kk = 0;
		if (bq) {                                        /* back into their slots */
			static Battle tmp[MAXB];
			short sl[MAXB];
			int ok = 1, j2;
			while (kk < k && read_longs(&bq, mv, 1) == 1) { sl[kk] = (short)mv[0]; if (mv[0] < 0 || mv[0] >= MAXB) ok = 0; kk++; }
			for (j2 = 0; j2 < kk && ok; j2++) { int j3; for (j3 = 0; j3 < j2; j3++) if (sl[j3] == sl[j2]) ok = 0; }
			if (ok && kk == k) {
				memcpy(tmp, m->b, sizeof tmp);
				memset(m->b, 0, sizeof m->b);
				for (j2 = 0; j2 < k; j2++) m->b[sl[j2]] = tmp[j2];
			}
		}
	}
	q = script_get(blk, "sieges"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 9) == 9)
		if (v[0] >= 0 && v[0] < MAX_PROV) {
			Siege *s = &m->siege[v[0]];
			s->next_test = v[1]; s->starve_m = v[2]; s->distress = (short)v[3]; s->breaches = (short)v[4]; s->besieger = (short)v[5];
			s->g_inf = v[6]; s->g_art = v[7]; s->g_mor = v[8];
		}
	/* the military AI */
	memset(m->king, 0, sizeof m->king); memset(m->ord, 0, sizeof m->ord); memset(m->plan, 0, sizeof m->plan); memset(m->aplan, 0, sizeof m->aplan); memset(m->astuck, 0, sizeof m->astuck);
	for (k = 0; k < AIG_MAX; k++) { memset(&m->gen[k], 0, sizeof m->gen[k]); m->gen[k].c = -1; }
	q = script_get(blk, "aiking"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 6) == 6)
		if (v[0] >= 0 && v[0] < MAX_COUNTRY) { MilKing *K = &m->king[v[0]]; K->next = v[1]; K->war = (unsigned char)v[2]; K->bf[0] = (short)v[3]; K->bf[1] = (short)v[4]; K->bf[2] = (short)v[5]; }
	q = script_get(blk, "aiord"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 8) == 8)
		if (v[0] >= 0 && v[0] < MAX_COUNTRY && v[1] >= 0 && v[1] < AIO_MAX) {
			MilOrder *o = &m->ord[v[0]][v[1]];
			o->inf = v[2]; o->cav = v[3]; o->art = v[4]; o->prov = (short)v[5]; o->plan = (short)v[6]; o->prio = (short)v[7];
		}
	q = script_get(blk, "aigen"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 5) == 5)
		if (v[0] >= 0 && v[0] < AIG_MAX) { MilGen *g = &m->gen[v[0]]; g->c = (short)v[1]; g->grp = (short)v[2]; g->next = v[3]; g->dnext = v[4]; }
	q = script_get(blk, "aiplan"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 14) == 14)
		if (v[0] >= 0 && v[0] < AIP_MAX) {
			MilPlan *pl = &m->plan[v[0]];
			pl->type = (unsigned char)v[1]; pl->phase = (unsigned char)v[2]; pl->engaged = (unsigned char)v[3]; pl->asked = (unsigned char)v[4];
			pl->c = (short)v[5]; pl->gen = (short)v[6]; pl->target = (short)v[7]; pl->gather = (short)v[8];
			pl->score = v[9]; pl->need = v[10]; pl->own = v[11]; pl->next = v[12]; pl->power = v[13];
		}
	q = script_get(blk, "aiarmy"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 3) == 3) if (v[0] >= 0 && v[0] < MAX_LAND) { if (v[1] > 0 && v[1] <= AIP_MAX) m->aplan[v[0]] = (unsigned short)v[1]; m->astuck[v[0]] = v[2]; }
	for (k = 0; k < AIG_MAX; k++) m->gen[k].foe = -1;
	q = script_get(blk, "aiadm"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 5) == 5)
		if (v[0] >= 0 && v[0] < AIG_MAX) { MilGen *g = &m->gen[v[0]]; g->hport = (short)v[1]; g->anext = v[2]; g->exp = (unsigned char)v[3]; g->foe = (short)v[4]; }
	for (k = 0; k < AIP_MAX; k++) { MilPlan *pl = &m->plan[k]; pl->army = pl->fleet = pl->parent = -1; pl->ezone = pl->lzone = pl->land = 0; pl->t0 = 0; }
	q = script_get(blk, "aiplanx"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 8) == 8)
		if (v[0] >= 0 && v[0] < AIP_MAX) {
			MilPlan *pl = &m->plan[v[0]];
			pl->army = (short)v[1]; pl->fleet = (short)v[2]; pl->ezone = (short)v[3]; pl->lzone = (short)v[4]; pl->land = (short)v[5]; pl->parent = (short)v[6]; pl->t0 = v[7];
		}
	memset(m->fplan, 0, sizeof m->fplan);
	q = script_get(blk, "aifleet"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 2) == 2) if (v[0] >= 0 && v[0] < MAX_NAVAL && v[1] > 0 && v[1] <= AIP_MAX) m->fplan[v[0]] = (unsigned short)v[1];
	build_warbits(m);
	pirates_refresh(m);                                  /* besieged; blockaded and P as saved: */
	for (k = 1; k < m->w->nprov; k++) { m->w->prov[k].blockaded = 0; m->w->prov[k].pirates = 0; m->w->prov[k].hostile = 0; }
	q = script_get(blk, "blockades"); q = q ? q->child : NULL;
	while (read_longs(&q, v, 3) == 3) if (v[0] > 0 && v[0] < m->w->nprov) { m->w->prov[v[0]].blockaded = (unsigned char)(v[1] & 1); m->w->prov[v[0]].hostile = (unsigned char)((v[1] >> 1) & 1); m->w->prov[v[0]].pirates = (short)v[2]; }
}

/* ------------------------------------------------------------ recruitment
 * Unit cost from SPEC_TIME_ECONOMY §6.2 (base 9 / 21 / 30, x (1 + inflation), h = 0). What a recruit
 * costs is NIEZNANE in the spec; ours (NIEPEWNE): one batch = 1000 infantry, 1000 cavalry or 10 artillery pieces, price = that
 * unit cost, 1000 men of manpower. The time is decoded (0x437e8c..0x437ed5): the order's date + 2 months for infantry, 3 for
 * cavalry, 4 for artillery - the slowest kind in the order (0x429800 / 820 / 840; months of 30 days). */
#define BUILD_DAYS(inf, cav, art) ((art) > 0 ? 120 : (cav) > 0 ? 90 : 60)

/* windows4 1.4: the price per 1000 men / 10 cannons / ship from the domestic policy (d_i = slider - 5),
 * + (difficulty - 2) for a human, x (1 + inflation), at least 1 */
static long unit_price(const World *w, int c, int kind)
{
	const Country *co = &w->country[c];
	long d0 = co->policy[0] - 5, d4 = co->policy[4] - 5, d5 = co->policy[5] - 5, d6 = co->policy[6] - 5, d7 = co->policy[7] - 5, b10, b;
	switch (kind) {                                          /* x10 */
	case 0: b10 = 90 - 6 * d5 + 4 * d6 - 4 * d7; break;       /* infantry */
	case 1: b10 = 210 - 14 * d0 - 10 * d5 + 4 * d6; break;    /* cavalry */
	case 2: b10 = 300 + 20 * d4; break;                       /* artillery */
	case 3: b10 = 500 + 20 * d0 + 20 * d5; break;             /* warship */
	case 4: b10 = 100 + 6 * d5; break;                        /* galley */
	default: b10 = 150 + 10 * d5; break;                      /* transport */
	}
	b = (b10 + (co->human ? 10L * w->opt_diff : 0)) / 10;
	if (b < 0) b = 0;
	b = b * (1000000L + (co->inflation_u > 0 ? co->inflation_u : 0)) / 1000000L;
	return (b < 1 ? 1 : b) * 1000;                           /* ducats x1000 */
}

long mil_recruit_cost(const Military *m, int c, int kind)
{
	if (kind < 0 || kind > 2) return 0;
	return unit_price(m->w, c, kind);
}

static int core_of(const Province *p, int c) { int k; for (k = 0; k < 8; k++) if (p->cores[k] == c) return 1; return 0; }

static long recruit_cap(const Military *m, int c, int prov, int kind, long q);
long mil_recruit_avail(const Military *m, int c, int prov, int kind)
{
	const World *w = m->w;
	long q = 0;
	int i;
	if (prov <= 0 || prov >= w->nprov || kind < 0 || kind > 5) return 0;
	for (i = 0; i < w->nbuild; i++) {
		const Build *b = &w->build[i];
		if (b->country != c || b->prov != prov) continue;
		if (kind < 3 && !b->ship) q += b->inf + b->cav + 100L * b->art;
		else if (kind >= 3 && b->ship >= 1 && b->ship <= 3) q += 5000L * b->inf;
	}
	return recruit_cap(m, c, prov, kind, q);
}

/* the capacity with q (men, or 5000 x ships) already queued there (the military AI keeps q per province) */
static long recruit_cap(const Military *m, int c, int prov, int kind, long q)
{
	const World *w = m->w;
	const Province *p;
	long cap, a;
	if (prov <= 0 || prov >= w->nprov || kind < 0 || kind > 5) return 0;
	p = &w->prov[prov];
	if (p->owner != c || p->controller != c || p->population < 1000 || !land_prov(w, prov)) return 0;
	cap = p->income_m;                                       /* tax x 1000 */
	if (w->country[c].war_tax > 0) cap = cap * 5 / 2;         /* war taxes: the tax multiplier starts at 2.5 (SPEC_SMALLRULES 1.3) */
	if (!core_of(p, c)) cap /= 2;
	if (kind < 3) {
		if (p->bflags & 0x02) cap *= 2;                      /* conscription centre (buildings.h PB_CONSCRIPT) */
		a = cap / 1000;
		if (a > w->country[c].manpower_m / 1000) a = w->country[c].manpower_m / 1000;
		a -= q / 1000;
		if (a < 0) a = 0;
		return kind == 2 ? a * 10 : a * 1000;
	}
	if (!port_prov(w, prov)) return 0;
	if (p->bflags & 0x01) cap *= 10;                         /* shipyard (PB_SHIPYARD) */
	if (cap > 0 && cap < 5000) cap = 5000;
	a = (cap - q) / 5000;
	return a < 0 ? 0 : a;
}

int mil_recruit(Military *m, int c, int prov, int kind, long today)
{
	World *w = m->w;
	Country *co = &w->country[c];
	Province *p;
	Build *b;
	long cost = mil_recruit_cost(m, c, kind);
	if (prov <= 0 || prov >= w->nprov || kind < 0 || kind > 2) return MIL_RC_BADPROV;
	p = &w->prov[prov];
	if (p->owner != c || p->controller != c || p->population <= 0 || !land_prov(w, prov)) return MIL_RC_BADPROV;
	if (co->treasury_m < cost) return MIL_RC_MONEY;
	if (co->manpower_m < 1000) return MIL_RC_MANPOWER;
	if (w->nbuild >= MAX_BUILD) return MIL_RC_FULL;
	co->treasury_m -= cost; world_book_exp(m->w, (int)(co - m->w->country), EXP_ARMY, cost);
	co->manpower_m -= 1000;
	b = &w->build[w->nbuild++];
	b->country = (short)c; b->prov = (short)prov; b->ship = 0;
	b->inf = kind == 0 ? 1000 : 0; b->cav = kind == 1 ? 1000 : 0; b->art = kind == 2 ? 10 : 0;
	b->done = today + BUILD_DAYS(b->inf, b->cav, b->art);
	logm(m, "%s raises %s in %s (%ld ducats), ready in %d days", co->tag, kind == 0 ? "1000 infantry" : kind == 1 ? "1000 cavalry" : "10 artillery",
	     p->name ? p->name : "?", cost / 1000, BUILD_DAYS(b->inf, b->cav, b->art));
	return MIL_RC_OK;
}

/* finished recruits join the owner's first unit in the province, else become a new army */
static void builds_day(Military *m)
{
	World *w = m->w;
	int i, k;
	for (i = 0; i < w->nbuild; ) {
		Build *b = &w->build[i];
		const Province *p = &w->prov[b->prov];
		if (b->done > m->today) { i++; continue; }
		if (b->ship >= BK_TAXCOL) bld_done(w, b);        /* an improvement (buildings.c) */
		else if (b->ship == 5) {                              /* a missionary's work (envoys.c): rolled when sent */
			Province *pp = &w->prov[b->prov];
			if (b->inf && pp->owner == b->country) { pp->religion = w->country[b->country].religion; w->owner_epoch++; }
			logm(m, "%s: the mission in %s %s", w->country[b->country].tag, pp->name ? pp->name : "?", b->inf && pp->owner == b->country ? "converted the province" : "failed");
			world_note(w, b->inf && pp->owner == b->country ? NOTE_MISSION_OK : NOTE_MISSION_FAIL, b->country, b->prov, -1);
		} else if (b->ship == 4 && p->owner == b->country && p->controller == b->country) {   /* fortress works done */
			Province *pp = &w->prov[b->prov];
			if (pp->fort < 6) pp->fort++;
			logm(m, "%s: the fortress of %s is now level %d", w->country[b->country].tag, pp->name ? pp->name : "?", pp->fort);
		} else if (b->ship && b->ship < 4 && p->owner == b->country && p->controller == b->country) {   /* ships: join our fleet in the port, else a new one */
			int f = -1;
			for (k = 0; k < w->nnaval; k++) if (w->naval[k].owner == b->country && w->naval[k].location == b->prov) { f = k; break; }
			if (f < 0) {                                  /* an empty fleet's slot first */
				for (k = 0; k < w->nnaval && w->naval[k].warships + w->naval[k].galleys + w->naval[k].transports > 0; k++) ;
				if (k == w->nnaval && w->nnaval < MAX_NAVAL) w->nnaval++;
				if (k < w->nnaval) {
					NavalUnit *n = &w->naval[k];
					memset(n, 0, sizeof *n);
					n->id = 950000L + k; n->name = "New Fleet"; n->owner = b->country; n->location = b->prov;
					m->fplan[k] = 0;
					f = k;
				}
			}
			if (f >= 0) {
				NavalUnit *n = &w->naval[f];
				if (b->ship == 1) n->warships = (short)(n->warships + b->inf);
				else if (b->ship == 2) n->galleys = (short)(n->galleys + b->inf);
				else n->transports = (short)(n->transports + b->inf);
				logm(m, "%s: new ships ready in %s", w->country[b->country].tag, p->name ? p->name : "?");
				world_sfx(w, b->country, "navy_complete");   /* UNIT_FINISH, naval (0x48c499) */
			}
		} else if (!b->ship && p->owner == b->country && p->controller == b->country) {      /* lost meanwhile: the recruits are gone */
			int u = -1;
			for (k = 0; k < w->nland; k++) if (!w->land[k].dead && w->land[k].owner == b->country && w->land[k].location == b->prov) { u = k; break; }
			if (u < 0) {                                  /* a destroyed unit's slot first, so the table does not grow forever */
				LandUnit *n;
				for (k = 0; k < w->nland && w->land[k].dead != 1; k++) ;   /* never an army at sea */
				if (k == w->nland && w->nland < MAX_LAND) w->nland++;
				if (k < w->nland) {
					n = &w->land[k];
					memset(n, 0, sizeof *n);
					n->id = 900000L + k; n->name = "New Army"; n->owner = b->country; n->location = b->prov;
					n->morale_m = max_morale(m, b->country); m->aplan[k] = 0; m->astuck[k] = 0;
					u = k;
				}
			}
			if (u >= 0) {
				w->land[u].infantry += b->inf; w->land[u].cavalry += b->cav; w->land[u].artillery += b->art;
				logm(m, "%s: new troops ready in %s", w->country[b->country].tag, p->name ? p->name : "?");
				world_sfx(w, b->country, "army_complete");   /* UNIT_FINISH, land (0x48c4a2) */
			}
		}
		*b = w->build[--w->nbuild];                      /* unordered removal */
	}
}

long mil_max_morale(const Military *m, int c) { return max_morale(m, c); }
void mil_strength_inputs(const Military *m, int c, long *land, long *nav, long *mor)
{
	const LandLev *L = lev_of(m, c);
	const NavLev *N = nlev_of(m, c);
	land[0] = L ? L->vif : 0; land[1] = L ? L->vcf : 0; land[2] = L ? L->vaf : 0;
	land[3] = L ? L->vis : 0; land[4] = L ? L->vcs : 0; land[5] = L ? L->vas : 0;
	nav[0] = N ? N->wf : 0; nav[1] = N ? N->ws : 0; nav[2] = N ? N->gf : 0; nav[3] = N ? N->gs : 0;
	mor[0] = max_morale(m, c); mor[1] = nmax_morale(m, c);
}

/* ------------------------------------------------------------ fleets (§11, §12) */
/* The naval move graph: sea zones (terrain 5) linked by ADJACENT.TBL, and each port (a land province with
 * port_sea) linked to its sea zone. Naval speed is NIEZNANE in the spec (§11.1); ours: 10 days per
 * sea zone, 5 days into or out of a port (NIEPEWNE). */
static int sea_prov(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].id >= 0 && w->prov[p].terrain == 5; }   /* 5 = sea (6 = river) */
static int port_prov(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].id >= 0 && w->prov[p].terrain < 5 && sea_prov(w, w->prov[p].port_sea); }

/* calls fn for every naval neighbour of p with the step's days */
static int naval_next(const Military *m, int p, short *nb, long *days, int max)
{
	const World *w = m->w;
	int n = 0, q;
	unsigned long k;
	if (port_prov(w, p)) { nb[0] = w->prov[p].port_sea; days[0] = PORT_DAYS; return 1; }
	if (!sea_prov(w, p)) return 0;
	if (p < 1616)
		for (k = m->astart[p]; k < m->astart[p + 1] && n < max; k++) {
			q = (int)m->arec[2 * k];
			if (sea_prov(w, q)) { nb[n] = (short)q; days[n++] = SEA_DAYS; }
		}
	for (q = 1; q < w->nprov && n < max; q++)                       /* ports opening onto this zone */
		if (w->prov[q].port_sea == p && port_prov(w, q)) { nb[n] = (short)q; days[n++] = PORT_DAYS; }
	return n;
}

long mil_order_fleet(Military *m, int fi, int dest, long today)
{
	World *w = m->w;
	NavalUnit *f = &w->naval[fi];
	static long dist[MAX_PROV];
	static short prev[MAX_PROV];
	static unsigned char done[MAX_PROV];
	short nb[64], rev[64];
	long dd[64];
	int i, c, src = f->location, steps = 0;
	if (dest <= 0 || dest >= w->nprov || src == dest || (!sea_prov(w, dest) && !port_prov(w, dest))) return -1;
	for (i = 0; i < w->nprov; i++) { dist[i] = 0x7FFFFFFFL; prev[i] = -1; done[i] = 0; }
	dist[src] = 0;
	for (;;) {
		long best = 0x7FFFFFFFL;
		int n, k;
		c = -1;
		for (i = 1; i < w->nprov; i++) if (!done[i] && dist[i] < best) { best = dist[i]; c = i; }
		if (c < 0 || c == dest) break;
		done[c] = 1;
		if (c != src && port_prov(w, c)) continue;                  /* no sailing through somebody's harbour */
		n = naval_next(m, c, nb, dd, 64);
		for (k = 0; k < n; k++)
			if (!done[nb[k]] && dist[c] + dd[k] < dist[nb[k]] && may_enter(w, f->owner, nb[k], 1, f->l_id ? f->l_type : 0)) { dist[nb[k]] = dist[c] + dd[k]; prev[nb[k]] = (short)c; }
	}
	if (dist[dest] == 0x7FFFFFFFL) return -1;
	for (c = dest; c != src && steps < 64; c = prev[c]) rev[steps++] = (short)c;
	if (steps > 40) steps = 40;
	for (i = 0; i < steps; i++) f->path[i] = rev[steps - 1 - i];
	f->npath = (short)steps; f->pathpos = 0;
	f->arrive_day = today + (port_prov(w, src) || port_prov(w, f->path[0]) ? PORT_DAYS : SEA_DAYS);
	return dist[dest];
}

/* 4.2: a fleet reveals the zone it enters and, in a sea zone, the unknown land next to it: all of it from
 * naval tech 18; below that only with an explorer / conquistador aboard, each on a roll
 * ((date + unit + zone + province) mod 100 < maneuver) or for sure after 8 misses in a row (one
 * counter). Ours: the roll and the counter for human fleets only (the AI's knowledge is not saved),
 * an AI explorer simply sees the coast. */
static void fleet_discover(Military *m, int fi)
{
	World *w = m->w;
	const NavalUnit *f = &w->naval[fi];
	int c = f->owner, z = f->location, expl = f->l_id && (f->l_type == 2 || f->l_type == 3);
	unsigned long k;
	if (c < 0 || z <= 0 || z >= w->nprov) return;
	world_vp_discover(w, c, z, 1); WORLD_DISCOVER(w, c, z);
	if (!sea_prov(w, z) || z >= 1616 || !m->astart) return;
	if (w->country[c].naval_tech < 18 && !expl) return;
	for (k = m->astart[z]; k < m->astart[z + 1]; k++) {
		int p = (int)m->arec[2 * k];
		if (!land_prov(w, p)) continue;
		if (w->country[c].naval_tech >= 18) { WORLD_DISCOVER(w, c, p); continue; }
		if (WORLD_KNOWN(w, c, p)) continue;
		if ((long)((m->today & 0xFF) + fi + z + p) % 100 < f->l_move || m->explmiss > (w->country[c].human ? 8 : 3)) { WORLD_DISCOVER(w, c, p); m->explmiss = 0; }   /* one counter for all fleets; > 3 for the AI, > 8 for the player */
		else m->explmiss++;
	}
}

static void fleets_day(Military *m)
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *f = &w->naval[i];
		int next;
		if (f->pathpos >= f->npath || m->today < f->arrive_day) continue;
		if (f->location > 0 && f->location < MAX_PROV && m->inbat[f->location]) { f->arrive_day = m->today + 1; continue; }   /* fighting */
		next = f->path[f->pathpos++];
		f->location = (short)next;
		if (port_prov(w, next)) f->sea_since = w->now.year * 12L + w->now.month;
		fleet_discover(m, i);
		if (f->pathpos < f->npath)
			f->arrive_day = m->today + (port_prov(w, next) || port_prov(w, f->path[f->pathpos]) ? PORT_DAYS : SEA_DAYS);
		else { f->npath = 0; world_note(w, NOTE_FLEET_ARRIVE, f->owner, next, i); logm(m, "%s fleet %s arrives in %s", w->country[f->owner].tag, f->name ? f->name : "?", w->prov[next].name ? w->prov[next].name : "?"); }
	}
}

/* ------------------------------------------------------------ pirates (docs/SPEC_PIRATES.md) */
static int pir_busy(const Military *m, int z)            /* any unit of anybody in z (Z+0xae) */
{
	const World *w = m->w;
	int i;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].location == z && fleet_ships(&w->naval[i]) > 0) return 1;
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].location == z) return 1;
	return 0;
}
/* 2.4 the zone score (0x55c570): rich unfortified non-European colonial coasts with empty harbours; one draw if > 0 */
static long pir_score(Military *m, int z)
{
	static const short hot[] = { 965, 966, 967, 969, 1009, 1043, 1044, 1045, 1046, 1047, 1048, 1185, 1188, 1325, 1378 };
	World *w = m->w;
	long s = 0;
	unsigned long k;
	int i;
	if (z <= 0 || z >= 1616 || !m->astart || pir_busy(m, z)) return 0;      /* HINT_42: not where any fleet is */
	for (k = m->astart[z]; k < m->astart[z + 1]; k++) {
		int n = (int)m->arec[2 * k], f, j, inport = 0;
		const Province *pn;
		long inc;
		if (n <= 0 || n >= w->nprov || w->prov[n].owner < 0) continue;
		pn = &w->prov[n];
		inc = world_income_value_m(w, n) / 1000;
		f = pn->fort;
		s += f == 0 ? inc / 2 : inc / (f + 4);
		if (m->tradefn) s += m->tradefn(m->tradeu, n) / 1000;
		if (pn->tp) s *= 3;                                          /* on the running total (quirk of the exe) */
		if (m->europe >= 0 && pn->continent == m->europe) s /= 2;
		if (port_prov(w, n)) for (j = 0; j < w->nnaval && !inport; j++) if (w->naval[j].location == n && fleet_ships(&w->naval[j]) > 0) inport = 1;
		if (inport) s /= 5;
		if (s < 0) s = 0;
	}
	for (i = 0; i < (int)(sizeof hot / sizeof hot[0]); i++) if (hot[i] == z) { s *= 2; break; }
	return s > 0 ? mrand(m) % s : s;
}
/* 2.2 the cap (0x55c4b0): shipyards + warships / 100 of everybody, by the year */
static int pir_max(const Military *m)
{
	const World *w = m->w;
	long W = 0, S = 0, b;
	int i, y = w->now.year;
	for (i = 0; i < w->nnaval; i++) if (fleet_ships(&w->naval[i]) > 0) W += w->naval[i].warships;
	for (i = 1; i < w->nprov; i++) if (w->prov[i].owner >= 0 && (w->prov[i].bflags & PB_SHIPYARD)) S++;
	b = S + W / 100;
	if (y < 1550) return (int)(b / 2);
	if (y < 1600) return (int)(b + 1);
	if (y < 1700) return (int)(2 * b + 2);
	if (y < 1770) return (int)(b / 2 > 2 ? b / 2 : 2);
	return (int)(b / 4 > 1 ? b / 4 : 1);
}
/* a new pirate fleet (CREATE_FLEET for PIR): full morale, no leader, the PIRATES news to whoever knows the zone */
static int pir_fleet(Military *m, int z, int ships)
{
	World *w = m->w;
	int fi = mil_new_fleet(m, w->pir, z, ships, "Pirates"), c;
	if (fi < 0) return -1;
	w->naval[fi].morale_m = nmax_morale(m, w->pir);
	for (c = 0; c < w->ncountry; c++) if (w->country[c].human && WORLD_KNOWN(w, c, z)) world_note(w, NOTE_PIRATES, c, z, -1);
	return fi;
}
/* 5 commissioning privateers: cost 25 x (1 + inflation) (0x45b2a0); "a naval power": player+0x1f84 = the ports it owns on its
 * capital's land mass (+0x498) or continent (the recount 0x43ad8c..0x43add9) */
int mil_privateer_state(const Military *m, int c, int z, long *cost_m)
{
	const World *w = m->w;
	const Country *co;
	long infl, cost;
	int i, port = 0;
	if (c < 0 || c >= w->ncountry || w->pir < 0 || !sea_prov(w, z)) return 3;
	co = &w->country[c];
	infl = co->inflation_u < 0 ? 0 : co->inflation_u;
	cost = 25000L + infl / 40;                                   /* 25 x 1000 x (1 + infl / 1e6) */
	if (cost_m) *cost_m = cost;
	{	int cap = co->capital;
		for (i = 1; i < w->nprov && !port; i++)
			if (w->prov[i].owner == c && port_prov(w, i) && cap > 0 && cap < w->nprov
			    && ((m->lgrp[i] >= 0 && m->lgrp[i] == m->lgrp[cap]) || w->prov[i].continent == w->prov[cap].continent)) port = 1;
	}
	if (!port) return 3;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == w->pir && w->naval[i].location == z && fleet_ships(&w->naval[i]) > 0) return 2;
	if (cost > co->treasury_m) return 1;
	return 0;
}

int mil_privateers(Military *m, int c, int z)
{
	World *w = m->w;
	long cost;
	unsigned long k;
	short done[16];
	int nd = 0, fi, j;
	if (mil_privateer_state(m, c, z, &cost) != 0) return -1;
	fi = pir_fleet(m, z, 1 + mrand(m) % 3 + mrand(m) % 3);      /* 1..5 warships (also the PIRATES news) */
	if (fi < 0) return -1;
	w->country[c].treasury_m -= cost; world_book_exp(w, c, EXP_EVENT, cost);   /* LOSEMONEY 0x60, +0x10 = 15 */
	if (mrand(m) % 100 < 10) w->country[c].badboy_m += 1000;     /* message 0x148: badboy +1 */
	if (z < 1616 && m->astart)
		for (k = m->astart[z]; k < m->astart[z + 1]; k++) {      /* every coast owner once: relation and a CB for 3 months */
			int n = (int)m->arec[2 * k], o, cap;
			if (n <= 0 || n >= w->nprov || (o = w->prov[n].owner) < 0 || o == c) continue;
			for (j = 0; j < nd && done[j] != o; j++) ;
			if (j < nd) continue;
			if (nd < 16) done[nd++] = (short)o;
			cap = w->country[o].capital;
			world_rel_add(w, c, o, cap > 0 && cap < w->nprov && w->prov[cap].continent == w->prov[n].continent ? -50000L : -25000L);
			world_give_cb(w, o, c, m->today + 90);                   /* GIVECB 0xad: today + 3 months */
		}
	logm(m, "%s commissions privateers in %s", w->country[c].tag, w->prov[z].name ? w->prov[z].name : "?");
	return fi;
}

/* 2.1 the spawner (0x55c300) */
static void pir_spawn(Military *m)
{
	World *w = m->w;
	int y = w->now.year, ch, n = 0, i, best = 969;
	long bs = 0;
	m->pir_next = m->today + 25 + mrand(m) % 25 + mrand(m) % 25;
	ch = y < 1680 ? 100 : y < 1730 ? (1730 - y) * 2 : 1;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == w->pir && fleet_ships(&w->naval[i]) > 0) n++;
	if (n >= pir_max(m)) return;
	if (mrand(m) % 100 >= ch) return;
	for (i = 1; i < w->nprov && i < 0x7e4; i++) {
		long s;
		if (!sea_prov(w, i) || i == m->pir_last) continue;
		s = pir_score(m, i);
		if (s > bs) { bs = s; best = i; }
	}
	if (!sea_prov(w, best)) return;
	if (pir_fleet(m, best, 2 + mrand(m) % 3) >= 0) m->pir_last = (short)best;
}
/* 3 the mover (0x55c160): one random neighbour; there if it is empty open sea and its draw beats ours */
static void pir_move(Military *m)
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *f = &w->naval[i];
		int p = f->location, t, nn;
		long a, b;
		if (f->owner != w->pir || fleet_ships(f) <= 0 || f->pathpos < f->npath) continue;
		if (p <= 0 || p >= 1616 || m->inbat[p] || !m->astart) continue;
		nn = (int)(m->astart[p + 1] - m->astart[p]);
		if (nn <= 0) continue;                                       /* (the exe would divide by zero) */
		t = (int)m->arec[2 * (m->astart[p] + (unsigned long)(mrand(m) % nn))];
		a = pir_score(m, t) + 1; b = pir_score(m, p) + 1;
		if (!sea_prov(w, t) || a <= b) continue;
		f->path[0] = (short)t; f->npath = 1; f->pathpos = 0; f->arrive_day = m->today + SEA_DAYS;
	}
}
static void pirates_day(Military *m)                     /* 1.1 the PIR tick: spawner + mover every 25..73 days */
{
	if (m->w->pir < 0 || m->today <= m->pir_next) return;
	pir_spawn(m);
	pir_move(m);
}
/* 4.1 P of every port: the idle pirate warships in its sea zone; 4.4 the blockade (0x47e7c0): idle fleets in the port's zone
 * hostile to its controller (pirates always), warships + galleys / 2 >= 5 (counted doubled: 2W + G >= 10) */
static void pirates_refresh(Military *m)
{
	static short hs[MAX_PROV];
	World *w = m->w;
	int i, p;
	for (p = 1; p < w->nprov; p++) {
		Province *pv = &w->prov[p];
		pv->pirates = 0; hs[p] = 0; pv->besieged = (unsigned char)(m->siege[p].besieger != 0); pv->hostile = 0; pv->cot_nb = 0;
		if (pv->looted && m->today >= pv->loot_until) { pv->looted = 0; pv->loot_until = 0; }   /* command 0x5e, a year on */
	}
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		int o;
		if (u->dead || u->location <= 0 || u->location >= w->nprov) continue;
		o = w->prov[u->location].owner;
		if (o >= 0 && o != u->owner && atw(m, u->owner, o)) w->prov[u->location].hostile = 1;
	}
	if (m->astart)
		for (i = 0; i < w->ncot; i++) {
			int cp = w->cot[i].prov;
			unsigned long k2;
			if (cp <= 0 || cp >= 1616) continue;
			for (k2 = m->astart[cp]; k2 < m->astart[cp + 1]; k2++) { int nb = (int)m->arec[2 * k2]; if (nb > 0 && nb < w->nprov && w->prov[nb].cot_nb < 255) w->prov[nb].cot_nb++; }
		}
	for (i = 0; i < w->nnaval; i++) {
		const NavalUnit *f = &w->naval[i];
		int z = f->location;
		if (f->owner < 0 || fleet_ships(f) <= 0 || f->pathpos < f->npath || z <= 0 || z >= MAX_PROV || m->inbat[z]) continue;
		for (p = m->zp_head[z]; p > 0; p = m->zp_next[p]) {
			int k = w->prov[p].controller;
			if (k < 0) continue;
			if (f->owner == w->pir) w->prov[p].pirates += f->warships;
			if (f->owner != k && atw(m, f->owner, k)) hs[p] += 2 * f->warships + f->galleys;
		}
	}
	for (p = 1; p < w->nprov; p++) w->prov[p].blockaded = (unsigned char)(hs[p] >= 10);
}
/* 1.5 the pirates' naval tech: 2 below the best Christian / Muslim navy (at least 21 counted), never above the best - 2 */
static void pirates_tech(Military *m)
{
	World *w = m->w;
	int c, all = 0, elig = 21, v;
	if (w->pir < 0) return;
	for (c = 0; c < w->ncountry; c++) {
		int r;
		if (c == w->pir || c == w->reb || c == w->nat || w->country[c].nprov_owned <= 0) continue;
		if (w->country[c].naval_tech > all) all = w->country[c].naval_tech;
		r = world_religion_index(w, w->country[c].religion);
		if (r >= 0 && r <= 6 && w->country[c].naval_tech > elig) elig = w->country[c].naval_tech;
	}
	v = (elig < all ? elig : all) - 2;
	w->country[w->pir].naval_tech = (short)(v < 0 ? 0 : v);
	w->country[w->pir].land_tech = w->country[w->pir].infra_tech = w->country[w->pir].trade_tech = 0;
}

/* ------------------------------------------------------------ naval attrition (0x4896f0 = ships x rate 0x489470) */
static int sup_friend(const Military *m, int k, int c);
static int nat_friend(const Military *m, int k, int c) { return sup_friend(m, k, c); }   /* 0x43a530 */
/* 0x483570, sea zone: storm +10 (naval tech < 38), ice in winter +10 (tech < 41), a zone with ports -3,
 * -2 more next to a friendly port */
static long sea_supply(const Military *m, int z, int c)
{
	const World *w = m->w;
	long s = 0;
	unsigned long k;
	if (w->prov[z].storm_now && w->country[c].naval_tech < 38) s = 10;  /* the month's storm (+0x248) */
	if (w->prov[z].ice && w->prov[z].winter && w->country[c].naval_tech < 41) s += 10;   /* ice in winter (+0x9e, +0x3e2) */
	if (m->zp_head[z] > 0 && z < 1616) {
		s -= 3;
		for (k = m->astart[z]; k < m->astart[z + 1]; k++) {
			int n = (int)m->arec[2 * k];
			if (port_prov(w, n) && w->prov[n].controller >= 0 && nat_friend(m, w->prov[n].controller, c)) { s -= 2; break; }
		}
	}
	return s;
}
/* the monthly rate in percent of the fleet's ships (0x489470) */
static long naval_rate(const Military *m, const NavalUnit *f)
{
	const World *w = m->w;
	int z = f->location, c = f->owner, ships = fleet_ships(f), lo = 1;
	unsigned long k;
	long a, cap;
	if (z <= 0 || z >= w->nprov || z >= 1616 || !m->astart || port_prov(w, z) || ships <= 0) return 0;
	for (k = m->astart[z]; k < m->astart[z + 1]; k++) if (w->prov[(int)m->arec[2 * k]].owner == c) return 0;   /* friendly waters */
	a = sea_supply(m, z, c);
	if (w->country[c].naval_tech < 41) {                              /* months at sea x 2 x NAVAL.csv Attrition, truncated */
		long mo = sea_prov(w, z) ? w->now.year * 12L + w->now.month - f->sea_since : 0;
		a = (nlev_of(m, c)->att * 2 * mo + a * 1000) / 1000;
	}
	if (f->l_id) { if (f->l_type == 3) { a -= 2 * f->l_move; lo = 0; } else a -= f->l_move; }   /* an explorer counts twice and lifts the floor */
	if (m->zp_head[z] > 0)
		for (k = m->astart[z]; k < m->astart[z + 1]; k++) {             /* blockading a hostile port: -3 */
			const Province *pn = &w->prov[(int)m->arec[2 * k]];
			if (pn->blockaded && pn->controller >= 0 && atw(m, pn->controller, c)) { a -= 3; break; }
		}
	cap = 100 / ships;
	if (a > cap) a = cap;
	if (a > 0 && a < cap / 10) a = cap / 10;
	if (a < lo) a = lo;
	return a;
}
void mil_usage(const Military *m, int *battles, int *plans, int *generals)
{
	int k;
	*battles = *plans = *generals = 0;
	for (k = 0; k < MAXB; k++) if (m->b[k].active) (*battles)++;
	for (k = 0; k < AIP_MAX; k++) if (m->plan[k].type != PL_FREE) (*plans)++;
	for (k = 0; k < AIG_MAX; k++) if (m->gen[k].c >= 0) (*generals)++;
}
long mil_naval_rate(const Military *m, int fi) { return m && fi >= 0 && fi < m->w->nnaval ? naval_rate(m, &m->w->naval[fi]) : 0; }
static void naval_attrition(Military *m)                  /* monthly (the unit upkeep 0x4de5a0; not the pirates) */
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nnaval; i++) {
		NavalUnit *f = &w->naval[i];
		long r;
		if (fleet_ships(f) <= 0 || f->owner < 0 || f->owner == w->pir) continue;
		if (port_prov(w, f->location)) { f->sea_since = w->now.year * 12L + w->now.month; continue; }
		if (f->location > 0 && f->location < MAX_PROV && m->inbat[f->location]) continue;
		r = naval_rate(m, f);
		f->att_m += (long)fleet_ships(f) * r * 10;                      /* ships x rate % */
		while (f->att_m >= 1000 && fleet_ships(f) > 0) {               /* a whole ship: from the largest kind (NIEPEWNE) */
			if (f->transports >= f->warships && f->transports >= f->galleys) f->transports--;
			else if (f->warships >= f->galleys) f->warships--;
			else f->galleys--;
			f->att_m -= 1000;
		}
		if (fleet_ships(f) <= 0) { f->att_m = 0; logm(m, "%s fleet %s is lost to attrition", w->country[f->owner].tag, f->name ? f->name : "?"); }
	}
}

int mil_fleet_at(const World *w, int prov, int owner)
{
	int i;
	for (i = 0; i < w->nnaval; i++)
		if (w->naval[i].location == prov && (owner < 0 || w->naval[i].owner == owner) && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports > 0) return i;
	return -1;
}

int mil_is_port(const World *w, int prov) { return port_prov(w, prov); }

/* ships: unit cost of SPEC_TIME_ECONOMY §6.2 (warship 50, galley 10, transport 15, x (1 + inflation));
 * build times are ours (NIEPEWNE: the manual only orders them warship > transport > galley) */
long mil_ship_cost(const Military *m, int c, int kind)
{
	if (kind < 0 || kind > 2) return 0;
	return unit_price(m->w, c, 3 + kind);
}

/* 0x438298..0x4382ef: warships 9 months + the naval level's Low Wind (0x429860 / 0x433630, record +0x34), galleys 5, transports 6;
 * an order takes its slowest kind */
long mil_ship_days(const Military *m, int c, int kind)
{
	if (kind == 0) return 30L * (9 + (c >= 0 ? nlev_of(m, c)->lowwind : 0));
	return kind == 1 ? 150 : kind == 2 ? 180 : 0;
}

int mil_build_ship(Military *m, int c, int prov, int kind, long today)
{
	World *w = m->w;
	Country *co = &w->country[c];
	const Province *p;
	Build *b;
	long cost = mil_ship_cost(m, c, kind);
	static const char *nm[3] = { "a warship", "a galley", "a transport" };
	if (prov <= 0 || prov >= w->nprov || kind < 0 || kind > 2 || !port_prov(w, prov)) return MIL_RC_BADPROV;
	p = &w->prov[prov];
	if (p->owner != c || p->controller != c || p->population <= 0) return MIL_RC_BADPROV;
	if (co->treasury_m < cost) return MIL_RC_MONEY;                  /* ships need no manpower (manual p.22) */
	if (w->nbuild >= MAX_BUILD) return MIL_RC_FULL;
	co->treasury_m -= cost; world_book_exp(m->w, (int)(co - m->w->country), EXP_FLEET, cost);
	b = &w->build[w->nbuild++];
	memset(b, 0, sizeof *b);
	b->country = (short)c; b->prov = (short)prov; b->ship = (short)(kind + 1); b->inf = 1; b->done = today + mil_ship_days(m, c, kind);
	logm(m, "%s builds %s in %s (%ld ducats), ready in %ld days", co->tag, nm[kind], p->name ? p->name : "?", cost / 1000, mil_ship_days(m, c, kind));
	return MIL_RC_OK;
}

long mil_fort_cost(const Military *m, int c)
{
	long cost = 100L * (1000000L + m->w->country[c].inflation_u) / 1000000L;
	return (cost < 1 ? 1 : cost) * 1000;
}

int mil_fort_max(const World *w, int c)
{
	int t = w->country[c].land_tech;
	return t >= 41 ? 6 : t >= 26 ? 5 : t >= 21 ? 4 : t >= 11 ? 3 : t >= 2 ? 2 : 1;
}

int mil_build_fort(Military *m, int c, int prov, long today)
{
	World *w = m->w;
	Country *co = &w->country[c];
	const Province *p;
	Build *b;
	long cost = mil_fort_cost(m, c);
	int i;
	if (prov <= 0 || prov >= w->nprov || !land_prov(w, prov)) return MIL_RC_BADPROV;
	p = &w->prov[prov];
	if (p->owner != c || p->controller != c || p->population <= 0) return MIL_RC_BADPROV;
	for (i = 0; i < w->nbuild; i++) if (w->build[i].prov == prov && w->build[i].ship == 4) return MIL_RC_FULL;   /* already building */
	if (p->fort >= mil_fort_max(w, c)) return MIL_RC_TECH;
	if (co->treasury_m < cost) return MIL_RC_MONEY;
	if (w->nbuild >= MAX_BUILD) return MIL_RC_FULL;
	co->treasury_m -= cost; world_book_exp(m->w, (int)(co - m->w->country), EXP_FORT, cost);
	b = &w->build[w->nbuild++];
	memset(b, 0, sizeof *b);
	b->country = (short)c; b->prov = (short)prov; b->ship = 4; b->done = today + FORT_DAYS;
	logm(m, "%s expands the fortress of %s to level %d (%ld ducats), done in %d days", co->tag, p->name ? p->name : "?", p->fort + 1, cost / 1000, FORT_DAYS);
	return MIL_RC_OK;
}

/* ------------------------------------------------------------ sea transport (manual p.17)
 * Capacity: warship 1, galley 0.5, transport 2 (manual). Army weight: infantry 1, cavalry 2 per 1000
 * men, artillery 2 per 10 pieces - ours (NIEPEWNE: the weight formula 0x48fad0 was not read; the
 * manual only says cavalry and artillery are heavier). Loading is from a province next to the fleet's
 * sea zone, unloading into a province next to it; a fleet that enters a port unloads at once
 * (manual). Both are immediate here (NIEPEWNE: the original walks them in with the (dis)embark time). */
long mil_army_weight(const LandUnit *u) { return u->infantry + 2 * u->cavalry + 200 * u->artillery; }
long mil_fleet_capacity(const NavalUnit *f) { return 1000L * f->warships + 500L * f->galleys + 2000L * f->transports; }
long mil_fleet_load(const World *w, int fleet)
{
	long s = 0;
	int i;
	for (i = 0; i < w->nland; i++) if (w->land[i].dead == LAND_ABOARD && w->land[i].aboard == fleet) s += mil_army_weight(&w->land[i]);
	return s;
}

static int adjacent(const Military *m, int a, int b)
{
	unsigned long k;
	if (a <= 0 || b <= 0 || a >= m->w->nprov || !m->astart) return 0;
	for (k = m->astart[a]; k < m->astart[a + 1]; k++) if ((int)m->arec[2 * k] == b) return 1;
	return 0;
}

int mil_embark(Military *m, int army, int fleet)
{
	World *w = m->w;
	LandUnit *u;
	const NavalUnit *f;
	if (army < 0 || army >= w->nland || fleet < 0 || fleet >= w->nnaval) return MIL_RC_BADPROV;
	u = &w->land[army]; f = &w->naval[fleet];
	if (u->dead || f->owner != u->owner || !sea_prov(w, f->location) || f->npath > f->pathpos) return MIL_RC_BADPROV;
	if (!adjacent(m, u->location, f->location) && w->prov[u->location].port_sea != f->location) return MIL_RC_BADPROV;
	if (mil_fleet_load(w, fleet) + mil_army_weight(u) > mil_fleet_capacity(f)) return MIL_RC_FULL;
	u->dead = LAND_ABOARD; u->aboard = (short)fleet; u->from = u->location; u->location = f->location;
	u->npath = u->pathpos = 0;
	logm(m, "%s: %s boards %s", w->country[u->owner].tag, u->name ? u->name : "army", f->name ? f->name : "fleet");
	return MIL_RC_OK;
}

int mil_disembark(Military *m, int fleet, int prov)
{
	World *w = m->w;
	int i, n = 0;
	if (fleet < 0 || fleet >= w->nnaval || prov <= 0 || prov >= w->nprov || !land_prov(w, prov)) return 0;
	if (!adjacent(m, w->naval[fleet].location, prov) && w->prov[prov].port_sea != w->naval[fleet].location) return 0;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		if (u->dead != LAND_ABOARD || u->aboard != fleet) continue;
		u->dead = 0; u->from = u->location; u->location = (short)prov; u->npath = u->pathpos = 0; u->aboard = 0;
		if (u->owner >= 0) { world_vp_discover(w, u->owner, prov, 0); WORLD_DISCOVER(w, u->owner, prov); }
		n++;
	}
	if (n) logm(m, "%s: %d army unit(s) land in %s", w->country[w->naval[fleet].owner].tag, n, w->prov[prov].name ? w->prov[prov].name : "?");
	return n;
}

/* carried armies follow their fleet; a fleet in port lets them off, a lost fleet takes them down */
static void cargo_day(Military *m)
{
	World *w = m->w;
	int i;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		const NavalUnit *f;
		if (u->dead != LAND_ABOARD) continue;
		f = u->aboard >= 0 && u->aboard < w->nnaval ? &w->naval[u->aboard] : NULL;
		if (!f || f->owner != u->owner || f->warships + f->galleys + f->transports <= 0) {
			u->dead = 1; u->infantry = u->cavalry = u->artillery = 0;
			logm(m, "%s army lost at sea", w->country[u->owner].tag);
			continue;
		}
		u->location = f->location;
		if (land_prov(w, f->location) && !f->npath) { u->dead = 0; u->aboard = 0;   /* in port at the end of the voyage (not while an AI fleet is leaving) */ logm(m, "%s army lands in port %s", w->country[u->owner].tag, w->prov[f->location].name ? w->prov[f->location].name : "?"); }
	}
}

/* nearest port of the fleet's owner (controlled by it) by sailing days; -1 none */
static int nearest_own_port(Military *m, int fi)
{
	World *w = m->w;
	const NavalUnit *f = &w->naval[fi];
	static long dist[MAX_PROV];
	static unsigned char done[MAX_PROV];
	short nb[64];
	long dd[64];
	int i, c;
	for (i = 0; i < w->nprov; i++) { dist[i] = 0x7FFFFFFFL; done[i] = 0; }
	dist[f->location] = 0;
	for (;;) {
		long best = 0x7FFFFFFFL;
		int n, k;
		c = -1;
		for (i = 1; i < w->nprov; i++) if (!done[i] && dist[i] < best) { best = dist[i]; c = i; }
		if (c < 0) return -1;
		if (c != f->location && port_prov(w, c) && w->prov[c].owner == f->owner && w->prov[c].controller == f->owner) return c;
		done[c] = 1;
		if (c != f->location && port_prov(w, c)) continue;
		n = naval_next(m, c, nb, dd, 64);
		for (k = 0; k < n; k++) if (!done[nb[k]] && dist[c] + dd[k] < dist[nb[k]]) dist[nb[k]] = dist[c] + dd[k];
	}
}

/* ---------------------------------------------------------------- natives (docs/SPEC_NATIVES.md) */
int mil_native_style(const World *w, int p)                 /* 2.1, by the region's name */
{
	const char *r = world_name(w, w->prov[p].region);
	if (script_ieq(r, "North America")) return 2;
	if (script_ieq(r, "Central America") || script_ieq(r, "Carribean")) return 1;
	if (script_ieq(r, "South America")) return 0;
	if (script_ieq(r, "Siberia")) return 9;
	if (script_ieq(r, "India")) return 5;
	if (script_ieq(r, "China")) return 6;
	if (script_ieq(r, "Indochina") || script_ieq(r, "Indonesia") || script_ieq(r, "Australia") || script_ieq(r, "Pacific")) return 8;
	if (script_ieq(r, "Japan")) return 7;
	if (script_ieq(r, "East Africa") || script_ieq(r, "West Africa")) return script_ieq(world_name(w, w->prov[p].religion), "sunni") ? 4 : 3;
	return 4;
}

/* 4.1 0x4d8f10: S = natives x 100 men, split by the style; natives := 0; the army is pacified (disbanded)
 * once it stands idle 5 days after it came out (arrive_day holds that day; 4.2) */
int mil_native_attack(Military *m, int p)
{
	World *w = m->w;
	Province *pv = &w->prov[p];
	long S = pv->natives * 100L, inf, cav;
	int k, st;
	LandUnit *u;
	if (S <= 0 || w->nat < 0 || pv->terrain >= 5) return 0;
	st = mil_native_style(w, p);
	cav = st == 4 ? S / 2 : (st >= 5 && st <= 8) ? S / 5 : st == 9 ? S / 4 : 0;
	inf = S - cav;
	pv->natives = 0;
	for (k = 0; k < w->nland && w->land[k].dead != 1; k++) ;
	if (k == w->nland) { if (w->nland >= MAX_LAND) return 0; w->nland++; }
	u = &w->land[k];
	memset(u, 0, sizeof *u);
	u->id = 980000L + k; u->name = "Natives"; u->owner = w->nat; u->location = (short)p;
	u->infantry = inf; u->cavalry = cav; u->morale_m = 2000;
	u->arrive_day = m->today + 5;                            /* PACIFY_NATIVE 0xd3 at +5 days */
	logm(m, "natives rise in %s: %ld men", pv->name ? pv->name : "?", S);
	return 1;
}

/* 4.3 (NAT's PROVCHANGECONTROL 0x55bce0): D = Tolerance x the natives' men / 40; a colony / city loses D people
 * (a fort: D / 3, and nothing if that would leave fewer than 25); below 10 it is destroyed (a CoT seat stays at
 * 10); a trading post loses D / 60 levels. News NATIVES / NATIVESKILL to the player who held it. */
static void natives_damage(Military *m, int p)
{
	if (m->w->tutorial) return;                               /* 0x55bd10 */
	World *w = m->w;
	Province *pv = &w->prov[p];
	long men = 0, D, np;
	int i, own = pv->controller, cot = 0;
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].owner == w->nat && w->land[i].location == p) men += unit_men(&w->land[i]);
	D = (long)pv->tolerance * men / 40;
	for (i = 0; i < w->ncot; i++) if (w->cot[i].prov == p) cot = 1;
	if (pv->tp) {
		long lv = pv->tp - D / 60;
		if (lv < 1 && !cot) { pv->tp = 0; if (pv->population <= 0 && pv->owner >= 0) { w->country[pv->owner].nprov_owned--; pv->owner = -1; w->owner_epoch++; } world_note(w, NOTE_NATIVESKILL, own, p, -1); }
		else { pv->tp = (unsigned char)(lv < 0 ? 0 : lv); world_note(w, NOTE_NATIVES, own, p, -1); }
		return;
	}
	if (pv->population <= 0) return;
	np = pv->population - D;
	if (pv->fort > 0 && np < 25) { np = pv->population - D / 3; if (np < 25) return; }
	if (np < 10) {
		if (cot) pv->population = 10;
		else { pv->population = 0; if (pv->owner >= 0) { w->country[pv->owner].nprov_owned--; pv->owner = -1; w->owner_epoch++; } world_note(w, NOTE_NATIVESKILL, own, p, -1); return; }
	} else pv->population = np;
	world_note(w, NOTE_NATIVES, own, p, -1);
}

/* 4.2 PACIFY_NATIVE: a native army that stands idle (no battle there) on or after its day is disbanded; in a
 * battle the day moves to 2 days on (the unit-idle path). The men do not return to the province. */
static void natives_pacify(Military *m)
{
	World *w = m->w;
	int i;
	if (w->nat < 0) return;
	for (i = 0; i < w->nland; i++) {
		LandUnit *u = &w->land[i];
		if (u->dead || u->owner != w->nat) continue;
		if (u->location > 0 && u->location < MAX_PROV && m->inbat[u->location]) { if (u->arrive_day < m->today + 2) u->arrive_day = m->today + 2; continue; }
		if (m->siege[u->location].besieger == w->nat + 1) continue;   /* besieging a settlement (NIEPEWNE: counts as busy) */
		if (m->today >= u->arrive_day) { u->dead = 1; u->infantry = u->cavalry = u->artillery = 0; }
	}
}

/* 5.2 + 5.1: the first army ever in a native province cuts the natives to 21..40 % (+ a conquistador's skills,
 * +5 in the Central / South Americas and the Caribbean), then the perhaps-attack */
static void natives_arrival(Military *m, int p)
{
	World *w = m->w;
	Province *pv = &w->prov[p];
	int i, best = -1, minr = 9999, spa = world_country(w, "SPA"), fra = world_country(w, "FRA");
	long total = 0, b, c;
	if (!pv->conq_done) {
		int base = 40 - mrand(m) % 20, cq = -1, cr = 9999;
		const char *rg = world_name(w, pv->region);
		for (i = 0; i < w->nland; i++) {
			const LandUnit *u = &w->land[i];
			if (u->dead || u->location != p || !u->l_id || u->l_type != 2) continue;
			if (u->l_rank < cr) { cr = u->l_rank; cq = i; }
		}
		if (cq >= 0) { const LandUnit *u = &w->land[cq]; base += u->l_fire + u->l_shock + u->l_move + (u->owner == spa || u->owner == fra ? 2 : 0); }   /* NIEPEWNE: fire + shock */
		if (script_ieq(rg, "Central America") || script_ieq(rg, "Carribean") || script_ieq(rg, "South America")) base += 5;
		pv->conq_done = 1;
		pv->natives = pv->natives * base / 100;
		if (pv->natives <= 0) return;
	}
	for (i = 0; i < w->nland; i++) {                           /* only the units that set a new lowest rank are summed (as found) */
		const LandUnit *u = &w->land[i];
		int rk;
		if (u->dead || u->location != p || u->owner == w->nat) continue;
		rk = u->l_id ? u->l_rank : 99;
		if (rk < minr) { minr = rk; best = i; total += u->infantry + u->cavalry + 100L * u->artillery; }
	}
	b = 10L * pv->tolerance;
	if (best >= 0 && w->land[best].l_id) {
		const LandUnit *u = &w->land[best];
		b -= 10L * u->l_move;
		if (u->owner == spa) b += 20;
		if (u->owner == fra) b -= 20;
		if (u->l_type == 2) b -= 30;
	}
	c = total / 1000 - pv->natives / 10 + b + 25;           /* natives in hundreds, as the exe's +0x12c */
	if (mrand(m) % 100 < c * pv->ferocity / 10) mil_native_attack(m, p);
}

/* the battles c has a unit in (the pattern players start only then, 0x48f130): prov[], info[] = MSB_* bits */
int mil_sound_battles(const Military *m, int c, short *prov, unsigned char *info, int max)
{
	const World *w = m->w;
	int k, i, n = 0;
	for (k = 0; k < MAXB && n < max; k++) {
		const Battle *b = &m->b[k];
		int sea, in = 0;
		if (!b->active || b->prov <= 0 || b->prov >= w->nprov) continue;
		sea = sea_prov(w, b->prov);
		if (sea) { for (i = 0; i < w->nnaval && !in; i++) in = w->naval[i].owner == c && w->naval[i].location == b->prov && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports > 0; }
		else { for (i = 0; i < w->nland && !in; i++) in = !w->land[i].dead && w->land[i].owner == c && w->land[i].location == b->prov; }
		if (b->assault && !in) in = w->prov[b->prov].controller == c;   /* the garrison */
		if (!in) continue;
		prov[n] = b->prov;
		info[n++] = (unsigned char)((sea ? MSB_NAVAL : 0) | (b->phase ? MSB_SHOCK : 0) | (b->assault ? MSB_ASSAULT : 0));
	}
	return n;
}

int mil_battle_view(const Military *m, int prov, MilBattleView *v)
{
	const World *w = m->w;
	int k, s, i;
	memset(v, 0, sizeof *v);
	v->country[0] = v->country[1] = -1;
	if (prov <= 0 || prov >= w->nprov) return MBV_NONE;
	v->terrain = w->prov[prov].terrain;
	for (k = 0; k < MAXB; k++) if (m->b[k].active && m->b[k].prov == prov) break;
	if (k < MAXB && m->b[k].assault) {                          /* an assault: the besieger against the garrison */
		const Battle *b = &m->b[k];
		const Siege *sg = &m->siege[prov];
		long am, ai, aa, af, as, amor, gm, gf, gs;
		int ac, gc;
		as_attacker(m, b, &am, &ai, &aa, &af, &as, &amor, &ac); as_garrison(m, b, &gm, &gf, &gs, &gc);
		if (ac < 0 || gc < 0) return MBV_NONE;
		v->kind = MBV_LAND; v->phase = b->phase;
		v->country[0] = ac; v->country[1] = gc;
		v->morale[0] = amor; v->morale[1] = sg->g_mor;
		v->power[0] = b->phase == 0 ? af : as; v->power[1] = b->phase == 0 ? gf : gs;
		v->roll[0] = b->die[0] - (sg->breaches == 0 ? w->prov[prov].fort : 0); v->roll[1] = b->die[1];
		v->n[0][0] = ai; v->n[0][2] = aa; v->n[1][0] = sg->g_inf; v->n[1][2] = sg->g_art;
		v->nstack[0] = 1; v->nstack[1] = 1;
		v->maxmor = max_morale(m, ac) > max_morale(m, gc) ? max_morale(m, ac) : max_morale(m, gc);
		return MBV_LAND;
	}
	if (k < MAXB && sea_prov(w, prov)) {                       /* a naval battle (window 7, 0x4ff550) */
		const Battle *b = &m->b[k];
		long sh[2], fire[2], shock[2], mor[2];
		int c[2];
		for (s = 0; s < 2; s++) nside_sum(m, b, s, &sh[s], &fire[s], &shock[s], &mor[s], &c[s]);
		if (c[0] < 0 || c[1] < 0) return MBV_NONE;
		v->kind = MBV_NAVAL; v->phase = b->phase;
		for (s = 0; s < 2; s++) {
			long mx = nmax_morale(m, c[s]);
			v->country[s] = c[s]; v->morale[s] = mor[s];
			v->power[s] = b->phase == 0 ? fire[s] : shock[s];
			v->roll[s] = b->die[s];
			if (mx > v->maxmor) v->maxmor = mx;
		}
		for (i = 0; i < w->nnaval; i++) {
			const NavalUnit *u = &w->naval[i];
			if (u->location != prov || fleet_ships(u) <= 0) continue;
			s = side_of(m, b, u->owner);
			if (s < 0 || s > 1 || (s == 1 && !atw(m, u->owner, b->attacker))) continue;
			v->n[s][0] += u->warships; v->n[s][1] += u->galleys; v->n[s][2] += u->transports;
			v->nstack[s]++;
		}
		return MBV_NAVAL;
	}
	if (k < MAXB) {
		const Battle *b = &m->b[k];
		long men[2], fire[2], shock[2], cav[2], mor[2];
		int c[2];
		for (s = 0; s < 2; s++) side_sum(m, b, s, &men[s], &fire[s], &shock[s], &cav[s], &mor[s], &c[s]);
		if (c[0] < 0 || c[1] < 0) return MBV_NONE;               /* over: the window drops back (0x530d60) */
		v->kind = MBV_LAND; v->phase = b->phase;
		for (s = 0; s < 2; s++) {
			long mx = max_morale(m, c[s]);
			v->country[s] = c[s]; v->morale[s] = mor[s];
			v->power[s] = b->phase == 0 ? fire[s] : shock[s];
			v->roll[s] = b->die[s] + roll_mod(m, b, s, cav, v->terrain);
			v->cav_adv[s] = v->terrain != 1 && v->terrain != 2 && v->terrain != 4 && cav[s] > 2 * cav[1 - s];
			v->attr[s] = max_attrition(m, prov, c[s]);
			if (mx > v->maxmor) v->maxmor = mx;
		}
		for (i = 0; i < w->nland; i++) {
			const LandUnit *u = &w->land[i];
			if (u->dead || u->location != prov) continue;
			s = side_of(m, b, u->owner);
			if (s == 1 && !atw(m, u->owner, b->attacker)) continue;
			v->n[s][0] += u->infantry; v->n[s][1] += u->cavalry; v->n[s][2] += u->artillery;
			v->nstack[s]++;
		}
		return MBV_LAND;
	}
	if (m->siege[prov].besieger > 0 && land_prov(w, prov) && w->prov[prov].controller >= 0) {
		const Siege *sg = &m->siege[prov];
		const Province *pv = &w->prov[prov];
		int bes = sg->besieger - 1;
		long mm[2] = { 0, 0 }, nn[2] = { 0, 0 };
		v->kind = MBV_SIEGE;
		v->country[0] = bes; v->country[1] = pv->controller;
		for (i = 0; i < w->nland; i++) {
			const LandUnit *u = &w->land[i];
			if (u->dead || u->location != prov) continue;
			s = u->owner == bes ? 0 : u->owner == pv->controller ? 1 : -1;
			if (s < 0) continue;
			v->n[s][0] += u->infantry; v->n[s][1] += u->cavalry; v->n[s][2] += u->artillery;
			nn[s] += unit_men(u); mm[s] += unit_men(u) * (u->morale_m / 10);
		}
		v->n[1][0] += sg->g_inf; v->n[1][2] += sg->g_art;          /* the fortress's own garrison (SPEC_ASSAULT 2) */
		nn[1] += sg->g_inf + 100 * sg->g_art; mm[1] += (sg->g_inf + 100 * sg->g_art) * (sg->g_mor / 10);
		for (s = 0; s < 2; s++) {
			long mx = max_morale(m, v->country[s]);
			v->morale[s] = nn[s] ? mm[s] / nn[s] * 10 : (s == 1 ? mx : 0);    /* a fortress without an army: full (NIEPEWNE) */
			v->attr[s] = max_attrition(m, prov, v->country[s]);
			if (mx > v->maxmor) v->maxmor = mx;
		}
		v->fort = pv->fort; v->distress = sg->distress; v->breaches = sg->breaches;
		v->rating = v->n[0][2] >= 40L * pv->fort ? 3 : v->n[0][2] >= 25L * pv->fort ? 2 : v->n[0][2] >= 10L * pv->fort ? 1 : 0;
		v->hold = pv->fort - (int)(sg->starve_m / 1000) - sg->distress;
		return MBV_SIEGE;
	}
	return MBV_NONE;
}

int mil_detach(Military *m, int i, int cover)
{
	World *w = m->w;
	LandUnit *u;
	long need, men;
	int k, p;
	if (i < 0 || i >= w->nland) return -1;
	u = &w->land[i];
	p = u->location;
	if (u->dead || p <= 0 || p >= w->nprov || m->siege[p].besieger != u->owner + 1) return -1;
	need = (cover ? 1000L : 5000L) * (w->prov[p].fort > 0 ? w->prov[p].fort : 1);
	men = u->infantry + u->cavalry;
	if (men <= need) return -1;
	if ((k = mil_new_army(m, u->owner, p, u->infantry * need / men, u->name)) < 0) return -1;
	u = &w->land[i];
	w->land[k].cavalry = need - w->land[k].infantry; w->land[k].morale_m = u->morale_m;
	u->infantry -= w->land[k].infantry; u->cavalry -= w->land[k].cavalry;
	logm(m, "%s: a %s force of %ld men detached in %s", w->country[u->owner].tag, cover ? "cover" : "siege", need, w->prov[p].name ? w->prov[p].name : "?");
	return k;
}

/* the fortress falls - a surrender or a won assault (SPEC_ASSAULT 6): 20 % the fort loses a level (not
 * below 1), plunder between hostile religions (ours: different state religions, NIEPEWNE pairs) of
 * attacker men / 100 people (at least 1000, only above 5000), then the new controller and the messages */
static void siege_won(Military *m, int p, int bes)
{
	World *w = m->w;
	Province *pv = &w->prov[p];
	int ctl = pv->controller, i;
	if (pv->fort > 1 && mrand(m) % 100 < 20) pv->fort--;
	if (ctl >= 0 && pv->population > 5000 && w->country[bes].religion != w->country[ctl].religion) {
		long men = 0, pl;
		for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].location == p && w->land[i].owner == bes) men += unit_men(&w->land[i]);
		pl = men / 100 < 1000 ? 1000 : men / 100;
		pv->population -= pl;
		if (pv->population < 1000) pv->population = 1000;
	}
	if (ctl >= 0 && bes == pv->owner) world_note(w, NOTE_LIBERATED, bes, p, ctl);
	else if (ctl >= 0) { world_note(w, NOTE_SIEGE_WIN, bes, p, ctl); world_note(w, NOTE_PROV_LOST, ctl, p, bes); siege_vp(w, p, bes, ctl); }
	pv->controller = (short)bes;
	memset(&m->siege[p], 0, sizeof m->siege[p]);
}

/* BUTTON_ASSAULT (SPEC_ASSAULT 4.1): land tech 5, infantry, a full siege (5000 x fort level men), the
 * leading besieger, morale at least half the maximum; then the assault battle starts (next day on) */
int mil_assault(Military *m, int i)
{
	World *w = m->w;
	LandUnit *u;
	Province *pv;
	long inf = 0, men = 0, mm = 0;
	int p, j, c, k;
	if (i < 0 || i >= w->nland) return MIL_AS_NOSIEGE;
	u = &w->land[i]; c = u->owner; p = u->location;
	if (u->dead || p <= 0 || p >= w->nprov || m->siege[p].besieger != c + 1 || m->inbat[p]) return MIL_AS_NOSIEGE;
	if (w->country[c].land_tech < 5) return MIL_AS_TECH;                    /* "Allows Assault" */
	pv = &w->prov[p];
	for (j = 0; j < w->nland; j++) {
		const LandUnit *v = &w->land[j];
		if (v->dead || v->owner != c || v->location != p) continue;
		inf += v->infantry; men += unit_men(v); mm += unit_men(v) * (v->morale_m / 10);
	}
	if (inf <= 0) return MIL_AS_NOINF;
	if (men < 5000L * (pv->fort > 0 ? pv->fort : 1)) return MIL_AS_NOSIEGE;   /* only covering */
	if (men && mm / men * 10 * 2 < max_morale(m, c)) return MIL_AS_MORALE;
	for (k = 0; k < MAXB && m->b[k].active; k++) ;
	if (k == MAXB) return MIL_AS_NOSIEGE;
	memset(&m->b[k], 0, sizeof m->b[k]);
	m->b[k].active = 1; m->b[k].assault = 1; m->b[k].prov = (short)p; m->b[k].attacker = (short)c;
	m->b[k].started = m->today; m->b[k].lastc[0] = (short)c; m->b[k].lastc[1] = pv->controller;
	m->inbat[p] = 1;
	{	long am, ai, aa, af, as, amor, gm, gf, gs; int ac, gc;
		as_attacker(m, &m->b[k], &am, &ai, &aa, &af, &as, &amor, &ac); as_garrison(m, &m->b[k], &gm, &gf, &gs, &gc);
		m->b[k].phase = (unsigned char)(af > 0 || gf > 0 ? 0 : 1);
	}
	as_phase_losses(m, &m->b[k]);
	logm(m, "%s assaults %s (garrison %ld / %ld art)", w->country[c].tag, pv->name ? pv->name : "?", m->siege[p].g_inf, m->siege[p].g_art);
	return MIL_AS_OK;
}

static int army_free(const Military *m, int i)
{
	const LandUnit *u = &m->w->land[i];
	return !u->dead && u->location > 0 && u->location < MAX_PROV && !m->inbat[u->location];
}

int mil_split(Military *m, int i)
{
	World *w = m->w;
	LandUnit *u, *n;
	int k;
	if (i < 0 || i >= w->nland || !army_free(m, i)) return -1;
	u = &w->land[i];
	if (u->infantry + u->cavalry < 2 && u->artillery < 2) return -1;
	if ((k = mil_new_army(m, u->owner, u->location, u->infantry / 2, u->name)) < 0) return -1;
	u = &w->land[i]; n = &w->land[k];
	n->cavalry = u->cavalry / 2; n->artillery = u->artillery / 2; n->morale_m = u->morale_m;
	u->infantry -= n->infantry; u->cavalry -= n->cavalry; u->artillery -= n->artillery;
	logm(m, "%s: %s splits in %s", w->country[u->owner].tag, u->name ? u->name : "army", w->prov[u->location].name ? w->prov[u->location].name : "?");
	return k;
}

int mil_merge_count(const Military *m, int i)
{
	const World *w = m->w;
	int j, n = 0;
	if (i < 0 || i >= w->nland || !army_free(m, i)) return 0;
	for (j = 0; j < w->nland; j++)
		if (j != i && !w->land[j].dead && w->land[j].owner == w->land[i].owner && w->land[j].location == w->land[i].location) n++;
	return n;
}

int mil_merge(Military *m, int i)
{
	World *w = m->w;
	LandUnit *u;
	int j, n = 0;
	if (mil_merge_count(m, i) <= 0) return 0;
	u = &w->land[i];
	for (j = 0; j < w->nland; j++) {
		LandUnit *v = &w->land[j];
		long a, b;
		if (j == i || v->dead || v->owner != u->owner || v->location != u->location) continue;
		a = unit_men(u); b = unit_men(v);
		if (a + b > 0) u->morale_m = (u->morale_m / 10 * a + v->morale_m / 10 * b) / (a + b) * 10;
		u->infantry += v->infantry; u->cavalry += v->cavalry; u->artillery += v->artillery;
		if (!u->l_id && v->l_id && v->l_type) { u->l_id = v->l_id; u->l_type = v->l_type; u->l_rank = v->l_rank; u->l_move = v->l_move; u->l_fire = v->l_fire; u->l_shock = v->l_shock; u->l_siege = v->l_siege; }
		v->dead = 1; v->infantry = v->cavalry = v->artillery = 0; v->npath = 0; v->l_id = 0;
		n++;
	}
	if (n) logm(m, "%s: %d unit(s) merge into %s", w->country[u->owner].tag, n, u->name ? u->name : "army");
	return n;
}

void mil_disband(Military *m, int i)
{
	World *w = m->w;
	if (i < 0 || i >= w->nland || !army_free(m, i)) return;
	logm(m, "%s: %s disbanded", w->country[w->land[i].owner].tag, w->land[i].name ? w->land[i].name : "army");
	w->land[i].dead = 1; w->land[i].infantry = w->land[i].cavalry = w->land[i].artillery = 0; w->land[i].npath = 0; w->land[i].l_id = 0;
}

static int fleet_free(const World *w, int i) { const NavalUnit *f = &w->naval[i]; return f->warships + f->galleys + f->transports > 0 && f->location > 0 && mil_fleet_load(w, i) == 0; }

int mil_split_fleet(Military *m, int i)
{
	World *w = m->w;
	NavalUnit *f, *n;
	int k;
	if (i < 0 || i >= w->nnaval || !fleet_free(w, i)) return -1;
	f = &w->naval[i];
	if (f->warships + f->galleys + f->transports < 2) return -1;
	if ((k = mil_new_fleet(m, f->owner, f->location, f->warships / 2, f->name)) < 0) return -1;
	f = &w->naval[i]; n = &w->naval[k];
	n->galleys = (short)(f->galleys / 2); n->transports = (short)(f->transports / 2);
	f->warships = (short)(f->warships - n->warships); f->galleys = (short)(f->galleys - n->galleys); f->transports = (short)(f->transports - n->transports);
	return k;
}

int mil_merge_fleet_count(const Military *m, int i)
{
	const World *w = m->w;
	int j, n = 0;
	if (i < 0 || i >= w->nnaval || !fleet_free(w, i)) return 0;
	for (j = 0; j < w->nnaval; j++)
		if (j != i && w->naval[j].owner == w->naval[i].owner && w->naval[j].location == w->naval[i].location && fleet_free(w, j)) n++;
	return n;
}

int mil_merge_fleet(Military *m, int i)
{
	World *w = m->w;
	NavalUnit *f;
	int j, n = 0;
	if (mil_merge_fleet_count(m, i) <= 0) return 0;
	f = &w->naval[i];
	for (j = 0; j < w->nnaval; j++) {
		NavalUnit *g = &w->naval[j];
		if (j == i || g->owner != f->owner || g->location != f->location || !fleet_free(w, j)) continue;
		f->warships = (short)(f->warships + g->warships); f->galleys = (short)(f->galleys + g->galleys); f->transports = (short)(f->transports + g->transports);
		if (!f->l_id && g->l_id) { f->l_id = g->l_id; f->l_move = g->l_move; f->l_type = g->l_type; }
		g->warships = g->galleys = g->transports = 0; g->npath = 0; g->l_id = 0;
		n++;
	}
	return n;
}

void mil_disband_fleet(Military *m, int i)
{
	World *w = m->w;
	if (i < 0 || i >= w->nnaval || !fleet_free(w, i)) return;
	w->naval[i].warships = w->naval[i].galleys = w->naval[i].transports = 0; w->naval[i].npath = 0; w->naval[i].l_id = 0;
}

int mil_new_army(Military *m, int c, int prov, long inf, const char *name)
{
	World *w = m->w;
	LandUnit *n;
	int k;
	for (k = 0; k < w->nland && w->land[k].dead != 1; k++) ;   /* a destroyed unit's slot first (never one at sea) */
	if (k == w->nland) { if (w->nland >= MAX_LAND) return -1; w->nland++; }
	n = &w->land[k];
	memset(n, 0, sizeof *n);
	n->id = 900000L + k; n->name = name && *name ? name : "New Army"; n->owner = (short)c; n->location = (short)prov;
	n->infantry = inf; n->morale_m = max_morale(m, c); m->aplan[k] = 0; m->astuck[k] = 0;
	logm(m, "%s: %s arrives in %s with %ld men", w->country[c].tag, n->name, w->prov[prov].name ? w->prov[prov].name : "?", inf);
	return k;
}

int mil_new_fleet(Military *m, int c, int prov, int ships, const char *name)
{
	World *w = m->w;
	NavalUnit *n;
	int k;
	for (k = 0; k < w->nnaval && w->naval[k].warships + w->naval[k].galleys + w->naval[k].transports > 0; k++) ;
	if (k == w->nnaval) { if (w->nnaval >= MAX_NAVAL) return -1; w->nnaval++; }
	n = &w->naval[k];
	memset(n, 0, sizeof *n);
	n->id = 950000L + k; n->name = name && *name ? name : "New Fleet"; n->owner = (short)c; n->location = (short)prov;
	n->warships = (short)ships;
	n->sea_since = w->now.year * 12L + w->now.month;
	m->fplan[k] = 0;
	logm(m, "%s: %s sets sail from %s with %d ships", w->country[c].tag, n->name, w->prov[prov].name ? w->prov[prov].name : "?", ships);
	return k;
}

int mil_besieger(const Military *m, int prov) { return m && prov > 0 && prov < MAX_PROV ? m->siege[prov].besieger - 1 : -1; }

/* ================================================================ the military AI
 * docs/SPEC_MILITARY_AI.md (eu2.exe v1.09, read as a specification only). Parts: A the King, B
 * recruitment, C the land war, D defence and fleets. Ours where the exe's object model does not fit
 * (each marked where it is done):
 *  - a General per (country, land mass) instead of per connected area of one owner (C.1, NIEPEWNE);
 *  - the Generals' candidate scans are polled on their think instead of the exe's war / province events
 *    (C.2.5) and committed at once instead of a day later (C.2.4);
 *  - the King's money queues (B.4) are the treasury itself; the orders are served in priority order;
 *  - strength (0x48f070, NIEZNANE in detail) = (fire + shock per man of the country's LAND.csv level)
 *    x current morale; every need / threat / power below is in these units ("str");
 *  - the AI-speed option k of 0x55a4a0 is taken as 0 (the day counts as written in 0.1).
 *  - part D (fleets, invasions) has its own "ours" list at ai_admiral, ai_send, ai_transport_think, ai_generals and
 *    ai_split; the main ones: one Admiral per General (not per sea area), its theatre = the sea zones within
 *    AI_ADM_R steps of its home port, war scans polled (only while it has a free fighting fleet), expedition
 *    Generals only for the enemy capital's land mass (at most AI_EXP_MAX), TRANSPORT polled daily with our own
 *    embarkation / landing choice, and a transport may take a fleet that only waits for a naval plan.
 * Fixed point: scores and .ai weights x1000. */

static const long aiw_default[AIW_N] = { 5000, 100, 500, 2000, 1000, 1000, 1000, 3000, 1000, 2000, 5000, 100, 5000 };   /* default.ai */

void mil_set_ai_weights(Military *m, int c, const long *wt) { if (m && c >= 0 && c < MAX_COUNTRY) { memcpy(m->aiw[c], wt, sizeof m->aiw[c]); m->aiw_set[c] = 1; } }
void mil_set_ai_query(Military *m, MilAiQuery fn, void *user) { if (m) { m->aiq = fn; m->aiqu = user; } }
static const long *aiw(const Military *m, int c) { return m->aiw_set[c] ? m->aiw[c] : aiw_default; }
static int ai_query(const Military *m, int c, int what, int dflt) { return m->aiq ? m->aiq(m->aiqu, c, what) : dflt; }

/* land masses (static: the map), provinces per continent; empty AI state */
static void ai_init(Military *m)
{
	World *w = m->w;
	static short q[MAX_PROV];
	int p, g = 0;
	for (p = 0; p < MAX_PROV; p++) m->lgrp[p] = -1;
	for (p = 1; p < w->nprov && p < 1616; p++) {
		int h = 0, t = 0;
		if (!land_prov(w, p) || m->lgrp[p] >= 0) continue;
		m->lgrp[p] = (short)g; q[t++] = (short)p;
		while (h < t) {
			int a = q[h++];
			unsigned long k;
			if (a >= 1616) continue;
			for (k = m->astart[a]; k < m->astart[a + 1]; k++) {
				int nb = (int)m->arec[2 * k];
				if (nb < MAX_PROV && land_prov(w, nb) && m->lgrp[nb] < 0) { m->lgrp[nb] = (short)g; q[t++] = (short)nb; }
			}
		}
		g++;
	}
	for (p = 1; p < w->nprov; p++) if (land_prov(w, p) && w->prov[p].continent >= 0 && w->prov[p].continent < MAX_NAMES) m->ncont[w->prov[p].continent]++;
	for (p = 0; p < w->nprov && p < MAX_PROV; p++) {
		unsigned long e;
		w->prov[p].lgroup = m->lgrp[p];                         /* for the economy (+0x498) */
		w->prov[p].island = 1;
		if (p > 0 && p < 1616 && m->astart) for (e = m->astart[p]; e < m->astart[p + 1]; e++) { int q = (int)m->arec[2 * e]; if (q > 0 && q < w->nprov && w->prov[q].terrain < 5) w->prov[p].island = 0; }
	}
	for (p = 1; p < w->nprov && p < MAX_PROV; p++) if (land_prov(w, p)) m->lprov[m->nlprov++] = (short)p;
	for (p = 0; p < MAX_PROV; p++) m->zp_head[p] = m->gp_head[p] = m->fz_head[p] = -1;
	for (p = (w->nprov < MAX_PROV ? w->nprov : MAX_PROV) - 1; p > 0; p--) {   /* descending pushes: ascending lists */
		unsigned long k;
		if (!land_prov(w, p)) continue;
		if (port_prov(w, p)) { int z = w->prov[p].port_sea; m->zp_next[p] = m->zp_head[z]; m->zp_head[z] = (short)p; m->coast[p] = 1; }
		if (p < 1616) for (k = m->astart[p]; k < m->astart[p + 1]; k++) if (sea_prov(w, (int)m->arec[2 * k])) m->coast[p] = 1;
		if (m->lgrp[p] >= 0) { m->gp_next[p] = m->gp_head[m->lgrp[p]]; m->gp_head[m->lgrp[p]] = (short)p; if (m->coast[p]) m->gcoast[m->lgrp[p]] = 1; }
	}
	{	unsigned long k;                                        /* sea zone adjacency, sea neighbours only (the naval searches) */
		int z;
		long n = 0;
		for (z = 0; z < 1616; z++) if (sea_prov(w, z)) for (k = m->astart[z]; k < m->astart[z + 1]; k++) if (m->arec[2 * k] < 1616 && sea_prov(w, (int)m->arec[2 * k])) n++;
		m->sadj = (short *)malloc((size_t)(n + 1) * sizeof(short));
		n = 0;
		for (z = 0; z < 1616; z++) {
			m->sst[z] = (unsigned short)n;
			if (m->sadj && sea_prov(w, z) && n < 65000)
				for (k = m->astart[z]; k < m->astart[z + 1]; k++) if (m->arec[2 * k] < 1616 && sea_prov(w, (int)m->arec[2 * k])) m->sadj[n++] = (short)m->arec[2 * k];
		}
		m->sst[1616] = (unsigned short)n;
		m->thpool = (short *)malloc(16384 * sizeof(short)); m->thused = 0;
	}
	for (p = 0; p < AIG_MAX; p++) m->gen[p].c = -1;
}

/* who the AI runs: not the player, not the rebels or the other special tags (0x45bce0: 0, 0x14f..0x153) */
/* the King's money pool (SPEC_MILITARY_AI A.6), ours: what the AI may spend on troops is the treasury less what its waiting
   envoy plans hold (NIEPEWNE: the original hands money out by plan score) */
#define AI_CASH(w, c) ((w)->country[c].treasury_m - ((w)->ai_resv ? (w)->ai_resv((w), (c)) : 0))
static int ai_country(const Military *m, int c)
{
	const World *w = m->w;
	return c >= 0 && c < w->ncountry && c != w->reb && c != w->nat && c != w->pir && !w->country[c].human && w->country[c].nprov_owned > 0;   /* the exe's 0x14f..0x153: its special countries */
}

static long ai_isqrt(long v) { long r = 0, b = 1L << 30; if (v <= 0) return 0; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static long ai_sqd(const World *w, int a, int b)       /* squared map distance (prov+0x3e4/+0x3e8, x wraps at 0x4a00) */
{
	long dx = w->prov[a].army_x - w->prov[b].army_x, dy = w->prov[a].army_y - w->prov[b].army_y;
	if (dx < 0) dx = -dx;
	if (dx > 0x2500) dx = 0x4a00 - dx;
	return dx * dx + dy * dy;
}
static long ai_dist(const World *w, int a, int b) { return ai_isqrt(ai_sqd(w, a, b)) / 100; }   /* 0x43a820: map pixels / 100 */

static void ai_ufs(const Military *m, const LandUnit *u, long *f, long *s)   /* fire, shock (plain terrain, no leader) */
{
	const LandLev *L = lev_of(m, u->owner);
	*f = u->infantry / 10 * L->vif / 100 + u->cavalry / 10 * L->vcf / 100 + u->artillery * L->vaf / 1000;   /* (32-bit safe: 68k long) */
	*s = u->infantry / 10 * L->vis / 100 + u->cavalry / 10 * L->vcs / 100 + u->artillery * L->vas / 1000;
}
static long ai_ustr(const Military *m, const LandUnit *u)
{
	long f, s, mo = u->morale_m ? u->morale_m : max_morale(m, u->owner);
	ai_ufs(m, u, &f, &s);
	return (f + s) / 10 * mo / 100;
}
/* the fire / shock an army of inf, cav men and art pieces of country c would have, x c's maximum morale */
static void ai_ref(const Military *m, int c, long inf, long cav, long art, long *f, long *s)
{
	const LandLev *L = lev_of(m, c);
	long mo = max_morale(m, c);
	*f = (inf * L->vif / 1000 + cav * L->vcf / 1000 + art * L->vaf / 1000) * mo / 1000;
	*s = (inf * L->vis / 1000 + cav * L->vcs / 1000 + art * L->vas / 1000) * mo / 1000;
}
/* 0x48efe0 over province p: fire / shock of the armies hostile to c, x their morale; returns f + s */
static long ai_hostile(const Military *m, int p, int c, long *f, long *s)
{
	const World *w = m->w;
	long ff = 0, ss = 0;
	int i;
	if (p > 0 && p < MAX_PROV)
		for (i = m->loc_head[p]; i >= 0; i = m->loc_next[i]) {
			const LandUnit *u = &w->land[i];
			long a, b, mo;
			if (u->dead || u->location != p || !atw(m, u->owner, c)) continue;
			ai_ufs(m, u, &a, &b);
			mo = u->morale_m ? u->morale_m : max_morale(m, u->owner);
			ff += a / 10 * mo / 100; ss += b / 10 * mo / 100;
		}
	if (f) *f = ff;
	if (s) *s = ss;
	return ff + ss;
}
static long ai_hostile_nb(const Military *m, int p, int c, long *f, long *s)   /* 0x559ca0: over p's land neighbours */
{
	long ff = 0, ss = 0, a, b;
	unsigned long k;
	if (p > 0 && p < 1616)
		for (k = m->astart[p]; k < m->astart[p + 1]; k++) {
			int nb = (int)m->arec[2 * k];
			if (!land_prov(m->w, nb) || m->loc_head[nb] < 0) continue;
			ai_hostile(m, nb, c, &a, &b); ff += a; ss += b;
		}
	*f = ff; *s = ss;
	return ff + ss;
}

/* x is a friend of c (0x43a530): c itself, x gives c military access, or the same military alliance (malli:
 * a country's first military alliance - diplo.c keeps one per country, §3.2) */
static int ai_friend(const Military *m, int c, int x)
{
	return x >= 0 && x < MAX_COUNTRY && (x == c || (m->w->dipflag[x][c] & DF_ACCESS) || (m->malli[c] >= 0 && m->malli[c] == m->malli[x]));
}
#define FRIEND(m, x) ai_friend(m, c, x)

/* 0x45f730: c (whose friends are marked) may enter p */
static int may_pass(const Military *m, int c, int p);
static int ai_enter(const Military *m, int c, int p) { return may_pass(m, c, p); }
static int ai_ctl_ok(const Military *m, int c, int p) { int k = m->w->prov[p].controller; return k == c || FRIEND(m, k) || m->siege[p].besieger == c + 1; }
/* 0x483940: in supply - p or a land neighbour controlled by c or a friend (a siege counts for the besieger) */
static int ai_in_supply(const Military *m, int c, int p)
{
	unsigned long k;
	if (ai_ctl_ok(m, c, p)) return 1;
	if (p >= 1616) return 0;
	for (k = m->astart[p]; k < m->astart[p + 1]; k++) { int nb = (int)m->arec[2 * k]; if (land_prov(m->w, nb) && ai_ctl_ok(m, c, nb)) return 1; }
	return 0;
}
static int ai_borders_own(const Military *m, int c, int p)   /* 0x484260: a neighbour c owns and controls */
{
	unsigned long k;
	if (p >= 1616) return 0;
	for (k = m->astart[p]; k < m->astart[p + 1]; k++) { int nb = (int)m->arec[2 * k]; if (land_prov(m->w, nb) && m->w->prov[nb].owner == c && m->w->prov[nb].controller == c) return 1; }
	return 0;
}
static int ai_city(const Province *pv) { return pv->population >= 1000; }   /* 0x482920 (NIEPEWNE: "has a city") */

/* C.3.3 province value V (0x559990), x1000. The last term (+10 x prov+0x140) is NIEZNANE here: 0. */
static long ai_value_v(const Military *m, int c, int p)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	long v = pv->income_m / 12 + (6 - pv->fort) * 1000L + (ai_city(pv) ? 5000 : 0) + (core_of(pv, c) ? 10000 : 0), sup;
	int cap = w->country[c].capital;
	if (cap > 0 && adjacent(m, p, cap)) v = 2 * (v + 25000);
	if (ai_borders_own(m, c, p)) v = 2 * (v + 25000);
	sup = supply_value(m, p, c);
	if (sup < 5) v = v * 85 / 100;
	if (sup < 10) v = v * 85 / 100;
	if (sup < 20) v = v * 85 / 100;
	if (sup < 30) v = v * 85 / 100;
	return v;
}

/* C.3.1 CONQUER score (0x550830), x1000; -1 = not a target */
static long ai_cq_score(const Military *m, int c, int p)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	const long *W = aiw(m, c);
	long s, hs;
	int sup;
	if (!land_prov(w, p) || pv->owner < 0 || pv->controller < 0 || pv->controller == c || !atw(m, c, pv->controller)) return -1;
	if (!ai_enter(m, c, p)) return -1;                     /* ours: the plan would be cancelled at once (0x551090) */
	sup = ai_in_supply(m, c, p);
	if (pv->owner != c && !sup) return -1;
	s = ai_value_v(m, c, p) / 10 * W[AIW_ENEMY] / 100;
	if (sup) {
		int o = pv->owner, cap = w->country[o].capital;
		s += 30 * W[AIW_SUPPLY];
		if (o != c && atw(m, c, o) && cap > 0 && cap < w->nprov && m->lgrp[cap] == m->lgrp[p]) {   /* the owner's capital area (NIEPEWNE: = its land mass) */
			long d, dd = ai_dist(w, p, cap);
			d = p == cap ? 10000 : dd < 10 ? 9000 : 9000 / dd;     /* 0x475790 in 0x43a820 units (NIEPEWNE) */
			s += W[AIW_DIST] * d / 1000;
		} else if (o == c) s += 3 * W[AIW_OWNER];
	} else s = s / 10 * W[AIW_NOTSUP] / 100 / 4;
	hs = ai_hostile(m, p, c, NULL, NULL);                  /* (f + s) x morale(them) / (morale(us) x 10000) */
	if (hs > 0) s -= hs * 100 / max_morale(m, c);
	if (s <= 100) s = 1100;
	return s + 15 * W[AIW_BASE];
}

/* C.4.1 LANDATTACK score (0x54cbf0) on our province p of land mass grp, x1000; -1 = nothing to do */
static long ai_la_score(const Military *m, int c, int grp, int p)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	long v, sup;
	int occ, sieged, bes, cap = w->country[c].capital;
	if (!land_prov(w, p) || m->lgrp[p] != grp || pv->owner != c || pv->controller < 0) return -1;
	occ = pv->controller != c;
	bes = m->siege[p].besieger - 1;
	sieged = !occ && bes >= 0 && bes != c;
	if (!occ && ai_hostile(m, p, c, NULL, NULL) <= 0) return -1;      /* 0x54c200: done */
	v = pv->income_m / 12 + (6 - pv->fort) * 1000L;                  /* V2 (0x559ae0) */
	if (ai_city(pv)) v *= 2;
	v *= 2;                                                           /* we own it */
	if (core_of(pv, c)) v *= 5;
	sup = supply_value(m, p, c);
	if (sup < 5) v /= 4;
	if (sup < 10) v /= 3;
	if (sup < 20) v /= 2;
	if (sup < 30) v = v * 4 / 5;
	if (cap > 0 && cap < MAX_PROV && m->lgrp[cap] == grp) v = v * 11 / 10;
	v = sieged ? v * 5 / 4 : v / 2;
	if (occ || sieged) v = 4 * v + 100000;
	v *= 2;
	if (pv->controller == w->reb) v *= 2;
	else if (bes >= 0 && bes == w->reb) v *= 3;
	return (v > 0 ? v : 0) + 20000;
}

/* C.4.1 force needed of a LANDATTACK (0x54bdb0), str */
static long ai_la_need(const Military *m, int c, int p)
{
	const Province *pv = &m->w->prov[p];
	long a, b, cc, d, A, B, n;
	int F = pv->fort;
	ai_hostile(m, p, c, &a, &b);
	ai_hostile_nb(m, p, c, &cc, &d);
	if (F > 0 && pv->controller != c) {
		/* NIEPEWNE: the spec's two readings of 0x54bdb0 differ - C.4.1 (15000F + 2000) infantry, D.3 (3000F + 2000);
		 * D's is taken (with C's the AI of a small country never retakes a fortified province from rebels, and
		 * 3000F + 2000 + 1000(F + 1) men is about the 5000 x F a siege needs). x morale(U) once (the exe: squared). */
		ai_ref(m, c, 3000L * F + 2000, 1000L * (F + 1), 5L * F, &A, &B);
		if (a < A) a = A;
		if (b < B) b = B;
	} else if (b < 500) b = 500;
	n = a + b + (cc + d) / 4;
	if (pv->terrain == 1) n = n * 11 / 10;
	else if (pv->terrain == 2) n *= 2;
	else if (pv->terrain == 4) n = n * 3 / 2;
	return n * 3 / 2;
}

/* C.3.4 force needed of a CONQUER (0x550b20), str; no leaders on either side (the leader terms give 1.0) */
static long ai_cq_need(const Military *m, int c, int p)
{
	const Province *pv = &m->w->prov[p];
	long a, b, cc, d, A, B, n;
	int F = pv->fort;
	ai_hostile(m, p, c, &a, &b);
	ai_hostile_nb(m, p, c, &cc, &d);
	a += cc / 2; b += d / 2;
	ai_ref(m, c, 7500L * F + 5000, 1000L * (F + 2), 30L * F, &A, &B);
	if (a < A) a = A;
	if (b < B) b = B;
	n = a + b;
	if (pv->terrain == 2) n = n * 11 / 10;                   /* the mountain bonus m = 1 */
	return n;
}

/* ------------------------------------------------ per-day lists */
static void ai_lists(Military *m)
{
	World *w = m->w;
	int i, p;
	for (i = 0; i < MAX_COUNTRY; i++) { m->own_head[i] = m->ctl_head[i] = m->arm_head[i] = m->cpl_head[i] = m->cg_head[i] = m->malli[i] = m->fo_head[i] = -1; m->navw[i] = m->navg[i] = m->navt[i] = 0; m->cdue[i] = 0x7FFFFFFFL; }
	for (i = 0; i < w->nally; i++)
		if (w->ally[i].type == ALLY_MILITARY) { int k; for (k = 0; k < w->ally[i].n; k++) { int x = w->ally[i].member[k]; if (x >= 0 && x < MAX_COUNTRY && m->malli[x] < 0) m->malli[x] = (short)i; } }
	for (i = m->nlprov - 1; i >= 0; i--) {
		const Province *pv = &w->prov[p = m->lprov[i]];
		m->qland[p] = m->qsea[p] = 0;
		if (pv->owner >= 0 && pv->owner < MAX_COUNTRY) { m->own_next[p] = m->own_head[pv->owner]; m->own_head[pv->owner] = (short)p; }
		if (pv->controller >= 0 && pv->controller < MAX_COUNTRY) { m->ctl_next[p] = m->ctl_head[pv->controller]; m->ctl_head[pv->controller] = (short)p; }
	}
	for (i = 0; i < w->nbuild; i++) {                       /* what mil_recruit_avail counts, per province */
		const Build *b = &w->build[i];
		if (!land_prov(w, b->prov) || b->country != w->prov[b->prov].owner) continue;
		if (!b->ship) m->qland[b->prov] += b->inf + b->cav + 100L * b->art;
		else if (b->ship >= 1 && b->ship <= 3) m->qsea[b->prov] += 5000L * b->inf;
	}
	memset(m->fload, 0, (size_t)w->nnaval * sizeof m->fload[0]);
	for (i = w->nland - 1; i >= 0; i--) {
		const LandUnit *u = &w->land[i];
		if (u->dead == LAND_ABOARD && u->aboard >= 0 && u->aboard < w->nnaval) m->fload[u->aboard] += mil_army_weight(u);   /* what each fleet carries */
		if (u->dead || u->owner < 0 || u->owner >= MAX_COUNTRY || u->location <= 0) continue;
		m->arm_next[i] = m->arm_head[u->owner]; m->arm_head[u->owner] = (short)i;
	}
	for (i = AIP_MAX - 1; i >= 0; i--) {
		MilPlan *pl = &m->plan[i];
		if (pl->type == PL_FREE) { if (pl->c == -2) pl->c = -1; continue; }
		if (pl->c < 0 || pl->c >= MAX_COUNTRY) { pl->type = PL_FREE; pl->c = -1; continue; }
		m->cpl_next[i] = m->cpl_head[pl->c]; m->cpl_head[pl->c] = (short)i;
		if (pl->next < m->cdue[pl->c]) m->cdue[pl->c] = pl->next;
	}
	for (i = AIG_MAX - 1; i >= 0; i--) {
		MilGen *g = &m->gen[i];
		if (g->c == -2) g->c = -1;
		if (g->c < 0) continue;
		m->cg_next[i] = m->cg_head[g->c]; m->cg_head[g->c] = (short)i;
		if (g->next < m->cdue[g->c]) m->cdue[g->c] = g->next;
		if (g->dnext < m->cdue[g->c]) m->cdue[g->c] = g->dnext;
		if (!g->exp && g->anext < m->cdue[g->c]) m->cdue[g->c] = g->anext;
	}
	while (m->nfz > 0) m->fz_head[m->fzused[--m->nfz]] = -1;          /* (only the heads set last time) */
	for (i = w->nnaval - 1; i >= 0; i--) {
		const NavalUnit *f = &w->naval[i];
		if (f->owner >= 0 && f->owner < MAX_COUNTRY) { m->navw[f->owner] = (short)(m->navw[f->owner] + f->warships); m->navg[f->owner] = (short)(m->navg[f->owner] + f->galleys); m->navt[f->owner] = (short)(m->navt[f->owner] + f->transports); }
		if (fleet_ships(f) <= 0) { m->fplan[i] = 0; continue; }       /* sunk: the plan lets go of it */
		if (f->owner < 0 || f->owner >= MAX_COUNTRY || f->location <= 0 || f->location >= MAX_PROV) continue;
		m->fo_next[i] = m->fo_head[f->owner]; m->fo_head[f->owner] = (short)i;
		if (m->fz_head[f->location] < 0) m->fzused[m->nfz++] = f->location;
		m->fz_next[i] = m->fz_head[f->location]; m->fz_head[f->location] = (short)i;
	}
}

/* ------------------------------------------------ plans: start / end, armies */
static const char *ai_pname(int t)
{
	switch (t) {
	case PL_LANDATTACK: return "LANDATTACK";
	case PL_CONQUER: return "CONQUER";
	case PL_GARRISON: return "GARRISON";
	case PL_NAVALATTACK: return "NAVALATTACK";
	case PL_BLOCKADE: return "BLOCKADE";
	case PL_BREAKBLOCKADE: return "BREAKBLOCKADE";
	case PL_INVASION: return "INVASION";
	case PL_TRANSPORT: return "TRANSPORT";
	case PL_EXSEA: return "explorer";
	case PL_EXLAND: return "conquistador";
	case PL_KILLNAT: return "genocider";
	}
	return "?";
}
static int ai_naval_type(int t) { return t == PL_NAVALATTACK || t == PL_BLOCKADE || t == PL_BREAKBLOCKADE; }
static int ai_land_type(int t) { return t == PL_LANDATTACK || t == PL_CONQUER || t == PL_INVASION; }
static const char *ai_prov(const World *w, int p) { return p > 0 && p < w->nprov && w->prov[p].name ? w->prov[p].name : "?"; }

static void ai_end_plan(Military *m, int k, const char *why)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int i, c = pl->c;
	for (i = 0; i < w->nland; i++) if (m->aplan[i] == k + 1) m->aplan[i] = 0;
	if (pl->type == PL_TRANSPORT || pl->type == PL_EXSEA || ai_naval_type(pl->type))
		for (i = 0; i < w->nnaval; i++) if (m->fplan[i] == k + 1) m->fplan[i] = 0;
	if (c >= 0 && c < MAX_COUNTRY) {
		for (i = 0; i < AIO_MAX; i++) if (m->ord[c][i].prov > 0 && m->ord[c][i].plan == k + 1) memset(&m->ord[c][i], 0, sizeof m->ord[c][i]);
		for (i = m->cpl_head[c]; i >= 0; i = m->cpl_next[i]) if (m->plan[i].type == PL_TRANSPORT && m->plan[i].parent == k) m->plan[i].parent = -1;   /* its transports go on, for nobody */
	}
	if (pl->type != PL_GARRISON) logm(m, "AI: %s %s #%d on %s %s", c >= 0 ? w->country[c].tag : "?", ai_pname(pl->type), k, ai_prov(w, pl->target), why);
	pl->type = PL_FREE; pl->c = -2;
}

static int s_newc = -1;                    /* the country of a plan made for a plan without a General (ai_send) */
static int ai_new_plan(Military *m, int gi, int type, int t, long score)
{
	World *w = m->w;
	MilPlan *pl;
	int k, c = gi >= 0 ? m->gen[gi].c : s_newc;
	if (c < 0) return -1;
	for (k = 0; k < AIP_MAX && (m->plan[k].type != PL_FREE || m->plan[k].c == -2); k++) ;
	if (k == AIP_MAX) return -1;
	pl = &m->plan[k];
	memset(pl, 0, sizeof *pl);
	pl->type = (unsigned char)type; pl->c = (short)c; pl->gen = (short)gi; pl->target = (short)t; pl->score = score;
	pl->army = pl->fleet = pl->parent = -1;
	if (type == PL_INVASION) pl->phase = pl->engaged = 1;               /* always phase 1: its armies go as they are offered (0x5547a0) */
	pl->next = m->today + 1;                                   /* first think a day after the start (C.4.6) */
	m->cpl_next[k] = m->cpl_head[c]; m->cpl_head[c] = (short)k;
	if (type != PL_GARRISON && type != PL_TRANSPORT) logm(m, "AI: %s starts %s #%d on %s (score %ld)", w->country[c].tag, ai_pname(type), k, ai_prov(w, t), score / 1000);
	return k;
}

/* an army that may be handed out (A.6 0x5432a0): alive, on land, not at sea, not fighting, no plan; ordinary requests
   leave out units under a conquistador / explorer (0x48c1f0): they wait for an explore plan */
static int ai_free_army0(const Military *m, int i)
{
	const LandUnit *u = &m->w->land[i];
	return !u->dead && !m->aplan[i] && land_prov(m->w, u->location) && !m->inbat[u->location] && unit_men(u) > 0;
}
static int ai_free_army(const Military *m, int i)
{
	const LandUnit *u = &m->w->land[i];
	return ai_free_army0(m, i) && !(u->l_id && (u->l_type == 2 || u->l_type == 3));
}

/* the plan's armies: strength (validating the list: dead / foreign units leave it) */
static long ai_plan_own(Military *m, int k)
{
	World *w = m->w;
	int i, c = m->plan[k].c;
	long own = 0;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		if (m->aplan[i] != k + 1) continue;
		if (w->land[i].dead || w->land[i].owner != c) { m->aplan[i] = 0; continue; }
		own += ai_ustr(m, &w->land[i]);
	}
	if (ai_land_type(m->plan[k].type))                       /* + the armies of its TRANSPORT sub-plans (D.3 +0x7a) */
		for (i = m->cpl_head[c]; i >= 0; i = m->cpl_next[i]) {
			const MilPlan *t = &m->plan[i];
			if (t->type == PL_TRANSPORT && t->c == c && t->parent == k && t->army >= 0 && t->army < w->nland && w->land[t->army].dead != 1 && w->land[t->army].owner == c)
				own += ai_ustr(m, &w->land[t->army]);
		}
	return own;
}

/* bounded route (the exe's path object; ours: Dijkstra with a heap over provinces c may enter, at most
 * AI_ROUTE_DAYS): the army gets the path to dest; days or -1 */
#define AI_ROUTE_DAYS 360
static long ai_route(Military *m, int ui, int dest)
{
	World *w = m->w;
	LandUnit *u = &w->land[ui];
	static long dist[MAX_PROV], hk[8192];
	static short prev[MAX_PROV], hp[8192];
	static unsigned short stamp[MAX_PROV], cur;
	int nh, src = u->location, c = u->owner, sp = speed_x2(u) == 3 ? 0 : speed_x2(u) == 4 ? 1 : 2;
	if (w->tutorial) return 0;                                /* 0x55a038: in a tutorial the AI's route order succeeds and moves nothing */
	if (!land_prov(w, dest) || !land_prov(w, src) || src == dest || m->lgrp[src] != m->lgrp[dest]) return -1;
	if (++cur == 0) { memset(stamp, 0, sizeof stamp); cur = 1; }
#define RDIST(p) (stamp[p] == cur ? dist[p] : 0x7FFFFFFFL)
	stamp[src] = cur; dist[src] = 0; prev[src] = -1;
	hk[0] = 0; hp[0] = (short)src; nh = 1;
	while (nh > 0) {
		long d = hk[0];
		int p = hp[0], k, j;
		unsigned long e;
		nh--;
		hk[0] = hk[nh]; hp[0] = hp[nh];
		for (k = 0; ; ) {
			int l = 2 * k + 1, r = l + 1, s = k;
			if (l < nh && hk[l] < hk[s]) s = l;
			if (r < nh && hk[r] < hk[s]) s = r;
			if (s == k) break;
			{ long tk = hk[k]; short tp = hp[k]; hk[k] = hk[s]; hp[k] = hp[s]; hk[s] = tk; hp[s] = tp; }
			k = s;
		}
		if (d != RDIST(p)) continue;
		if (p == dest) {
			int steps = 0, q;
			short rev[64];
			for (q = p; q != src && steps < 64; q = prev[q]) rev[steps++] = (short)q;
			if (steps > 40) steps = 40;                          /* long orders are cut (re-issued at the next think) */
			for (q = 0; q < steps; q++) u->path[q] = rev[steps - 1 - q];
			u->npath = (short)steps; u->pathpos = 0;
			u->arrive_day = m->today + edge_days(m, u, u->path[0], adj_type(m, src, u->path[0]));
			return d;
		}
		if (p >= 1616) continue;
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
			int nb = (int)m->arec[2 * e], ty = (int)m->arec[2 * e + 1];
			long nd;
			if (!land_prov(w, nb) || (nb != dest && !ai_enter(m, c, nb)) || !may_enter(w, c, nb, 0, u->l_id ? u->l_type : 0)) continue;
			nd = d + m->eday[sp][w->prov[nb].terrain < 16 ? w->prov[nb].terrain : 0][ty == 1 ? 1 : ty == 2 ? 2 : 0];
			if (nd >= RDIST(nb) || nd > AI_ROUTE_DAYS || nh >= 8192) continue;
			stamp[nb] = cur; dist[nb] = nd; prev[nb] = (short)p;
			for (j = nh++; j > 0 && hk[(j - 1) / 2] > nd; j = (j - 1) / 2) { hk[j] = hk[(j - 1) / 2]; hp[j] = hp[(j - 1) / 2]; }
			hk[j] = nd; hp[j] = (short)nb;
		}
	}
#undef RDIST
	return -1;
}

/* C.4.6 merge (0x558e90): the plan's idle armies standing together become one */
static void ai_merge(Military *m, int k)
{
	World *w = m->w;
	int i, j, c = m->plan[k].c;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		if (m->aplan[i] != k + 1 || u->dead || u->npath || m->inbat[u->location]) continue;
		for (j = m->arm_next[i]; j >= 0; j = m->arm_next[j]) {
			LandUnit *v = &w->land[j];
			long a, b;
			if (m->aplan[j] != k + 1 || v->dead || v->npath || v->location != u->location || v->owner != c) continue;
			a = unit_men(u); b = unit_men(v);
			if (a + b > 0) u->morale_m = (u->morale_m / 10 * a + v->morale_m / 10 * b) / (a + b) * 10;
			u->infantry += v->infantry; u->cavalry += v->cavalry; u->artillery += v->artillery;
			if (!u->l_id && v->l_id) { u->l_id = v->l_id; u->l_type = v->l_type; u->l_rank = v->l_rank; u->l_move = v->l_move; u->l_fire = v->l_fire; u->l_shock = v->l_shock; u->l_siege = v->l_siege; }
			v->dead = 1; v->infantry = v->cavalry = v->artillery = 0; v->npath = 0; v->l_id = 0; m->aplan[j] = 0;
			logm(m, "AI: %s merges two armies in %s", w->country[c].tag, ai_prov(w, u->location));
		}
	}
}

/* C.4.2 gather point (0x54c6a0): lowest cost among the provinces c controls on the target's land mass */
static int ai_gather(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	const long *W = aiw(m, pl->c);
	int c = pl->c, t = pl->target, S = w->country[c].capital, q, i, best = 0, to = w->prov[t].owner;
	long bc = 10000, bs = 0x7FFFFFFFL;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {          /* the start point: the plan's main army */
		long sc;
		if (m->aplan[i] != k + 1 || w->land[i].dead) continue;
		sc = 10000 - ai_ustr(m, &w->land[i]) / 10 + ai_dist(w, t, w->land[i].location);
		if (sc < bs) { bs = sc; S = w->land[i].location; }
	}
	if (S <= 0 || S >= w->nprov) S = t;
	for (q = m->ctl_head[c]; q > 0; q = m->ctl_next[q]) {
		long cost = 10000;
		unsigned long e;
		if (w->prov[q].controller != c || m->lgrp[q] != m->lgrp[t] || q == t) continue;
		if (adjacent(m, q, t)) cost = 4000;
		else if (to >= 0 && w->prov[q].owner == to) cost = 4500;      /* the target's area (NIEPEWNE: = its owner's provinces) */
		else if (to >= 0 && q < 1616) {
			for (e = m->astart[q]; e < m->astart[q + 1]; e++) if (land_prov(w, (int)m->arec[2 * e]) && w->prov[m->arec[2 * e]].owner == to) { cost = 5000; break; }
		}
		cost += m->siege[q].besieger ? 2500 + 10000 : -250;
		cost += 10 * W[AIW_RBASE] * ai_dist(w, S, q) / 1000 + 5 * W[AIW_RFRONT] * ai_dist(w, t, q) / 1000 - supply_value(m, q, c);
		if (cost < bc) { bc = cost; best = q; }
	}
	if (best > 0) logm(m, "AI: %s will attack %s (gathering in %s)", w->country[c].tag, ai_prov(w, t), ai_prov(w, best));
	return best;
}

/* C.4.2 phases */
static void ai_phase(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, t = pl->target, i;
	long enemy;
	if (pl->gather <= 0) { pl->phase = 1; }
	else {
		if (pl->phase == 1 && pl->own * 4 < pl->need) { pl->gather = (short)ai_gather(m, k); pl->phase = 0; }   /* fall back */
		else if (pl->phase == 0 && pl->own * 4 >= pl->need * 3) {
			long there = 0;
			int all = 1;
			for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
				const LandUnit *u = &w->land[i];
				if (m->aplan[i] != k + 1 || u->dead) continue;
				if (!u->npath && (u->location == t || u->location == pl->gather)) there += ai_ustr(m, u); else all = 0;
			}
			if (all || there * 4 > pl->need * 3) pl->phase = 1;
		}
		if (pl->gather > 0 && w->prov[pl->gather].controller == c && m->siege[pl->gather].besieger) pl->gather = (short)ai_gather(m, k);
		if (pl->gather <= 0) pl->phase = 1;
	}
	if (pl->phase == 1 && (enemy = ai_hostile(m, t, c, NULL, NULL)) > 0) {   /* do not arrive piecemeal: back to gathering */
		long in = 0;
		for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) if (m->aplan[i] == k + 1 && !w->land[i].dead && w->land[i].location == t) in += ai_ustr(m, &w->land[i]);
		for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
			const LandUnit *u = &w->land[i];
			if (m->aplan[i] != k + 1 || u->dead || !u->npath || u->path[u->npath - 1] != t) continue;
			if (in + ai_ustr(m, u) < enemy) {
				if (pl->gather <= 0) pl->gather = (short)ai_gather(m, k);
				if (pl->gather > 0) { pl->phase = 0; pl->engaged = 0; }
				break;
			}
		}
	}
}

/* C.4.3 ready to attack (0x54c0a0; CONQUER first 0x551040) */
static int ai_ready(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, i;
	long men = 0, mx = max_morale(m, c);
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) if (m->aplan[i] == k + 1 && !w->land[i].dead) men += unit_men(&w->land[i]);
	if (pl->type == PL_CONQUER && men < 5000L * w->prov[pl->target].fort && pl->own < pl->need) return 0;
	if (pl->phase == 0) return 1;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i])
		if (m->aplan[i] == k + 1 && !w->land[i].dead && w->land[i].morale_m * 100 < mx * 99) return 0;
	return pl->own > ai_hostile(m, pl->target, c, NULL, NULL);
}

/* C.4.3 engage (0x54c140): idle armies march to the target (phase 1) or the gather point; one that cannot
 * get there goes back to the pool */
static void ai_engage(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, i, dest = pl->phase == 1 || pl->gather <= 0 ? pl->target : pl->gather;
	if (!pl->engaged) logm(m, "AI: %s %s #%d (%s) engages in battle!", w->country[c].tag, ai_pname(pl->type), k, ai_prov(w, pl->target));
	pl->engaged = 1;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		if (m->aplan[i] != k + 1 || u->dead || u->npath || u->location == dest || m->inbat[u->location]) continue;
		if (ai_route(m, i, dest) < 0) { m->aplan[i] = 0; m->astuck[i] = m->today + 30; logm(m, "AI: %s army in %s finds no way to %s", w->country[c].tag, ai_prov(w, u->location), ai_prov(w, dest)); }
	}
}

/* C.4.4 retreat from a battle (0x54c350): our side below 0.66 of theirs in men and in morale. Only when every
 * army of our side is the AI's (the player's units are never moved). */
static int ai_retreats(Military *m, int k)
{
	World *w = m->w;
	int c = m->plan[k].c, i, j, kb;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		const LandUnit *u = &w->land[i];
		Battle *b = NULL;
		long men[2], f, s, cav, mor[2];
		int cc, side, ok = 1;
		if (m->aplan[i] != k + 1 || u->dead || !m->inbat[u->location]) continue;
		for (kb = 0; kb < MAXB; kb++) if (m->b[kb].active && m->b[kb].prov == u->location) { b = &m->b[kb]; break; }
		if (!b || b->assault || sea_prov(w, b->prov)) continue;
		side = side_of(m, b, c);
		side_sum(m, b, side, &men[0], &f, &s, &cav, &mor[0], &cc);
		side_sum(m, b, 1 - side, &men[1], &f, &s, &cav, &mor[1], &cc);
		if (!(men[0] * 100 < 66 * men[1] && mor[0] * 100 < 66 * mor[1])) continue;
		for (j = 0; j < w->nland && ok; j++) {
			const LandUnit *v = &w->land[j];
			if (!v->dead && v->location == b->prov && side_of(m, b, v->owner) == side && v->owner >= 0 && w->country[v->owner].human) ok = 0;
		}
		if (!ok) continue;
		logm(m, "AI: %s retreats from the battle of %s (men %ld vs %ld)", w->country[c].tag, ai_prov(w, b->prov), men[0], men[1]);
		retreat_side(m, b, side);
		return 1;
	}
	return 0;
}

/* C.4.5 the assault rule (0x54cfc0) */
static int ai_assault_try(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, t = pl->target, i, inf = -1, ctl = w->prov[t].controller;
	const Siege *sg = &m->siege[t];
	long men = 0, ours = 0, theirs, gf, gs, mo, mm = 0;
	unsigned long e;
	if (!pl->engaged || sg->besieger != c + 1 || m->inbat[t] || ctl < 0) return 0;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		const LandUnit *u = &w->land[i];
		if (u->dead || u->location != t) continue;
		men += unit_men(u); ours += ai_ustr(m, u); mm += unit_men(u) * (u->morale_m / 10);
		if (inf < 0 && u->infantry > 0 && m->aplan[i] == k + 1) inf = i;
	}
	if (inf < 0 || w->country[c].land_tech < 5 || men < 5000L * (w->prov[t].fort > 0 ? w->prov[t].fort : 1) || mm / men * 10 * 2 < max_morale(m, c)) return 0;   /* what mil_assault refuses */
	if (men / 10000 < w->prov[t].fort && !(sg->breaches > 0 || sg->distress >= 7)) return 0;   /* 0x471590 / vfunc 0x84 >= 7 (NIEPEWNE: breaches / distress) */
	{	const LandLev *L = lev_of(m, ctl);                                  /* the garrison of SPEC_ASSAULT 2 */
		mo = sg->g_mor ? sg->g_mor : max_morale(m, ctl);
		gf = (sg->g_inf * L->vif + sg->g_art * L->vaf) / 1000; gs = (sg->g_inf * L->vis + sg->g_art * L->vas) / 1000;
		theirs = (gf + gs) * mo / 1000 + ai_hostile(m, t, c, NULL, NULL);
	}
	if (ours <= theirs) return 0;
	if (!(mo * 4 <= max_morale(m, ctl) || (w->prov[t].owner >= 0 && w->country[w->prov[t].owner].nprov_owned <= 1))) {
		if (t < 1616)
			for (e = m->astart[t]; e < m->astart[t + 1]; e++) {
				int nb = (int)m->arec[2 * e];
				if (land_prov(w, nb) && ai_hostile(m, nb, c, NULL, NULL) * 10 > ours) return 0;   /* a relief army next door */
			}
	}
	i = mil_assault(m, inf);
	logm(m, "AI: %s #%d (%s) assaults the fortress! (%d)", w->country[c].tag, k, ai_prov(w, t), i);
	return i == MIL_AS_OK;
}

/* ------------------------------------------------ recruitment (part B) */
/* the land support limit 0x45ab50 = the economy's (econ_support_limit, the same formula as the inline one in 0x459c40);
 * without an economy linked (test programs) the old stand-in 1000 x (5 (provinces + 1) + 2 provinces + yearly income / 10) */
void mil_set_supportfn(Military *m, long (*fn)(void *, int), void *user) { if (m) { m->supfn = fn; m->supu = user; } }
static long ai_support(const Military *m, int c)
{
	const Country *co = &m->w->country[c];
	long n = co->nprov_owned, inc = co->inc_m > 0 ? co->inc_m / 1000 * 12 : 0;
	if (m->supfn) {                                          /* once a day per country (the economy walks every province) */
		Military *mm = (Military *)m;
		if (c >= 0 && c < MAX_COUNTRY && mm->sup_day[c] == m->today + 1) return mm->sup_val[c];
		if (c >= 0 && c < MAX_COUNTRY) { mm->sup_day[c] = m->today + 1; return mm->sup_val[c] = m->supfn(m->supu, c); }
		return m->supfn(m->supu, c);
	}
	return 1000L * (5 * (n + 1) + 2 * n + inc / 10);
}

static void ai_counts(const Military *m, int c, long *inf, long *cav, long *art)   /* owned + queued (vf 0x388..0x390) */
{
	const World *w = m->w;
	int i;
	*inf = *cav = *art = 0;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) if (!w->land[i].dead) { *inf += w->land[i].infantry; *cav += w->land[i].cavalry; *art += w->land[i].artillery; }
	for (i = 0; i < w->nbuild; i++) if (w->build[i].country == c && !w->build[i].ship) { *inf += w->build[i].inf; *cav += w->build[i].cav; *art += w->build[i].art; }
}

/* B.3.1 the unit mix for power P. wgt = 1: P in str (a plan's deficit, power per man = (IF + IS) x morale);
 * wgt = 0: P in men (the King's own BuildArmy, power per man = IS - literal; the result is then capped at
 * P men, ours: the literal reading would build P / IS men). Returns 0 when nothing is to be built. */
static int ai_mix(const Military *m, int c, long P, int wgt, int prov, long *oi, long *oc, long *oa)
{
	const World *w = m->w;
	const LandLev *L = lev_of(m, c);
	long inf, cav, art, ci = unit_price(w, c, 0), cc = unit_price(w, c, 1), mo = wgt ? max_morale(m, c) : 1000;
	long pI = (wgt * L->vif + L->vis) * mo / 1000, pC = (wgt * L->vcf + L->vcs) * mo / 1000, pA = (wgt * L->vaf + L->vas) * mo / 1000;   /* x1000 per man / piece */
	long hi, hc, ha;
	ai_counts(m, c, &hi, &hc, &ha);
	if (P > 200000L) P = 200000L;                                           /* (keeps the products below inside 32 bits) */
	if (pI < 1) pI = 1;
	if (pC < 1) pC = 1;
	{	long r = ci + cc < 1 ? 1000 : ci * 1000 / (ci + cc);                  /* cavalry share */
		long f = L->vcs + L->vis < 1 ? r : L->vcs * r / (L->vcs + L->vis);
		cav = (f * (P / 10) / 100 + 500) / 1000 * 1000;
	}
	art = 0;
	if (w->country[c].land_tech >= 7 && pA > 0) {                          /* artillery allowed (0x432d70) */
		art = P / 10 * 1000 / pA;                                           /* 0.1 P / pArt */
		if (prov > 0 && w->prov[prov].fort > 0 && w->prov[prov].controller >= 0 && atw(m, c, w->prov[prov].controller)) art += 10L * w->prov[prov].fort;
		art = (art + 5) / 10 * 10;
	}
	if (cc > 2 * ci && hc > hi / 4) cav = 0;
	if (ha > hi / 1000) art = 0;
	inf = (P - cav / 10 * pC / 100 - art * pA / 1000) * 1000 / pI;         /* (P - cav pCav - art pArt) / pInf */
	inf = (inf + 500) / 1000 * 1000;
	if (inf < 0) inf = 0;
	if (!wgt) {                                                             /* ours: the King's order never above its deficit */
		if (inf + cav + 100 * art > P) { inf = P - cav - 100 * art; if (inf < 0) { inf = 0; cav = P - 100 * art; if (cav < 0) { cav = 0; art = P / 100; } } }
		inf = inf / 1000 * 1000; cav = cav / 1000 * 1000; art = art / 10 * 10;
	}
	*oi = inf; *oc = cav; *oa = art;
	return inf + cav + art > 0;
}

static void ai_add_order(Military *m, int c, int plan, long P, int wgt, int prov, int prio)
{
	int k;
	long i, cv, a;
	for (k = 0; k < AIO_MAX && m->ord[c][k].prov > 0; k++) ;
	if (k == AIO_MAX || prov <= 0) return;
	if (!ai_mix(m, c, P, wgt, prov, &i, &cv, &a)) return;
	m->ord[c][k].inf = i; m->ord[c][k].cav = cv; m->ord[c][k].art = a;
	m->ord[c][k].prov = (short)prov; m->ord[c][k].plan = (short)plan; m->ord[c][k].prio = (short)prio;
	logm(m, "AI: BuildPlan of %s plans to build units (%ld, %ld, %ld) for %s", m->w->country[c].tag, i, cv, a, plan ? ai_pname(m->plan[plan - 1].type) : "the King");
}

/* B.3.3 the province for an army order (0x547660): lowest squared distance to the target, +250000 with an
 * enemy army there, +4e6 on another land mass */
static int ai_build_prov(Military *m, int c, int target)
{
	const World *w = m->w;
	int p, best = -1;
	long bs = 0x7FFFFFFFL;
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		long s;
		if (w->prov[p].controller != c) continue;
		s = ai_sqd(w, p, target) + (m->lgrp[p] != m->lgrp[target] ? 4000000L : 0);
		if (s >= bs || recruit_cap(m, c, p, 0, m->qland[p]) < 1000) continue;     /* (the enemy term only adds: cheap tests first) */
		if (ai_hostile(m, p, c, NULL, NULL) > 0 && (s += 250000) >= bs) continue;
		if (s < bs) { bs = s; best = p; }
	}
	return best;
}

/* the order is paid and queued in one go (one Build of every arm; the price, manpower and time of the
 * player's recruits, mil_recruit) */
static int ai_raise(Military *m, int c, int p, long ni, long nc, long na)
{
	World *w = m->w;
	Country *co = &w->country[c];
	Build *b;
	long cost = ni * unit_price(w, c, 0) + nc * unit_price(w, c, 1) + na * unit_price(w, c, 2);
	if (w->nbuild >= MAX_BUILD - AI_BUILD_RESERVE || AI_CASH(w, c) < cost || ni + nc + na <= 0) return 0;
	co->treasury_m -= cost; world_book_exp(m->w, (int)(co - m->w->country), EXP_ARMY, cost);
	co->manpower_m -= 1000L * (ni + nc + na);
	if (co->manpower_m < 0) co->manpower_m = 0;
	b = &w->build[w->nbuild++];
	memset(b, 0, sizeof *b);
	b->country = (short)c; b->prov = (short)p; b->ship = 0; b->done = m->today + BUILD_DAYS(ni, nc, na);
	b->inf = 1000L * ni; b->cav = 1000L * nc; b->art = 10L * na;
	m->qland[p] += b->inf + b->cav + 100L * b->art;
	logm(m, "AI: BuildPlan of %s starts building (%ld, %ld, %ld) in %s for %ld ducats", co->tag, b->inf, b->cav, b->art, ai_prov(w, p), cost / 1000);
	return 1;
}

/* B.3.4-3.5: each order takes one province a King think, in priority order */
static void ai_orders(Military *m, int c)
{
	World *w = m->w;
	Country *co = &w->country[c];
	unsigned char done[AIO_MAX];
	int n, k, pend;
	long pi = unit_price(w, c, 0), pc = unit_price(w, c, 1), pa = unit_price(w, c, 2), hi, hc, ha, sup = ai_support(m, c);
	memset(done, 0, sizeof done);
	for (n = 0; n < AIO_MAX && m->ord[c][n].prov <= 0; n++) ;
	if (n == AIO_MAX) return;                                          /* no order */
	for (pend = k = 0; k < w->nbuild; k++) if (w->build[k].country == c && !w->build[k].ship) pend++;
	if (pend >= AI_BUILDS_EACH) return;                                   /* ours: the shared queue (MAX_BUILD) is not the exe's - a country waits for its recruits */
	ai_counts(m, c, &hi, &hc, &ha);
	for (n = 0; n < AIO_MAX; n++) {
		MilOrder *o = NULL;
		int p, cap;
		long ni, nc, na;
		for (k = 0; k < AIO_MAX; k++) if (!done[k] && m->ord[c][k].prov > 0 && (!o || m->ord[c][k].prio > o->prio)) o = &m->ord[c][k];
		if (!o) break;
		done[o - m->ord[c]] = 1;
		if (o->inf < 1000 && o->cav < 1000 && o->art < 10) goto finish;
		if (o->plan && (o->plan > AIP_MAX || m->plan[o->plan - 1].type == PL_FREE || m->plan[o->plan - 1].c != c)) goto finish;
		if (AI_CASH(w, c) < pi) continue;                                 /* 3.5 step 2: not even one regiment - waits */
		if ((p = ai_build_prov(m, c, o->prov)) <= 0) continue;
		cap = (int)(recruit_cap(m, c, p, 0, m->qland[p]) / 1000);
		ni = o->inf / 1000; nc = o->cav / 1000; na = o->art / 10;
		if (ni > cap) ni = cap;
		if (nc > cap - ni) nc = cap - ni;
		if (na > cap - ni - nc) na = cap - ni - nc;
		ni += cap - ni - nc - na;                                         /* leftover capacity: infantry (3.4) */
		if (hi + hc + 100 * ha + ni + nc + 100 * na > 3 * sup) { logm(m, "AI: BuildPlan of %s cancelled (support limit)", co->tag); goto finish; }
		while (ni * pi + nc * pc + na * pa > AI_CASH(w, c) && na > 0) na--;    /* 3.5 step 4 */
		while (ni * pi + nc * pc + na * pa > AI_CASH(w, c) && nc > 0) nc--;
		while (ni * pi + nc * pc + na * pa > AI_CASH(w, c) && ni > 0) ni--;
		if (pend >= AI_BUILDS_EACH || !ai_raise(m, c, p, ni, nc, na)) continue;   /* waits for money (or for its recruits) */
		pend++;
		o->inf -= 1000 * ni; o->cav -= 1000 * nc; o->art -= 10 * na;
		hi += 1000 * ni; hc += 1000 * nc; ha += 10 * na;
		{	const LandLev *L = lev_of(m, c);                                  /* a surplus lowers the next arm (3.5 step 6) */
			long pI = L->vif + L->vis, pC = L->vcf + L->vcs, pA = L->vaf + L->vas;
			if (o->art < 0) { if (pC > 0) o->cav += o->art * pA / pC; o->art = 0; }
			if (o->cav < 0) { if (pI > 0) o->inf += o->cav / 10 * pC / pI * 10; o->cav = 0; }
			if (o->inf < 0) o->inf = 0;
		}
		if (o->inf >= 1000 || o->cav >= 1000 || o->art >= 10) continue;
finish:
		if (o->plan && o->plan <= AIP_MAX) m->plan[o->plan - 1].asked = 0;
		memset(o, 0, sizeof *o);
	}
}

/* B.2.2 / B.3.2-3.3 BuildFleet: ships in the own port nearest the capital (a shipyard first); priority 10
 * (ours: only with no army order waiting) */
static void ai_fleet_build(Military *m, int c)
{
	World *w = m->w;
	Country *co = &w->country[c];
	MilKing *K = &m->king[c];
	int p, best = -1, k, cap = w->country[c].capital;
	long bs = 0x7FFFFFFFL;
	if (K->bf[0] + K->bf[1] + K->bf[2] <= 0 || cap <= 0) return;
	for (k = 0; k < AIO_MAX; k++) if (m->ord[c][k].prov > 0) return;
	for (k = 0; k < w->nbuild; k++) if (w->build[k].country == c && w->build[k].ship >= 1 && w->build[k].ship <= 3) return;   /* ours: one batch at a time (the shared queue) */
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		long s;
		if (w->prov[p].controller != c || !port_prov(w, p) || recruit_cap(m, c, p, 3, m->qsea[p]) <= 0 || ai_hostile(m, p, c, NULL, NULL) > 0) continue;
		s = ai_sqd(w, p, cap) / 4 - ((w->prov[p].bflags & PB_SHIPYARD) ? 9000000L : 0) + (m->lgrp[p] != m->lgrp[cap] ? 1000000L : 0);
		if (s < bs) { bs = s; best = p; }
	}
	if (best < 0) return;
	{	long room = recruit_cap(m, c, best, 3, m->qsea[best]);
		for (k = 0; k < 3 && room > 0; k++) {
			long n = K->bf[k] < room ? K->bf[k] : room, cost;
			Build *b;
			cost = mil_ship_cost(m, c, k);
			while (n > 0 && n * cost > AI_CASH(w, c)) n--;
			if (n <= 0 || w->nbuild >= MAX_BUILD - AI_BUILD_RESERVE) continue;
			co->treasury_m -= n * cost; world_book_exp(m->w, (int)(co - m->w->country), EXP_FLEET, n * cost);
			b = &w->build[w->nbuild++];
			memset(b, 0, sizeof *b);
			b->country = (short)c; b->prov = (short)best; b->ship = (short)(k + 1); b->inf = n; b->done = m->today + mil_ship_days(m, c, k);
			K->bf[k] = (short)(K->bf[k] - n); room -= n; m->qsea[best] += 5000L * n;
			logm(m, "AI: BuildFleet of %s starts %ld %s in %s", co->tag, n, k == 0 ? "warships" : k == 1 ? "galleys" : "transports", ai_prov(w, best));
		}
	}
}

/* ------------------------------------------------ the Generals (part C) and DEFEND (D.5) */
typedef struct { short type, target; long score; } AiCand;
static int ai_cand(AiCand *cd, int nc, int type, int t, long s)       /* sorted, best 16 kept (0x548d60) */
{
	int i, j;
	if (nc == 16 && s <= cd[15].score) return nc;
	for (i = 0; i < nc && cd[i].score >= s; i++) ;
	if (nc < 16) nc++;
	for (j = nc - 1; j > i; j--) cd[j] = cd[j - 1];
	cd[i].type = (short)type; cd[i].target = (short)t; cd[i].score = s;
	return nc;
}

static unsigned short g_tst[MAX_PROV], g_tcur;                        /* provinces targeted by a plan or a candidate */

/* C.3.2 CONQUER candidates: our land, then breadth first through own, hostile and access land; the walk stops
 * two rings after the first ring with a candidate (the exe: at the first area with one; ours) */
static int ai_conquer_scan(Military *m, int c, int grp, int exp, AiCand *cd, int nc)
{
	World *w = m->w;
	static short q[MAX_PROV];
	static unsigned char dep[MAX_PROV];
	static unsigned short vs[MAX_PROV], vcur;
	int h = 0, t = 0, found = -1, p;
	if (++vcur == 0) { memset(vs, 0, sizeof vs); vcur = 1; }
	if (exp) { for (p = m->ctl_head[c]; p > 0; p = m->ctl_next[p]) if (m->lgrp[p] == grp) { vs[p] = vcur; dep[p] = 0; q[t++] = (short)p; } }   /* an expedition: from what we hold there */
	else
		for (p = m->own_head[c]; p > 0; p = m->own_next[p]) if (m->lgrp[p] == grp) { vs[p] = vcur; dep[p] = 0; q[t++] = (short)p; }
	while (h < t) {
		int a = q[h++], o;
		const Province *pv = &w->prov[a];
		unsigned long e;
		if (pv->controller >= 0 && pv->controller != c && atw(m, c, pv->controller) && g_tst[a] != g_tcur) {
			long s = ai_cq_score(m, c, a);
			if (s > 0) { nc = ai_cand(cd, nc, PL_CONQUER, a, s); g_tst[a] = g_tcur; if (found < 0) found = dep[a]; }
		}
		if ((found >= 0 && dep[a] >= found + 2) || dep[a] >= 12 || a >= 1616) continue;
		o = pv->owner;
		if (dep[a] > 0 && !(o == c || (o >= 0 && atw(m, c, o)) || (o >= 0 && (w->dipflag[o][c] & DF_ACCESS)))) continue;
		for (e = m->astart[a]; e < m->astart[a + 1]; e++) {
			int nb = (int)m->arec[2 * e];
			if (nb >= MAX_PROV || !land_prov(w, nb) || vs[nb] == vcur) continue;
			vs[nb] = vcur; dep[nb] = (unsigned char)(dep[a] + 1); q[t++] = (short)nb;
		}
	}
	return nc;
}

static int ai_accepts(const MilPlan *pl, long str)   /* 0x5510e0: enough already? */
{
	return !(pl->own > 10 && pl->own + str > pl->need * 3 / 2 && pl->own > pl->need);
}

/* A.6 / C.4.6 free armies of the General's land mass go to its attack plans, nearest target first; at peace
 * what is left goes to the garrison with the largest shortfall (D.5) */
static void ai_distribute(Military *m, int gi)
{
	World *w = m->w;
	MilGen *g = &m->gen[gi];
	int c = g->c, i, k;
	short pk[24]; int np = 0;
	for (k = m->cpl_head[c]; k >= 0 && np < 24; k = m->cpl_next[k]) if ((ai_land_type(m->plan[k].type) || m->plan[k].type == PL_GARRISON) && m->plan[k].c == c && m->plan[k].gen == gi) pk[np++] = (short)k;
	if (!np) return;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		long str, bd = 0, bdef = 0;
		int j, best = -1;
		unsigned char tried[24];
		int ntry = 0, done = 0;
		if (!ai_free_army(m, i) || m->lgrp[u->location] != g->grp || u->npath || m->astuck[i] > m->today) continue;
		str = ai_ustr(m, u);
		memset(tried, 0, sizeof tried);
		while (!done && ntry < 3) {                                       /* attack plans, nearest first */
			int bj = -1, dest;
			MilPlan *pb;
			best = -1;
			for (j = 0; j < np; j++) {
				MilPlan *pl = &m->plan[pk[j]];
				long d;
				if (tried[j] || pl->type == PL_GARRISON || pl->type == PL_FREE || !ai_accepts(pl, str)) continue;
				d = ai_sqd(w, u->location, pl->target);
				if (best < 0 || d < bd) { best = pk[j]; bd = d; bj = j; }
			}
			if (best < 0) break;
			tried[bj] = 1; ntry++;
			pb = &m->plan[best];
			dest = pb->phase == 0 && pb->gather > 0 ? pb->gather : pb->target;
			if (u->location != dest && ai_route(m, i, dest) < 0) continue;      /* the path test of 0x5510e0 */
			if (!(pb->phase == 0 && pb->gather > 0) && !pb->engaged) u->npath = 0;   /* (marches only when the plan engages) */
			m->aplan[i] = (unsigned short)(best + 1); pb->own += str;
			if (pb->next > m->today + 1) pb->next = m->today + 1;
			logm(m, "AI: %s %s #%d accepts an army (%ld men) in %s", w->country[c].tag, ai_pname(pb->type), best, unit_men(u), ai_prov(w, u->location));
			done = 1;
		}
		if (done) continue;
		if (ntry) { m->astuck[i] = m->today + 30; continue; }             /* ours: no way to any of them - not offered for 30 days */
		if (m->realwar[c]) continue;
		for (j = 0; j < np; j++) {                                        /* garrisons */
			MilPlan *pl = &m->plan[pk[j]];
			long def;
			if (pl->type != PL_GARRISON) continue;
			def = pl->power - pl->own;
			if (def > 0 && (best < 0 || def > bdef || (def == bdef && ai_sqd(w, u->location, pl->target) < bd))) { best = pk[j]; bdef = def; bd = ai_sqd(w, u->location, pl->target); }
		}
		if (best < 0) continue;
		m->aplan[i] = (unsigned short)(best + 1); m->plan[best].own += str;
		if (u->location != m->plan[best].target && ai_route(m, i, m->plan[best].target) < 0) { m->aplan[i] = 0; m->plan[best].own -= str; m->astuck[i] = m->today + 30; }
	}
}

/* D.5 garrison score (0x554140), x1000 */
static long ai_gar_score(Military *m, int c, int p, int ngen, int T)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	const long *W = aiw(m, c);
	long s = pv->fort * W[AIW_GFORT] / 2, nc, f;              /* + strategic x prov+0x140: NIEZNANE here, 0 */
	unsigned long e;
	nc = pv->continent >= 0 && pv->continent < MAX_NAMES ? m->ncont[pv->continent] : 1;
	s += nc / 2 * W[AIW_GSIZE];
	if (nc == 1) s /= 2 * ngen + 1;
	f = supply_value(m, p, c) * 100;
	if (f < 50) f = 50;
	s = s / 10 * (W[AIW_GSUPPLY] * f / 1000) / 100;
	if (T >= 0 && p < 1616) {
		int near = 0, al = world_ally_of(w, T, ALLY_MILITARY), nearal = 0;
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
			int nb = (int)m->arec[2 * e], o;
			if (!land_prov(w, nb)) continue;
			o = w->prov[nb].owner;
			if (o == T || w->prov[nb].controller == T) near = 1;
			else if (al >= 0 && o >= 0) { int x; for (x = 0; x < w->ally[al].n; x++) if (w->ally[al].member[x] == o) nearal = 1; }
		}
		if (near) s += W[AIW_GWAR] * 200;
		else if (nearal) s += W[AIW_GWAR] * 50;
	}
	if (pv->religion >= 0 && pv->religion != w->country[c].religion) s *= 2;   /* (1 + 0x481a30) - the unrest term, NIEPEWNE: 1 for another religion */
	return s;
}

/* D.5 DEFEND (0x5512f0 / 0x551500): at peace, the threat of the neighbours on our land mass makes the
 * garrisons' power; the best provinces by the garrison weights get garrisons. Ours: at most AI_GARR. */
#define AI_GARR 8
static void ai_defend(Military *m, int gi)
{
	World *w = m->w;
	MilGen *g = &m->gen[gi];
	int c = g->c, k, p, n = 0, ngen = 0, T, nb = 0, ng = 0;
	long threat = 0, power, sum = 0, cut;
	static unsigned short os[MAX_COUNTRY], ocur;
	short gp[AI_GARR]; long gs[AI_GARR];
	for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) if (m->plan[k].type == PL_GARRISON && m->plan[k].c == c && m->plan[k].gen == gi) ai_end_plan(m, k, "");
	g->dnext = m->today + 250 + mrand(m) % 500;
	if (m->realwar[c]) return;                                  /* at war DEFEND only reschedules */
	for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) if (m->gen[k].c == c) ngen++;
	if (++ocur == 0) { memset(os, 0, sizeof os); ocur = 1; }
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {           /* our provinces here, the neighbours that own land next to them */
		unsigned long e;
		if (m->lgrp[p] != g->grp) continue;
		n++;
		if (p >= 1616) continue;
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
			int q = (int)m->arec[2 * e], o, i;
			long t = 0, a = 0;
			if (!land_prov(w, q) || (o = w->prov[q].owner) < 0 || o == c || os[o] == ocur) continue;
			os[o] = ocur; nb++;
			if (world_overlord(w, o) == c || world_overlord(w, c) == o) continue;   /* vassal relations either way */
			for (i = m->arm_head[o]; i >= 0; i = m->arm_next[i]) if (!w->land[i].dead) t += ai_ustr(m, &w->land[i]);
			if (world_allied(w, c, o, ALLY_MILITARY)) { a = t; t /= 10; }
			if (world_allied(w, c, o, ALLY_DYNASTIC)) t /= 2;
			if (w->country[o].religion != w->country[c].religion) t = t * 3 / 4;
			if (w->dipflag[o][c] & DF_CB_PERM) t = t * 3 / 2;
			threat += t - a / 2;
		}
	}
	if (!n) return;
	threat /= 10;
	if (!nb) threat /= 10;                                     /* the theatre is all ours */
	if (threat < 0) threat = 0;
	power = (threat < 5000 ? threat : 5000) * 1000 / max_morale(m, c);   /* (the cap by income x the King's share +0x80: NIEPEWNE, not applied) */
	if (power < 1) power = 1;
	T = ai_query(m, c, 1, -1);
	cut = (long)n * 500;                                       /* score > provinces / 2 */
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		long s;
		int i, j;
		if (m->lgrp[p] != g->grp || w->prov[p].controller != c) continue;
		s = ai_gar_score(m, c, p, ngen, T);
		if (s <= cut) continue;
		sum += s / 1000;
		for (i = 0; i < ng && gs[i] >= s; i++) ;
		if (i >= AI_GARR) continue;
		if (ng < AI_GARR) ng++;
		for (j = ng - 1; j > i; j--) { gs[j] = gs[j - 1]; gp[j] = gp[j - 1]; }
		gs[i] = s; gp[i] = (short)p;
	}
	if (!ng) { int cp = w->country[c].capital; if (cp > 0 && cp < MAX_PROV && m->lgrp[cp] == g->grp && w->prov[cp].controller == c) { gp[0] = (short)cp; gs[0] = 1000; sum = 1; ng = 1; } }
	if (sum < 1) sum = 1;
	for (k = 0; k < ng; k++) {
		int pk = ai_new_plan(m, gi, PL_GARRISON, gp[k], gs[k]);
		long pw, lim = supply_value(m, gp[k], c) * 1000;
		if (pk < 0) break;
		pw = power / 10 * (gs[k] / 1000) / sum * 10;
		if (pw > lim) pw = lim;
		m->plan[pk].power = pw < 1 ? 1 : pw;
		m->plan[pk].next = m->today + 10 + mrand(m) % 30;
		logm(m, "AI: %s starts a garrison in %s with %ld power", w->country[c].tag, ai_prov(w, gp[k]), m->plan[pk].power);
	}
	ai_distribute(m, gi);
}

/* D.5 GARRISON think (0x5544b0): at war it dissolves; at peace its armies go back to it */
static void ai_garrison_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, i;
	if (m->realwar[c]) { ai_end_plan(m, k, "dissolved (war)"); return; }
	pl->own = ai_plan_own(m, k);
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		if (m->aplan[i] != k + 1 || u->dead || u->npath || u->location == pl->target || m->inbat[u->location]) continue;
		if (ai_route(m, i, pl->target) < 0) { m->aplan[i] = 0; m->astuck[i] = m->today + 30; }
	}
	pl->next = m->today + 60 + mrand(m) % 120;
}

/* ------------------------------------------------ fleets: the Admirals and the naval plans (D.2-D.4) */
/* fleet strength (ours, as ai_ustr on land): (fire + shock) of the fighting ships at the owner's NAVAL.csv level x morale,
 * x1000 (transports have none) */
static long ai_fstr(const Military *m, const NavalUnit *f)
{
	const NavLev *N = nlev_of(m, f->owner);
	long mo = f->morale_m ? f->morale_m : nmax_morale(m, f->owner);
	return ((long)f->warships * (N->wf + N->ws) + (long)f->galleys * (N->gf + N->gs)) / 10 * (mo / 10) / 10;
}
static int ai_still(const NavalUnit *f) { return f->pathpos >= f->npath; }
static long ai_fmor(const Military *m, const NavalUnit *f) { return f->morale_m ? f->morale_m : nmax_morale(m, f->owner); }   /* 0 = not yet set: the maximum */
/* fleets hostile to c in p (a sea zone or a port): strength; n = their warships, galleys, transports; still = only those not under way */
static long ai_hfleets(const Military *m, int p, int c, int still, long *n)
{
	long s = 0;
	int i;
	if (n) n[0] = n[1] = n[2] = 0;
	if (p <= 0 || p >= MAX_PROV) return 0;
	for (i = m->fz_head[p]; i >= 0; i = m->fz_next[i]) {
		const NavalUnit *f = &m->w->naval[i];
		if (f->location != p || fleet_ships(f) <= 0 || !atw(m, f->owner, c) || (still && !ai_still(f))) continue;
		s += ai_fstr(m, f);
		if (n) { n[0] += f->warships; n[1] += f->galleys; n[2] += f->transports; }
	}
	return s;
}
/* 0x54bdb0 for a sea zone: no fortress, no terrain - 1.5 x (hostile fleets there + 0.25 x those in the neighbouring zones) */
static long ai_nav_need(const Military *m, int c, int z)
{
	long a = ai_hfleets(m, z, c, 0, NULL), b = 0;
	unsigned long k;
	if (z < 1616) for (k = m->astart[z]; k < m->astart[z + 1]; k++) { int q = (int)m->arec[2 * k]; if (sea_prov(m->w, q)) b += ai_hfleets(m, q, c, 0, NULL); }
	return (a + b / 4) * 3 / 2;
}
/* D.4 BLOCKADE value of zone z (0x54d9f0): the ports onto it a country hostile to c controls - value (0x47d970, ours: the
 * province income) + hostile ships in them + 30 if besieged */
static long ai_blk_v(const Military *m, int c, int z)
{
	long v = 0, n[3];
	int p;
	for (p = m->zp_head[z]; p > 0; p = m->zp_next[p]) {
		const Province *pv = &m->w->prov[p];
		if (pv->controller < 0 || pv->controller == c || !atw(m, c, pv->controller)) continue;
		ai_hfleets(m, p, c, 0, n);
		v += pv->income_m + 1000L * (n[0] + n[1] + n[2]) + (m->siege[p].besieger ? 30000L : 0);
	}
	return v;
}
static long ai_blk_need(const Military *m, int c, int z)   /* 0x54db50: 10 x (two naval tech values, NIEPEWNE: warship fire + shock) + 0.8 x the ports' fleets */
{
	const NavLev *N = nlev_of(m, c);
	long s = 10L * (N->wf + N->ws);
	int p;
	for (p = m->zp_head[z]; p > 0; p = m->zp_next[p]) {
		int k = m->w->prov[p].controller;
		if (k >= 0 && k != c && atw(m, c, k)) s += ai_hfleets(m, p, c, 0, NULL) * 4 / 5;
	}
	return s;
}
/* D.4 the three evaluations, x1000, -1 = not a target */
static long ai_nav_score(const Military *m, int c, int z, int type)
{
	long n[3], v = 0;
	int p;
	if (!sea_prov(m->w, z) || z >= 1616) return -1;
	if (type == PL_NAVALATTACK) {                           /* a stationary hostile fleet: 40 + 5 n0 + 4 n1 + n2 */
		ai_hfleets(m, z, c, 1, n);
		if (n[0] + n[1] + n[2] <= 0) return -1;
		return 40000 + 1000L * (5 * n[0] + 4 * n[1] + n[2]);
	}
	if (m->zp_head[z] <= 0) return -1;                      /* +0x488: the zone touches ports */
	if (type == PL_BLOCKADE) { v = ai_blk_v(m, c, z); return v >= 100 ? 15000 + v : -1; }
	ai_hfleets(m, z, c, 1, n);                              /* BREAKBLOCKADE: ours - a port is "blockaded" while a hostile fleet stands in its zone (NIEPEWNE) */
	if (n[0] + n[1] + n[2] <= 0) return -1;
	for (p = m->zp_head[z]; p > 0; p = m->zp_next[p]) {
		int i;
		if (m->w->prov[p].controller != c) continue;
		v += m->w->prov[p].income_m;
		for (i = m->fz_head[p]; i >= 0; i = m->fz_next[i]) if (m->w->naval[i].owner == c && m->w->naval[i].location == p) v += 1000L * fleet_ships(&m->w->naval[i]);
	}
	return v >= 100 ? 25000 + v : -1;
}

/* breadth first over the sea zones from the sources to the first zone marked as a goal (ours: steps, not days -
 * a sea step is 10 days everywhere); s_par leads back, s_q[0..s_n) are the zones reached */
#define AI_SAIL_MAX 40
static short s_q[MAX_PROV], s_par[MAX_PROV];
static unsigned short s_st[MAX_PROV], s_cur, s_goal[MAX_PROV], s_gcur;
static int s_n;
static void ai_goals_clear(void) { if (++s_gcur == 0) { memset(s_goal, 0, sizeof s_goal); s_gcur = 1; } }
static int s_bc = -1, s_bx;                /* the searching country (-1 any) and its fleet's leader type (SPEC_EXPLORERS 3.1) */
static int ai_sea_bfs(const Military *m, int nsrc, const short *src, int maxdep)
{
	const World *w = m->w;
	static unsigned char dep[MAX_PROV];
	int h = 0, i;
	if (++s_cur == 0) { memset(s_st, 0, sizeof s_st); s_cur = 1; }
	s_n = 0;
	for (i = 0; i < nsrc; i++) {
		int z = src[i];
		if (!sea_prov(w, z) || z >= 1616 || s_st[z] == s_cur) continue;
		s_st[z] = s_cur; s_par[z] = -1; dep[z] = 0; s_q[s_n++] = (short)z;
	}
	while (h < s_n) {
		int z = s_q[h++];
		unsigned k;
		if (s_goal[z] == s_gcur) return z;
		if (dep[z] >= maxdep) continue;
		for (k = m->sst[z]; k < m->sst[z + 1]; k++) {
			int q = m->sadj[k];
			if (s_st[q] == s_cur) continue;
			if (s_bc >= 0 && !may_enter(w, s_bc, q, 1, s_bx)) continue;   /* unknown water: explorers / naval tech 27 only (NIEPEWNE: the spec agent read "never through") */
			s_st[q] = s_cur; s_par[q] = (short)z; dep[q] = (unsigned char)(dep[z] + 1); s_q[s_n++] = (short)q;
		}
	}
	return -1;
}
/* the sea zones next to land province p (and its port's zone); count */
static int ai_zones_of(const Military *m, int p, short *out, int max)
{
	int n = 0;
	unsigned long k;
	if (port_prov(m->w, p)) out[n++] = m->w->prov[p].port_sea;
	if (p < 1616)
		for (k = m->astart[p]; k < m->astart[p + 1] && n < max; k++) {
			int q = (int)m->arec[2 * k];
			if (sea_prov(m->w, q) && !(n && q == out[0])) out[n++] = (short)q;
		}
	return n;
}
/* the AI's sail order (ours, cheaper than mil_order_fleet's full Dijkstra): to a sea zone or a port; steps or -1 */
static int ai_sail(Military *m, int fi, int dest)
{
	World *w = m->w;
	NavalUnit *f = &w->naval[fi];
	short src, rev[48];
	int gz, z, steps = 0, i;
	if (f->location == dest || w->tutorial) { f->npath = f->pathpos = 0; return 0; }   /* (0x55a038 for fleets too) */
	src = (short)(port_prov(w, f->location) ? w->prov[f->location].port_sea : f->location);
	gz = port_prov(w, dest) ? w->prov[dest].port_sea : dest;
	if (!sea_prov(w, src) || !sea_prov(w, gz)) return -1;
	ai_goals_clear(); s_goal[gz] = s_gcur;
	s_bc = f->owner == w->pir || f->owner == w->reb ? -1 : f->owner; s_bx = f->l_id ? f->l_type : 0;
	if (ai_sea_bfs(m, 1, &src, AI_SAIL_MAX) < 0) { s_bc = -1; return -1; }
	s_bc = -1;
	if (dest != gz) rev[steps++] = (short)dest;
	for (z = gz; z >= 0 && steps < 48; z = s_par[z]) if (z != f->location) rev[steps++] = (short)z;
	if (steps == 0 || steps > 40) return -1;
	for (i = 0; i < steps; i++) f->path[i] = rev[steps - 1 - i];
	f->npath = (short)steps; f->pathpos = 0;
	f->arrive_day = m->today + (port_prov(w, f->location) || port_prov(w, f->path[0]) ? PORT_DAYS : SEA_DAYS);
	return steps;
}

/* D.2 home port (0x54ab60): own, controlled ports of the General's land mass - value + 2 x fort - 10 on a border + 10 with a
 * shipyard - 10 with a hostile unit there */
static int ai_home_port(Military *m, int gi)
{
	const World *w = m->w;
	int c = m->gen[gi].c, grp = m->gen[gi].grp, p, best = 0;
	long bs = 0;
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		const Province *pv = &w->prov[p];
		long s;
		unsigned long k;
		if (m->lgrp[p] != grp || pv->controller != c || !port_prov(w, p)) continue;
		s = pv->income_m + 2000L * pv->fort + ((pv->bflags & PB_SHIPYARD) ? 10000 : 0);
		if (p < 1616)
			for (k = m->astart[p]; k < m->astart[p + 1]; k++) { int q = (int)m->arec[2 * k]; if (land_prov(w, q) && w->prov[q].owner != c) { s -= 10000; break; } }
		if (ai_hostile(m, p, c, NULL, NULL) > 0 || ai_hfleets(m, p, c, 0, NULL) > 0) s -= 10000;
		if (!best || s > bs) { best = p; bs = s; }
	}
	return best;
}

/* which Admiral a free fleet belongs to (ours: the one with the nearest home port) */
static int ai_fleet_adm(const Military *m, int c, int fi)
{
	int k, best = -1;
	long bd = 0;
	for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) {
		const MilGen *g = &m->gen[k];
		long d;
		if (g->c != c || g->exp || g->hport <= 0) continue;
		d = ai_sqd(m->w, m->w->naval[fi].location, g->hport);
		if (best < 0 || d < bd) { best = k; bd = d; }
	}
	return best;
}
static int ai_free_fleet0(const Military *m, int i)
{
	const NavalUnit *f = &m->w->naval[i];
	return !m->fplan[i] && fleet_ships(f) > 0 && ai_still(f) && !m->fload[i] && f->location > 0 && f->location < MAX_PROV && !m->inbat[f->location];
}
static int ai_free_fleet(const Military *m, int i)
{
	const NavalUnit *f = &m->w->naval[i];
	return ai_free_fleet0(m, i) && !(f->l_id && (f->l_type == 2 || f->l_type == 3));
}

/* D.2 0x54aa10: the Admiral's free fleets - in the home port (strength > 0.1) offered to its naval plans nearest first,
 * elsewhere sent to the home port. Ours: a fleet below 0.9 of its maximum morale is not offered (it rests). */
static void ai_fleets_out(Military *m, int gi)
{
	World *w = m->w;
	MilGen *g = &m->gen[gi];
	int c = g->c, i, k, hp = g->hport;
	for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) {
		NavalUnit *f = &w->naval[i];
		long str, bd = 0;
		int best = -1;
		if (!ai_free_fleet(m, i) || ai_fleet_adm(m, c, i) != gi) continue;
		if (f->location != hp) { if (ai_sail(m, i, hp) > 0) logm(m, "AI: %s fleet %s returns to %s", w->country[c].tag, f->name ? f->name : "?", ai_prov(w, hp)); continue; }
		str = ai_fstr(m, f);
		if (str <= 100 || ai_fmor(m, f) * 10 < nmax_morale(m, c) * 9) continue;
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) {
			const MilPlan *pl = &m->plan[k];
			long d;
			if (pl->c != c || pl->gen != gi || !ai_naval_type(pl->type) || !ai_accepts(pl, str)) continue;
			d = ai_sqd(w, hp, pl->target);
			if (best < 0 || d < bd) { best = k; bd = d; }
		}
		if (best < 0) continue;
		m->fplan[i] = (unsigned short)(best + 1); m->plan[best].own += str;
		if (m->plan[best].engaged && ai_sail(m, i, m->plan[best].target) < 0) { m->fplan[i] = 0; m->plan[best].own -= str; continue; }
		if (m->plan[best].next > m->today + 1) m->plan[best].next = m->today + 1;
		logm(m, "AI: %s %s #%d accepts fleet %s (%d ships)", w->country[c].tag, ai_pname(m->plan[best].type), best, f->name ? f->name : "?", fleet_ships(f));
	}
}

/* the Admiral's think (0x54a640 / war scans 0x54a920, 0x54a950, BREAKBLOCKADE 0x54aaa0). Ours: one Admiral per General with an
 * own port on its land mass (the exe: one per sea area); its theatre = the sea zones within AI_ADM_R steps of the home port
 * (the exe's sea area, NIEPEWNE); the scans are polled on its think while at war (the exe: on war / blockade events). */
#define AI_ADM_R 5
static void ai_admiral(Military *m, int gi)
{
	World *w = m->w;
	MilGen *g = &m->gen[gi];
	AiCand cd[17];
	const short *th = NULL;
	int c = g->c, cap = w->country[c].capital, k, i = -1, nz = 0, nc = 0, limit, active = 0, worst = -1, hp;
	g->anext = m->today + (cap > 0 && cap < MAX_PROV && m->lgrp[cap] == g->grp ? 3 + mrand(m) % 10 : 7 + mrand(m) % 14);
	hp = g->hport;                                              /* ours (CPU): kept while it stays a good own port, re-chosen 1 think in 8 (the exe: on province events) */
	if (hp <= 0 || w->prov[hp].owner != c || w->prov[hp].controller != c || m->lgrp[hp] != g->grp || mrand(m) % 8 == 0 || ai_hostile(m, hp, c, NULL, NULL) > 0 || ai_hfleets(m, hp, c, 0, NULL) > 0) {
		hp = ai_home_port(m, gi);
		if (hp != g->hport && hp > 0) logm(m, "AI: %s Admiral of land mass %d: home port %s", w->country[c].tag, g->grp, ai_prov(w, hp));
	}
	g->hport = (short)hp;
	if (hp <= 0) return;
	if (m->realwar[c] && m->navw[c] + m->navg[c] > 0) {
		for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i])        /* ours (CPU): the war scans only when this Admiral has a free fighting fleet to give */
			if (ai_free_fleet(m, i) && w->naval[i].warships + w->naval[i].galleys > 0 && ai_fleet_adm(m, c, i) == gi) break;
	}
	if (m->realwar[c] && m->navw[c] + m->navg[c] > 0 && i >= 0) {
		int z0 = w->prov[hp].port_sea;
		if (m->thoff[z0]) { th = m->thpool + m->thoff[z0] - 1; nz = m->thlen[z0]; }   /* the theatre of that home zone, kept (the map is static) */
		else {
			short src = (short)z0;
			ai_goals_clear();
			ai_sea_bfs(m, 1, &src, AI_ADM_R);
			th = s_q; nz = s_n;
			if (m->thpool && m->thused + s_n <= 16384) { memcpy(m->thpool + m->thused, s_q, (size_t)s_n * sizeof(short)); m->thoff[z0] = (short)(m->thused + 1); m->thlen[z0] = (short)s_n; m->thused += s_n; }
		}
		limit = 4 + nz / 3;                                     /* 0x548800 over the theatre; <= 0.25 x the fighting ships (ours for +0xec4) */
		if (limit > (m->navw[c] + m->navg[c]) / 4) limit = (m->navw[c] + m->navg[c]) / 4;
		if (limit < 4) limit = 4;
		if (limit > 16) limit = 16;
		if (++g_tcur == 0) { memset(g_tst, 0, sizeof g_tst); g_tcur = 1; }
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) {
			const MilPlan *pl = &m->plan[k];
			if (pl->c != c || !ai_naval_type(pl->type)) continue;
			g_tst[pl->target] = g_tcur;
			if (pl->gen != gi) continue;
			active++;
			if (worst < 0 || pl->score < m->plan[worst].score) worst = k;
		}
		for (i = 0; i < nz; i++) {
			int z = th[i], t, bt = 0;
			long s, bs = -1;
			if (g_tst[z] == g_tcur || (m->fz_head[z] < 0 && m->zp_head[z] <= 0)) continue;   /* (no fleet, no port: nothing to evaluate) */
			for (t = 0; t < 3; t++) {
				int ty = t == 0 ? PL_NAVALATTACK : t == 1 ? PL_BLOCKADE : PL_BREAKBLOCKADE;
				if (ty != PL_BLOCKADE ? m->fz_head[z] < 0 : m->zp_head[z] <= 0) continue;
				if ((s = ai_nav_score(m, c, z, ty)) > bs) { bs = s; bt = ty; }
			}
			if (bs > 0) { nc = ai_cand(cd, nc, bt, z, bs); g_tst[z] = g_tcur; }
		}
		for (k = 0; k < nc; k++) {                               /* commit (D.1 0x548df0) */
			int pk = -1;
			if (active < limit) { if ((pk = ai_new_plan(m, gi, cd[k].type, cd[k].target, cd[k].score)) >= 0) active++; }
			else {
				if (worst >= 0 && cd[k].score > m->plan[worst].score) { ai_end_plan(m, worst, "replaced"); pk = ai_new_plan(m, gi, cd[k].type, cd[k].target, cd[k].score); }
				k = nc;
			}
			if (pk >= 0) { m->plan[pk].phase = 1; m->plan[pk].gather = (short)hp; }   /* naval plans skip the phases (0x54c450); they gather in the home port (0x54d840) */
		}
	}
	ai_fleets_out(m, gi);
}

/* 0x54c350 at sea: our side below 0.66 of theirs in ships and in morale - the AI's fleets break off (never the player's) */
static int ai_nretreats(Military *m, int k)
{
	World *w = m->w;
	int c = m->plan[k].c, i, j, kb;
	for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) {
		const NavalUnit *f = &w->naval[i];
		Battle *b = NULL;
		long sh[2], fi, so, mo[2];
		int cc, side, ok = 1;
		if (m->fplan[i] != k + 1 || fleet_ships(f) <= 0 || !sea_prov(w, f->location) || !m->inbat[f->location]) continue;
		for (kb = 0; kb < MAXB; kb++) if (m->b[kb].active && m->b[kb].prov == f->location) { b = &m->b[kb]; break; }
		if (!b) continue;
		side = side_of(m, b, c);
		nside_sum(m, b, side, &sh[0], &fi, &so, &mo[0], &cc);
		nside_sum(m, b, 1 - side, &sh[1], &fi, &so, &mo[1], &cc);
		if (!(sh[0] * 100 < 66 * sh[1] && mo[0] * 100 < 66 * mo[1])) continue;
		for (j = 0; j < w->nnaval && ok; j++) {
			const NavalUnit *v = &w->naval[j];
			if (v->location == b->prov && fleet_ships(v) > 0 && v->owner >= 0 && side_of(m, b, v->owner) == side && w->country[v->owner].human) ok = 0;
		}
		if (!ok) continue;
		logm(m, "AI: %s fleets break off the battle in %s (ships %ld vs %ld)", w->country[c].tag, ai_prov(w, b->prov), sh[0], sh[1]);
		nretreat_side(m, b, side);
		return 1;
	}
	return 0;
}

/* NAVALATTACK / BLOCKADE / BREAKBLOCKADE think (0x54c920, every 2 + 3 days) */
static void ai_naval_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, z = pl->target, i, done;
	long own = 0, mx = nmax_morale(m, c), low = 0;
	pl->next = m->today + 2 + mrand(m) % 3;
	for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) {
		const NavalUnit *f = &w->naval[i];
		if (m->fplan[i] != k + 1) continue;
		if (f->owner != c || fleet_ships(f) <= 0) { m->fplan[i] = 0; continue; }
		own += ai_fstr(m, f);
		if (ai_fmor(m, f) * 10 < mx * 8) low = 1;
	}
	pl->own = own;
	if (ai_nretreats(m, k)) return;
	if (pl->type == PL_BLOCKADE) done = low || ai_blk_v(m, c, z) < 100;             /* 0x54dc00 (0.8: NIEPEWNE morale) */
	else done = ai_hfleets(m, z, c, 0, NULL) <= 0;                                   /* 0x54c200: no hostile fleet left */
	if (done) { ai_end_plan(m, k, "done"); return; }
	if (pl->gen >= 0 && pl->gen < AIG_MAX && m->gen[pl->gen].c == c && m->gen[pl->gen].hport > 0) pl->gather = m->gen[pl->gen].hport;
	pl->need = pl->type == PL_BLOCKADE ? ai_blk_need(m, c, z) : ai_nav_need(m, c, z);
	if (pl->engaged && pl->own * 4 < pl->need) pl->engaged = 0;                     /* ours: the attacking force is gone - gather again */
	if (!pl->engaged && pl->own >= pl->need && pl->own > ai_hfleets(m, z, c, 0, NULL)) {   /* ready (0x54c0a0): engage (0x54c140) */
		pl->engaged = 1;
		logm(m, "AI: %s %s #%d (%s) engages in battle!", w->country[c].tag, ai_pname(pl->type), k, ai_prov(w, z));
	}
	if (pl->engaged)
		for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) {
			const NavalUnit *f = &w->naval[i];
			if (m->fplan[i] != k + 1 || !ai_still(f) || f->location == z || m->inbat[f->location]) continue;
			if (ai_sail(m, i, z) < 0) m->fplan[i] = 0;                                  /* a fleet whose order fails goes back to the Admiral */
		}
	else if (!pl->own) pl->next = m->today + 7 + mrand(m) % 7;                     /* ours (CPU): a plan without fleets looks again in a week (a fleet given to it wakes it) */
}

/* ------------------------------------------------ TRANSPORT (D.7) and INVASION (D.6) */
#define AI_TR_WAIT 391                 /* 0x555f70: no fleet a year + a month + a day after the start - cancelled (NIEPEWNE) */
enum { TR_WALK = 1, TR_WAIT, TR_SAIL };
static const char *s_why = "";            /* why the last ai_send failed (log) */

/* D.7 0x55a320: send army ui to dest for plan par - by land when it can, else with a TRANSPORT. Ours where the exe's
 * path object is not ported: the embarkation province = the army's own when coastal, else the nearest coastal province of
 * its land mass that c or a friend controls; the landing province = dest when coastal, else the nearest coastal province of
 * dest's land mass c may enter (controlled by c or a friend first); the sea leg = breadth first between their zones.
 * 1 = on its way. */
static int ai_send(Military *m, int ui, int dest, int par)
{
	World *w = m->w;
	LandUnit *u = &w->land[ui];
	int c = u->owner, here = u->location, E = -1, L = -1, p, k, i, ns, gz, ez;
	long wt = mil_army_weight(u), capa = 0, bd = 0;
	short src[16], zz[16];
	MilPlan *pl;
	if (here == dest) { m->aplan[ui] = (unsigned short)(par + 1); return 1; }
	if (!land_prov(w, here) || !land_prov(w, dest) || m->lgrp[here] < 0 || m->lgrp[dest] < 0) { s_why = "no land"; return 0; }
	if (m->lgrp[here] == m->lgrp[dest]) { if (ai_route(m, ui, dest) < 0) { s_why = "no way by land"; return 0; } m->aplan[ui] = (unsigned short)(par + 1); return 1; }
	for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) capa += mil_fleet_capacity(&w->naval[i]);
	if (wt > capa || !m->gcoast[m->lgrp[dest]] || !m->gcoast[m->lgrp[here]]) { s_why = wt > capa ? "too few ships" : "no coast"; return 0; }   /* 0x5551f0: more than the country's ships carry */
	if (m->coast[dest]) L = dest;
	else
		for (p = m->gp_head[m->lgrp[dest]]; p > 0; p = m->gp_next[p]) {
			long d;
			if (!m->coast[p] || !ai_enter(m, c, p)) continue;
			d = ai_sqd(w, p, dest) + (ai_ctl_ok(m, c, p) ? 0 : 4000000L);
			if (L < 0 || d < bd) { L = p; bd = d; }
		}
	if (m->coast[here]) E = here;
	else
		for (p = m->gp_head[m->lgrp[here]]; p > 0; p = m->gp_next[p]) {
			long d;
			if (!m->coast[p] || !ai_ctl_ok(m, c, p)) continue;
			d = ai_sqd(w, p, here);
			if (E < 0 || d < bd) { E = p; bd = d; }
		}
	if (L < 0 || E < 0) { s_why = "no landing or embarkation province"; return 0; }
	ai_goals_clear();
	ns = ai_zones_of(m, L, zz, 16);
	for (i = 0; i < ns; i++) s_goal[zz[i]] = s_gcur;
	ns = ai_zones_of(m, E, src, 16);
	s_bc = c; s_bx = 0;
	gz = ai_sea_bfs(m, ns, src, AI_SAIL_MAX);
	s_bc = -1;
	if (gz < 0) { s_why = "no way by sea"; return 0; }     /* empty path: -1 */
	for (ez = gz; s_par[ez] >= 0; ez = s_par[ez]) ;
	s_newc = m->plan[par].c;
	if ((k = ai_new_plan(m, m->plan[par].gen, PL_TRANSPORT, dest, 0)) < 0) { s_why = "no plan slot"; return 0; }
	pl = &m->plan[k];
	pl->army = (short)ui; pl->gather = (short)E; pl->ezone = (short)ez; pl->lzone = (short)gz; pl->land = (short)L; pl->parent = (short)par;
	pl->t0 = m->today; pl->phase = TR_WALK; pl->next = m->today + 1;
	m->aplan[ui] = (unsigned short)(k + 1);
	if (here != E && ai_route(m, ui, E) < 0) { ai_end_plan(m, k, "cancelled (no way to the coast)"); s_why = "no way to the coast"; return 0; }
	logm(m, "AI: AITransport #%d: Army #%d (%s) starts transporting to %s (embarks in %s, lands in %s).", k, ui, w->country[c].tag, ai_prov(w, dest), ai_prov(w, E), ai_prov(w, L));
	return 1;
}

/* TRANSPORT think (ours: polled daily - the exe reacts to the arrival events 0x555a70 / 0x555b30; its own think 15 + 25 days only
 * runs the time-outs): walk to the coast, find a fleet (the King is asked for transports meanwhile), load, sail, unload */
static void ai_transport_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, ui = pl->army, fi = pl->fleet, i;
	LandUnit *u;
	pl->next = m->today + 1;
	if (ui < 0 || ui >= w->nland || w->land[ui].owner != c || w->land[ui].dead == 1 || m->aplan[ui] != k + 1) { ai_end_plan(m, k, "cancelled (the army is gone)"); return; }
	u = &w->land[ui];
	if (fi >= 0 && (fi >= w->nnaval || w->naval[fi].owner != c || fleet_ships(&w->naval[fi]) <= 0 || m->fplan[fi] != k + 1)) { pl->fleet = -1; fi = -1; }
	if (pl->phase == TR_SAIL) {
		NavalUnit *f;
		if (u->dead != LAND_ABOARD || fi < 0) { ai_end_plan(m, k, "cancelled (landed elsewhere)"); return; }
		f = &w->naval[fi];
		if (!ai_still(f) || m->inbat[f->location]) return;
		if (f->location != pl->lzone) { if (ai_sail(m, fi, pl->lzone) < 0) ai_end_plan(m, k, "cancelled (no way to the unload point)"); return; }
		if (mil_disembark(m, fi, pl->land) <= 0) { ai_end_plan(m, k, "cancelled (cannot unload)"); return; }
		logm(m, "AI: AITransport #%d of %s: Starts unloading to %s.", k, w->country[c].tag, ai_prov(w, pl->land));
		{	int par = pl->parent, dest = pl->target;
			ai_end_plan(m, k, "done");
			if (par >= 0 && m->plan[par].type != PL_FREE && m->plan[par].c == c) {
				m->aplan[ui] = (unsigned short)(par + 1);
				if (u->location != dest) ai_route(m, ui, dest);
			}
		}
		return;
	}
	if (u->dead == LAND_ABOARD) { ai_end_plan(m, k, "cancelled (aboard)"); return; }
	if (pl->phase == TR_WALK) {
		if (u->location == pl->gather && !u->npath) { pl->phase = TR_WAIT; logm(m, "AI: AITransport #%d of %s: Army #%d has arrived at %s!", k, w->country[c].tag, ui, ai_prov(w, u->location)); }
		else if (!u->npath && !m->inbat[u->location] && ai_route(m, ui, pl->gather) < 0) { ai_end_plan(m, k, "cancelled (no way to the coast)"); return; }
	}
	if (fi < 0) {                                                       /* a free fleet that carries the army, nearest first */
		long wt = mil_army_weight(u), bd = 0;
		if (m->today > pl->t0 + AI_TR_WAIT) { ai_end_plan(m, k, "cancelled (no fleet)"); return; }
		for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i]) {      /* ours: also one that only waits for a naval plan (not engaged), free ones first */
			const NavalUnit *f = &w->naval[i];
			long d;
			int busy = 0;
			if (!ai_free_fleet(m, i)) {
				int pk = (int)m->fplan[i] - 1;
				if (pk < 0 || !ai_naval_type(m->plan[pk].type) || m->plan[pk].engaged || !ai_still(f) || m->fload[i] || m->inbat[f->location]) continue;
				busy = 1;
			}
			if (mil_fleet_capacity(f) < wt) continue;
			d = ai_sqd(w, f->location, pl->gather) / 4 + (busy ? 0x10000000L : 0);
			if (fi < 0 || d < bd) { fi = i; bd = d; }
		}
		if (fi < 0) return;
		if (m->fplan[fi]) m->plan[m->fplan[fi] - 1].own -= ai_fstr(m, &w->naval[fi]);
		pl->fleet = (short)fi; m->fplan[fi] = (unsigned short)(k + 1);
		logm(m, "AI: AITransport #%d of %s: fleet %s comes to %s.", k, w->country[c].tag, w->naval[fi].name ? w->naval[fi].name : "?", ai_prov(w, pl->ezone));
	}
	{	NavalUnit *f = &w->naval[fi];
		if (!ai_still(f) || m->inbat[f->location]) return;
		if (f->location != pl->ezone) { if (ai_sail(m, fi, pl->ezone) < 0) { m->fplan[fi] = 0; pl->fleet = -1; } return; }
		if (pl->phase != TR_WAIT) return;
		if (mil_embark(m, ui, fi) != MIL_RC_OK) { m->fplan[fi] = 0; pl->fleet = -1; return; }
		logm(m, "AI: AITransport #%d of %s: Loads Army #%d.", k, w->country[c].tag, ui);
		if (ai_sail(m, fi, pl->lzone) < 0) { mil_disembark(m, fi, pl->gather); ai_end_plan(m, k, "cancelled (no way to the unload point)"); return; }
		pl->phase = TR_SAIL;
		logm(m, "AI: AITransport #%d of %s: Starts moving to the Unload Point (%s).", k, w->country[c].tag, ai_prov(w, pl->lzone));
	}
}

/* D.6 INVASION score (0x554560), x1000; -1 = not a target */
static long ai_inv_score(const Military *m, int c, int p)
{
	const World *w = m->w;
	const Province *pv = &w->prov[p];
	long s, sup;
	int cap = w->country[c].capital;
	unsigned long e;
	if (!m->coast[p] || pv->controller < 0 || pv->controller == c || !atw(m, c, pv->controller) || !ai_enter(m, c, p)) return -1;   /* (hostile, enterable: ours - else done at once, 0x554840) */
	s = pv->income_m / 12 + 5000L * (6 - pv->fort) + (ai_city(pv) ? 5000 : 0) + (core_of(pv, c) ? 10000 : 0);
	if (ai_borders_own(m, c, p)) s = 2 * (s + 25000);
	if (p < 1616)
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) { int q = (int)m->arec[2 * e]; if (land_prov(w, q) && ai_borders_own(m, c, q)) { s += 100000; break; } }
	sup = supply_value(m, p, c);
	if (sup < 5) s = s * 95 / 100;
	if (sup < 10) s = s * 95 / 100;
	if (sup < 20) s = s * 95 / 100;
	if (sup < 30) s = s * 95 / 100;
	if (ai_query(m, c, 0, 1) != 2 && cap > 0 && cap < w->nprov) s -= 1000L * ai_dist(w, cap, p);   /* (+ 2.5 x prov+0x140: NIEZNANE, 0) */
	if (s < -30000) s = -30000;
	return s + 35000;
}

/* INVASION think (attack family, 2 + 3 days; always phase 1 - its armies go as they are offered, 0x5547a0) */
static void ai_inv_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, t = pl->target;
	const Province *pv = &w->prov[t];
	long deficit;
	pl->own = ai_plan_own(m, k);
	pl->phase = 1; pl->engaged = 1;
	if (pv->controller < 0 || pv->controller == c || !atw(m, c, pv->controller) || !ai_enter(m, c, t)) { ai_end_plan(m, k, "done"); return; }   /* 0x554840 */
	if (ai_retreats(m, k)) { pl->next = m->today + 2 + mrand(m) % 5; return; }
	pl->need = ai_la_need(m, c, t);
	deficit = pl->need - pl->own;
	if (deficit > 1 && !pl->asked) {
		long hi, hc, ha;
		ai_counts(m, c, &hi, &hc, &ha);
		if (hi + hc + 100 * ha <= 2 * ai_support(m, c)) { ai_add_order(m, c, k + 1, deficit, 1, t, (int)(pl->score / 1000 > 30000 ? 30000 : pl->score / 1000)); pl->asked = 1; }
	}
	ai_merge(m, k);
	ai_assault_try(m, k);
	pl->next = m->today + 2 + mrand(m) % 3;
}

/* ours (the exe's King gives free units to a General by 0x5499f0 and the plan moves them with 0x55a320): a free army with no
 * land target on its land mass is offered to the land plans of other land masses, nearest first, and goes by TRANSPORT */
static void ai_offer_far(Military *m, int c, int ui)
{
	World *w = m->w;
	LandUnit *u = &w->land[ui];
	long str = ai_ustr(m, u);
	unsigned char tried[AIP_MAX / 8 + 1];
	int n;
	memset(tried, 0, sizeof tried);
	for (n = 0; n < 3; n++) {
		int k, best = -1, dest;
		long bd = 0;
		MilPlan *pl;
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) {
			const MilPlan *q = &m->plan[k];
			long d;
			if (q->c != c || !ai_land_type(q->type) || (tried[k >> 3] & (1 << (k & 7))) || m->lgrp[q->target] == m->lgrp[u->location] || !ai_accepts(q, str)) continue;
			d = ai_sqd(w, u->location, q->target);
			if (best < 0 || d < bd) { best = k; bd = d; }
		}
		if (best < 0) break;
		tried[best >> 3] |= (unsigned char)(1 << (best & 7));
		pl = &m->plan[best];
		dest = pl->type != PL_INVASION && pl->phase == 0 && pl->gather > 0 ? pl->gather : pl->target;
		if (ai_send(m, ui, dest, best)) {
			pl->own += str;
			if (pl->type == PL_INVASION) logm(m, "AI: %s sends an invasion to %s(%d)", w->country[c].tag, ai_prov(w, pl->target), pl->target);
			return;
		}
		if (pl->type == PL_INVASION) logm(m, "AI: %s cancelled an invasion to %s(%d) (%s)", w->country[c].tag, ai_prov(w, pl->target), pl->target, s_why);
	}
	m->astuck[ui] = m->today + 30;                               /* bounced: not offered for 30 days */
}

/* C.4.5 split (0x54b560): a plan with more than 1.5 x what it needs gives its biggest idle army's surplus back - artillery first,
 * then cavalry, then infantry, by the strength of a piece / 1000 men; only when both parts keep >= 2000 men. Ours: the
 * surplus is what exceeds 1.25 x the need (so the plan does not take the part back: 0x5510e0 refuses it). */
static long ai_arm_str(const Military *m, const LandUnit *u, long i, long cv, long a) { LandUnit t = *u; t.infantry = i; t.cavalry = cv; t.artillery = a; return ai_ustr(m, &t); }
static void ai_split(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, i, big = -1, n;
	long bs = 0, S, pa, pc, pi, a, cv, in, left;
	LandUnit *u;
	if (pl->need <= 0 || pl->own * 2 <= pl->need * 3) return;
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		const LandUnit *v = &w->land[i];
		long s;
		if (m->aplan[i] != k + 1 || v->dead || v->npath || m->inbat[v->location]) continue;
		if ((s = ai_ustr(m, v)) > bs) { bs = s; big = i; }
	}
	if (big < 0) return;
	u = &w->land[big];
	S = pl->own - pl->need * 5 / 4;
	if (S > bs) S = bs;
	pa = ai_arm_str(m, u, 0, 0, 1); pc = ai_arm_str(m, u, 0, 1000, 0); pi = ai_arm_str(m, u, 1000, 0, 0);
	a = pa > 0 ? S / pa : 0;
	if (a > u->artillery) a = u->artillery;
	S -= a * pa;
	cv = pc > 0 ? S / pc : 0;
	if (cv > u->cavalry / 1000) cv = u->cavalry / 1000;
	S -= cv * pc;
	in = pi > 0 ? S / pi : 0;
	if (in > u->infantry / 1000) in = u->infantry / 1000;
	cv *= 1000; in *= 1000;
	left = in + cv + 100 * a;
	if (left < 2000 || unit_men(u) - left < 2000) return;
	if ((n = mil_new_army(m, c, u->location, in, u->name)) < 0) return;
	u = &w->land[big];
	w->land[n].cavalry = cv; w->land[n].artillery = a; w->land[n].morale_m = u->morale_m;
	u->infantry -= in; u->cavalry -= cv; u->artillery -= a;
	pl->own -= ai_ustr(m, &w->land[n]);
	logm(m, "AI: Agent #%d (%s) of %s splits Army #%d (%ld,%ld,%ld) and leaves (%ld,%ld,%ld).", k, ai_pname(pl->type), w->country[c].tag, big,
	     u->infantry, u->cavalry, u->artillery, in, cv, a);
}

/* LANDATTACK / CONQUER think (0x54cd30 / 0x550f20); the other types go to their own */
/* ------------------------------------------------------------- the colonial agent's explore plans (docs/SPEC_AI_EXPLORE.md)
 * EXSEA 0x32 (one fleet to an unknown sea zone), EXLAND 0x33 (armies to unknown land next to known land), KILLNAT 0x34 (an
 * ordinary army against natives). Children of the colonial agent (envoys.c calls mil_ai_colonial), no General (gen -1).
 * Units come as from the King (3): nearest free one first; kinds 2/3 take only units that can explore (0x48c0e0), every other
 * request leaves those out (0x48c1f0). Ours: polled daily for the unit events; the King's serving on a 20-39 day (war 5-9)
 * clock per plan (power = next serving day); one army per serving (NIEPEWNE: the power-4000 scale was not measured). */
static int x_type(int t) { return t == PL_EXSEA || t == PL_EXLAND || t == PL_KILLNAT; }
static int x_lexpl(const World *w, const LandUnit *u) { return w->country[u->owner].land_tech >= 31 || (u->l_id && (u->l_type == 2 || u->l_type == 3)); }
static int x_fexpl(const World *w, const NavalUnit *f) { return w->country[f->owner].naval_tech >= 27 || (f->l_id && (f->l_type == 2 || f->l_type == 3)); }
static int x_pref(const World *w, int c, int p)                  /* 0x55dd10 without the base: the .ai area / region / continent bonus */
{
	const Country *co = &w->country[c];
	const Province *pv = &w->prov[p];
	int i, b = 0;
	for (i = 0; i < 20 && co->ai_area[i] >= 0; i++) if (co->ai_area[i] == pv->area) { b += 50; break; }
	for (i = 0; i < 8 && co->ai_region[i] >= 0; i++) if (co->ai_region[i] == pv->region) { b += 25; break; }
	for (i = 0; i < 6 && co->ai_cont[i] >= 0; i++) if (co->ai_cont[i] == pv->continent) { b += 10; break; }
	return b;
}
static int x_new(Military *m, int c, int type, int t, int prio)
{
	World *w = m->w;
	MilPlan *pl;
	int k;
	for (k = 0; k < AIP_MAX && (m->plan[k].type != PL_FREE || m->plan[k].c == -2); k++) ;
	if (k == AIP_MAX) return -1;
	pl = &m->plan[k];
	memset(pl, 0, sizeof *pl);
	pl->type = (unsigned char)type; pl->c = (short)c; pl->gen = -1; pl->target = (short)t; pl->score = prio * 1000L;
	pl->army = pl->fleet = pl->parent = -1;
	pl->next = m->today + 1; pl->power = m->today + 1;
	pl->t0 = m->today + (type == PL_EXLAND ? 30 + mrand(m) % 30 : 10 + mrand(m) % 15);
	if (type == PL_EXLAND) {                                   /* S: the last land neighbour of the target c knows, else the capital */
		unsigned long e;
		pl->gather = w->country[c].capital;
		if (t < 1616) for (e = m->astart[t]; e < m->astart[t + 1]; e++) { int nb = (int)m->arec[2 * e]; if (land_prov(w, nb) && WORLD_KNOWN(w, c, nb)) pl->gather = (short)nb; }
	}
	m->cpl_next[k] = m->cpl_head[c]; m->cpl_head[c] = (short)k;
	logm(m, "AI: %s starts an %s plan for %s(%d)", w->country[c].tag, ai_pname(type), ai_prov(w, t), t);
	return k;
}

/* 0x553570 the sea zone's value x1000 */
static long x_sea_value(Military *m, int c, int p, int cap, int eucap)
{
	World *w = m->w;
	unsigned long e;
	long n = 0, s = 0, v, d;
	const char *rn;
	if (p < 1616) for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
		int nb = (int)m->arec[2 * e];
		long wt;
		if (nb <= 0 || nb >= w->nprov || WORLD_KNOWN(w, c, nb)) continue;
		n++;
		wt = w->prov[nb].income_m / 1000;
		if (w->prov[nb].owner >= 0 && w->prov[nb].owner != c) wt *= 2;
		wt -= w->prov[nb].col_diff / 5;
		if (wt < 1) wt = 1;
		s += wt + x_pref(w, c, nb);
	}
	if (n == 0) v = 0;
	else {
		if (!WORLD_KNOWN(w, c, p)) s += 20;
		d = cap > 0 ? ai_dist(w, cap, p) : 0;
		v = (n + s) * 1000;
		if (d >= 1) v /= d;
		v *= 40;
		if (eucap) {                                          /* the region table 0x553800 (our sea region names) */
			static const char *const zero[] = { "AlaskaSea", "EPacificSea", "SWPacificSea", "SEPacificSea", "OceaniaSea", "BarrierReefSea", "BarrierreefSea", "WPacificSea", "ChinaSea", "JapanSea", "KamtchatkaSea", "NWPacificSea", NULL };
			static const char *const ind[] = { "SWIndianSea", "NWIndianSea", "SEIndianSea", "NEIndianSea", "ThaiSea", NULL };
			int i, b = 100;
			rn = w->prov[p].region >= 0 ? world_name(w, w->prov[p].region) : "";
			for (i = 0; zero[i]; i++) if (!strcmp(rn, zero[i])) b = 0;
			for (i = 0; ind[i]; i++) if (!strcmp(rn, ind[i])) b = c == world_country(w, "POR") ? 10 : 0;
			v += b * 1000L;
		}
	}
	return v + (ai_query(m, c, 0, 1) == 2 ? 25000 : 1000);
}

/* called by the colonial agent (0x53bbc0 step 5; start = the agent's start, one of each) */
void mil_ai_colonial(Military *m, int c, int start)
{
	World *w = m->w;
	int k, p, n32 = 0, n33 = 0, n34 = 0, nl56 = 0, best, cap = w->country[c].capital, eucap, ships;
	long bv;
	if (!m || c < 0 || c >= w->ncountry || !ai_country(m, c)) return;
	if (cap <= 0 || cap >= w->nprov) cap = 0;
	eucap = cap && m->europe >= 0 && w->prov[cap].continent == m->europe;
	for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k])
		if (m->plan[k].c == c) { if (m->plan[k].type == PL_EXSEA) n32++; else if (m->plan[k].type == PL_EXLAND) n33++; else if (m->plan[k].type == PL_KILLNAT) n34++; }
	for (p = 1; p < w->nprov; p++) {
		unsigned long e;
		if (!land_prov(w, p) || WORLD_KNOWN(w, c, p) || p >= 1616) continue;
		for (e = m->astart[p]; e < m->astart[p + 1]; e++) { int nb = (int)m->arec[2 * e]; if (nb > 0 && nb < w->nprov && w->prov[nb].terrain != 6 && WORLD_KNOWN(w, c, nb)) { nl56++; break; } }
	}
	/* 0x32: Russia only with its byte U+0xfec (NIEZNANE, taken as unset) */
	if ((start || n32 < 1) && c != world_country(w, "RUS")) {
		for (best = -1, bv = -10000000L, p = 1; p < w->nprov; p++) {
			long v;
			int j, dup = 0;
			if (!sea_prov(w, p) || WORLD_KNOWN(w, c, p)) continue;
			for (j = m->cpl_head[c]; j >= 0; j = m->cpl_next[j]) if (m->plan[j].c == c && m->plan[j].type == PL_EXSEA && m->plan[j].target == p) dup = 1;
			if (dup) continue;
			v = x_sea_value(m, c, p, cap, eucap);
			if (v > bv) { bv = v; best = p; }
		}
		if (best > 0) x_new(m, c, PL_EXSEA, best, 25);
	}
	/* 0x33: the duplicate test compares with 0x32 (literal: never matches), so successive plans take the same province */
	if (start || n33 < (nl56 / 5 + 1 < 5 ? nl56 / 5 + 1 : 5)) {
		ships = m->navw[c] + m->navt[c];
		for (best = -1, bv = -10000000L, p = 1; p < w->nprov; p++) {
			unsigned long e;
			long v;
			int ok = 0;
			if (!land_prov(w, p) || WORLD_KNOWN(w, c, p) || p >= 1616) continue;
			for (e = m->astart[p]; e < m->astart[p + 1] && !ok; e++) { int nb = (int)m->arec[2 * e]; if (nb > 0 && nb < w->nprov && w->prov[nb].terrain != 6 && WORLD_KNOWN(w, c, nb)) ok = 1; }
			if (!ok) continue;
			if (ships < 1 && cap && w->prov[p].continent != w->prov[cap].continent) v = -1000;
			else { v = 1 + x_pref(w, c, p); if (v < 1) v = 1; v = v * 1000 + (ai_query(m, c, 0, 1) == 2 ? 25000 : 1000); }
			if (v > bv) { bv = v; best = p; }
		}
		if (best > 0) x_new(m, c, PL_EXLAND, best, 52);
	}
	/* 0x34: weighted random over known native provinces with aggressiveness > 4 (L8c; ours without the may-trader test) */
	if (start || n34 < 2) {
		long W = 0, r;
		for (p = 1; p < w->nprov; p++) if (land_prov(w, p) && WORLD_KNOWN(w, c, p) && w->prov[p].owner < 0 && w->prov[p].tolerance > 4 && w->prov[p].natives > 0) W += w->prov[p].natives < 99 ? 100 - w->prov[p].natives : 1;
		if (W > 0) {
			r = (long)(((unsigned long)mrand(m) << 15 | (unsigned long)mrand(m)) % (unsigned long)W);
			for (p = 1; p < w->nprov; p++) {
				if (!(land_prov(w, p) && WORLD_KNOWN(w, c, p) && w->prov[p].owner < 0 && w->prov[p].tolerance > 4 && w->prov[p].natives > 0)) continue;
				if ((r -= w->prov[p].natives < 99 ? 100 - w->prov[p].natives : 1) < 0) { x_new(m, c, PL_KILLNAT, p, 51); break; }
			}
		}
	}
}

static long x_serve_next(Military *m, int c) { return m->today + (m->realwar[c] ? 5 + mrand(m) % 5 : 20 + mrand(m) % 20); }

static void x_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, t = pl->target, i, nar = 0;
	pl->next = m->today + 1;
	if (pl->type == PL_EXSEA) {
		if (pl->fleet >= 0 && (pl->fleet >= w->nnaval || w->naval[pl->fleet].owner != c || fleet_ships(&w->naval[pl->fleet]) <= 0)) pl->fleet = -1;
		if (pl->fleet >= 0) {
			const NavalUnit *f = &w->naval[pl->fleet];
			if (f->location == t) { ai_end_plan(m, k, "done (has now sea explored)"); return; }
			if (ai_still(f)) { ai_end_plan(m, k, "cancelled (not moving)"); return; }
			return;
		}
		if (m->today < pl->power) return;
		pl->power = x_serve_next(m, c);
		{	/* the free explore-capable fleets, nearest first, until one has a route (a rejected one stays out, +0x28) */
			static short cf[MAX_NAVAL]; static long cdd[MAX_NAVAL];
			int nc = 0, j, bf;
			for (i = m->fo_head[c]; i >= 0; i = m->fo_next[i])
				if (ai_free_fleet0(m, i) && x_fexpl(w, &w->naval[i])) { cf[nc] = (short)i; cdd[nc++] = ai_sqd(w, w->naval[i].location, t); }
			while (nc > 0) {
				int b = 0;
				for (j = 1; j < nc; j++) if (cdd[j] < cdd[b]) b = j;
				bf = cf[b]; cf[b] = cf[nc - 1]; cdd[b] = cdd[nc - 1]; nc--;
				if (w->naval[bf].location != t && ai_sail(m, bf, t) < 0) { pl->asked = 1; continue; }
				m->fplan[bf] = (unsigned short)(k + 1); pl->fleet = (short)bf;
				logm(m, "AI: %s sends an explorer towards %s(%d)", w->country[c].tag, ai_prov(w, t), t);
				if (w->naval[bf].location == t) ai_end_plan(m, k, "done (has now sea explored)");
				return;
			}
		}
		return;
	}
	/* land plans: their armies (and armies on the way in a TRANSPORT of theirs) */
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		if (m->aplan[i] != k + 1) continue;
		if (u->dead || u->owner != c) { m->aplan[i] = 0; continue; }
		nar++;
		if (u->pathpos < u->npath) continue;
		if (u->location == t) {
			if (pl->type == PL_EXLAND) { ai_end_plan(m, k, "done (has now land explored)"); return; }
			if (!m->inbat[t] && w->prov[t].natives > 0) {            /* 0x553f30: KillNatives */
				if (!pl->phase || m->today >= pl->t0) { logm(m, "AI: %s will now KillNatives in %s(%d)", w->country[c].tag, ai_prov(w, t), t); mil_native_attack(m, t); pl->phase = 1; pl->t0 = m->today + 20 + mrand(m) % 10; }
			} else if (w->prov[t].natives <= 0) { ai_end_plan(m, k, "done (genocider is done)"); return; }
			continue;
		}
		if (ai_route(m, i, t) >= 0) continue;                      /* landed short of the target (ours: the transport's landing province) */
		ai_end_plan(m, k, "cancelled (isnt moving)"); return;
	}
	for (i = m->cpl_head[c]; i >= 0; i = m->cpl_next[i]) if (m->plan[i].type == PL_TRANSPORT && m->plan[i].c == c && m->plan[i].parent == k) nar++;
	if (pl->type == PL_EXLAND && m->today >= pl->t0) {          /* 0x553bc0, every 30-59 days */
		pl->t0 = m->today + 30 + mrand(m) % 30;
		if (pl->asked) { ai_end_plan(m, k, "cancelled (a unit was rejected)"); return; }
		if (WORLD_KNOWN(w, c, t)) { ai_end_plan(m, k, "done (target known)"); return; }
		if (pl->engaged && !nar) { ai_end_plan(m, k, "cancelled (no army left)"); return; }
	}
	if (pl->type == PL_KILLNAT && w->prov[t].natives <= 0 && nar == 0 && pl->engaged) { ai_end_plan(m, k, "done (genocider is done)"); return; }
	if (m->today < pl->power || (pl->type == PL_KILLNAT && nar > 0)) return;
	pl->power = x_serve_next(m, c);
	{
		static short cand[MAX_LAND]; static long cd[MAX_LAND];
		int nc = 0, j, S = pl->type == PL_EXLAND && pl->gather > 0 ? pl->gather : t;
		for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
			const LandUnit *u = &w->land[i];
			int ex;
			if (!ai_free_army0(m, i) || m->astuck[i] > m->today) continue;
			ex = u->l_id && (u->l_type == 2 || u->l_type == 3);
			if (pl->type == PL_EXLAND ? !x_lexpl(w, u) : ex) continue;
			cand[nc] = (short)i; cd[nc] = ai_sqd(w, u->location, S) + (m->lgrp[u->location] != m->lgrp[S] ? 1000000L : 0); nc++;
		}
		while (nc > 0) {
			int b = 0;
			for (j = 1; j < nc; j++) if (cd[j] < cd[b]) b = j;
			i = cand[b]; cand[b] = cand[nc - 1]; cd[b] = cd[nc - 1]; nc--;
			if (ai_send(m, i, t, k)) {
				pl->engaged = 1;
				logm(m, "AI: %s sends an %s towards %s(%d) with a %s plan", w->country[c].tag, pl->type == PL_EXLAND ? "conquistador" : "army", ai_prov(w, t), t, ai_pname(pl->type));
				return;
			}
			if (pl->type == PL_EXLAND) logm(m, "AI: %s %s army in %s rejected for exploring %s(%d) (%s)", w->country[c].tag, ai_prov(w, w->land[i].location), ai_prov(w, w->land[i].location), ai_prov(w, t), t, s_why);
			pl->asked = 1;                                     /* rejected: remembered (+0x28) */
		}
	}
}

static void ai_plan_think(Military *m, int k)
{
	World *w = m->w;
	MilPlan *pl = &m->plan[k];
	int c = pl->c, t = pl->target;
	const Province *pv = &w->prov[t];
	long deficit;
	if (x_type(pl->type)) { x_think(m, k); return; }
	if (pl->type == PL_GARRISON) { ai_garrison_think(m, k); return; }
	if (ai_naval_type(pl->type)) { ai_naval_think(m, k); return; }
	if (pl->type == PL_TRANSPORT) { ai_transport_think(m, k); return; }
	if (pl->type == PL_INVASION) { ai_inv_think(m, k); return; }
	pl->own = ai_plan_own(m, k);
	if (!ai_enter(m, c, t)) { ai_end_plan(m, k, "cancelled (may not enter)"); return; }
	if (pl->type == PL_CONQUER ? (pv->controller < 0 || pv->controller == c || !atw(m, c, pv->controller))
	                           : !((pv->owner == c && pv->controller != c) || ai_hostile(m, t, c, NULL, NULL) > 0)) {
		ai_end_plan(m, k, "done"); return;
	}
	if (ai_retreats(m, k)) { pl->next = m->today + 2 + mrand(m) % 5; return; }
	pl->need = pl->type == PL_CONQUER ? ai_cq_need(m, c, t) : ai_la_need(m, c, t);
	if (!pl->gather && pl->phase == 0) { pl->gather = (short)ai_gather(m, k); if (pl->gather <= 0) { pl->gather = -1; pl->phase = 1; } }
	ai_phase(m, k);
	deficit = pl->need - pl->own;                               /* C.4.5 0x54d3a0 -> B.2.3 */
	if (deficit > 1 && !pl->asked) {
		long hi, hc, ha;
		ai_counts(m, c, &hi, &hc, &ha);
		if (hi + hc + 100 * ha <= 2 * ai_support(m, c)) {
			ai_add_order(m, c, k + 1, deficit, 1, t, (int)(pl->score / 1000 > 30000 ? 30000 : pl->score / 1000));
			pl->asked = 1;
		}
	}
	ai_merge(m, k);
	ai_split(m, k);
	if (!(pl->engaged && ai_assault_try(m, k)) && ai_ready(m, k)) ai_engage(m, k);
	if (pl->engaged) pl->next = m->today + 2 + mrand(m) % 5;
	else pl->next = m->today + (pl->type == PL_CONQUER ? 3 + mrand(m) % 5 : 3 + mrand(m) % 7);
}

/* the General's think (0x549850): candidates, commit, armies. An expedition General (D.6, a land mass where we own nothing)
 * has the minimum 4 plans; its CONQUER walk starts from what we control there, and it scans INVASION over the land mass (the
 * invasion check 0x54a220: for it c = n). (A home General's own 25 % check is not ported: with one General per land mass its
 * LANDATTACK candidates already take every own occupied province.) */
static void ai_gen_think(Military *m, int gi)
{
	World *w = m->w;
	MilGen *g = &m->gen[gi];
	AiCand cd[17];
	int c = g->c, grp = g->grp, nc = 0, n = 0, links = 0, limit, active = 0, worst = -1, k, p, cap = w->country[c].capital;
	static unsigned short os[MAX_COUNTRY], ocur;
	if (++g_tcur == 0) { memset(g_tst, 0, sizeof g_tst); g_tcur = 1; }
	if (++ocur == 0) { memset(os, 0, sizeof os); ocur = 1; }
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		unsigned long e;
		if (m->lgrp[p] != grp) continue;
		n++;
		if (p < 1616)
			for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
				int q = (int)m->arec[2 * e], o;
				if (land_prov(w, q) && (o = w->prov[q].owner) >= 0 && o != c && os[o] != ocur) { os[o] = ocur; links++; }
			}
	}
	g->next = m->today + (cap > 0 && cap < MAX_PROV && m->lgrp[cap] == grp ? 3 + mrand(m) % 10 : 7 + mrand(m) % 14);
	if (!n && !g->exp) return;
	limit = 4 + n / 3 + links;                                  /* 0x548800 (links: the neighbouring owners, ours) */
	if (limit > w->country[c].manpower_m / 4000) limit = (int)(w->country[c].manpower_m / 4000);   /* 0.25 x manpower (the pool, NIEPEWNE) */
	if (limit < 4) limit = 4;
	if (limit > 16) limit = 16;
	for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) {
		const MilPlan *pl = &m->plan[k];
		if (pl->c != c || !ai_land_type(pl->type)) continue;
		g_tst[pl->target] = g_tcur;
		if (pl->gen != gi) continue;
		active++;
		if (worst < 0 || pl->score < m->plan[worst].score) worst = k;
	}
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {        /* LANDATTACK over our provinces (C.2.2, C.4.1) */
		long s;
		if (m->lgrp[p] != grp || g_tst[p] == g_tcur) continue;
		if ((s = ai_la_score(m, c, grp, p)) > 0) { nc = ai_cand(cd, nc, PL_LANDATTACK, p, s); g_tst[p] = g_tcur; }
	}
	if (m->realwar[c] && active < limit) nc = ai_conquer_scan(m, c, grp, g->exp, cd, nc);   /* CONQUER at war (C.2.5 0x549b80; the exe scans on war / province events - ours: polled while a plan slot is free) */
	if (g->exp && m->realwar[c] && active < limit)               /* INVASION over the land mass (D.6) */
		for (p = m->gp_head[grp]; p > 0; p = m->gp_next[p]) {
			long s;
			if (g_tst[p] == g_tcur || !m->coast[p]) continue;
			if ((s = ai_inv_score(m, c, p)) > 0) { nc = ai_cand(cd, nc, PL_INVASION, p, s); g_tst[p] = g_tcur; }
		}
	for (k = 0; k < nc; k++) {                                  /* commit (C.2.4 / D.1) */
		if (active < limit) { if (ai_new_plan(m, gi, cd[k].type, cd[k].target, cd[k].score) >= 0) active++; }
		else {
			if (worst >= 0 && cd[k].score > m->plan[worst].score) { ai_end_plan(m, worst, "replaced"); ai_new_plan(m, gi, cd[k].type, cd[k].target, cd[k].score); }
			break;
		}
	}
	ai_distribute(m, gi);
}

static int ai_owns_in(const Military *m, int c, int grp) { int p; for (p = m->own_head[c]; p > 0; p = m->own_next[p]) if (m->lgrp[p] == grp) return 1; return 0; }

/* A.2 step 6: a General for every land mass where we own land; one whose land is gone ends with its plans. At war, with an own
 * port: an expedition General (0x544160) for the land mass of an enemy where we own nothing (ours: the land mass of the enemy's
 * capital, at most AI_EXP_MAX per country; it ends when that war is over or the enemy owns nothing there, and becomes a home
 * General when we own land there) */
#define AI_EXP_MAX 2
static void ai_generals(Military *m, int c)
{
	World *w = m->w;
	short grp[32], own[32];
	int ng = 0, p, i, k, e, nexp = 0, port = 0;
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
		int gr = m->lgrp[p];
		if (gr < 0) continue;
		if (!port && w->prov[p].controller == c && port_prov(w, p)) port = 1;
		for (i = 0; i < ng && grp[i] != gr; i++) ;
		if (i == ng && ng < 32) grp[ng++] = (short)gr;
	}
	memcpy(own, grp, sizeof grp);
	for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) {
		MilGen *g = &m->gen[k];
		if (g->c != c) continue;
		for (i = 0; i < ng && grp[i] != g->grp; i++) ;
		if (i < ng) {
			grp[i] = -1;
			if (g->exp) { g->exp = 0; g->foe = -1; g->dnext = m->today + 1 + mrand(m) % 2; g->anext = m->today + 5 + mrand(m) % 10; logm(m, "AI: %s expedition on land mass %d becomes a home General", w->country[c].tag, g->grp); }
			continue;
		}
		if (g->exp && m->realwar[c] && g->foe >= 0 && atw(m, c, g->foe) && ai_owns_in(m, g->foe, g->grp)) { nexp++; continue; }
		for (i = m->cpl_head[c]; i >= 0; i = m->cpl_next[i]) if (m->plan[i].type != PL_FREE && m->plan[i].c == c && m->plan[i].gen == k) ai_end_plan(m, i, g->exp ? "ended (expedition over)" : "ended (land lost)");
		g->c = -2;
	}
	for (i = 0; i < ng; i++) {
		if (grp[i] < 0) continue;
		for (k = 0; k < AIG_MAX && m->gen[k].c != -1; k++) ;
		if (k == AIG_MAX) break;
		memset(&m->gen[k], 0, sizeof m->gen[k]);
		m->gen[k].c = (short)c; m->gen[k].grp = grp[i]; m->gen[k].foe = -1;
		m->gen[k].next = m->today + 5 + mrand(m) % 10;        /* first think 0x55a4a0(5, 10) */
		m->gen[k].dnext = m->today + 1 + mrand(m) % 2;        /* DEFEND start 1 + 2 */
		m->gen[k].anext = m->today + 5 + mrand(m) % 10;       /* the Admiral's first think (D.1) */
		m->cg_next[k] = m->cg_head[c]; m->cg_head[c] = (short)k;
		logm(m, "AI: %s starts a CAIMilitaryMinister for land mass %d", w->country[c].tag, grp[i]);
	}
	if (!m->realwar[c] || !port) return;
	for (e = 0; e < w->ncountry && e < MAX_COUNTRY && nexp < AI_EXP_MAX; e++) {   /* our enemies: c's row of the war bits, a byte at a time */
		long bi = (long)c * MAX_COUNTRY + e;
		if (!(bi & 7) && !m->warbits[bi >> 3]) { e += 7; continue; }
		if (!((m->warbits[bi >> 3] >> (bi & 7)) & 1)) continue;
		{
			int cap, eg;
			if (e == c || e == w->reb || e == w->nat || e == w->pir || w->country[e].nprov_owned <= 0) continue;   /* (the exe's 0x14f..0x153) */
			cap = w->country[e].capital;
			if (cap <= 0 || cap >= MAX_PROV || (eg = m->lgrp[cap]) < 0 || !m->gcoast[eg]) continue;
			for (i = 0; i < ng && own[i] != eg; i++) ;
			if (i < ng) continue;                                    /* we own land there: the home General's CONQUER */
			for (k = m->cg_head[c]; k >= 0 && !(m->gen[k].c == c && m->gen[k].grp == eg); k = m->cg_next[k]) ;
			if (k >= 0) continue;
			for (k = 0; k < AIG_MAX && m->gen[k].c != -1; k++) ;
			if (k == AIG_MAX) break;
			memset(&m->gen[k], 0, sizeof m->gen[k]);
			m->gen[k].c = (short)c; m->gen[k].grp = (short)eg; m->gen[k].exp = 1; m->gen[k].foe = (short)e;
			m->gen[k].next = m->today;                             /* its init runs the invasion check at once (0x54a2c0) */
			m->gen[k].dnext = m->gen[k].anext = 0x7FFFFFFFL;       /* no DEFEND, no Admiral */
			m->cg_next[k] = m->cg_head[c]; m->cg_head[c] = (short)k;
			nexp++;
			logm(m, "AI: %s starts an expedition General for land mass %d (against %s)", w->country[c].tag, eg, w->country[e].tag);
		}
	}
}

/* A.7 peace-time demobilisation (0x543c80) */
static void ai_demob(Military *m, int c)
{
	World *w = m->w;
	long inc = w->country[c].inc_m > 0 ? w->country[c].inc_m * 12 : 0, infl = 1000 + w->country[c].inflation_u / 1000;
	int i, city = 0, p;
	for (p = m->own_head[c]; p > 0 && !city; p = m->own_next[p]) city = ai_city(&w->prov[p]);
	for (i = m->arm_head[c]; i >= 0; i = m->arm_next[i]) {
		LandUnit *u = &w->land[i];
		long upkeep;
		if (u->dead || u->npath || m->inbat[u->location] || !land_prov(w, u->location)) continue;
		upkeep = (9 * u->infantry / 120 + 21 * u->cavalry / 120 + 30 * u->artillery * 1000 / 1200) * infl / 1000 * 12;   /* a year (economy.c) - vfunc 0x38 NIEPEWNE */
		if (ai_enter(m, c, u->location) && city && inc >= upkeep) continue;
		logm(m, "AI: %s disbands an army of %ld men in %s (peace)", w->country[c].tag, unit_men(u), ai_prov(w, u->location));
		u->dead = 1; u->infantry = u->cavalry = u->artillery = 0; u->l_id = 0; m->aplan[i] = 0;
	}
}

/* the King's think (0x543f20): the Generals (A.2), wanted forces (B.1), BuildFleet / BuildArmy (B.2), the
 * orders (B.3), demobilisation (A.7); the interval (A.4) */
static void ai_king_think(Military *m, int c)
{
	World *w = m->w;
	Country *co = &w->country[c];
	MilKing *K = &m->king[c];
	int war = m->realwar[c], k, n, p, ports = 0, yards = 0, armies = 0;
	long inf, cav, art, sup, s, wInf, wCav, wArt, wW = 0, wG = 0, wT = 0, V, M = 0, ws = 0, gal = 0, tr = 0;
	if (war && !K->war) {                                        /* war: garrisons dissolve, the Generals scan now (C.2.5 0x84, D.5) */
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) if (m->plan[k].type == PL_GARRISON && m->plan[k].c == c) ai_end_plan(m, k, "");
		for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) if (m->gen[k].c == c) m->gen[k].next = m->today;
	}
	if (!war && K->war)                                          /* peace: DEFEND lays out the garrisons (0x80) */
		for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) if (m->gen[k].c == c) m->gen[k].dnext = m->today;
	K->war = (unsigned char)war;
	ai_generals(m, c);
	/* B.1 wanted levels */
	ai_counts(m, c, &inf, &cav, &art);
	for (k = m->arm_head[c]; k >= 0; k = m->arm_next[k]) if (!w->land[k].dead) armies++;
	sup = ai_support(m, c);
	s = co->nprov_owned;
	if (200 * s > sup) s = sup / 200;
	wInf = 1000 * s + 10000;
	wCav = 500 * s + 5000;                                       /* cavalry allowed (0x429590: New-World natives below land tech 5 - not checked, NIEPEWNE) */
	wArt = co->land_tech >= 7 ? s / 2 + 5 : 0;
	if (unit_price(w, c, 1) / 2 < unit_price(w, c, 0)) { wCav += wCav / 2; wInf /= 2; }
	/* B.1.6-11 the navy */
	for (p = m->own_head[c]; p > 0; p = m->own_next[p]) { if (port_prov(w, p)) ports++; if (w->prov[p].bflags & PB_SHIPYARD) yards++; }
	ws = m->navw[c]; gal = m->navg[c];
	tr = m->navt[c];
	for (k = 0; k < w->nbuild; k++) if (w->build[k].country == c && w->build[k].ship >= 1 && w->build[k].ship <= 3) { if (w->build[k].ship == 1) ws += w->build[k].inf; else if (w->build[k].ship == 2) gal += w->build[k].inf; else tr += w->build[k].inf; }
	if (ports) {
		static unsigned short os[MAX_COUNTRY], ocur;
		/* B.1.8: the exe's table by its country index (country.csv row + 1): ENG 1 / FRA 2 / HOL 4 / SPA 8 also measure each other */
		static const char *const extra[4][4] = { { "ENG", "SPA", "FRA", "HOL" }, { "FRA", "SPA", "ENG", "HOL" }, { "HOL", "SPA", "FRA", "ENG" }, { "SPA", "ENG", "FRA", "HOL" } };
		wT = 2 * (co->col_rate_m >= 990 ? 2 : 1);              /* 2 x Admirals (ours: one per country with a port) x 2 for a colonial power */
		V = wT + ports + 2 * yards;                              /* owned x coast share = our ports */
		if (++ocur == 0) { memset(os, 0, sizeof os); ocur = 1; }
		for (p = m->own_head[c]; p > 0; p = m->own_next[p]) {
			unsigned long e;
			if (p >= 1616) continue;
			for (e = m->astart[p]; e < m->astart[p + 1]; e++) {
				int q = (int)m->arec[2 * e], o;
				if (land_prov(w, q) && (o = w->prov[q].owner) >= 0 && o != c && os[o] != ocur) { os[o] = ocur; if (m->navg[o] / 4 + m->navw[o] > M) M = m->navg[o] / 4 + m->navw[o]; }
			}
		}
		for (k = 0; k < 4; k++) if (!strcmp(co->tag, extra[k][0])) {
			int k2;
			for (k2 = 1; k2 < 4; k2++) { int o = world_country(w, extra[k][k2]); if (o >= 0 && m->navg[o] / 4 + m->navw[o] > M) M = m->navg[o] / 4 + m->navw[o]; }
		}
		if (V < M) V = M;
		if (V > co->nprov_owned) V = co->nprov_owned;
		if (!strcmp(co->tag, "TUR") || !strcmp(co->tag, "VEN") || !strcmp(co->tag, "KNI") || !strcmp(co->tag, "GEN")) { wG = 2 * V; wW = V / 2; } else wW = V;   /* galley navies: exe 10 / 11 / 22 / 26 */
		{	long infl = 1000 + co->inflation_u / 1000, up = (50 * ws + 10 * gal + 15 * tr) * 1000 / 480 * infl / 1000;
			if (co->inc_m / 5 < up) wW = wG = wT = 0; }        /* the navy is not affordable */
	}
	K->bf[0] = (short)(wW - ws < 0 ? 0 : wW - ws > 10 ? 10 : wW - ws);   /* B.2.2 BuildFleet, re-targeted every think */
	K->bf[1] = (short)(wG - gal < 0 ? 0 : wG - gal > 10 ? 10 : wG - gal);
	K->bf[2] = (short)(wT - tr < 0 ? 0 : wT - tr > 10 ? 10 : wT - tr);
	if (ports && wW + wG + wT > 0) {                             /* the plans' shortfalls (D.3 0x54d710 / D.7 -> K vfunc 0x1e0; ours: on top of the wanted levels) */
		NavalUnit one;
		long w1, aw = 0, at = 0;
		memset(&one, 0, sizeof one); one.owner = (short)c; one.warships = 1; one.morale_m = nmax_morale(m, c);
		w1 = ai_fstr(m, &one);
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) {
			const MilPlan *pl = &m->plan[k];
			if (pl->c != c) continue;
			if (ai_naval_type(pl->type) && pl->need > pl->own && w1 > 0) aw += (pl->need - pl->own) / w1 + 1;
			else if (pl->type == PL_TRANSPORT && pl->fleet < 0 && pl->army >= 0 && pl->army < w->nland) at += mil_army_weight(&w->land[pl->army]) / 2000 + 1;
		}
		if (aw) K->bf[0] = (short)(K->bf[0] + aw > 10 ? 10 : K->bf[0] + aw);
		if (at) K->bf[2] = (short)(K->bf[2] + at > 10 ? 10 : K->bf[2] + at);
	}
	if (war) {                                                   /* B.2.1 BuildArmy, at war only */
		long d = (wInf > inf ? wInf - inf : 0) + (wCav > cav ? wCav - cav : 0) + (wArt > art ? wArt - art : 0);
		for (k = 0; k < AIO_MAX && !(m->ord[c][k].prov > 0 && m->ord[c][k].plan == 0); k++) ;
		if (k == AIO_MAX && d > 0 && inf + cav + 100 * art <= 2 * sup && co->capital > 0)
			ai_add_order(m, c, 0, d, 0, co->capital, armies ? 65 : 350);
	}
	ai_orders(m, c);
	ai_fleet_build(m, c);
	if (!war) ai_demob(m, c);
	else                                                         /* ours: armies with no land target go to the plans overseas (D.7) */
		for (k = m->arm_head[c]; k >= 0; k = m->arm_next[k]) {
			int gk, has = 0;
			if (!ai_free_army(m, k) || w->land[k].npath || atw(m, c, w->prov[w->land[k].location].controller)) continue;
			for (gk = m->cpl_head[c]; gk >= 0 && !has; gk = m->cpl_next[gk])
				if (m->plan[gk].c == c && ai_land_type(m->plan[gk].type) && m->lgrp[m->plan[gk].target] == m->lgrp[w->land[k].location]) has = 1;
			if (!has && m->astuck[k] <= m->today) ai_offer_far(m, c, k);
		}
	n = 20 + mrand(m) % 20;                                     /* A.4 */
	if (w->country[c].tracked) n /= 2;                          /* +0x11e: a featured country thinks twice as often */
	if (war) n /= 4;
	if (n < 2) n = 2;
	K->next = m->today + n;
}

#ifdef MILAI_PROF                                             /* host timing of the parts (test builds only) */
clock_t milai_prof[6];
#define AIP_T0 clock_t t0_ = clock()
#define AIP_ADD(i) do { clock_t t1_ = clock(); milai_prof[i] += t1_ - t0_; t0_ = t1_; } while (0)
#else
#define AIP_T0 (void)0
#define AIP_ADD(i) (void)0
#endif
static void milai_day(Military *m)
{
	World *w = m->w;
	int c, k;
	AIP_T0;
	ai_lists(m); AIP_ADD(0);
	for (c = 0; c < w->ncountry && c < MAX_COUNTRY; c++) {    /* (the exe's slot 0 is empty; ours is ENG) */
		int due;
		if (m->king[c].next > m->today && m->cdue[c] > m->today) continue;   /* nothing due today (a country no longer the AI's ends its plans when one is due) */
		if (!ai_country(m, c)) {                                 /* gone, or the player's: its plans and Generals end */
			for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k]) if (m->plan[k].type != PL_FREE && m->plan[k].c == c) ai_end_plan(m, k, "ended");
			for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) if (m->gen[k].c == c) m->gen[k].c = -2;
			for (k = 0; k < AIO_MAX; k++) m->ord[c][k].prov = 0;
			continue;
		}
		if (!m->king[c].next) m->king[c].next = m->today + 1 + mrand(m) % 20;   /* ours: the first thinks spread over 20 days */
		due = m->today >= m->king[c].next || m->today >= m->cdue[c];
		if (!due) continue;
		AIP_ADD(5);
		if (m->today >= m->king[c].next) ai_king_think(m, c);
		AIP_ADD(1);
		for (k = m->cg_head[c]; k >= 0; k = m->cg_next[k]) {
			if (m->gen[k].c != c) continue;
			if (m->today >= m->gen[k].dnext) { ai_defend(m, k); AIP_ADD(2); }
			if (m->gen[k].c == c && m->today >= m->gen[k].next) { ai_gen_think(m, k); AIP_ADD(3); }
			if (m->gen[k].c == c && !m->gen[k].exp && m->today >= m->gen[k].anext) { ai_admiral(m, k); AIP_ADD(3); }
		}
		for (k = m->cpl_head[c]; k >= 0; k = m->cpl_next[k])
			if (m->plan[k].type != PL_FREE && m->plan[k].c == c && m->today >= m->plan[k].next) { ai_plan_think(m, k); AIP_ADD(4); }
	}
}
