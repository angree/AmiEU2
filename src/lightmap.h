/* lightmap.h - EU2 map tiles (map/lightmapN.tbl, docs/MAP_FORMAT.md). Portable C.
 * The whole file is kept compressed in RAM (L1 11.3 MB, L2 4 MB, L3 1.7 MB); tiles of 32x32
 * pixels are decoded on demand into province ids + 6-bit shade. */
#ifndef EU2_LIGHTMAP_H
#define EU2_LIGHTMAP_H
#include "pak.h"

#define LM_SPECIAL 0x7E4      /* palette values >= this are border/river pixels */

typedef struct {
	unsigned char *raw;
	unsigned long size, base;  /* base = start of the data block */
	int level, tw, th;         /* tiles across / down */
	long ntiles;
} Lightmap;

typedef struct { unsigned char x, y, lg, shade; unsigned short val; } LmLeaf;

int  lm_load(Lightmap *lm, Pak *map, int level);   /* 0 = ok */
void lm_free(Lightmap *lm);
/* leaves of tile (tx wraps around the globe, ty clamped); returns the leaf count (<= 1024)
 * with val already resolved: border/river values become the province they belong to */
int  lm_leaves(const Lightmap *lm, int tx, int ty, LmLeaf *out);
/* full tile: ids[32*32] province ids, shade[32*32] 0..63 (bilinear exactly as eu2.exe) */
void lm_tile(const Lightmap *lm, int tx, int ty, unsigned short *ids, unsigned char *shade);
/* tile as screen pens: pens[32*32] = prov_tab[id][shade + dither(0..7)] (tables of >= 72 bytes) */
extern unsigned long lm_flat_px, lm_slope_px;
/* pens from a tile's ids + shade (lm_tile) the caller kept: the dither alone, no tree decode */
void lm_pens_from(const unsigned char *shade, const unsigned short *ids, unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab);
void lm_tile_pens(const Lightmap *lm, int tx, int ty, unsigned char *pens, const unsigned char *const *prov_tab, int nprov_tab);
/* drop the internal leaf cache (after freeing a Lightmap) */
/* both at once (country borders need the ids) */
void lm_tile_pens_ids(const Lightmap *lm, int tx, int ty, unsigned char *pens, unsigned short *ids, const unsigned char *const *prov_tab, int nprov_tab);
void lm_cache_reset(void);
/* does tile (tx, ty) hold province p? Only its palette is read (the ids the tile uses), no decoding */
int  lm_tile_has(const Lightmap *lm, int tx, int ty, unsigned p);
/* ADJACENT.TBL as the decoder needs it (start[np + 1] into prov[], file order): a border/river pixel whose bits
 * 5..8 (b) are not 15 becomes neighbour b of its province - that is how rivers get their own (water) province
 * (eu2.exe 0x476f84). Without it the pixel stays its province (the old behaviour). */
void lm_set_adjacency(const unsigned short *start, const unsigned short *prov, int np);
#endif
