/* test_mapmode.c - host: the map of Europe in every map mode, as PPM, plus a count of provinces per
 * scale so the modes can be checked without looking. test_mapmode <text.pak> <map.pak> <outprefix> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapview.h"
#include "mapmode.h"
#include "military.h"

static World w;

static int no_war(const World *ww, int a, int b) { (void)ww; (void)a; (void)b; return 0; }

int main(int argc, char **argv)
{
	Pak text, map;
	static unsigned char fb[576 * 400];
	char err[160], name[256];
	MapView *v;
	MapMode mm;
	int mode;
	if (argc < 4) return 2;
	if (pak_open(&text, argv[1]) || pak_open(&map, argv[2])) { printf("packs\n"); return 1; }
	if (world_load_db(&w, &text, err, sizeof err) || world_load_scenario(&w, &text, "scenarios/the grand campaign.eug", err, sizeof err)) { printf("%s\n", err); return 1; }
	if (!(v = mv_create(&text, &map, &w, 0, err, sizeof err))) { printf("%s\n", err); return 1; }
	memset(&mm, 0, sizeof mm);
	mm.w = &w; mm.player = world_country(&w, "FRA"); mm.focus = -1; mm.at_war = no_war;
	for (mode = 0; mode < MM_N; mode++) {
		FILE *f;
		const unsigned char *pal = mv_palette(v);
		int i;
		mapmode_set(v, &mm, mode);
		mv_set(v, 9800, 1900, 3);
		mv_draw(v, fb, 576, 576, 400);
		snprintf(name, sizeof name, "%s_%s.ppm", argv[3], mapmode_name(mode));
		f = fopen(name, "wb");
		if (!f) return 1;
		fprintf(f, "P6 576 400 255\n");
		for (i = 0; i < 576 * 400; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
		fclose(f);
		{	/* a fingerprint: pixels per light-level-4 pen of each scale */
			long cnt[26];
			int k, used = 0;
			memset(cnt, 0, sizeof cnt);
			for (i = 0; i < 576 * 400; i++) if (fb[i] >= MV_PEN_SCALES && fb[i] < MV_PEN_SCALES + 26 * 8) cnt[(fb[i] - MV_PEN_SCALES) / 8]++;
			for (k = 0; k < 26; k++) if (cnt[k]) used++;
			printf("%-12s %d scales on screen\n", mapmode_name(mode), used);
		}
	}
	mv_destroy(v);
	return 0;
}
