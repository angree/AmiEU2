/* innodump - developer tool: prints the data entries of an Inno 5.5.0u installer.
 * innodump setup.exe            -> list
 * innodump setup.exe verify     -> also decompresses every chunk and checks every SHA-1 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inno.h"
#include "sha1.h"

typedef struct { InnoSetup *s; uint32_t chunk; Sha1 *ctx; int *done; uint32_t *idx; uint32_t n; } V;

static int vsink(void *u, uint64_t pos, const unsigned char *p, size_t n)
{
	V *v = u;
	uint32_t k;
	for (k = 0; k < v->n; k++) {
		InnoData *d = &v->s->data[v->idx[k]];
		uint64_t a = d->file_offset, b = d->file_offset + d->file_size;
		uint64_t lo = pos > a ? pos : a, hi = pos + n < b ? pos + n : b;
		if (lo < hi) sha1_update(&v->ctx[k], p + (lo - pos), (size_t)(hi - lo));
	}
	return 0;
}

int main(int argc, char **argv)
{
	InnoSetup s;
	char err[256];
	uint32_t i;
	if (argc < 2) { fprintf(stderr, "usage: innodump setup.exe [verify]\n"); return 2; }
	if (inno_open(&s, argv[1], err, sizeof err)) { fprintf(stderr, "error: %s\n", err); return 1; }
	printf("version \"%s\" header@%u data@%u entries %u block1 %u bytes\n", s.version, s.header_offset,
	       s.data_offset, s.ndata, (unsigned)s.block1_len);
	for (i = 0; i < s.ndata; i++) {
		InnoData *d = &s.data[i];
		int k;
		printf("%5u chunk %10u csize %10llu off %10llu size %10llu flags %04x ", i, d->chunk_offset,
		       (unsigned long long)d->chunk_size, (unsigned long long)d->file_offset,
		       (unsigned long long)d->file_size, d->flags);
		for (k = 0; k < 20; k++) printf("%02x", d->sha1[k]);
		printf("\n");
	}
	if (argc > 2 && !strcmp(argv[2], "verify")) {
		uint32_t bad = 0, good = 0;
		uint32_t *idx = malloc(s.ndata * 4);
		Sha1 *ctx = malloc(s.ndata * sizeof(Sha1));
		for (i = 0; i < s.ndata; i++) {
			V v; uint32_t j, n = 0; uint64_t need = 0;
			int first = 1;
			for (j = 0; j < i; j++) if (s.data[j].chunk_offset == s.data[i].chunk_offset) first = 0;
			if (!first) continue;
			for (j = 0; j < s.ndata; j++) if (s.data[j].chunk_offset == s.data[i].chunk_offset) {
				idx[n] = j; sha1_init(&ctx[n]); n++;
				if (s.data[j].file_offset + s.data[j].file_size > need) need = s.data[j].file_offset + s.data[j].file_size;
			}
			v.s = &s; v.chunk = s.data[i].chunk_offset; v.ctx = ctx; v.idx = idx; v.n = n;
			if (inno_read_chunk(&s, s.data[i].chunk_offset, s.data[i].chunk_size, need, vsink, &v, err, sizeof err)) {
				fprintf(stderr, "chunk %u: %s\n", s.data[i].chunk_offset, err); bad += n; continue;
			}
			for (j = 0; j < n; j++) {
				unsigned char h[20];
				sha1_final(&ctx[j], h);
				if (memcmp(h, s.data[idx[j]].sha1, 20)) {
					bad++;
					if (!(s.data[idx[j]].flags & 0x10)) fprintf(stderr, "SHA-1 mismatch entry %u (flags %04x)\n", idx[j], s.data[idx[j]].flags);
				} else good++;
			}
		}
		printf("verify: %u good, %u bad\n", good, bad);
	}
	inno_close(&s);
	return 0;
}
