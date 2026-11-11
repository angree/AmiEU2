/* text.c - see text.h. The csv buffer is kept and split in place; an index sorted by key
 * (ASCII case-insensitive) is binary-searched. */
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "script.h"

typedef struct { const char *k, *v; } TEnt;
static char *g_buf;
static TEnt *g_ent;
static int g_n;

static int icmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		int x = (unsigned char)*a, y = (unsigned char)*b;
		if (x >= 'a' && x <= 'z') x -= 32;
		if (y >= 'a' && y <= 'z') y -= 32;
		if (x != y || !x) return x - y;
	}
}

static int ecmp(const void *a, const void *b) { return icmp(((const TEnt *)a)->k, ((const TEnt *)b)->k); }

int text_load(Pak *t)
{
	unsigned long sz;
	char *cur, *line, *f[3];
	int cap;
	text_free();
	if (!(g_buf = (char *)pak_load(t, "config/text.csv", &sz))) return -1;
	cap = (int)(sz / 16) + 16;
	g_ent = malloc(sizeof *g_ent * cap);
	if (!g_ent) return -2;
	cur = g_buf;
	while ((line = csv_line(&cur)) != NULL) {
		int n = csv_split(line, f, 3);
		if (n < 2 || !f[0][0] || f[0][0] == '#') continue;
		if (g_n == cap) break;
		g_ent[g_n].k = f[0]; g_ent[g_n].v = f[1]; g_n++;
	}
	qsort(g_ent, g_n, sizeof *g_ent, ecmp);
	return 0;
}

void text_free(void) { free(g_buf); free(g_ent); g_buf = NULL; g_ent = NULL; g_n = 0; }
int text_count(void) { return g_n; }

const char *text_get(const char *key)
{
	int lo = 0, hi = g_n - 1;
	if (!key) return "";
	while (lo <= hi) {
		int mid = (lo + hi) / 2, c = icmp(g_ent[mid].k, key);
		if (!c) return g_ent[mid].v;                         /* an empty entry is empty (TUTORIAL_3_C2_NAME), not its key */
		if (c < 0) lo = mid + 1; else hi = mid - 1;
	}
	return key;
}
