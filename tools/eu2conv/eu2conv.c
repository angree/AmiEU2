/* eu2conv - extracts and converts the player's own GOG copy of Europa Universalis II
 * into the data set the Amiga port reads. Format: docs/DATA_FORMAT.md.
 *
 *   eu2conv <setup_europa_universalis2_2.0.0.1.exe> <outdir>
 *   eu2conv --dir "<installed EU2 folder>" <outdir>
 *
 * Every file is checked against its SHA-1 (knownfiles.h); nothing is converted from a
 * file that does not match the GOG v1.09 release byte for byte.
 * Portable C99, builds for Windows (mingw) and Linux. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif
#include "inno.h"
#include "sha1.h"
#include "knownfiles.h"
#include "bink.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "third_party/minimp3.h"

#define CONVERSION_VERSION 4   /* 2: music as raw 8-bit PCM 28 kHz (EUM1); 3: + the menus' palette (amieu2_frontend.pal);
                                  4: + intro.pak (still frames of the Bink intro videos) */
#define CONV_TAG "EU2 Amiga data conversion"

/* ------------------------------------------------------------- helpers */
static void die(const char *m) { fprintf(stderr, "eu2conv: %s\n", m); exit(1); }
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("out of memory"); return p; }

typedef struct { unsigned char *p; size_t n, cap; } Buf;
static void buf_put(Buf *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		b->cap = (b->n + n) * 2 + 4096;
		b->p = realloc(b->p, b->cap);
		if (!b->p) die("out of memory");
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}
static void put8(Buf *b, unsigned v)  { unsigned char c = (unsigned char)v; buf_put(b, &c, 1); }
static void put16(Buf *b, unsigned v) { put8(b, v >> 8); put8(b, v); }               /* big-endian */
static void put32(Buf *b, unsigned long v) { put16(b, (unsigned)(v >> 16)); put16(b, (unsigned)(v & 0xFFFF)); }
static void fput16le(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void fput32le(FILE *f, unsigned long v) { fput16le(f, v & 0xFFFF); fput16le(f, (unsigned)(v >> 16)); }
static unsigned rd16le(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32le(const unsigned char *p) { return rd16le(p) | ((unsigned long)rd16le(p + 2) << 16); }

static void mkdirs(const char *path)
{
	char tmp[1024];
	size_t i;
	snprintf(tmp, sizeof tmp, "%s", path);
	for (i = 1; tmp[i]; i++)
		if (tmp[i] == '/' || tmp[i] == '\\') { char c = tmp[i]; tmp[i] = 0; MKDIR(tmp); tmp[i] = c; }
	MKDIR(tmp);
}

static const char *ext_of(const char *path)
{
	const char *s = strrchr(path, '/'), *d = strrchr(path, '.');
	if (!d || (s && d < s)) return "";
	return d + 1;
}
static int ext_is(const char *path, const char *e)
{
	const char *x = ext_of(path);
	while (*x && *e) if (tolower((unsigned char)*x++) != *e++) return 0;
	return !*x && !*e;
}

/* --------------------------------------------------------------- packs */
typedef struct { char *name; unsigned long off, size; } PakEnt;
typedef struct { FILE *f; const char *fname; PakEnt *e; int n, cap; unsigned long pos; } Pak;

static void pak_open(Pak *k, const char *outdir, const char *name)
{
	char path[1024];
	unsigned char hdr[20] = { 0 };
	memset(k, 0, sizeof *k);
	snprintf(path, sizeof path, "%s/%s", outdir, name);
	k->fname = name;
	k->f = fopen(path, "wb");
	if (!k->f) die("cannot create a pack in the output folder");
	fwrite(hdr, 1, 20, k->f);
	k->pos = 20;
}
static void pak_add(Pak *k, const char *name, const void *data, size_t n)
{
	PakEnt *e;
	size_t i;
	static const unsigned char zero = 0;
	if (k->n == k->cap) { k->cap = k->cap * 2 + 256; k->e = realloc(k->e, k->cap * sizeof *k->e); }
	e = &k->e[k->n++];
	e->name = xmalloc(strlen(name) + 1);
	for (i = 0; name[i]; i++) e->name[i] = (char)tolower((unsigned char)(name[i] == '\\' ? '/' : name[i]));
	e->name[i] = 0;
	e->off = k->pos; e->size = (unsigned long)n;
	if (fwrite(data, 1, n, k->f) != n) die("write failed (disk full?)");
	k->pos += (unsigned long)n;
	while (k->pos & 3) { fwrite(&zero, 1, 1, k->f); k->pos++; }    /* keep every entry long-aligned */
}
static int ent_cmp(const void *a, const void *b) { return strcmp(((const PakEnt *)a)->name, ((const PakEnt *)b)->name); }
static void pak_close(Pak *k)
{
	Buf d = { 0 }, h = { 0 };
	int i;
	qsort(k->e, k->n, sizeof *k->e, ent_cmp);
	for (i = 0; i < k->n; i++) {
		size_t l = strlen(k->e[i].name);
		put32(&d, k->e[i].off); put32(&d, k->e[i].size); put8(&d, (unsigned)l);
		buf_put(&d, k->e[i].name, l);
		while (d.n & 1) put8(&d, 0);
		free(k->e[i].name);
	}
	fwrite(d.p, 1, d.n, k->f);
	buf_put(&h, "EU2P", 4); put32(&h, 1); put32(&h, (unsigned long)k->n); put32(&h, k->pos); put32(&h, (unsigned long)d.n);
	fseek(k->f, 0, SEEK_SET);
	fwrite(h.p, 1, 20, k->f);
	fclose(k->f);
	printf("  %-10s %5d entries, %8.1f MB\n", k->fname, k->n, (k->pos + d.n) / 1048576.0);
	free(d.p); free(h.p); free(k->e);
}

/* ------------------------------------------------------------ PackBits */
/* unit = 1 (bytes) or 2 (16-bit words); control byte n: 0..127 -> n+1 literal units,
 * 129..255 -> repeat next unit 257-n times (2..128). */
static void packbits(Buf *o, const unsigned char *p, size_t units, int u)
{
	size_t i = 0;
	while (i < units) {
		size_t run = 1;
		while (i + run < units && run < 128 && !memcmp(p + (i + run) * u, p + i * u, u)) run++;
		if (run >= 2) { put8(o, 257 - run); buf_put(o, p + i * u, u); i += run; continue; }
		{
			size_t j = i, lit = 0;
			while (j < units && lit < 128) {
				if (j + 1 < units && !memcmp(p + (j + 1) * u, p + j * u, u)) break;
				j++; lit++;
			}
			put8(o, lit - 1); buf_put(o, p + i * u, lit * u); i += lit;
		}
	}
}

/* --------------------------------------------------------------- images */
/* EUI1: "EUI1" u16 w u16 h u8 format(1=PAL8,2=RGB565) u8 flags(bit0 PackBits)
 *       u16 ncolors, ncolors*RGB, u32 datalen, data (rows top-down) */
static int convert_bmp(const unsigned char *f, size_t n, Buf *o, char *why, int wl)
{
	unsigned long off, hsz, comp, ncol;
	long w, h;
	int bpp, topdown, fmt;
	unsigned char *pix;
	size_t units, i;
	long x, y;
	Buf pk = { 0 };
	if (n < 54 || f[0] != 'B' || f[1] != 'M') { snprintf(why, wl, "not a BMP"); return -1; }
	off = rd32le(f + 10); hsz = rd32le(f + 14);
	w = (long)rd32le(f + 18); h = (long)rd32le(f + 22);
	bpp = rd16le(f + 28); comp = rd32le(f + 30); ncol = rd32le(f + 46);
	topdown = h < 0; if (topdown) h = -h;
	if (w <= 0 || h <= 0 || w > 32767 || h > 32767) { snprintf(why, wl, "bad size"); return -1; }
	if (!((bpp == 8 && (comp == 0 || comp == 1)) || (bpp == 4 && comp == 0) || (bpp == 24 && comp == 0))) {
		snprintf(why, wl, "unsupported BMP (%d bpp, compression %lu)", bpp, comp); return -1;
	}
	fmt = bpp == 24 ? 2 : 1;
	if (fmt == 1 && ncol == 0) ncol = 1UL << bpp;
	if (fmt == 1 && 14 + hsz + ncol * 4 > n) { snprintf(why, wl, "palette past end"); return -1; }
	units = (size_t)w * h;
	pix = xmalloc(units * (fmt == 2 ? 2 : 1));
	memset(pix, 0, units * (fmt == 2 ? 2 : 1));
	if (bpp == 8 && comp == 1) {               /* RLE8, bottom-up */
		size_t p = off;
		x = 0; y = h - 1;
		while (p + 1 < n && y >= 0) {
			unsigned a = f[p], b = f[p + 1];
			p += 2;
			if (a) { while (a--) { if (x < w) pix[y * w + x] = (unsigned char)b; x++; } }
			else if (b == 0) { x = 0; y--; }
			else if (b == 1) break;
			else if (b == 2) { if (p + 1 >= n) break; x += f[p]; y -= f[p + 1]; p += 2; }
			else { unsigned k; for (k = 0; k < b && p < n; k++, p++) { if (x < w) pix[y * w + x] = f[p]; x++; } if (b & 1) p++; }
		}
	} else {
		size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
		if (off + stride * h > n) { free(pix); snprintf(why, wl, "pixels past end"); return -1; }
		for (y = 0; y < h; y++) {
			const unsigned char *r = f + off + stride * (size_t)(topdown ? y : h - 1 - y);
			for (x = 0; x < w; x++) {
				if (bpp == 8) pix[y * w + x] = r[x];
				else if (bpp == 4) pix[y * w + x] = (x & 1) ? (r[x >> 1] & 15) : (r[x >> 1] >> 4);
				else {
					unsigned bl = r[x * 3], g = r[x * 3 + 1], rd = r[x * 3 + 2];
					unsigned v = ((rd >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3);
					pix[(y * w + x) * 2] = (unsigned char)(v >> 8);
					pix[(y * w + x) * 2 + 1] = (unsigned char)v;
				}
			}
		}
	}
	packbits(&pk, pix, units, fmt == 2 ? 2 : 1);
	buf_put(o, "EUI1", 4); put16(o, (unsigned)w); put16(o, (unsigned)h); put8(o, fmt);
	{
		int use = pk.n < units * (fmt == 2 ? 2 : 1) * 9 / 10;
		put8(o, use ? 1 : 0);
		put16(o, fmt == 1 ? (unsigned)ncol : 0);
		if (fmt == 1) for (i = 0; i < ncol; i++) {
			const unsigned char *q = f + 14 + hsz + i * 4;
			put8(o, q[2]); put8(o, q[1]); put8(o, q[0]);
		}
		if (use) { put32(o, (unsigned long)pk.n); buf_put(o, pk.p, pk.n); }
		else { put32(o, (unsigned long)(units * (fmt == 2 ? 2 : 1))); buf_put(o, pix, units * (fmt == 2 ? 2 : 1)); }
	}
	free(pk.p); free(pix);
	return 0;
}

/* ---------------------------------------------------------------- sound */
/* read a PCM WAV into mono 16-bit samples */
static short *wav_pcm(const unsigned char *f, size_t n, long *count, long *rate)
{
	size_t p = 12;
	int ch = 0, bits = 0;
	const unsigned char *data = NULL;
	unsigned long dlen = 0;
	long i, frames;
	short *out;
	if (n < 12 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4)) return NULL;
	while (p + 8 <= n) {
		unsigned long cl = rd32le(f + p + 4);
		if (!memcmp(f + p, "fmt ", 4) && p + 24 <= n) {
			if (rd16le(f + p + 8) != 1) return NULL;
			ch = rd16le(f + p + 10); *rate = (long)rd32le(f + p + 12); bits = rd16le(f + p + 22);
		} else if (!memcmp(f + p, "data", 4)) {
			data = f + p + 8; dlen = cl; if (p + 8 + dlen > n) dlen = (unsigned long)(n - p - 8);
		}
		p += 8 + cl + (cl & 1);
	}
	if (!data || ch < 1 || (bits != 8 && bits != 16)) return NULL;
	frames = (long)(dlen / (ch * bits / 8));
	out = xmalloc(frames * sizeof(short) + 2);
	for (i = 0; i < frames; i++) {
		long s = 0; int c;
		for (c = 0; c < ch; c++) {
			const unsigned char *q = data + ((size_t)i * ch + c) * (bits / 8);
			s += bits == 16 ? (short)rd16le(q) : ((int)q[0] - 128) * 256;
		}
		out[i] = (short)(s / ch);
	}
	*count = frames;
	return out;
}

/* resample mono 16-bit to dst rate: box-filter average over each output interval
 * (anti-alias for downsampling), linear interpolation when upsampling */
static short *resample(const short *in, long n, long src, long dst, long *outn)
{
	long m, i;
	short *o;
	if (src == dst) { o = xmalloc(n * 2 + 2); memcpy(o, in, n * 2); *outn = n; return o; }
	m = (long)((double)n * dst / src);
	o = xmalloc(m * 2 + 2);
	for (i = 0; i < m; i++) {
		double a = (double)i * src / dst, b = (double)(i + 1) * src / dst;
		if (src > dst) {
			long k0 = (long)a, k1 = (long)b, k;
			double s = 0; int c = 0;
			if (k1 >= n) k1 = n - 1;
			for (k = k0; k <= k1 && k < n; k++) { s += in[k]; c++; }
			o[i] = (short)(c ? s / c : 0);
		} else {
			long k = (long)a; double t = a - k;
			short s0 = in[k < n ? k : n - 1], s1 = in[k + 1 < n ? k + 1 : n - 1];
			o[i] = (short)(s0 + (s1 - s0) * t);
		}
	}
	*outn = m;
	return o;
}

/* EUS1: "EUS1" u32 rate u32 length(even), signed 8-bit samples */
static int convert_wav(const unsigned char *f, size_t n, Buf *o)
{
	long cnt = 0, rate = 0, m, i;
	short *pcm = wav_pcm(f, n, &cnt, &rate), *r;
	if (!pcm) return -1;
	r = resample(pcm, cnt, rate, rate > 22050 ? 22050 : rate, &m);
	buf_put(o, "EUS1", 4); put32(o, rate > 22050 ? 22050 : rate); put32(o, (unsigned long)((m + 1) & ~1L));
	for (i = 0; i < m; i++) put8(o, (unsigned)(signed char)(r[i] >> 8));
	if (m & 1) put8(o, 0);
	free(pcm); free(r);
	return 0;
}

/* ------------------------------------------- music: MP3 -> 8-bit PCM, mono, 28 kHz
 * Raw signed 8-bit so the Amiga only copies bytes (no decoding cost on a 68k). Windowed-sinc
 * resampler (Blackman, 32 taps, 256 phases, cutoff 0.45 x 28 kHz), peak normalised, TPDF dither. */
#define MUSIC_RATE 28000
#define SINC_HALF  16
#define SINC_PH    256
static double sinc_tab[SINC_PH + 1][2 * SINC_HALF];

static void sinc_init(double cutoff)    /* cutoff as a fraction of the INPUT rate (0..0.5) */
{
	int ph, k;
	for (ph = 0; ph <= SINC_PH; ph++) {
		double frac = (double)ph / SINC_PH, sum = 0;
		for (k = 0; k < 2 * SINC_HALF; k++) {
			double x = (double)(k - SINC_HALF + 1) - frac, t = 2 * cutoff * x, w;
			double s = x == 0 ? 2 * cutoff : sin(3.14159265358979 * t) / (3.14159265358979 * x);
			double pos = (x + SINC_HALF) / (2 * SINC_HALF);          /* 0..1 across the window */
			w = pos <= 0 || pos >= 1 ? 0 : 0.42 - 0.5 * cos(2 * 3.14159265358979 * pos) + 0.08 * cos(4 * 3.14159265358979 * pos);
			sinc_tab[ph][k] = s * w; sum += s * w;
		}
		for (k = 0; k < 2 * SINC_HALF; k++) sinc_tab[ph][k] /= sum;  /* unity gain at DC */
	}
}

static int write_pcm8(const char *path, const short *in, long n, long src)
{
	FILE *f = fopen(path, "wb");
	long m = (long)((double)n * MUSIC_RATE / src), i;
	double *o = xmalloc(m * sizeof(double) + 8), peak = 1, g;
	unsigned long seed = 12345;
	signed char *b = xmalloc(m + 1);
	if (!f) { free(o); free(b); return -1; }
	sinc_init(src > MUSIC_RATE ? 0.45 * MUSIC_RATE / src : 0.45);
	for (i = 0; i < m; i++) {
		double a = (double)i * src / MUSIC_RATE, s = 0;
		long k0 = (long)a, k;
		const double *t = sinc_tab[(int)((a - k0) * SINC_PH + 0.5)];
		for (k = 0; k < 2 * SINC_HALF; k++) {
			long j = k0 + k - SINC_HALF + 1;
			if (j >= 0 && j < n) s += in[j] * t[k];
		}
		o[i] = s;
		if (s > peak) peak = s;
		if (-s > peak) peak = -s;
	}
	g = 126.0 / peak;
	for (i = 0; i < m; i++) {
		double r1, r2, v;
		long q;
		seed = seed * 1103515245UL + 12345UL; r1 = (double)((seed >> 16) & 0x7FFF) / 32768.0;
		seed = seed * 1103515245UL + 12345UL; r2 = (double)((seed >> 16) & 0x7FFF) / 32768.0;
		v = o[i] * g + (r1 - r2);                                     /* TPDF, +-1 LSB */
		q = (long)(v < 0 ? v - 0.5 : v + 0.5);
		b[i] = (signed char)(q > 127 ? 127 : q < -128 ? -128 : q);
	}
	fwrite("EUM1", 1, 4, f);
	fputc((MUSIC_RATE >> 24) & 255, f); fputc((MUSIC_RATE >> 16) & 255, f); fputc((MUSIC_RATE >> 8) & 255, f); fputc(MUSIC_RATE & 255, f);
	fputc((int)((m >> 24) & 255), f); fputc((int)((m >> 16) & 255), f); fputc((int)((m >> 8) & 255), f); fputc((int)(m & 255), f);
	fwrite(b, 1, (size_t)m, f);
	free(o); free(b);
	return fclose(f);
}

static int convert_mp3(const unsigned char *f, size_t n, const char *outpath, double *secs)
{
	mp3dec_t d;
	mp3dec_frame_info_t info;
	short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	size_t p = 0;
	long cnt = 0, cap = 1 << 20, hz = 0, m;
	short *mono = xmalloc(cap * 2);
	int rc;
	mp3dec_init(&d);
	while (p < n) {
		int k, smp = mp3dec_decode_frame(&d, f + p, (int)(n - p), pcm, &info);
		if (info.frame_bytes == 0) break;
		p += info.frame_bytes;
		if (smp <= 0) continue;
		if (!hz) hz = info.hz;
		if (cnt + smp > cap) { cap = (cnt + smp) * 2; mono = realloc(mono, cap * 2); if (!mono) die("out of memory"); }
		for (k = 0; k < smp; k++)
			mono[cnt++] = info.channels == 2 ? (short)((pcm[2 * k] + pcm[2 * k + 1]) / 2) : pcm[k];
	}
	if (!cnt || !hz) { free(mono); return -1; }
	rc = write_pcm8(outpath, mono, cnt, hz);
	*secs = (double)cnt / hz;
	free(mono);
	return rc;
}

/* ------------------------------------------- frontend palette (the menus' own 256 colours)
 * The Amiga screen is 8-bit; the menus' 24-bit backgrounds drawn with the map palette come out
 * posterised. A 15-bit histogram of the frontend art and the shields is cut (median cut) to 253
 * colours; pens 17-19 are left for the AGA pointer (as in eui_build_inverse). Written into gfx.pak as
 * gfx/palette/amieu2_frontend.pal (768 bytes RGB). Derived from the user's data, never shipped. */
static unsigned long fp_hist[32768];

static int path_has(const char *p, const char *s)
{
	size_t n = strlen(s);
	for (; *p; p++) { size_t i; for (i = 0; i < n && p[i] && tolower((unsigned char)p[i]) == s[i]; i++) ; if (i == n) return 1; }
	return 0;
}

static void fp_add_bmp(const unsigned char *f, size_t n, unsigned weight)
{
	unsigned long off;
	long w, h, x, y, stride;
	int bpp;
	if (n < 54 || f[0] != 'B' || f[1] != 'M') return;
	off = rd32le(f + 10); w = (long)rd32le(f + 18); h = (long)rd32le(f + 22); bpp = rd16le(f + 28);
	if (h < 0) h = -h;
	if (bpp != 24 || rd32le(f + 30) != 0) return;
	stride = (w * 3 + 3) & ~3L;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const unsigned char *p = f + off + y * stride + x * 3;
			int b = p[0], g = p[1], r = p[2];
			if (off + (size_t)(y * stride + x * 3 + 3) > n) return;
			if (g >= 240 && r <= 16 && b <= 16) continue;          /* the transparent key */
			if (r >= 240 && g <= 16 && b <= 16) continue;          /* the shadow key */
			fp_hist[(r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)] += weight;
		}
}

typedef struct { int lo[3], hi[3]; unsigned long n; } FpBox;

static void fp_shrink(FpBox *b)
{
	int c, k, lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 };
	unsigned long n = 0;
	int r, g, bl;
	for (r = b->lo[0]; r <= b->hi[0]; r++) for (g = b->lo[1]; g <= b->hi[1]; g++) for (bl = b->lo[2]; bl <= b->hi[2]; bl++) {
		unsigned long v = fp_hist[r << 10 | g << 5 | bl];
		int t[3];
		if (!v) continue;
		n += v; t[0] = r; t[1] = g; t[2] = bl;
		for (c = 0; c < 3; c++) { if (t[c] < lo[c]) lo[c] = t[c]; if (t[c] > hi[c]) hi[c] = t[c]; }
	}
	b->n = n;
	if (n) for (k = 0; k < 3; k++) { b->lo[k] = lo[k]; b->hi[k] = hi[k]; }
}

static void make_frontend_palette(unsigned char *pal)
{
	static FpBox box[256];
	int nb = 1, i, k, want = 253;
	box[0].lo[0] = box[0].lo[1] = box[0].lo[2] = 0; box[0].hi[0] = box[0].hi[1] = box[0].hi[2] = 31;
	fp_shrink(&box[0]);
	while (nb < want) {                                    /* split the box with the most pixels x longest side */
		int bi = -1, ax = 0, cut, c;
		unsigned long best = 0, acc = 0;
		for (i = 0; i < nb; i++) {
			int len = 0, a = 0;
			for (c = 0; c < 3; c++) if (box[i].hi[c] - box[i].lo[c] > len) { len = box[i].hi[c] - box[i].lo[c]; a = c; }
			if (len > 0 && box[i].n * (unsigned long)len > best) { best = box[i].n * (unsigned long)len; bi = i; ax = a; }
		}
		if (bi < 0) break;
		for (cut = box[bi].lo[ax]; cut < box[bi].hi[ax]; cut++) {          /* median along the axis */
			int r, g, b;
			for (r = box[bi].lo[0]; r <= box[bi].hi[0]; r++) for (g = box[bi].lo[1]; g <= box[bi].hi[1]; g++) for (b = box[bi].lo[2]; b <= box[bi].hi[2]; b++) {
				int t[3]; t[0] = r; t[1] = g; t[2] = b;
				if (t[ax] == cut) acc += fp_hist[r << 10 | g << 5 | b];
			}
			if (acc * 2 >= box[bi].n) break;
		}
		if (cut >= box[bi].hi[ax]) cut = box[bi].hi[ax] - 1;
		box[nb] = box[bi];
		box[bi].hi[ax] = cut; box[nb].lo[ax] = cut + 1;
		fp_shrink(&box[bi]); fp_shrink(&box[nb]);
		nb++;
	}
	memset(pal, 0, 768);
	for (i = 0, k = 0; i < 256; i++) {
		unsigned long n = 0, sr = 0, sg = 0, sb = 0;
		int r, g, b;
		if (i >= 17 && i <= 19) { pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (unsigned char)(i == 17 ? 0 : 255); continue; }
		if (k >= nb) continue;
		for (r = box[k].lo[0]; r <= box[k].hi[0]; r++) for (g = box[k].lo[1]; g <= box[k].hi[1]; g++) for (b = box[k].lo[2]; b <= box[k].hi[2]; b++) {
			unsigned long v = fp_hist[r << 10 | g << 5 | b];
			n += v; sr += v * (unsigned long)(r * 8 + 4); sg += v * (unsigned long)(g * 8 + 4); sb += v * (unsigned long)(b * 8 + 4);
		}
		if (n) { pal[i * 3] = (unsigned char)(sr / n); pal[i * 3 + 1] = (unsigned char)(sg / n); pal[i * 3 + 2] = (unsigned char)(sb / n); }
		k++;
	}
}

/* ------------------------------------------------- intro stills (conversion 4)
 * The intro videos are Bink 1; the Amiga shows still frames instead (user decision: video is
 * too heavy for a 68k). bink.c decodes them with tables read from the user's own RAD player
 * (avi/binkplay.exe); each chosen frame becomes a PAL8 EUI1 with its own palette: median cut
 * (largest squared error first) over the exact colours to 253 colours, pens 17-19 kept for the
 * AGA pointer as in the menus' palette, serpentine Floyd-Steinberg dithering. */
#ifndef STILL_DITHER
#define STILL_DITHER 1
#endif
static const KnownFile bink_player = { "avi/binkplay.exe", 103936UL, "3ce96d56503ba63779f5017086a589621a0fc44f" };

typedef struct { const char *video, *entry; int frame; long ms; } Still;
static const Still stills[] = {                     /* in showing order (intro/index.txt) */
	{ "paradox",    "intro/paradox.eui",       112, 3000 },    /* the logo, fully lit */
	{ "intro",      "intro/intro.eui",         123, 3000 },    /* the publisher's logo (frame 124 is black) */
	{ "eu2_intro",  "intro/eu2_intro_01.eui",  168, 10000 },   /* one still per caption card */
	{ "eu2_intro",  "intro/eu2_intro_02.eui",  456, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_03.eui",  648, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_04.eui",  816, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_05.eui", 1032, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_06.eui", 1248, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_07.eui", 1464, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_08.eui", 1608, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_09.eui", 1800, 6000 },
	{ "eu2_intro",  "intro/eu2_intro_10.eui", 2040, 5000 },
	{ "eu2_intro",  "intro/eu2_intro_11.eui", 2280, 4000 },    /* the title */
	{ "dedication", "intro/dedication.eui",     96, 4000 },
};
#define NSTILLS ((int)(sizeof stills / sizeof stills[0]))
#define NVIDEOS 4
static const char *const video_names[NVIDEOS] = { "paradox", "intro", "eu2_intro", "dedication" };

typedef struct { int r, g, b; unsigned long n; } QCol;
typedef struct { int lo, hi, axis; double err; } QBox;
static int q_axis;
static int qcol_cmp(const void *a, const void *b)
{
	const QCol *x = a, *y = b;
	int u = q_axis == 0 ? x->r : q_axis == 1 ? x->g : x->b, v = q_axis == 0 ? y->r : q_axis == 1 ? y->g : y->b;
	return u < v ? -1 : u > v;
}
static void qbox_stat(const QCol *c, QBox *bx)
{
	double s[3] = { 0, 0, 0 }, s2[3] = { 0, 0, 0 }, n = 0, best = -1;
	int i, k;
	for (i = bx->lo; i < bx->hi; i++) {
		double v[3]; v[0] = c[i].r; v[1] = c[i].g; v[2] = c[i].b;
		for (k = 0; k < 3; k++) { s[k] += v[k] * c[i].n; s2[k] += v[k] * v[k] * c[i].n; }
		n += c[i].n;
	}
	bx->err = 0; bx->axis = 0;
	for (k = 0; k < 3; k++) {
		double e = s2[k] - s[k] * s[k] / (n ? n : 1);
		bx->err += e;
		if (e > best) { best = e; bx->axis = k; }
	}
	if (bx->hi - bx->lo < 2) bx->err = 0;
}

static int u32_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

/* rgb (w*h*3) -> pal (768 bytes) + pix (w*h pens) */
static void quantise_still(const unsigned char *rgb, int w, int h, unsigned char *pal, unsigned char *pix, int dither)
{
	static QBox box[256];
	QCol *col;
	uint32_t *all;
	int ncol = 0, nb = 1, i, k, pen[253];
	long x, y, np = (long)w * h;
	all = xmalloc(sizeof *all * np);                           /* the exact colours with their counts */
	for (i = 0; i < np; i++) all[i] = (uint32_t)rgb[3 * i] << 16 | rgb[3 * i + 1] << 8 | rgb[3 * i + 2];
	qsort(all, np, sizeof *all, u32_cmp);
	col = xmalloc(sizeof *col * np);
	for (i = 0; i < np; i++) {
		if (i && all[i] == all[i - 1]) { col[ncol - 1].n++; continue; }
		col[ncol].r = (int)(all[i] >> 16); col[ncol].g = (int)(all[i] >> 8 & 255); col[ncol].b = (int)(all[i] & 255); col[ncol].n = 1;
		ncol++;
	}
	free(all);
	box[0].lo = 0; box[0].hi = ncol; qbox_stat(col, &box[0]);
	while (nb < 253) {                                        /* split the box with the largest error */
		int bi = 0, cut;
		unsigned long tot = 0, acc = 0;
		for (i = 1; i < nb; i++) if (box[i].err > box[bi].err) bi = i;
		if (box[bi].err <= 0) break;
		q_axis = box[bi].axis;
		qsort(col + box[bi].lo, box[bi].hi - box[bi].lo, sizeof *col, qcol_cmp);
		for (i = box[bi].lo; i < box[bi].hi; i++) tot += col[i].n;
		for (cut = box[bi].lo; cut < box[bi].hi - 1; cut++) { acc += col[cut].n; if (acc * 2 >= tot) break; }
		cut++;                                                /* [lo,cut) and [cut,hi), both non-empty */
		box[nb].lo = cut; box[nb].hi = box[bi].hi; box[bi].hi = cut;
		qbox_stat(col, &box[bi]); qbox_stat(col, &box[nb]);
		nb++;
	}
	memset(pal, 0, 768);
	pal[17 * 3] = pal[17 * 3 + 1] = pal[17 * 3 + 2] = 0;      /* pointer pens, as amieu2_frontend.pal */
	for (i = 18; i <= 19; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = 255;
	for (k = 0, i = 0; k < nb; k++, i++) {
		double s[3] = { 0, 0, 0 }, n = 0;
		int j;
		if (i == 17) i = 20;
		for (j = box[k].lo; j < box[k].hi; j++) { s[0] += (double)col[j].r * col[j].n; s[1] += (double)col[j].g * col[j].n; s[2] += (double)col[j].b * col[j].n; n += col[j].n; }
		for (j = 0; j < 3; j++) pal[i * 3 + j] = (unsigned char)(s[j] / n + 0.5);
		pen[k] = i;
	}
	free(col);
	{	/* map, Floyd-Steinberg (serpentine) when dithering */
		int *err = xmalloc(sizeof(int) * 3 * (w + 2) * 2), *cur = err, *nxt = err + 3 * (w + 2);
		memset(err, 0, sizeof(int) * 3 * (w + 2) * 2);
		for (y = 0; y < h; y++) {
			int dir = (y & 1) ? -1 : 1;
			long x0 = dir > 0 ? 0 : w - 1;
			memset(nxt, 0, sizeof(int) * 3 * (w + 2));
			for (x = x0; x >= 0 && x < w; x += dir) {
				const unsigned char *p = rgb + 3 * ((size_t)y * w + x);
				int v[3], c, bestk = 0;
				long bd = 0x7fffffffL;
				for (c = 0; c < 3; c++) {
					v[c] = p[c] + (dither ? cur[3 * (x + 1) + c] / 16 : 0);
					if (v[c] < 0) v[c] = 0;
					if (v[c] > 255) v[c] = 255;
				}
				for (k = 0; k < nb; k++) {
					const unsigned char *q = pal + 3 * pen[k];
					long dr = v[0] - q[0], dg = v[1] - q[1], db = v[2] - q[2], d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
					if (d < bd) { bd = d; bestk = k; }
				}
				pix[(size_t)y * w + x] = (unsigned char)pen[bestk];
				if (dither)
					for (c = 0; c < 3; c++) {
						int e = v[c] - pal[3 * pen[bestk] + c];
						cur[3 * (x + 1 + dir) + c] += e * 7;
						nxt[3 * (x + 1 - dir) + c] += e * 3;
						nxt[3 * (x + 1) + c] += e * 5;
						nxt[3 * (x + 1 + dir) + c] += e;
					}
			}
			{ int *t = cur; cur = nxt; nxt = t; }
		}
		free(err);
	}
}

static void eui_pal8(Buf *o, int w, int h, const unsigned char *pal, const unsigned char *pix)
{
	Buf pk = { 0 };
	size_t units = (size_t)w * h;
	int use, i;
	packbits(&pk, pix, units, 1);
	use = pk.n < units * 9 / 10;
	buf_put(o, "EUI1", 4); put16(o, (unsigned)w); put16(o, (unsigned)h); put8(o, 1); put8(o, use ? 1 : 0);
	put16(o, 256);
	for (i = 0; i < 768; i++) put8(o, pal[i]);
	if (use) { put32(o, (unsigned long)pk.n); buf_put(o, pk.p, pk.n); }
	else { put32(o, (unsigned long)units); buf_put(o, pix, units); }
	free(pk.p);
}

/* returns the number of stills written, -1 on error */
static int make_intro_stills(const char *outdir, Buf *video, const Buf *player)
{
	static BinkTables tab;
	char why[200];
	Pak ip;
	Buf idx = { 0 };
	int v, s, n = 0;
	if (bink_tables_from_player(player->p, player->n, &tab, why, sizeof why)) { fprintf(stderr, "intro: %s\n", why); return -1; }
	pak_open(&ip, outdir, "intro.pak");
	for (v = 0; v < NVIDEOS; v++) {
		Bink *k;
		int last = -1, f;
		unsigned char *rgb, *pix, pal[768];
		for (s = 0; s < NSTILLS; s++) if (!strcmp(stills[s].video, video_names[v]) && stills[s].frame > last) last = stills[s].frame;
		if (last < 0) continue;
		if (!video[v].p) { fprintf(stderr, "intro: avi/%s.bik missing\n", video_names[v]); return -1; }
		k = bink_open(video[v].p, video[v].n, &tab, why, sizeof why);
		if (!k) { fprintf(stderr, "intro: %s.bik: %s\n", video_names[v], why); return -1; }
		rgb = xmalloc((size_t)bink_width(k) * bink_height(k) * 3);
		pix = xmalloc((size_t)bink_width(k) * bink_height(k));
		for (f = 0; f <= last; f++) {
			if (bink_decode_next(k, why, sizeof why) < 0) { fprintf(stderr, "intro: %s.bik: %s\n", video_names[v], why); bink_close(k); return -1; }
			for (s = 0; s < NSTILLS; s++)
				if (!strcmp(stills[s].video, video_names[v]) && stills[s].frame == f) {
					Buf o = { 0 };
					bink_rgb24(k, rgb);
					quantise_still(rgb, bink_width(k), bink_height(k), pal, pix, STILL_DITHER);
					eui_pal8(&o, bink_width(k), bink_height(k), pal, pix);
					pak_add(&ip, stills[s].entry, o.p, o.n);
					printf("  still %-24s %s.bik frame %4d, %dx%d, %6lu bytes\n", stills[s].entry, video_names[v], f,
					       bink_width(k), bink_height(k), (unsigned long)o.n);
					free(o.p);
					n++;
				}
		}
		bink_close(k); free(rgb); free(pix);
	}
	{
		const char *hdr = "# intro stills, in showing order: <entry> <milliseconds>\n";
		buf_put(&idx, hdr, strlen(hdr));
		for (s = 0; s < NSTILLS; s++) {
			char l[96];
			snprintf(l, sizeof l, "%s %ld\n", stills[s].entry, stills[s].ms);
			buf_put(&idx, l, strlen(l));
		}
		pak_add(&ip, "intro/index.txt", idx.p, idx.n);
		free(idx.p);
	}
	pak_close(&ip);
	return n;
}

/* ------------------------------------------------------------ main flow */
typedef struct { Buf b; } Sink;
static int sink_fn(void *u, uint64_t pos, const unsigned char *p, size_t n) { (void)pos; buf_put(&((Sink *)u)->b, p, n); return 0; }

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void hex2bin(const char *h, unsigned char *o) { int i; for (i = 0; i < 20; i++) o[i] = (unsigned char)(hexval(h[2*i]) << 4 | hexval(h[2*i+1])); }

static int read_from_dir(const char *dir, const KnownFile *k, Buf *b)
{
	char path[1024];
	FILE *f;
	unsigned char tmp[65536];
	size_t got;
	snprintf(path, sizeof path, "%s/%s", dir, k->path);
	f = fopen(path, "rb");
	if (!f) return -1;
	while ((got = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(b, tmp, got);
	fclose(f);
	return 0;
}

/* one file outside the main list (the Bink player), checked by size + SHA-1 */
static int fetch_known(const char *dir, InnoSetup *s, const KnownFile *k, Buf *b)
{
	unsigned char want[20], h[20];
	Sha1 c;
	char err[256];
	hex2bin(k->sha1, want);
	if (dir) {
		if (read_from_dir(dir, k, b)) return -1;
	} else {
		Sink sk = { { 0 } };
		uint32_t d;
		for (d = 0; d < s->ndata; d++)
			if (s->data[d].file_size == k->size && !memcmp(s->data[d].sha1, want, 20)) break;
		if (d == s->ndata) return -1;
		if (inno_read_chunk(s, s->data[d].chunk_offset, s->data[d].chunk_size, s->data[d].file_offset + s->data[d].file_size, sink_fn, &sk, err, sizeof err)) {
			fprintf(stderr, "%s: %s\n", k->path, err); free(sk.b.p); return -1;
		}
		if (s->data[d].file_offset) memmove(sk.b.p, sk.b.p + s->data[d].file_offset, (size_t)s->data[d].file_size);
		sk.b.n = (size_t)s->data[d].file_size;
		*b = sk.b;
	}
	sha1_init(&c); sha1_update(&c, b->p, b->n); sha1_final(&c, h);
	if (!dir && b->n == k->size && memcmp(h, want, 20)) {
		/* Inno stores executables with x86 CALL/JMP targets made absolute ("call instruction
		 * optimisation"): every E8/E9 whose 4th address byte is 00 or FF had the address of the
		 * next instruction added to its low 24 bits, and that byte flipped when bit 23 of the
		 * relative value is set. Undo it; the SHA-1 below decides. */
		size_t i = 0;
		while (i + 4 < b->n) {
			if (b->p[i] != 0xE8 && b->p[i] != 0xE9) { i++; continue; }
			i++;
			if (b->p[i + 3] == 0x00 || b->p[i + 3] == 0xFF) {
				uint32_t x = b->p[i] | b->p[i + 1] << 8 | (uint32_t)b->p[i + 2] << 16;
				x = (x - (uint32_t)(i + 4)) & 0xFFFFFF;
				if (x & 0x800000) b->p[i + 3] ^= 0xFF;
				b->p[i] = (unsigned char)x; b->p[i + 1] = (unsigned char)(x >> 8); b->p[i + 2] = (unsigned char)(x >> 16);
				i += 4;
			}
		}
		sha1_init(&c); sha1_update(&c, b->p, b->n); sha1_final(&c, h);
	}
	if (b->n != k->size || memcmp(h, want, 20)) { free(b->p); b->p = NULL; return -1; }
	return 0;
}

typedef struct { const KnownFile *k; uint32_t chunk; int di; } Job;
static int job_cmp(const void *a, const void *b)
{
	const Job *x = a, *y = b;
	return x->chunk < y->chunk ? -1 : x->chunk > y->chunk;
}

int main(int argc, char **argv)
{
	const char *src, *outdir, *dir = NULL;
	InnoSetup s;
	char err[256], path[1024];
	Job *jobs;
	int i, nj = 0, bad = 0, missing = 0, nmus = 0, nimg = 0, nsnd = 0, nraw = 0, nstill = 0;
	Buf videos[NVIDEOS], player = { 0 };
	Pak text, map, gfx, sfx;
	FILE *idx, *stamp;
	time_t t0 = time(NULL);

	memset(videos, 0, sizeof videos);
	printf("eu2conv %d - Europa Universalis II (GOG v1.09) data converter for the Amiga port\n", CONVERSION_VERSION);
	if (argc == 4 && !strcmp(argv[1], "--dir")) { dir = argv[2]; outdir = argv[3]; src = dir; }
	else if (argc == 3) { src = argv[1]; outdir = argv[2]; }
	else {
		printf("usage: eu2conv <setup_europa_universalis2_2.0.0.1.exe> <output folder>\n"
		       "       eu2conv --dir \"<installed Europa Universalis 2 folder>\" <output folder>\n");
		return 2;
	}
	if (!dir) {
		if (inno_open(&s, src, err, sizeof err)) { fprintf(stderr, "eu2conv: %s\n", err); return 1; }
		printf("installer: %s (%u files)\n", s.version, s.ndata);
	}
	jobs = xmalloc(sizeof(Job) * KNOWN_FILES_COUNT);
	for (i = 0; i < KNOWN_FILES_COUNT; i++) {
		Job *j = &jobs[nj];
		j->k = &known_files[i]; j->di = -1; j->chunk = 0;
		if (!dir) {
			unsigned char want[20];
			uint32_t d;
			hex2bin(j->k->sha1, want);
			for (d = 0; d < s.ndata; d++)
				if (s.data[d].file_size == j->k->size && !memcmp(s.data[d].sha1, want, 20)) { j->di = (int)d; j->chunk = s.data[d].chunk_offset; break; }
			if (j->di < 0) { missing++; continue; }
		}
		nj++;
	}
	if (missing) { fprintf(stderr, "eu2conv: %d files of GOG v1.09 are not in this installer - is it the right one?\n", missing); return 1; }
	qsort(jobs, nj, sizeof *jobs, job_cmp);

	mkdirs(outdir);
	snprintf(path, sizeof path, "%s/music", outdir); mkdirs(path);
	pak_open(&text, outdir, "text.pak");
	pak_open(&map, outdir, "map.pak");
	pak_open(&gfx, outdir, "gfx.pak");
	pak_open(&sfx, outdir, "sfx.pak");
	snprintf(path, sizeof path, "%s/music/index.txt", outdir);
	idx = fopen(path, "w");
	if (!idx) die("cannot write the music index");

	for (i = 0; i < nj; i++) {
		const KnownFile *k = jobs[i].k;
		Sink sk = { { 0 } };
		unsigned char h[20], want[20];
		Sha1 c;
		char why[128] = "";
		if (dir) {
			if (read_from_dir(dir, k, &sk.b)) { fprintf(stderr, "missing: %s\n", k->path); bad++; continue; }
		} else {
			InnoData *d = &s.data[jobs[i].di];
			if (inno_read_chunk(&s, d->chunk_offset, d->chunk_size, d->file_offset + d->file_size, sink_fn, &sk, err, sizeof err)) {
				fprintf(stderr, "%s: %s\n", k->path, err); bad++; continue;
			}
			if (d->file_offset) { memmove(sk.b.p, sk.b.p + d->file_offset, (size_t)d->file_size); }
			sk.b.n = (size_t)d->file_size;
		}
		sha1_init(&c); sha1_update(&c, sk.b.p, sk.b.n); sha1_final(&c, h);
		hex2bin(k->sha1, want);
		if (sk.b.n != k->size || memcmp(h, want, 20)) {
			/* the game itself rewrites its settings files in an installed folder: not an error */
			if (dir && (ext_is(k->path, "ini") || ext_is(k->path, "cfg"))) {
				fprintf(stderr, "note: %s differs from GOG v1.09 (the game rewrites it) - used as found\n", k->path);
			} else { fprintf(stderr, "not the GOG v1.09 file: %s\n", k->path); bad++; free(sk.b.p); continue; }
		}

		if (ext_is(k->path, "bmp")) {
			Buf o = { 0 };
			if (convert_bmp(sk.b.p, sk.b.n, &o, why, sizeof why)) { fprintf(stderr, "%s: %s\n", k->path, why); bad++; }
			else { pak_add(&gfx, k->path, o.p, o.n); nimg++; }
			if (path_has(k->path, "interface/frontend/") || path_has(k->path, "interface\\frontend\\")) fp_add_bmp(sk.b.p, sk.b.n, 4);
			else if (path_has(k->path, "shields")) fp_add_bmp(sk.b.p, sk.b.n, 1);
			free(o.p);
		} else if (ext_is(k->path, "wav")) {
			Buf o = { 0 };
			if (convert_wav(sk.b.p, sk.b.n, &o)) { fprintf(stderr, "%s: unsupported WAV\n", k->path); bad++; }
			else { pak_add(&sfx, k->path, o.p, o.n); nsnd++; }
			free(o.p);
		} else if (ext_is(k->path, "mp3")) {
			char op[1100], base[256];
			const char *nm = strrchr(k->path, '/');
			double secs = 0;
			snprintf(base, sizeof base, "%s", nm ? nm + 1 : k->path);
			base[strlen(base) - 4] = 0;
			snprintf(op, sizeof op, "%s/music/m%02d.pcm", outdir, nmus);
			printf("  music %-40s ", base); fflush(stdout);
			if (convert_mp3(sk.b.p, sk.b.n, op, &secs)) { printf("FAILED\n"); bad++; }
			else { printf("%5.0f s\n", secs); fprintf(idx, "m%02d.pcm %s\n", nmus, base); nmus++; }
		} else if (ext_is(k->path, "bik")) {
			/* videos: kept in memory, a few still frames go into intro.pak after the loop */
			int v;
			for (v = 0; v < NVIDEOS; v++) {
				char nm[64];
				snprintf(nm, sizeof nm, "avi/%s.bik", video_names[v]);
				if (!strcmp(k->path, nm) && !videos[v].p) { videos[v] = sk.b; sk.b.p = NULL; }
			}
		} else if (ext_is(k->path, "tbl")) {
			pak_add(&map, k->path, sk.b.p, sk.b.n); nraw++;
		} else {
			pak_add(&text, k->path, sk.b.p, sk.b.n); nraw++;
		}
		free(sk.b.p);
		if ((i & 255) == 0) { printf("  %d / %d files\n", i, nj); fflush(stdout); }
	}
	fclose(idx);
	printf("intro stills:\n");                           /* conversion 4 */
	if (fetch_known(dir, &s, &bink_player, &player)) { fprintf(stderr, "missing or not the GOG v1.09 file: %s\n", bink_player.path); bad++; }
	else if ((nstill = make_intro_stills(outdir, videos, &player)) < 0) { bad++; nstill = 0; }
	free(player.p);
	for (i = 0; i < NVIDEOS; i++) free(videos[i].p);
	printf("packs:\n");
	{	/* the menus' palette (conversion 3) */
		unsigned char fpal[768];
		make_frontend_palette(fpal);
		pak_add(&gfx, "gfx/palette/amieu2_frontend.pal", fpal, sizeof fpal);
	}
	pak_close(&text); pak_close(&map); pak_close(&gfx); pak_close(&sfx);
	if (!dir) inno_close(&s);

	snprintf(path, sizeof path, "%s/conversion.txt", outdir);
	stamp = fopen(path, "w");
	if (stamp) {
		fprintf(stamp, "%s %d\nsource GOG v1.09 %s\nimages %d\nsounds %d\nmusic %d\nraw %d\nstills %d\nerrors %d\n",
		        CONV_TAG, CONVERSION_VERSION, dir ? "installed folder" : "installer", nimg, nsnd, nmus, nraw, nstill, bad);
		fclose(stamp);
	}
	printf("%d images, %d sounds, %d music tracks, %d data files, %d intro stills, %d errors, %ld s\n",
	       nimg, nsnd, nmus, nraw, nstill, bad, (long)(time(NULL) - t0));
	if (bad) { printf("CONVERSION INCOMPLETE - see the messages above\n"); return 1; }
	printf("done: copy the folder \"%s\" to the Amiga as PROGDIR:data\n", outdir);
	return 0;
}
