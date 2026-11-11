#ifndef EU2_UNITS_H
#define EU2_UNITS_H
#include "pak.h"
#include "world.h"
#include "mapview.h"
#include "military.h"
#include "sprite.h"
/* inv: 4096-byte RGB444 -> screen pen table of the map palette (eui_build_inverse) */
void units_init(Pak *text, Pak *gfx, const unsigned char *inv);
/* draws one sprite per province holding armies (and one per fleet location) below row 'top';
 * returns the number drawn */
void units_select(int army, int fleet);
void units_select_list(const short *armies, int na, const short *fleets, int nf);   /* the whole selection */
void units_set_time(unsigned long ms);                    /* animation clock */
void units_set_today(long day);                          /* the game day (route progress) */
void units_order_anim(int x, int y, int ok);              /* moveorderanim / illegalmoveorder once at (x,y) of the map area */
int  units_anim_active(void);
int  units_pick(const World *w, const MapView *mv, int sx, int sy, int player, int *naval);   /* province of our unit sprite there, 0 none */
int  units_in_rect(const World *w, const MapView *mv, int x0, int y0, int x1, int y1, int player, short *out, int max, int *naval);
void units_set_viewer(int c);                    /* whose discoveries decide what is drawn (-1 all) */
void units_set_sizes(int z1, int z2, int z3);    /* army / navy size per zoom, per mille (unitsize= in eu2.cfg) */
void units_set_anim(int on);                     /* 1: the unit sprites animate (A-STAND / A-WALK frames); 0 frame 0 */
void units_set_fog(const unsigned char *vis);    /* fog of war: per province 1 = visible; other countries' units drawn only there (NULL off) */
int  units_draw_cities(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh);   /* cities, colonies, posts */
int  units_draw_sieges(const World *w, const MapView *mv, const Military *m, unsigned char *dst, int pitch, int sw, int sh);
int  units_draw_cores(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int player);   /* political map */
int  units_draw_terrain(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int zoom);   /* normal map mode */          /* the selected unit gets a ring, its order a destination mark */
int  units_draw(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int top);
/* the diplomatic map modes: every known capital gets its owner's shield, and against `focus` (the diplomacy
 * window's country, else the player) the vassal lions, alliance fist, marriage crown, casus belli cloud and
 * the war's ring of fire (eu2.exe 0x47fd20) */
void units_draw_dipl(const World *w, const MapView *mv, unsigned char *dst, int pitch, int sw, int sh, int focus);
/* the battle window's figures (docs/SPEC_BATTLE_MERC.md 1): the side's map sprite, FIRE facing NE (attacker) / SW
 * (defender), level by the owner's tech (ours: 1 + tech / 15, at most 4 - NIEPEWNE) */
const Sprite *units_battle_sprite(const World *w, int country, int attacker, int naval);
#endif
