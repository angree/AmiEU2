/* bink.h - Bink 1 ("BIKi") video decoder for eu2conv: decodes the intro videos of the
 * player's own EU2 copy so the converter can keep a few still frames.
 *
 * Written for this project from the public description of the format (MultimediaWiki
 * "Bink Video") plus experiments on the real files. No code from FFmpeg or any other
 * decoder. The format's constant tables (Huffman decoding tables, run-block scan
 * patterns, coefficient scan, dequantisation matrices) are NOT in this source: they are
 * read at conversion time out of the user's own RAD Bink player (avi/binkplay.exe,
 * UPX-packed; bink_tables_from_player unpacks it), so nothing of RAD's is in the repo.
 * Video only: audio packets are skipped. */
#ifndef EU2_BINK_H
#define EU2_BINK_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t  pattern[16][64];    /* run-block scan patterns (raster index 0..63) */
	uint8_t  huff_bits[16];      /* peek width of each Huffman decoding table */
	uint8_t  huff[16][128];      /* decoding table: entry = code length << 4 | symbol */
	uint8_t  rle[4];             /* run lengths of block-type symbols 12..15 */
	uint8_t  scan[64];           /* DCT coefficient order (raster index) */
	int32_t  qintra[16][64];     /* dequantisation, raster order, * 2^-11 */
	int32_t  qinter[16][64];
} BinkTables;

/* exe = the whole avi/binkplay.exe of GOG EU2 v1.09 (the caller checks its SHA-1).
 * Returns 0 on success, else -1 and a reason in why. */
int bink_tables_from_player(const unsigned char *exe, size_t n, BinkTables *t, char *why, int wl);

typedef struct Bink Bink;

/* data must stay valid while the Bink is open. */
Bink *bink_open(const unsigned char *data, size_t n, const BinkTables *t, char *why, int wl);
int   bink_width(const Bink *b);
int   bink_height(const Bink *b);
int   bink_frames(const Bink *b);
int   bink_fps_num(const Bink *b);
int   bink_fps_den(const Bink *b);
/* decodes the next frame (frames must be decoded in order: inter blocks need the
 * previous picture). Returns the frame number decoded, or -1 on error/end. */
int   bink_decode_next(Bink *b, char *why, int wl);
/* plane 0 = Y (width x height), 1 = U (Cb), 2 = V (Cr) (half size, rounded up) */
const uint8_t *bink_plane(const Bink *b, int plane, int *stride);
/* the current frame as RGB24, width*height*3 bytes, rows top-down */
void  bink_rgb24(const Bink *b, uint8_t *out);
void  bink_close(Bink *b);
#endif
