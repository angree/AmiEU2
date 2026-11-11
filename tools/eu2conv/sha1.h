#ifndef EU2_SHA1_H
#define EU2_SHA1_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[5]; uint64_t len; unsigned char buf[64]; size_t n; } Sha1;
void sha1_init(Sha1 *c);
void sha1_update(Sha1 *c, const void *data, size_t n);
void sha1_final(Sha1 *c, unsigned char out[20]);
#endif
