/* bink_test - developer tool, not part of eu2conv: decodes a Bink video with bink.c and
 * writes chosen frames as PPM (convert to PNG on the host).
 *
 *   bink_test <binkplay.exe> <video.bik> <out prefix> <frame> [<frame> ...]
 *       writes <out prefix>_<frame>.ppm
 *   bink_test <binkplay.exe> <video.bik> <out prefix> -every <n>
 *       writes every n-th frame, quarter size, for picking stills */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bink.h"

static unsigned char *load(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *p;
	long l;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
	p = malloc((size_t)l + 1);
	if (fread(p, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(p); return NULL; }
	fclose(f);
	*n = (size_t)l;
	return p;
}

static void write_ppm(const char *path, const unsigned char *rgb, int w, int h, int sub)
{
	FILE *f = fopen(path, "wb");
	int x, y;
	if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
	fprintf(f, "P6\n%d %d\n255\n", w / sub, h / sub);
	for (y = 0; y + sub <= h; y += sub)
		for (x = 0; x + sub <= w; x += sub) fwrite(rgb + ((size_t)y * w + x) * 3, 1, 3, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	static BinkTables t;
	unsigned char *exe, *bik, *rgb;
	size_t ne, nb;
	char why[200], path[1024];
	Bink *k;
	int i, every = 0, last = 0, errors = 0;
	if (argc < 5) { fprintf(stderr, "usage: bink_test <binkplay.exe> <video.bik> <prefix> <frame>... | -every n\n"); return 2; }
	exe = load(argv[1], &ne); bik = load(argv[2], &nb);
	if (!exe || !bik) { fprintf(stderr, "cannot read input\n"); return 1; }
	if (bink_tables_from_player(exe, ne, &t, why, sizeof why)) { fprintf(stderr, "%s\n", why); return 1; }
	k = bink_open(bik, nb, &t, why, sizeof why);
	if (!k) { fprintf(stderr, "%s\n", why); return 1; }
	printf("%dx%d, %d frames, %d/%d fps\n", bink_width(k), bink_height(k), bink_frames(k), bink_fps_num(k), bink_fps_den(k));
	if (!strcmp(argv[4], "-every")) { every = atoi(argv[5]); last = bink_frames(k) - 1; }
	else for (i = 4; i < argc; i++) if (atoi(argv[i]) > last) last = atoi(argv[i]);
	rgb = malloc((size_t)bink_width(k) * bink_height(k) * 3);
	for (i = 0; i <= last; i++) {
		int want = 0, a;
		if (bink_decode_next(k, why, sizeof why) < 0) { if (errors++ < 10) fprintf(stderr, "%s\n", why); }
		if (every) want = i % every == 0;
		else for (a = 4; a < argc; a++) if (atoi(argv[a]) == i) want = 1;
		if (!want) continue;
		bink_rgb24(k, rgb);
		snprintf(path, sizeof path, "%s_%04d.ppm", argv[3], i);
		write_ppm(path, rgb, bink_width(k), bink_height(k), every ? 4 : 1);
	}
	printf("%d decode errors\n", errors);
	bink_close(k);
	return errors ? 1 : 0;
}
