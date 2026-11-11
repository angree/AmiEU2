/* inno.h - minimal reader for the GOG Inno Setup 5.5.0 (unicode) installer of EU2.
 * Written for this project from the public description of the format (innoextract's
 * documentation/sources were used as reference only). Reads just what eu2conv needs:
 * the loader offset table, the data-entry block (file locations + SHA-1) and the
 * compressed chunks. File NAMES are not parsed from the installer: eu2conv identifies
 * every file by its SHA-1 against its own table (knownfiles.h). */
#ifndef EU2_INNO_H
#define EU2_INNO_H
#include <stdio.h>
#include <stdint.h>

typedef struct {
	uint32_t chunk_offset;     /* relative to data_offset */
	uint64_t file_offset;      /* offset inside the decompressed chunk */
	uint64_t file_size;
	uint64_t chunk_size;       /* compressed size of the whole chunk */
	uint8_t  sha1[20];
	uint32_t flags;            /* raw option bits */
} InnoData;

typedef struct {
	FILE    *f;
	char     version[65];
	uint32_t header_offset;
	uint32_t data_offset;
	uint32_t ndata;
	InnoData *data;
	unsigned char *block1;     /* decompressed setup header + entries (not parsed) */
	size_t   block1_len;
} InnoSetup;

int  inno_open(InnoSetup *s, const char *path, char *err, int errlen);
void inno_close(InnoSetup *s);

/* Streams one chunk (all files that share chunk_offset) through LZMA; for every
 * produced byte range calls sink(user, pos_in_chunk, bytes, n). Returns 0 on success. */
typedef int (*inno_sink)(void *user, uint64_t pos, const unsigned char *p, size_t n);
int  inno_read_chunk(InnoSetup *s, uint32_t chunk_offset, uint64_t chunk_size,
                     uint64_t need_bytes, inno_sink sink, void *user, char *err, int errlen);

uint32_t crc32_buf(uint32_t crc, const unsigned char *p, size_t n);
#endif
