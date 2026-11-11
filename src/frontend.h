/* frontend.h - data for the main menu (SPEC_UI §8): the scenarios in text.pak with their header
 * (name, start year, selectable countries). Portable C. */
#ifndef EU2_FRONTEND_H
#define EU2_FRONTEND_H
#include "pak.h"

#define FRONT_MAXSCEN 16
#define FRONT_MAXSEL 200
/* a featured power's header block (eu2.exe parser 0x4a4e7d): text keys used verbatim, "" = the default
 * <TAG>_DESC (history / strategy) or nothing (mission texts); ratings 1..5 (0 = not given) */
typedef struct {
	char hist[48];             /* desc / scenariotactics (one slot: the later line wins) */
	char strat[48];            /* countrytactics */
	char mtact[48], mcountry[48];   /* missiontactics, missioncountry */
	char picture[64];
	signed char diff, eco, mil, dip;
} FrontCountry;
#define FRONT_MAXFEAT 12

typedef struct {
	char path[64];             /* pak name, e.g. "scenarios/the grand campaign.eug" */
	char name[48];
	int startyear;
	char sel[FRONT_MAXSEL][4]; /* selectable tags in header order */
	int nsel, nfeat;           /* nfeat: the header's featured powers, first in sel[] */
	FrontCountry feat[FRONT_MAXFEAT];   /* the featured powers' blocks (same order as sel[]) */
} FrontScen;

/* fills out[] (sorted by start year), returns the count */
int front_scenarios(Pak *text, FrontScen *out, int max);
#endif
