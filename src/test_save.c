/* test_save.c - proves the save game is exact.
 *   mode a: 1419, all AI, 3 years -> save A; 2 more years -> save X
 *   mode b: load A -> save A2 (must equal A); 2 years -> save Y (must equal X)
 * Host: test_save <text.pak> <map.pak> <dir> a|b    Amiga: test_save a|b (files in PROGDIR:)
 * Compare with cmp: A == A2 and X == Y. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "calendar.h"
#include "savegame.h"
#include "envoys.h"
#include "leaders.h"
static Leaders *g_tlead;
#include "religion.h"
#include "missions.h"
#include "savegame.h"
#include "revolts.h"
#include "buildings.h"

static World w;
static Missions *g_tmis;                                   /* the AI's missions run in the host test too */
static void tmis_save(FILE *f) { if (g_tmis) mis_save(g_tmis, &w, f); }
static void tmis_restore(const SNode *root) { if (g_tmis) mis_restore(g_tmis, &w, root); }
#ifdef AI_PROF
extern long ai_prof[8];
#endif
static void ailog(void *u, long today, const char *text) { (void)u; fprintf(stderr, "%ld %d %s%c", today, w.now.year, text, 10); }

static Military *g_tm; static Diplo *g_td; static long g_tday; static Events *g_tev;
static int ev_cmd(void *u, int code, int c, long which, long value)   /* as main_amiga.c ev_cmd */
{
	(void)u; (void)value;
	switch (code) {
	case EVC_NEIGHBOUR: return diplo_neighbours(g_td, c, (int)which);
	case EVC_REVOLT: case EVC_RELREVOLT: case EVC_COLREVOLT: return rev_spawn(&w, g_tm, (int)which);
	case EVC_INHERIT: diplo_annex_now(g_td, c, (int)which); return 1;
	case EVC_WAR: return diplo_declare_war(g_td, c, (int)which, g_tday) >= 0;
	case EVC_INDEPENDENCE: return rev_release(&w, (int)which, c);
	case EVC_COUNTRY: world_swap_identity(&w, c, (int)which); events_swap_countries(g_tev, c, (int)which); rev_swap(c, (int)which); if (g_tlead) lead_swap(g_tlead, c, (int)which); return 1;
	case EVC_LEADER: case EVC_SLEEPLEADER:
		if (w.nleadev < 64) { w.leadev_id[w.nleadev] = which; w.leadev_day[w.nleadev] = g_tday; w.leadev_kind[w.nleadev] = (unsigned char)(code == EVC_LEADER); w.nleadev++; return 1; }
		return 0;
	case EVC_MONARCH: case EVC_SLEEPMONARCH:
		if (w.nmonev < 64) { w.monev_c[w.nmonev] = (short)c; w.monev_id[w.nmonev] = which; w.monev_day[w.nmonev] = g_tday; w.monev_kind[w.nmonev] = (unsigned char)(code == EVC_MONARCH); w.nmonev++; return 1; }
		return 0;
	default: return 0;
	}
}

static long trade_value_cb(void *u, int p) { return econ_trade_value_m((const Economy *)u, p); }
static long support_cb(void *u, int c) { return econ_support_limit((const Economy *)u, c); }
static int risk_cb(void *u, int p) { return rev_risk(&w, (const Military *)u, p); }
static int g_peak[3];                                      /* tests: the fullest our fixed tables got (l mode) */
static void run(Economy *ec, Events *ev, Military *mil, Peace *pc, Diplo *dp, long *day, int years)
{
	long end = *day + (years < 0 ? -years : years * 360L);   /* years < 0: that many days (TSDAYS) */
	while (*day < end) {
		int y, m, d, oldm = w.now.month, oldy = w.now.year;
		(*day)++;
		cal_date(*day, &y, &m, &d);
		w.now.year = (short)y; w.now.month = (unsigned char)m; w.now.day = (unsigned char)d;
		if (m != oldm) { lead_spawn(g_tlead, &w, mil, 0); lead_assign(g_tlead, &w); world_weather_month(&w); econ_month(ec); mil_month(mil); peace_month(pc, *day); diplo_month(dp, *day); env_ai_month(&w, ec, mil, *day); rev_month(&w, mil); rel_ai_month(&w, *day); world_hre_month(&w, *day); if (g_tmis) mis_month(g_tmis, &w, -1, *day); bld_ai_month(&w, mil, *day); if (y != oldy) econ_year(ec); }
		g_tday = *day; events_day(ev, *day);
		mil_day(mil, *day); rev_day(&w, *day); env_day(&w, *day);
		{ int u[3], q; mil_usage(mil, &u[0], &u[1], &u[2]); for (q = 0; q < 3; q++) if (u[q] > g_peak[q]) g_peak[q] = u[q]; }
		diplo_day(dp, *day);
	}
}

int main(int argc, char **argv)
{
	Pak t, map;
	char err[160], pa[256], pa2[256], px[256], py[256];
	const char *dir = argc > 4 ? argv[3] : "PROGDIR:", *mode = argc > 4 ? argv[4] : argc > 1 ? argv[1] : "a";
	const char *sep = dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/";
	Economy *ec; Events *ev; Military *mil; Peace *pc; Diplo *dp;
	long day;
	int load = mode[0] == 'b';
	snprintf(pa, sizeof pa, "%s%ssave_a.eug", dir, sep); snprintf(pa2, sizeof pa2, "%s%ssave_a2.eug", dir, sep);
	snprintf(px, sizeof px, "%s%ssave_x.eug", dir, sep); snprintf(py, sizeof py, "%s%ssave_y.eug", dir, sep);
	if (pak_open(&t, argc > 4 ? argv[1] : "PROGDIR:data/text.pak") || pak_open(&map, argc > 4 ? argv[2] : "PROGDIR:data/map.pak")) { printf("packs\n"); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, load ? pa : "scenarios/the grand campaign.eug", err, sizeof err)) { printf("%s\n", err); return 20; }
	day = cal_day(w.now.year, w.now.month, w.now.day);
	if (!(ev = events_load(&w, &t, day, 12345, err, sizeof err)) || !(ec = econ_load(&w, &t, err, sizeof err)) ||
	    !(mil = mil_load(&w, &t, &map, 12345, err, sizeof err)) || !(pc = peace_new(&w, day, 777)) || !(dp = diplo_new(&w, mil, pc, 4242))) {
		printf("modules: %s\n", err); return 20;
	}
	diplo_load_ai(dp, &t); env_set_trade_offer(diplo_ai_offer_trade, dp); env_set_econ(ec); env_set_military(mil); env_attach(&w); mil_set_tradefn(mil, trade_value_cb, ec); mil_set_supportfn(mil, support_cb, ec); econ_set_riskfn(ec, risk_cb, mil); rev_set_peace(pc); rev_set_diplo(dp); rev_load(&w, &t); g_tlead = lead_load(&w, &t);
	g_tm = mil; g_td = dp; g_tev = ev; events_set_cmds(ev, ev_cmd, NULL);
	g_tmis = mis_load(&w, &t); save_set_extra(tmis_save, tmis_restore);
	if (getenv("AILOG")) { diplo_set_log(dp, ailog, NULL); peace_set_log(pc, ailog, NULL); }
	if (getenv("MILLOG")) mil_set_log(mil, ailog, NULL);  /* the military's log (battles, sieges, the military AI) */
	if (getenv("PRICES")) {                              /* the market prices after a year */
		static const char *const gn[] = { "CLO", "COT", "FISH", "FURS", "GOLD", "GRAI", "IVOR", "METAL", "NAVS", "MINERAL", "ORIENT", "SALT", "SLAV", "SPIC", "SUG", "TOB", "WINE", "WOOL", "COFFEE", "TEA" };
		int i;
		econ_new_game(ec);
		run(ec, ev, mil, pc, dp, &day, 1);
		for (i = 0; i < 20; i++) printf("%s %ld  ", gn[i], econ_price_m(ec, world_intern(&w, gn[i])));
		printf("%c", 10);
		return 0;
	}
	if (mode[0] == 'l') {                                /* long run: 100 years, a summary every 10 */
		int y, c, live, units, majors;
		econ_new_game(ec); lead_spawn(g_tlead, &w, mil, 1); world_weather_month(&w); env_seed(12345); rev_seed(4711);
		for (y = 0; y < 10; y++) {
			run(ec, ev, mil, pc, dp, &day, 10);
			for (c = 0, live = 0; c < w.ncountry; c++) live += w.country[c].nprov_owned > 0;
			for (c = 0, units = 0; c < w.nland; c++) units += !w.land[c].dead;
			{ int rb = 0, rp = 0, k2; for (k2 = 0; k2 < w.nland; k2++) rb += !w.land[k2].dead && w.land[k2].owner == w.reb;
			  for (k2 = 1; k2 < w.nprov; k2++) rp += w.prov[k2].controller == w.reb && w.reb >= 0;
			  printf("%d: %d countries, %d wars, %d alliances, %d live units of %d slots (%d rebel armies, %d rebel-held), %d recruits waiting;", w.now.year, live, w.nwar, w.nally, units, w.nland, rb, rp, w.nbuild); }
			for (c = 0, majors = 0; c < w.ncountry && majors < 6; c++)
				if (w.country[c].major && w.country[c].nprov_owned > 0) { printf(" %s %d", w.country[c].tag, w.country[c].nprov_owned); majors++; }
			printf(" | peak battles %d/64 plans %d/800 generals %d/400 builds %d", g_peak[0], g_peak[1], g_peak[2], w.nbuild);
			memset(g_peak, 0, sizeof g_peak);
			printf("\n");
			fflush(stdout);
		}
		{ char pl[256]; snprintf(pl, sizeof pl, "%s%ssave_l.eug", dir, sep); save_game(pl, &w, ec, ev, mil, pc, dp); }   /* the end state, to inspect */
	} else if (load) {
		if (restore_game(pa, ec, ev, mil, pc, dp)) { printf("restore failed\n"); return 20; }
		if (save_game(pa2, &w, ec, ev, mil, pc, dp)) { printf("save A2 failed\n"); return 20; }
		printf("loaded %s at %d-%d-%d, saved %s\n", pa, w.now.year, w.now.month, w.now.day, pa2);
		run(ec, ev, mil, pc, dp, &day, getenv("TSDAYS") ? -atoi(getenv("TSDAYS")) : 2);
		if (save_game(py, &w, ec, ev, mil, pc, dp)) { printf("save Y failed\n"); return 20; }
		printf("saved %s at %d-%d-%d, %d wars\n", py, w.now.year, w.now.month, w.now.day, w.nwar);
	} else {
		econ_new_game(ec); lead_spawn(g_tlead, &w, mil, 1); world_weather_month(&w); env_seed(12345); rev_seed(4711);
		run(ec, ev, mil, pc, dp, &day, 3);
		if (save_game(pa, &w, ec, ev, mil, pc, dp)) { printf("save A failed\n"); return 20; }
		printf("saved %s at %d-%d-%d, %d wars\n", pa, w.now.year, w.now.month, w.now.day, w.nwar);
		run(ec, ev, mil, pc, dp, &day, getenv("TSDAYS") ? -atoi(getenv("TSDAYS")) : 2);
		if (save_game(px, &w, ec, ev, mil, pc, dp)) { printf("save X failed\n"); return 20; }
		printf("saved %s at %d-%d-%d, %d wars\n", px, w.now.year, w.now.month, w.now.day, w.nwar);
#ifdef AI_PROF
		{ int i; for (i = 0; i < 8; i++) printf(" p%d=%ld", i, ai_prof[i] / 1000); printf(" ms\n"); }
#endif
	}
	env_set_trade_offer(NULL, NULL); diplo_free(dp); peace_free(pc); mil_free(mil); econ_free(ec); events_free(ev);
	world_free(&w);
	return 0;
}
