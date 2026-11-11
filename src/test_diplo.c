/* test_diplo.c - 1419 for 20 years with military + peace + diplomacy, all AI: logs alliances read,
 * war declarations with who joined, peaces, and every year the number of wars, a few relations and a
 * checksum of all relations. Host: test_diplo <text.pak> <map.pak>   Amiga: PROGDIR:data, log file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "military.h"
#include "peace.h"
#include "diplo.h"
#include "calendar.h"
#include <time.h>
static clock_t t_mil, t_peace, t_dip;

static World w;
static FILE *out;

static void lg(void *u, long today, const char *t)
{
	int y, m, d;
	(void)u;
	if (!strncmp(t, "  phase", 7) || !strncmp(t, "battle of", 9) || !strncmp(t, "AI:", 3) || strstr(t, "retreats") || strstr(t, "wins against")) return;
	cal_date(today, &y, &m, &d);
	fprintf(out, "%04d-%02d-%02d %s\n", y, m, d + 1, t);
}

static void rel(const char *a, const char *b)
{
	int x = world_country(&w, a), y = world_country(&w, b);
	long r = world_rel(&w, x, y);
	fprintf(out, " %s-%s %s%ld.%03ld", a, b, r < 0 ? "-" : "", (r < 0 ? -r : r) / 1000, (r < 0 ? -r : r) % 1000);
}

int main(int argc, char **argv)
{
	Pak t, map;
	char err[160];
	Military *mil;
	Peace *pc;
	Diplo *dp;
	long day, start;
	int i, k, nmil = 0, nvas = 0, ndyn = 0;
	out = stdout;
	if (argc < 3) { remove("PROGDIR:test_diplo.log"); out = fopen("PROGDIR:test_diplo.log", "w"); if (!out) return 20; }
	if (pak_open(&t, argc > 2 ? argv[1] : "PROGDIR:data/text.pak") || pak_open(&map, argc > 2 ? argv[2] : "PROGDIR:data/map.pak")) { fprintf(out, "packs\n"); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, "scenarios/the grand campaign.eug", err, sizeof err)) { fprintf(out, "%s\n", err); return 20; }
	start = cal_day(w.start.year, w.start.month, w.start.day);
	for (i = 0; i < w.nally; i++) { nmil += w.ally[i].type == ALLY_MILITARY; nvas += w.ally[i].type == ALLY_VASSAL; ndyn += w.ally[i].type == ALLY_DYNASTIC; }
	fprintf(out, "%d alliances: %d military, %d vassal, %d dynastic; %d wars\n", w.nally, nmil, nvas, ndyn, w.nwar);
	for (i = 0; i < w.nally; i++) {
		fprintf(out, "  type %d until %d:", w.ally[i].type, w.ally[i].expiry.year);
		for (k = 0; k < w.ally[i].n; k++) fprintf(out, " %s", w.country[w.ally[i].member[k]].tag);
		fprintf(out, "\n");
	}
	fprintf(out, "start:"); rel("FRA", "ENG"); rel("FRA", "AUV"); rel("ENG", "SCO"); rel("CAS", "ARA"); rel("TUR", "BYZ"); fprintf(out, "\n");
	if (!(mil = mil_load(&w, &t, &map, 12345, err, sizeof err))) { fprintf(out, "%s\n", err); return 20; }
	if (!(pc = peace_new(&w, start, 777))) return 20;
	if (!(dp = diplo_new(&w, mil, pc, 4242))) return 20;
	fprintf(out, "ai files read: %d\n", diplo_load_ai(dp, &t));
	mil_set_log(mil, lg, NULL); peace_set_log(pc, lg, NULL); diplo_set_log(dp, lg, NULL);
	fprintf(out, "ceiling FRA-ENG %ld, CAS-GRA %ld\n", diplo_ceiling(dp, world_country(&w, "FRA"), world_country(&w, "ENG")) / 1000,
	        diplo_ceiling(dp, world_country(&w, "CAS"), world_country(&w, "GRA")) / 1000);
	for (day = start; day < start + 20 * 360; day++) {
		int y, m, dd;
		cal_date(day, &y, &m, &dd);
		w.now.year = (short)y; w.now.month = (unsigned char)m; w.now.day = (unsigned char)dd;
		if (dd == 0) { clock_t c0 = clock(), c1, c2; mil_month(mil); peace_month(pc, day); c1 = clock(); diplo_month(dp, day); c2 = clock(); t_peace += c1 - c0; t_dip += c2 - c1; }
		{ clock_t c0 = clock(); mil_day(mil, day); t_mil += clock() - c0; c0 = clock(); diplo_day(dp, day); t_dip += clock() - c0; }
		if ((day - start) % 360 == 359) {
			unsigned long sum = 0;
			long j, nrel = (long)MAX_COUNTRY * (MAX_COUNTRY - 1) / 2;
			for (j = 0; j < nrel; j++) sum = (sum * 31 + (unsigned long)w.rel_m[j]) & 0xFFFFFFFFUL;
			fprintf(out, "== %d: %d wars, %d alliances, rel sum %08lx;", y, w.nwar, w.nally, sum);
			rel("FRA", "ENG"); rel("FRA", "AUV"); rel("ENG", "SCO"); rel("CAS", "ARA"); rel("TUR", "BYZ");
			fprintf(out, "\n");
		}
	}
	for (i = 0; i < w.ncountry; i++)
		if (w.country[i].major && w.country[i].nprov_owned > 0)
			fprintf(out, "  %s %d provinces, stability %d, badboy %ld.%03ld, treasury %ld\n", w.country[i].tag, w.country[i].nprov_owned,
			        w.country[i].stability, w.country[i].badboy_m / 1000, w.country[i].badboy_m % 1000, w.country[i].treasury_m / 1000);
	{ long p[6]; mil_prof(p); fprintf(out, "mil: move %ld, battles %ld, lists %ld, sieges %ld, ai %ld\n", p[0], p[1], p[2], p[3], p[4]); }
	fprintf(out, "time: military %ld ms, peace %ld ms, diplomacy %ld ms\n", (long)(t_mil * 1000 / CLOCKS_PER_SEC), (long)(t_peace * 1000 / CLOCKS_PER_SEC), (long)(t_dip * 1000 / CLOCKS_PER_SEC));
	{	/* the player's actions: annex a vassal at peace (relation forced to 200), offer a trade agreement */
		int i2, lord = -1, v = -1, st, before;
		long today = start + 50L * 360;
		for (i2 = 0; i2 < w.nally && lord < 0; i2++)
			if (w.ally[i2].type == ALLY_VASSAL && w.ally[i2].n >= 2 && !mil_at_war(&w, w.ally[i2].member[0], -1)) {
				int a = w.ally[i2].member[0], b = w.ally[i2].member[1], k, war = 0;
				for (k = 0; k < w.nwar; k++) { int j; for (j = 0; j < w.war[k].natt; j++) if (w.war[k].att[j] == a || w.war[k].att[j] == b) war = 1; for (j = 0; j < w.war[k].ndef; j++) if (w.war[k].def[j] == a || w.war[k].def[j] == b) war = 1; }
				if (!war && w.country[b].nprov_owned > 0) { lord = a; v = b; }
			}
		if (lord >= 0) {
			world_rel_set(&w, lord, v, 200000L);
			w.country[lord].diplomats_m = 3000;
			st = diplo_action_state(dp, lord, v, DA_ANNEX, today);
			before = w.country[lord].nprov_owned;
			fprintf(out, "annex: %s of %s, state %d (2 = possible)", w.country[v].tag, w.country[lord].tag, st);
			if (st == DS_ON) { int res = diplo_action(dp, lord, v, DA_ANNEX, today);
			                   fprintf(out, ", result %d, provinces %d -> %d, %s keeps %d", res, before, w.country[lord].nprov_owned, w.country[v].tag, w.country[v].nprov_owned); }
			fprintf(out, "\n");
		} else fprintf(out, "annex: no vassal pair at peace\n");
		{
			int a = world_country(&w, "CAS"), b = world_country(&w, "POR");
			if (a >= 0 && b >= 0) {
				world_rel_set(&w, a, b, 150000L); w.country[a].diplomats_m = 3000;
				fprintf(out, "trade agreement CAS-POR: state %d", diplo_action_state(dp, a, b, DA_OFFTRADE, today + 40));
				{ int res = diplo_action(dp, a, b, DA_OFFTRADE, today + 40);
				  fprintf(out, ", result %d, flags %d/%d\n", res, (w.dipflag[a][b] & DF_TRADEAGR) != 0, (w.dipflag[b][a] & DF_TRADEAGR) != 0); }
			}
		}
	}
	diplo_free(dp); peace_free(pc); mil_free(mil);
	world_free(&w);
	if (out != stdout) fclose(out);
	return 0;
}
