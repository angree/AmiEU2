/* test_peace.c - 1419 for 6 years with the military and peace modules: the French royal army is sent
 * into English Gascony as in test_war, then war scores are logged every 6 months and every peace
 * (AI demands, white peace, automatic peace) is logged with its terms.
 * Host: test_peace <text.pak> <map.pak>   Amiga: PROGDIR:data, log file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "military.h"
#include "peace.h"
#include "calendar.h"

static World w;
static FILE *out;

static void lg(void *u, long today, const char *t)
{
	int y, m, d;
	(void)u;
	if (!strncmp(t, "  phase", 7)) return;          /* CRT detail is test_war's business */
	cal_date(today, &y, &m, &d);
	fprintf(out, "%04d-%02d-%02d %s\n", y, m, d + 1, t);
}

static int offer_cb(void *u, int war, int sender, int winner, const short *provs, int np, long gold_m)
{
	int i;
	(void)u;
	fprintf(out, "OFFER to the player in war %d: winner side %d, %d provinces", war, winner, np);
	for (i = 0; i < np; i++) fprintf(out, " %s", w.prov[provs[i]].name);
	fprintf(out, ", %ld ducats -> accepted\n", gold_m / 1000);
	return 1;
}

int main(int argc, char **argv)
{
	Pak t, map;
	char err[160];
	Military *mil;
	Peace *pc;
	long day, start;
	int i, fra, eng, a = -1, b = -1;
	out = stdout;
	if (argc < 3) { remove("PROGDIR:test_peace.log"); out = fopen("PROGDIR:test_peace.log", "w"); if (!out) return 20; }
	if (pak_open(&t, argc > 2 ? argv[1] : "PROGDIR:data/text.pak") || pak_open(&map, argc > 2 ? argv[2] : "PROGDIR:data/map.pak")) { fprintf(out, "packs\n"); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, "scenarios/the grand campaign.eug", err, sizeof err)) { fprintf(out, "%s\n", err); return 20; }
	start = cal_day(w.start.year, w.start.month, w.start.day);
	if (!(mil = mil_load(&w, &t, &map, 12345, err, sizeof err))) { fprintf(out, "%s\n", err); return 20; }
	if (!(pc = peace_new(&w, start, 777))) return 20;
	mil_set_log(mil, lg, NULL);
	peace_set_log(pc, lg, NULL);
	if (argc > 3) {                                     /* host only: test_peace t m TAG -> TAG is human, accepts offers */
		int h = world_country(&w, argv[3]);
		if (h >= 0) { w.country[h].human = 1; peace_set_offer(pc, offer_cb, NULL); fprintf(out, "%s is human\n", argv[3]); }
	}
	fra = world_country(&w, "FRA"); eng = world_country(&w, "ENG");
	for (i = 0; i < w.nland; i++)
		if (w.land[i].owner == fra && (a < 0 || w.land[i].infantry + w.land[i].cavalry > w.land[a].infantry + w.land[a].cavalry)) a = i;
	for (i = 0; i < w.nland; i++)
		if (w.land[i].owner == eng && w.prov[w.land[i].location].continent == w.prov[w.land[a].location].continent && (b < 0 || w.land[i].location != w.land[a].location)) b = i;
	if (a < 0 || b < 0) { fprintf(out, "armies not found\n"); return 20; }
	fprintf(out, "%d wars; order FRA -> %s: %ld days\n", w.nwar, w.prov[w.land[b].location].name, mil_order_move(mil, a, w.land[b].location, start));
	for (day = start; day < start + 6 * 360; day++) {
		if (day % 30 == 0) { mil_month(mil); peace_month(pc, day); }
		mil_day(mil, day);
		if (argc > 4 && day == start + 2 * 360) {          /* host only: test_peace t m TAG window -> the peace window's offers */
			int h = world_country(&w, argv[3]), k, wi;
			static short cand[256];
			for (wi = 0; wi < w.nwar; wi++) if (peace_side(&w, wi, h) && (w.war[wi].att[0] == h || w.war[wi].def[0] == h)) break;
			if (wi < w.nwar) {
				PeaceOffer o;
				int nc, r, sp = peace_side(&w, wi, h) == 1 ? peace_war_score(&w, wi) : -peace_war_score(&w, wi);
				for (k = 0; k < 2; k++) {
					nc = peace_candidates(pc, wi, h, k ? PO_OFFER : PO_DEMAND, cand, 256);
					fprintf(out, "window: war %d score %d, %s candidates %d:", wi, sp, k ? "offer" : "demand", nc);
					for (r = 0; r < nc && r < 8; r++) fprintf(out, " %s(%d)", w.prov[cand[r]].name, peace_prov_value(&w, cand[r]));
					fprintf(out, "%c", 10);
				}
				memset(&o, 0, sizeof o);
				o.mode = sp >= 0 ? PO_DEMAND : PO_OFFER;
				nc = peace_candidates(pc, wi, h, o.mode, cand, 256);
				if (nc > 0) { o.prov[0] = cand[0]; o.nprov = 1; }
				o.gold_m = 50000; o.access = 1;
				fprintf(out, "window: can annex %d; propose mode %d value %d -> %d%c", peace_can_annex(pc, wi, h), o.mode, peace_offer_value(&w, &o), r = peace_player_propose(pc, wi, h, &o, day), 10);
				fprintf(out, "window: %d wars now; access flag %d%c", w.nwar, r == PEACE_ACCEPTED, 10);
			}
		}
		if ((day - start) % 180 == 0)
			for (i = 0; i < w.nwar; i++)
				fprintf(out, "  day %ld war %d %s vs %s: score %d\n", day - start, i, w.country[w.war[i].att[0]].tag, w.country[w.war[i].def[0]].tag, peace_war_score(&w, i));
	}
	fprintf(out, "after 6 years: %d wars, FRA %d provinces, ENG %d provinces, FRA badboy %ld.%03ld\n", w.nwar,
	        w.country[fra].nprov_owned, w.country[eng].nprov_owned, w.country[fra].badboy_m / 1000, w.country[fra].badboy_m % 1000);
	peace_free(pc);
	mil_free(mil);
	world_free(&w);
	if (out != stdout) fclose(out);
	return 0;
}
