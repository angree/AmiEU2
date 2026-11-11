/* test_war.c - 1419: lists the scenario wars, sends the largest French army against the nearest
 * English army and logs movement, the battle (dice, CRT rows, losses) and sieges for 180 days.
 * Host: test_war <text.pak> <map.pak>   Amiga: PROGDIR:data, log file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "military.h"
#include "calendar.h"

static World w;
static FILE *out;

static void lg(void *u, long today, const char *t)
{
	int y, m, d;
	(void)u;
	cal_date(today, &y, &m, &d);
	fprintf(out, "%04d-%02d-%02d %s\n", y, m, d + 1, t);
}

int main(int argc, char **argv)
{
	Pak t, map;
	char err[160];
	Military *mil;
	long day, start;
	int i, fra, eng, a = -1, b = -1;
	out = stdout;
	if (argc < 3) { remove("PROGDIR:test_war.log"); out = fopen("PROGDIR:test_war.log", "w"); if (!out) return 20; }
	if (pak_open(&t, argc > 2 ? argv[1] : "PROGDIR:data/text.pak") || pak_open(&map, argc > 2 ? argv[2] : "PROGDIR:data/map.pak")) { fprintf(out, "packs\n"); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, "scenarios/the grand campaign.eug", err, sizeof err)) { fprintf(out, "%s\n", err); return 20; }
	start = cal_day(w.start.year, w.start.month, w.start.day);
	if (!(mil = mil_load(&w, &t, &map, 12345, err, sizeof err))) { fprintf(out, "%s\n", err); return 20; }
	mil_set_log(mil, lg, NULL);
	fprintf(out, "%d wars at the start\n", w.nwar);
	for (i = 0; i < w.nwar; i++) {
		int k;
		fprintf(out, "  war %d:", i);
		for (k = 0; k < w.war[i].natt; k++) fprintf(out, " %s", w.country[w.war[i].att[k]].tag);
		fprintf(out, "  vs ");
		for (k = 0; k < w.war[i].ndef; k++) fprintf(out, " %s", w.country[w.war[i].def[k]].tag);
		fprintf(out, "\n");
	}
	fra = world_country(&w, "FRA"); eng = world_country(&w, "ENG");
	fprintf(out, "FRA at war with ENG: %d\n", mil_at_war(&w, fra, eng));
	for (i = 0; i < w.nland; i++) {
		LandUnit *u = &w.land[i];
		if (u->owner == fra && (a < 0 || u->infantry + u->cavalry > w.land[a].infantry + w.land[a].cavalry)) a = i;
	}
	for (i = 0; i < w.nland; i++) {                      /* English army on the continent, any */
		LandUnit *u = &w.land[i];
		if (u->owner == eng && w.prov[u->location].continent == w.prov[w.land[a].location].continent && (b < 0 || u->location != w.land[a].location)) { b = i; }
	}
	if (a < 0 || b < 0) { fprintf(out, "armies not found\n"); return 20; }
	fprintf(out, "FRA army %s: %ld inf %ld cav in %s; ENG army %s: %ld inf %ld cav in %s\n",
	        w.land[a].name, w.land[a].infantry, w.land[a].cavalry, w.prov[w.land[a].location].name,
	        w.land[b].name, w.land[b].infantry, w.land[b].cavalry, w.prov[w.land[b].location].name);
	fprintf(out, "order: %ld days\n", mil_order_move(mil, a, w.land[b].location, start));
	for (day = start; day < start + 180; day++) {
		if (day % 30 == 0) mil_month(mil);
		mil_day(mil, day);
	}
	fprintf(out, "after 180 days: FRA %ld inf %ld cav (morale %ld) in %s %s; ENG %ld inf %ld cav (morale %ld) in %s %s\n",
	        w.land[a].infantry, w.land[a].cavalry, w.land[a].morale_m, w.prov[w.land[a].location].name, w.land[a].dead ? "DEAD" : "",
	        w.land[b].infantry, w.land[b].cavalry, w.land[b].morale_m, w.prov[w.land[b].location].name, w.land[b].dead ? "DEAD" : "");
	{	/* fleets: the first French fleet sails to the first English port */
		int f = -1, p, dest = -1;
		long d;
		for (i = 0; i < w.nnaval && f < 0; i++) if (w.naval[i].owner == fra) f = i;
		for (p = 1; p < w.nprov && dest < 0; p++) if (w.prov[p].owner == eng && w.prov[p].terrain < 5 && w.prov[p].port_sea > 0) dest = p;
		if (f >= 0 && dest > 0) {
			fprintf(out, "fleet %s (%d warships) in %s orders to %s: ", w.naval[f].name, w.naval[f].warships, w.prov[w.naval[f].location].name, w.prov[dest].name);
			d = mil_order_fleet(mil, f, dest, start + 180);
			fprintf(out, "%ld days, %d steps\n", d, w.naval[f].npath);
			for (day = start + 180; day < start + 300; day++) mil_day(mil, day);
			fprintf(out, "after 120 days the fleet is in %s\n", w.prov[w.naval[f].location].name);
			{	/* an English fleet sails into the zone off Meath: a naval battle */
				int e = -1, z = w.prov[dest].port_sea;
				for (i = 0; i < w.nnaval && e < 0; i++) if (w.naval[i].owner == eng && w.naval[i].warships > 0) e = i;
				mil_order_fleet(mil, f, z, start + 300);
				if (e >= 0) fprintf(out, "ENG fleet %s (%d warships) in %s to %s: %ld days\n", w.naval[e].name, w.naval[e].warships,
				                    w.prov[w.naval[e].location].name, w.prov[z].name, mil_order_fleet(mil, e, z, start + 300));
				for (day = start + 300; day < start + 420; day++) mil_day(mil, day);
				fprintf(out, "FRA fleet %d/%d/%d in %s; ENG fleet %d/%d/%d in %s\n", w.naval[f].warships, w.naval[f].galleys, w.naval[f].transports,
				        w.prov[w.naval[f].location].name, e >= 0 ? w.naval[e].warships : 0, e >= 0 ? w.naval[e].galleys : 0,
				        e >= 0 ? w.naval[e].transports : 0, e >= 0 ? w.prov[w.naval[e].location].name : "-");
			}
		}
	}
	{	/* sea transport: a French army in Vendee boards the French fleet off the coast, sails to the
		 * zone off an English province and lands there */
		int f = -1, u = -1, vendee = -1, sea, land = -1, rc, n;
		long d;
		for (i = 1; i < w.nprov && vendee < 0; i++) if (w.prov[i].name && !strcmp(w.prov[i].name, "Vend\xe9" "e")) vendee = i;
		for (i = 0; i < w.nnaval && f < 0; i++) if (w.naval[i].owner == fra && w.naval[i].warships + w.naval[i].galleys + w.naval[i].transports > 0) f = i;
		for (i = 0; i < w.nland && u < 0; i++) if (w.land[i].owner == fra && !w.land[i].dead) u = i;
		if (vendee > 0 && f >= 0 && u >= 0) {
			sea = w.prov[vendee].port_sea;
			w.country[fra].human = 1;                                          /* no AI orders for the test fleet */
			w.land[u].location = (short)vendee; w.land[u].npath = 0;           /* test set-up: a small army on the coast */
			w.land[u].infantry = 5000; w.land[u].cavalry = 1000; w.land[u].artillery = 0;
			d = mil_order_fleet(mil, f, sea, start + 420);
			for (day = start + 420; day < start + 520 && w.naval[f].location != sea; day++) mil_day(mil, day);
			fprintf(out, "transport: fleet %d/%d/%d in %s (%ld days), capacity %ld, army weight %ld\n", w.naval[f].warships, w.naval[f].galleys,
			        w.naval[f].transports, w.prov[w.naval[f].location].name, d, mil_fleet_capacity(&w.naval[f]), mil_army_weight(&w.land[u]));
			rc = mil_embark(mil, u, f);
			fprintf(out, "embark: %d, aboard %d, load %ld\n", rc, w.land[u].dead == LAND_ABOARD, mil_fleet_load(&w, f));
			n = mil_disembark(mil, f, vendee);
			fprintf(out, "disembark into %s: %d unit(s), army in %s, dead %d\n", w.prov[vendee].name, n, w.prov[w.land[u].location].name, w.land[u].dead);
			w.land[u].cavalry = 5000;                                           /* too heavy now: 5000 + 2 x 5000 > 9000 */
			fprintf(out, "embark heavier army: %d (expected %d)\n", mil_embark(mil, u, f), MIL_RC_FULL);
			w.land[u].cavalry = 1000;
			rc = mil_embark(mil, u, f);
			fprintf(out, "embark again: %d\n", rc);
			for (i = 1; i < w.nprov && land < 0; i++) if (w.prov[i].owner == eng && w.prov[i].terrain < 5 && w.prov[i].port_sea > 0 && w.prov[i].port_sea != sea) land = i;
			if (land > 0) {
				d = mil_order_fleet(mil, f, w.prov[land].port_sea, day);
				for (n = 0; n < 200 && w.naval[f].location != w.prov[land].port_sea; n++, day++) mil_day(mil, day);
				fprintf(out, "sailed %ld days to %s; the army is in %s (aboard %d)\n", d, w.prov[w.naval[f].location].name, w.prov[w.land[u].location].name, w.land[u].dead == LAND_ABOARD);
				n = mil_disembark(mil, f, land);
				fprintf(out, "disembark into %s: %d unit(s); army in %s, dead %d\n", w.prov[land].name, n, w.prov[w.land[u].location].name, w.land[u].dead);
			}
		} else fprintf(out, "transport: set-up not found\n");
	}
	mil_free(mil);
	world_free(&w);
	if (out != stdout) fclose(out);
	return 0;
}
