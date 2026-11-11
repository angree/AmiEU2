/* test_lightmap.c - decodes a whole lightmap level; writes raw shade (1 B/px) and ids (u16 LE)
 * for comparison with tools/maptool.py, prints checksums and the decode time.
 * Host: test_lightmap <map.pak> <level> [outprefix]   Amiga: reads PROGDIR:test_lightmap.cfg-free defaults */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lightmap.h"

int main(int argc, char **argv)
{
	Pak p;
	Lightmap lm;
	int level = argc > 2 ? atoi(argv[2]) : 3, tx, ty, y;
	const char *path = argc > 1 ? argv[1] : "PROGDIR:data/map.pak";
	const char *pre = argc > 3 ? argv[3] : NULL;
	static unsigned short ids[1024];
	static unsigned char sh[1024];
	unsigned long hs = 0, hi = 0;
	FILE *fs = NULL, *fi = NULL, *out = stdout;
	unsigned char *rowsh = NULL, *rowid = NULL;
	clock_t c0;
	if (argc <= 1) { remove("PROGDIR:test_lightmap.log"); out = fopen("PROGDIR:test_lightmap.log", "w"); if (!out) return 20; }
	if (pak_open(&p, path)) { fprintf(out, "cannot open %s\n", path); return 20; }
	if (lm_load(&lm, &p, level)) { fprintf(out, "lm_load %d failed\n", level); return 20; }
	fprintf(out, "level %d: %d x %d tiles, %lu bytes\n", level, lm.tw, lm.th, lm.size);
	if (pre) {
		char n[256];
		snprintf(n, sizeof n, "%s_shade%d.raw", pre, level); fs = fopen(n, "wb");
		snprintf(n, sizeof n, "%s_ids%d.raw", pre, level); fi = fopen(n, "wb");
		rowsh = malloc((size_t)lm.tw * 32 * 32);
		rowid = malloc((size_t)lm.tw * 32 * 32 * 2);
	}
	c0 = clock();
	for (ty = 0; ty < lm.th; ty++) {
		for (tx = 0; tx < lm.tw; tx++) {
			int k;
			lm_tile(&lm, tx, ty, ids, sh);
			for (k = 0; k < 1024; k++) { hs = hs * 31 + sh[k]; hi = hi * 33 + ids[k]; }
			if (pre) for (y = 0; y < 32; y++) {
				size_t o = (size_t)y * lm.tw * 32 + (size_t)tx * 32;
				memcpy(rowsh + o, sh + y * 32, 32);
				for (k = 0; k < 32; k++) { rowid[2 * (o + k)] = (unsigned char)ids[y * 32 + k]; rowid[2 * (o + k) + 1] = (unsigned char)(ids[y * 32 + k] >> 8); }
			}
		}
		if (pre) { fwrite(rowsh, 1, (size_t)lm.tw * 1024, fs); fwrite(rowid, 1, (size_t)lm.tw * 2048, fi); }
	}
	fprintf(out, "shade %08lx ids %08lx\n", hs & 0xFFFFFFFFUL, hi & 0xFFFFFFFFUL);
	fprintf(out, "decode+hash %ld tiles: %ld ms\n", (long)lm.tw * lm.th, (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC));
	{	/* phase timings without the per-pixel hash */
		static LmLeaf lv[1024];
		static unsigned char tab[72], pens[1024];
		static const unsigned char *pt[4096];
		long nl = 0;
		int k;
		for (k = 0; k < 72; k++) tab[k] = (unsigned char)k;
		for (k = 0; k < 4096; k++) pt[k] = tab;
		c0 = clock();
		for (ty = 0; ty < lm.th; ty++) for (tx = 0; tx < lm.tw; tx++) nl += lm_leaves(&lm, tx, ty, lv);
		fprintf(out, "leaves only: %ld leaves, %ld ms\n", nl, (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC));
		c0 = clock();
		for (ty = 0; ty < lm.th; ty++) for (tx = 0; tx < lm.tw; tx++) lm_tile_pens(&lm, tx, ty, pens, pt, 4096);
		fprintf(out, "tile_pens (cached leaves): %ld ms, flat px %lu, sloped px %lu\n", (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC), lm_flat_px, lm_slope_px);
	}
	if (fs) fclose(fs);
	if (fi) fclose(fi);
	lm_free(&lm);
	pak_close(&p);
	if (out != stdout) fclose(out);
	return 0;
}
