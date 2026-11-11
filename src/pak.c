/* pak.c - see pak.h */
#include <stdlib.h>
#include <string.h>
#include "pak.h"

int pak_open(Pak *p, const char *path)
{
	unsigned char h[20], *dir;
	unsigned long doff, dsize, i, pos = 0, npos = 0;
	memset(p, 0, sizeof *p);
	p->f = fopen(path, "rb");
	if (!p->f) return -1;
	if (fread(h, 1, 20, p->f) != 20 || memcmp(h, "EU2P", 4) || BE32(h + 4) != 1) { pak_close(p); return -2; }
	p->count = BE32(h + 8); doff = BE32(h + 12); dsize = BE32(h + 16);
	dir = malloc(dsize);
	p->ent = malloc(p->count * sizeof *p->ent + 1);
	p->names = malloc(dsize + 1);
	if (!dir || !p->ent || !p->names) { free(dir); pak_close(p); return -3; }
	if (fseek(p->f, (long)doff, SEEK_SET) || fread(dir, 1, dsize, p->f) != dsize) { free(dir); pak_close(p); return -4; }
	for (i = 0; i < p->count; i++) {
		unsigned l;
		if (pos + 9 > dsize) { free(dir); pak_close(p); return -5; }
		p->ent[i].off = BE32(dir + pos); p->ent[i].size = BE32(dir + pos + 4);
		l = dir[pos + 8];
		memcpy(p->names + npos, dir + pos + 9, l);
		p->names[npos + l] = 0;
		p->ent[i].name = p->names + npos;
		npos += l + 1;
		pos += 9 + l;
		pos += pos & 1;
	}
	free(dir);
	return 0;
}

void pak_close(Pak *p)
{
	if (p->f) fclose(p->f);
	free(p->ent); free(p->names);
	memset(p, 0, sizeof *p);
}

const PakEntry *pak_find(const Pak *p, const char *name)
{
	char key[256];
	unsigned i;
	long lo = 0, hi = (long)p->count - 1;
	for (i = 0; name[i] && i < sizeof key - 1; i++) {
		char c = name[i] == '\\' ? '/' : name[i];
		key[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	key[i] = 0;
	while (lo <= hi) {
		long mid = (lo + hi) / 2;
		int c = strcmp(p->ent[mid].name, key);
		if (c == 0) return &p->ent[mid];
		if (c < 0) lo = mid + 1; else hi = mid - 1;
	}
	return NULL;
}

unsigned char *pak_load(Pak *p, const char *name, unsigned long *size)
{
	const PakEntry *e = pak_find(p, name);
	unsigned char *b;
	if (!e) return NULL;
	b = malloc(e->size + 1);
	if (!b) return NULL;
	if (fseek(p->f, (long)e->off, SEEK_SET) || fread(b, 1, e->size, p->f) != e->size) { free(b); return NULL; }
	b[e->size] = 0;            /* text entries become C strings for free */
	if (size) *size = e->size;
	return b;
}
