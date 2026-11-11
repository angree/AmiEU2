/* test_script.c - parses EVERY script/CSV file of text.pak and prints counts plus a checksum of the
 * resulting trees. Built for the host and for the Amiga: the two outputs must be identical line for
 * line (a difference on the same input is the compiler). Usage: test_script <text.pak> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pak.h"
#include "script.h"

static unsigned long hash_str(unsigned long h, const char *s)
{
	if (!s) return h * 31 + 7;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h;
}

static unsigned long walk(const SNode *n, unsigned long h, unsigned long *count, long *msum)
{
	for (; n; n = n->next) {
		(*count)++;
		h = hash_str(hash_str(h, n->key), n->val);
		if (n->val && ((n->val[0] >= '0' && n->val[0] <= '9') || n->val[0] == '-')) *msum += parse_milli(n->val) & 0xFFFF;
		if (n->child) h = walk(n->child, h * 17, count, msum);
	}
	return h;
}

static int is_csv(const char *name) { size_t l = strlen(name); return l > 4 && !strcmp(name + l - 4, ".csv"); }

int main(int argc, char **argv)
{
	Pak p;
	unsigned long i, files = 0, nodes = 0, bad = 0, csvrows = 0, total = 0, h = 5381, peak = 0;
	long msum = 0;
	const char *path = argc > 1 ? argv[1] : "PROGDIR:data/text.pak";
	FILE *out = stdout;
	if (argc <= 1) { remove("PROGDIR:test_script.log"); out = fopen("PROGDIR:test_script.log", "w"); if (!out) return 20; }
	if (pak_open(&p, path)) { fprintf(out, "cannot open %s\n", path); return 20; }
	/* self-test of the number parser first */
	{
		static const char *in[] = { "0.083333", "-1.5", "12", "0.0005", "-0.0004", "3,25", "+7.1239", "99" };
		static const long want[] = { 83, -1500, 12000, 1, 0, 3250, 7124, 99000 };
		int k;
		for (k = 0; k < 8; k++) if (parse_milli(in[k]) != want[k]) { fprintf(out, "parse_milli(%s) = %ld, want %ld\n", in[k], parse_milli(in[k]), want[k]); bad++; }
	}
	for (i = 0; i < p.count; i++) {
		const char *name = p.ent[i].name;
		unsigned long sz;
		char *buf;
		if (!strcmp(name, "readme.txt") || !strcmp(name, "db/event_manual.txt") || !strcmp(name, "scenarioeditor.txt")) continue;
		if (strstr(name, ".spr") == NULL && strstr(name, ".txt") == NULL && strstr(name, ".inc") == NULL &&
		    strstr(name, ".eug") == NULL && strstr(name, ".ai") == NULL && !is_csv(name)) continue;
		buf = (char *)pak_load(&p, name, &sz);
		if (!buf) { fprintf(out, "load failed: %s\n", name); bad++; continue; }
		files++; total += sz;
		if (is_csv(name)) {
			char *cur = buf, *line, *f[128];
			while ((line = csv_line(&cur)) != NULL) { int n = csv_split(line, f, 128); csvrows++; h = hash_str(h * 3 + (unsigned long)n, f[0]); }
		} else {
			Arena *a = arena_new(65536);
			char err[128];
			SNode *root = script_parse(buf, sz, a, err, sizeof err);
			if (!root) { fprintf(out, "%s: %s\n", name, err); bad++; }
			else h = walk(root->child, h, &nodes, &msum);
			if (arena_used(a) > peak) peak = arena_used(a);
			arena_free(a);
		}
		free(buf);
	}
	fprintf(out, "files %lu (%lu bytes), nodes %lu, csv rows %lu, failures %lu\n", files, total, nodes, csvrows, bad);
	/* 32-bit masks: long is 64-bit on the host and 32-bit on the Amiga, the lines must match */
	fprintf(out, "checksum %08lx milli-sum %08lx repairs %lu\n", h & 0xFFFFFFFFUL, (unsigned long)msum & 0xFFFFFFFFUL, script_warnings());
	fprintf(out, "peak arena %lu bytes\n", peak);
	pak_close(&p);
	if (out != stdout) fclose(out);
	return bad ? 5 : 0;
}
