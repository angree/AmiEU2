/* mapmode.h - the map modes: which colour scale and shade each province gets (eu2.exe 0x478950,
 * docs/SPEC_MAPMODES.md). Normal (terrain, the start mode), political, economic are the three mode
 * buttons; diplomatic (ties / relations), trade, religion and colonisation come with the diplomat /
 * merchant / missionary / colonist buttons; the goods map with the resource window. */
#ifndef EU2_MAPMODE_H
#define EU2_MAPMODE_H
#include "mapview.h"

/* ours -> the exe's mode ids: NORMAL 1, POLITICAL 0, ECONOMIC 3, RELIGIOUS 7, DIPLOMATIC 5 (ties),
 * TRADE 4, COLONIAL 10, RELATIONS 6, GOODS 2 */
enum { MM_NORMAL, MM_POLITICAL, MM_ECONOMIC, MM_RELIGIOUS, MM_DIPLOMATIC, MM_TRADE, MM_COLONIAL, MM_RELATIONS, MM_GOODS, MM_N };

typedef struct {
	const World *w;
	int mode;
	int player;                 /* our country (colonisation, the default diplomatic focus) */
	int focus;                  /* diplomatic modes: whose ties / relations are shown (-1 = player) */
	int goods;                  /* goods map: the selected province's goods (-1 none) */
	const short *prov_cot;      /* trade mode: CoT index per province (economy), NULL = none known */
	int (*at_war)(const World *w, int a, int b);
	int fog;                    /* unknown provinces of the player drawn as terra incognita */
	const unsigned char *vis;   /* normal map: 1 = the player sees it now (NULL all); the rest is drawn darker */
	void *user;
	int (*risk)(void *user, int prov);        /* religion map: revolt risk (shade), NULL none */
	int (*envoy_ok)(void *user, int prov);    /* colonisation map: a colonist or a trader may go there */
} MapMode;

void        mapmode_set(MapView *v, MapMode *m, int mode);   /* m must outlive the setting */
const char *mapmode_name(int mode);
#endif
