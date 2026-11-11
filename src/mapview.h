/* mapview.h - draws the political map into an 8-bit chunky buffer. Portable C.
 * Palette layout (screen pens):
 *   0..31   UI / pointer (17..19 = hardware pointer)
 *   32..239 26 colour scales (Map/colorscales.csv) x 8 light levels
 *   240..255 free for the UI
 * A tile cache keeps decoded 32x32 tiles as pens, so scrolling only decodes the new edge. */
#ifndef EU2_MAPVIEW_H
#define EU2_MAPVIEW_H
#include "pak.h"
#include "world.h"
#include "lightmap.h"

#define MV_LEVELS 3            /* the game uses lightmap1..3 */
#define MV_PEN_SCALES 32

typedef struct MapView MapView;

/* a province shade (color_fn's sh[]) with this bit draws at half brightness: the fog of war */
#define MV_SHADE_HALF 0x40
MapView *mv_create(Pak *text, Pak *map, World *w, int cache_tiles, char *err, int errlen);
void     mv_destroy(MapView *v);
/* the 256-entry screen palette (RGB) the map expects */
const unsigned char *mv_palette(const MapView *v);
/* view position: centre in FULL-resolution map pixels (18944 x 7296), zoom 1..3 (1 = closest) */
void mv_set(MapView *v, long cx, long cy, int zoom);
void mv_get(const MapView *v, long *cx, long *cy, int *zoom);
/* draws w x h pixels at dst (pitch bytes per row). Returns the number of tiles decoded. */
int  mv_draw(MapView *v, unsigned char *dst, int pitch, int w, int h);
/* only the rectangle rx, ry, rw x rh of the w x h view (dst = the view's origin); also returns the view's top-left in
 * level pixels and the level's width (the map layer's scroll: copy what stays, draw what is new) */
int  mv_draw_rect(MapView *v, unsigned char *dst, int pitch, int w, int h, int rx, int ry, int rw, int rh, long *lx0, long *ly0, long *lw);
unsigned long mv_gen(const MapView *v);         /* changes whenever the drawn map pixels would */
void mv_set_scr_off2(MapView *v, int offx, int offy);
/* probes (eu2.cfg probemodes=1): a microsecond clock, then the summed time of the tile work since the last read:
 * t[0] lightmap decode + colour, t[1] the border pass, t[2] the selection remap, t[3] assign_scales */
void mv_probe_clock(unsigned long (*us)(void));
void mv_probe_read(unsigned long t[6]);   /* t[4] tile_core, t[5] fs_pens (inside t[0]) */
/* screens taller than the sidebar: mv_draw also fills `left` px left of dst for rows y0..h-1 (the map continued
 * under the sidebar; mv_province_at then takes x down to -left there). 0 = off. */
void mv_set_left(MapView *v, int left, int y0);
/* added to every mv_to_screen x (redraw's overlays drawn from the screen's x 0 instead of the map's) */
void mv_set_scr_off(MapView *v, int off);
/* forget cached tiles (ownership changed) */
void mv_flush(MapView *v);
/* the centre of a province's bounding box (boundbox.tbl), full-resolution pixels; 0 = none */
int mv_prov_centre(const MapView *v, int prov, long *x, long *y);
/* "Buffer province repaint" (eu2setup, mapbuf=1): keep each cached tile's decoded province ids + shade
 * (3 KB a tile slot: 3 MB with the 32x32 cache, 12 MB with 64x64), so a repaint (map mode, owners,
 * winter) skips the lightmap tree. Returns 0 on, -1 when the memory is not there (stays off). */
int mv_set_idcache(MapView *v, int on);
/* province owners changed: recolour (re-derives the scale of every province and flushes the cache) */
void mv_refresh_owners(MapView *v);
/* map modes: after the political colours are assigned (owner scale per province, water, Terra
 * Incognita), fn may rewrite scale[0..n-1] (n = provinces) with scale indices; NULL = political */
typedef void (*MvColorFn)(void *user, const MapView *v, unsigned char *scale, unsigned char *shade, int n);   /* shade: 0..31, 15 = normal, larger = lighter; 255 = terra incognita (flat, no borders) */
void mv_set_color_fn(MapView *v, MvColorFn fn, void *user);
/* the selected province draws darker: shade - 16, at least 0 (eu2.exe 0x482870, not in the trade mode: dark = 0).
 * Only that province's table changes and only the cached tiles in its bounding box (boundbox.tbl) are redone. */
void mv_select(MapView *v, int prov, int dark);
int  mv_adj(const MapView *v, int prov, const unsigned short **list);   /* ADJACENT.TBL neighbours of prov (count) */   /* recolours at once */
void mv_set_borders(MapView *v, int on);                        /* country borders in the tiles (normal map) */
int  mv_scale_named(const MapView *v, const char *name);       /* colorscales.csv name ("dark green"), -1 none */
/* the country scale whose middle shade is nearest to r,g,b (the csv names do not describe the colours:
 * "light yellow" is orange, "dark brown" purple, "light brown" cyan) */
int  mv_scale_rgb(const MapView *v, int r, int g, int b);
int  mv_country_scale(const MapView *v, int country);           /* the political scale of a country */
/* province id under a screen pixel of the last drawn frame (-1 none) */
int  mv_province_at(MapView *v, int sx, int sy, int w, int h);
/* full-resolution map position -> pixel of the last drawn frame (may be off screen); 0 if not drawn */
int  mv_to_screen(const MapView *v, long fx, long fy, int *sx, int *sy);
#endif
