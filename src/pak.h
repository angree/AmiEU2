/* pak.h - reader for the EU2P packs written by eu2conv (docs/DATA_FORMAT.md).
 * Portable C: builds for the Amiga (libnix stdio) and for the host. */
#ifndef EU2_PAK_H
#define EU2_PAK_H
#include <stdio.h>

typedef struct {
	unsigned long off, size;
	const char *name;          /* lower case, '/' separators */
} PakEntry;

typedef struct {
	FILE *f;
	unsigned long count;
	PakEntry *ent;             /* sorted by name */
	char *names;               /* storage for all names */
} Pak;

int  pak_open(Pak *p, const char *path);      /* 0 = ok */
void pak_close(Pak *p);
const PakEntry *pak_find(const Pak *p, const char *name);   /* name: any case, '/' or '\\' */
/* reads a whole entry into a new malloc'ed buffer; NULL if missing / no memory */
unsigned char *pak_load(Pak *p, const char *name, unsigned long *size);

/* big-endian helpers shared by the data readers */
#define BE16(p) ((unsigned)(((const unsigned char *)(p))[0] << 8 | ((const unsigned char *)(p))[1]))
#define BE32(p) ((unsigned long)BE16(p) << 16 | BE16((const unsigned char *)(p) + 2))
#endif
