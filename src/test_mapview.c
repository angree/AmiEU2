/* test_mapview.c - host: renders 640x480 views of the 1419 political map at zoom 1..3 to PPM,
 * with units. test_mapview <text.pak> <map.pak> <outprefix> <gfx.pak> [cx cy] */
#include <stdio.h>
#include <stdlib.h>
#include "mapview.h"
#include "units.h"
#include "eui.h"
#include "military.h"

static World w;
int mil_besieger(const Military *m, int prov) { (void)m; (void)prov; return -1; }   /* no military here */

static int g_sh = -1;
static void eco(void *u, const MapView *v, unsigned char *s, unsigned char *sh, int n)   /* economic-like: light green at shade g_sh */
{
	int i;
	(void)u; (void)v;
	for (i = 1; i < n; i++) if (w.prov[i].terrain < 5) { s[i] = 17; sh[i] = (unsigned char)g_sh; }
}

int main(int argc, char **argv)
{
	Pak text, map, gfx;
	static unsigned char inv[EUI_INV_SIZE];
	char err[160], name[256];
	static unsigned char fb[640 * 480];
	MapView *v;
	int z;
	if (argc < 5) return 2;
	if (pak_open(&text, argv[1]) || pak_open(&map, argv[2]) || pak_open(&gfx, argv[4])) { printf("packs\n"); return 1; }
	if (world_load_db(&w, &text, err, sizeof err) || world_load_scenario(&w, &text, "scenarios/the grand campaign.eug", err, sizeof err)) { printf("%s\n", err); return 1; }
	if (!(v = mv_create(&text, &map, &w, 0, err, sizeof err))) { printf("%s\n", err); return 1; }
	if (argc > 7) { g_sh = atoi(argv[7]); mv_set_color_fn(v, eco, NULL); }
	eui_build_inverse(mv_palette(v), 256, inv);
	units_init(&text, &gfx, inv);
	for (z = 1; z <= 3; z++) {
		const unsigned char *pal = mv_palette(v);
		FILE *f;
		int i, n, u;
		mv_set(v, argc > 6 ? atol(argv[5]) : 9500, argc > 6 ? atol(argv[6]) : 1900, z);   /* optional centre */
		n = mv_draw(v, fb, 640, 640, 480);
		u = units_draw(&w, v, fb, 640, 640, 480, 0);
		snprintf(name, sizeof name, "%s_z%d.ppm", argv[3], z);
		f = fopen(name, "wb");
		fprintf(f, "P6 640 480 255\n");
		for (i = 0; i < 640 * 480; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
		fclose(f);
		printf("zoom %d: %d tiles decoded, %d unit sprites, province at centre %d\n", z, n, u, mv_province_at(v, 320, 240, 640, 480));
	}
	mv_destroy(v);
	world_free(&w);
	pak_close(&text); pak_close(&map); pak_close(&gfx);
	return 0;
}
