/* bink.c - Bink 1 ("BIKi") video decoder for eu2conv. See bink.h for where everything
 * comes from. Portable C99, no dependencies.
 *
 * Bitstream summary (as used here):
 *   - bits are read least-significant first out of little-endian 32-bit words, which is
 *     the same as LSB-first out of consecutive bytes;
 *   - a frame (after the audio packets) = u32 (luma size, skipped) + three planes
 *     (Y, then the two chroma planes), each padded to a 32-bit boundary;
 *   - a plane starts with the symbol trees of its nine "bundles" (block types, sub-block
 *     types, colours, patterns, x/y motion, intra/inter DC, runs); every 8-pixel block row
 *     first tops the bundles up, then decodes its 8x8 blocks, each of one of ten types. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bink.h"

/* ------------------------------------------------------------------ tables from the player */

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }

/* UPX "NRV2B" stream (the only method of UPX 1.01 for win32/pe): a bit flag stream in
 * 32-bit little-endian words read from the top bit down, interleaved with literal bytes.
 * flag 1 = literal byte; flag 0 = match: offset as an Elias-gamma-like number (2 = reuse the
 * previous offset), its low 8 bits as a byte, then a length coded the same way. */
typedef struct { const unsigned char *s; size_t n, ip; uint32_t bb; int left; int bad; } Nrv;
static int nrv_bit(Nrv *z)
{
	int b;
	if (!z->left) {
		if (z->ip + 4 > z->n) { z->bad = 1; return 0; }
		z->bb = le32(z->s + z->ip); z->ip += 4; z->left = 32;
	}
	z->left--;
	b = (int)(z->bb >> 31);
	z->bb <<= 1;
	return b;
}
static long nrv2b_unpack(const unsigned char *src, size_t n, unsigned char *out, size_t cap)
{
	Nrv z = { src, n, 0, 0, 0, 0 };
	size_t op = 0;
	uint32_t last = 1;
	for (;;) {
		uint32_t off = 1, len;
		while (nrv_bit(&z)) {
			if (z.bad || z.ip >= n || op >= cap) return -1;
			out[op++] = src[z.ip++];
		}
		do { off = off * 2 + nrv_bit(&z); } while (!nrv_bit(&z) && !z.bad && off < 0x2000000);
		if (z.bad || off >= 0x2000000) return -1;
		if (off == 2) off = last;
		else {
			if (z.ip >= n) return -1;
			off = (off - 3) * 256 + src[z.ip++];
			if (off == 0xFFFFFFFFu) break;                 /* end marker */
			last = ++off;
		}
		len = (uint32_t)nrv_bit(&z) * 2;
		len += (uint32_t)nrv_bit(&z);
		if (!len) {
			len = 1;
			do { len = len * 2 + nrv_bit(&z); } while (!nrv_bit(&z) && !z.bad && len < 0x1000000);
			len += 2;
		}
		len += off > 0xD00;
		len += 1;
		if (z.bad || off > op || op + len > cap) return -1;
		while (len--) { out[op] = out[op - off]; op++; }
	}
	return (long)op;
}

/* offsets of the tables inside the unpacked image of GOG EU2 v1.09's binkplay.exe
 * (found by searching the image for their structure; each is checked below) */
#define OFF_PATTERN 0x342F0
#define OFF_HUFF    0x346F8
#define OFF_HUFFW   0x34AD0
#define OFF_RLE     0x34AE0
#define OFF_QUANT   0x34B84      /* 4 x 16 x 64 int32: intra dequant, intra quant, inter dequant, inter quant */
#define OFF_SCAN    0x3A288

static int is_perm64(const uint8_t *p)
{
	uint8_t seen[64] = { 0 };
	int i;
	for (i = 0; i < 64; i++) { if (p[i] >= 64 || seen[p[i]]) return 0; seen[p[i]] = 1; }
	return 1;
}

int bink_tables_from_player(const unsigned char *exe, size_t n, BinkTables *t, char *why, int wl)
{
	uint32_t pe, nsec, opt, sec, i, j, q;
	uint32_t raw1 = 0, rawsz1 = 0, vtotal = 0;
	unsigned char *img;
	long got;
	size_t hp;
	if (n < 0x400 || exe[0] != 'M' || exe[1] != 'Z') { snprintf(why, wl, "binkplay.exe: not an exe"); return -1; }
	pe = le32(exe + 0x3C);
	if (pe + 24 > n || memcmp(exe + pe, "PE\0\0", 4)) { snprintf(why, wl, "binkplay.exe: no PE header"); return -1; }
	nsec = le16(exe + pe + 6); opt = le16(exe + pe + 20);
	sec = pe + 24 + opt;
	if (nsec < 2 || sec + 40 * nsec > n || memcmp(exe + sec, "UPX0", 4) || memcmp(exe + sec + 40, "UPX1", 4)) {
		snprintf(why, wl, "binkplay.exe: not the UPX-packed player of GOG v1.09"); return -1;
	}
	for (i = 0; i < 2; i++) vtotal += le32(exe + sec + 40 * i + 8);
	raw1 = le32(exe + sec + 40 + 20); rawsz1 = le32(exe + sec + 40 + 16);
	if (raw1 + rawsz1 > n || vtotal > (16u << 20)) { snprintf(why, wl, "binkplay.exe: bad sections"); return -1; }
	img = malloc(vtotal);
	if (!img) { snprintf(why, wl, "out of memory"); return -1; }
	got = nrv2b_unpack(exe + raw1, rawsz1, img, vtotal);
	if (got < OFF_SCAN + 64) { free(img); snprintf(why, wl, "binkplay.exe: unpacking failed"); return -1; }

	memcpy(t->pattern, img + OFF_PATTERN, sizeof t->pattern);
	memcpy(t->huff_bits, img + OFF_HUFFW, 16);
	memcpy(t->rle, img + OFF_RLE, 4);
	memcpy(t->scan, img + OFF_SCAN, 64);
	memset(t->huff, 0, sizeof t->huff);
	for (i = 0, hp = OFF_HUFF; i < 16; i++) {
		uint32_t w = t->huff_bits[i];
		if (w < 4 || w > 7) { free(img); snprintf(why, wl, "binkplay.exe: unexpected Huffman tables"); return -1; }
		memcpy(t->huff[i], img + hp, 1u << w);
		for (j = 0; j < (1u << w); j++)
			if ((t->huff[i][j] >> 4) == 0 || (t->huff[i][j] >> 4) > w) { free(img); snprintf(why, wl, "binkplay.exe: bad Huffman table %u", i); return -1; }
		hp += 1u << w;
	}
	if (hp + 8 != OFF_HUFFW) { free(img); snprintf(why, wl, "binkplay.exe: Huffman layout"); return -1; }
	for (q = 0; q < 16; q++)
		for (j = 0; j < 64; j++) {
			t->qintra[q][j] = (int32_t)le32(img + OFF_QUANT + (0 * 1024 + q * 64 + j) * 4);
			t->qinter[q][j] = (int32_t)le32(img + OFF_QUANT + (2 * 1024 + q * 64 + j) * 4);
		}
	free(img);
	for (i = 0; i < 16; i++) if (!is_perm64(t->pattern[i])) { snprintf(why, wl, "binkplay.exe: bad pattern table"); return -1; }
	if (!is_perm64(t->scan) || t->scan[0] != 0) { snprintf(why, wl, "binkplay.exe: bad scan table"); return -1; }
	if (t->rle[0] != 4 || t->rle[3] != 32) { snprintf(why, wl, "binkplay.exe: bad run table"); return -1; }
	if (t->qintra[0][0] != 65536 || t->qinter[0][0] != 65536) { snprintf(why, wl, "binkplay.exe: bad quantiser tables"); return -1; }
	return 0;
}

/* ------------------------------------------------------------------------- bit reader */
typedef struct { const uint8_t *p; size_t nbits, pos; int over; } Bits;

static uint32_t bits_get(Bits *b, int k)
{
	uint64_t v = 0;
	size_t by = b->pos >> 3;
	int i;
	if (!k) return 0;
	if (b->pos + k > b->nbits) { b->over = 1; b->pos += k; return 0; }
	for (i = 0; i < 6; i++) v |= (uint64_t)b->p[by + i] << (8 * i);   /* buffer is padded */
	v >>= b->pos & 7;
	b->pos += k;
	return (uint32_t)(v & ((1ull << k) - 1));
}
static int bit1(Bits *b) { return (int)bits_get(b, 1); }

/* ------------------------------------------------------------------------ decoder state */
enum { B_TYPES, B_SUBTYPES, B_COLORS, B_PATTERN, B_XOFF, B_YOFF, B_INTRADC, B_INTERDC, B_RUN, NBUNDLE };
enum { BLK_SKIP, BLK_SCALED, BLK_MOTION, BLK_RUN, BLK_RESIDUE, BLK_INTRA, BLK_FILL, BLK_INTER, BLK_PATTERN, BLK_RAW };

typedef struct { int table; uint8_t sym[16]; } Tree;
typedef struct {
	int    lenbits;              /* bits of the item count read at each top-up */
	Tree   tree;
	int16_t *buf;
	size_t cap, wr, rd;          /* written / consumed items */
	int    done;                 /* a count of 0 was read: nothing more this plane */
} Bundle;

struct Bink {
	const unsigned char *d;
	size_t n;
	const BinkTables *t;
	int w, h, frames, fnum, fden, ntracks, cur;
	uint32_t *index;             /* frames+1 offsets */
	int pw[3], ph[3], stride[3], rows[3];
	uint8_t *pl[2][3];           /* two pictures: current and previous */
	int curpic;
	Bundle bun[NBUNDLE];
	Tree colhi[16];
	int colhi_last;
	uint8_t *fbuf;               /* padded copy of the frame's video data */
	size_t fcap;
};

static int huff_sym(Bits *b, const BinkTables *t, const Tree *tr)
{
	int w = t->huff_bits[tr->table];
	uint8_t e;
	size_t save = b->pos;
	uint32_t peek;
	/* peek w bits (may run past the end of the data: the buffer is padded with zeros) */
	if (b->pos + w > b->nbits) {
		size_t nb = b->nbits;
		b->nbits = b->pos + w;
		peek = bits_get(b, w);
		b->nbits = nb;
	} else peek = bits_get(b, w);
	e = t->huff[tr->table][peek];
	b->pos = save + (e >> 4);
	if (b->pos > b->nbits) b->over = 1;
	return tr->sym[e & 15];
}

/* in-place two-way merge used by the shuffled symbol order: a flag bit per output
 * position picks the next symbol from the first or the second half */
static void tree_merge(Bits *b, uint8_t *dst, const uint8_t *src, int size)
{
	const uint8_t *a = src, *c = src + size;
	int na = size, nc = size;
	while (na && nc) {
		if (!bit1(b)) { *dst++ = *a++; na--; }
		else { *dst++ = *c++; nc--; }
	}
	while (na--) *dst++ = *a++;
	while (nc--) *dst++ = *c++;
}

static void read_tree(Bits *b, Tree *tr)
{
	int i, k;
	tr->table = (int)bits_get(b, 4);
	for (i = 0; i < 16; i++) tr->sym[i] = (uint8_t)i;
	if (!tr->table) return;
	if (bit1(b)) {                               /* explicit list of the first symbols */
		uint8_t used[16] = { 0 };
		int cnt = (int)bits_get(b, 3) + 1;
		for (i = 0; i < cnt; i++) { tr->sym[i] = (uint8_t)bits_get(b, 4); used[tr->sym[i]] = 1; }
		for (k = 0; k < 16 && cnt < 16; k++) if (!used[k]) tr->sym[cnt++] = (uint8_t)k;
	} else {                                     /* merge shuffle of depth 1..4 */
		uint8_t x[16], y[16], *in = x, *out = y, *s;
		int depth = (int)bits_get(b, 2), size;
		for (i = 0; i < 16; i++) x[i] = (uint8_t)i;
		for (k = 0; k <= depth; k++) {
			size = 1 << k;
			for (i = 0; i < 16; i += size * 2) tree_merge(b, out + i, in + i, size);
			s = in; in = out; out = s;
		}
		memcpy(tr->sym, in, 16);
	}
}

static int ilog2(unsigned v) { int r = -1; while (v) { v >>= 1; r++; } return r; }

/* ------------------------------------------------------------------------ bundle top-up */
/* returns the number of items to read now (0 = nothing to do), -1 = overflow */
static int bun_begin(Bits *b, Bundle *u)
{
	int cnt;
	if (u->done || u->wr > u->rd) return 0;
	cnt = (int)bits_get(b, u->lenbits);
	if (!cnt) { u->done = 1; return 0; }
	if (u->wr + cnt + 64 > u->cap) return -1;
	return cnt;
}
static void bun_fill(Bundle *u, int v, int cnt) { while (cnt-- > 0) u->buf[u->wr++] = (int16_t)v; }

static int read_types(Bits *b, Bink *k, Bundle *u)
{
	int cnt = bun_begin(b, u), last = 0;
	size_t end;
	if (cnt <= 0) return cnt;
	end = u->wr + cnt;
	if (bit1(b)) { bun_fill(u, (int)bits_get(b, 4), cnt); return 0; }
	while (u->wr < end) {
		int v = huff_sym(b, k->t, &u->tree);
		if (v < 12) { last = v; u->buf[u->wr++] = (int16_t)v; }
		else {
			int r = k->t->rle[v - 12];
			if (u->wr + r > end) r = (int)(end - u->wr);
			bun_fill(u, last, r);
		}
		if (b->over) return -1;
	}
	return 0;
}

static int read_colors(Bits *b, Bink *k, Bundle *u)
{
	int cnt = bun_begin(b, u);
	size_t end;
	if (cnt <= 0) return cnt;
	end = u->wr + cnt;
	if (bit1(b)) {
		int v;
		k->colhi_last = huff_sym(b, k->t, &k->colhi[k->colhi_last]);
		v = huff_sym(b, k->t, &u->tree);
		bun_fill(u, k->colhi_last << 4 | v, cnt);
		return 0;
	}
	while (u->wr < end) {
		int v;
		k->colhi_last = huff_sym(b, k->t, &k->colhi[k->colhi_last]);
		v = huff_sym(b, k->t, &u->tree);
		u->buf[u->wr++] = (int16_t)(k->colhi_last << 4 | v);
		if (b->over) return -1;
	}
	return 0;
}

static int read_patterns(Bits *b, Bink *k, Bundle *u)
{
	int cnt = bun_begin(b, u);
	size_t end;
	if (cnt <= 0) return cnt;
	end = u->wr + cnt;
	while (u->wr < end) {
		int lo = huff_sym(b, k->t, &u->tree);
		int hi = huff_sym(b, k->t, &u->tree);
		u->buf[u->wr++] = (int16_t)(hi << 4 | lo);
		if (b->over) return -1;
	}
	return 0;
}

static int read_motion(Bits *b, Bink *k, Bundle *u)
{
	int cnt = bun_begin(b, u);
	size_t end;
	if (cnt <= 0) return cnt;
	end = u->wr + cnt;
	if (bit1(b)) {
		int v = (int)bits_get(b, 4);
		if (v && bit1(b)) v = -v;
		bun_fill(u, v, cnt);
		return 0;
	}
	while (u->wr < end) {
		int v = huff_sym(b, k->t, &u->tree);
		if (v && bit1(b)) v = -v;
		u->buf[u->wr++] = (int16_t)v;
		if (b->over) return -1;
	}
	return 0;
}

static int read_runs(Bits *b, Bink *k, Bundle *u)
{
	int cnt = bun_begin(b, u);
	size_t end;
	if (cnt <= 0) return cnt;
	end = u->wr + cnt;
	if (bit1(b)) { bun_fill(u, (int)bits_get(b, 4), cnt); return 0; }
	while (u->wr < end) {
		u->buf[u->wr++] = (int16_t)huff_sym(b, k->t, &u->tree);
		if (b->over) return -1;
	}
	return 0;
}

/* DC values: a first value (11 bits unsigned, or 10 bits + sign), then groups of up to 8
 * deltas, each group with its own 4-bit width (0 = repeat) and a sign per non-zero delta */
static int read_dcs(Bits *b, Bundle *u, int sgn)
{
	int cnt = bun_begin(b, u), v, i, j;
	if (cnt <= 0) return cnt;
	if (sgn) { v = (int)bits_get(b, 10); if (v && bit1(b)) v = -v; }
	else v = (int)bits_get(b, 11);
	u->buf[u->wr++] = (int16_t)v;
	for (i = 1; i < cnt; i += 8) {
		int grp = cnt - i < 8 ? cnt - i : 8;
		int w = (int)bits_get(b, 4);
		for (j = 0; j < grp; j++) {
			if (w) {
				int d = (int)bits_get(b, w);
				if (d && bit1(b)) d = -d;
				v += d;
				if (v < -32768 || v > 32767) return -1;
			}
			u->buf[u->wr++] = (int16_t)v;
		}
		if (b->over) return -1;
	}
	return 0;
}

static int val(Bink *k, int which)
{
	Bundle *u = &k->bun[which];
	if (u->rd >= u->wr) return 0;              /* exhausted: corrupt stream, keep going */
	return u->buf[u->rd++];
}

/* ------------------------------------------------------------------- coefficient reading */
/* The 63 AC positions (in scan order) are split into lists that are refined bit plane by
 * bit plane, most significant first. Each list entry is (start, mode):
 *   mode 0: a group of 16 still to be visited (start .. start+15): on a hit, the first 4 are
 *           examined now and the entry turns into mode 1 for the remaining 12;
 *   mode 1: on a hit, splits into three mode-2 groups of 4;
 *   mode 2: a group of 4: on a hit, each of the 4 either becomes significant now (flag 0)
 *           or is postponed as a single (mode 3, flag 1);
 *   mode 3: a single coefficient: on a hit it becomes significant.
 * DCT: values get their magnitude at the current plane ("bits" further bits) and a sign.
 * Residue: values are +-mask, and coefficients already significant get refined each plane. */
typedef struct { int pos[128], mode[128], first, last; } CoefLists;

static void lists_init(CoefLists *L, int dct)
{
	L->first = L->last = 64;
	L->pos[L->last] = 4;  L->mode[L->last++] = 0;
	L->pos[L->last] = 24; L->mode[L->last++] = 0;
	L->pos[L->last] = 44; L->mode[L->last++] = 0;
	if (dct) {
		L->pos[L->last] = 1; L->mode[L->last++] = 3;
		L->pos[L->last] = 2; L->mode[L->last++] = 3;
		L->pos[L->last] = 3; L->mode[L->last++] = 3;
	} else {
		L->pos[L->last] = 0; L->mode[L->last++] = 2;
	}
}

static int dct_value(Bits *b, int bits)
{
	int v;
	if (!bits) return bit1(b) ? -1 : 1;
	v = (int)bits_get(b, bits) | 1 << bits;
	return bit1(b) ? -v : v;
}

/* reads AC coefficients into blk (raster order) and dequantises blk (incl. the DC already
 * in blk[0]) with the 16 matrices q */
static int read_dct(Bits *b, const BinkTables *t, int *blk, const int32_t (*q)[64])
{
	CoefLists L;
	int hit[64], nhit = 0, bits, i, qi;
	lists_init(&L, 1);
	for (bits = (int)bits_get(b, 4) - 1; bits >= 0; bits--) {
		int p = L.first;
		while (p < L.last) {
			int c = L.pos[p], m = L.mode[p];
			if ((!c && !m) || !bit1(b)) { p++; continue; }
			if (m == 0 || m == 2) {
				if (m == 0) { L.pos[p] = c + 4; L.mode[p] = 1; }
				else { L.pos[p] = 0; L.mode[p] = 0; p++; }
				for (i = 0; i < 4; i++, c++) {
					if (bit1(b)) { L.first--; L.pos[L.first] = c; L.mode[L.first] = 3; }
					else { blk[t->scan[c]] = dct_value(b, bits); hit[nhit++] = c; }
				}
			} else if (m == 1) {
				L.mode[p] = 2;
				for (i = 0; i < 3; i++) { c += 4; L.pos[L.last] = c; L.mode[L.last++] = 2; }
			} else {
				blk[t->scan[c]] = dct_value(b, bits); hit[nhit++] = c;
				L.pos[p] = 0; L.mode[p] = 0; p++;
			}
			if (b->over || L.first < 1 || L.last > 127 || nhit > 63) return -1;
		}
	}
	qi = (int)bits_get(b, 4);
	blk[0] = (int)(((long long)blk[0] * q[qi][0]) >> 11);
	for (i = 0; i < nhit; i++) {
		int r = t->scan[hit[i]];
		blk[r] = (int)(((long long)blk[r] * q[qi][r]) >> 11);
	}
	return 0;
}

static int read_residue(Bits *b, const BinkTables *t, int *blk, int masks)
{
	CoefLists L;
	int nz[64], nnz = 0, mask, i;
	lists_init(&L, 0);
	for (mask = 1 << bits_get(b, 3); mask; mask >>= 1) {
		int p;
		for (i = 0; i < nnz; i++) {
			if (!bit1(b)) continue;
			if (blk[nz[i]] < 0) blk[nz[i]] -= mask; else blk[nz[i]] += mask;
			if (--masks < 0) return 0;
		}
		p = L.first;
		while (p < L.last) {
			int c = L.pos[p], m = L.mode[p];
			if ((!c && !m) || !bit1(b)) { p++; continue; }
			if (m == 0 || m == 2) {
				if (m == 0) { L.pos[p] = c + 4; L.mode[p] = 1; }
				else { L.pos[p] = 0; L.mode[p] = 0; p++; }
				for (i = 0; i < 4; i++, c++) {
					if (bit1(b)) { L.first--; L.pos[L.first] = c; L.mode[L.first] = 3; }
					else {
						int r = t->scan[c];
						nz[nnz++] = r;
						blk[r] = bit1(b) ? -mask : mask;
						if (--masks < 0) return 0;
					}
				}
			} else if (m == 1) {
				L.mode[p] = 2;
				for (i = 0; i < 3; i++) { c += 4; L.pos[L.last] = c; L.mode[L.last++] = 2; }
			} else {
				int r = t->scan[c];
				nz[nnz++] = r;
				blk[r] = bit1(b) ? -mask : mask;
				L.pos[p] = 0; L.mode[p] = 0; p++;
				if (--masks < 0) return 0;
			}
			if (b->over || L.first < 1 || L.last > 127 || nnz > 64) return -1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------------ IDCT */
/* Scaled 8-point inverse DCT (the Arai-Agui-Nakajima factorisation; the dequantisation
 * matrices already carry its scale factors). Constants are 2^11-scaled:
 * sqrt(2), 2cos(3pi/8)*sqrt(2)... i.e. 1.414214, 1.847759, 1.082392, 2.613126. */
#define C_R2   2896   /* 1.414214 * 2048 */
#define C_E5   3784   /* 1.847759 * 2048 */
#define C_B6   2217   /* 1.082392 * 2048 */
#define C_A6   5352   /* 2.613126 * 2048 */
static void idct8(const int *in, int is, int *out, int os, int round, int shift)
{
	int e0 = in[0] + in[4 * is], e1 = in[0] - in[4 * is];
	int e2 = in[2 * is] + in[6 * is];
	int e3 = (C_R2 * (in[2 * is] - in[6 * is]) >> 11) - e2;
	int t0 = e0 + e2, t3 = e0 - e2, t1 = e1 + e3, t2 = e1 - e3;
	int z13 = in[5 * is] + in[3 * is], z10 = in[5 * is] - in[3 * is];
	int z11 = in[1 * is] + in[7 * is], z12 = in[1 * is] - in[7 * is];
	int o7 = z11 + z13;
	int z5 = C_E5 * (z10 + z12) >> 11;
	int o6 = (-(C_A6 * z10) >> 11) + z5 - o7;
	int o5 = (C_R2 * (z11 - z13) >> 11) - o6;
	int o4 = (C_B6 * z12 >> 11) - z5 + o5;
	out[0 * os] = (t0 + o7 + round) >> shift;
	out[7 * os] = (t0 - o7 + round) >> shift;
	out[1 * os] = (t1 + o6 + round) >> shift;
	out[6 * os] = (t1 - o6 + round) >> shift;
	out[2 * os] = (t2 + o5 + round) >> shift;
	out[5 * os] = (t2 - o5 + round) >> shift;
	out[3 * os] = (t3 - o4 + round) >> shift;
	out[4 * os] = (t3 + o4 + round) >> shift;
}
/* blk raster 8x8 -> res (pixel values, not clamped) */
static void idct_block(const int *blk, int *res)
{
	int tmp[64], c, r;
	for (c = 0; c < 8; c++) {
		const int *s = blk + c;
		if (!(s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56])) {
			for (r = 0; r < 8; r++) tmp[r * 8 + c] = s[0];
		} else idct8(s, 8, tmp + c, 8, 0, 0);
	}
	for (r = 0; r < 8; r++) idct8(tmp + r * 8, 1, res + r * 8, 1, 0x7F, 8);
}

static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* ------------------------------------------------------------------------- block decode */
static void run_block(Bits *b, Bink *k, uint8_t *o)            /* o = 64 bytes raster */
{
	const uint8_t *pat = k->t->pattern[bits_get(b, 4)];
	int i = 0, j;
	do {
		int run = val(k, B_RUN) + 1;
		if (i + run > 64) run = 64 - i;
		if (bit1(b)) { int v = val(k, B_COLORS); for (j = 0; j < run; j++) o[pat[i + j]] = (uint8_t)v; }
		else for (j = 0; j < run; j++) o[pat[i + j]] = (uint8_t)val(k, B_COLORS);
		i += run;
	} while (i < 63 && !b->over);
	if (i == 63) o[pat[63]] = (uint8_t)val(k, B_COLORS);
}
static void pattern_block(Bink *k, uint8_t *o)
{
	int c[2], y, x;
	c[0] = val(k, B_COLORS); c[1] = val(k, B_COLORS);
	for (y = 0; y < 8; y++) {
		int v = val(k, B_PATTERN);
		for (x = 0; x < 8; x++, v >>= 1) o[y * 8 + x] = (uint8_t)c[v & 1];
	}
}

static void put8x8(uint8_t *d, int st, const uint8_t *s)
{ int y; for (y = 0; y < 8; y++) memcpy(d + y * st, s + y * 8, 8); }
static void put16x16(uint8_t *d, int st, const uint8_t *s)
{
	int y, x;
	for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) d[y * st + x] = s[(y >> 1) * 8 + (x >> 1)];
}

static int decode_plane(Bits *b, Bink *k, int pl)
{
	int pw = k->pw[pl], ph = k->ph[pl], st = k->stride[pl];
	int bw = (pw + 7) >> 3, bh = (ph + 7) >> 3, bx, by, i, w8;
	uint8_t *cur = k->pl[k->curpic][pl], *prev = k->pl[k->curpic ^ 1][pl];
	int maxy = k->rows[pl] - 8, maxx = st - 8;
	w8 = pw < 8 ? 8 : pw;
	k->bun[B_TYPES].lenbits = ilog2((w8 >> 3) + 511) + 1;
	k->bun[B_SUBTYPES].lenbits = ilog2((w8 >> 4) + 511) + 1;
	k->bun[B_COLORS].lenbits = ilog2(bw * 64 + 511) + 1;
	k->bun[B_INTRADC].lenbits = k->bun[B_INTERDC].lenbits =
	k->bun[B_XOFF].lenbits = k->bun[B_YOFF].lenbits = ilog2((w8 >> 3) + 511) + 1;
	k->bun[B_PATTERN].lenbits = ilog2((bw << 3) + 511) + 1;
	k->bun[B_RUN].lenbits = ilog2(bw * 48 + 511) + 1;
	for (i = 0; i < NBUNDLE; i++) {
		Bundle *u = &k->bun[i];
		if (i == B_COLORS) { int j; for (j = 0; j < 16; j++) read_tree(b, &k->colhi[j]); k->colhi_last = 0; }
		if (i != B_INTRADC && i != B_INTERDC) read_tree(b, &u->tree);
		u->wr = u->rd = 0; u->done = 0;
	}
	if (b->over) return -1;
	for (by = 0; by < bh; by++) {
		if (read_types(b, k, &k->bun[B_TYPES]) || read_types(b, k, &k->bun[B_SUBTYPES]) ||
		    read_colors(b, k, &k->bun[B_COLORS]) || read_patterns(b, k, &k->bun[B_PATTERN]) ||
		    read_motion(b, k, &k->bun[B_XOFF]) || read_motion(b, k, &k->bun[B_YOFF]) ||
		    read_dcs(b, &k->bun[B_INTRADC], 0) || read_dcs(b, &k->bun[B_INTERDC], 1) ||
		    read_runs(b, k, &k->bun[B_RUN]))
			return -1;
		for (bx = 0; bx < bw; bx++) {
			uint8_t *d = cur + by * 8 * st + bx * 8;
			const uint8_t *ref;
			uint8_t tmp[64];
			int blk[64], res[64], x, y, t = val(k, B_TYPES);
			if ((by & 1) && t == BLK_SCALED) { bx++; continue; }   /* lower half of a 16x16 block */
			switch (t) {
			case BLK_SKIP:
				ref = prev + (d - cur);
				for (y = 0; y < 8; y++) memcpy(d + y * st, ref + y * st, 8);
				break;
			case BLK_SCALED: {
				int s = val(k, B_SUBTYPES);
				switch (s) {
				case BLK_RUN: run_block(b, k, tmp); break;
				case BLK_INTRA:
					memset(blk, 0, sizeof blk);
					blk[0] = val(k, B_INTRADC);
					if (read_dct(b, k->t, blk, k->t->qintra)) return -1;
					idct_block(blk, res);
					for (i = 0; i < 64; i++) tmp[i] = clip8(res[i]);
					break;
				case BLK_FILL: memset(tmp, val(k, B_COLORS), 64); break;
				case BLK_PATTERN: pattern_block(k, tmp); break;
				case BLK_RAW: for (i = 0; i < 64; i++) tmp[i] = (uint8_t)val(k, B_COLORS); break;
				default: return -1;
				}
				put16x16(d, st, tmp);
				bx++;
				break;
			}
			case BLK_MOTION: case BLK_RESIDUE: case BLK_INTER: {
				int xo = val(k, B_XOFF), yo = val(k, B_YOFF);
				int sx = bx * 8 + xo, sy = by * 8 + yo;
				if (sx < 0) sx = 0;
				if (sx > maxx) sx = maxx;
				if (sy < 0) sy = 0;
				if (sy > maxy) sy = maxy;
				ref = prev + sy * st + sx;
				for (y = 0; y < 8; y++) memcpy(d + y * st, ref + y * st, 8);
				if (t == BLK_RESIDUE) {
					int masks = (int)bits_get(b, 7);
					memset(blk, 0, sizeof blk);
					if (read_residue(b, k->t, blk, masks)) return -1;
					for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) d[y * st + x] = clip8(d[y * st + x] + blk[y * 8 + x]);
				} else if (t == BLK_INTER) {
					memset(blk, 0, sizeof blk);
					blk[0] = val(k, B_INTERDC);
					if (read_dct(b, k->t, blk, k->t->qinter)) return -1;
					idct_block(blk, res);
					for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) d[y * st + x] = clip8(d[y * st + x] + res[y * 8 + x]);
				}
				break;
			}
			case BLK_RUN: run_block(b, k, tmp); put8x8(d, st, tmp); break;
			case BLK_INTRA:
				memset(blk, 0, sizeof blk);
				blk[0] = val(k, B_INTRADC);
				if (read_dct(b, k->t, blk, k->t->qintra)) return -1;
				idct_block(blk, res);
				for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) d[y * st + x] = clip8(res[y * 8 + x]);
				break;
			case BLK_FILL: { int v = val(k, B_COLORS); for (y = 0; y < 8; y++) memset(d + y * st, v, 8); break; }
			case BLK_PATTERN: pattern_block(k, tmp); put8x8(d, st, tmp); break;
			case BLK_RAW: for (i = 0; i < 64; i++) tmp[i] = (uint8_t)val(k, B_COLORS); put8x8(d, st, tmp); break;
			default: return -1;
			}
			if (b->over) return -1;
		}
	}
	if (b->pos & 31) b->pos += 32 - (b->pos & 31);
	return 0;
}

/* ---------------------------------------------------------------------------- container */
/* header: "BIKi", u32 size-8, u32 frames, u32 largest frame, u32 frames, u32 width,
 * u32 height, u32 fps numerator, u32 fps denominator, u32 video flags, u32 audio tracks;
 * per track 4 + 4 + 4 bytes of audio info; then frames+1 u32 frame offsets (bit 0 = key
 * frame). A frame: per audio track u32 packet size + packet, then the video data. */
Bink *bink_open(const unsigned char *d, size_t n, const BinkTables *t, char *why, int wl)
{
	Bink *k;
	size_t p;
	int i, j;
	if (n < 44 || memcmp(d, "BIK", 3)) { snprintf(why, wl, "not a Bink file"); return NULL; }
	if (d[3] != 'i') { snprintf(why, wl, "Bink version '%c' (only 'i' is supported)", d[3]); return NULL; }
	k = calloc(1, sizeof *k);
	if (!k) { snprintf(why, wl, "out of memory"); return NULL; }
	k->d = d; k->n = n; k->t = t;
	k->frames = (int)le32(d + 8);
	k->w = (int)le32(d + 20); k->h = (int)le32(d + 24);
	k->fnum = (int)le32(d + 28); k->fden = (int)le32(d + 32);
	k->ntracks = (int)le32(d + 40);
	if (le32(d + 36) != 0) { snprintf(why, wl, "Bink with alpha/flags not supported"); free(k); return NULL; }
	if (k->w < 8 || k->h < 8 || k->w > 4096 || k->h > 4096 || k->frames < 1 || k->ntracks > 16) {
		snprintf(why, wl, "bad Bink header"); free(k); return NULL;
	}
	p = 44 + 12 * (size_t)k->ntracks;
	if (p + 4 * ((size_t)k->frames + 1) > n) { snprintf(why, wl, "truncated Bink index"); free(k); return NULL; }
	k->index = malloc(4 * ((size_t)k->frames + 1));
	for (i = 0; i <= k->frames; i++) k->index[i] = le32(d + p + 4 * i) & ~1u;
	k->index[k->frames] = (uint32_t)n < k->index[k->frames] ? (uint32_t)n : k->index[k->frames];
	for (i = 0; i < 3; i++) {
		int pw = i ? (k->w + 1) >> 1 : k->w, ph = i ? (k->h + 1) >> 1 : k->h;
		k->pw[i] = pw; k->ph[i] = ph;
		k->stride[i] = (((pw + 7) >> 3) + 2) * 8;        /* room for a 16x16 block at the edge */
		k->rows[i] = (((ph + 7) >> 3) + 2) * 8;
		for (j = 0; j < 2; j++) {
			k->pl[j][i] = malloc((size_t)k->stride[i] * k->rows[i]);
			memset(k->pl[j][i], i ? 128 : 0, (size_t)k->stride[i] * k->rows[i]);
		}
	}
	{
		size_t blocks = (size_t)((k->w + 7) >> 3) * ((k->h + 7) >> 3) + 64;
		for (i = 0; i < NBUNDLE; i++) {
			k->bun[i].cap = blocks * (i == B_COLORS || i == B_RUN ? 64 : i == B_PATTERN ? 8 : 1) + 1024;
			k->bun[i].buf = malloc(k->bun[i].cap * sizeof(int16_t));
		}
	}
	k->cur = 0;
	k->curpic = 0;
	return k;
}

int bink_width(const Bink *b) { return b->w; }
int bink_height(const Bink *b) { return b->h; }
int bink_frames(const Bink *b) { return b->frames; }
int bink_fps_num(const Bink *b) { return b->fnum; }
int bink_fps_den(const Bink *b) { return b->fden; }

int bink_decode_next(Bink *k, char *why, int wl)
{
	size_t p, end, vlen;
	int i;
	Bits b;
	if (k->cur >= k->frames) { snprintf(why, wl, "end of video"); return -1; }
	p = k->index[k->cur]; end = k->index[k->cur + 1];
	if (end > k->n || p > end) { snprintf(why, wl, "frame %d out of the file", k->cur); return -1; }
	for (i = 0; i < k->ntracks; i++) {
		if (p + 4 > end) { snprintf(why, wl, "frame %d: bad audio packet", k->cur); return -1; }
		p += 4 + le32(k->d + p);
	}
	if (p > end) { snprintf(why, wl, "frame %d: bad audio packet", k->cur); return -1; }
	vlen = end - p;
	if (vlen + 16 > k->fcap) { k->fcap = vlen + 16 + 65536; k->fbuf = realloc(k->fbuf, k->fcap); }
	memcpy(k->fbuf, k->d + p, vlen);
	memset(k->fbuf + vlen, 0, 16);
	b.p = k->fbuf; b.nbits = vlen * 8; b.pos = 0; b.over = 0;

	k->curpic ^= 1;
	if (vlen) {
		bits_get(&b, 32);                                  /* size of the luma plane */
		for (i = 0; i < 3; i++) {
			int pl = i == 0 ? 0 : i == 1 ? 2 : 1;          /* coded order: Y, V, U */
			if (decode_plane(&b, k, pl)) { snprintf(why, wl, "frame %d: plane %d: bad data", k->cur, i); k->cur++; return -1; }
			if (b.pos >= b.nbits) break;
		}
	} else {                                               /* empty frame: repeat */
		for (i = 0; i < 3; i++) memcpy(k->pl[k->curpic][i], k->pl[k->curpic ^ 1][i], (size_t)k->stride[i] * k->rows[i]);
	}
	return k->cur++;
}

const uint8_t *bink_plane(const Bink *k, int plane, int *stride)
{
	if (stride) *stride = k->stride[plane];
	return k->pl[k->curpic][plane];
}

/* BT.601 YCbCr -> RGB, studio range (the videos' black is Y=16, their white Y=235) */
void bink_rgb24(const Bink *k, uint8_t *out)
{
	int x, y;
	const uint8_t *Y = k->pl[k->curpic][0], *U = k->pl[k->curpic][1], *V = k->pl[k->curpic][2];
	for (y = 0; y < k->h; y++)
		for (x = 0; x < k->w; x++) {
			int yy = (Y[y * k->stride[0] + x] - 16) * 76309;
			int u = U[(y >> 1) * k->stride[1] + (x >> 1)] - 128;
			int v = V[(y >> 1) * k->stride[2] + (x >> 1)] - 128;
			*out++ = clip8((yy + 104597 * v + 32768) >> 16);
			*out++ = clip8((yy - 25675 * u - 53279 * v + 32768) >> 16);
			*out++ = clip8((yy + 132201 * u + 32768) >> 16);
		}
}

void bink_close(Bink *k)
{
	int i, j;
	if (!k) return;
	for (i = 0; i < 3; i++) for (j = 0; j < 2; j++) free(k->pl[j][i]);
	for (i = 0; i < NBUNDLE; i++) free(k->bun[i].buf);
	free(k->index); free(k->fbuf); free(k);
}
