/* test_world.c - loads the DB and every scenario, prints a summary and a checksum.
 * Host and Amiga outputs must match line for line. Usage: test_world [text.pak] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "script.h"

static World w;   /* large: static, not on the stack */

int main(int argc, char **argv)
{
	Pak t;
	char err[160];
	unsigned long i;
	int bad = 0;
	const char *path = argc > 1 ? argv[1] : "PROGDIR:data/text.pak";
	FILE *out = stdout;
	if (argc <= 1) { remove("PROGDIR:test_world.log"); out = fopen("PROGDIR:test_world.log", "w"); if (!out) return 20; }
	if (pak_open(&t, path)) { fprintf(out, "cannot open %s\n", path); return 20; }
	for (i = 0; i < t.count; i++) {
		const char *name = t.ent[i].name;
		size_t l = strlen(name);
		short k, top[5] = { -1, -1, -1, -1, -1 };
		unsigned long sum = 0;
		int j, owned = 0;
		if (strncmp(name, "scenarios/", 10) || l < 5 || strcmp(name + l - 4, ".eug") || strstr(name, "template")) continue;
		if (world_load_db(&w, &t, err, sizeof err)) { fprintf(out, "db: %s\n", err); bad++; break; }
		if (world_load_scenario(&w, &t, name, err, sizeof err)) { fprintf(out, "%s: %s\n", name, err); bad++; world_free(&w); continue; }
		for (k = 0; k < w.nprov; k++) {
			if (w.prov[k].owner >= 0) owned++;
			sum = sum * 31 + (unsigned long)(w.prov[k].owner + 7) + (unsigned long)w.prov[k].population;
		}
		for (k = 0; k < w.ncountry; k++) {
			sum = sum * 17 + (unsigned long)w.country[k].treasury_m + (unsigned long)w.country[k].nprov_owned;
			for (j = 0; j < 5; j++)
				if (top[j] < 0 || w.country[k].nprov_owned > w.country[top[j]].nprov_owned) {
					memmove(top + j + 1, top + j, (4 - j) * sizeof top[0]); top[j] = k; break;
				}
		}
		for (j = 0; j < w.nland; j++) sum = sum * 3 + (unsigned long)w.land[j].infantry;
		fprintf(out, "%s \"%s\" %d.%d.%d-%d: provinces %d (owned %d), countries %d, armies %d, fleets %d, names %d, repairs %lu\n",
		        name, w.scenario_name, w.start.year, w.start.month, w.start.day, w.end.year, w.nprov, owned,
		        w.ncountry, w.nland, w.nnaval, w.nnames, w.repairs);
		fprintf(out, "   largest:");
		for (j = 0; j < 5; j++) if (top[j] >= 0) fprintf(out, " %s %d", w.country[top[j]].tag, w.country[top[j]].nprov_owned);
		fprintf(out, "   checksum %08lx\n", sum & 0xFFFFFFFFUL);
		world_free(&w);
	}
	pak_close(&t);
	fprintf(out, "done, %d failures\n", bad);
	if (out != stdout) fclose(out);
	return bad ? 5 : 0;
}
