/* inno.c - see inno.h. */
#include <stdlib.h>
#include <string.h>
#include "inno.h"
#include "third_party/LzmaDec.h"
#include "third_party/Lzma2Dec.h"

/* ---------------------------------------------------------------- CRC-32 */
static uint32_t crc_tab[256];
static int crc_ready;
uint32_t crc32_buf(uint32_t crc, const unsigned char *p, size_t n)
{
	if (!crc_ready) {
		uint32_t i, k, c;
		for (i = 0; i < 256; i++) {
			c = i;
			for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			crc_tab[i] = c;
		}
		crc_ready = 1;
	}
	crc = ~crc;
	while (n--) crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

static uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const unsigned char *p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

static void *sz_alloc(ISzAllocPtr p, size_t n) { (void)p; return malloc(n); }
static void  sz_free(ISzAllocPtr p, void *a)   { (void)p; free(a); }
static const ISzAlloc g_alloc = { sz_alloc, sz_free };

#define ERR(...) do { if (err) snprintf(err, errlen, __VA_ARGS__); return -1; } while (0)

/* ------------------------------------------------- loader offset table */
static const unsigned char LDR_MAGIC[12] = { 'r','D','l','P','t','S',0xCD,0xE6,0xD7,0x7B,0x0B,0x2A };

static int find_loader(InnoSetup *s, char *err, int errlen)
{
	enum { WIN = 1 << 20 };
	unsigned char *buf = malloc(WIN + 64);
	long pos = 0;
	size_t got;
	if (!buf) ERR("out of memory");
	fseek(s->f, 0, SEEK_SET);
	while ((got = fread(buf, 1, WIN + 64, s->f)) >= 44 && pos < (64L << 20)) {
		size_t i;
		for (i = 0; i + 44 <= got; i++) {
			if (buf[i] == 'r' && memcmp(buf + i, LDR_MAGIC, 12) == 0) {
				const unsigned char *t = buf + i;
				uint32_t want = le32(t + 40);
				if (crc32_buf(0, t, 40) != want || le32(t + 12) != 1) continue;
				s->header_offset = le32(t + 32);   /* rev@12, ?@16, exe@20, exe_size@24, exe_crc@28 */
				s->data_offset   = le32(t + 36);
				free(buf);
				return 0;
			}
		}
		pos += WIN;
		fseek(s->f, pos, SEEK_SET);
	}
	free(buf);
	ERR("not an Inno Setup 5.1.5+ installer (no loader table)");
}

/* ------------------------------------------- one CRC-framed header block */
static int read_block(InnoSetup *s, unsigned char **out, size_t *outlen, char *err, int errlen)
{
	unsigned char h[9], *raw, *pay;
	uint32_t stored, left, plen = 0;
	if (fread(h, 1, 9, s->f) != 9) ERR("truncated block header");
	if (crc32_buf(0, h + 4, 5) != le32(h)) ERR("block header CRC mismatch");
	stored = le32(h + 4);
	raw = malloc(stored);
	pay = malloc(stored);
	if (!raw || !pay) ERR("out of memory");
	if (fread(raw, 1, stored, s->f) != stored) ERR("truncated block");
	for (left = stored; left > 0; ) {
		uint32_t seg;
		const unsigned char *p = raw + (stored - left);
		if (left < 5) ERR("bad block framing");
		seg = left - 4 > 4096 ? 4096 : left - 4;
		if (crc32_buf(0, p + 4, seg) != le32(p)) ERR("block data CRC mismatch");
		memcpy(pay + plen, p + 4, seg);
		plen += seg;
		left -= seg + 4;
	}
	free(raw);
	if (!h[8]) { *out = pay; *outlen = plen; return 0; }
	{	/* LZMA1: 5 property bytes, then a raw stream of unknown length */
		CLzmaDec d;
		size_t cap = plen * 4 + 65536, have = 0;
		unsigned char *o = malloc(cap);
		SizeT inpos = 5;
		LzmaDec_Construct(&d);
		if (plen < 5 || LzmaDec_Allocate(&d, pay, 5, &g_alloc) != SZ_OK) ERR("bad LZMA properties in header block");
		LzmaDec_Init(&d);
		for (;;) {
			SizeT dl, sl = plen - inpos;
			ELzmaStatus st;
			if (cap - have < 65536) { cap *= 2; o = realloc(o, cap); }
			dl = cap - have;
			if (LzmaDec_DecodeToBuf(&d, o + have, &dl, pay + inpos, &sl, LZMA_FINISH_ANY, &st) != SZ_OK) {
				LzmaDec_Free(&d, &g_alloc); ERR("LZMA error in header block");
			}
			have += dl; inpos += sl;
			if (st == LZMA_STATUS_FINISHED_WITH_MARK || (dl == 0 && sl == 0)) break;
		}
		LzmaDec_Free(&d, &g_alloc);
		free(pay);
		*out = o; *outlen = have;
	}
	return 0;
}

/* data entry layout for 5.5.0 (unicode):
 * u32 first_slice, u32 last_slice, u32 chunk_offset, u64 file_offset, u64 file_size,
 * u64 chunk_size, sha1[20], u64 filetime, u32 ver_ms, u32 ver_ls, options */
#define DE_FIXED 72

int inno_open(InnoSetup *s, const char *path, char *err, int errlen)
{
	unsigned char *b2;
	size_t b2len, rec, i;
	memset(s, 0, sizeof *s);
	s->f = fopen(path, "rb");
	if (!s->f) ERR("cannot open %s", path);
	if (find_loader(s, err, errlen)) return -1;
	fseek(s->f, s->header_offset, SEEK_SET);
	if (fread(s->version, 1, 64, s->f) != 64) ERR("truncated setup header");
	if (strncmp(s->version, "Inno Setup Setup Data (5.5.0) (u)", 33) != 0)
		ERR("unsupported Inno Setup version \"%.64s\"", s->version);
	if (read_block(s, &s->block1, &s->block1_len, err, errlen)) return -1;
	if (read_block(s, &b2, &b2len, err, errlen)) return -1;
	/* option bits are a Delphi set: try 1, 2 and 4 bytes, keep the size whose records all look sane */
	for (rec = DE_FIXED + 1; rec <= DE_FIXED + 4; rec++) {
		int ok = (b2len % rec) == 0 && b2len > 0;
		for (i = 0; ok && i < b2len / rec; i++) {
			const unsigned char *p = b2 + i * rec;
			if (le32(p) != 0 || le32(p + 4) != 0) ok = 0;            /* single embedded slice */
			if (le64(p + 12) > le64(p + 20) + 0 && 0) ok = 0;
		}
		if (ok) break;
	}
	if (rec > DE_FIXED + 4) ERR("cannot work out data entry size (block of %u bytes)", (unsigned)b2len);
	s->ndata = (uint32_t)(b2len / rec);
	s->data = calloc(s->ndata, sizeof *s->data);
	for (i = 0; i < s->ndata; i++) {
		const unsigned char *p = b2 + i * rec;
		InnoData *d = &s->data[i];
		d->chunk_offset = le32(p + 8);
		d->file_offset  = le64(p + 12);
		d->file_size    = le64(p + 20);
		d->chunk_size   = le64(p + 28);
		memcpy(d->sha1, p + 36, 20);
		d->flags = p[72] | (rec > 73 ? p[73] << 8 : 0);
	}
	free(b2);
	return 0;
}

void inno_close(InnoSetup *s)
{
	if (s->f) fclose(s->f);
	free(s->data);
	free(s->block1);
	memset(s, 0, sizeof *s);
}

/* --------------------------------------------------------- chunk stream */
int inno_read_chunk(InnoSetup *s, uint32_t chunk_offset, uint64_t chunk_size,
                    uint64_t need_bytes, inno_sink sink, void *user, char *err, int errlen)
{
	enum { IN = 1 << 16, OUT = 1 << 18 };
	unsigned char magic[4], first;
	unsigned char *in = malloc(IN), *out = malloc(OUT);
	uint64_t left = chunk_size, produced = 0;
	SizeT inlen = 0, inpos = 0;
	int lz2, rc = 0;
	CLzmaDec d1; CLzma2Dec d2;
	if (!in || !out) ERR("out of memory");
	fseek(s->f, (long)(s->data_offset + chunk_offset), SEEK_SET);
	if (fread(magic, 1, 4, s->f) != 4 || memcmp(magic, "zlb\x1a", 4) != 0) ERR("chunk at %u has no zlb magic", chunk_offset);
	/* first byte tells the codec: LZMA1 props byte is lc/lp/pb (0x5D typical), LZMA2 dict prop <= 40 */
	if (fread(&first, 1, 1, s->f) != 1) ERR("truncated chunk");
	left--;
	lz2 = first <= 40;
	if (lz2) {
		Lzma2Dec_Construct(&d2);
		if (Lzma2Dec_Allocate(&d2, first, &g_alloc) != SZ_OK) ERR("bad LZMA2 property");
		Lzma2Dec_Init(&d2);
	} else {
		unsigned char pr[5];
		pr[0] = first;
		if (fread(pr + 1, 1, 4, s->f) != 4) ERR("truncated chunk");
		left -= 4;
		LzmaDec_Construct(&d1);
		if (LzmaDec_Allocate(&d1, pr, 5, &g_alloc) != SZ_OK) ERR("bad LZMA properties");
		LzmaDec_Init(&d1);
	}
	while (produced < need_bytes) {
		SizeT dl = OUT, sl;
		ELzmaStatus st;
		SRes r;
		if (inpos == inlen && left > 0) {
			size_t want = left > IN ? IN : (size_t)left;
			inlen = fread(in, 1, want, s->f);
			if (inlen == 0) { snprintf(err, errlen, "chunk read error"); rc = -1; break; }
			left -= inlen; inpos = 0;
		}
		sl = inlen - inpos;
		r = lz2 ? Lzma2Dec_DecodeToBuf(&d2, out, &dl, in + inpos, &sl, LZMA_FINISH_ANY, &st)
		        : LzmaDec_DecodeToBuf(&d1, out, &dl, in + inpos, &sl, LZMA_FINISH_ANY, &st);
		if (r != SZ_OK) { snprintf(err, errlen, "LZMA data error in chunk %u", chunk_offset); rc = -1; break; }
		inpos += sl;
		if (dl) {
			if (sink(user, produced, out, dl)) { snprintf(err, errlen, "write failed"); rc = -1; break; }
			produced += dl;
		}
		if (st == LZMA_STATUS_FINISHED_WITH_MARK) break;
		if (dl == 0 && sl == 0 && left == 0) break;
	}
	if (rc == 0 && produced < need_bytes) { snprintf(err, errlen, "chunk %u ended early (%llu of %llu)", chunk_offset,
		(unsigned long long)produced, (unsigned long long)need_bytes); rc = -1; }
	if (lz2) Lzma2Dec_Free(&d2, &g_alloc); else LzmaDec_Free(&d1, &g_alloc);
	free(in); free(out);
	return rc;
}
