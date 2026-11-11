/* sprite.h - EU2 map sprites: a text .spr (text.pak) naming a strip bitmap (gfx.pak) of Frames
 * frames side by side, with an Origin (hot spot). Converted once to screen pens through the screen
 * palette's inverse table. Pixels of the colour key (0,255,0) become SPR_CLEAR, pure red (255,0,0)
 * (the ground shadow, NIEPEWNE) becomes SPR_SHADOW; neither is drawn yet. Portable C. */
#ifndef EU2_SPRITE_H
#define EU2_SPRITE_H
#include "pak.h"

#define SPR_CLEAR  17          /* pointer pens never occur in mapped pixels, so they can mark */
#define SPR_SHADOW 18

typedef struct {
	int w, h, frames, ox, oy;  /* w = one frame */
	unsigned char *pens;       /* frames*w x h */
	unsigned short *runs;      /* per frame row: count, then (x, len, kind) triplets; kind 1 = shadow */
	unsigned long *rowoff;     /* frames*h offsets into runs */
} Sprite;

/* spr: pak name of the .spr, e.g. "gfx/map/units/t-army c-eng a-stand l-1.spr" */
int  sprite_load(Sprite *s, Pak *text, Pak *gfx, const char *spr, const unsigned char *inv);
/* the same scaled by scale/1000 (< 1000) with Lanczos-3, cached on disk (sprite_set_cache_dir) */
int  sprite_load_scaled(Sprite *s, Pak *text, Pak *gfx, const char *spr, const unsigned char *inv, int scale);
/* sprite_draw shrunk by 2^sh (the exe's map objects, +0x23e = base + zoom shift: every 2^sh-th pixel of every
 * 2^sh-th row, the origin offset shifted too); sh 0 = sprite_draw */
void sprite_draw_shift(const Sprite *s, int f, unsigned char *dst, int pitch, int w, int h, int x, int y, int sh);
/* any bitmap from gfx.pak as a sprite of "frames" frames side by side, scaled by scale/1000 */
int  sprite_from_bmp(Sprite *s, Pak *gfx, const char *path, const unsigned char *inv, int frames, int scale);
/* pen -> darker pen table for shadow pixels (NULL: shadows are skipped) */
void sprite_set_shadow_table(const unsigned char *tab);
const unsigned char *sprite_get_shadow_table(void);
/* where scaled sprites are cached (directory must exist; "" = no cache); inv identifies the palette */
void sprite_set_cache_dir(const char *dir, const unsigned char *inv);
void sprite_free(Sprite *s);
/* draws frame f with its origin at (x,y); clips to w x h */
void sprite_draw(const Sprite *s, int f, unsigned char *dst, int pitch, int w, int h, int x, int y);
#endif
