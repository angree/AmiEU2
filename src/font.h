/* font.h - EU2 bitmap fonts (gfx/fonts/<name>.bmp). Portable C.
 * Format (worked out from the files): a W-pixel-wide strip of W x W cells, one per character
 * from '!' (33) upwards; pen 1 (and the odd pen-0 pixel) = ink, pen 255 = background;
 * advance = rightmost ink + 2. Space advances W/3. */
#ifndef EU2_FONT_H
#define EU2_FONT_H
#include "pak.h"

typedef struct {
	int cell, first, count;
	unsigned char *bits;       /* count * cell * cell, 1 = ink */
	unsigned char adv[256];
} Font;

int  font_load(Font *f, Pak *gfx, const char *name);    /* name e.g. "garamond14" ; 0 = ok */
void font_free(Font *f);
int  font_load_euf(Font *f, const char *path);          /* our EUF1 faces (tools/mkfont), 0 = ok */
void font_set_cc0(const char *dir, int big);            /* font_load takes the CC0 faces from dir (NULL: the game's) */
int  font_width(const Font *f, const char *s);
void font_set_code_pens(int yellow, int white);   /* the pens for the texts' colour codes (0xA7 Y / W) */
/* draws s at (x,y) into an 8-bit buffer with pen; clips to w x h; returns the end x */
int  font_draw(const Font *f, unsigned char *dst, int pitch, int w, int h, int x, int y, const char *s, int pen);
/* tests (textprobe=1): every text drawn between begin and report is kept; report lists the texts that
 * overlap one another or cross x = edge (the sidebar's right side), once per pair */
void font_probe_begin(void);
int  font_probe_report(const char *tag, int x0, int x1, int (*right)(int y0, int y1), void (*log)(const char *));   /* right: the visible edge on rows y0..y1 (NULL: x1) */
#endif
