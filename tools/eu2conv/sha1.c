/* sha1.c - small SHA-1 (FIPS 180-1), written for eu2conv. */
#include <string.h>
#include "sha1.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(Sha1 *c, const unsigned char *p)
{
	uint32_t w[80], a, b, d, e, f, k, t, cc;
	int i;
	for (i = 0; i < 16; i++) w[i] = ((uint32_t)p[4*i] << 24) | (p[4*i+1] << 16) | (p[4*i+2] << 8) | p[4*i+3];
	for (i = 16; i < 80; i++) w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
	for (i = 0; i < 80; i++) {
		if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
		else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
		else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
		t = ROL(a, 5) + f + e + k + w[i];
		e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void sha1_init(Sha1 *c)
{
	c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
	c->len = 0; c->n = 0;
}

void sha1_update(Sha1 *c, const void *data, size_t n)
{
	const unsigned char *p = data;
	c->len += n;
	while (n) {
		size_t take = 64 - c->n;
		if (take > n) take = n;
		memcpy(c->buf + c->n, p, take);
		c->n += take; p += take; n -= take;
		if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
	}
}

void sha1_final(Sha1 *c, unsigned char out[20])
{
	uint64_t bits = c->len * 8;
	unsigned char pad = 0x80, z = 0, l[8];
	int i;
	sha1_update(c, &pad, 1);
	while (c->n != 56) sha1_update(c, &z, 1);
	for (i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (56 - 8 * i));
	sha1_update(c, l, 8);
	for (i = 0; i < 20; i++) out[i] = (unsigned char)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}
