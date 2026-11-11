/* tutorial.h - the scripted tutorial (docs/SPEC_TUTORIAL.md; eu2.exe 0x567770 / 0x568890 / 0x569120).
 * Portable C: the script, its step machine and the state the interface asks about. Everything that
 * touches the screen or the selection goes through TutHost (main_amiga.c). */
#ifndef EU2_TUTORIAL_H
#define EU2_TUTORIAL_H
#include "pak.h"
#include "world.h"

/* the buttons the scripts name (Config/buttons.txt ids that exist; BUTTON_DISBAND is not one, the exe drops it) */
enum { TB_NONE = -1, TB_DIPLOMAT, TB_MERCHANT, TB_COLONIST, TB_MISSION, TB_VIEW_STATISTICS, TB_LAND_TECHNOLOGY,
       TB_NAVAL_TECHNOLOGY, TB_VIEW_COUNTRY, TB_SET_BUDGET, TB_MISSIONARY, TB_VP, TB_VIEW_LAND, TB_VIEW_NAVAL,
       TB_VIEW_FINANCIAL, TB_CHURCH, TB_BUILD_ARMY, TB_BUILD_NAVY, TB_BUILD_MANUFACTORY, TB_BUILD_BAILIFF,
       TB_BUILD_CITYRIGHTS, TB_BUILD_COURTHOUSE, TB_EXPAND_FORTRESS, TB_ASSAULT, TB_MAPMODE_NORMAL,
       TB_MAPMODE_POLITICAL, TB_MAPMODE_RESOURCE, TB_N };

typedef struct {
	void *u;
	int  (*sel_armies)(void *u);                 /* armies in the selection */
	int  (*sel_fleets)(void *u);                 /* fleets in the selection */
	int  (*sel_army_at)(void *u, int prov);      /* 1: an army of the selection is at prov (0 = aboard a ship) */
	int  (*sel_fleet_at)(void *u, int prov);
	int  (*infowin)(void *u);                    /* the exe's info-window id of the sidebar (4 COT, 9 province, 11 ...) */
	void (*show)(void *u, const char *name, const char *desc);   /* ShowMessage: modal, returns when closed */
	void (*province_iw)(void *u, int prov);      /* select prov, province window */
	void (*center)(void *u, int prov);
	void (*build_army)(void *u, int c, int prov, long men);
	void (*pause)(void *u, int on);
	void (*marriage)(void *u, int from, int to); /* from offers a royal marriage to to */
	void (*declare)(void *u, int a, int b);
	void (*end)(void *u);                        /* EndTutorial: back to the chapter list */
} TutHost;

typedef struct { int pid; int x, y; } TutArrow;   /* pid > 0: the province's centre, else the 800x600 point (x,y) */

typedef struct Tutorial Tutorial;

Tutorial *tut_load(Pak *text, const char *csv, World *w, int player, const TutHost *h, char *err, int errlen);
void tut_free(Tutorial *t);
void tut_tick(Tutorial *t);                     /* once per interface update: runs / tests the current step */
int  tut_btn_ok(const Tutorial *t, int tb);     /* 0: the script disabled the button */
void tut_btn_pressed(Tutorial *t, int tb);      /* a click on a button (IsButtonPress) */
void tut_scrolled(Tutorial *t);                 /* a keyboard scroll command / a frame of edge scrolling */
void tut_merchant_arrived(Tutorial *t, int cot_prov);   /* one of our merchants reached that CoT (CheckMerchant) */
int  tut_always_success(const Tutorial *t);     /* AlwaysSuccess ran: our colonists always succeed */
int  tut_cavalry(const Tutorial *t);            /* EnableCavalry ran */
int  tut_step(const Tutorial *t);               /* the step number (saved as "tutorial = N") */
void tut_restart_at(Tutorial *t, int step);     /* a loaded game: step N runs again from its start */
int  tut_arrows(const Tutorial *t, TutArrow *out, int max);
int  tut_messages(const Tutorial *t, const char **keys, int max);   /* the open AddMessage popups: key bases */
int  tut_changed(Tutorial *t);                  /* arrows / popups changed since the last call (redraw) */
#endif
