/* main_amiga.c - AmiEU2 entry point on AmigaOS / AROS / MorphOS (one 68k binary).
 * Stage 2: title screen, then the 1419 political map with scrolling and 3 zoom levels.
 * Settings come from PROGDIR:eu2.cfg (libnix hands out a broken argv):
 *   gfx=aga|rtg   rtgmode=WxH   titlebar=0|1   pointer=game|system   sound=0|1   audio=paula|ahi   title_ms=N   autoexit_ms=N   bench=1 (scripted scroll/zoom timing run)
 *   scenario=<pak name of an .eug>   x= y= zoom=   start view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "amiga_gfx.h"
#include "amiga_uclock.h"
#include "amiga_trap.h"
#include "pak.h"
#include "eui.h"
#include "world.h"
#include "mapview.h"
#include "version.h"
#include "eu2_sys.h"
#include "font.h"
#include "units.h"
#include "sprite.h"
#include "calendar.h"
#include "events.h"
#include "economy.h"
#include "military.h"
#include "peace.h"
#include "diplo.h"
#include "savegame.h"
#include "frontend.h"
#include "sound.h"
#include "amiga_audio.h"
#include "text.h"
#include "ui.h"
#include "mapmode.h"
#include "msgkinds.h"
#include "envoys.h"
#include "ledger.h"
#include "monarchs.h"
#include "leaders.h"
#include "revolts.h"
#include "missions.h"
#include "buildings.h"
#include "options.h"
#include "religion.h"
#include "tutorial.h"
static void scr_blit(int x, int y, int w, int h);   /* amigagfx_blit with the software cursor on top */

#define LOGFILE "PROGDIR:eu2.log"

static void logf_(const char *fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	vsnprintf(b, sizeof b, fmt, a);
	va_end(a);
	fputs(b, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

typedef struct {
	char gfx[16], scenario[96], country[4], debugwar[4];   /* debugwar: that country declares war on us on day 2 (tests) */
	int debugend;                          /* tests: the scenario ends a month after the start */
	int debugsel;                          /* tests: select our first army in a battle / siege, pause (1) or not (2) */
	int debugmanu;                         /* tests: open the build-manufactory sub-panel on our capital once */
	int debugnaval;                        /* tests: on day 3 the debugwar country's first fleet and ours meet in a sea zone */
	int intro;                             /* the intro stills before the menu (1, default) */
	char debugpeace[4];                    /* tests: the peace window with that country on day 3, paused */
	char debugask[20];                     /* tests: an AI proposal of that type (FRA proposing) on day 2 */
	int sound;
	int rtg_w, rtg_h;                      /* rtgmode=WxH: the RTG screen size (800x600 default; 640x480 = the AGA layout) */
	int rtg_bits;                          /* rtgdepth=8|15|16|24|32 from eu2setup (0 = 8-bit first) */
	int titlebar;                          /* 1 = the Intuition screen title bar "AmiEU2 68K" above the game */
	int syspointer;                        /* pointer=system: the Intuition pointer, drawn like the game's cursor */
	int ahi;                               /* audio=ahi: ahi.device instead of Paula's audio.device */
	int anim;                              /* anim=0|1: the unit sprites animate; -1 (missing) = on for RTG only, AGA is too slow */
	int autoexit_ms, title_ms, bench, zoom, select, menu;
	int mapbuf;                            /* mapbuf=1: "Buffer province repaint" - ids + shade kept per cached map tile */
	int usz[3];                            /* unitsize=850,500,350: army / navy size per zoom (closest first), per mille */
	int probemodes;                        /* probemodes=1: time every map mode switch (amiga_uclock) into the log */
	int testmsg;                           /* testmsg=1: three message windows at the start (a test of the windows) */
	long x, y;
	int xy_set;                            /* x / y given in eu2.cfg (else the map opens on our capital) */
} Cfg;

static void read_cfg(Cfg *c)
{
	FILE *f = fopen("PROGDIR:eu2.cfg", "r");
	char line[160];
	memset(c, 0, sizeof *c);                /* a key missing from eu2.cfg is 0 unless set below (probemodes was stack garbage) */
	strcpy(c->gfx, "aga");
	strcpy(c->scenario, "scenarios/the grand campaign.eug");
	strcpy(c->country, "FRA");
	c->sound = 1;
	c->rtg_w = 800; c->rtg_h = 600; c->rtg_bits = 0; c->probemodes = 0; c->titlebar = 0; c->syspointer = 0; c->ahi = 0; c->anim = -1;
	c->autoexit_ms = 0; c->title_ms = 1500; c->bench = 0; c->select = 0; c->menu = 1; c->xy_set = 0; c->debugwar[0] = 0; c->debugend = 0; c->debugsel = 0; c->debugmanu = 0; c->debugnaval = 0; c->debugpeace[0] = 0; c->debugask[0] = 0; c->intro = 1;
	c->x = 9500; c->y = 1900; c->zoom = 2;
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *v = strchr(line, '='), *e;
		if (!v) continue;
		*v++ = 0;
		for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
		if (!strcmp(line, "gfx")) snprintf(c->gfx, sizeof c->gfx, "%s", v);
		else if (!strcmp(line, "scenario")) snprintf(c->scenario, sizeof c->scenario, "%s", v);
		else if (!strcmp(line, "autoexit_ms")) c->autoexit_ms = atoi(v);
		else if (!strcmp(line, "select")) c->select = atoi(v);                /* province selected at the start */
		else if (!strcmp(line, "debugwar")) snprintf(c->debugwar, sizeof c->debugwar, "%s", v);
		else if (!strcmp(line, "debugend")) c->debugend = atoi(v);
		else if (!strcmp(line, "debugsel")) c->debugsel = atoi(v);
		else if (!strcmp(line, "debugmanu")) c->debugmanu = atoi(v);
		else if (!strcmp(line, "debugnaval")) c->debugnaval = atoi(v);
		else if (!strcmp(line, "intro")) c->intro = atoi(v);
		else if (!strcmp(line, "debugpeace")) snprintf(c->debugpeace, sizeof c->debugpeace, "%s", v);
		else if (!strcmp(line, "debugask")) snprintf(c->debugask, sizeof c->debugask, "%s", v);
		else if (!strcmp(line, "menu")) c->menu = atoi(v);                    /* 0 = straight into the game (tests) */
		else if (!strcmp(line, "title_ms")) c->title_ms = atoi(v);
		else if (!strcmp(line, "bench")) c->bench = atoi(v);
		else if (!strcmp(line, "probemodes")) c->probemodes = atoi(v);
		else if (!strcmp(line, "testmsg")) c->testmsg = atoi(v);
		else if (!strcmp(line, "mapbuf")) c->mapbuf = atoi(v);
		else if (!strcmp(line, "unitsize")) { const char *q = v; int k; for (k = 0; k < 3 && *q; k++) { c->usz[k] = atoi(q); while (*q && *q != ',') q++; if (*q) q++; } }
		else if (!strcmp(line, "x")) { c->x = atol(v); c->xy_set = 1; }
		else if (!strcmp(line, "y")) { c->y = atol(v); c->xy_set = 1; }
		else if (!strcmp(line, "zoom")) c->zoom = atoi(v);
		else if (!strcmp(line, "country")) snprintf(c->country, sizeof c->country, "%s", v);
		else if (!strcmp(line, "sound")) c->sound = atoi(v);
		else if (!strcmp(line, "titlebar")) c->titlebar = atoi(v);
		else if (!strcmp(line, "pointer")) c->syspointer = !strcmp(v, "system");
		else if (!strcmp(line, "audio")) c->ahi = !strcmp(v, "ahi");
		else if (!strcmp(line, "anim")) c->anim = atoi(v) != 0;
		else if (!strcmp(line, "rtgmode")) {                                  /* e.g. 1024x768, from the settings tool */
			int rw = atoi(v); const char *xx = strchr(v, 'x');
			int rh = xx ? atoi(xx + 1) : 0;
			if (((rw >= 800 && rh >= 600) || (rw == 640 && rh == 480)) && rw <= 4096 && rh <= 4096) { c->rtg_w = rw; c->rtg_h = rh; }
		}
		else if (!strcmp(line, "rtgdepth")) c->rtg_bits = atoi(v);
	}
	fclose(f);
}

/* A start-up error: into the log and, since a player who started us from an icon sees no log, a requester */
static void fatal_(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	logf_("%s", b);
	amigagfx_message(b);
}

static int check_conversion(void)
{
	FILE *f = fopen("PROGDIR:data/conversion.txt", "r");
	char line[128];
	int ver = 0, errors = -1;
	if (!f) { fatal_("The game data is missing (PROGDIR:data/conversion.txt).\nRun eu2conv on your own copy of Europa Universalis II\nand put its output folder next to eu2 as \"data\"."); return -1; }
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "EU2 Amiga data conversion ", 26)) ver = atoi(line + 26);
		if (!strncmp(line, "errors ", 7)) errors = atoi(line + 7);
	}
	fclose(f);
	logf_("data: conversion %d, errors %d", ver, errors);
	if (ver < 2 || errors != 0) { fatal_("The game data is old or incomplete\n(conversion %d, errors %d) - run eu2conv again.", ver, errors); return -1; }
	return 0;
}

/* title screen: the loading picture mapped onto the map palette (no extra palette needed) */
static void show_title(Pak *gfx, const unsigned char *pal)
{
	unsigned long sz, t0 = amigagfx_millis();
	unsigned char *buf = pak_load(gfx, "gfx/load_800.bmp", &sz), *inv, *pens;
	EuiImage im;
	int x, y, gw = amigagfx_game_width(), gh = amigagfx_game_height(), pitch = amigagfx_pitch();
	unsigned char *dst = amigagfx_chunky();
	if (!buf || eui_decode(buf, sz, &im)) { free(buf); logf_("title: load_800 missing"); return; }
	inv = malloc(EUI_INV_SIZE);
	pens = malloc((size_t)im.w * im.h);
	eui_build_inverse(pal, 256, inv);
	eui_to_pens(&im, inv, pens);
	for (y = 0; y < gh; y++) {
		const unsigned char *s = pens + (long)(y * im.h / gh) * im.w;
		unsigned char *d = dst + (long)y * pitch;
		for (x = 0; x < gw; x++) d[x] = s[(long)x * im.w / gw];
	}
	scr_blit(0, 0, gw, gh);
	logf_("title: %lu ms", amigagfx_millis() - t0);
	eui_free(&im); free(buf); free(inv); free(pens);
}

static World g_world;
static MapView *g_mv;
static Military *g_mil;
static int g_player = -1;
/* SPEC_SOUND 4: heard only within 1536 map pixels of the centre of the view (volume 0xC0 - d / 8 > 0; the
 * volume itself is not used, every sound plays at full SFX volume, centred) */
static int sfx_audible(int prov)
{
	long cx, cy, dx, dy;
	int z;
	if (prov <= 0 || prov >= g_world.nprov || !g_mv) return prov < 0;
	mv_get(g_mv, &cx, &cy, &z);
	dx = g_world.prov[prov].army_x - cx; dy = g_world.prov[prov].army_y - cy;
	if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
	return dx < 1536 && dy < 1536 && dx * dx + dy * dy < 1536L * 1536L;
}
static unsigned long g_srand = 12345;                 /* the sound's own rand(): never the simulation's */
static int sfx_rand(int n) { g_srand = g_srand * 1103515245UL + 12345UL; return (int)((g_srand >> 16) % (unsigned long)n); }
/* the pattern tables (0x624a78 swords, 0x624be8 muskets, 0x624d00 sea): 9 plays then goto 0; a play picks one
 * of three ids at random and waits that many records-ticks */
static const unsigned char pat_sword_w[9] = { 4, 4, 4, 8, 4, 8, 6, 4, 14 };
static const unsigned char pat_fire_w[9] = { 8, 12, 8, 12, 12, 8, 12, 12, 16 };
typedef struct { short prov; unsigned char info, rec, cnt, divc, div, live; } SndPat;
static SndPat g_pat[8];
static int g_npat;
static unsigned long g_pat_ms;
static void pat_service(void)
{
	static const char *const sword[3] = { "land_sword1", "land_sword2", "land_sword3" };
	static const char *const musket[3] = { "land_musket1", "land_musket2", "land_musket3" };
	static const char *const sea[3] = { "sea_cannon1", "sea_cannon2", "sea_cannon3" };
	short prov[8];
	unsigned char info[8];
	unsigned long now = amigagfx_millis();
	int n = 0, i, k, ticks;
	if (g_mil && g_player >= 0) n = mil_sound_battles(g_mil, g_player, prov, info, 8);
	for (k = 0; k < g_npat; k++) g_pat[k].live = 0;
	for (i = 0; i < n; i++) {                         /* phase start 0x46f5b0: the other player starts from record 0 */
		for (k = 0; k < g_npat && g_pat[k].prov != prov[i]; k++) ;
		if (k == g_npat) { if (g_npat >= 8) continue; g_npat++; g_pat[k].prov = prov[i]; g_pat[k].info = (unsigned char)(info[i] ^ 0xFF); }
		if (g_pat[k].info != info[i]) {
			int shock = (info[i] & MSB_SHOCK) != 0;
			g_pat[k].info = info[i]; g_pat[k].rec = 0; g_pat[k].cnt = 1;
			/* dividers: field battle muskets 4 / swords 2 (0x46ec50); the siege-combat base (an assault) 4 / 4 (0x46ee00);
			   naval fire 4 / shock 2 */
			g_pat[k].div = (unsigned char)(shock && !(info[i] & MSB_ASSAULT) ? 2 : 4);
			g_pat[k].divc = g_pat[k].div;
		}
		g_pat[k].live = 1;
	}
	for (k = 0; k < g_npat; ) { if (!g_pat[k].live) g_pat[k] = g_pat[--g_npat]; else k++; }   /* combat end: stopped */
	if (!g_pat_ms || now - g_pat_ms > 1000) g_pat_ms = now;
	ticks = (int)((now - g_pat_ms) / 15);              /* the game timer: a 15 ms one-shot, re-armed (0x416530, period 0xF) */
	g_pat_ms += (unsigned long)ticks * 15;
	while (ticks-- > 0)
		for (k = 0; k < g_npat; k++) {
			SndPat *pp = &g_pat[k];
			if (--pp->divc) continue;
			pp->divc = pp->div;
			if (--pp->cnt) continue;
			{	int naval = pp->info & MSB_NAVAL, shock = (pp->info & MSB_SHOCK) != 0;
				const unsigned char *wt = !naval && shock ? pat_sword_w : pat_fire_w;
				const char *const *ids = naval ? sea : shock ? sword : musket;
				if (pp->rec >= 9) pp->rec = 0;              /* record 9: goto 0 */
				if (sfx_audible(pp->prov)) sound_play(ids[sfx_rand(3)]);
				pp->cnt = wt[pp->rec++];
			}
		}
}
static void sfx_service(void)                        /* the simulation's queued effects (world_sfx), the battle patterns, then music */
{
	int i;
	for (i = 0; i < g_world.nsfx; i++) {
		static const char *const cannon[3] = { "land_cannon1", "land_cannon2", "land_cannon3" };
		const char *key = g_world.sfx[i];
		if (g_world.sfxprov[i] >= 0 && !sfx_audible(g_world.sfxprov[i])) continue;
		if (!strcmp(key, "siege_shot")) key = cannon[sfx_rand(3)];   /* the siege table 0x624b90: one shot, then silence */
		sound_play(key);
	}
	g_world.nsfx = 0;
	pat_service();
	sound_service();
}
static unsigned char g_inv[EUI_INV_SIZE];   /* RGB444 -> pen of the map palette */
static Font g_font;
static int g_have_font, g_selected = -1, g_paused = 1;

static Events *g_ev;
static Economy *g_ec;
static long trade_value_cb(void *u, int p) { return econ_trade_value_m((const Economy *)u, p); }
static long support_cb(void *u, int c) { return econ_support_limit((const Economy *)u, c); }   /* the pirates' zone score */
static int risk_cb(void *u, int p);                     /* rev_risk for the tax modifier */
static Peace *g_peace;
static Diplo *g_dip;
static int g_army = -1;                  /* selected land unit of the player */
/* the whole selection (docs/SPEC_UNIT_UI.md 2): armies or fleets, never both; g_army / g_fleet is the
 * first one (the sidebar shows it) */
static short g_selA[64], g_selF[64];
static int g_nselA, g_nselF, g_shift;
/* an accepted army move order (0x531276): infantry >= cavalry over the selection -> if_move_army, else if_move_cav */
static const char *move_sound(void)
{
	long inf = 0, cav = 0;
	int k;
	for (k = 0; k < g_nselA; k++) if (g_selA[k] >= 0 && g_selA[k] < g_world.nland) { inf += g_world.land[g_selA[k]].infantry; cav += g_world.land[g_selA[k]].cavalry; }
	if (!g_nselA && g_army >= 0 && g_army < g_world.nland) { inf = g_world.land[g_army].infantry; cav = g_world.land[g_army].cavalry; }
	return inf >= cav ? "if_move_army" : "if_move_cav";
}
static int g_fleet = -1;                 /* selected fleet of the player */
static int g_infowin = UI_IW_PROVINCE, g_sl_drag = -1;   /* sidebar info window, budget slider being dragged */
/* the peace window (SPEC_UI §4.12) */
#define PW_MAX 256
static int g_pw_enemy = -1, g_pw_page, g_pw_demand, g_pw_scroll, g_pw_ncand, g_pw_nside;
static int g_pw_target = -1;                 /* the country the window was opened for (an enemy member) */
static long g_pw_gold;                       /* ducats */
static int g_pw_flag[3];                     /* vassal, religion, access */
static short g_pw_cand[PW_MAX], g_pw_recv[PW_MAX], g_pw_side[32];
static unsigned char g_pw_pick[PW_MAX];
static char g_pw_msg[400];

static void msg_typed(const char *type, int goto_prov, const char *a1, const char *a2, const char *a3);
static int g_stab_seen;                                   /* the player's stability the messages last reported */
static int g_pu_prov = -1, g_pu_naval;             /* the purchase window: province, navy */
static int g_rel_sel = -1;                          /* the religion window: the radio choice */
static int g_vas_sel = -1;                          /* the vassal window: the chosen row */
static int g_lo_target = -1, g_lo_rate = 10, g_lo_months = 36; static long g_lo_amount = 100;   /* the loans window (defaults 0x5103ba; 20 % clamped to 10 at once) */
static long g_pu_n[3];                              /* ordered steps per column */
static int pw_sep(int war)                       /* the counterpart if the peace is separate, else -1 */
{
	const War *wr;
	int s;
	if (war < 0) return -1;
	wr = &g_world.war[war]; s = peace_side(&g_world, war, g_player);
	if ((s == 1 ? wr->att[0] : wr->def[0]) != g_player || (s == 1 ? wr->def[0] : wr->att[0]) != g_pw_enemy) return g_pw_enemy;
	return -1;
}
static int pw_war(void) { return g_pw_enemy >= 0 ? peace_find_war(&g_world, g_player, g_pw_enemy) : -1; }

static void pw_offer(PeaceOffer *o)
{
	int k;
	memset(o, 0, sizeof *o);
	o->mode = g_pw_demand ? PO_DEMAND : PO_OFFER;
	for (k = 0; k < g_pw_ncand && o->nprov < PO_MAXPROV; k++) if (g_pw_pick[k]) o->prov[o->nprov++] = g_pw_cand[k];
	o->gold_m = g_pw_gold * 1000;
	if (g_pw_demand) { o->vassal = g_pw_flag[0]; o->religion = g_pw_flag[1]; o->access = g_pw_flag[2]; }
	{ int sp2 = pw_sep(pw_war()); o->sep_with = sp2 >= 0 ? sp2 + 1 : 0; if (o->sep_with) o->vassal = o->religion = 0; }
}

/* the provinces on offer and who would get each one (as peace_make gives them) */
static MapMode g_mm = { NULL, MM_NORMAL, -1, -1, -1, NULL, NULL };   /* the map mode (mapmode.h): the normal map is "the standard view" (manual p.11) */
static Monarchs *g_mon;                   /* historical monarchs (DB/monarchs) */
static Leaders *g_lead;                   /* historical generals (DB/leaders) */
static Missions *g_mis;                   /* the player's missions (DB/Missions.csv) */
static char g_mis_t[22][96];
static LedgerHist g_hist;                 /* the Ledger graphs' yearly samples */
static int g_hist_loaded;                /* a loaded game brought its own history */
static Tutorial *g_tut;                    /* the scripted tutorial of a tutorial scenario (NULL none) */
static int g_tut_end, g_tut_restore, g_tut_redraw, g_fe_screen;   /* EndTutorial ran; the step of a loaded save; the frontend's first screen */
static void hist_save(FILE *f)
{
	ledger_hist_save(&g_hist, &g_world, f);
	if (g_mis) mis_save(g_mis, &g_world, f);
	if (g_tut) fprintf(f, "amieu2_tutorial = { step = %d }\n", tut_step(g_tut));   /* the exe: "tutorial = N" */
}
static void hist_restore(const SNode *root)
{
	const SNode *t = script_get(root, "amieu2_tutorial");
	if (script_get(root, "amieu2_ledger")) { ledger_hist_restore(&g_hist, &g_world, root); g_hist_loaded = 1; }
	if (g_mis) mis_restore(g_mis, &g_world, root);
	g_tut_restore = t ? (int)script_int(t, "step", 0) : 0;
}
static int g_game_over;                  /* the end date was reached and the score shown */
static int g_speed = 2, g_ctrl;           /* game speed 1..5 = GAME SPEED Below Normal .. Very Fast (Ctrl +/- on the keypad), Ctrl held */
static const int day_ms[5] = { 200, 100, 60, 35, 20 };   /* ms per day (ours, NIEPEWNE) */
#define OPTSFILE "PROGDIR:eu2opts.txt"
#define AUTOSAVE "PROGDIR:save/autosave.eug"
static GameOpts g_opts;                    /* the frontend's VICTORY / OPTIONS (options.h) */
static int g_bench_fog;                    /* the bench keeps the fog on whatever the options say */
static short g_start_prov[MAX_COUNTRY];    /* provinces at the start: DEATHMATCH counts the new ones (not saved: a loaded game starts counting anew, NIEPEWNE) */
static int g_unload;                     /* 1: the next map click is where the selected fleet's troops land */
static int g_env_prov = -1, g_env_cot = -1;  /* the envoy windows' province / centre of trade */
/* diplomacy windows (SPEC_UI 4.8): view 0 alliances / 1 relations, target of the action panel, hovered row,
 * pending confirmed action, the relations list sort */
static int g_dip_view, g_dip_target = -1, g_dip_hover = -1, g_dip_confirm = -1, g_dip_sortnat, g_dip_sortasc;
static UiDipAct g_dip_act[32]; static int g_ndip_act;
static char g_dip_msg[600];
static char g_dip_line[8][200]; static int g_ndip_line;
static void dip_lines(int c);
static unsigned long g_mil_epoch;        /* bumped when armies move / fight: redraw the map */
static long g_dayno;
static unsigned long g_events_fired;
static unsigned long g_env_ms, g_month_ms, g_prof_m[5];   /* bench: economy, military month, peace, diplomacy, revolts */
static const char *mname[12] = { "January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December" };

static void mil_logged(void *u, long today, const char *text)
{
	(void)u; (void)today;
	logf_("war: %s", text);
}

static void event_logged(void *u, long today, int c, long id, int act, const char *name)
{
	(void)u; (void)today;
	g_events_fired++;
	if (c == g_player || g_events_fired <= 20)
		logf_("event %ld \"%s\" -> %s, action %c", id, text_get(name), g_world.country[c].tag, (char)(65 + act));
}

/* the monarchs on the thrones today: name and skills into every country (monthly and after a load) */
static int g_smsg_primed;                       /* (tentative; set by check_state_messages) */
static void mon_update(void)
{
	int c;
	for (c = 0; c < g_world.ncountry; c++) {
		const Monarch *mo = mon_current_w(g_mon, &g_world, c, g_world.now);
		if (g_smsg_primed && c != g_player && mo && g_world.country[c].m_name && g_world.country[c].m_name != mo->name && g_world.country[c].nprov_owned > 0)
			world_news(&g_world, NOTE_OTHER, c, OM_NEWMON, -1);   /* NEWMONOTHER: not at the start date (0x44fda0) */
		if (g_smsg_primed && c == g_world.emperor && mo && g_world.country[c].m_name && g_world.country[c].m_name != mo->name)
			world_hre_elect(&g_world, g_dayno);               /* the emperor's monarch changed: an election (0x44fda0 -> 0x5b20a0) */
		g_world.country[c].m_name = mo ? mo->name : NULL;
		g_world.country[c].m_adm = mo ? mo->adm : 3; g_world.country[c].m_dip = mo ? mo->dip : 3; g_world.country[c].m_mil = mo ? mo->mil : 3;
		if (mo) {                                         /* event ADM / DIP / MIL changes while they last (none without a ruler) */
			Country *co = &g_world.country[c];
			unsigned char *st[3];
			int s;
			st[0] = &co->m_adm; st[1] = &co->m_dip; st[2] = &co->m_mil;
			for (s = 0; s < 3; s++) if (co->ev_stat[s] && g_dayno < co->ev_stat_until[s]) { int v = *st[s] + co->ev_stat[s]; *st[s] = (unsigned char)(v < 1 ? 1 : v > 9 ? 9 : v); }
		}
	}
}

/* event commands that need the other modules (events.h EVC_*) */
static int g_dlg_redraw;                        /* (tentative; see dialog) */
static int ev_cmd(void *u, int code, int c, long which, long value)
{
	(void)u; (void)value;
	switch (code) {
	case EVC_NEIGHBOUR: return g_dip ? diplo_neighbours(g_dip, c, (int)which) : 0;
	case EVC_REVOLT: case EVC_RELREVOLT: case EVC_COLREVOLT: return rev_spawn(&g_world, g_mil, (int)which);
	case EVC_INHERIT: if (g_dip) { diplo_annex_now(g_dip, c, (int)which); return 1; } return 0;
	case EVC_WAR: return g_dip ? diplo_declare_war(g_dip, c, (int)which, g_dayno) >= 0 : 0;
	case EVC_INDEPENDENCE: return rev_release(&g_world, (int)which, c);
	case EVC_MONARCH: case EVC_SLEEPMONARCH:
		if (g_world.nmonev < 64) { g_world.monev_c[g_world.nmonev] = (short)c; g_world.monev_id[g_world.nmonev] = which; g_world.monev_day[g_world.nmonev] = g_dayno; g_world.monev_kind[g_world.nmonev] = (unsigned char)(code == EVC_MONARCH); g_world.nmonev++; return 1; }
		return 0;
	case EVC_LEADER: case EVC_SLEEPLEADER:
		if (g_world.nleadev < 64) { g_world.leadev_id[g_world.nleadev] = which; g_world.leadev_day[g_world.nleadev] = g_dayno; g_world.leadev_kind[g_world.nleadev] = (unsigned char)(code == EVC_LEADER); g_world.nleadev++; return 1; }
		return 0;
	case EVC_IS_MONARCH: { const Monarch *mo = mon_current_w(g_mon, &g_world, c, g_world.now); return mo && mo->id == which; }
	case EVC_IS_LEADER: return lead_active(g_lead, &g_world, which);
	case EVC_COUNTRY:                                    /* our tag becomes `which` (CAS -> SPA): the slots swap identities */
		world_swap_identity(&g_world, c, (int)which);
		if (g_ev) events_swap_countries(g_ev, c, (int)which);
		mon_swap(g_mon, c, (int)which); lead_swap(g_lead, c, (int)which); rev_swap(c, (int)which);
		logf_("country: %s is now %s", g_world.country[which].tag, g_world.country[c].tag);
		g_dlg_redraw = 1;                                 /* the map colours: repainted with the next frame */
		return 1;
	default: return 0;
	}
}

/* one game day: 30-day months, 360-day years (docs/SPEC_TIME_ECONOMY.md §2); the economy runs on the
 * month and year change, events every day */
static void next_day(void)
{
	int y, m, d, oldm = g_world.now.month, oldy = g_world.now.year;
	g_dayno++;
	cal_date(g_dayno, &y, &m, &d);
	if (d == 1 && m == 1) AmigaMemProbe("new year");     /* lightmaps load on first use per zoom: the peak grows with play */
	g_world.now.year = (short)y; g_world.now.month = (unsigned char)m; g_world.now.day = (unsigned char)d;
	if (m != oldm && g_ec) {
		unsigned long tm0 = amigagfx_millis(), ta;
		mon_update();
		lead_spawn(g_lead, &g_world, g_mil, 0);            /* conquistadors / explorers whose day came */
		lead_assign(g_lead, &g_world);
		world_weather_month(&g_world);                     /* the province monthly starts with the weather (0x480377) */
		ta = amigagfx_millis(); econ_month(g_ec); g_prof_m[0] += amigagfx_millis() - ta;
		ta = amigagfx_millis(); if (g_mil) mil_month(g_mil); g_prof_m[1] += amigagfx_millis() - ta;
		ta = amigagfx_millis(); if (g_peace) peace_month(g_peace, g_dayno); g_prof_m[2] += amigagfx_millis() - ta;
		ta = amigagfx_millis(); if (g_dip) diplo_month(g_dip, g_dayno); g_prof_m[3] += amigagfx_millis() - ta;
		{ unsigned long t = amigagfx_millis(); env_ai_month(&g_world, g_ec, g_mil, g_dayno); g_env_ms += amigagfx_millis() - t; }
		ta = amigagfx_millis(); if (g_mil) rev_month(&g_world, g_mil); rel_ai_month(&g_world, g_dayno); world_hre_month(&g_world, g_dayno); g_prof_m[4] += amigagfx_millis() - ta;
		if (g_mis) mis_month(g_mis, &g_world, g_player, g_dayno);
		bld_ai_month(&g_world, g_mil, g_dayno);
		if (y != oldy) { econ_year(g_ec); ledger_hist_sample(&g_hist, &g_world, g_ec); }
		g_month_ms += amigagfx_millis() - tm0;
	}
	if (g_ev) events_day(g_ev, g_dayno);
	if (g_mil) mil_day(g_mil, g_dayno);
	rev_day(&g_world, g_dayno);                              /* the control dates of the revolters (SPEC_REVOLTERS 4.1) */
	env_day(&g_world, g_dayno);                              /* envoys arriving */
	{ unsigned long ta = amigagfx_millis(); if (g_dip) diplo_day(g_dip, g_dayno); g_prof_m[3] += amigagfx_millis() - ta; }
}

/* domestic policy (docs/SPEC_DOMESTIC.md): a step locks both buttons for 10 years (0x429410 stamps today + 1 year, 0x42a0d0
 * opens at stamp + 9 years); the economy reads the sliders every month. The note is the DOMW_DELAY tooltip text while the gate
 * is closed ("<Month> <day>, <year>"), empty while it is open. */
#define POLICY_DAYS (10L * 360)
static void domestic_note(char *s, int n)
{
	long d = g_player >= 0 ? g_world.country[g_player].policy_day : 0;
	s[0] = 0;
	if (d && g_dayno - d < POLICY_DAYS) {
		int y, m, dd;
		char date[40];
		cal_date(d + POLICY_DAYS, &y, &m, &dd);
		snprintf(date, sizeof date, "%s %d, %d", mname[m >= 1 && m <= 12 ? m - 1 : 0], dd * cal_month_days(m >= 1 && m <= 12 ? m : 1) / 30 + 1, y);
		snprintf(s, (size_t)n, text_get("DOMW_DELAY"), date);
	}
}

static void domestic_step(int k, int dir)
{
	Country *c = &g_world.country[g_player];
	int v = c->policy[k] + dir;
	if (k < 0 || k > 7 || v < 0 || v > 10) return;
	if (c->policy_day && g_dayno - c->policy_day < POLICY_DAYS) { logf_("domestic: change not allowed yet"); return; }
	c->policy[k] = (signed char)v; c->policy_day = g_dayno;
	if (c->stability > -3) {                                  /* 0x82: stability -1 (0x436490(1)) */
		c->stability--;
		msg_typed("STABDROP", -1, NULL, NULL, NULL);          /* at once, not at the month's check (0x5783d0(0x0F) in 0x448270) */
		g_stab_seen = c->stability;
	}
	else if (g_mil) rev_roll_country(&g_world, g_mil, g_player, 0);   /* at -3 already: a revolt roll at once */
	logf_("domestic: slider %d -> %d, stability %d", k, v, c->stability);
}

#define BAR_H 32         /* top area redrawn when only the date/values change (bar at y 4..28 at 800x600) */
/* the game's printf-style texts without floats or the locale: %d and %c take the next value as is,
 * %.Nf takes it as a fixed-point number x1000 */
static void fmt_game(char *out, int n, const char *f, const long *v, int nv)
{
	int o = 0, k = 0, i;
	while (*f && o < n - 1) {
		if (f[0] == '%' && f[1] == '%') { out[o++] = '%'; f += 2; continue; }
		if (f[0] == '%' && (f[1] == 'd' || f[1] == 'c' || (f[1] == '.' && f[2] >= '0' && f[2] <= '9' && f[3] == 'f'))) {
			long x = k < nv ? v[k] : 0;
			char t[32];
			k++;
			if (f[1] == 'd') { snprintf(t, sizeof t, "%ld", x); f += 2; }
			else if (f[1] == 'c') { t[0] = (char)x; t[1] = 0; f += 2; }
			else {
				int dec = f[2] - '0';
				long a = x < 0 ? -x : x, div = 1;
				for (i = dec; i < 3; i++) div *= 10;
				a = (a + div / 2) / div;
				if (dec == 0) snprintf(t, sizeof t, "%s%ld", x < 0 ? "-" : "", a);
				else { long p = 1; for (i = 0; i < dec; i++) p *= 10; snprintf(t, sizeof t, "%s%ld.%0*ld", x < 0 ? "-" : "", a / p, dec, a % p); }
				f += 4;
			}
			for (i = 0; t[i] && o < n - 1; i++) out[o++] = t[i];
			continue;
		}
		out[o++] = *f++;
	}
	out[o] = 0;
}

/* the top bar's rollover texts (BAR_*_I instant, BAR_*_D delayed) */
static void bar_tip(int item, char *t1, char *t2, int n)
{
	const Country *c = &g_world.country[g_player];
	EconReport r;
	long v[3];
	memset(&r, 0, sizeof r);
	if (g_ec) econ_report(g_ec, g_player, &r);
	t1[0] = t2[0] = 0;
	switch (item) {
	case 0: v[0] = c->merchants_m / 1000; v[1] = r.rate_mer; fmt_game(t1, n, text_get("BAR_MERCHANT_I"), v, 2); break;
	case 1: v[0] = c->colonists_m / 1000; v[1] = r.rate_col; fmt_game(t1, n, text_get("BAR_COLONIST_I"), v, 2); break;
	case 2: v[0] = c->diplomats_m / 1000; v[1] = r.rate_dip; fmt_game(t1, n, text_get("BAR_DIPLOMAT_I"), v, 2); break;
	case 3: v[0] = c->missionaries_m / 1000; v[1] = r.rate_miss; fmt_game(t1, n, text_get("BAR_MISSIONARY_I"), v, 2); break;
	case 4: v[0] = c->stability < 0 ? '-' : '+'; v[1] = c->stability < 0 ? -c->stability : c->stability;
	        fmt_game(t1, n, text_get("BAR_FLAG_I"), v, 2); snprintf(t2, (size_t)n, "%s", text_get("BAR_FLAG_D")); break;
	case 5: v[0] = c->manpower_m / 1000; v[1] = 2 * r.manpower_rate_m / 1000;   /* the cap is twice the yearly gain (economy step 20) */
	        fmt_game(t1, n, text_get("BAR_SOLDIER_I"), v, 2); v[0] = r.manpower_rate_m / 1000; fmt_game(t2, n, text_get("BAR_SOLDIER_D"), v, 1); break;
	case 6: {
		EconBudget b;
		memset(&b, 0, sizeof b);
		if (g_ec) econ_budget(g_ec, g_player, &b);
		v[0] = c->treasury_m / 1000; v[1] = b.income_m / 1000 * b.slider[ECON_SL_TREAS] + b.income_m % 1000 * b.slider[ECON_SL_TREAS] / 1000;
		fmt_game(t1, n, text_get("BAR_TREASURY_I"), v, 2); snprintf(t2, (size_t)n, "%s", text_get("BAR_TREASURY_D")); break; }
	default: break;
	}
}

/* recolour the map: owners changed or the map mode / its focus did (the economy and the player may
 * have been replaced by a load, so the mode's inputs are taken fresh every time) */
static int mm_risk(void *u, int p) { (void)u; return g_mil ? rev_risk(&g_world, g_mil, p) : 0; }
static int mm_envoy(void *u, int p)
{
	(void)u;
	if (g_player < 0) return 0;
	return (g_mil && env_colonist_can(&g_world, g_mil, g_player, p) == ENV_OK) || env_trader_can(&g_world, g_player, p) == ENV_OK;
}

/* what the player sees on the normal map (0x531460, NIEPEWNE in detail): own / controlled provinces
 * and their land neighbours, the provinces of countries tied to us (marriage, alliance, vassal, war
 * ally), the places of our units and their neighbours */
static unsigned char g_vis[MAX_PROV];
static void compute_vis(void)
{
	static unsigned char tie[MAX_COUNTRY];
	short nb[64];
	int i, k, n;
	memset(g_vis, 0, sizeof g_vis); memset(tie, 0, sizeof tie);
	if (g_player < 0) { memset(g_vis, 1, sizeof g_vis); return; }
	for (i = 0; i < g_world.nally; i++) {
		int has = 0;
		for (k = 0; k < g_world.ally[i].n; k++) if (g_world.ally[i].member[k] == g_player) has = 1;
		if (has) for (k = 0; k < g_world.ally[i].n; k++) tie[g_world.ally[i].member[k]] = 1;
	}
	for (i = 0; i < g_world.nwar; i++) {
		const War *x = &g_world.war[i];
		int sa = 0, sd = 0;
		for (k = 0; k < x->natt; k++) if (x->att[k] == g_player) sa = 1;
		for (k = 0; k < x->ndef; k++) if (x->def[k] == g_player) sd = 1;
		if (sa) for (k = 0; k < x->natt; k++) tie[x->att[k]] = 1;
		if (sd) for (k = 0; k < x->ndef; k++) tie[x->def[k]] = 1;
	}
	for (i = 1; i < g_world.nprov && i < MAX_PROV; i++) {
		const Province *p = &g_world.prov[i];
		int mine = p->owner == g_player || p->controller == g_player;
		if (mine || (p->owner >= 0 && tie[p->owner]) || (p->controller >= 0 && tie[p->controller])) g_vis[i] = 1;
		if (mine && g_mil) for (n = mil_adjacent(g_mil, i, nb, 64), k = 0; k < n; k++)     /* land neighbours; sea ones too from naval tech 18 (0x432e30) */
			if (nb[k] > 0 && nb[k] < MAX_PROV && (g_world.prov[nb[k]].terrain < 5 || g_world.country[g_player].naval_tech >= 18)) g_vis[nb[k]] = 1;
	}
	for (i = 0; i < g_world.nland; i++) {
		const LandUnit *l = &g_world.land[i];
		if (l->dead || l->owner < 0 || (l->owner != g_player && !tie[l->owner]) || l->location <= 0 || l->location >= MAX_PROV) continue;
		g_vis[l->location] = 1;
		if (g_mil) for (n = mil_adjacent(g_mil, l->location, nb, 64), k = 0; k < n; k++) if (nb[k] > 0 && nb[k] < MAX_PROV) g_vis[nb[k]] = 1;
	}
}

static void refresh_map(MapView *mv)
{
	g_mm.w = &g_world; g_mm.player = g_player; g_mm.at_war = mil_at_war; g_mm.fog = g_bench_fog ? 1 : g_opts.opt[OPT_FOG];
	g_mm.prov_cot = g_ec ? econ_prov_cots(g_ec) : NULL;
	g_mm.risk = mm_risk; g_mm.envoy_ok = mm_envoy; g_mm.user = NULL;
	if (g_mm.mode == MM_NORMAL && g_opts.opt[OPT_FOG] && !g_bench_fog) { compute_vis(); g_mm.vis = g_vis; } else g_mm.vis = NULL;
	mapmode_set(mv, &g_mm, g_mm.mode);
	units_set_fog(g_mm.vis);
}

/* units moved: the fog follows them; the map is recoloured only when what we see changed */
static void fog_update(MapView *mv)
{
	static unsigned char old[MAX_PROV];
	if (!g_mm.vis) return;
	memcpy(old, g_vis, sizeof old);
	compute_vis();
	if (memcmp(old, g_vis, sizeof old)) refresh_map(mv);
}

static void set_mapmode(MapView *mv, int mode);
/* the economic map button (0x52e0d6): mode 3; with no province selected one is (0x476380: the capital, NIEPEWNE);
 * then window 0xA, the province information (ui.c draws hprov for any province on this map) */
static void econ_mode(MapView *mv)
{
	set_mapmode(mv, MM_ECONOMIC);
	if (g_selected <= 0 && g_player >= 0) g_selected = g_world.country[g_player].capital;
	if (g_selected > 0) { g_infowin = UI_IW_PROVINCE; g_army = g_fleet = -1; }
}
static void set_mapmode(MapView *mv, int mode)
{
	g_mm.mode = mode; g_mm.focus = -1;
	refresh_map(mv);
	logf_("map mode: %s", mapmode_name(mode));
}

/* the interface over the map: sidebar + top bar (src/ui.c, docs/SPEC_UI.md §2) */
static void tut_overlay(void);
static void draw_bar(void)
{
	char date[64];
	UiState st;
	const Date *d = &g_world.now;
	int mi = (d->month >= 1 && d->month <= 12) ? d->month - 1 : 0;
	/* display day = internal day x real month length / 30, +1 (SPEC_TIME_ECONOMY §2) */
	snprintf(date, sizeof date, "%d %s %d", d->day * cal_month_days(mi + 1) / 30 + 1, mname[mi], d->year);
	memset(&st, 0, sizeof st);
	st.date = date; st.paused = g_paused; st.player = g_player; st.selected = g_selected; st.mapmode = g_mm.mode;
	if (g_player >= 0) {
		const Country *c = &g_world.country[g_player];
		st.merchants = c->merchants_m / 1000; st.colonists = c->colonists_m / 1000; st.diplomats = c->diplomats_m / 1000;
		st.missionaries = c->missionaries_m / 1000; st.stability = c->stability; st.manpower = c->manpower_m / 1000;
		st.treasury = c->treasury_m / 1000;
		if (g_infowin == UI_IW_BUDGET && g_ec) {
			static EconBudget b;
			econ_budget(g_ec, g_player, &b); st.infowin = UI_IW_BUDGET; st.budget = &b;
			st.wartax_state = econ_war_tax_state(&g_world, g_player); st.loan_ok = econ_can_take_loan(g_ec, g_player); st.loan_size = econ_loan_size_of(&g_world, g_player) / 1000;
			{ int k; for (k = 0; k < 5; k++) st.boost_ok[k] = (unsigned char)econ_boost_ok(g_ec, g_player, k); }
		}
		if ((g_infowin == UI_IW_FINANCE || g_infowin == UI_IW_LANDTECH || g_infowin == UI_IW_NAVALTECH) && g_ec) {
			static EconReport rep; static EconBudget bud;
			int nv = g_infowin == UI_IW_NAVALTECH, k;
			econ_report(g_ec, g_player, &rep); econ_budget(g_ec, g_player, &bud);
			st.infowin = g_infowin; st.report = &rep; st.budget = &bud; st.inflation_u = c->inflation_u;
			{ long am[5], du[5]; short rt[5]; int n = econ_loans(g_ec, g_player, am, du, 5), k2; econ_loan_rates(g_ec, g_player, rt, 5); for (k2 = 0; k2 < n; k2++) st.interest_m += am[k2] / 1200 * rt[k2]; }
			st.tech_level = nv ? c->naval_tech : c->land_tech;
			st.support = nv || !g_ec ? -1 : econ_support_limit(g_ec, g_player);   /* the naval limit's constants: NIEPEWNE, not shown */
			st.maint_month = nv ? rep.maint_naval : rep.maint_land;
			st.maint_level = econ_maint_level(g_ec, g_player, nv);
			if (!nv) { for (k = 0; k < g_world.nland; k++) if (g_world.land[k].owner == g_player && !g_world.land[k].dead) {
				st.units[0] += g_world.land[k].infantry; st.units[1] += g_world.land[k].cavalry; st.units[2] += g_world.land[k].artillery; } }
			else { for (k = 0; k < g_world.nnaval; k++) if (g_world.naval[k].owner == g_player) {
				st.units[0] += g_world.naval[k].warships; st.units[1] += g_world.naval[k].galleys; st.units[2] += g_world.naval[k].transports; } }
		}
		if (g_infowin == UI_IW_COUNTRY) {                  /* rank by VP (score mode 2 of 0x517227, without its double count) */
			int k;
			static short vc[64];
			st.infowin = UI_IW_COUNTRY; st.rank = 1;
			if (g_dip) { st.nvas = diplo_vassal_candidates(g_dip, g_player, vc, 64); st.vas = vc; }
			for (k = 0; k < g_world.ncountry; k++) if (k != g_player && g_world.country[k].nprov_owned > 0 && g_world.country[k].vp > c->vp) st.rank++;
		}
		if (g_infowin == UI_IW_DOMESTIC) { static char note[48]; domestic_note(note, sizeof note); st.infowin = UI_IW_DOMESTIC; st.domestic_note = note; }
		if (g_infowin == UI_IW_PROVINCE && g_army >= 0 && g_army < g_world.nland && !g_world.land[g_army].dead && g_mil) {   /* our selected army */
			st.infowin = UI_IW_ARMY; st.army = g_army; st.army_maxmorale = mil_max_morale(g_mil, g_player);
			{	const LandUnit *su = &g_world.land[g_army];
				int k2;
				st.can_split = su->infantry + su->cavalry >= 2 || su->artillery >= 2; st.can_merge = mil_merge_count(g_mil, g_army) > 0;
			{	MilBattleView kb;   /* 0x45fa20: stationary, not in a battle, natives there */
				st.can_kill_natives = su->npath == 0 && su->location > 0 && su->location < g_world.nprov && g_world.prov[su->location].natives > 0
				                      && mil_battle_view(g_mil, su->location, &kb) == MBV_NONE;
			}
				for (k2 = 0; k2 < g_world.nland; k2++) if (!g_world.land[k2].dead && g_world.land[k2].owner == g_player && g_world.land[k2].location == su->location) st.nstack++;
			}
			st.army_days = g_world.land[g_army].arrive_day - g_dayno;
			st.attrition = mil_attrition(g_mil, g_world.land[g_army].location, g_player);
			if (g_world.land[g_army].l_id == LEAD_RANDOM) st.army_leader = g_world.land[g_army].lname;
			else if (g_world.land[g_army].l_id) { const Leader *ld = lead_get(g_lead, g_world.land[g_army].l_id - 1); st.army_leader = ld ? ld->name : NULL; }
			{	/* in a battle or a siege: the battle window instead (0x530d60) */
				static MilBattleView bv;
				if (mil_battle_view(g_mil, g_world.land[g_army].location, &bv) != MBV_NONE) { st.infowin = UI_IW_BATTLE; st.battle = &bv; }
			}
		}
		else if (g_infowin == UI_IW_PROVINCE && g_fleet >= 0 && g_fleet < g_world.nnaval && g_mil
		         && g_world.naval[g_fleet].warships + g_world.naval[g_fleet].galleys + g_world.naval[g_fleet].transports > 0) {   /* our selected fleet */
			st.infowin = UI_IW_NAVY; st.fleet = g_fleet;
			st.attrition = mil_naval_rate(g_mil, g_fleet);
			{	/* in a naval battle: the battle window (id 7) */
				static MilBattleView nbv;
				if (mil_battle_view(g_mil, g_world.naval[g_fleet].location, &nbv) == MBV_NAVAL) { st.infowin = UI_IW_BATTLE; st.battle = &nbv; st.army = -1; }
			}
			if (g_world.naval[g_fleet].l_id == LEAD_RANDOM) st.army_leader = g_world.naval[g_fleet].lname;
			else if (g_world.naval[g_fleet].l_id > 0) { const Leader *ld = lead_get(g_lead, g_world.naval[g_fleet].l_id - 1); st.army_leader = ld ? ld->name : NULL; }
			{	const NavalUnit *sf = &g_world.naval[g_fleet];
				int k2;
				st.can_split = sf->warships + sf->galleys + sf->transports >= 2 && mil_fleet_load(&g_world, g_fleet) == 0; st.can_merge = mil_merge_fleet_count(g_mil, g_fleet) > 0;
				for (k2 = 0; k2 < g_world.nnaval; k2++) if (g_world.naval[k2].owner == g_player && g_world.naval[k2].location == sf->location && g_world.naval[k2].warships + g_world.naval[k2].galleys + g_world.naval[k2].transports > 0) st.nstack++;
			}
			st.load_m = mil_fleet_load(&g_world, g_fleet); st.cap_m = mil_fleet_capacity(&g_world.naval[g_fleet]); st.unload_mode = g_unload;
			st.army_days = g_world.naval[g_fleet].arrive_day - g_dayno;
		}
	}
	if (g_infowin == UI_IW_PEACE && g_pw_enemy >= 0 && g_player >= 0) {
		static UiPeace up;
		PeaceOffer o;
		int war = pw_war(), sc;
		if (war < 0) { g_infowin = UI_IW_PROVINCE; g_pw_enemy = -1; }
		else {
			sc = peace_war_score(&g_world, war);
			memset(&up, 0, sizeof up);
			up.page = g_pw_page; up.demand = g_pw_demand; up.us = g_player; up.enemy = g_pw_enemy;
			up.score = peace_side(&g_world, war, g_player) == 1 ? sc : -sc;
			up.cand = g_pw_cand; up.picked = g_pw_pick; up.recv = g_pw_recv; up.ncand = g_pw_ncand; up.scroll = g_pw_scroll;
			pw_offer(&o);
			up.value = peace_offer_value(&g_world, &o); up.gold = g_pw_gold;
			up.vassal = g_pw_flag[0]; up.religion = g_pw_flag[1]; up.access = g_pw_flag[2]; up.can_send = 1;
			up.msg = g_pw_msg; up.side = g_pw_side; up.nside = g_pw_nside;
			{	const War *wr2 = &g_world.war[war];
				int s2 = peace_side(&g_world, war, g_player), lead_e = s2 == 1 ? wr2->def[0] : wr2->att[0];
				up.us_alone = (s2 == 1 ? wr2->att[0] : wr2->def[0]) != g_player;
				up.them_alone = g_pw_enemy != lead_e;
				up.can_toggle = !up.us_alone && g_pw_target >= 0 && g_pw_target != lead_e;
			}
			st.infowin = UI_IW_PEACE; st.peace = &up;
		}
	}
	if (g_infowin == UI_IW_RESOURCE && g_player >= 0 && g_ec && g_selected > 0 && g_selected < g_world.nprov && g_world.prov[g_selected].goods >= 0) {   /* windows4 4 */
		static EconGoods eg;
		econ_goods(g_ec, g_player, g_world.prov[g_selected].goods, &eg);
		st.infowin = UI_IW_RESOURCE; st.goods = &eg; st.goods_id = g_world.prov[g_selected].goods;
	}
	if (g_infowin == UI_IW_VASSAL && g_player >= 0 && g_dip) {             /* windows4 6.2 */
		static short vc2[64];
		st.infowin = UI_IW_VASSAL; st.nvas = diplo_vassal_candidates(g_dip, g_player, vc2, 64); st.vas = vc2;
		st.vas_sel = g_vas_sel < st.nvas ? g_vas_sel : -1;
	}
	if (g_infowin == UI_IW_LOANS && g_player >= 0 && g_lo_target >= 0) {   /* windows4 6.1 */
		long mx2 = g_world.country[g_player].treasury_m / 2000;
		if (g_lo_amount > mx2) g_lo_amount = mx2;
		if (g_lo_amount < 1) g_lo_amount = mx2 >= 1 ? 1 : 0;
		st.infowin = UI_IW_LOANS; st.loan_target = g_lo_target; st.loan_amount = g_lo_amount; st.loan_rate = g_lo_rate; st.loan_months = g_lo_months;
	}
	if (g_infowin == UI_IW_RELIGION && g_player >= 0) {             /* windows4 2 */
		static UiReligion ur;
		int s0 = rel_index(&g_world, g_world.country[g_player].religion), k;
		memset(&ur, 0, sizeof ur);
		if (s0 < 0) g_infowin = UI_IW_COUNTRY;
		else {
			ur.state = s0; ur.sel = g_rel_sel >= 0 ? g_rel_sel : s0;
			ur.ntarget = rel_targets(&g_world, s0, ur.target); ur.nshown = rel_shown(&g_world, s0, ur.shown);
			for (k = 0; k < ur.ntarget; k++) if (ur.target[k] == ur.sel) ur.can_convert = 1;
			ur.can_defend = rel_can_defend(&g_world, g_player);
			st.infowin = UI_IW_RELIGION; st.religion = &ur;
		}
	}
	if (g_infowin == UI_IW_PURCHASE && g_pu_prov > 0 && g_mil && g_player >= 0) {   /* windows4 1 */
		static UiPurchase pu;
		long used, k;
		memset(&pu, 0, sizeof pu);
		pu.naval = g_pu_naval; pu.prov = g_pu_prov;
		used = g_pu_naval ? g_pu_n[0] + g_pu_n[1] + g_pu_n[2] : (g_pu_n[0] + g_pu_n[1]) * 1000 + g_pu_n[2] * 1000;
		for (k = 0; k < 3; k++) {
			long a = g_pu_naval ? mil_recruit_avail(g_mil, g_player, g_pu_prov, 3) : mil_recruit_avail(g_mil, g_player, g_pu_prov, 0);
			a -= used; if (a < 0) a = 0;
			pu.avail[k] = g_pu_naval ? a : k == 2 ? a / 1000 * 10 : a / 1000 * 1000;
			pu.cost[k] = (g_pu_naval ? mil_ship_cost(g_mil, g_player, (int)k) : mil_recruit_cost(g_mil, g_player, (int)k)) / 1000;
			pu.ordered[k] = g_pu_naval ? g_pu_n[k] : k == 2 ? g_pu_n[k] * 10 : g_pu_n[k] * 1000;
			pu.total += pu.cost[k] * g_pu_n[k];
		}
		pu.can_ok = pu.total > 0 && pu.total * 1000 <= g_world.country[g_player].treasury_m;
		pu.bld_ok = bld_can(&g_world, g_mil, g_player, g_pu_prov, g_pu_naval ? BK_SHIPYARD : BK_CONSCRIPT) == BLD_OK;
		pu.bld_cost = bld_cost(&g_world, g_player, g_pu_prov, g_pu_naval ? BK_SHIPYARD : BK_CONSCRIPT) / 1000;
		if (!g_pu_naval) {                                  /* RAISEMERC (windows4 1.5, battle_merc 4) */
			long mi, mc, ma, price = mil_merc_price(g_mil, g_player);
			int have = mil_merc_offer(g_mil, &mi, &mc, &ma), acc = mil_merc_access(g_mil, g_player, g_pu_prov);
			pu.merc_shown = 1;
			pu.merc_ok = have && acc && price <= g_world.country[g_player].treasury_m;
			if (!acc) snprintf(pu.merc_tip, sizeof pu.merc_tip, "%s", text_get("RAISEMERC_I4"));
			else if (!have) snprintf(pu.merc_tip, sizeof pu.merc_tip, "%s", text_get("RAISEMERC_I2"));
			else {
				long v[4];
				v[0] = mi; v[1] = mc; v[2] = ma; v[3] = price;      /* %.0f of x1000 */
				fmt_game(pu.merc_tip, sizeof pu.merc_tip, text_get("RAISEMERC_I3"), v, 4);
				if (!pu.merc_ok) {                              /* RAISEMERC_I1 has "%0.f": made "%.0f" for fmt_game */
					char f1[160], *q;
					long v1[1];
					snprintf(f1, sizeof f1, "%s", text_get("RAISEMERC_I1"));
					if ((q = strstr(f1, "%0.f")) != NULL) { q[1] = '.'; q[2] = '0'; }
					v1[0] = price;
					fmt_game(pu.merc_tip, sizeof pu.merc_tip, f1, v1, 1);
				}
			}
		}
		st.infowin = UI_IW_PURCHASE; st.purchase = &pu;
	}
	if (g_infowin == UI_IW_MISSIONS && g_mis) {
		MisCur cu[11]; short av[11];
		int nc = mis_current(g_mis, cu, 11), na = mis_offers(g_mis, av, 11), k;
		st.infowin = UI_IW_MISSIONS;
		for (k = 0; k < nc; k++) { mis_title(g_mis, &g_world, cu[k].row, g_mis_t[k], 96, 0); st.mis_cur[k] = g_mis_t[k]; }
		for (k = 0; k < na; k++) { mis_title(g_mis, &g_world, av[k], g_mis_t[11 + k], 96, 0); st.mis_av[k] = g_mis_t[11 + k]; }
		st.nmis_cur = nc; st.nmis_av = na;
	}
	if ((g_infowin == UI_IW_COT || g_infowin == UI_IW_COLONIST || g_infowin == UI_IW_MISSIONARY) && g_player >= 0) {
		st.infowin = g_infowin; st.env_prov = g_env_prov; st.env_cot = g_env_cot;
		if (g_infowin == UI_IW_COT && g_env_cot >= 0) {
			st.env_can[0] = env_merchant_can(&g_world, g_ec, g_player, g_env_cot);
			st.env_cost = env_merchant_cost(&g_world, g_ec, g_player, g_env_cot); st.env_chance = env_merchant_chance(&g_world, g_ec, g_player);
		} else if (g_infowin == UI_IW_COLONIST && g_env_prov > 0) {
			st.env_can[1] = env_colonist_can(&g_world, g_mil, g_player, g_env_prov);
			st.env_cost = env_colonist_cost(&g_world, g_mil, g_player, g_env_prov); st.env_chance = env_colonist_chance(&g_world, g_ec, g_player, g_env_prov);
			st.env_months = 0;                                   /* arrives at once here (NIEPEWNE) */
			st.tr_can = env_trader_can(&g_world, g_player, g_env_prov);
			st.tr_cost = env_trader_cost(&g_world, g_player, g_env_prov); st.tr_chance = env_trader_chance(&g_world, g_ec, g_player, g_env_prov);
		} else if (g_infowin == UI_IW_MISSIONARY && g_env_prov > 0) {
			st.env_can[2] = env_missionary_can(&g_world, g_player, g_env_prov);
			st.env_cost = env_missionary_cost(&g_world, g_player, g_env_prov); st.env_chance = env_missionary_chance(&g_world, g_ec, g_player, g_env_prov);
			st.env_months = env_missionary_months(&g_world, g_player, g_env_prov);
		}
	}
	if (g_infowin == UI_IW_DIPLO && g_player >= 0) {
		int sel = g_dip_target >= 0 ? g_dip_target : g_player, k, n = 0;
		long bb = g_world.country[g_player].badboy_m;          /* reputation tier: badboy / limit, the limit is ours (25, NIEPEWNE) */
		static const long cut[9] = { 250, 2500, 5000, 10000, 15000, 20000, 25000, 30000, 35000 };
		st.infowin = UI_IW_DIPLO; st.dip_view = g_dip_view; st.dip_sel = sel; st.dip_target = g_dip_target; st.dip_hover = g_dip_hover;
		st.dip_act = g_dip_act; st.ndip_act = g_ndip_act; st.dip_confirm = g_dip_confirm >= 0 ? g_dip_msg : NULL;
		st.dip_rel = g_dip_target >= 0 ? world_rel(&g_world, g_player, g_dip_target) / 1000 : 0;
		for (st.dip_badboy_tier = 0; st.dip_badboy_tier < 9 && bb >= cut[st.dip_badboy_tier]; st.dip_badboy_tier++) ;
		dip_lines(sel);
		for (k = 0; k < g_ndip_line; k++) st.dip_lines[k] = g_dip_line[k];
		st.ndip_lines = g_ndip_line;
		for (k = 0; k < g_world.ncountry; k++) {                /* relations view: everyone sel has dealings with, sorted */
			long r;
			int j;
			if (k == sel || g_world.country[k].nprov_owned <= 0) continue;
			r = world_rel(&g_world, sel, k) / 1000;
			if (r == 0 && !mil_at_war(&g_world, sel, k) && !world_allied(&g_world, sel, k, ALLY_MILITARY)) continue;
			for (j = n; j > 0; j--) {
				long q = st.dip_listv[j - 1];
				int before = g_dip_sortnat ? strcmp(text_get(g_world.country[k].tag), text_get(g_world.country[st.dip_list[j - 1]].tag)) < 0
				                           : (g_dip_sortasc ? r < q : r > q);
				if (!before) break;
				if (j < 16) { st.dip_list[j] = st.dip_list[j - 1]; st.dip_listv[j] = q; }
			}
			if (j < 16) { st.dip_list[j] = (short)k; st.dip_listv[j] = r; if (n < 16) n++; }
		}
		st.ndip_list = n;
	}
	if (g_selected > 0 && g_selected < g_world.nprov && g_infowin == UI_IW_PROVINCE) {   /* foreign province / sea zone figures */
		const Province *p = &g_world.prov[g_selected];
		if (g_mil && g_player >= 0) {
			st.supply = mil_supply(g_mil, g_selected, g_player);
			st.attrition = mil_attrition(g_mil, g_selected, g_player);
			st.revolt = rev_risk(&g_world, g_mil, g_selected);
			{	/* green: our armies there would not starve; yellow: tight; red: they would (NIEPEWNE: the two checks 0x484000 / 0x483bf0) */
				long men = 0; int k;
				for (k = 0; k < g_world.nland; k++) if (g_world.land[k].owner == g_player && !g_world.land[k].dead && g_world.land[k].location == g_selected)
					men += g_world.land[k].infantry + g_world.land[k].cavalry;
				st.supply_col = men <= st.supply * 1000 / 2 ? 2 : men <= st.supply * 1000 ? 1 : 0;
			}
		}
		if (g_ec) { const short *pc = econ_prov_cots(g_ec); int k = pc ? pc[g_selected] : -1; if (k >= 0 && k < g_world.ncot) st.cot_name = g_world.prov[g_world.cot[k].prov].name; }
		if (p->terrain == 5 && g_mil) {
			short nb[64]; int n = mil_adjacent(g_mil, g_selected, nb, 64), k, j, pir = world_country(&g_world, "PIR");
			for (k = 0; k < n; k++) {
				int o = nb[k] > 0 && nb[k] < g_world.nprov ? g_world.prov[nb[k]].owner : -1;
				if (o < 0 || st.nnat >= 10) continue;
				for (j = 0; j < st.nnat && st.nat[j] != o; j++) ;
				if (j == st.nnat) st.nat[st.nnat++] = (short)o;
			}
			for (k = 0; k < g_world.nnaval; k++) if (pir >= 0 && g_world.naval[k].owner == pir && g_world.naval[k].location == g_selected)
				st.pirates += g_world.naval[k].warships + g_world.naval[k].galleys + g_world.naval[k].transports;
			st.priv_state = mil_privateer_state(g_mil, g_player, g_selected, NULL);
		}
	}
	if (g_mv) {                                         /* the minimap frame */
		int mx, my, mw, mh;
		ui_map_rect(&mx, &my, &mw, &mh);
		mv_get(g_mv, &st.view_cx, &st.view_cy, &st.view_zoom);
		st.view_w = mw; st.view_h = mh;
	}
	ui_draw(amigagfx_chunky(), amigagfx_pitch(), &g_world, &st);
	tut_overlay();
}

/* ------------------------------------------------------------- scripted input
 * PROGDIR:autoinput.txt drives the game from inside the guest (never synthesise host input):
 * one action per line, "<ms since start> <action> [args]":
 *   key <rawkey>   click <x> <y>   rclick <x> <y>   move <x> <y>   snap <file.ppm>
 *   wait dialog|choice|map   holds the script until that screen is up (drawn and waiting); the lines after it then
 *                            count from that moment (their times minus the wait line's)
 *   wait tut:<text>          until the tutorial message whose key contains <text> is up: its ShowMessage dialog,
 *                            or its AddMessage popup over a ready map; any other dialog meanwhile gets key 1
 * Clicks become a mouse-down + mouse-up pair. Lines starting with # are ignored. */
static const char *g_drawn = "";                /* the last redraw's reason; "closed" when a modal window returned */
static const char *g_tut_dlg;                   /* the key of the tutorial ShowMessage on screen, NULL none */
static int ain_screen_is(const char *want)
{
	int modal = !strcmp(g_drawn, "dialog") || !strcmp(g_drawn, "choice") || !strcmp(g_drawn, "closed") || !strcmp(g_drawn, "closing") || !strcmp(g_drawn, "menu")
	            || !strcmp(g_drawn, "slots") || !strcmp(g_drawn, "popmenu") || !strcmp(g_drawn, "msgset") || !strcmp(g_drawn, "manufactory");
	if (!strcmp(want, "map")) return *g_drawn && !modal;
	return !strcmp(g_drawn, want);
}
#define AIN_MAX 512
typedef struct { unsigned long ms; char act[8]; int a, b; char arg[40]; } AutoIn;
static AutoIn g_ain[AIN_MAX]; static int g_nain, g_ain_pos, g_ain_up;    /* g_ain_up: pending mouse-up */
static unsigned long g_ain_t0;

static void autoinput_load(void)
{
	FILE *f = fopen("PROGDIR:autoinput.txt", "r");
	char line[96];
	if (!f) return;
	while (g_nain < AIN_MAX && fgets(line, sizeof line, f)) {
		AutoIn *a = &g_ain[g_nain];
		char *p = line, *q;
		if (*p == '#' || *p < ' ') continue;
		a->ms = strtoul(p, &q, 10);
		if (q == p) continue;
		p = q; while (*p == ' ') p++;
		for (q = a->act; *p > ' ' && q < a->act + 7; ) *q++ = *p++;
		*q = 0;
		while (*p == ' ') p++;
		snprintf(a->arg, sizeof a->arg, "%s", p);
		for (q = a->arg; *q; q++) if (*q == '\n' || *q == '\r') *q = 0;
		a->a = (int)strtol(p, &q, 10); a->b = (int)strtol(q, NULL, 10);
		g_nain++;
	}
	fclose(f);
	g_ain_t0 = amigagfx_millis();
	logf_("autoinput: %d actions", g_nain);
}

/* The game's cursor (ui_cursor_*), drawn in software: at every blit it is put into the chunky screen,
 * the changed areas (the asked one, the cursor's new and old places) go to the display, and the pixels
 * under it are put back, so the screen buffer itself never holds the cursor. */
static int g_cur_on, g_ptr_x = -1, g_ptr_y = -1, g_cx0 = -1, g_cy0, g_cw0, g_ch0;
static unsigned char g_cur_save[96 * 64];

/* pointer=system: the hardware (Intuition) pointer instead of the software cursor - no drawing, no
 * save/restore, nothing to blit when the mouse moves. One pointerclass image per cursor kind (and per
 * palette: the menus have their own), made on first use from the game's bitmap in three colours (pens
 * 17-19, set from the select cursor at the start). g_cur_on 0 (the intro) = no pointer. */
static int g_sys_ptr, g_sys_last = -2, g_ptr_rgb_have;
static unsigned char g_ptr_rgb[9];
static void *g_sys_img[2 * UI_CUR_N];
static signed char g_sys_tried[2 * UI_CUR_N];
static void sys_ptr_sync(void)
{
	int id = g_cur_on ? ui_cursor_id() : -1;
	if (id == g_sys_last) return;
	g_sys_last = id;
	if (id >= 0 && id < 2 * UI_CUR_N && !g_sys_tried[id]) {
		static unsigned char pix[64 * 64];
		int w, h, hx, hy;
		g_sys_tried[id] = 1;
		if (ui_cursor_pointer(pix, &w, &h, &hx, &hy, g_ptr_rgb, &g_ptr_rgb_have) == 0 && h <= 64)
			g_sys_img[id] = amigagfx_pointer_new(pix, w, h, hx, hy);
		if (!g_sys_img[id]) logf_("pointer: no system pointer for cursor %d", id);
	}
	amigagfx_pointer_use(id >= 0 ? g_sys_img[id] : NULL);
}

static void scr_blit(int x, int y, int w, int h)
{
	unsigned char *d = amigagfx_chunky();
	int p = amigagfx_pitch(), gw = amigagfx_game_width(), gh = amigagfx_game_height(), cx = 0, cy = 0, cw = 0, ch = 0, have = 0, r;
	if (g_sys_ptr) { sys_ptr_sync(); if (w > 0 && h > 0) amigagfx_blit(x, y, w, h); return; }
	if (g_cur_on && g_ptr_x >= 0 && ui_cursor_box(g_ptr_x, g_ptr_y, &cx, &cy, &cw, &ch)) {
		if (cx < 0) { cw += cx; cx = 0; }
		if (cy < 0) { ch += cy; cy = 0; }
		if (cx + cw > gw) cw = gw - cx;
		if (cy + ch > gh) ch = gh - cy;
		if (cw > 0 && ch > 0 && cw <= 96 && ch <= 64) {
			for (r = 0; r < ch; r++) memcpy(g_cur_save + r * 96, d + (long)(cy + r) * p + cx, (size_t)cw);
			ui_cursor_draw(d, p, g_ptr_x, g_ptr_y);
			have = 1;
		}
	}
	if (w > 0 && h > 0) amigagfx_blit(x, y, w, h);
	if (have && (cx < x || cy < y || cx + cw > x + w || cy + ch > y + h)) amigagfx_blit(cx, cy, cw, ch);
	if (g_cx0 >= 0 && (!have || g_cx0 != cx || g_cy0 != cy || g_cw0 != cw || g_ch0 != ch)
	    && (g_cx0 < x || g_cy0 < y || g_cx0 + g_cw0 > x + w || g_cy0 + g_ch0 > y + h)) amigagfx_blit(g_cx0, g_cy0, g_cw0, g_ch0);
	if (have) { for (r = 0; r < ch; r++) memcpy(d + (long)(cy + r) * p + cx, g_cur_save + r * 96, (size_t)cw); g_cx0 = cx; g_cy0 = cy; g_cw0 = cw; g_ch0 = ch; }
	else g_cx0 = -1;
}

static int poll_raw(AmigaGfxEvent *e);
/* every pointer event moves the software cursor */
static int poll_ev(AmigaGfxEvent *e)
{
	int r = poll_raw(e);
	if (r && (e->type == AMIGAGFX_EV_MOUSEMOVE || e->type == AMIGAGFX_EV_MOUSEDOWN || e->type == AMIGAGFX_EV_MOUSEUP)) {
		int moved = e->x != g_ptr_x || e->y != g_ptr_y;
		g_ptr_x = e->x; g_ptr_y = e->y;
		if (moved && g_cur_on && !g_sys_ptr) scr_blit(0, 0, 0, 0);
	}
	return r;
}

/* amigagfx_poll plus the script */
static int poll_raw(AmigaGfxEvent *e)
{
	if (g_ain_up) { memset(e, 0, sizeof *e); e->type = AMIGAGFX_EV_MOUSEUP; e->x = g_ain_up >> 16; e->y = (g_ain_up >> 4) & 0xFFF; e->code = g_ain_up & 1; g_ain_up = 0; return 1; }
	while (g_ain_pos < g_nain && amigagfx_millis() - g_ain_t0 >= g_ain[g_ain_pos].ms) {
		const AutoIn *a = &g_ain[g_ain_pos];
		if (!strcmp(a->act, "wait") && !strncmp(a->arg, "tut:", 4)) {
			const char *want = a->arg + 4, *k[8];
			int dlg = !strcmp(g_drawn, "dialog") || !strcmp(g_drawn, "choice"), n, i, ok = 0;
			if (dlg && g_tut_dlg && strstr(g_tut_dlg, want)) ok = 1;
			else if (dlg) {                                   /* not ours (an AI offer, an older tutorial message) */
				g_drawn = "closing";
				memset(e, 0, sizeof *e); e->type = AMIGAGFX_EV_KEY; e->code = 0x01;
				logf_("autoinput: wait %s: dialog answered with key 1", a->arg);
				return 1;
			} else if (ain_screen_is("map") && g_tut)
				for (n = tut_messages(g_tut, k, 8), i = 0; i < n && !ok; i++) ok = strstr(k[i], want) != NULL;
			if (!ok) break;
			g_ain_t0 = amigagfx_millis() - a->ms;
		} else if (!strcmp(a->act, "wait")) {
			if (!strcmp(a->arg, "map") && (!strcmp(g_drawn, "dialog") || !strcmp(g_drawn, "choice"))) {   /* an unscripted dialog (AI offers): its first button */
				g_drawn = "closing";
				memset(e, 0, sizeof *e); e->type = AMIGAGFX_EV_KEY; e->code = 0x01;
				logf_("autoinput: wait map: dialog answered with key 1");
				return 1;
			}
			if (!ain_screen_is(a->arg)) break;
			g_ain_t0 = amigagfx_millis() - a->ms;
		}
		g_ain_pos++;
		memset(e, 0, sizeof *e);
		logf_("autoinput: %s %s", a->act, a->arg);
		if (!strcmp(a->act, "snap")) {                         /* with the software cursor, as the screen shows it */
			static unsigned char keep[96 * 64];
			int cx, cy, cw, ch, r2, on = g_cur_on && !g_sys_ptr && g_ptr_x >= 0 && ui_cursor_box(g_ptr_x, g_ptr_y, &cx, &cy, &cw, &ch);
			unsigned char *d = amigagfx_chunky();
			int p = amigagfx_pitch();
			if (on) { if (cx < 0) { cw += cx; cx = 0; } if (cy < 0) { ch += cy; cy = 0; }
			          if (cx + cw > amigagfx_game_width()) cw = amigagfx_game_width() - cx; if (cy + ch > amigagfx_game_height()) ch = amigagfx_game_height() - cy;
			          on = cw > 0 && ch > 0 && cw <= 96 && ch <= 64; }
			if (on) { for (r2 = 0; r2 < ch; r2++) memcpy(keep + r2 * 96, d + (long)(cy + r2) * p + cx, (size_t)cw); ui_cursor_draw(d, p, g_ptr_x, g_ptr_y); }
			amigagfx_dump_screen(a->arg);
			if (on) for (r2 = 0; r2 < ch; r2++) memcpy(d + (long)(cy + r2) * p + cx, keep + r2 * 96, (size_t)cw);
			continue;
		}
		if (!strcmp(a->act, "wait")) continue;
		e->x = a->a; e->y = a->b;
		if (!strcmp(a->act, "key")) { e->type = AMIGAGFX_EV_KEY; e->code = a->a; return 1; }
		if (!strcmp(a->act, "move")) { e->type = AMIGAGFX_EV_MOUSEMOVE; return 1; }
		if (!strcmp(a->act, "down") || !strcmp(a->act, "up")) {   /* a left button half (drags: down, move ..., up) */
			e->type = a->act[0] == 'd' ? AMIGAGFX_EV_MOUSEDOWN : AMIGAGFX_EV_MOUSEUP; e->code = AMIGAGFX_BUTTON_LEFT; return 1;
		}
		if (!strcmp(a->act, "click") || !strcmp(a->act, "rclick")) {
			e->type = AMIGAGFX_EV_MOUSEDOWN; e->code = a->act[0] == 'r' ? AMIGAGFX_BUTTON_RIGHT : AMIGAGFX_BUTTON_LEFT;
			g_ain_up = (a->a << 16) | ((a->b & 0xFFF) << 4) | e->code;
			return 1;
		}
	}
	return amigagfx_poll(e);
}

/* RTG screens taller than the sidebar: the map continues under it (rows g_side_h..), mv_set_left */
static int g_left, g_side_h = 1 << 30;
static int on_map(int x, int y, int mx, int my)
{
	int mx2, my2, mw, mh;
	ui_map_rect(&mx2, &my2, &mw, &mh);
	if (x >= mx && y >= my2 + mh - ui_log_height()) return 0;     /* the message log panel takes the click (0x577610) */
	return y >= my && (x >= mx || (g_left && y >= g_side_h));
}

static void redraw(MapView *mv, const char *why);
static Cfg *g_cfg;
static unsigned long g_t0;

/* every "%t" of a message template -> the player's address by victory points (0x579150):
 * MESS_MASTERCALL1..6 for VP/100 < 1, 2, 3, 4, 5, else 6; done before the printf formatting */
static void expand_t(char *out, int n, const char *src)
{
	int o = 0;
	char key[20];
	const char *call;
	long vp = g_player >= 0 ? g_world.country[g_player].vpt_m / 100000 : 0;   /* x1000 -> /100 */
	snprintf(key, sizeof key, "MESS_MASTERCALL%d", vp < 1 ? 1 : vp < 2 ? 2 : vp < 3 ? 3 : vp < 4 ? 4 : vp < 5 ? 5 : 6);
	call = text_get(key);
	for (; *src && o < n - 1; src++) {
		if (src[0] == '%' && src[1] == 't') { const char *c; for (c = call; *c && o < n - 1; c++) out[o++] = *c; src++; continue; }
		out[o++] = *src;
	}
	out[o] = 0;
}
static void fmt_date(char *date, int n)
{
	const Date *d = &g_world.now;
	int mi = (d->month >= 1 && d->month <= 12) ? d->month - 1 : 0;
	snprintf(date, (size_t)n, "%d %s %d", d->day * cal_month_days(mi + 1) / 30 + 1, mname[mi], d->year);
}

/* a modal window over the map (ui_event_draw): the game stands still until a choice is clicked or
 * keys 1-4 pressed. Esc, closing the window or the autoexit timer return -1. */
static int g_dlg_redraw;                        /* a dialog was closed: repaint the map under it */
static void (*g_dlg_bg)(void);                  /* what a dialog is drawn over when not the map (the Ledger) */
static int dialog(const char *title, const char *date, const char *text, const char *const lab[4])
{
	int hover = -1, page = 0, pages = 1, dirty = 1, pick = -2;
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			if (g_dlg_bg) { g_dlg_bg(); g_drawn = "dialog"; } else redraw(g_mv, "dialog");
			pages = ui_event_draw(amigagfx_chunky(), amigagfx_pitch(), title, date, text, lab, hover, page);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (pick == -2 && !dirty && poll_ev(&e)) {         /* a hover / page change is drawn before the next event */
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) pick = -1;
			else if (e.type == AMIGAGFX_EV_KEY && e.code >= 0x01 && e.code <= 0x04 && lab[e.code - 1]) pick = e.code - 1;   /* keys 1..4 */
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) {
				int h = ui_event_hit(e.x, e.y);
				if (h < 0) h = -1;
				if (h != hover) { hover = h; dirty = 1; }
			} else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_event_hit(e.x, e.y);
				if (h >= 0) pick = h;
				else if (h == UI_EV_PREV && page > 0) { page--; dirty = 1; }
				else if (h == UI_EV_NEXT && page < pages - 1) { page++; dirty = 1; }
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = -1;
		if (pick == -2) eu2_sleep_ms(20);
	}
	
	logf_("dialog \"%s\": choice %d", title, pick);
	g_dlg_redraw = 1;
	g_drawn = "closed";
	return pick;
}

/* an event for the player (events.c calls this while the day runs); -1 leaves the AI's pick */
/* the effect list of an action (windows4 3.2: one EE_* line per command); "\n"-separated into out */
static void action_effects(long id, int action, char *out, int n)
{
	static const struct { const char *kw, *key; char kind; } map[] = {   /* S sign+value, P province, Q province+sign+value, V value+province, C country, R country+sign+value, E event */
		{ "treasury", "EE_CASH", 'S' }, { "cash", "EE_CASH", 'S' }, { "manpower", "EE_MANPOWER", 'S' }, { "land", "EE_LANDTECH", 'S' },
		{ "naval", "EE_NAVALTECH", 'S' }, { "infra", "EE_INFRATECH", 'S' }, { "trade", "EE_TRADETECH", 'S' }, { "stability", "EE_STABILITY", 'S' },
		{ "diplomats", "EE_DIPLOMAT", 'S' }, { "colonists", "EE_COLONIST", 'S' }, { "merchants", "EE_MERCHANT", 'S' },
		{ "missionaries", "EE_MISSIONARY", 'S' }, { "inflation", "EE_INFLATION", 'S' }, { "vp", "EE_VP", 'S' }, { "badboy", "EE_REPUTATION", 'S' },
		{ "revolt", "EE_N_REVOLT", 'P' }, { "religiousrevolt", "EE_R_REVOLT", 'P' }, { "colonialrevolt", "EE_C_REVOLT", 'P' },
		{ "heretic", "EE_HERETICS", 'P' }, { "conversion", "EE_CONVERTED", 'P' }, { "conquistador", "EE_CONQUISTADOR", 'P' },
		{ "explorer", "EE_EXPLORER", 'P' }, { "gainmanufactory", "EE_GAIN_MANU", 'P' }, { "losemanufactory", "EE_LOSE_MANU", 'P' },
		{ "capital", "EE_CAPITAL", 'P' }, { "addcore", "EE_ADDCORE", 'P' }, { "removecore", "EE_REMCORE", 'P' }, { "cot", "EE_COT", 'P' },
		{ "removecot", "EE_RCOT", 'P' }, { "provincetax", "EE_P_TAX", 'Q' }, { "provincemanpower", "EE_P_MAN", 'Q' }, { "mine", "EE_P_MINE", 'Q' },
		{ "fortress", "EE_FORT", 'Q' }, { "population", "EE_POP", 'Q' }, { "province_revoltrisk", "EE_P_RR", 'Q' },
		{ "inf", "EE_INFANTRY", 'V' }, { "cav", "EE_CAVALRY", 'V' }, { "art", "EE_ARTILLERY", 'V' }, { "warships", "EE_WARSHIPS", 'V' },
		{ "galleys", "EE_GALLEYS", 'V' }, { "transports", "EE_TRANSPORTS", 'V' }, { "desertion", "EE_DESERTIONS", 'V' },
		{ "dynastic", "EE_ROY", 'C' }, { "vassal", "EE_VAS", 'C' }, { "alliance", "EE_ALL", 'C' }, { "breakdynastic", "EE_BROY", 'C' },
		{ "breakvassal", "EE_BVAS", 'C' }, { "inherit", "EE_INHERIT", 'C' }, { "war", "EE_WAR", 'C' }, { "independence", "EE_INDY", 'C' },
		{ "relation", "EE_RELATION", 'R' }, { "trigger", "EE_TRIGGER", 'E' }, { "sleepevent", "EE_SLEEP", 'E' } };
	EventEffect ef[24];
	int k, m, cnt = g_ev ? events_action_effects(g_ev, id, action, ef, 24) : 0;
	size_t l = 0;
	out[0] = 0;
	for (k = 0; k < cnt && l + 2 < (size_t)n; k++) {
		const char *pn, *cn, *sg = ef[k].value > 0 ? "+" : "";
		char line[160];
		for (m = 0; m < (int)(sizeof map / sizeof map[0]) && strcmp(map[m].kw, ef[k].kw); m++) ;
		if (m == (int)(sizeof map / sizeof map[0])) continue;
		pn = ef[k].which > 0 && ef[k].which < g_world.nprov && g_world.prov[ef[k].which].name ? g_world.prov[ef[k].which].name
		   : ef[k].which == -2 ? "our capital" : "a random province";               /* (the exe resolves it first: ours) */
		cn = ef[k].which >= 0 && ef[k].which < g_world.ncountry ? text_get(g_world.country[ef[k].which].tag) : "a country";
		switch (map[m].kind) {
		case 'S': snprintf(line, sizeof line, text_get(map[m].key), sg, (int)ef[k].value); break;
		case 'P': snprintf(line, sizeof line, text_get(map[m].key), pn); break;
		case 'Q': snprintf(line, sizeof line, text_get(map[m].key), pn, sg, (int)ef[k].value); break;
		case 'V': snprintf(line, sizeof line, text_get(map[m].key), (int)ef[k].value, pn); break;
		case 'C': snprintf(line, sizeof line, text_get(map[m].key), cn); break;
		case 'R': snprintf(line, sizeof line, text_get(map[m].key), cn, sg, (int)ef[k].value); break;
		default: { const char *en = events_name_of(g_ev, ef[k].value); snprintf(line, sizeof line, text_get(map[m].key), en ? text_get(en) : "?"); }
		}
		l += (size_t)snprintf(out + l, (size_t)n - l, "%s%s", l ? "\\n" : "", line);
	}
}

/* the event choice popup (windows4 3): modal; the action picked, -1 none (Esc: the AI's pick) */
static int choice_dialog(const char *title, const char *desc, const char *const lab[4], long evid)
{
	int mxp = 0, myp = 0;
	int hover = -1, page = 0, pages = 1, dirty = 1, pick = -2, was = g_paused;
	g_paused = 1;                                        /* the game stands still while it is open (0x590b80, NIEPEWNE) */
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "choice");
			pages = ui_choice_draw(amigagfx_chunky(), amigagfx_pitch(), title, desc, text_get("EVENTBUTTON_INFO"), lab,
			                       g_player >= 0 ? g_world.country[g_player].tag : NULL, hover, page);
			if (hover >= 0 && evid >= 0) {                     /* the action's effects (0x5914a0) */
				static char eff[700];
				action_effects(evid, hover, eff, sizeof eff);
				if (eff[0]) ui_tip_text(amigagfx_chunky(), amigagfx_pitch(), mxp, myp, eff, NULL);
			}
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (pick == -2 && !dirty && poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) pick = -1;
			else if (e.type == AMIGAGFX_EV_KEY && e.code >= 0x01 && e.code <= 0x04 && lab[e.code - 1]) pick = e.code - 1;   /* keys 1..4 */
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_choice_hit(e.x, e.y); if (h < 0) h = -1; if (h != hover) { hover = h; mxp = e.x; myp = e.y; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_choice_hit(e.x, e.y);
				if (h >= 0) pick = h;
				else if (h == UI_EV_PREV && page > 0) { page--; dirty = 1; }
				else if (h == UI_EV_NEXT && page < pages - 1) { page++; dirty = 1; }
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = -1;
		if (pick == -2) eu2_sleep_ms(20);
	}
	
	g_paused = was;
	logf_("choice \"%s\": %d", title ? title : "", pick);
	g_drawn = "closed";
	return pick;
}

static int event_choice(void *u, long today, int c, const EventInfo *ev)
{
	const char *lab[4];
	char date[64];
	int i;
	(void)u; (void)today; (void)c;
	fmt_date(date, sizeof date);
	for (i = 0; i < 4; i++) lab[i] = ev->action[i] ? text_get(ev->action[i]) : NULL;
	logf_("event dialog: %ld \"%s\"", ev->id, text_get(ev->name));
	i = choice_dialog(text_get(ev->name), text_get(ev->desc), lab, ev->id);
	{	char l[120];                                     /* the message log: the event and the choice (ours) */
		int a = i >= 0 && i < 4 && lab[i] ? i : 0;
		snprintf(l, sizeof l, "%s: %s", text_get(ev->name), lab[a] ? lab[a] : "");
		ledger_log(&g_world, l);
	}
	return i;
}

static void list_terms(char *s, int n, const short *provs, int np, long gold_m)
{
	int i, k = 0;
	s[0] = 0;
	for (i = 0; i < np && k < n - 40; i++) k += snprintf(s + k, (size_t)(n - k), "%s%s", i ? ", " : "", g_world.prov[provs[i]].name ? g_world.prov[provs[i]].name : "?");
	if (gold_m >= 1000) snprintf(s + k, (size_t)(n - k), "%s%ld ducats", np ? " and " : "", gold_m / 1000);
	else if (!np) snprintf(s, (size_t)n, "nothing (white peace)");
}

/* a winning AI leader offers the player terms (peace.c, monthly) */
static int peace_offer(void *u, int war, int winner, const short *provs, int np, long gold_m)
{
	const char *lab[4] = { NULL, NULL, NULL, NULL };
	char text[900], terms[560], date[64], l1[96], l2[160], l3[96], l5[600];
	const War *wr = &g_world.war[war];
	int lead = winner == 1 ? wr->att[0] : wr->def[0];
	const char *cn;
	(void)u;
	cn = text_get(g_world.country[lead].tag);
	list_terms(terms, sizeof terms, provs, np, gold_m);
	/* the herald (PEACEPROP_1..6): "%t" is the address by victory points (0x579150) */
	snprintf(l1, sizeof l1, text_get("PEACEPROP_1"), cn);
	{	char fmt[96];
		expand_t(fmt, sizeof fmt, text_get("PEACEPROP_2"));
		snprintf(l2, sizeof l2, fmt, cn);
	}
	snprintf(l3, sizeof l3, text_get("PEACEPROP_3"), "");
	snprintf(l5, sizeof l5, text_get("PEACEPROP_5"), terms);
	snprintf(text, sizeof text, "%s\\n%s %s %s\\n%s\\n%s", l1, l2, l3, text_get("PEACEPROP_4"), l5, text_get("PEACEPROP_6"));
	lab[0] = text_get("PEACEPROP_BTN1"); lab[1] = text_get("PEACEPROP_BTN2");
	logf_("peace offer from %s: %s", g_world.country[lead].tag, terms);
	fmt_date(date, sizeof date);
	sound_play("diplomat_alert");
	return dialog(text_get("PEACEPROP_HEADER"), date, text, lab) == 0;
}

/* the fort of an own city: expand it one level (mil_build_fort) */
static void fort_window(int prov)
{
	static const char *const ok[4] = { "OK", NULL, NULL, NULL };
	char text[300], date[64], title[64], l0[40];
	const char *lab[4] = { NULL, "Close", NULL, NULL };
	const Province *p = &g_world.prov[prov];
	int pick, rc, mx = mil_fort_max(&g_world, g_player);
	fmt_date(date, sizeof date);
	snprintf(title, sizeof title, "%s", p->name ? p->name : "?");
	(void)l0; (void)mx;
	lab[0] = "OK"; lab[1] = text_get("CANCEL");
	snprintf(text, sizeof text, text_get("CONFIRMFORT"), p->name ? p->name : "?", (int)(mil_fort_cost(g_mil, g_player) / 1000));   /* the confirm popup 0x5333d0 kind 1 */
	pick = dialog(title, date, text, lab);
	if (pick != 0) return;
	rc = mil_build_fort(g_mil, g_player, prov, g_dayno);
	logf_("fort in %d: %d", prov, rc);
	if (rc != MIL_RC_OK) {                                 /* the PIW's own texts (PIWEXPANDFORTERR*, tooltips in the exe) */
		if (rc == MIL_RC_MONEY) snprintf(text, sizeof text, text_get("PIWEXPANDFORTERR2"), (int)(mil_fort_cost(g_mil, g_player) / 1000));
		else if (rc == MIL_RC_TECH) snprintf(text, sizeof text, text_get("PIWEXPANDFORTERR3"), mx);
		else if (rc == MIL_RC_FULL) snprintf(text, sizeof text, "%s", text_get("PIWEXPANDFORTERR1"));
		else snprintf(text, sizeof text, "%s", text_get(p->population < 1000 ? "PIWEXPANDFORTERR5" : "PIWEXPANDFORTERR4"));
		dialog(title, date, text, ok);
	}
}

/* the build-manufactory sub-panel (SPEC_UI 3.3) over the province window; the chosen kind, or -1 */
static int manufactory_panel(int prov)
{
	unsigned char can[5];
	int i, hover = -1, dirty = 1, pick = -2;
	for (i = 0; i < 5; i++) can[i] = (unsigned char)(bld_can(&g_world, g_mil, g_player, prov, BK_MANU1 + i) == BLD_OK);
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "manufactory");
			ui_manupanel_draw(amigagfx_chunky(), amigagfx_pitch(), can, hover);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (pick == -2 && !dirty && poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) pick = -1;
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_manupanel_hit(e.x, e.y); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_manupanel_hit(e.x, e.y);
				if (h == 5) pick = -1;
				else if (h >= 0 && h < 5 && can[h]) pick = BK_MANU1 + h;
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = -1;
		if (pick == -2) eu2_sleep_ms(20);
	}
	logf_("manufactory panel in %d: %d", prov, pick);
	g_dlg_redraw = 1;
	return pick;
}

/* improvements of an own city (docs/SPEC_BUILDINGS.md): officials, then manufactory / shipyard /
 * conscription centre, then the five manufactories; the officials' place in the PIW is NIEZNANE, so
 * they are offered from the church */
static void improvements_window(int prov, int direct)   /* direct: that BK_* kind's confirm at once (-1 the manufactory panel), 0 the browser */
{
	static const char *const ok[4] = { "OK", NULL, NULL, NULL };
	static const int pk[4][3] = { { BK_TAXCOL, BK_JUDGE, BK_GOVERNOR }, { -1, BK_SHIPYARD, BK_CONSCRIPT },
	                              { BK_MANU1, BK_MANU1 + 1, BK_MANU1 + 2 }, { BK_MANU1 + 3, BK_MANU1 + 4, -2 } };
	static const char *nk[] = { "RCDM_BUILDBAILIFF", "RCDM_BUILDCOURTHOUSE", "RCDM_BUILDCITYRIGHTS", "RCDM_BUILDSHIPYARD", "RCDM_BUILDBARRACK",
	                            "PIWFACILITY_LUXURYGOODS", "PIWFACILITY_WEAPONS", "PIWFACILITY_NAVALEQUIPMENTS", "PIWFACILITY_REFINERY", "PIWFACILITY_MANUFACTUREDGOODS" };   /* the right-click menu's texts */
#define nm(i) text_get(nk[i])
	char text[300], date[64], title[64], l[3][44];
	int page = 0;
	fmt_date(date, sizeof date);
	snprintf(title, sizeof title, "%s", g_world.prov[prov].name ? g_world.prov[prov].name : "?");
	for (;;) {
		const char *lab[4] = { NULL, NULL, NULL, NULL };
		int i, pick, kind, rc;
		for (i = 0; i < 3; i++) {
			int k = pk[page][i];
			if (k == -1) { snprintf(l[i], sizeof l[i], "%s", text_get("RCDM_BUILDMANUFAC")); lab[i] = l[i]; }
			else if (k >= BK_TAXCOL) { snprintf(l[i], sizeof l[i], "%s %ld$", nm(k - BK_TAXCOL), bld_cost(&g_world, g_player, prov, k) / 1000); lab[i] = l[i]; }
		}
		lab[3] = page == 0 || page == 2 ? "More..." : "Close";
		if (direct) kind = direct;                         /* from the right-click menu */
		else {
			snprintf(text, sizeof text, "Improvements of %s.\\nTreasury: %ld ducats. One improvement at a time.", title, g_world.country[g_player].treasury_m / 1000);
			pick = dialog(title, date, text, lab);
			if (pick < 0) return;
			if (pick == 3) { if (page == 0) { page = 1; continue; } if (page == 2) { page = 3; continue; } return; }
			kind = pk[page][pick];
		}
		if (kind == -1 && (kind = manufactory_panel(prov)) < 0) return;   /* the sub-panel (SPEC_UI 3.3) */
		if (kind < BK_TAXCOL) return;
		if (!tut_btn_ok(g_tut, kind >= BK_MANU1 ? TB_BUILD_MANUFACTORY : kind == BK_TAXCOL ? TB_BUILD_BAILIFF : kind == BK_JUDGE ? TB_BUILD_COURTHOUSE
		                : kind == BK_GOVERNOR ? TB_BUILD_CITYRIGHTS : TB_NONE)) return;   /* the tutorial switched it off (NIEPEWNE: which official is which button) */
		{	/* the confirm popup (0x5333d0): CONFIRMMAN / BAILIFF / COURTHOUSE / CITYRIGHTS / BARRACKS / SHIPYARD */
			const char *ck = kind >= BK_MANU1 ? "CONFIRMMAN" : kind == BK_TAXCOL ? "CONFIRMBAILIFF" : kind == BK_JUDGE ? "CONFIRMCOURTHOUSE" :
			                 kind == BK_GOVERNOR ? "CONFIRMCITYRIGHTS" : kind == BK_CONSCRIPT ? "CONFIRMBARRACKS" : "CONFIRMSHIPYARD";
			const char *cl[4] = { "OK", NULL, NULL, NULL };
			cl[1] = text_get("CANCEL");
			snprintf(text, sizeof text, text_get(ck), title, (int)(bld_cost(&g_world, g_player, prov, kind) / 1000), bld_months(kind));
			if (dialog(title, date, text, cl) != 0) return;
		}
		rc = bld_start(&g_world, g_mil, g_player, prov, kind, g_dayno);
		logf_("improvement %d in %d: %d", kind, prov, rc);
		if (rc == BLD_OK) { snprintf(text, sizeof text, "%s", nm(kind - BK_TAXCOL));   /* the confirm already gave the months */ dialog(title, date, text, ok); }
		else {                                             /* the game's texts for each case (tooltips in the exe) */
			int cost = (int)(bld_cost(&g_world, g_player, prov, kind) / 1000);
			const char *k = NULL;
			text[0] = 0;
			switch (kind) {
			case BK_TAXCOL: k = rc == BLD_MONEY ? "PIWBAILIFF_I3" : rc == BLD_HAVE ? "PIWBAILIFF_I0" : "PURCHASEBAR_IF4"; break;
			case BK_JUDGE: k = rc == BLD_MONEY ? "PIWCOURTHOUSE_I3" : rc == BLD_TECH ? "PIWCOURTHOUSE_I4" : rc == BLD_HAVE ? "PIWCOURTHOUSE_I0" : "PURCHASEBAR_IF4"; break;
			case BK_GOVERNOR: k = rc == BLD_MONEY ? "PIWCITYRIGHTS_I3" : rc == BLD_HAVE ? "PIWCITYRIGHTS_I0" : "PURCHASEBAR_IF4"; break;
			case BK_SHIPYARD: k = rc == BLD_MONEY ? "PURCHASESHIP_IF3" : rc == BLD_HAVE ? "PURCHASESHIP_IF1" : rc == BLD_BUSY ? "PURCHASESHIP_IF4" : rc == BLD_TECH && g_world.country[g_player].naval_tech < 11 ? "PURCHASESHIP_IF2" : "PURCHASESHIP_IF5"; break;
			case BK_CONSCRIPT: k = rc == BLD_MONEY ? "PURCHASEBAR_IF3" : rc == BLD_HAVE ? "PURCHASEBAR_IF1" : rc == BLD_BUSY ? "PURCHASEBAR_IF4" : rc == BLD_TECH ? "PURCHASEBAR_IF2" : "PURCHASEBAR_IF5"; break;
			default: k = rc == BLD_MONEY ? "PIWBUILDMANUFACTORYERR4" : rc == BLD_BUSY ? "PIWBUILDMANUFACTORYERR3" : "PIWBUILDMANUFACTORYERR1"; break;
			}
			if (strstr(text_get(k), "%s")) snprintf(text, sizeof text, text_get(k), title);
			else snprintf(text, sizeof text, text_get(k), cost);
			dialog(title, date, text, ok);
		}
		return;
	}
#undef nm
}

/* ------------------------------------------- the peace window (SPEC_UI §4.12) */
static void pw_build(void)
{
	int war = pw_war(), s, k, give;
	const War *wr;
	g_pw_ncand = g_pw_nside = 0; g_pw_scroll = 0;
	memset(g_pw_pick, 0, sizeof g_pw_pick);
	if (war < 0 || !g_peace) return;
	wr = &g_world.war[war];
	s = peace_side(&g_world, war, g_player);
	give = g_pw_demand ? 3 - s : s;
	if (pw_sep(war) >= 0) g_pw_ncand = peace_candidates_sep(g_peace, war, g_player, g_pw_enemy, g_pw_demand ? PO_DEMAND : PO_OFFER, g_pw_cand, PW_MAX);
	else g_pw_ncand = peace_candidates(g_peace, war, g_player, g_pw_demand ? PO_DEMAND : PO_OFFER, g_pw_cand, PW_MAX);
	for (k = 0; k < g_pw_ncand; k++) {
		int c = g_world.prov[g_pw_cand[k]].controller;
		g_pw_recv[k] = (short)(c >= 0 && peace_side(&g_world, war, c) == 3 - give ? c : (3 - give == 1 ? wr->att[0] : wr->def[0]));
	}
	if (3 - give == 1) for (k = 0; k < wr->natt && g_pw_nside < 32; k++) g_pw_side[g_pw_nside++] = wr->att[k];
	else for (k = 0; k < wr->ndef && g_pw_nside < 32; k++) g_pw_side[g_pw_nside++] = wr->def[k];
}

static void dip_open(MapView *mv, int target);
static void dip_close(MapView *mv);

/* Offer Peace (0x10): the leader of the other side; page 2 when we may annex them */
static int peace_open(int target)
{
	static const char *const ok[4] = { "OK", NULL, NULL, NULL };
	int war = peace_find_war(&g_world, g_player, target), s;
	char date[64];
	const War *wr;
	if (war < 0 || !g_peace) return 0;
	wr = &g_world.war[war];
	s = peace_side(&g_world, war, g_player);
	(void)ok; (void)date;
	g_pw_target = peace_side(&g_world, war, target) == 3 - s ? target : (s == 1 ? wr->def[0] : wr->att[0]);
	g_pw_enemy = (s == 1 ? wr->att[0] : wr->def[0]) != g_player ? g_pw_target : (s == 1 ? wr->def[0] : wr->att[0]);   /* a member settles apart (0x5119d7) */
	g_pw_page = g_opts.opt[OPT_ANNEX] && pw_sep(war) < 0 && peace_can_annex(g_peace, war, g_player) ? 2 : 0;   /* FORCED ANNEXATION (0x45cd00) */
	g_pw_gold = 0; g_pw_flag[0] = g_pw_flag[1] = g_pw_flag[2] = 0; g_pw_ncand = 0;
	snprintf(g_pw_msg, sizeof g_pw_msg, text_get("PCAMSG"), text_get(g_world.country[g_pw_enemy].tag),
	         world_name(&g_world, g_world.country[g_pw_enemy].religion) ? world_name(&g_world, g_world.country[g_pw_enemy].religion) : "");
	g_infowin = UI_IW_PEACE;
	return 1;
}

/* a click in the peace window; returns 1 when it was the window's */
static int peace_click(MapView *mv, int x, int y)
{
	static const char *const ok[4] = { "OK", NULL, NULL, NULL };
	UiPeace up;
	int h, war = pw_war(), r = -99;
	char date[64], text[160];
	memset(&up, 0, sizeof up);
	up.page = g_pw_page; up.demand = g_pw_demand; up.ncand = g_pw_ncand; up.scroll = g_pw_scroll;
	if (war >= 0) {
		const War *wr2 = &g_world.war[war];
		int s2 = peace_side(&g_world, war, g_player), lead_e = s2 == 1 ? wr2->def[0] : wr2->att[0];
		up.can_toggle = (s2 == 1 ? wr2->att[0] : wr2->def[0]) == g_player && g_pw_target >= 0 && g_pw_target != lead_e;
	}
	h = ui_peace_hit(x, y, &up);
	if (h == UP_NONE) return 0;
	if (war < 0) { g_infowin = UI_IW_PROVINCE; g_pw_enemy = -1; return 1; }   /* the war is over meanwhile */
	switch (h) {
	case UP_CANCEL:
		if (g_pw_page == 2) g_pw_page = 0;
		else { int e = g_pw_enemy; g_pw_enemy = -1; dip_open(mv, e); }
		return 1;
	case UP_THEM: {                                     /* the enemy leader or the clicked member alone */
		const War *wr2 = &g_world.war[war];
		int s2 = peace_side(&g_world, war, g_player), lead_e = s2 == 1 ? wr2->def[0] : wr2->att[0];
		g_pw_enemy = g_pw_enemy == lead_e ? g_pw_target : lead_e;
		g_pw_page = 0;
		return 1;
	}
	case UP_DEMAND: case UP_OFFER:
		g_pw_demand = h == UP_DEMAND; g_pw_page = 1; g_pw_gold = 0; g_pw_flag[0] = g_pw_flag[1] = g_pw_flag[2] = 0; pw_build();
		return 1;
	case UP_NOTRIB: { PeaceOffer o; memset(&o, 0, sizeof o); r = peace_player_propose(g_peace, war, g_player, &o, g_dayno); break; }
	case UP_SEND: { PeaceOffer o; pw_offer(&o); r = peace_player_propose(g_peace, war, g_player, &o, g_dayno); break; }
	case UP_ANNEX: r = peace_player_annex(g_peace, war, g_player, g_dayno); break;
	case UP_PLUS: { long cap = g_pw_demand ? 2500 : g_world.country[g_player].treasury_m / 1000; if (g_pw_gold + 25 <= cap) g_pw_gold += 25; return 1; }
	case UP_MINUS: if (g_pw_gold >= 25) g_pw_gold -= 25; return 1;
	case UP_VASSAL: case UP_RELIGION: case UP_ACCESS: g_pw_flag[h - UP_VASSAL] = !g_pw_flag[h - UP_VASSAL]; return 1;
	case UP_MORE: g_pw_scroll += ui_peace_rows(); if (g_pw_scroll >= g_pw_ncand) g_pw_scroll = 0; return 1;
	default:
		if (h >= UP_ROW && h - UP_ROW < g_pw_ncand) {
			int k = h - UP_ROW, n = 0, i;
			for (i = 0; i < g_pw_ncand; i++) n += g_pw_pick[i];
			if (g_pw_pick[k] || n < PO_MAXPROV) g_pw_pick[k] = (unsigned char)!g_pw_pick[k];
		}
		return 1;
	}
	logf_("peace window: action %d to %s -> %d", h, g_world.country[g_pw_enemy].tag, r);
	fmt_date(date, sizeof date);
	if (r == PEACE_ACCEPTED || r == PEACE_REFUSED)                 /* the game's PEACEACCEPT / PEACEREJECT */
		msg_typed(r == PEACE_ACCEPTED ? "PEACEACCEPT" : "PEACEREJECT", g_world.country[g_pw_enemy].capital, text_get(g_world.country[g_pw_enemy].tag), NULL, NULL);
	else { snprintf(text, sizeof text, "%s", "Only the leader of our side can make peace."); dialog(text_get(g_world.country[g_pw_enemy].tag), date, text, ok); }
	if (r == PEACE_ACCEPTED) { g_pw_enemy = -1; dip_close(mv); refresh_map(mv); }
	else { int e = g_pw_enemy; g_pw_enemy = -1; dip_open(mv, e); }   /* back to the diplomacy window (id 2) */
	return 1;
}

/* the rubber band: four white 1-px lines (0x526fe4); the pixels under them are kept so moving it
 * needs no map redraw */
static int g_mouse_x = -1, g_mouse_y = -1, band_x0, band_y0, band_on;
static unsigned char *g_band_save;
static int g_bx0, g_by0, g_bx1, g_by1, g_band_have;
static void band_restore(void)
{
	unsigned char *d = amigagfx_chunky();
	int p = amigagfx_pitch(), y;
	if (!g_band_have || !g_band_save) return;
	for (y = g_by0; y <= g_by1; y++) {
		long o = (long)y * p;
		if (y == g_by0 || y == g_by1) memcpy(d + o + g_bx0, g_band_save + o + g_bx0, (size_t)(g_bx1 - g_bx0 + 1));
		else { d[o + g_bx0] = g_band_save[o + g_bx0]; d[o + g_bx1] = g_band_save[o + g_bx1]; }
	}
}
static void band_draw(int x0, int y0, int x1, int y1)
{
	unsigned char *d = amigagfx_chunky();
	int p = amigagfx_pitch(), gw = amigagfx_game_width(), gh = amigagfx_game_height(), mx, my, mw, mh, y, ux0, uy0, ux1, uy1;
	unsigned char white = g_inv[0xFFF];
	ui_map_rect(&mx, &my, &mw, &mh);
	if (!g_band_save) { g_band_save = (unsigned char *)malloc((size_t)p * gh); if (!g_band_save) return; memcpy(g_band_save, d, (size_t)p * gh); }
	band_restore();
	ux0 = g_band_have ? g_bx0 : gw; uy0 = g_band_have ? g_by0 : gh; ux1 = g_band_have ? g_bx1 : 0; uy1 = g_band_have ? g_by1 : 0;
	if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
	if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
	if (x0 < mx) x0 = mx;
	if (y0 < my) y0 = my;
	if (x1 > gw - 1) x1 = gw - 1;
	if (y1 > gh - 1) y1 = gh - 1;
	g_bx0 = x0; g_by0 = y0; g_bx1 = x1; g_by1 = y1; g_band_have = 1;
	for (y = y0; y <= y1; y++) {
		long o = (long)y * p;
		if (y == y0 || y == y1) memset(d + o + x0, white, (size_t)(x1 - x0 + 1));
		else { d[o + x0] = white; d[o + x1] = white; }
	}
	if (x0 < ux0) ux0 = x0;
	if (y0 < uy0) uy0 = y0;
	if (x1 > ux1) ux1 = x1;
	if (y1 > uy1) uy1 = y1;
	scr_blit(ux0, uy0, ux1 - ux0 + 1, uy1 - uy0 + 1);
}
static void band_end(void)
{
	band_restore();
	if (g_band_have) scr_blit(g_bx0, g_by0, g_bx1 - g_bx0 + 1, g_by1 - g_by0 + 1);
	free(g_band_save); g_band_save = NULL; g_band_have = 0; band_on = 0;
}

static unsigned long g_pr[5];                    /* probes (probemodes=1), microseconds: map, overlays, units, ui, blit */

/* what lies on the map and moves with it but never animates: terrain decoration, cities, sieges, cores */
static void map_objects(MapView *mv, unsigned char *d, int pitch, int w, int h)
{
	if (g_mm.mode == MM_NORMAL) { long cx, cy; int z; mv_get(mv, &cx, &cy, &z); units_draw_terrain(&g_world, mv, d, pitch, w, h, z); }
	if (g_mm.mode == MM_NORMAL || g_mm.mode == MM_POLITICAL) units_draw_cities(&g_world, mv, d, pitch, w, h);
	if (g_mil) units_draw_sieges(&g_world, mv, g_mil, d, pitch, w, h);
	if (g_mm.mode == MM_POLITICAL) units_draw_cores(&g_world, mv, d, pitch, w, h, g_player);
}

/* The map layer (user 2026-11-10: "dirty rectangles" when scrolling): the map tiles plus map_objects in a buffer of
 * their own, kept while nothing that draws into it changed (the tiles' generation, zoom, day, mode, player, owners).
 * A scroll moves what stays with memmove and draws only the strips that came into view; units, routes, the log and
 * the bars are drawn over the copy on the screen every frame, as before. */
static unsigned char *g_lay;
static long g_lay_sz, g_lay_lx, g_lay_ly;
static int g_lay_ok, g_lay_w, g_lay_h, g_lay_last;          /* g_lay_last: 0 full, 1 scrolled, 2 reused as it was */
static unsigned long g_lay_key[6];
static int layer_draw(MapView *mv, unsigned char *scr, int pitch, int w, int h)
{
	long lx0, ly0, lw, cx, cy;
	int z, n = 0, dx = 0, dy = 0, full = 1, y;
	unsigned long key[6];
	if ((long)w * h > g_lay_sz) {
		free(g_lay); g_lay_ok = 0;
		g_lay = malloc((size_t)w * h);
		g_lay_sz = g_lay ? (long)w * h : 0;
		if (!g_lay) return -1;
	}
	mv_get(mv, &cx, &cy, &z);
	key[0] = mv_gen(mv); key[1] = (unsigned long)z; key[2] = (unsigned long)g_dayno; key[3] = (unsigned long)g_mm.mode;
	key[4] = (unsigned long)g_player; key[5] = (unsigned long)g_world.owner_epoch;
	mv_draw_rect(mv, g_lay, w, w, h, 0, 0, 0, 0, &lx0, &ly0, &lw);          /* only where the view now is */
	if (g_lay_ok && w == g_lay_w && h == g_lay_h && !memcmp(key, g_lay_key, sizeof key)) {
		dx = (int)(lx0 - g_lay_lx);
		if (dx > lw / 2) dx -= (int)lw; else if (dx < -lw / 2) dx += (int)lw;   /* across the date line */
		dy = (int)(ly0 - g_lay_ly);
		if (abs(dx) < w && abs(dy) < h) full = 0;
	}
	if (full) {
		n = mv_draw_rect(mv, g_lay, w, w, h, 0, 0, w, h, NULL, NULL, NULL);
		map_objects(mv, g_lay, w, w, h);
		g_lay_last = 0;
	} else if (dx || dy) {
		int keep = w - abs(dx);
		if (dy >= 0) for (y = 0; y < h - dy; y++) memmove(g_lay + (long)y * w + (dx < 0 ? -dx : 0), g_lay + (long)(y + dy) * w + (dx > 0 ? dx : 0), (size_t)keep);
		else for (y = h - 1; y >= -dy; y--) memmove(g_lay + (long)y * w + (dx < 0 ? -dx : 0), g_lay + (long)(y + dy) * w + (dx > 0 ? dx : 0), (size_t)keep);
		if (dx) {                                       /* the column that came in */
			int rx = dx > 0 ? w - dx : 0, rw = abs(dx);
			n += mv_draw_rect(mv, g_lay, w, w, h, rx, 0, rw, h, NULL, NULL, NULL);
			mv_set_scr_off2(mv, -rx, 0); map_objects(mv, g_lay + rx, w, rw, h); mv_set_scr_off2(mv, 0, 0);
		}
		if (dy) {                                       /* the rows that came in (the corner again: tiles first, so no double shadows) */
			int ry = dy > 0 ? h - dy : 0, rh = abs(dy);
			n += mv_draw_rect(mv, g_lay, w, w, h, 0, ry, w, rh, NULL, NULL, NULL);
			mv_set_scr_off2(mv, 0, -ry); map_objects(mv, g_lay + (long)ry * w, w, w, rh); mv_set_scr_off2(mv, 0, 0);
		}
		g_lay_last = 1;
	} else g_lay_last = 2;
	g_lay_ok = 1; g_lay_w = w; g_lay_h = h; g_lay_lx = lx0; g_lay_ly = ly0;
	memcpy(g_lay_key, key, sizeof key);
	for (y = 0; y < h; y++) memcpy(scr + (long)y * pitch, g_lay + (long)y * w, (size_t)w);
	return n;
}

/* ---- message windows (SPEC: 0x578d50 / 0x426610 / 0x428e10): not modal, any number, the newest in
 * front; dragged by the body; BTN1 (Enter) closes, BTN2 (Space) Goto / Details and the window stays;
 * a right click opens Message properties. The game runs on unless the type's pause is set. */
#define NMW 24
static UiMsgWin g_mw[NMW];
static short g_mw_prov[NMW], g_mw_kind[NMW];
static char g_mw_type[NMW][24];
static int g_nmw, g_mw_drag = -1, g_mw_gx, g_mw_gy, g_mw_hw = -1, g_mw_hb = -1, g_mw_paused;
static void mw_draw(unsigned char *dst, int pitch)
{
	int i;
	for (i = 0; i < g_nmw; i++) ui_msgwin_draw(dst, pitch, &g_mw[i], i == g_mw_hw ? g_mw_hb : -1);
}
static void mw_close(int i)
{
	if (i < 0 || i >= g_nmw) return;
	memmove(&g_mw[i], &g_mw[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw[0]);
	memmove(&g_mw_prov[i], &g_mw_prov[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_prov[0]);
	memmove(&g_mw_kind[i], &g_mw_kind[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_kind[0]);
	memmove(g_mw_type[i], g_mw_type[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_type[0]);
	g_nmw--;
	g_mw_hw = g_mw_hb = -1; g_mw_drag = -1;
	if (g_mw_paused) { g_paused = 0; g_mw_paused = 0; }   /* the dtor clears the message pause for any popup (0x4270c0) */
}
static void mw_front(int i)
{
	UiMsgWin m; short p, k; char t[24];
	if (i < 0 || i >= g_nmw - 1) return;
	m = g_mw[i]; p = g_mw_prov[i]; k = g_mw_kind[i]; memcpy(t, g_mw_type[i], sizeof t);
	memmove(&g_mw[i], &g_mw[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw[0]);
	memmove(&g_mw_prov[i], &g_mw_prov[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_prov[0]);
	memmove(&g_mw_kind[i], &g_mw_kind[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_kind[0]);
	memmove(g_mw_type[i], g_mw_type[i + 1], (size_t)(g_nmw - i - 1) * sizeof g_mw_type[0]);
	g_mw[g_nmw - 1] = m; g_mw_prov[g_nmw - 1] = p; g_mw_kind[g_nmw - 1] = k; memcpy(g_mw_type[g_nmw - 1], t, sizeof t);
}

static void redraw(MapView *mv, const char *why)
{
	unsigned long t0 = amigagfx_millis(), t1, tm, tu, u0 = amiga_uclock_us(), u1;
	g_drawn = why;
	int gw = amigagfx_game_width(), gh = amigagfx_game_height(), n, mx, my, mw, mh, lay = 0;
	unsigned char *dst = amigagfx_chunky();
	int pitch = amigagfx_pitch();
	ui_map_rect(&mx, &my, &mw, &mh);
	units_set_viewer(g_player);                          /* before anything of the map is drawn: terra incognita hides it all */
	mv_select(mv, g_selected, g_mm.mode != MM_TRADE);   /* the selected province darker (0x482870) */
	if (g_left == 0 && (n = layer_draw(mv, dst + mx, pitch, mw, mh)) >= 0) lay = 1;   /* the map layer (not with the map under the sidebar) */
	else n = mv_draw(mv, dst + mx, pitch, mw, mh);
	u1 = amiga_uclock_us(); g_pr[0] = u1 - u0; u0 = u1;
	mv_set_scr_off(mv, g_left);                         /* the overlays from the screen's x 0: the map under the sidebar gets them too */
	tm = amigagfx_millis();
	if (!lay) map_objects(mv, dst + mx - g_left, pitch, mw + g_left, mh);
	{	/* a single pick elsewhere replaced g_army / g_fleet: the selection follows */
		int k, in = 0;
		for (k = 0; k < g_nselA; k++) if (g_selA[k] == g_army) in = 1;
		if (g_army < 0) g_nselA = 0; else if (!in) { g_selA[0] = (short)g_army; g_nselA = 1; }
		for (k = 0, in = 0; k < g_nselF; k++) if (g_selF[k] == g_fleet) in = 1;
		if (g_fleet < 0) g_nselF = 0; else if (!in) { g_selF[0] = (short)g_fleet; g_nselF = 1; }
		for (k = 0; k < g_nselA; ) if (g_selA[k] < 0 || g_selA[k] >= g_world.nland || g_world.land[g_selA[k]].dead || g_world.land[g_selA[k]].owner != g_player) g_selA[k] = g_selA[--g_nselA]; else k++;
		units_set_time(amigagfx_millis());
		units_select_list(g_selA, g_nselA, g_selF, g_nselF);
	}
	u1 = amiga_uclock_us(); g_pr[1] = u1 - u0; u0 = u1;
	units_set_viewer(g_player);                          /* terra incognita hides units always (the fog option is about the rest) */
	units_set_today(g_dayno);
	if (g_mm.mode != MM_DIPLOMATIC && g_mm.mode != MM_RELATIONS)    /* the diplomatic maps carry no units or cities (eu2.exe 0x47fd20) */
		units_draw(&g_world, mv, dst + mx - g_left, pitch, mw + g_left, mh, 0);
	else                                                              /* shields + relation ornaments on the capitals */
		units_draw_dipl(&g_world, mv, dst + mx - g_left, pitch, mw + g_left, mh, g_infowin == UI_IW_DIPLO && g_dip_target >= 0 ? g_dip_target : g_player);
	mv_set_scr_off(mv, 0);
	{	/* the message log: the last five messages over the bottom of the map */
		const char *ll[5];
		int nl = ledger_log_recent(ll, 5);
		ui_log_draw(dst, pitch, mx, my + mh, mw, ll, nl);   /* always, also empty (0x577080) */
	}
	tu = amigagfx_millis();
	u1 = amiga_uclock_us(); g_pr[2] = u1 - u0; u0 = u1;
	draw_bar();
	mw_draw(dst, pitch);                                  /* the message windows over everything */
	t1 = amigagfx_millis();
	u1 = amiga_uclock_us(); g_pr[3] = u1 - u0; u0 = u1;
	if (lay && g_lay_last == 2 && !g_nmw && (!strcmp(why, "uanim") || !strcmp(why, "anim"))) scr_blit(mx, 0, gw - mx, gh);   /* only the map changed */
	else scr_blit(0, 0, gw, gh);
	g_pr[4] = amiga_uclock_us() - u0;
	{
		long cx, cy; int z;
		mv_get(mv, &cx, &cy, &z);
		if (strcmp(why, "uanim")) logf_("map %s: zoom %d at %ld,%ld - %d tiles decoded%s, draw %lu ms (map %lu, units %lu, ui %lu), blit %lu ms", why, z, cx, cy, n, !lay ? "" : g_lay_last == 0 ? " (layer new)" : g_lay_last == 1 ? " (layer scrolled)" : " (layer kept)",
		      t1 - t0, tm - t0, tu - tm, t1 - tu, amigagfx_millis() - t1);
	}
}

/* ------------------------------------------------------- diplomacy windows (SPEC_UI 4.8) */

static void dip_add(int id, const char *key, long cost_m)
{
	UiDipAct *a;
	int st;
	if (g_ndip_act >= 32) return;
	a = &g_dip_act[g_ndip_act];
	if (id < 0) { if (g_ndip_act && g_dip_act[g_ndip_act - 1].id >= 0) { a->id = -1; a->label[0] = 0; a->state = 0; g_ndip_act++; } return; }
	st = g_dip ? diplo_action_state(g_dip, g_player, g_dip_target, id, g_dayno) : DS_OFF;
	if (id == DA_WAR && g_tut && st == DS_ON) st = DS_OFF;   /* tutorial games: the DIPWAR rule is false (vfunc +0x378) */
	if (st == DS_HIDDEN) return;
	a->id = id; a->state = st;
	snprintf(a->label, sizeof a->label, text_get(key), (int)(cost_m / 1000));
	g_ndip_act++;
}

/* the action list of the panel, in the original's order (0x50c980) */
static void dip_build(void)
{
	g_ndip_act = 0;
	if (g_dip_target < 0 || g_player < 0) return;
#define C(a) (g_dip ? diplo_action_cost(g_dip, g_player, g_dip_target, a) : 0)
	if (g_dip_view) {
		dip_add(DA_GIFT_STATE, "DIP_STATE_GIFT", C(DA_GIFT_STATE)); dip_add(DA_GIFT_PERS, "DIP_PERSONAL_GIFT", C(DA_GIFT_PERS));
		dip_add(DA_INTRO, "DIP_INTRODUCTION", C(DA_INTRO)); dip_add(DA_LOAN, "DIPOLOAN", 0); dip_add(-1, NULL, 0);   /* (the loan's place: NIEPEWNE - the action list is full) */
		dip_add(DA_WARN, "DIP_WARNING", C(DA_WARN)); dip_add(DA_INSULT, "DIP_INSULT", C(DA_INSULT));
		dip_add(DA_CLAIM, "DIP_CLAIM", C(DA_CLAIM)); dip_add(DA_GUARANTEE, "DIP_GUARANTEE", C(DA_GUARANTEE));
	} else {
		dip_add(DA_PEACE, "DIPPEACE", 0); dip_add(DA_WAR, "DIPWAR", 0); dip_add(-1, NULL, 0);
		dip_add(DA_BREAKDYN, "DIPBRKDYN", 0); dip_add(DA_MARRY, "DIPOFFDYN", 0); dip_add(DA_CANVAS, "DIPCANVAS", 0);
		dip_add(DA_BRKVAS, "DIPBRKVAS", 0); dip_add(DA_OFFVAS, "DIPOFFVAS", 0); dip_add(DA_ANNEX, "DIPANNEX", 0);
		dip_add(DA_ASKACC, "DIPAACCESS", 0); dip_add(DA_CANACC, "DIPCACCESS", 0); dip_add(DA_REVACC, "DIPRACCESS", 0); dip_add(-1, NULL, 0);
		dip_add(DA_CREATE, "DIPCNALL", 0); dip_add(DA_LEAVE, "DIPBRKALL", 0); dip_add(DA_BAN, "DIPBANALL", 0);
		dip_add(DA_INVITE, "DIPINVAL", 0); dip_add(DA_JOIN, "DIPJOINALL", 0); dip_add(-1, NULL, 0);
		dip_add(DA_ALLOWTRADE, "DIPATRADE", 0); dip_add(DA_EMBARGO, "DIPRTRADE", 0); dip_add(DA_CANTRADE, "DIPCANTRADE", 0);
		dip_add(DA_OFFTRADE, "DIPOFFTRADE", 0); dip_add(DA_EXCDIS, "DIPEXCDIS", 0);
	}
#undef C
}

static void members(char *out, int n, int ally, int skip)       /* "A, B and C" with the colour codes of DIAND */
{
	const Alliance *al = &g_world.ally[ally];
	int k, cnt = 0, tot = 0;
	out[0] = 0;
	for (k = 0; k < al->n; k++) if (al->member[k] != skip) tot++;
	for (k = 0; k < al->n; k++) {
		size_t l = strlen(out);
		if (al->member[k] == skip) continue;
		snprintf(out + l, (size_t)n - l, "%s%s", cnt == 0 ? "" : cnt == tot - 1 ? text_get("DIAND") : ", ", text_get(g_world.country[al->member[k]].tag));
		cnt++;
	}
}

/* the alliances view text of country c (0x509040: marriages, lord/vassal, military alliance, wars, access) */
static void dip_lines(int c)
{
	int i, k;
	char m[160];
	g_ndip_line = 0;
	for (i = 0; i < g_world.nally && g_ndip_line < 8; i++) {
		const Alliance *al = &g_world.ally[i];
		int in = 0;
		for (k = 0; k < al->n; k++) if (al->member[k] == c) in = 1;
		if (!in) continue;
		members(m, sizeof m, i, c);
		if (al->type == ALLY_DYNASTIC) snprintf(g_dip_line[g_ndip_line++], 200, text_get("DIRM"), m);
		else if (al->type == ALLY_MILITARY) snprintf(g_dip_line[g_ndip_line++], 200, "%s%s", text_get("DIMIL"), m);
		else if (al->type == ALLY_VASSAL) {
			if (al->member[0] == c) for (k = 1; k < al->n && g_ndip_line < 8; k++) snprintf(g_dip_line[g_ndip_line++], 200, text_get("DILORD"), text_get(g_world.country[al->member[k]].tag));
			else snprintf(g_dip_line[g_ndip_line++], 200, text_get("DIVASSAL"), text_get(g_world.country[al->member[0]].tag));
		}
	}
	for (i = 0; i < g_world.nwar && g_ndip_line < 8; i++) {
		const War *wr = &g_world.war[i];
		int side = 0;
		for (k = 0; k < wr->natt; k++) if (wr->att[k] == c) side = 1;
		for (k = 0; k < wr->ndef; k++) if (wr->def[k] == c) side = 2;
		if (!side) continue;
		{
			const short *mine = side == 1 ? wr->att : wr->def;
			int n = side == 1 ? wr->natt : wr->ndef, cnt = 0;
			m[0] = 0;
			for (k = 0; k < n; k++) if (mine[k] != c) { size_t l = strlen(m); snprintf(m + l, sizeof m - l, "%s%s", cnt++ ? ", " : "", text_get(g_world.country[mine[k]].tag)); }
			if (cnt) snprintf(g_dip_line[g_ndip_line++], 200, "%s%s", text_get("DIWAR"), m);
			else snprintf(g_dip_line[g_ndip_line++], 200, "%s", text_get("DIWARALONE"));
		}
	}
}

static void dip_open(MapView *mv, int target)
{
	g_infowin = UI_IW_DIPLO; g_dip_confirm = -1; g_dip_hover = -1;
	g_dip_target = target >= 0 && target != g_player && target < g_world.ncountry && g_world.country[target].nprov_owned > 0 ? target : -1;
	dip_build();
	g_mm.mode = g_dip_view ? MM_RELATIONS : MM_DIPLOMATIC;     /* mode 6 View Relations / 5 View Alliances (NIEPEWNE which radio) */
	g_mm.focus = g_dip_target >= 0 ? g_dip_target : -1;
	refresh_map(mv);
}

static void dip_close(MapView *mv)
{
	g_infowin = UI_IW_PROVINCE; g_dip_target = -1; g_dip_confirm = -1;
	g_mm.mode = MM_NORMAL; g_mm.focus = -1;
	refresh_map(mv);
}

/* the message of the confirm popup (0x50b880): war with its stability clauses, alliances, embargo */
static void dip_message(int act)
{
	const char *tn = text_get(g_world.country[g_dip_target].tag);
	char m[160];
	g_dip_msg[0] = 0;
	if (act == DA_WAR) {
		int s = 0, cb = (g_world.dipflag[g_player][g_dip_target] & DF_CB_PERM) != 0;
		long r = world_rel(&g_world, g_player, g_dip_target) / 1000;
		int dyn = world_allied(&g_world, g_player, g_dip_target, ALLY_DYNASTIC);
		int truce = g_peace && peace_truce(g_peace, g_player, g_dip_target, g_dayno);
		size_t l;
		if (!cb) s += 2;
		if (dyn) s += 1;
		if (truce) s += 5;
		if (r > 150) s += 2; else if (r > 100) s += 1;
		snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWDW1"), tn);
		if (s > 0) {
			l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, " "); l++;
			snprintf(g_dip_msg + l, sizeof g_dip_msg - l, text_get("AWDWLS"), s);
			if (!cb) { l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, "%s", text_get("AWDWCB")); }
			if (dyn) { l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, "%s", text_get("AWDWRM")); }
			if (truce) { l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, "%s", text_get("AWDWQUAT")); }
			if (r > 150) { l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, "%s", text_get("AWDWFRIE2")); }
			else if (r > 100) { l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, "%s", text_get("AWDWFRIE1")); }
			l = strlen(g_dip_msg); snprintf(g_dip_msg + l, sizeof g_dip_msg - l, ".");
		}
	} else if (act == DA_CREATE) snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWCNEW"), tn);
	else if (act == DA_INVITE && g_dip && diplo_milally(g_dip, g_player) >= 0) {
		members(m, sizeof m, diplo_milally(g_dip, g_player), g_player);
		snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWINV"), tn);
		strncat(g_dip_msg, m, sizeof g_dip_msg - strlen(g_dip_msg) - 1);
	} else if (act == DA_JOIN && g_dip && diplo_milally(g_dip, g_dip_target) >= 0) {
		members(m, sizeof m, diplo_milally(g_dip, g_dip_target), -1);
		snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWJOIN"), tn);
		strncat(g_dip_msg, " ", sizeof g_dip_msg - strlen(g_dip_msg) - 1);
		strncat(g_dip_msg, text_get("DIMIL"), sizeof g_dip_msg - strlen(g_dip_msg) - 1);
		strncat(g_dip_msg, m, sizeof g_dip_msg - strlen(g_dip_msg) - 1);
	} else if (act == DA_LEAVE && g_dip && diplo_milally(g_dip, g_player) >= 0) {
		members(m, sizeof m, diplo_milally(g_dip, g_player), g_player);
		snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWLVALL"), m);
	} else if (act == DA_BAN) snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWBANALL"), tn);
	else if (act == DA_EMBARGO) snprintf(g_dip_msg, sizeof g_dip_msg, text_get("AWRTRADE"), tn);
	else snprintf(g_dip_msg, sizeof g_dip_msg, "%s", tn);
}

static int dip_needs_confirm(int act)
{
	return act == DA_WAR || act == DA_INVITE || act == DA_CREATE || act == DA_JOIN || act == DA_BAN || act == DA_LEAVE ||
	       act == DA_EXCDIS || act == DA_EMBARGO;
}

static void dip_run(MapView *mv, int act)
{
	static const char *const ok[4] = { "OK", NULL, NULL, NULL };
	int r, t = g_dip_target;
	char date[64], text[160];
	if (t < 0 || !g_dip) return;
	if (act == DA_LOAN) { g_lo_target = t; g_lo_amount = 100; g_lo_rate = 10; g_lo_months = 36; g_infowin = UI_IW_LOANS; g_mm.mode = MM_NORMAL; g_mm.focus = -1; refresh_map(mv); return; }   /* window 0x13 */
	if (act == DA_PEACE) { g_infowin = UI_IW_PROVINCE; if (peace_open(t)) { g_mm.mode = MM_NORMAL; g_mm.focus = -1; refresh_map(mv); } else dip_build(); return; }   /* the peace window (0x10) */
	r = diplo_action(g_dip, g_player, t, act, g_dayno);
	logf_("diplomacy: action %d -> %s: %d", act, g_world.country[t].tag, r);
	{	/* the answer as the game's message types */
		static const struct { int act; const char *yes, *no; } ans[] = {
			{ DA_MARRY, "ROYMARACCEPT", "MOYMARREJECT" }, { DA_CREATE, "MILALLACCEPT", "MILALLREJECT" }, { DA_INVITE, "MILALLINVACCEPT", "MILALLINVREJECT" },
			{ DA_OFFVAS, "VASALLACCEPT", "VASALLREJECT" }, { DA_ANNEX, "ANNEXACCEPT", "ANNEXREJECT" }, { DA_OFFTRADE, "ACTRAWITHUS", "DETRAWITHUS" },
			{ DA_ASKACC, "ACCMILUS", "DECMILUS" } };
		int k;
		for (k = 0; k < (int)(sizeof ans / sizeof ans[0]); k++)
			if (ans[k].act == act && (r == DR_ACCEPTED || r == DR_REFUSED)) {
				msg_typed(r == DR_ACCEPTED ? ans[k].yes : ans[k].no, g_world.country[t].capital, text_get(g_world.country[t].tag), text_get(g_world.country[g_player].tag), NULL);
				r = DR_DONE;
			}
	}
	if (r == DR_ACCEPTED || r == DR_REFUSED) {                 /* other answers */
		fmt_date(date, sizeof date);
		snprintf(text, sizeof text, r == DR_ACCEPTED ? "%s has accepted our proposal." : "%s has declined our proposal.", text_get(g_world.country[t].tag));
		dialog(text_get(g_world.country[t].tag), date, text, ok);
	}
	dip_build();
	refresh_map(mv);
}

/* ------------------------------------------------------- messages to the player
 * text.csv message types: <TYPE>_HEADER (title), <TYPE>_LOG (the one-line text), BTN1 OK, BTN2 Goto.
 * Shown in the event box; Goto centres the map on a province. */
static long g_goto_x = -1, g_goto_y;
static int g_mw_dirty;                                      /* a window came or went: the loop redraws */
/* a new message window: <TYPE>_HEADER, _1.._6 with the message's strings, BTN1, and BTN2 for the kinds
 * with Goto (1, only with a province) or Details (2) (0x578d40); placed by the cascade */
static void msg_popup(const char *type, const char *a1, const char *a2, const char *a3, int goto_prov)
{
	UiMsgWin *m;
	char key[48];
	int b, kind = 0, i, any = 0;
	const char *cat = "mon";
	{	int bits = opts_msg_bits(&g_opts, type);            /* the message settings */
		if (!(bits & MSG_BOX)) return;
		if (bits & MSG_PAUSE) { if (!g_paused) g_mw_paused = 1; g_paused = 1; }   /* 0x427000 */
	}
	for (i = 0; i < NMSGKIND; i++) if (!strcmp(g_msgkind[i].type, type)) { kind = g_msgkind[i].kind; cat = g_msgkind[i].cat; break; }
	if (g_nmw == NMW) mw_close(0);                          /* ours: at most 24, the oldest goes */
	m = &g_mw[g_nmw];
	memset(m, 0, sizeof *m);
	snprintf(m->tag, sizeof m->tag, "%s", g_player >= 0 ? g_world.country[g_player].tag : "");
	snprintf(m->cat, sizeof m->cat, "%s", cat);
	for (b = 0; b < 7; b++) {
		const char *t;
		if (b == 0) snprintf(key, sizeof key, "%s_HEADER", type); else snprintf(key, sizeof key, "%s_%d", type, b);
		t = text_get(key);
		if (t == (const char *)key || !strcmp(t, key)) continue;   /* no such line */
		{	char f[200];
			expand_t(f, sizeof f, t);                            /* %t: the address (0x579150) */
			snprintf(m->box[b], sizeof m->box[b], f, b == 0 ? "" : a1 ? a1 : "", a2 ? a2 : "", a3 ? a3 : "");
		}
		if (!m->box[b][0]) continue;
		if (b) any = 1;
	}
	if (!any) {                                             /* no _1.._6: the log line */
		const char *t;
		snprintf(key, sizeof key, "%s_LOG", type); t = text_get(key);
		snprintf(m->box[1], sizeof m->box[1], t, a1 ? a1 : "", a2 ? a2 : "", a3 ? a3 : "");
	}
	snprintf(key, sizeof key, "%s_BTN1", type);
	snprintf(m->btn[0], sizeof m->btn[0], "%s", strcmp(text_get(key), key) ? text_get(key) : "OK");
	if ((kind == 1 && goto_prov > 0 && goto_prov < g_world.nprov) || kind == 2) {
		snprintf(key, sizeof key, "%s_BTN2", type);
		snprintf(m->btn[1], sizeof m->btn[1], "%s", strcmp(text_get(key), key) ? text_get(key) : kind == 1 ? "Goto" : "Details");
	}
	ui_msgwin_layout(m);
	ui_msgwin_place(m);
	g_mw_prov[g_nmw] = (short)goto_prov; g_mw_kind[g_nmw] = (short)kind;
	snprintf(g_mw_type[g_nmw], sizeof g_mw_type[0], "%s", type);
	g_nmw++;
	g_mw_dirty = 1;
	logf_("msgwin: %s kind %d at %d,%d (%d open)", type, kind, m->x, m->y, g_nmw);
}

/* a message type of text.csv: <TYPE>_1 as the headline, <TYPE>_LOG with up to three strings */
static void msg_typed(const char *type, int goto_prov, const char *a1, const char *a2, const char *a3)
{
	char k[48], l[200], t[260];
	snprintf(k, sizeof k, "%s_LOG", type);
	snprintf(l, sizeof l, text_get(k), a1 ? a1 : "", a2 ? a2 : "", a3 ? a3 : "");
	snprintf(k, sizeof k, "%s_1", type);
	{	/* text_get hands the key back when a type has no headline (REVOLT) */
		const char *one = text_get(k);
		char h1[96];
		if (one != (const char *)k && strchr(one, '%')) { snprintf(h1, sizeof h1, one, a1 ? a1 : ""); one = h1; }   /* PEACEACCEPT_1 = "%s" */
		if (one == (const char *)k) snprintf(t, sizeof t, "%s", l);
		else snprintf(t, sizeof t, "%s\\n%s", one, l);
	}
	logf_("message: %s", l);
	if (opts_msg_bits(&g_opts, type) & MSG_LOG) ledger_log(&g_world, l);
	(void)t;
	msg_popup(type, a1, a2, a3, goto_prov);
}

/* a proposal of the AI (diplo_set_ask): <TYPE>_1 headline, <TYPE>_LOG with the countries, <TYPE>_6 the
 * question, BTN1 accept / BTN2 decline (ROYMARPROP, MILALLPROP, MILALLINVPROP, MILALLJOINPROP, WPINVPROP) */
static int diplo_ask(void *u, const char *type, int from, int against, int third)
{
	char k[48], l[220], t[360], h1[96], date[64];
	const char *lab[4] = { NULL, NULL, NULL, NULL }, *one, *six;
	const char *a1 = from >= 0 ? text_get(g_world.country[from].tag) : "?";
	const char *a2 = against >= 0 ? text_get(g_world.country[against].tag) : "?";
	const char *a3 = third >= 0 ? text_get(g_world.country[third].tag) : "?";
	(void)u;
	snprintf(k, sizeof k, "%s_LOG", type); snprintf(l, sizeof l, text_get(k), a1, a2, a3);
	snprintf(k, sizeof k, "%s_1", type); one = text_get(k);
	if (strchr(one, '%')) { snprintf(h1, sizeof h1, one, a1); one = h1; }
	snprintf(k, sizeof k, "%s_6", type); six = text_get(k);
	if (six == (const char *)k) six = "";
	snprintf(t, sizeof t, "%s\\n%s\\n%s", one, l, six);
	logf_("proposal: %s", l);
	if (opts_msg_bits(&g_opts, type) & MSG_LOG) ledger_log(&g_world, l);
	snprintf(k, sizeof k, "%s_BTN1", type); lab[0] = text_get(k);
	snprintf(k, sizeof k, "%s_BTN2", type); lab[1] = text_get(k);
	fmt_date(date, sizeof date);
	snprintf(k, sizeof k, "%s_HEADER", type);
	sound_play("diplomat_alert");                         /* an answer popup, first shown (0x520a17) */
	return dialog(text_get(k), date, t, lab) == 0;
}

/* after a day: what changed for the player, as the game's message types (ours: the exe raises them
 * where the change happens; here the day's result is compared with yesterday's). Not saved: a new
 * or loaded game takes a fresh snapshot (g_smsg_primed = 0). */
static int g_smsg_primed;
static int g_new_game;                     /* sim_start without a save: the leaders' arrival at the start is still due */
static void check_state_messages(void)
{
	static int tech[4];
	static const char *mon;
	static unsigned char bfl[MAX_PROV], manu[MAX_PROV], ldead[MAX_LAND], nlost[MAX_NAVAL];
	static unsigned char lseen[512];                    /* leaders announced (bit per leaders.c id) */
	static short larmy[MAX_LAND];
	static const char *const bname[5] = { "Shipyard", "Conscription Centre", "Tax Collector", "Governor", "Chief Judge" };
	static const char *const mname[5] = { "Fine Arts Academy", "Weapons Manufactory", "Naval Equipment Manufactory", "Refinery", "Goods Manufactory" };
	static const char *const tk[4] = { "LTINCREASE", "NTINCREASE", "TRADETINCREASE", "INFRATINCREASE" };
	const Country *c;
	static long prime_day;
	int i, b, t[4], prime = !g_smsg_primed, quiet;
	char v[16];
	if (g_player < 0) return;
	if (prime) prime_day = g_dayno;
	quiet = g_dayno - prime_day < 35;                   /* the generals of the first monthly assignment were there from the start */
	c = &g_world.country[g_player];
	t[0] = c->land_tech; t[1] = c->naval_tech; t[2] = c->trade_tech; t[3] = c->infra_tech;
	for (i = 0; i < 4; i++) {
		if (!prime && t[i] > tech[i]) { snprintf(v, sizeof v, "%d", t[i]); msg_typed(tk[i], -1, v, "", NULL); }
		tech[i] = t[i];
	}
	if (!prime && c->stability != g_stab_seen) msg_typed(c->stability > g_stab_seen ? "STABINCREASE" : "STABDROP", -1, NULL, NULL, NULL);
	g_stab_seen = c->stability;
	if (!prime && c->m_name && c->m_name != mon) { msg_typed("NEWMON", -1, c->m_name, NULL, NULL); sound_play("game_monarch"); }   /* ACTIVATE_MONARCH 0x52f5bd */
	mon = c->m_name;
	for (i = 1; i < g_world.nprov && i < MAX_PROV; i++) {   /* improvements finished */
		const Province *p = &g_world.prov[i];
		if (!prime && p->owner == g_player) {
			for (b = 0; b < 5; b++) if ((p->bflags & (1 << b)) && !(bfl[i] & (1 << b))) { msg_typed("MANUFINISH", i, bname[b], p->name ? p->name : "?", NULL); sound_play("improvement_done"); }   /* NIEPEWNE which of 0x3B / 0x3C: same file */
			if (p->manu && !manu[i]) { msg_typed("MANUFINISH", i, mname[p->manu >= 1 && p->manu <= 5 ? p->manu - 1 : 0], p->name ? p->name : "?", NULL); sound_play("manufactory_success"); }   /* BUILD_FACILITY 0x52ead2 */
		}
		bfl[i] = p->bflags; manu[i] = p->manu;
	}
	for (i = 0; i < g_world.nland && i < MAX_LAND; i++) {   /* armies wiped out; generals */
		const LandUnit *u = &g_world.land[i];
		int dead = u->dead == 1;
		if (!prime && u->owner == g_player && dead && !ldead[i] && u->location > 0 && u->location < g_world.nprov)
			msg_typed("ARMYDESTROY", u->location, g_world.prov[u->location].name ? g_world.prov[u->location].name : "?", NULL, NULL);
		ldead[i] = (unsigned char)dead;
		if (u->owner == g_player && !dead && u->l_id > 0 && u->l_id <= 4096 && u->l_id != larmy[i]) {
			int id = u->l_id - 1;
			if (!(lseen[id >> 3] & (1 << (id & 7)))) {
				const Leader *ld = g_lead ? lead_get(g_lead, id) : NULL;
				lseen[id >> 3] |= (unsigned char)(1 << (id & 7));
				if (!prime && !quiet && ld && u->location > 0 && u->location < g_world.nprov)
				{	msg_typed("LEADERNEW", u->location, ld->name, g_world.prov[u->location].name ? g_world.prov[u->location].name : "?", NULL); sound_play("historical_leader"); }   /* ACTIVATE_LEADER 0x52f664 */
			}
		}
		larmy[i] = u->l_id;
	}
	for (i = 0; i < 4096 && g_lead; i++)                 /* announced generals who reached their death date */
		if (lseen[i >> 3] & (1 << (i & 7))) {
			const Leader *ld = lead_get(g_lead, i);
			if (ld && ld->death.year > 0 && (g_world.now.year > ld->death.year || (g_world.now.year == ld->death.year && g_world.now.month >= ld->death.month))) {
				lseen[i >> 3] &= (unsigned char)~(1 << (i & 7));
				if (!prime) msg_typed("LEADERDIES", -1, ld->name, NULL, NULL);
			}
		}
	for (i = 0; i < g_world.nnaval && i < MAX_NAVAL; i++) {
		const NavalUnit *f = &g_world.naval[i];
		int lost = f->warships + f->galleys + f->transports <= 0;
		if (!prime && f->owner == g_player && lost && !nlost[i] && f->location > 0 && f->location < g_world.nprov)
			msg_typed("NAVYDESTROY", f->location, g_world.prov[f->location].name ? g_world.prov[f->location].name : "?", NULL, NULL);
		nlost[i] = (unsigned char)lost;
	}
	if (prime) memset(larmy, 0, sizeof larmy);
	g_smsg_primed = 1;
}

/* after a day: wars between others (DECLWAROTHER, log only by default): a war is new when its leaders and
 * start day were not there yesterday */
static void check_other_wars(void)
{
	static long seen[64], cur[64];
	static int nseen = -1;
	int i, k, n = 0;
	for (i = 0; i < g_world.nwar && n < 64; i++) {
		const War *wr = &g_world.war[i];
		long sig = ((long)wr->att[0] * 512 + wr->def[0]) * 4096 + (wr->start_day & 4095);
		cur[n++] = sig;
		if (nseen < 0 || !g_smsg_primed || g_player < 0 || peace_side(&g_world, i, g_player)) continue;
		for (k = 0; k < nseen && seen[k] != sig; k++) ;
		if (k == nseen) msg_typed("DECLWAROTHER", g_world.country[wr->def[0]].capital, text_get(g_world.country[wr->att[0]].tag), text_get(g_world.country[wr->def[0]].tag), NULL);
	}
	memcpy(seen, cur, sizeof cur[0] * (size_t)n); nseen = n;
}

/* after a day: wars that someone started against us (DECLWAR) */
static void check_messages(void)
{
	static unsigned char enemy[MAX_COUNTRY];
	static int primed;
	int i, k;
	if (g_player < 0) return;
	for (i = 0; i < g_world.ncountry; i++) {
		int at = i != g_player && mil_at_war(&g_world, g_player, i);
		if (at && !enemy[i] && primed) {
			int leader = -1;                               /* who declared: the attackers' leader of the war we defend in */
			for (k = 0; k < g_world.nwar && leader < 0; k++) {
				const War *wr = &g_world.war[k];
				int j, def = 0, att = 0;
				for (j = 0; j < wr->ndef; j++) if (wr->def[j] == g_player) def = 1;
				for (j = 0; j < wr->natt; j++) if (wr->att[j] == i) att = 1;
				if (def && att) leader = wr->att[0];
			}
			if (leader == i) {
				char t[200], l[160];
				snprintf(l, sizeof l, text_get("DECLWAR_LOG"), text_get(g_world.country[i].tag));
				snprintf(t, sizeof t, "%s\\n%s", text_get("DECLWAR_1"), l);
				logf_("message: %s", l);
				if (opts_msg_bits(&g_opts, "DECLWAR") & MSG_LOG) ledger_log(&g_world, l);
				(void)t;
				msg_popup("DECLWAR", text_get(g_world.country[i].tag), NULL, NULL, g_world.country[i].capital);
			}
		}
		enemy[i] = (unsigned char)at;
	}
	primed = 1;
	for (i = 0; i < g_world.nnote; i++) {             /* the modules' notifications for us */
		const Note *n = &g_world.note[i];
		const char *pn = n->prov > 0 && n->prov < g_world.nprov && g_world.prov[n->prov].name ? g_world.prov[n->prov].name : "?";
		if (n->country != g_player) continue;
		if (n->type == NOTE_MERCLOSS) {
			char left[8];
			int cot = -1, j;
			for (j = 0; j < g_world.ncot; j++) if (g_world.cot[j].prov == n->prov) cot = j;
			snprintf(left, sizeof left, "%d", cot >= 0 ? env_merchant_count(&g_world, g_player, cot) : 0);
			msg_typed("MERCLOSS", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", left);
		} else if (n->type == NOTE_MISSION_OK) msg_typed("MISSOSUCC", n->prov, pn, NULL, NULL);
		else if (n->type == NOTE_MISSION_FAIL) msg_typed("MISSOFAIL", n->prov, pn, NULL, NULL);
		else if (n->type == NOTE_REVOLT) msg_typed("REVOLT", n->prov, pn, NULL, NULL);
		else if (n->type == NOTE_LOAN_REPAID || n->type == NOTE_LOAN_DEFAULT) {
			char amt[16];
			snprintf(amt, sizeof amt, "%d", n->prov);
			msg_typed(n->type == NOTE_LOAN_REPAID ? "LOANSREPAY" : "LOANSNOREPAY", -1, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", amt, NULL);
		}
		else if (n->type == NOTE_NAVAL_WIN || n->type == NOTE_NAVAL_LOSS)
			msg_typed(n->type == NOTE_NAVAL_WIN ? "NBWIN" : "NBLOSS", n->prov, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", pn, NULL);
		else if (n->type == NOTE_ARMY_ARRIVE && n->other >= 0 && n->other < g_world.nland)
			msg_typed("ARMYARRIVE", n->prov, g_world.land[n->other].name ? g_world.land[n->other].name : "?", pn, NULL);
		else if (n->type == NOTE_FLEET_ARRIVE && n->other >= 0 && n->other < g_world.nnaval)
			msg_typed("FLEETARRIVE", n->prov, g_world.naval[n->other].name ? g_world.naval[n->other].name : "?", pn, NULL);
		else if (n->type == NOTE_LIBERATED) msg_typed("LIBPROV", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if (n->type == NOTE_AUTOLOAN) msg_typed("AUTOLOAN", -1, NULL, NULL, NULL);
		else if (n->type == NOTE_BANKRUPT) msg_typed("BANKRUPT", -1, NULL, NULL, NULL);
		else if (n->type == NOTE_BROKEN) msg_typed("BROKENUS", -1, NULL, NULL, NULL);
		else if (n->type == NOTE_PIRATES) msg_typed("PIRATES", n->prov, pn, NULL, NULL);   /* 0x87, from the fleet creator */
		else if (n->type == NOTE_NATIVES || n->type == NOTE_NATIVESKILL) msg_typed(n->type == NOTE_NATIVES ? "NATIVES" : "NATIVESKILL", n->prov, pn, NULL, NULL);   /* 0x88 / 0x89 */
		else if (n->type == NOTE_LOANDUE && g_ec) {        /* a bank loan is due: pay or extend (0x522de0) */
			const char *lab[4] = { NULL, NULL, NULL, NULL };
			char t[200], date[64];
			int r;
			lab[0] = text_get("BPAYLOAN"); lab[1] = text_get("BEXTENDLOAN");
			snprintf(t, sizeof t, "%s\n%d$", text_get("EXTENDLOAN_ROLL"), n->other);
			fmt_date(date, sizeof date);
			r = econ_loan_answer(g_ec, g_player, n->prov, dialog(text_get("EXTENDLOAN_ROLL"), date, t, lab) == 0);
			logf_("loan due: %s", r == 1 ? "paid" : r == 0 ? "extended" : "-");
		}
		else if (n->type == NOTE_ENVOY && n->other >= 0 && n->other <= EA_TPFAIL) {   /* an envoy arrived */
			static const char *const ek[8] = { "MERCSUCC", "MERCFAIL", "COLNEWSUCCESS", "COLEXPANDSUCCESS", "COLFAIL", "TPNEWSUCCESS", "TPEXPANDSUCCESS", "TPFAILEST" };
			msg_typed(ek[n->other], n->prov, pn, NULL, NULL);
			if (n->other <= EA_MERCFAIL) tut_merchant_arrived(g_tut, n->prov);   /* CheckMerchant (0x568550) */
			else refresh_map(g_mv);
		}
		else if (n->type == NOTE_DEFECT_US) msg_typed("DEFECTU", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if (n->type == NOTE_DEFECT_THEM) msg_typed("DEFECTTU", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if (n->type == NOTE_EXPLORESPEC) {           /* 0xb0 EXPLORESPEC: the special's name and its VP */
			int vp; char v[12];
			const char *key = world_special(n->other, &vp);
			snprintf(v, sizeof v, "%d", vp);
			msg_typed("EXPLORESPEC", n->prov, text_get(key), v, NULL);
		}
		else if (n->type == NOTE_DIPMSG)
			msg_typed(diplo_msg_type(n->prov), n->country >= 0 ? g_world.country[n->country].capital : -1, n->country >= 0 ? text_get(g_world.country[n->country].tag) : "?",
			          n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if (n->type == NOTE_WHITEPEACE) msg_typed("WHITEPEACEUS", -1, text_get(g_world.country[n->country].tag), n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if ((n->type == NOTE_MISSION_DONE || n->type == NOTE_MISSION_LOST) && g_mis) {   /* MISSUCC / MISSFAIL: the row is in prov */
			char title[96], vp[12];
			mis_title(g_mis, &g_world, n->prov, title, sizeof title, 0);
			snprintf(vp, sizeof vp, "%d", n->other);
			msg_typed(n->type == NOTE_MISSION_DONE ? "MISSUCC" : "MISSFAIL", 0, title, vp, NULL);
		}
		else if (n->type == NOTE_BATTLE_WIN || n->type == NOTE_BATTLE_LOSS)          /* "We won a battle against %s in %s." */
			msg_typed(n->type == NOTE_BATTLE_WIN ? "LBWIN" : "LBLOSS", n->prov, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", pn, NULL);
		else if (n->type == NOTE_SIEGE_WIN) msg_typed("SBWIN", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
		else if (n->type == NOTE_PROV_LOST) msg_typed("PROVOWNOCC", n->prov, pn, n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?", NULL);
	}
	g_world.nnote = 0;
	for (i = 0; i < g_world.nnews; i++) {             /* other countries' news (SPEC_SMALLRULES 5.1): only when we know their capital */
		const Note *n = &g_world.news[i];
		const char *pn = n->prov > 0 && n->prov < g_world.nprov && g_world.prov[n->prov].name ? g_world.prov[n->prov].name : "?";
		int cap = n->country >= 0 ? g_world.country[n->country].capital : -1;
		int knows = cap > 0 && cap < g_world.nprov && WORLD_KNOWN(&g_world, g_player, cap);
		const char *ct = n->country >= 0 ? text_get(g_world.country[n->country].tag) : "?", *ot = n->other >= 0 ? text_get(g_world.country[n->other].tag) : "?";
		if (n->type == NOTE_BATTLE_WIN && knows && n->other != g_player) msg_typed("LBWINOTHER", n->prov, ct, ot, pn);
		else if (n->type == NOTE_SIEGE_WIN && knows && n->other != g_player) msg_typed("SBWINOTHER", n->prov, ct, pn, ot);
		else if (n->type == NOTE_BANKRUPT && knows) msg_typed("BANKRUPTOTHER", cap, ct, NULL, NULL);
		else if (n->type == NOTE_BROKEN && knows) msg_typed("BROKENOTHER", cap, ct, NULL, NULL);
		else if (n->type == NOTE_EMPEROR && (knows || n->country == g_player))   /* 0x60 EMPEROR: the monarch of the country */
			msg_typed("EMPEROR", cap, g_world.country[n->country].m_name ? g_world.country[n->country].m_name : "?", ct, NULL);
		else if (n->type == NOTE_OTHER && knows && n->country != g_player && n->prov >= 0 && n->prov < OM_N) {   /* the acting country is not us (SPEC_SMALLRULES 5.1) */
			static const char *const ok[OM_N] = { "ROYMARACCEPTOTHER", "ANNEXACCEPTOTHER", "OTHERTRADEAG", "BRKTRADEOTH", "ACCMILOTHER", "CANMILOTHE", "CLAIMTOOTH", "OTHERDEFENDER",
			                                      "NEWMONOTHER", "MONOPOLYOTHER", "PEACEACCEPTOTHER" };
			const char *a2 = n->prov == OM_DEFENDER ? text_get(rel_label(-1, n->other)) : ot;
			if (n->prov == OM_NEWMON) msg_typed(ok[n->prov], cap, g_world.country[n->country].m_name ? g_world.country[n->country].m_name : "?", ct, NULL);   /* monarch, country */
			else if (n->prov == OM_PEACE) { if (n->other != g_player) msg_typed(ok[n->prov], cap, ct, ot, ""); }   /* not our own peace */
			else if (n->prov == OM_MONOPOLY) msg_typed(ok[n->prov], n->other, ct, n->other > 0 && n->other < g_world.nprov && g_world.prov[n->other].name ? g_world.prov[n->other].name : "?", NULL);
			else msg_typed(ok[n->prov], cap, ct, a2, NULL);
		}
		else if (n->type == NOTE_NEWNATION && n->prov > 0 && n->prov < g_world.nprov && WORLD_KNOWN(&g_world, g_player, n->prov))   /* 0x5f NEWNATION */
			msg_typed("NEWNATION", n->prov, ct, ot, NULL);
		else if (n->type == NOTE_DEFECT && n->prov > 0 && n->prov < g_world.nprov && WORLD_KNOWN(&g_world, g_player, n->prov))      /* DEFECTO */
			msg_typed("DEFECTO", n->prov, pn, ct, NULL);
		else if (n->type == NOTE_RELIGION && knows) msg_typed("OTHERRELIGION", cap, ct, text_get(rel_label(-1, n->other)), NULL);
		else if (n->type == NOTE_JOINWAR && knows && n->prov >= 0)   /* JOINWAROTHER: joiner, ally, enemy (the player too) */
			msg_typed("JOINWAROTHER", cap, ct, text_get(g_world.country[n->prov].tag), ot);
		else if (n->type == NOTE_REVOLT && n->prov > 0 && n->prov < g_world.nprov && WORLD_KNOWN(&g_world, g_player, n->prov)
		         && g_world.prov[n->prov].controller != g_player) msg_typed("REVOLTOTHER", n->prov, pn, ct, NULL);
	}
	g_world.nnews = 0;
}

/* ------------------------------------------------------- the Ledger (F6, BUTTON_VIEW_STATISTICS) */
static UiLedger g_led;
static int g_led_page, g_led_chpage[7] = { 0, 4, 16, 20, 22, 29, 34 };

static void ledger_bg(void)                          /* a tutorial message over the open Ledger (chapter 9) */
{
	ui_ledger_draw(amigagfx_chunky(), amigagfx_pitch(), &g_world, &g_led);
	if (g_tut) tut_overlay();
}

static void ledger_window(void)
{
	int done = 0, dirty = 1, gw = amigagfx_game_width(), gh = amigagfx_game_height();
	g_dlg_bg = ledger_bg;
	while (!done) {
		AmigaGfxEvent e;
		if (dirty) {
			int k;
			ledger_set_sources((const struct Monarchs *)g_mon, (const struct Leaders *)g_lead); ledger_set_missions((const struct Missions *)g_mis);
			ledger_fill(&g_led, g_led_page, &g_world, g_ec, g_mil, &g_hist, g_player);
			if (g_led_page == 3 && g_mon && g_player >= 0) {       /* past and present monarchs (the original keeps the past ones in the save) */
				const Monarch *ml[LG_MAXROW];
				int n = mon_list(g_mon, g_player, ml, LG_MAXROW), j, r = 0;
				static const char *rk[6] = { "EYRVPOOR", "EYRPOOR", "EYRAVERAGE", "EYRGOOD", "EYRVGOOD", "EYREXCELLENT" };
				for (j = 0; j < n && r < LG_MAXROW; j++) {
					const int sk[3] = { ml[j]->adm, ml[j]->mil, ml[j]->dip };
					int q;
					if (ml[j]->dormant || ml[j]->start.year > g_world.now.year || (j + 1 < n && ml[j + 1]->start.year < g_world.start.year)) continue;   /* only from the scenario start to today; not the dormant pretenders */
					snprintf(g_led.cell[r][0], 28, "%s", ml[j]->name);
					for (q = 0; q < 3; q++) { int t = sk[q] <= 2 ? 0 : sk[q] <= 4 ? 1 : sk[q] == 5 ? 2 : sk[q] == 6 ? 3 : sk[q] <= 8 ? 4 : 5;   /* NIEPEWNE steps (0x4f1df0) */
					                          snprintf(g_led.cell[r][1 + q], 28, "%s", text_get(rk[t])); }
					snprintf(g_led.cell[r][4], 28, "%d", ml[j]->start.year);
					if (j + 1 < n && ml[j + 1]->start.year <= g_world.now.year) snprintf(g_led.cell[r][5], 28, "%d", ml[j + 1]->start.year);
					else snprintf(g_led.cell[r][5], 28, "-");
					snprintf(g_led.cell[r][6], 28, "-");
					r++;
				}
				g_led.nrow = r;
			}
			for (k = 0; k < g_led.nser && !g_led.ser_cat; k++) { int sc = mv_country_scale(g_mv, g_led.ser_country[k]); g_led.ser_pen[k] = (short)(MV_PEN_SCALES + (sc < 0 ? 0 : sc) * 8 + 3); }
			ui_ledger_draw(amigagfx_chunky(), amigagfx_pitch(), &g_world, &g_led);
			if (g_tut) tut_overlay();
			scr_blit(0, 0, gw, gh);
			dirty = 0;
		}
		if (g_tut) {                                         /* the tutorial goes on over the Ledger (chapter 9: its messages and arrows; after the
		                                                        first fill, so a message is drawn over this page) */
			tut_tick(g_tut);
			if (tut_changed(g_tut) || g_tut_redraw) { g_tut_redraw = 0; dirty = 1; }
			if (g_tut_end) break;
		}
		while (!done && !dirty && poll_ev(&e)) {               /* a page change is drawn before the next event */
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && (e.code == 0x45 || e.code == 0x55))) done = 1;   /* Esc / F6 */
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_ledger_hit(e.x, e.y), ch = ledger_chapter(g_led_page);
				if (h == LH_EXIT) done = 1;
				else if (h == LH_BACK && g_led_page > ledger_first_page(ch)) { g_led_page--; dirty = 1; }
				else if (h == LH_FWD && g_led_page < ledger_last_page(ch)) { g_led_page++; dirty = 1; }
				else if (h >= LH_CHAPTER && h < LH_CHAPTER + 7) { g_led_page = g_led_chpage[h - LH_CHAPTER]; dirty = 1; }
				if (dirty) { g_led_chpage[ledger_chapter(g_led_page)] = g_led_page; logf_("ledger: page %d", g_led_page + 1); }
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) done = 1;
		if (!done && !dirty) eu2_sleep_ms(20);
	}
	g_dlg_bg = NULL;
}

/* ------------------------------------------------------- new game / load game */
#define QUICKSAVE "PROGDIR:save/quick.eug"

/* world + every simulation module, from the scenario (save == NULL) or from a save game */
/* the events' unit commands: new armies / fleets in P (the conquistador's 500 men are infantry when the
 * capital is in America, else cavalry; event leaders are random: rank 25, maneuver 3, fire / shock 2 -
 * the policy-based values of 1.5 are NIEPEWNE) */
static void ev_units(void *user, int c, int p, int kind, long value)
{
	int k = -1, q = p, i;
	(void)user;
	if (!g_mil || c < 0 || p <= 0 || p >= g_world.nprov) return;
	if (kind >= 3 && kind != 6) {                           /* ships: P's port, else any port of ours */
		if (!mil_is_port(&g_world, q) || g_world.prov[q].controller != c)
			for (q = -1, i = 1; q < 0 && i < g_world.nprov; i++) if (g_world.prov[i].owner == c && g_world.prov[i].controller == c && mil_is_port(&g_world, i)) q = i;
		if (q < 0) return;
		k = mil_new_fleet(g_mil, c, q, 0, kind == 7 ? "Explorer" : NULL);
		if (k < 0) return;
		if (kind == 3 || kind == 7) g_world.naval[k].warships = (short)(kind == 7 ? 1 : value);
		else if (kind == 4) g_world.naval[k].galleys = (short)value;
		else g_world.naval[k].transports = (short)value;
		if (kind == 7) { g_world.naval[k].l_id = LEAD_RANDOM; g_world.naval[k].l_type = LT_EXPLORER; g_world.naval[k].l_move = 3; }
		return;
	}
	if (kind == 6) {
		int cap = g_world.country[c].capital, am = cap > 0 && !strcmp(world_name(&g_world, g_world.prov[cap].continent), "America");
		if ((k = mil_new_army(g_mil, c, p, am ? 500 : 0, "Conquistador")) < 0) return;
		if (!am) g_world.land[k].cavalry = 500;
		g_world.land[k].l_id = LEAD_RANDOM; g_world.land[k].l_type = LT_CONQ; g_world.land[k].l_rank = 25; g_world.land[k].l_move = 3;
		g_world.land[k].l_fire = 2; g_world.land[k].l_shock = 2;
		return;
	}
	if ((k = mil_new_army(g_mil, c, p, kind == 0 ? value : 0, NULL)) < 0) return;
	if (kind == 1) g_world.land[k].cavalry = value;
	if (kind == 2) g_world.land[k].artillery = value;
}

/* the message settings (SPEC_UI 9: per type popup box / log line / pause), kept in eu2opts.txt */
static void msgset_window(void)
{
	static char tl[80][48];
	const char *titles[80];
	int n = opts_msg_count(), i, first = 0, hover = -1, dirty = 1, done = 0;
	if (n > 80) n = 80;
	for (i = 0; i < n; i++) {                    /* the start of the type's log text (<TYPE>_LOG), else its headline (_1) */
		char k[48];
		const char *t;
		char *pc;
		snprintf(k, sizeof k, "%s_LOG", opts_msg_type(i)); t = text_get(k);
		snprintf(tl[i], sizeof tl[i], "%s", t == (const char *)k ? "" : t);
		if ((pc = strchr(tl[i], '%')) != NULL) *pc = 0;
		if (strlen(tl[i]) < 8) {
			snprintf(k, sizeof k, "%s_1", opts_msg_type(i)); t = text_get(k);
			snprintf(tl[i], sizeof tl[i], "%s", t == (const char *)k || strchr(t, '%') ? opts_msg_type(i) : t);
		}
		titles[i] = tl[i];
	}
	while (!done) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "msgset");
			ui_msgset_draw(amigagfx_chunky(), amigagfx_pitch(), titles, g_opts.msg, n, first, hover);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) done = 1;
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_msgset_hit(e.x, e.y, n, first); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_msgset_hit(e.x, e.y, n, first);
				if (h == 1000) done = 1;
				else if (h == 1001 && first > 0) first -= UI_MSGSET_ROWS;
				else if (h == 1002 && first + UI_MSGSET_ROWS < n) first += UI_MSGSET_ROWS;
				else if (h >= 0 && h < 1000 && h % 4) g_opts.msg[h / 4] ^= (unsigned char)(1 << (h % 4 - 1));
				if (h >= 0) { dirty = 1; }
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) done = 1;
		if (!done) eu2_sleep_ms(20);
	}
	if (opts_save(&g_opts, OPTSFILE)) logf_("msgset: cannot write %s", OPTSFILE);
	logf_("msgset: saved");
}

/* the right-click menu: a modal list at the mouse; the row picked or -1 */
static int popmenu(int x, int y, const char *const *lab, const unsigned char *fl, int n)
{
	int hover = -1, dirty = 1, pick = -2;
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "popmenu");
			ui_popmenu_draw(amigagfx_chunky(), amigagfx_pitch(), x, y, lab, fl, n, hover);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) pick = -1;
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_popmenu_hit(e.x, e.y, n); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN) {
				if (e.code != AMIGAGFX_BUTTON_LEFT || !ui_popmenu_inside(e.x, e.y)) pick = -1;
				else pick = ui_popmenu_hit(e.x, e.y, n) >= 0 ? ui_popmenu_hit(e.x, e.y, n) : -2;   /* greyed / a separator: nothing (0x523e37) */
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = -1;
		if (pick == -2) eu2_sleep_ms(20);
	}
	
	logf_("popmenu: %d", pick);
	return pick;
}

/* Message properties of one type (0x575520 / 0x576840): off / log / popup + log / popup + log + pause */
static void msgprop_window(int x, int y, const char *type)
{
	static const unsigned char val[4] = { 0, MSG_LOG, MSG_BOX | MSG_LOG, MSG_BOX | MSG_LOG | MSG_PAUSE };
	int k = opts_msg_index(type), cur, hover = -1, dirty = 1, done = 0, i;
	char title[64];
	if (k < 0) { logf_("msgset: %s has no setting", type); return; }
	for (cur = 0, i = 0; i < 4; i++) if (g_opts.msg[k] == val[i]) cur = i;
	snprintf(title, sizeof title, "%s", type);
	while (!done) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "msgset");
			ui_msgprop_draw(amigagfx_chunky(), amigagfx_pitch(), x, y, title, cur, hover);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && (e.code == 0x45 || e.code == 0x44 || e.code == 0x43))) done = 1;
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_msgprop_hit(e.x, e.y); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_msgprop_hit(e.x, e.y);
				if (h >= 0 && h < 4) { cur = h; dirty = 1; }
				else if (h == 4 || h < 0) done = 1;
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) done = 1;
		if (!done) eu2_sleep_ms(20);
	}
	g_opts.msg[k] = val[cur];
	if (opts_save(&g_opts, OPTSFILE)) logf_("msgset: cannot write %s", OPTSFILE);
	logf_("msgset: %s = %d", type, g_opts.msg[k]);
}

/* the message windows take the event first (the window manager hands it newest to oldest, 0x40f210);
 * 1 = taken. *moved: something to redraw */
static int mw_event(const AmigaGfxEvent *e, int *moved, int *bar)
{
	int i, h = -1;
	if (!g_nmw) return 0;
	if (e->type == AMIGAGFX_EV_MOUSEMOVE && g_mw_drag >= 0) {
		UiMsgWin *m = &g_mw[g_mw_drag];
		int gw = amigagfx_game_width(), gh = amigagfx_game_height();
		m->x = e->x - g_mw_gx; m->y = e->y - g_mw_gy;
		if (m->x > gw - m->w - 1) m->x = gw - m->w - 1;
		if (m->y > gh - m->h - 1) m->y = gh - m->h - 1;
		if (m->x < 0) m->x = 0;
		if (m->y < 0) m->y = 0;
		*moved = 1;
		return 1;
	}
	if (e->type == AMIGAGFX_EV_MOUSEUP && e->code == AMIGAGFX_BUTTON_LEFT && g_mw_drag >= 0) { g_mw_drag = -1; return 1; }
	if (e->type == AMIGAGFX_EV_MOUSEMOVE || e->type == AMIGAGFX_EV_MOUSEDOWN) {
		for (i = g_nmw - 1; i >= 0; i--) if ((h = ui_msgwin_hit(&g_mw[i], e->x, e->y)) >= 0) break;
		if (e->type == AMIGAGFX_EV_MOUSEMOVE) {
			int hb = i >= 0 && h < 2 ? h : -1, hw = hb >= 0 ? i : -1;
			if (hw != g_mw_hw || hb != g_mw_hb) { g_mw_hw = hw; g_mw_hb = hb; *moved = 1; }
			return i >= 0;
		}
		if (i < 0) return 0;
		if (e->code == AMIGAGFX_BUTTON_RIGHT) { char t[24]; memcpy(t, g_mw_type[i], sizeof t); msgprop_window(e->x, e->y, t); *moved = 1; return 1; }
		if (e->code != AMIGAGFX_BUTTON_LEFT) return 1;
		if (h == 2) { mw_front(i); g_mw_drag = g_nmw - 1; g_mw_gx = e->x - g_mw[g_mw_drag].x; g_mw_gy = e->y - g_mw[g_mw_drag].y; *moved = 1; return 1; }
	} else if (e->type == AMIGAGFX_EV_KEY && (e->code == 0x44 || e->code == 0x43)) { i = g_nmw - 1; h = 0; }      /* Enter: BTN1 of the front one */
	else if (e->type == AMIGAGFX_EV_KEY && e->code == 0x40 && g_mw[g_nmw - 1].btn[1][0]) { i = g_nmw - 1; h = 1; }   /* Space: BTN2 */
	else return 0;
	if (h == 0) { logf_("msgwin: %s closed", g_mw_type[i]); mw_close(i); *moved = 1; return 1; }
	if (h == 1) {                                         /* Goto / Details: the window stays */
		int p = g_mw_prov[i];
		if (g_mw_kind[i] == 1 && p > 0 && p < g_world.nprov) { g_goto_x = g_world.prov[p].army_x; g_goto_y = g_world.prov[p].army_y; }
		else if (g_mw_kind[i] == 2) {                       /* the sidebar window of the news (+0x9e; NIEPEWNE per type) */
			const char *t = g_mw_type[i];
			g_infowin = !strcmp(t, "LTINCREASE") ? UI_IW_LANDTECH : !strcmp(t, "NTINCREASE") ? UI_IW_NAVALTECH
			          : !strncmp(t, "MISS", 4) ? UI_IW_MISSIONS : UI_IW_BUDGET;   /* STABDROP: 0x0D the budget window */
			*bar = 2;
		}
		logf_("msgwin: %s button 2", g_mw_type[i]);
		*moved = 1;
	}
	return 1;
}


/* the frontend options into the running game (at the start and again after the menu) */
/* ---- the tutorial's host (docs/SPEC_TUTORIAL.md): what the script asks of the interface ---- */
static int th_sel_armies(void *u)              /* the selection (the rubber band selects several) */
{
	int k, n = 0;
	(void)u;
	if (g_army < 0 || g_world.land[g_army].dead) return 0;
	for (k = 0; k < g_nselA; k++) if (g_selA[k] >= 0 && g_selA[k] < g_world.nland && !g_world.land[g_selA[k]].dead) n++;
	return n ? n : 1;
}
static int th_sel_fleets(void *u) { (void)u; return g_fleet < 0 ? 0 : g_nselF ? g_nselF : 1; }
/* ArmyIn / FleetIn: any of our units there (NIEPEWNE: the exe asks the selection, but the scripts wait for a
 * new army in London before anyone could select it); 0 = aboard a ship */
static int th_army_at(void *u, int p)
{
	int k;
	(void)u;
	for (k = 0; k < g_world.nland; k++) {
		const LandUnit *l = &g_world.land[k];
		if (l->owner != g_player) continue;
		if (p == 0 ? l->dead == LAND_ABOARD : !l->dead && l->location == p) return 1;
	}
	return 0;
}
static int th_fleet_at(void *u, int p)
{
	int k;
	(void)u;
	for (k = 0; k < g_world.nnaval; k++) {
		const NavalUnit *n = &g_world.naval[k];
		if (n->owner == g_player && n->location == p && n->warships + n->galleys + n->transports > 0) return 1;
	}
	return 0;
}
static int th_infowin(void *u)                 /* our windows -> the exe's ids (SPEC_UI_WINDOWS4 0) */
{
	(void)u;
	switch (g_infowin) {
	case UI_IW_COT: return 4;
	case UI_IW_PURCHASE: return g_pu_naval ? 12 : 11;
	case UI_IW_PEACE: return 16;
	case UI_IW_RESOURCE: return 17;
	case UI_IW_RELIGION: return 24;
	case UI_IW_DOMESTIC: return 25;
	case UI_IW_PROVINCE: return g_army < 0 && g_fleet < 0 && g_selected > 0 ? 9 : -1;
	default: return -1;
	}
}
static void th_show(void *u, const char *nk, const char *dk)   /* ShowMessage: message type TUTORIAL */
{
	char title[96], text[1600], date[64], l[400];
	const char *lab[4] = { NULL, NULL, NULL, NULL };
	int bits = opts_msg_bits(&g_opts, "TUTORIAL");
	(void)u;
	snprintf(l, sizeof l, text_get("TUTORIAL_LOG"), text_get(nk), text_get(dk));
	logf_("tutorial: %s", nk);
	if (bits & MSG_LOG) ledger_log(&g_world, l);
	if (!(bits & MSG_BOX)) return;
	snprintf(title, sizeof title, text_get("TUTORIAL_HEADER"), "");
	snprintf(text, sizeof text, "%s\\n%s", text_get(nk), text_get(dk));
	lab[0] = text_get("TUTORIAL_BTN1");
	fmt_date(date, sizeof date);
	g_tut_dlg = nk;
	dialog(title, date, text, lab);
	g_tut_dlg = NULL;
	g_tut_redraw = 1;
}
static void th_province_iw(void *u, int p) { (void)u; g_selected = p; g_army = g_fleet = -1; g_infowin = UI_IW_PROVINCE; g_tut_redraw = 1; }
static void th_center(void *u, int p) { long cx, cy; int z; (void)u; mv_get(g_mv, &cx, &cy, &z); mv_set(g_mv, g_world.prov[p].army_x, g_world.prov[p].army_y, z); g_tut_redraw = 1; }
static void th_build(void *u, int c, int p, long men) { (void)u; ev_units(NULL, c, p, 0, men); g_tut_redraw = 1; }
static void th_pause(void *u, int on) { (void)u; g_paused = on; g_tut_redraw = 1; }
static void th_marriage(void *u, int a, int b) { (void)u; if (g_dip) logf_("tutorial: marriage %s -> %s: %d", g_world.country[a].tag, g_world.country[b].tag, diplo_offer_marriage(g_dip, a, b)); }
static void th_declare(void *u, int a, int b) { (void)u; if (g_dip) diplo_declare_war(g_dip, a, b, g_dayno); }
static void th_end(void *u) { (void)u; g_tut_end = 1; logf_("tutorial: end"); }

static void tut_setup(Pak *text, int step)
{
	TutHost h;
	char err[96];
	if (g_tut) { tut_free(g_tut); g_tut = NULL; }
	ui_btn_disable(0); env_always_success(-1);
	g_tut_end = 0;
	if (!g_world.tutorial || g_player < 0) return;
	memset(&h, 0, sizeof h);
	h.sel_armies = th_sel_armies; h.sel_fleets = th_sel_fleets; h.sel_army_at = th_army_at; h.sel_fleet_at = th_fleet_at;
	h.infowin = th_infowin; h.show = th_show; h.province_iw = th_province_iw; h.center = th_center; h.build_army = th_build;
	h.pause = th_pause; h.marriage = th_marriage; h.declare = th_declare; h.end = th_end;
	err[0] = 0;
	g_tut = tut_load(text, g_world.tutfile, &g_world, g_player, &h, err, sizeof err);
	logf_("tutorial: %s %s%s%s", g_world.tutfile, g_tut ? "loaded" : "failed", err[0] ? ": " : "", err);
	if (g_tut && step > 1) { tut_restart_at(g_tut, step); logf_("tutorial: step %d again", step); }
}

/* the script's buttons -> ours (Config/buttons.txt: SET_BUDGET is btn_treasury, VIEW_FINANCIAL btn_budget,
 * VIEW_STATISTICS the EYR); VIEW_COUNTRY and the tech boosts have no button here */
static const int g_tb_ui[TB_N] = { UI_DIPLOMAT, UI_MERCHANT, UI_COLONIST, UI_MISSIONS, UI_LEDGER, -1, -1, -1, UI_TREASURY, UI_MISSIONARY,
	UI_VP, UI_VIEW_LAND, UI_VIEW_NAVAL, UI_FINANCIAL, -1, -1, -1, -1, -1, -1, -1, -1, -1, UI_MAPMODE_NORMAL, UI_MAPMODE_POLITICAL, UI_MAPMODE_RESOURCE };
static int tb_of_ui(int ui) { int k; for (k = 0; k < TB_N; k++) if (g_tb_ui[k] == ui) return k; return TB_NONE; }
static int tut_ui_ok(int ui)                   /* a click on sidebar button ui: allowed? (and IsButtonPress learns it) */
{
	int tb = tb_of_ui(ui);
	if (!g_tut || tb < 0) return 1;
	if (!tut_btn_ok(g_tut, tb)) return 0;
	tut_btn_pressed(g_tut, tb);
	return 1;
}
static unsigned long tut_ui_mask(void)
{
	unsigned long m = 0;
	int k;
	for (k = 0; k < TB_N; k++) if (g_tb_ui[k] >= 0 && !tut_btn_ok(g_tut, k)) m |= 1UL << g_tb_ui[k];
	return m;
}

/* the arrows (pointing at a script point or following a province) and the AddMessage popups */
static void tut_overlay(void)
{
	TutArrow a[16];
	const char *k[8];
	int n, i, m = 0, cx[16], cy[16], tx[16], ty[16], mx, my, mw, mh;
	if (!g_tut) return;
	ui_map_rect(&mx, &my, &mw, &mh);
	n = tut_arrows(g_tut, a, 16);
	for (i = 0; i < n; i++) {
		if (a[i].pid > 0 && a[i].pid < g_world.nprov) {
			int sx, sy;
			if (!g_mv || !mv_to_screen(g_mv, g_world.prov[a[i].pid].army_x, g_world.prov[a[i].pid].army_y, &sx, &sy)) continue;
			ui_tut_pid(sx + mx, sy + my, &cx[m], &cy[m], &tx[m], &ty[m]);
		} else ui_tut_pos(a[i].x, a[i].y, &cx[m], &cy[m], &tx[m], &ty[m]);
		m++;
	}
	ui_tut_arrows(amigagfx_chunky(), amigagfx_pitch(), cx, cy, tx, ty, m);
	n = tut_messages(g_tut, k, 8);
	ui_tut_messages(amigagfx_chunky(), amigagfx_pitch(), k, n, tx, ty, m, my + mh);
}

static void apply_opts(const Cfg *cfg)
{
	if (cfg->bench) return;                               /* the bench keeps its fixed rules */
	{
		g_speed = g_opts.opt[OPT_SPEED] + 1;
		g_world.opt_novp = (unsigned char)!g_opts.opt[OPT_BASEVP];
		g_world.opt_nomis = (unsigned char)!g_opts.opt[OPT_MISSIONS];
		g_world.opt_diff = (signed char)(g_opts.opt[OPT_DIFFICULTY] - 2);   /* the human's unit prices (windows4 1.4) */
		if (g_dip) diplo_set_aggr(g_dip, g_opts.opt[OPT_AIAGGR]);
		if (g_ev) events_set_kinds(g_ev, g_opts.opt[OPT_EVENTS]);
		if (g_world.optmode) {                       /* header optionmode = 1: the scenario's set_* win */
			g_world.opt_diff = (signed char)(g_world.set_opt[0] - 2);
			if (g_dip) diplo_set_aggr(g_dip, g_world.set_opt[1]);
			g_speed = g_world.set_opt[2] + 1; if (g_speed < 1) g_speed = 1; if (g_speed > 5) g_speed = 5;
			g_world.opt_novp = (unsigned char)!g_world.set_opt[3];
			g_world.opt_nomis = (unsigned char)!g_world.set_opt[4];
		}
		logf_("options: victory %d (%d / %d / %d), speed %d, fog %d, annex %d, missions %d, basevp %d, autosave %d, events %d",
		      g_opts.vmode, g_opts.vyear, g_opts.vvp, g_opts.vprov, g_speed, g_opts.opt[OPT_FOG], g_opts.opt[OPT_ANNEX],
		      g_opts.opt[OPT_MISSIONS], g_opts.opt[OPT_BASEVP], g_opts.opt[OPT_AUTOSAVE], g_opts.opt[OPT_EVENTS]);
	}
}

static int sim_start(Pak *text, Pak *map, const Cfg *cfg, const char *save, char *err, int errlen)
{
	int c;
	if (world_load_db(&g_world, text, err, errlen) || world_load_scenario(&g_world, text, save ? save : cfg->scenario, err, errlen)) return -1;
	g_dayno = cal_day(g_world.now.year, g_world.now.month, g_world.now.day);
	if (!(g_ev = events_load(&g_world, text, g_dayno, cfg->bench ? 12345UL : amigagfx_millis(), err, errlen))) logf_("events: %s", err);
	else events_set_log(g_ev, event_logged, NULL);
	if (!(g_ec = econ_load(&g_world, text, err, errlen))) logf_("economy: %s", err);
	env_set_econ(g_ec);
	if (!(g_mil = mil_load(&g_world, text, map, cfg->bench ? 12345UL : amigagfx_millis(), err, errlen))) logf_("military: %s", err);
	else mil_set_log(g_mil, mil_logged, NULL);
	if ((g_peace = peace_new(&g_world, g_dayno, cfg->bench ? 777UL : amigagfx_millis() + 1)) != NULL) { peace_set_log(g_peace, mil_logged, NULL); rev_set_peace(g_peace); }
	if ((g_dip = diplo_new(&g_world, g_mil, g_peace, cfg->bench ? 4242UL : amigagfx_millis() + 2)) != NULL) { diplo_set_log(g_dip, mil_logged, NULL); diplo_load_ai(g_dip, text); rev_set_diplo(g_dip); env_set_trade_offer(diplo_ai_offer_trade, g_dip); if (!cfg->bench) diplo_set_ask(g_dip, diplo_ask, NULL); }
	rev_load(&g_world, text);                                /* revolt.txt + the control dates (after a save is loaded) */
	env_set_military(g_mil); env_attach(&g_world);
	mil_set_tradefn(g_mil, trade_value_cb, g_ec); mil_set_supportfn(g_mil, support_cb, g_ec);
	econ_set_riskfn(g_ec, risk_cb, g_mil);
	g_player = -1;
	if (save) {                                          /* the saved game knows its player */
		for (c = 0; c < g_world.ncountry; c++) if (g_world.country[c].human) g_player = c;
		if (restore_game(save, g_ec, g_ev, g_mil, g_peace, g_dip)) logf_("load: module state missing in %s", save);
	} else {
		if (g_ec) econ_new_game(g_ec);
		world_weather_month(&g_world);                      /* and at the start (0x480a66) */
		env_seed(cfg->bench ? 12345UL : amigagfx_millis());
		if (g_mis) mis_reset(g_mis);
		rev_seed(cfg->bench ? 4711UL : amigagfx_millis() ^ 0x5A5AUL);
		g_player = world_country(&g_world, cfg->country);
		if (g_player >= 0) g_world.country[g_player].human = 1;
	}
	if (g_ev && !cfg->bench) events_set_choice(g_ev, event_choice, NULL);     /* the player answers events */
	if (g_ev && !cfg->bench) events_set_units(g_ev, ev_units, NULL);         /* ... and its unit commands make units */
	if (g_ev) events_set_cmds(g_ev, ev_cmd, NULL);                          /* ... and its diplomatic / revolt commands */
	if (g_peace && !cfg->bench) peace_set_offer(g_peace, peace_offer, NULL);  /* ... and peace offers */
	apply_opts(cfg);
	g_smsg_primed = 0;
	g_new_game = save == NULL;
	for (c = 0; c < g_world.ncountry && c < MAX_COUNTRY; c++) g_start_prov[c] = (short)g_world.country[c].nprov_owned;
	g_army = -1; g_fleet = -1; g_selected = -1; g_infowin = UI_IW_PROVINCE;
	return 0;
}

/* the victory condition (SPEC_UI §8.3): -2 = go on, -1 = over by date (rank by VP), else the winner.
 * Standard: the scenario's end or the end of the chosen year; Supremacy: the first country with the VP;
 * Deathmatch: the first with that many provinces more than at the start; Mission: the first mission the
 * player completes (ours: AI countries have no missions, NIEPEWNE). */
static int victory_check(const Cfg *cfg)
{
	int c;
	if (g_world.end.year > 0 && (g_world.now.year > g_world.end.year || (g_world.now.year == g_world.end.year && g_world.now.month >= g_world.end.month))) return -1;
	if (cfg->bench || cfg->debugend) return -2;
	switch (g_opts.vmode) {
	case VIC_STANDARD: if (g_world.now.year > g_opts.vyear) return -1; break;
	case VIC_SUPREMACY:
		{	int best = -1;                               /* several on the same day: the most VP */
			for (c = 0; c < g_world.ncountry; c++)
				if (g_world.country[c].nprov_owned > 0 && g_world.country[c].vp >= g_opts.vvp && (best < 0 || g_world.country[c].vp > g_world.country[best].vp)) best = c;
			if (best >= 0) return best; }
		break;
	case VIC_DEATHMATCH:
		for (c = 0; c < g_world.ncountry && c < MAX_COUNTRY; c++) if (g_world.country[c].nprov_owned - g_start_prov[c] >= g_opts.vprov) return c;
		break;
	case VIC_MISSION: if (g_mis && mis_done(g_mis) > 0) return g_player; break;
	}
	return -2;
}

static void sim_stop(void)
{
	if (g_ev) events_free(g_ev);
	if (g_ec) econ_free(g_ec);
	env_set_trade_offer(NULL, NULL);
	if (g_dip) diplo_free(g_dip);
	if (g_peace) peace_free(g_peace);
	if (g_mil) mil_free(g_mil);
	g_ev = NULL; g_ec = NULL; g_dip = NULL; g_peace = NULL; g_mil = NULL;
	world_free(&g_world);
}

/* the save slots: quick save, autosave, slot 1..8 (fixed names: no directory listing needed, which
 * libnix cannot do on MorphOS) */
#define NSLOT 10
static const char *slot_path(int i)
{
	static char p[48];
	if (i == 0) return QUICKSAVE;
	if (i == 1) return AUTOSAVE;
	snprintf(p, sizeof p, "PROGDIR:save/slot%d.eug", i - 1);
	return p;
}

/* "Slot 3 - 12 May 1453" from the save's startdate line (= the date it was saved); 0 when there is no file */
static int slot_label(int i, char *out, int n)
{
	static const char *mn[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
	char line[256], nm[16];
	FILE *f = fopen(slot_path(i), "r");
	int k, have = f != NULL, year = 0, month = 0, day = 0;
	if (i == 0) snprintf(nm, sizeof nm, "Quick save"); else if (i == 1) snprintf(nm, sizeof nm, "Autosave"); else snprintf(nm, sizeof nm, "Slot %d", i - 1);
	for (k = 0; f && k < 6 && fgets(line, sizeof line, f); k++) {
		char *p = strstr(line, "startdate"), *q;
		if (!p) continue;
		if ((q = strstr(p, "year = ")) != NULL) year = atoi(q + 7);
		if ((q = strstr(p, "month = ")) != NULL) { int m; for (m = 0; m < 12; m++) if (!strncmp(q + 8, mn[m] + 0, 3) || (q[8] == mn[m][0] + 32 && !strncmp(q + 9, mn[m] + 1, 2))) { month = m; break; } }
		if ((q = strstr(p, "day = ")) != NULL) day = atoi(q + 6);
		break;
	}
	if (f) fclose(f);
	if (!have) snprintf(out, (size_t)n, "%s - empty", nm);
	else snprintf(out, (size_t)n, "%s - %d %s %d", nm, day + 1, mn[month], year);
	return have;
}

static void save_to(const char *path)
{
	unsigned long t0 = amigagfx_millis();
	int rc;
	eu2_mkdir("PROGDIR:save");
	rc = save_game(path, &g_world, g_ec, g_ev, g_mil, g_peace, g_dip);
	logf_("save: %s %s, %lu ms", path, rc ? "FAILED" : "ok", amigagfx_millis() - t0);
}

static int load_from(Pak *text, Pak *map, const Cfg *cfg, const char *path)
{
	char err[160];
	unsigned long t0 = amigagfx_millis();
	FILE *f = fopen(path, "r");
	if (!f) { logf_("load: no %s", path); return -1; }
	fclose(f);
	g_tut_restore = 0;
	sim_stop();
	if (sim_start(text, map, cfg, path, err, sizeof err)) {     /* a broken save: back to a new game */
		logf_("load: %s - starting a new game", err);
		sim_start(text, map, cfg, NULL, err, sizeof err);
		return -1;
	}
	tut_setup(text, g_tut_restore); g_tut_restore = 0;
	logf_("load: %s, %d-%d-%d, player %s, %lu ms", path, g_world.now.year, g_world.now.month, g_world.now.day + 1,
	      g_player >= 0 ? g_world.country[g_player].tag : "-", amigagfx_millis() - t0);
	return 0;
}

/* the list of slots: saving offers quick save and slots 1..8 (the autosave is the game's), loading the
 * ones that exist; returns the slot or -1 */
static int slot_window(int saving)
{
	static char lab[NSLOT][48];
	const char *rows[NSLOT];
	int st[NSLOT], i, hover = -1, dirty = 1, pick = -2;
	for (i = 0; i < NSLOT; i++) { int have = slot_label(i, lab[i], sizeof lab[i]); rows[i] = lab[i]; st[i] = saving ? i != 1 : have; }
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			redraw(g_mv, "slots");
			ui_slots_draw(amigagfx_chunky(), amigagfx_pitch(), saving ? "SAVEGAME" : "LOADGAME", rows, st, NSLOT, hover);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || (e.type == AMIGAGFX_EV_KEY && e.code == 0x45)) pick = -1;
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_slots_hit(e.x, e.y, NSLOT); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_slots_hit(e.x, e.y, NSLOT);
				if (h == 100) pick = -1;
				else if (h >= 0 && h < NSLOT && st[h]) pick = h;
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = -1;
		if (pick == -2) eu2_sleep_ms(20);
	}
	logf_("slots (%s): %d", saving ? "save" : "load", pick);
	return pick;
}

static void quick_save(void)
{
	unsigned long t0 = amigagfx_millis();
	int rc;
	eu2_mkdir("PROGDIR:save");
	rc = save_game(QUICKSAVE, &g_world, g_ec, g_ev, g_mil, g_peace, g_dip);
	logf_("save: %s %s, %lu ms", QUICKSAVE, rc ? "FAILED" : "ok", amigagfx_millis() - t0);
}

static int quick_load(Pak *text, Pak *map, const Cfg *cfg)
{
	char err[160];
	unsigned long t0 = amigagfx_millis();
	FILE *f = fopen(QUICKSAVE, "r");
	if (!f) { logf_("load: no %s", QUICKSAVE); return -1; }
	fclose(f);
	g_tut_restore = 0;
	sim_stop();
	if (sim_start(text, map, cfg, QUICKSAVE, err, sizeof err)) {     /* a broken save: back to a new game */
		logf_("load: %s - starting a new game", err);
		sim_start(text, map, cfg, NULL, err, sizeof err);
		return -1;
	}
	tut_setup(text, g_tut_restore); g_tut_restore = 0;
	logf_("load: %s, %d-%d-%d, player %s, %lu ms", QUICKSAVE, g_world.now.year, g_world.now.month, g_world.now.day + 1,
	      g_player >= 0 ? g_world.country[g_player].tag : "-", amigagfx_millis() - t0);
	return 0;
}

static int g_to_menu;                                    /* the game menu's Exit: to the main menu, not the system */
/* the in-game menu (MENU button or Esc): -1 resume, 1 save, 2 load, 3 exit to the main menu, 4 messages,
 * 5 quit (window closed / autoexit); the game stands still */
static int game_menu(void)
{
	int hover = -1, dirty = 1, pick = -2, has = 0, i, gh = amigagfx_game_height(), pitch = amigagfx_pitch();
	char l[48];
	unsigned char *under;
	for (i = 0; i < NSLOT && !has; i++) has = slot_label(i, l, sizeof l);
	/* the game behind it drawn once and kept: moving the mouse only redraws the menu over that copy and
	 * blits once (user 2026-11-11: a full map redraw blitted before the menu made it blink) */
	redraw(g_mv, "menu");
	under = malloc((size_t)pitch * gh);
	if (under) memcpy(under, amigagfx_chunky(), (size_t)pitch * gh);
	while (pick == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			if (under) memcpy(amigagfx_chunky(), under, (size_t)pitch * gh); else redraw(g_mv, "menu");
			ui_menu_draw(amigagfx_chunky(), pitch, hover, has);
			scr_blit(0, 0, amigagfx_game_width(), gh);
			dirty = 0;
		}
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT) pick = 5;
			else if (e.type == AMIGAGFX_EV_KEY && e.code == 0x45) pick = -1;               /* Esc: back to the game */
			else if (e.type == AMIGAGFX_EV_MOUSEMOVE) { int h = ui_menu_hit(e.x, e.y); if (h != hover) { hover = h; dirty = 1; } }
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) {
				int h = ui_menu_hit(e.x, e.y);
				if (h == 0) pick = -1;
				else if (h == 1 || h == 3 || h == 4 || (h == 2 && has)) pick = h;
			}
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) pick = 5;
		if (pick == -2) eu2_sleep_ms(20);
	}
	free(under);
	logf_("menu: %d", pick);
	return pick;
}

/* the main menu and the single-player setup (SPEC_UI §8, layout decoded from eu2.exe, see ui.h):
 * 0 = new game (scenario path + country tag filled), 1 = load the quick save, -1 = exit */
/* the text.csv code of a scenario's per-country texts (<TAG>_<code>_HISTDESC ...): not in the scenario
 * files, so by name (NIEPEWNE for the exploration and revolution scenarios) */
static const char *scen_code(const char *path)
{
	static const char *map[][2] = { { "grand campaign", "GC" }, { "age of mercantilism", "1617" }, { "age of enlightenment", "1700" },
	                                { "fantasia", "F" }, { "age of exploration", "GERC" }, { "age of revolutions", "RG" } };
	int i;
	for (i = 0; i < (int)(sizeof map / sizeof map[0]); i++) if (strstr(path, map[i][0])) return map[i][1];
	return NULL;
}

/* closing VICTORY / OPTIONS: the edit boxes are read with atoi and clamped (0x58dc0a), the choice is
 * logged as the game does (FEVIC_*_LOG) and kept in eu2opts.txt */
static char g_fe_log[10][96]; static int g_fe_nlog;   /* the setup screen's log box: victory and option changes */
static void fe_log(const char *l)
{
	if (g_fe_nlog == 10) { memmove(g_fe_log[0], g_fe_log[1], sizeof g_fe_log[0] * 9); g_fe_nlog = 9; }
	snprintf(g_fe_log[g_fe_nlog++], sizeof g_fe_log[0], "%s", l);
}

/* the VICTORY / OPTIONS box closed: the victory line or one line per changed option goes to the log
 * (0x5860e2, 0x586840: FECLI_HOSTSET1/2 "<%s changed to %s>" - ours: "label: value") */
static GameOpts g_fe_before;
static void front_opts_done(char vbuf[3][8], int which)
{
	static const char *lk[4] = { "FEVIC_STANDARD_LOG", "FEVIC_SUPREMACY_LOG", "FEVIC_DEATHMATCH_LOG", "FEVIC_MISSION_LOG" };
	char l[160];
	g_opts.vyear = opts_clamp_num(0, atoi(vbuf[0])); g_opts.vvp = opts_clamp_num(1, atoi(vbuf[1])); g_opts.vprov = opts_clamp_num(2, atoi(vbuf[2]));
	snprintf(vbuf[0], 8, "%d", g_opts.vyear); snprintf(vbuf[1], 8, "%d", g_opts.vvp); snprintf(vbuf[2], 8, "%d", g_opts.vprov);
	snprintf(l, sizeof l, text_get(lk[g_opts.vmode]), g_opts.vmode == VIC_STANDARD ? g_opts.vyear : g_opts.vmode == VIC_SUPREMACY ? g_opts.vvp : g_opts.vprov);
	logf_("frontend: %s", l);
	if (which == UF_VICTORY) fe_log(l);
	else {
		int r;
		for (r = 0; r < OPT_N; r++)
			if (g_opts.opt[r] != g_fe_before.opt[r]) { snprintf(l, sizeof l, "<%s: %s>", text_get(opts_label(r)), text_get(opts_value(r, g_opts.opt[r]))); fe_log(l); }
	}
	if (opts_save(&g_opts, OPTSFILE)) logf_("frontend: cannot write %s", OPTSFILE);
}

/* the intro (user decision: still frames instead of the Bink videos): the converter's intro/index.txt
 * lists "<entry> <ms>"; each still fades in from black, stays, fades out (its 8-bit palette scaled, user
 * 2026-11-10), and the next follows by itself. A click or a key ends the whole intro, as one click ends
 * the video on the PC. In data/intro.pak, else in gfx.pak. */
#define FADE_MS 400
static int intro_fade(const unsigned char *pal, int n, int up)   /* 1 = stopped by a click / key */
{
	unsigned char tmp[768];
	unsigned long t0 = amigagfx_millis(), t;
	for (;;) {
		AmigaGfxEvent e;
		long k;
		int i;
		while (poll_ev(&e))
			if (e.type == AMIGAGFX_EV_QUIT || e.type == AMIGAGFX_EV_KEY || e.type == AMIGAGFX_EV_MOUSEDOWN) return 1;
		t = amigagfx_millis() - t0;
		if (t > FADE_MS) t = FADE_MS;
		k = (long)t * 256 / FADE_MS;
		if (!up) k = 256 - k;
		for (i = 0; i < 3 * n; i++) tmp[i] = (unsigned char)(pal[i] * k >> 8);
		amigagfx_set_palette(tmp, 0, n);
		if (t >= FADE_MS) return 0;
		sfx_service();
		eu2_sleep_ms(20);
	}
}

static void intro_play(Pak *gfx)
{
	Pak ip, *pk = gfx;
	int own = pak_open(&ip, "PROGDIR:data/intro.pak") == 0, stop = 0;
	unsigned long sz;
	char *idx, *line, *cur;
	if (own) pk = &ip;
	if (!(idx = (char *)pak_load(pk, "intro/index.txt", &sz))) { if (own) pak_close(&ip); logf_("intro: no stills"); return; }
	idx = (char *)realloc(idx, sz + 1); idx[sz] = 0;
	for (cur = idx; !stop && (line = strtok(cur, "\r\n")) != NULL; cur = NULL) {
		char name[96];
		long ms = 2500;
		unsigned long t0;
		static unsigned char ipal[768];
		int inc = 0;
		if (sscanf(line, "%95s %ld", name, &ms) < 1 || name[0] == '#') continue;
		{	/* PAL8 with its own palette: the screen takes it while the still is shown; else through the menus' palette */
			unsigned long isz;
			unsigned char *raw = pak_load(pk, name, &isz);
			EuiImage im;
			int shown = 0;
			if (raw && !eui_decode(raw, isz, &im)) {
				if (im.format == 1 && im.w <= amigagfx_game_width() && im.h <= amigagfx_game_height()) {
					unsigned char *dst = amigagfx_chunky();
					int pitch = amigagfx_pitch(), gw = amigagfx_game_width(), gh = amigagfx_game_height(), x0 = (gw - im.w) / 2, y0 = (gh - im.h) / 2, y, k, black = 0;
					long best = 0x7fffffffL;
					unsigned char lut[256];
					/* pens 17-19 are the AGA pointer's (its colours from prefs, e.g. red): the still's own colours
					 * there are not shown, so its pixels in them take the nearest other pen, and so does the border */
					for (k = 0; k < 256; k++) lut[k] = (unsigned char)k;
					for (k = 0; k < im.ncolors; k++) {
						long d = (long)im.pal[3 * k] + im.pal[3 * k + 1] + im.pal[3 * k + 2];
						if (k >= 17 && k <= 19) {
							int j, bj = 0; long bd = 0x7fffffffL;
							for (j = 0; j < im.ncolors; j++) {
								long e0 = (long)im.pal[3 * j] - im.pal[3 * k], e1 = (long)im.pal[3 * j + 1] - im.pal[3 * k + 1], e2 = (long)im.pal[3 * j + 2] - im.pal[3 * k + 2];
								if (j >= 17 && j <= 19) continue;
								if (e0 * e0 + e1 * e1 + e2 * e2 < bd) { bd = e0 * e0 + e1 * e1 + e2 * e2; bj = j; }
							}
							lut[k] = (unsigned char)bj;
							continue;
						}
						if (d < best) { best = d; black = k; }
					}
					memcpy(ipal, im.pal, (size_t)im.ncolors * 3); inc = im.ncolors;
					{ static const unsigned char zero[768]; amigagfx_set_palette(zero, 0, inc); }   /* drawn under black, then faded in */
					for (y = 0; y < gh; y++) memset(dst + (long)y * pitch, black, (size_t)gw);
					for (y = 0; y < im.h; y++) {
						unsigned char *o = dst + (long)(y0 + y) * pitch + x0;
						const unsigned char *q = im.pix + (long)y * im.w;
						for (k = 0; k < im.w; k++) o[k] = lut[q[k]];
					}
					shown = 1;
				}
				eui_free(&im);
			}
			free(raw);
			if (!shown && ui_still_draw(amigagfx_chunky(), amigagfx_pitch(), pk, name)) { logf_("intro: %s missing", name); continue; }
		}
		scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
		logf_("intro: %s %ld ms", name, ms);
		if (inc && intro_fade(ipal, inc, 1)) { stop = 1; break; }
		for (t0 = amigagfx_millis(); !stop && amigagfx_millis() - t0 < (unsigned long)ms; ) {
			AmigaGfxEvent e;
			while (poll_ev(&e))
				if (e.type == AMIGAGFX_EV_QUIT || e.type == AMIGAGFX_EV_KEY || e.type == AMIGAGFX_EV_MOUSEDOWN) stop = 1;
			sfx_service();
			eu2_sleep_ms(20);
		}
		if (stop) break;
		if (inc && intro_fade(ipal, inc, 0)) { stop = 1; break; }
	}
	if (stop) logf_("intro: skipped");
	free(idx);
	if (own) pak_close(&ip);
	if (ui_front_palette()) amigagfx_set_palette(ui_front_palette(), 0, 256);   /* back to the menus' colours */
}

/* the CREDITS screen: 1 px up per 1/50 s (the exe: per main-loop pass, NIEPEWNE rate), loops until a
 * left-button release or a key */
static void credits_play(void)
{
	unsigned long t0;
	int stop = 0, down = 0;
	if (ui_credits_open()) { logf_("credits: no db/credits.txt"); ui_credits_close(); return; }
	logf_("credits: open");
	t0 = amigagfx_millis();
	while (!stop) {
		AmigaGfxEvent e;
		int n;
		while (poll_ev(&e)) {
			if (e.type == AMIGAGFX_EV_QUIT || e.type == AMIGAGFX_EV_KEY) stop = 1;
			else if (e.type == AMIGAGFX_EV_MOUSEDOWN && e.code == AMIGAGFX_BUTTON_LEFT) down = 1;
			else if (e.type == AMIGAGFX_EV_MOUSEUP && down) stop = 1;
		}
		for (n = 0; amigagfx_millis() - t0 >= 20 && n < 10; n++) { t0 += 20; ui_credits_step(); }
		if (amigagfx_millis() - t0 >= 20) t0 = amigagfx_millis();
		ui_credits_draw(amigagfx_chunky(), amigagfx_pitch());
		scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
		sfx_service();
		eu2_sleep_ms(10);
	}
	ui_credits_close();
	logf_("credits: closed");
}

static int g_fe_slot[NSLOT], g_fe_nslot, g_fe_load;
   /* the frontend's saved-game rows -> slots */
static int frontend(Pak *text, char *scen, int scenlen, char *tag)
{
	static FrontScen sc[FRONT_MAXSCEN];
	UiFront fr;
	static char vbuf[3][8];                     /* the VICTORY edit boxes */
	int ns = front_scenarios(text, sc, FRONT_MAXSCEN), i, dirty = 1, result = -2, has_save, cur = 0;
	char chosen[4] = "";
	FILE *f = fopen(QUICKSAVE, "r");
	has_save = f != NULL;
	if (f) fclose(f);
	memset(&fr, 0, sizeof fr);
	for (i = 0; i < ns; i++) { fr.list[i] = sc[i].name; if (script_ieq(sc[i].path, scen)) cur = i; }
	fr.nlist = ns;
	{	/* scenarios, then the saved games (quick save, autosave, slots) */
		static char sl[NSLOT][48];
		int k2;
		g_fe_nslot = 0;
		for (k2 = 0; k2 < NSLOT && fr.nlist < 24; k2++)
			if (slot_label(k2, sl[k2], sizeof sl[k2])) { fr.list[fr.nlist++] = sl[k2]; g_fe_slot[g_fe_nslot++] = k2; }
		has_save = g_fe_nslot > 0;
	}
	fr.nscen = ns;
	fr.hover = UF_NONE; fr.sel = cur; fr.country_feat = -1;
	fr.screen = g_fe_screen; g_fe_screen = 0;
	fr.opts = &g_opts; fr.vfocus = -1;
	for (i = 0; i < 3; i++) fr.vtext[i] = vbuf[i];
	logf_("frontend: %d scenarios%s", ns, has_save ? " + quick save" : "");
	if (!ns) return -1;
	while (result == -2) {
		AmigaGfxEvent e;
		if (dirty) {
			int k;
			fr.feat = fr.sel < ns ? sc[fr.sel].sel : NULL;
			fr.nfeat = fr.sel < ns ? sc[fr.sel].nfeat : 0;
			fr.others = fr.sel < ns ? sc[fr.sel].sel + sc[fr.sel].nfeat : NULL;
			fr.nothers = fr.sel < ns ? sc[fr.sel].nsel - sc[fr.sel].nfeat : 0;
			fr.country_tag = chosen[0] ? chosen : NULL;
			fr.country_feat = -1;
			for (k = 0; chosen[0] && k < fr.nfeat; k++) if (!strcmp(fr.feat[k], chosen)) fr.country_feat = k;
			/* the country panel (docs/SPEC_FRONTEND_SP.md 2): the header block's keys verbatim (text.csv,
			 * else the raw string), <TAG>_DESC by default; mission victory shows the mission texts */
			fr.panel = fr.sel < ns && chosen[0];
			if (fr.panel) {
				static char title[96], picture[64], defk[16];
				static const FrontCountry none;
				const FrontCountry *fc = fr.country_feat >= 0 && fr.country_feat < FRONT_MAXFEAT ? &sc[fr.sel].feat[fr.country_feat] : &none;
				int mis = g_opts.vmode == VIC_MISSION, adj = 2 - g_opts.opt[OPT_DIFFICULTY], c = world_country(&g_world, chosen), j;
				const char *key;
				snprintf(defk, sizeof defk, "%s_DESC", chosen);
				if (fr.p_strat) key = mis ? fc->mtact : fc->strat[0] ? fc->strat : defk;
				else key = mis ? fc->mcountry : fc->hist[0] ? fc->hist : defk;
				fr.p_text = key[0] ? text_get(key) : "";
				if (fr.p_strat) snprintf(title, sizeof title, "%s%s - %s", text_get(chosen), mis ? " - MISSION" : "", text_get("FESTR_STRATEGY"));
				else if (mis) snprintf(title, sizeof title, "%s - %s %s", text_get(chosen), text_get("FEVIC_MISSION"), text_get("FEBTN_INFO"));
				else snprintf(title, sizeof title, "%s %s", text_get(chosen), text_get("FESTR_HISTORICALINFO"));
				for (j = 0; title[j]; j++) if ((title[j] >= 'a' && title[j] <= 'z') || ((unsigned char)title[j] >= 0xE0 && (unsigned char)title[j] != 0xF7)) title[j] = (char)(title[j] - 32);
				fr.p_title = title;
				fr.p_rate[0] = fc->diff + adj; fr.p_rate[1] = fc->eco + adj; fr.p_rate[2] = fc->mil + adj; fr.p_rate[3] = fc->dip + adj;
				if (fc->picture[0]) {                                  /* scenarios\data\x.bmp -> the pak's name */
					snprintf(picture, sizeof picture, "%s", fc->picture);
					for (j = 0; picture[j]; j++) { if (picture[j] == '\\') picture[j] = '/'; if (picture[j] >= 'A' && picture[j] <= 'Z') picture[j] += 32; }
				} else {                                               /* by country.csv City Nationality, then techgroup (0x4a4c86) */
					const char *cn = c >= 0 ? g_world.country[c].city_nat : "", *pic = "latin";
					if (!strcmp(cn, "OHI")) pic = "northamerica";
					else if (!strcmp(cn, "CHI")) pic = "china";
					else if (!strcmp(cn, "MOG")) pic = "hindu";
					else if (!strcmp(cn, "AZT")) pic = "southamerica";
					else if (!strcmp(cn, "BAN")) pic = "afrika";
					else if (!strcmp(cn, "MUS")) pic = "muslim";                   /* NIEPEWNE: the exe's index for it */
					else if (c >= 0 && g_world.country[c].techgroup >= 0 && script_ieq(world_name(&g_world, g_world.country[c].techgroup), "orthodox")) pic = "orthodox";
					snprintf(picture, sizeof picture, "scenarios/data/%s_data.bmp", pic);
				}
				fr.p_picture = picture;
			}
			fr.ninfo = g_fe_nlog;
			for (k = 0; k < fr.ninfo; k++) fr.info[k] = g_fe_log[k];
			fr.can_start = fr.sel >= ns || chosen[0];
			ui_front_draw(amigagfx_chunky(), amigagfx_pitch(), &fr);
			scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
			dirty = 0;
		}
		while (poll_ev(&e)) {
			int h;
			if (e.type == AMIGAGFX_EV_QUIT) { result = -1; break; }
			if (e.type == AMIGAGFX_EV_KEY && fr.opt_popup == UF_VICTORY && fr.vfocus >= 0 && e.code != 0x45) {   /* typing into an edit box */
				static const char dig[11] = "1234567890";
				char *b = vbuf[fr.vfocus];
				size_t l = strlen(b);
				int mx = fr.vfocus == 0 ? 4 : 3;
				if (e.code >= 0x01 && e.code <= 0x0A && (int)l < mx) { b[l] = dig[e.code - 1]; b[l + 1] = 0; }
				else if (e.code == 0x41 && l > 0) b[l - 1] = 0;                   /* backspace */
				else if (e.code == 0x44 || e.code == 0x43) fr.vfocus = -1;       /* return / enter */
				dirty = 1; continue;
			}
			if (e.type == AMIGAGFX_EV_KEY && e.code == 0x45 && fr.opt_popup) { { int was = fr.opt_popup; fr.opt_popup = 0; fr.vfocus = -1; front_opts_done(vbuf, was); } dirty = 1; continue; }
			if (e.type == AMIGAGFX_EV_KEY && e.code == 0x45) {                  /* Esc: close / back / quit */
				if (fr.popup) fr.popup = 0; else if (fr.screen) fr.screen = 0; else result = -1;
				dirty = 1; continue;
			}
			if (e.type == AMIGAGFX_EV_MOUSEMOVE) { h = ui_front_hit(e.x, e.y, &fr); if (h != fr.hover) { fr.hover = h; dirty = 1; } continue; }
			if (e.type != AMIGAGFX_EV_MOUSEDOWN) continue;
			h = ui_front_hit(e.x, e.y, &fr);
			if (fr.opt_popup) {                                                  /* the VICTORY / OPTIONS popup takes every click */
				if (h == UF_NONE) continue;
				dirty = 1;
				if (h >= UF_OPTROW && h < UF_OPTROW + OPT_N) {
					int r = h - UF_OPTROW, n = opts_nvalues(r);
					g_opts.opt[r] = (g_opts.opt[r] + (e.code == AMIGAGFX_BUTTON_RIGHT ? n - 1 : 1)) % n;
				}
				else if (e.code != AMIGAGFX_BUTTON_LEFT) continue;
				else if (h >= UF_VRADIO && h < UF_VRADIO + 4) { g_opts.vmode = h - UF_VRADIO; fr.vfocus = -1; }
				else if (h >= UF_VEDIT && h < UF_VEDIT + 3) { fr.vfocus = h - UF_VEDIT; g_opts.vmode = fr.vfocus; }
				else if (h == UF_VBACK || h == UF_OPTOK) { { int was = fr.opt_popup; fr.opt_popup = 0; fr.vfocus = -1; front_opts_done(vbuf, was); } }
				continue;
			}
			if (e.code == AMIGAGFX_BUTTON_RIGHT) {                               /* right click on a shield: the others */
				if (fr.screen == 1 && !fr.popup && h >= UF_SHIELD && h < UF_SHIELD + 9 && fr.nothers > 0) { fr.popup = 1; fr.page = 0; dirty = 1; }
				else if (fr.popup) { fr.popup = 0; dirty = 1; }
				continue;
			}
			if (e.code != AMIGAGFX_BUTTON_LEFT || h == UF_NONE) continue;
			dirty = 1;
			if (h == UF_INFO) { fr.opt_popup = 0; fr.vfocus = -1; }                       /* INFO: the country panel again (0x5866ee) */
			else if (h == UF_STRAT) { fr.p_strat = !fr.p_strat; fr.p_page = 0; }            /* STRATEGY / BACK (0x58a340) */
			else if (h == UF_PTEXT) fr.p_page++;                                            /* the next page of the text (ours: the exe scrolls) */
			else if (h == UF_VICTORY || h == UF_OPTIONS) {
				fr.opt_popup = h; fr.vfocus = -1; fr.popup = 0; g_fe_before = g_opts;
				snprintf(vbuf[0], sizeof vbuf[0], "%d", g_opts.vyear); snprintf(vbuf[1], sizeof vbuf[1], "%d", g_opts.vvp); snprintf(vbuf[2], sizeof vbuf[2], "%d", g_opts.vprov);
			}
			else if (h == UF_SINGLE) fr.screen = 1;
			else if (h == UF_TUTORIAL) fr.screen = 2;
			else if (h == UF_TUTCH + 9) fr.screen = 0;                 /* BACK TO MAIN */
			else if (h >= UF_TUTCH && h < UF_TUTCH + 9) {               /* chapter i: Tutorial\Chapter<i>, played as England (the only country of its header) */
				snprintf(scen, (size_t)scenlen, "tutorial/chapter%d.eug", h - UF_TUTCH + 1); memcpy(tag, "ENG", 4); result = 0;
			}
			else if (h == UF_CREDITS) credits_play();
			else if (h == UF_EXIT) result = -1;
			else if (h == UF_BACK) { fr.screen = 0; fr.popup = 0; }
			else if (h == UF_POPCLOSE) fr.popup = 0;
			else if (h == UF_PREV && fr.page > 0) fr.page--;
			else if (h == UF_NEXT && (fr.page + 1) * UF_PAGE < fr.nothers) fr.page++;
			else if (h == UF_START && fr.can_start) {
				if (fr.sel >= ns) { g_fe_load = g_fe_slot[fr.sel - ns]; result = 1; }
				else { snprintf(scen, (size_t)scenlen, "%s", sc[fr.sel].path); memcpy(tag, chosen, 4); result = 0; }
			}
			else if (h >= UF_OTHER) { memcpy(chosen, fr.others[h - UF_OTHER], 4); fr.popup = 0; fr.p_strat = 0; fr.p_page = 0; }
			else if (h >= UF_SHIELD) {                                          /* a shield toggles (0x58cd80) */
				if (!strcmp(chosen, fr.feat[h - UF_SHIELD])) chosen[0] = 0; else memcpy(chosen, fr.feat[h - UF_SHIELD], 4);
				fr.p_strat = 0; fr.p_page = 0;
			}
			else if (h >= UF_LIST) { fr.sel = h - UF_LIST; chosen[0] = 0; fr.popup = 0; fr.p_strat = 0; fr.p_page = 0; fr.opt_popup = 0; }
		}
		sfx_service();
		if (g_cfg->autoexit_ms && amigagfx_millis() - g_t0 > (unsigned long)g_cfg->autoexit_ms) result = -1;
		if (result == -2 && !dirty) eu2_sleep_ms(20);
	}
	logf_("frontend: %s%s%s", result == 0 ? "new game as " : result == 1 ? "load" : "exit", result == 0 ? tag : "", "");
	return result;
}

/* which cursor (0x52fc00, docs/SPEC_UNIT_UI.md 7): left button held -> click; over the interface ->
 * select; colonist / merchant windows -> send; with an army: own unit -> select, own fleet in a sea
 * zone -> load, unknown land -> explore, enemy (at war) -> attack, other land -> move, sea -> illegal;
 * with a fleet: unload mode -> unload, sea -> move (unknown: explore), a port of ours or an ally's ->
 * enter port, else illegal. Reachability is not tested (ours: the order dialog says so). */
static void cursor_choose(MapView *mv, int held)
{
	int mx, my, mw, mh, k = UI_CUR_SELECT, id, nav = 0;
	ui_map_rect(&mx, &my, &mw, &mh);
	if (held) k = UI_CUR_CLICK;
	else if (on_map(g_ptr_x, g_ptr_y, mx, my) && g_player >= 0 && !(g_infowin == UI_IW_DIPLO && g_ptr_x < mx + 224)) {
		id = mv_province_at(mv, g_ptr_x - mx, g_ptr_y - my, mw, mh);
		if (id > 0 && id < g_world.nprov) {
			const Province *p = &g_world.prov[id];
			int known = WORLD_KNOWN(&g_world, g_player, id), sea = p->terrain == 5 || p->terrain == 6;
			int o = p->controller >= 0 ? p->controller : p->owner;
			if (g_infowin == UI_IW_COLONIST) k = !sea && p->owner < 0 ? UI_CUR_SEND_COLONIST : UI_CUR_ILLEGAL;
			else if (g_infowin == UI_IW_COT) k = UI_CUR_SEND_MERCHANT;
			else if (units_pick(&g_world, mv, g_ptr_x - mx, g_ptr_y - my, g_player, &nav)) k = (g_army >= 0 && nav && sea) ? UI_CUR_LOAD : UI_CUR_SELECT;
			else if (g_army >= 0) k = !known && !sea ? UI_CUR_EXPLORE_ARMY : sea ? (mil_fleet_at(&g_world, id, g_player) >= 0 ? UI_CUR_LOAD : UI_CUR_ILLEGAL)
			                        : o >= 0 && o != g_player && mil_at_war(&g_world, g_player, o) ? UI_CUR_ATTACK_ARMY : UI_CUR_MOVE_ARMY;
			else if (g_fleet >= 0) k = g_unload ? (sea ? UI_CUR_ILLEGAL : UI_CUR_UNLOAD) : sea ? (known ? UI_CUR_MOVE_NAVY : UI_CUR_EXPLORE_NAVY)
			                        : mil_is_port(&g_world, id) && (p->owner == g_player) ? UI_CUR_ENTERPORT : UI_CUR_ILLEGAL;
		}
	}
	if (k != ui_cursor_get()) { ui_cursor_kind(k); scr_blit(0, 0, 0, 0); }
}

static int risk_cb(void *u, int p) { return rev_risk(&g_world, (const Military *)u, p); }

int eu2_main(void)
{
	Cfg cfg;
	Pak gfx, text, map, sfxp;
	MapView *mv;
	char err[160];
	int want, w, h, dragging = 0, lastx = 0, lasty = 0, dragdist = 0, quit_now = 0;
	unsigned long lastday = 0;
	unsigned long t0;
	AmigaMemProbe("start");                               /* RAM use = start - the lowest later line (amiga_mem.log) */

	remove(LOGFILE);                                   /* freopen("w") does not truncate here */
	freopen(LOGFILE, "w", stdout);
	logf_("AmiEU2 %s built: %s %s", EU2_VERSION, __DATE__, __TIME__);
	amiga_trap_install();
	read_cfg(&cfg);
	opts_load(&g_opts, OPTSFILE);
	g_bench_fog = cfg.bench;
	{ extern unsigned long (*ai_clock)(void); if (cfg.bench) ai_clock = amigagfx_millis; }
	if (check_conversion()) return 20;
	if (pak_open(&gfx, "PROGDIR:data/gfx.pak") || pak_open(&text, "PROGDIR:data/text.pak") || pak_open(&map, "PROGDIR:data/map.pak") || pak_open(&sfxp, "PROGDIR:data/sfx.pak")) {
		fatal_("Cannot open the data packs in PROGDIR:data."); return 20;
	}

	want = !strcmp(cfg.gfx, "rtg") ? AMIGAGFX_BACKEND_RTG : AMIGAGFX_BACKEND_AGA;
	w = want == AMIGAGFX_BACKEND_RTG ? cfg.rtg_w : 640;
	h = want == AMIGAGFX_BACKEND_RTG ? cfg.rtg_h : 480;
	amigagfx_set_screen_title("AmiEU2 68K " EU2_VERSION);
	amigagfx_set_rtg_depth(cfg.rtg_bits);
	if (amigagfx_open(w, h, cfg.titlebar, want)) { fatal_("Cannot open a %dx%d screen.", w, h); return 20; }
	if (amigagfx_backend() != want && (w != 800 || h != 600)) {          /* no such RTG mode: the 800x600 the original had */
		logf_("display: no RTG %dx%d - trying 800x600", w, h);
		amigagfx_close(); w = 800; h = 600;
		if (amigagfx_open(w, h, cfg.titlebar, want)) { fatal_("Cannot open an 800x600 screen."); return 20; }
	}
	if (amigagfx_backend() != want) {
		amigagfx_close(); w = 640; h = 480;
		if (amigagfx_open(w, h, cfg.titlebar, AMIGAGFX_BACKEND_AGA)) { fatal_("No RTG mode and no AGA screen either."); return 20;  }
	}
	logf_("display: %s %dx%d, pitch %d%s", amigagfx_backend() == AMIGAGFX_BACKEND_RTG ? "RTG" : "AGA",
	      amigagfx_game_width(), amigagfx_game_height(), amigagfx_pitch(), amigagfx_is_morphos() ? " (MorphOS)" : "");

	t0 = amigagfx_millis();
	if (sim_start(&text, &map, &cfg, NULL, err, sizeof err)) { logf_("world: %s", err); amigagfx_close(); return 20; }
	logf_("world: \"%s\" %d countries, %d provinces, modules ready: %lu ms", g_world.scenario_name, g_world.ncountry, g_world.nprov, amigagfx_millis() - t0);
	t0 = amigagfx_millis();
	text_load(&text);
	logf_("events + economy + %d texts loaded: %lu ms", text_count(), amigagfx_millis() - t0);
	if (!(mv = mv_create(&text, &map, &g_world, amigagfx_game_width() > amigagfx_game_height() ? amigagfx_game_width() : amigagfx_game_height(), err, sizeof err))) { logf_("map: %s", err); amigagfx_close(); return 20; }
	AmigaMemProbe("map view created");
	if (cfg.mapbuf) {
		if (mv_set_idcache(mv, 1)) logf_("map buffer: not enough memory - off");
		else { logf_("map buffer: on (ids + shade per tile)"); AmigaMemProbe("map buffer"); }
	}
	if (amigagfx_game_height() > 600 && amigagfx_game_width() >= 800) {      /* (ui_sidebar_h needs ui_init: 600 at scale 1000) */
		int mx, my, mw, mh;
		g_side_h = 600; g_left = 224;
		mv_set_left(mv, g_left, g_side_h);
		(void)mx; (void)my; (void)mw; (void)mh;
	}
	amigagfx_set_palette(mv_palette(mv), 0, 256);
	if (cfg.title_ms > 0) { show_title(&gfx, mv_palette(mv)); eu2_sleep_ms(cfg.title_ms); }
	t0 = amigagfx_millis();
	eui_build_inverse(mv_palette(mv), 256, g_inv);
	units_init(&text, &gfx, g_inv);
	if (cfg.anim < 0) cfg.anim = amigagfx_backend() != AMIGAGFX_BACKEND_AGA && amigagfx_backend() != AMIGAGFX_BACKEND_EHB;
	units_set_sizes(cfg.usz[0], cfg.usz[1], cfg.usz[2]);   /* 0 = the default */
	units_set_anim(cfg.anim);
	logf_("unit animation %s", cfg.anim ? "on" : "off");
	if (amigagfx_game_width() < 800) { eu2_mkdir("PROGDIR:cache640"); sprite_set_cache_dir("PROGDIR:cache640", g_inv); }
	else { eu2_mkdir("PROGDIR:cache800"); sprite_set_cache_dir("PROGDIR:cache800", g_inv); }   /* the units' zoom sizes (Lanczos once) */
	{ unsigned long u0 = amigagfx_millis(); ui_init(&gfx, &text, mv_palette(mv), g_inv, amigagfx_game_width(), amigagfx_game_height()); logf_("ui: %lu ms", amigagfx_millis() - u0); }
	logf_("palette inverse %lu ms", amigagfx_millis() - t0);
	amigagfx_set_hide_system_pointer(1); g_cur_on = 1;        /* the game draws its own cursors (SPEC_UI 10) */
	if (cfg.syspointer || cfg.titlebar) {                     /* pens 17-19 = the select cursor's three colours (pointer + bar) */
		static unsigned char pix[64 * 64];
		int pw, ph, hx, hy;
		ui_cursor_kind(UI_CUR_SELECT);
		if (ui_cursor_pointer(pix, &pw, &ph, &hx, &hy, g_ptr_rgb, &g_ptr_rgb_have) == 0) amigagfx_pointer_colours(g_ptr_rgb);
		g_sys_ptr = cfg.syspointer;
		logf_("pointer: %s, colours %02x%02x%02x %02x%02x%02x %02x%02x%02x", g_sys_ptr ? "system" : "game", g_ptr_rgb[0], g_ptr_rgb[1], g_ptr_rgb[2],
		      g_ptr_rgb[3], g_ptr_rgb[4], g_ptr_rgb[5], g_ptr_rgb[6], g_ptr_rgb[7], g_ptr_rgb[8]);
	}
	g_have_font = font_load(&g_font, &gfx, "garamond14") == 0;
	g_mv = mv; g_cfg = &cfg;
	if (cfg.select > 0 && cfg.select < g_world.nprov) { g_selected = cfg.select; g_army = mil_land_unit_at(&g_world, cfg.select, g_player); g_fleet = g_army < 0 ? mil_fleet_at(&g_world, cfg.select, g_player) : -1; }
	logf_("font %s, player %s (%d)", g_have_font ? "garamond14" : "MISSING", cfg.country, g_player);

	if (cfg.sound) { AmigaAudio_PreferAHI(cfg.ahi); sound_init(&sfxp, &text); logf_("sound: %s", AmigaAudio_IsAHI() ? "ahi.device" : "audio.device (Paula)"); }
	g_t0 = t0 = amigagfx_millis();
	autoinput_load();
front_again:
	if (cfg.menu && !cfg.bench) {                        /* main menu: scenario + country, or the quick save */
		char scen[96], tag[4];
		int r;
		snprintf(scen, sizeof scen, "%s", cfg.scenario);
		if (cfg.sound) sound_music_list("frontend");
		if (ui_front_palette()) amigagfx_set_palette(ui_front_palette(), 0, 256);   /* the menus have their own colours */
		if (cfg.intro) { g_cur_on = 0; intro_play(&gfx); }
		ui_cursor_frontend(1); g_cur_on = 1; ui_cursor_kind(UI_CUR_SELECT);
		r = frontend(&text, scen, sizeof scen, tag);
		ui_cursor_frontend(0);
		{	/* the menu's picture must not show through the map palette while the game loads: a black
			 * palette first, then a cleared screen, then the map's colours */
			static unsigned char blk[768];
			int y, gw = amigagfx_game_width(), gh = amigagfx_game_height(), k, dark = 0;
			long best = 0x7fffffffL;
			const unsigned char *mp = mv_palette(mv);
			for (k = 0; k < 256; k++) { long d = (long)mp[3 * k] + mp[3 * k + 1] + mp[3 * k + 2]; if (k != 17 && k != 18 && k != 19 && d < best) { best = d; dark = k; } }
			amigagfx_set_palette(blk, 0, 256);
			for (y = 0; y < gh; y++) memset(amigagfx_chunky() + (long)y * amigagfx_pitch(), dark, (size_t)gw);
			scr_blit(0, 0, gw, gh);
		}
		ui_front_free();
		amigagfx_set_palette(mv_palette(mv), 0, 256);
		if (r < 0) quit_now = 1;
		else if (r == 1) load_from(&text, &map, &cfg, slot_path(g_fe_load));
		else if (!script_ieq(scen, cfg.scenario)) {          /* another scenario: load it */
			snprintf(cfg.scenario, sizeof cfg.scenario, "%s", scen);
			snprintf(cfg.country, sizeof cfg.country, "%s", tag);
			sim_stop();
			if (sim_start(&text, &map, &cfg, NULL, err, sizeof err)) { logf_("world: %s", err); quit_now = 1; }
		} else {                                             /* same scenario, another country */
			if (g_player >= 0) g_world.country[g_player].human = 0;
			snprintf(cfg.country, sizeof cfg.country, "%s", tag);
			g_player = world_country(&g_world, tag);
			if (g_player >= 0) g_world.country[g_player].human = 1;
		}
		if (!quit_now) {
			apply_opts(&cfg);
			g_smsg_primed = 0;                               /* the player may have changed */
			refresh_map(mv);
			if (g_player >= 0 && g_world.country[g_player].capital > 0) {   /* the map opens on our capital */
				const Province *cp = &g_world.prov[g_world.country[g_player].capital];
				cfg.x = cp->army_x; cfg.y = cp->army_y;
			}
			logf_("game: %s as %s", g_world.scenario_name, g_player >= 0 ? g_world.country[g_player].tag : "-");
			AmigaMemProbe("game loaded");
		}
	}
	if (!g_mon) g_mon = mon_load(&g_world, &text);
	if (!g_lead) g_lead = lead_load(&g_world, &text);
	if (!g_mis) { g_mis = mis_load(&g_world, &text); if (g_mis && g_player >= 0) mis_month(g_mis, &g_world, g_player, g_dayno); }
	mon_update();
	if (g_new_game) { lead_spawn(g_lead, &g_world, g_mil, 1); g_new_game = 0; }   /* the active conquistadors / explorers arrive */
	lead_assign(g_lead, &g_world);
	if (!g_tut && !quit_now) tut_setup(&text, 0);
	if (!g_hist_loaded) {
		ledger_hist_init(&g_hist, &g_world, g_player);          /* the Ledger's graphs: one sample now, then yearly */
		ledger_hist_sample(&g_hist, &g_world, g_ec);
	}
	save_set_extra(hist_save, hist_restore);
	refresh_map(mv);                                         /* the starting map mode's colours and borders */
	if (cfg.debugend) { g_world.end = g_world.now; if (++g_world.end.month > 12) { g_world.end.month = 1; g_world.end.year++; } }
	if (!cfg.xy_set && !cfg.menu && !cfg.bench && g_player >= 0 && g_world.country[g_player].capital > 0) {   /* no menu: open on our capital too */
		const Province *cp = &g_world.prov[g_world.country[g_player].capital];
		cfg.x = cp->army_x; cfg.y = cp->army_y;
	}
	mv_set(mv, cfg.x, cfg.y, cfg.zoom);
	if (!quit_now) redraw(mv, "first");

	if (cfg.probemodes && !quit_now) {                    /* what a map mode switch costs, piece by piece (user 2026-11-10) */
		int m, r;
		mv_probe_clock(amiga_uclock_us);
		for (r = 0; r < 2; r++)
			for (m = 1; m <= MM_N; m++) {
				int mode = m % MM_N;                             /* political .. goods, then back to normal */
				unsigned long t[6], d[6], a = amiga_uclock_us(), b, c;
				mv_probe_read(t);
				g_mm.mode = mode; g_mm.focus = -1;
				g_mm.w = &g_world; g_mm.player = g_player;
				if (mode == MM_NORMAL && g_opts.opt[OPT_FOG] && !g_bench_fog) compute_vis();
				b = amiga_uclock_us();
				refresh_map(mv);                                 /* compute_vis again inside for the normal mode: counted in b..c */
				c = amiga_uclock_us();
				mv_probe_read(t);
				redraw(mv, "probe");
				mv_probe_read(d);
				logf_("probe %-12s vis %lu | refresh %lu (scales %lu) | draw: map %lu (decode %lu = tree %lu + dither %lu, borders %lu, select %lu), overlays %lu, units %lu, ui %lu, blit %lu us",
				      mapmode_name(mode), b - a, c - b, t[3], g_pr[0], d[0], d[4], d[5], d[1], d[2], g_pr[1], g_pr[2], g_pr[3], g_pr[4]);
			}
		for (r = 0; r < 8; r++) {                            /* scrolling: 4 steps east, 4 south, 32 screen px each */
			long cx, cy; int z;
			unsigned long d[6];
			mv_get(mv, &cx, &cy, &z);
			mv_probe_read(d);
			if (r < 4) mv_set(mv, cx + (32L << (z - 1)), cy, z); else mv_set(mv, cx, cy + (32L << (z - 1)), z);
			redraw(mv, "probe");
			mv_probe_read(d);
			if (r == 7) {                                    /* the scrolled layer against a fresh draw: must be identical */
				amigagfx_dump_screen("PROGDIR:p_scrolled.ppm");
				g_lay_ok = 0; redraw(mv, "probe");
				amigagfx_dump_screen("PROGDIR:p_full.ppm");
			}
			logf_("probe scroll %d: map %lu (decode %lu = tree %lu + dither %lu, borders %lu), overlays %lu, units %lu, ui %lu, blit %lu us", r, g_pr[0], d[0], d[4], d[5], d[1], g_pr[1], g_pr[2], g_pr[3], g_pr[4]);
		}
		{	/* one province changes hands: only its tiles repainted, against everything drawn from scratch */
			int p = mv_province_at(mv, amigagfx_game_width() / 2, amigagfx_game_height() / 2, amigagfx_game_width(), amigagfx_game_height());
			if (p > 0 && p < g_world.nprov && g_world.prov[p].owner >= 0) {
				int o = g_world.prov[p].owner, no = (o + 1) % g_world.ncountry;
				unsigned long d[6], a;
				g_world.prov[p].owner = no; g_world.owner_epoch++;
				mv_probe_read(d); a = amiga_uclock_us();
				refresh_map(mv); redraw(mv, "probe");
				mv_probe_read(d);
				logf_("probe owner change (province %d): %lu us, map %lu (tree %lu + dither %lu)", p, amiga_uclock_us() - a, g_pr[0], d[4], d[5]);
				amigagfx_dump_screen("PROGDIR:p_own.ppm");
				mv_flush(mv); g_lay_ok = 0; redraw(mv, "probe");
				amigagfx_dump_screen("PROGDIR:p_own_full.ppm");
				g_world.prov[p].owner = o; g_world.owner_epoch++;
				refresh_map(mv);
			}
		}
		mv_probe_clock(NULL);
	}
	if (cfg.bench) {
		int i, z;
		long cx, cy;
		unsigned long b0 = amigagfx_millis();
		for (i = 0; i < 16; i++) { mv_get(mv, &cx, &cy, &z); mv_set(mv, cx + (32L << (z - 1)), cy, z); redraw(mv, "scroll"); }
		logf_("bench: 16 scroll steps of 32 px in %lu ms", amigagfx_millis() - b0);
		for (z = 1; z <= 3; z++) {
			char snap[48];
			mv_get(mv, &cx, &cy, &i);
			mv_set(mv, cx, cy, z);
			redraw(mv, "zoom");
			redraw(mv, "cached");
			snprintf(snap, sizeof snap, "PROGDIR:snap_z%d.ppm", z);
			amigagfx_dump_screen(snap);
		}
		{ int d; unsigned long s0 = amigagfx_millis(), s1; g_selected = 386; g_paused = 0;
		  if (g_mil && g_player >= 0) {                       /* bench: send the biggest French army to Gascogne (424) */
			int i, a = -1;
			for (i = 0; i < g_world.nland; i++) if (g_world.land[i].owner == g_player && (a < 0 || g_world.land[i].infantry > g_world.land[a].infantry)) a = i;
			if (a >= 0) logf_("bench order: %ld days", mil_order_move(g_mil, a, 424, g_dayno));
		  }
		  for (d = 0; d < 30; d++) next_day();
		  s1 = amigagfx_millis();
		  for (d = 0; d < 330; d++) next_day();
		  logf_("sim: first 30 days %lu ms, then 330 days %lu ms (%lu events fired); month steps %lu ms, of that AI envoys %lu ms", s1 - s0, amigagfx_millis() - s1, g_events_fired, g_month_ms, g_env_ms);
		  { long pr[6]; mil_prof(pr); logf_("military: move %ld, build+battles %ld, rebuild %ld, sieges %ld, AI %ld ms", pr[0], pr[1], pr[2], pr[3], pr[4]); }
		  logf_("month steps: economy %lu, military %lu, peace %lu, diplomacy %lu, revolts %lu ms", g_prof_m[0], g_prof_m[1], g_prof_m[2], g_prof_m[3], g_prof_m[4]);
		  { extern long ai_prof[8]; logf_("AI: mode %ld, enemy %ld, embargo %ld, war-peace %ld, war-other %ld, peace-diplo %ld, month %ld, war-decl %ld ms",
		          ai_prof[0], ai_prof[1], ai_prof[2], ai_prof[3], ai_prof[4], ai_prof[5], ai_prof[6], ai_prof[7]); }
		  mv_set(mv, 9500, 1900, 2); redraw(mv, "bar"); amigagfx_dump_screen("PROGDIR:snap_bar.ppm"); }
		logf_("bench: done");
	} else if (!quit_now) for (;;) {
		AmigaGfxEvent ev;
		int quit = 0, moved = 0, bar = 0, want_menu = 0;
		if (cfg.testmsg && g_player >= 0) {                  /* the three kinds: Details, Goto, OK */
			cfg.testmsg = 0;
			msg_typed("STABDROP", -1, NULL, NULL, NULL);
			msg_typed("MERCSUCC", g_world.country[g_player].capital, "Paris", NULL, NULL);
			msg_typed("NEWMON", -1, "Charles VII", NULL, NULL);
		}
		static int tip_btn = UI_NONE, tip_state, tip_x, tip_y, tip_over_map;
		static unsigned long tip_t;
		long cx, cy, step;
		int z;
		mv_get(mv, &cx, &cy, &z);
		step = 64L << (z - 1);
		if (g_dlg_redraw) { g_dlg_redraw = 0; moved = 1; bar = 2; }   /* the map under a closed dialog */
		if (g_tut) {                                          /* the script's step (message 0x18, every interface update, paused or not) */
			tut_tick(g_tut);
			env_always_success(tut_always_success(g_tut) ? g_player : -1);
			ui_btn_disable(tut_ui_mask());
			if (tut_changed(g_tut) || g_tut_redraw) { g_tut_redraw = 0; mv_get(mv, &cx, &cy, &z); moved = 1; }
			if (g_tut_end) break;
		}
		while (poll_ev(&ev)) {
			if (mw_event(&ev, &moved, &bar)) continue;          /* the message windows first */
			if (ev.type == AMIGAGFX_EV_QUIT) quit = 1;
			else if (ev.type == AMIGAGFX_EV_KEY) {
				switch (ev.code) {
				case 0x45: want_menu = 1; break;                              /* Esc: the in-game menu */
				case 0x40: g_paused = !g_paused; bar = 1; break;              /* Space: pause */
				case 0x52: case 0x54: if (!g_world.tutorial) quick_save(); break;   /* F3 (original) / F5: quick save (not in a tutorial, NIEPEWNE) */
				case 0x53: case 0x58: if (!quick_load(&text, &map, &cfg)) { refresh_map(mv); moved = 1; } break;   /* F4 / F9: quick load */
				case 0x51: g_infowin = g_infowin == UI_IW_COUNTRY ? UI_IW_PROVINCE : UI_IW_COUNTRY; bar = 2; break;   /* F2: country */
				case 0x59: want_menu = 1; break;                              /* F10: the in-game menu */
				case 0x16: if (g_fleet >= 0 && mil_fleet_load(&g_world, g_fleet) > 0) { g_unload = 1; bar = 2; } break;   /* u: unload (buttons.txt) */
				case 0x55: if (tut_ui_ok(UI_LEDGER)) { ledger_window(); moved = 1; bar = 2; } break;         /* F6: the Ledger (EYR) */
				case 0x36: if (tut_ui_ok(UI_MAPMODE_NORMAL)) { set_mapmode(mv, MM_NORMAL); moved = 1; } break;      /* n p e: map modes (buttons.txt shortcuts) */
				case 0x19: if (tut_ui_ok(UI_MAPMODE_POLITICAL)) { set_mapmode(mv, MM_POLITICAL); moved = 1; } break;
				case 0x12: if (tut_ui_ok(UI_MAPMODE_RESOURCE)) { econ_mode(mv); moved = 1; } break;
				case 0x4C: cy -= step; moved = 1; tut_scrolled(g_tut); break;                      /* cursor keys */
				case 0x4D: cy += step; moved = 1; tut_scrolled(g_tut); break;
				case 0x4F: cx -= step; moved = 1; tut_scrolled(g_tut); break;
				case 0x4E: cx += step; moved = 1; tut_scrolled(g_tut); break;
				case 0x63: g_ctrl = 1; break;                                 /* Ctrl down / up (raw codes pass releases too) */
				case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:        /* 1..0: selection groups (0x534ff0) */
				case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: {
					static short grp[10][64]; static unsigned char grp_nav[10]; static int ngrp[10], last = -1;
					int gi = ev.code == 0x0A ? 0 : ev.code, k2;
					if (g_ctrl) {                                             /* Ctrl+N: store */
						grp_nav[gi] = (unsigned char)(g_nselA == 0 && g_nselF > 0);
						ngrp[gi] = grp_nav[gi] ? g_nselF : g_nselA;
						for (k2 = 0; k2 < ngrp[gi]; k2++) grp[gi][k2] = grp_nav[gi] ? g_selF[k2] : g_selA[k2];
						sound_play("if_blip"); last = -1;                     /* 0x53504f */
					} else if (ngrp[gi] > 0) {                               /* N: recall; N twice: centre on it */
						long sx = 0, sy = 0; int nn = 0;
						g_nselA = g_nselF = 0; g_army = g_fleet = -1;
						for (k2 = 0; k2 < ngrp[gi]; k2++) {
							int u2 = grp[gi][k2];
							if (!grp_nav[gi] && u2 < g_world.nland && !g_world.land[u2].dead && g_world.land[u2].owner == g_player) {
								g_selA[g_nselA++] = (short)u2; sx += g_world.prov[g_world.land[u2].location].army_x; sy += g_world.prov[g_world.land[u2].location].army_y; nn++; }
							else if (grp_nav[gi] && u2 < g_world.nnaval && g_world.naval[u2].owner == g_player) {
								g_selF[g_nselF++] = (short)u2; sx += g_world.prov[g_world.naval[u2].location].army_x; sy += g_world.prov[g_world.naval[u2].location].army_y; nn++; }
						}
						if (g_nselA) { g_army = g_selA[0]; g_selected = g_world.land[g_army].location; }
						else if (g_nselF) { g_fleet = g_selF[0]; g_selected = g_world.naval[g_fleet].location; }
						sound_play("if_click");                                 /* 0x535194: after the group is re-selected */
						g_infowin = UI_IW_PROVINCE;
						if (last == gi && nn) { cx = sx / nn; cy = sy / nn; }
						last = gi; moved = 1; bar = 2;
					}
					break; }
				case 0x60: case 0x61: g_shift = 1; break;                     /* Shift down / up: add to the selection */
				case 0xE0: case 0xE1: g_shift = 0; break;
				case 0xE3: g_ctrl = 0; break;
				case 0x5E: if (g_ctrl) { if (g_speed < 5) g_speed++; logf_("game speed %d", g_speed); break; }   /* Ctrl + (keypad): faster (manual p.12) */
				/* fall through */
				case 0x7A: case 0x0C: if (ev.code == 0x7A) logf_("key: wheel up"); if (z > 1) { z--; moved = 1; } break;   /* + / wheel up (NewMouse, WinUAE): closer */
				case 0x4A: if (g_ctrl) { if (g_speed > 1) g_speed--; logf_("game speed %d", g_speed); break; }   /* Ctrl - (keypad): slower */
				/* fall through */
				case 0x7B: case 0x0B: if (z < MV_LEVELS) { z++; moved = 1; } break;
				default:                                                      /* the wheel's own codes, whatever the driver sends (user 2026-11-11: the wheel does nothing) */
					if (ev.code >= 0x68 && ev.code < 0x80) logf_("key: rawkey 0x%02x (not a key we use)", ev.code);
					break;
				}
			} else if (ev.type == AMIGAGFX_EV_MOUSEDOWN && ev.code == AMIGAGFX_BUTTON_LEFT) {
				int hit = ui_hit(ev.x, ev.y), mx, my, mw, mh;
				ui_map_rect(&mx, &my, &mw, &mh);
				if (hit != UI_NONE && !tut_ui_ok(hit)) continue;          /* a button the tutorial switched off */
				if (g_infowin == UI_IW_DIPLO) {                   /* diplomacy windows first: the panel lies over the map */
					UiState ds;
					int h;
					memset(&ds, 0, sizeof ds);
					ds.dip_view = g_dip_view; ds.dip_target = g_dip_target; ds.dip_confirm = g_dip_confirm >= 0 ? g_dip_msg : NULL;
					h = ui_dip_hit(ev.x, ev.y, &ds);
					if (h != UD_NONE || g_dip_confirm >= 0) {
						if (h == UD_RADIO_ALL || h == UD_RADIO_REL) { g_dip_view = h == UD_RADIO_REL; dip_build(); g_mm.mode = g_dip_view ? MM_RELATIONS : MM_DIPLOMATIC; refresh_map(mv); moved = 1; }
						else if (h == UD_BACK) { dip_close(mv); moved = 1; }
						else if (h == UD_GOTO && g_dip_target >= 0 && g_world.country[g_dip_target].capital > 0) {
							const Province *cp = &g_world.prov[g_world.country[g_dip_target].capital]; cx = cp->army_x; cy = cp->army_y; moved = 1; }
						else if (h == UD_SORTREL) { g_dip_sortnat = 0; g_dip_sortasc = !g_dip_sortasc; bar = 2; }
						else if (h == UD_SORTNAT) { g_dip_sortnat = 1; g_dip_sortasc = !g_dip_sortasc; bar = 2; }
						else if (h == UD_CANCEL) { g_dip_confirm = -1; moved = 1; }
						else if (h == UD_OK) { int a = g_dip_confirm; g_dip_confirm = -1; dip_run(mv, a); moved = 1; }
						else if (h >= UD_ACT && h - UD_ACT < g_ndip_act && g_dip_act[h - UD_ACT].state == DS_ON) {
							int a = g_dip_act[h - UD_ACT].id;
							if (dip_needs_confirm(a)) { g_dip_confirm = a; dip_message(a); }
							else dip_run(mv, a);
							moved = 1;
						}
						continue;
					}
				}
				if (g_infowin == UI_IW_PEACE && (ev.x < mx && ev.y < g_side_h) && peace_click(mv, ev.x, ev.y)) { bar = 2; moved = 1; continue; }
				if (g_infowin == UI_IW_VASSAL && (ev.x < mx && ev.y < g_side_h) && g_player >= 0 && g_dip && ui_vassal_hit(ev.x, ev.y) >= 0) {
					int h = ui_vassal_hit(ev.x, ev.y);
					if (h == 101) g_infowin = UI_IW_COUNTRY;
					else if (h == 100 && g_vas_sel >= 0) {
						static short vc3[64];
						int n = diplo_vassal_candidates(g_dip, g_player, vc3, 64), r;
						if (g_vas_sel < n) {
							r = diplo_release(g_dip, g_player, vc3[g_vas_sel], g_dayno);
							logf_("grant independence: %s, %d provinces", g_world.country[vc3[g_vas_sel]].tag, r);
							if (r > 0) refresh_map(mv);
						}
						g_vas_sel = -1; g_infowin = UI_IW_COUNTRY;
					}
					else if (h < 100) g_vas_sel = h;
					bar = 2; moved = 1; continue;
				}
				if (g_infowin == UI_IW_LOANS && (ev.x < mx && ev.y < g_side_h) && g_player >= 0 && ui_loans_hit(ev.x, ev.y)) {
					int h = ui_loans_hit(ev.x, ev.y);
					long mx2 = g_world.country[g_player].treasury_m / 2000;
					if (h == 1 && g_lo_amount + 10 <= mx2) g_lo_amount += 10;
					else if (h == 11 && g_lo_amount > 10) g_lo_amount -= 10;
					else if (h == 2 && g_lo_rate < 10) g_lo_rate++;
					else if (h == 12 && g_lo_rate > 1) g_lo_rate--;
					else if (h == 3 && g_lo_months < 300) g_lo_months++;
					else if (h == 13 && g_lo_months > 1) g_lo_months--;
					else if (h == 21) { int t2 = g_lo_target; g_lo_target = -1; dip_open(mv, t2); }
					else if (h == 20 && g_dip && g_lo_amount >= 1) {
						int t2 = g_lo_target, r = diplo_offer_loan(g_dip, g_player, t2, g_lo_amount * 1000, g_lo_rate, g_lo_months, g_dayno);
						logf_("loan offer to %s: %ld at %d %% for %d months -> %d", g_world.country[t2].tag, g_lo_amount, g_lo_rate, g_lo_months, r);
						if (r == DR_ACCEPTED || r == DR_REFUSED) msg_typed(r == DR_ACCEPTED ? "LOANSACCEPT" : "LOANSREJECT", g_world.country[t2].capital, text_get(g_world.country[t2].tag), NULL, NULL);
						g_lo_target = -1; dip_open(mv, t2);
					}
					bar = 2; moved = 1; continue;
				}
				if (g_infowin == UI_IW_RELIGION && (ev.x < mx && ev.y < g_side_h) && g_player >= 0) {
					int v = 0, h = ui_religion_hit(ev.x, ev.y, &v), s0 = rel_index(&g_world, g_world.country[g_player].religion);
					if (h != URL_NONE) {
						if (h == URL_BACK) { g_infowin = UI_IW_COUNTRY; set_mapmode(mv, MM_NORMAL); }
						else if (h == URL_DEFEND) {
							if (rel_claim_defender(&g_world, g_player)) {
								char l[160];
								snprintf(l, sizeof l, text_get("WEDEFENDER_LOG"), text_get(g_world.country[g_player].tag), text_get(rel_label(-1, s0)));
								ledger_log(&g_world, l); logf_("religion: %s", l);
							}
						}
						else if (h == URL_CONVERT) {
							int t2 = g_rel_sel;
							if (t2 >= 0 && t2 != s0 && rel_convert(&g_world, g_player, t2, g_dayno)) { logf_("religion: converted to %d", t2); g_rel_sel = -1; refresh_map(mv); }
						}
						else if (h >= URL_RADIO && h < URL_SLIDER) {
							int ts[4], n = rel_targets(&g_world, s0, ts), k = h - URL_RADIO;
							g_rel_sel = k == 0 ? s0 : k - 1 < n ? ts[k - 1] : s0;
						}
						else if (h >= URL_SLIDER) {
							int sh[11], n = rel_shown(&g_world, s0, sh), k = h - URL_SLIDER;
							int r = k == 0 ? s0 : k - 1 < n ? sh[k - 1] : -1;
							if (r >= 0) rel_set_tol(&g_world, g_player, r, v);
						}
						bar = 2; moved = 1; continue;
					}
				}
				if (g_infowin == UI_IW_PURCHASE && (ev.x < mx && ev.y < g_side_h) && ui_purchase_hit(ev.x, ev.y) && g_mil && g_pu_prov > 0) {
					int h = ui_purchase_hit(ev.x, ev.y), k;
					if (h >= UPU_UP && h < UPU_UP + 3) { if (g_pu_n[h - UPU_UP] > 0) g_pu_n[h - UPU_UP]--; }
					else if (h >= UPU_DOWN && h < UPU_DOWN + 3) {        /* one more step if the place, the pool and the treasury allow */
						int c2 = h - UPU_DOWN;
						long used = g_pu_naval ? g_pu_n[0] + g_pu_n[1] + g_pu_n[2] : (g_pu_n[0] + g_pu_n[1] + g_pu_n[2]) * 1000;
						long av = g_pu_naval ? mil_recruit_avail(g_mil, g_player, g_pu_prov, 3) : mil_recruit_avail(g_mil, g_player, g_pu_prov, 0);
						long tot = 0;
						for (k = 0; k < 3; k++) tot += (g_pu_naval ? mil_ship_cost(g_mil, g_player, k) : mil_recruit_cost(g_mil, g_player, k)) * (g_pu_n[k] + (k == c2));
						if (av - used >= (g_pu_naval ? 1 : 1000) && tot <= g_world.country[g_player].treasury_m) g_pu_n[c2]++;
					}
					else if (h == UPU_CANCEL) { g_infowin = UI_IW_PROVINCE; }
					else if (h == UPU_OK) {                                     /* the order: one queue entry per step */
						sound_play("army_build");                               /* both recruit windows (SPEC_SOUND 0x0A) */
						for (k = 0; k < 3; k++) {
							long j;
							for (j = 0; j < g_pu_n[k]; j++) {
								int rc = g_pu_naval ? mil_build_ship(g_mil, g_player, g_pu_prov, k, g_dayno) : mil_recruit(g_mil, g_player, g_pu_prov, k, g_dayno);
								if (rc != MIL_RC_OK) { logf_("purchase: kind %d stopped: %d", k, rc); break; }
							}
						}
						logf_("purchase in %d: %ld / %ld / %ld (%s)", g_pu_prov, g_pu_n[0], g_pu_n[1], g_pu_n[2], g_pu_naval ? "ships" : "army");
						g_infowin = UI_IW_PROVINCE;
					}
					else if (h == UPU_BUILD) { int rc = bld_start(&g_world, g_mil, g_player, g_pu_prov, g_pu_naval ? BK_SHIPYARD : BK_CONSCRIPT, g_dayno); logf_("purchase window build: %d", rc); }
					else if (h == UPU_MERC) { int k2 = mil_hire_merc(g_mil, g_player, g_pu_prov); logf_("hire mercenaries in %d: %d", g_pu_prov, k2); if (k2 >= 0) { g_infowin = UI_IW_PROVINCE; g_army = k2; } }
					bar = 2; moved = 1; continue;
				}
				long fx, fy;
				if (hit == UI_NONE && ui_minimap_hit(ev.x, ev.y, &fx, &fy)) { cx = fx; cy = fy; moved = 1; }   /* minimap: centre there */
				else if (hit == UI_ZOOM_IN) { if (z > 1) { z--; moved = 1; } }
				else if (hit == UI_ZOOM_OUT) { if (z < MV_LEVELS) { z++; moved = 1; } }
				else if (hit == UI_MENU) want_menu = 1;
				else if (hit == UI_PAUSE) { g_paused = !g_paused; bar = 1; }
				else if (hit == UI_TREASURY) { g_infowin = g_infowin == UI_IW_BUDGET ? UI_IW_PROVINCE : UI_IW_BUDGET; bar = 2; }
				else if (hit == UI_FINANCIAL) { g_infowin = g_infowin == UI_IW_FINANCE ? UI_IW_PROVINCE : UI_IW_FINANCE; bar = 2; }
				else if (hit == UI_VIEW_LAND) { g_infowin = g_infowin == UI_IW_LANDTECH ? UI_IW_PROVINCE : UI_IW_LANDTECH; bar = 2; }
				else if (hit == UI_VIEW_NAVAL) { g_infowin = g_infowin == UI_IW_NAVALTECH ? UI_IW_PROVINCE : UI_IW_NAVALTECH; bar = 2; }
				else if (hit == UI_VP) { g_infowin = g_infowin == UI_IW_COUNTRY ? UI_IW_PROVINCE : UI_IW_COUNTRY; bar = 2; }
				else if (hit == UI_MAPMODE_NORMAL) { set_mapmode(mv, MM_NORMAL); moved = 1; }
				else if (hit == UI_MAPMODE_POLITICAL) { set_mapmode(mv, MM_POLITICAL); moved = 1; }
				else if (hit == UI_MAPMODE_RESOURCE) { econ_mode(mv); moved = 1; }
				else if (hit == UI_MISSIONARY) { set_mapmode(mv, MM_RELIGIOUS); moved = 1; }
				else if (hit == UI_DIPLOMAT) {                                 /* 0x50b520: the overview, the panel for the selected province's owner */
					if (g_infowin == UI_IW_DIPLO) dip_close(mv);
					else dip_open(mv, g_selected > 0 && g_selected < g_world.nprov ? g_world.prov[g_selected].owner : -1);
					bar = 2; moved = 1;
				}
				else if (hit == UI_MERCHANT) {          /* button 42 (0x52e2f7): window 4, the selected province's CoT */
					const short *pc = g_ec ? econ_prov_cots(g_ec) : NULL;
					set_mapmode(mv, MM_TRADE); moved = 1;
					if (g_selected > 0 && g_selected < g_world.nprov && pc && pc[g_selected] >= 0) {
						g_env_prov = g_selected; g_env_cot = pc[g_selected]; g_infowin = UI_IW_COT; g_army = g_fleet = -1;
					}
				}
				else if (hit == UI_COLONIST) { set_mapmode(mv, MM_COLONIAL); moved = 1; }
				else if (hit == UI_LEDGER) { ledger_window(); moved = 1; bar = 2; }
				else if (hit == UI_MISSIONS) { g_infowin = g_infowin == UI_IW_MISSIONS ? UI_IW_PROVINCE : UI_IW_MISSIONS; bar = 2; }
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && g_army >= 0 && g_mil && ui_siege_hit(ev.x, ev.y)) {   /* the siege window */
					static const char *const ok[4] = { "OK", NULL, NULL, NULL };
					int h = ui_siege_hit(ev.x, ev.y), loc = g_world.land[g_army].location;
					char date[64];
					sound_play(h == 1 ? "army_assault" : h == 3 ? "detach_cover" : "detach_siege");   /* siege panel buttons 0x11 / 0x13 / 0x12, at the click */
					fmt_date(date, sizeof date);
					if (h == 1) tut_btn_pressed(g_tut, TB_ASSAULT);
					if (h == 1) {
						int r = mil_assault(g_mil, g_army);
						logf_("assault: %d", r);
						if (r == MIL_AS_NOINF) dialog(g_world.prov[loc].name ? g_world.prov[loc].name : "", date, text_get("ASSAULT_NOINF"), ok);
						else if (r == MIL_AS_TECH) dialog(g_world.prov[loc].name ? g_world.prov[loc].name : "", date, "Our land technology does not allow assaults yet.", ok);
						else if (r == MIL_AS_MORALE) dialog(g_world.prov[loc].name ? g_world.prov[loc].name : "", date, "Our troops' morale is too low for an assault.", ok);
						else if (r == MIL_AS_NOSIEGE) dialog(g_world.prov[loc].name ? g_world.prov[loc].name : "", date, "Only a full siege (5000 men for each fortress level) can storm the walls.", ok);
						if (g_army >= 0 && g_world.land[g_army].dead) g_army = -1;
					} else {
						int k = mil_detach(g_mil, g_army, h == 3);
						logf_("detach %s: %d", h == 3 ? "cover" : "siege", k);
						if (k >= 0) g_army = k;
					}
					moved = 1; bar = 2;
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && (g_army >= 0 || g_fleet >= 0) && g_mil && ui_unit_cmd_hit(ev.x, ev.y)) {
					int h = ui_unit_cmd_hit(ev.x, ev.y), land = g_army >= 0;
					if (h == 1) sound_play(land ? "army_move" : "navy_move");   /* button 0x0D: create a detachment */
					if (h == 6 && land) { int p6 = g_world.land[g_army].location; logf_("attack natives in %d: %d", p6, mil_native_attack(g_mil, p6)); moved = 1; }   /* 0x2d -> REQUEST_NATIVE_ATTACK */
					else if (h == 1) { int k = land ? mil_split(g_mil, g_army) : mil_split_fleet(g_mil, g_fleet); logf_("split: %d", k); }
					else if (h == 2) { int n = land ? mil_merge(g_mil, g_army) : mil_merge_fleet(g_mil, g_fleet); logf_("merge: %d", n); }
					else if (h == 3 && g_world.tutorial) ;              /* 0x45dfb8: no disbanding in a tutorial */
					else if (h == 3) {                                  /* CONFIRMDISBAND, then the unit is gone */
						static const char *lab[4] = { "OK", NULL, NULL, NULL };
						char date[64];
						lab[1] = text_get("CANCEL");
						fmt_date(date, sizeof date);
						if (dialog(text_get("BUTTON_0204"), date, text_get("CONFIRMDISBAND"), lab) == 0) {
							if (land) mil_disband(g_mil, g_army); else mil_disband_fleet(g_mil, g_fleet);
							sound_play(land ? "army_disband" : "navy_disband");
							logf_("disband: %s %d", land ? "army" : "fleet", land ? g_army : g_fleet);
							g_army = g_fleet = -1;
						}
					}
					else if (h == 4 || h == 5) {                         /* the previous / next own unit in the same place */
						int n = land ? g_world.nland : g_world.nnaval, cur = land ? g_army : g_fleet, k, step = h == 5 ? 1 : n - 1;
						int loc = land ? g_world.land[cur].location : g_world.naval[cur].location;
						for (k = (cur + step) % n; k != cur; k = (k + step) % n) {
							if (land ? (!g_world.land[k].dead && g_world.land[k].owner == g_player && g_world.land[k].location == loc)
							         : (g_world.naval[k].owner == g_player && g_world.naval[k].location == loc && g_world.naval[k].warships + g_world.naval[k].galleys + g_world.naval[k].transports > 0)) break;
						}
						if (land) g_army = k; else g_fleet = k;
					}
					moved = 1; bar = 2;
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && g_fleet >= 0 && ui_unload_hit(ev.x, ev.y)) {
					g_unload = !g_unload; bar = 2; sound_play("army_move");                /* then click the province (fleet button 0x15) */
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && (g_army >= 0 || g_fleet >= 0) && ui_iw_close_hit(ev.x, ev.y)) {
					g_army = g_fleet = -1; bar = 2;             /* BACK of the army / fleet window: the province again */
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && g_selected > 0 && g_selected < g_world.nprov && g_army < 0 && g_fleet < 0
				         && g_world.prov[g_selected].owner != g_player && g_world.prov[g_selected].terrain != 5 && ui_iw_close_hit(ev.x, ev.y)) {
					g_selected = -1; bar = 2;                  /* BACK of the foreign-province window */
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && g_selected > 0 && g_selected < g_world.nprov && g_army < 0 && g_fleet < 0
				         && g_world.prov[g_selected].terrain == 5 && g_mil && ui_iw_close_hit(ev.x, ev.y)) {
					long cost = 0;                                                     /* SEA_PIRB: Commission Privateers */
					int r = mil_privateer_state(g_mil, g_player, g_selected, &cost);
					logf_("privateers in %s: state %d cost %ld", g_world.prov[g_selected].name ? g_world.prov[g_selected].name : "?", r, cost);
					if (r == 0 && mil_privateers(g_mil, g_player, g_selected) >= 0) { moved = 1; }
					bar = 2;
				}
				else if (g_infowin == UI_IW_PROVINCE && (ev.x < mx && ev.y < g_side_h) && g_selected > 0 && g_selected < g_world.nprov && ui_piw_hit(ev.x, ev.y) != PIW_NONE) {
					int b = ui_piw_hit(ev.x, ev.y), own = g_player >= 0 && g_world.prov[g_selected].owner == g_player;
					{	int tb = b == PIW_BARRACKS ? TB_BUILD_ARMY : b == PIW_PORT ? TB_BUILD_NAVY : b == PIW_FORT ? TB_EXPAND_FORTRESS : b == PIW_CHURCH ? TB_CHURCH : TB_NONE;
						if (tb >= 0) { if (!tut_btn_ok(g_tut, tb)) b = PIW_NONE; else tut_btn_pressed(g_tut, tb); }
					}
					logf_("piw: building %d in %s", b, g_world.prov[g_selected].name ? g_world.prov[g_selected].name : "?");
					if (b == PIW_BARRACKS && own && g_mil) { g_infowin = UI_IW_PURCHASE; g_pu_prov = g_selected; g_pu_naval = 0; memset(g_pu_n, 0, sizeof g_pu_n); moved = 1; }   /* window 0xB */
					else if (b == PIW_PORT && own && g_mil && mil_is_port(&g_world, g_selected)) { g_infowin = UI_IW_PURCHASE; g_pu_prov = g_selected; g_pu_naval = 1; memset(g_pu_n, 0, sizeof g_pu_n); moved = 1; }   /* window 0xC */
					else if (b == PIW_FORT && own && g_mil) { fort_window(g_selected); moved = 1; }         /* BUTTON_EXPAND_FORTRESS */
					else if (b == PIW_BANK && own && g_ec) {                                   /* BUTTON_LOANS: a bank loan */
						static const char *const yn[4] = { NULL, NULL, NULL, NULL };
						const char *lab[4];
						char date[64], text[200], l0[40];
						long am[5], du[5];
						int n = econ_loans(g_ec, g_player, am, du, 5);
						memcpy(lab, yn, sizeof lab);
						fmt_date(date, sizeof date);
						snprintf(l0, sizeof l0, "OK");
						if (econ_can_take_loan(g_ec, g_player))           /* the treasury window's texts (TWR_TAKELOAN / TWR_5LOANS) */
							snprintf(text, sizeof text, text_get("TWR_TAKELOAN"), (int)(econ_loan_size_of(&g_world, g_player) / 1000));
						else snprintf(text, sizeof text, "%s", text_get("TWR_5LOANS"));
						lab[0] = econ_can_take_loan(g_ec, g_player) ? l0 : NULL; lab[1] = text_get("CANCEL");
						if (dialog(text_get("HEAD_LOA"), date, text, lab) == 0 && econ_take_loan(g_ec, g_player, g_dayno) == 0) logf_("loan taken: %d now", n + 1);
						moved = 1; bar = 2;
					}
					else if (b == PIW_EMBASSY) { dip_open(mv, -1); bar = 2; moved = 1; }
					else if (b == PIW_MERCHANT) { set_mapmode(mv, MM_TRADE); moved = 1; }
					else if (b == PIW_CHURCH && own) { improvements_window(g_selected, 0); moved = 1; bar = 2; }   /* the officials and the other improvements */
					else if (b == PIW_RESOURCE && g_world.prov[g_selected].goods >= 0) { g_infowin = UI_IW_RESOURCE; g_mm.goods = g_world.prov[g_selected].goods; set_mapmode(mv, MM_GOODS); moved = 1; bar = 2; }   /* window 0x11 + the goods map */
					else logf_("piw: building %d not ported yet", b);
				}
				else if (g_infowin == UI_IW_MISSIONS && (ev.x < mx && ev.y < g_side_h) && g_mis) {
					UiState ms;
					short av[11];
					int na = mis_offers(g_mis, av, 11), h;
					memset(&ms, 0, sizeof ms); ms.nmis_av = na;
					h = ui_mis_hit(ev.x, ev.y, &ms);
					if (h == 1) { g_infowin = UI_IW_PROVINCE; bar = 2; }
					else if (h >= 100 && h - 100 < na) {                         /* the mission's description, Accept / Cancel */
						const MisRow *r = mis_row(g_mis, av[h - 100]);
						char title[96], desc[200], text[400], date[64];
						const char *lab[4] = { NULL, NULL, NULL, NULL };
						mis_title(g_mis, &g_world, av[h - 100], title, sizeof title, 0);
						mis_title(g_mis, &g_world, av[h - 100], desc, sizeof desc, 1);
						snprintf(text, sizeof text, "%s\\n%s 5 years\\n%s %d\\n%s %d", desc, text_get("MWFINISHED"), text_get("MWVPSUCCESS"), mis_vp(r->code),
						         text_get("MWVPFAILURE"), mis_vp(r->code) * 66 / 100);
						lab[0] = text_get("MWACCEPT"); lab[1] = text_get("MWCLOSE");
						fmt_date(date, sizeof date);
						if (dialog(title, date, text, lab) == 0 && mis_accept(g_mis, av[h - 100], g_dayno) == 0) logf_("mission accepted: %s", title);
						moved = 1; bar = 2;
					}
				}
				else if ((g_infowin == UI_IW_COT || g_infowin == UI_IW_COLONIST || g_infowin == UI_IW_MISSIONARY) && (ev.x < mx && ev.y < g_side_h) && ui_env_hit(ev.x, ev.y, g_infowin)) {
					int h = ui_env_hit(ev.x, ev.y, g_infowin), r = ENV_OK;
					static const char *const ok[4] = { "OK", NULL, NULL, NULL };
					char date[64], msg[200];
					const char *nm = "";
					if (h == 1) g_infowin = UI_IW_PROVINCE;
					else if (h == 2 && g_infowin == UI_IW_COT) {
						r = env_merchant_send(&g_world, g_ec, g_player, g_env_cot); nm = g_world.prov[g_world.cot[g_env_cot].prov].name;
					}
					else if (h == 2 && g_infowin == UI_IW_COLONIST) { r = env_colonist_send(&g_world, g_ec, g_mil, g_player, g_env_prov); nm = g_world.prov[g_env_prov].name; }
					else if (h == 3 && g_infowin == UI_IW_COLONIST) { r = env_trader_send(&g_world, g_ec, g_player, g_env_prov); nm = g_world.prov[g_env_prov].name; }
					else if (h == 2 && g_infowin == UI_IW_MISSIONARY) { r = env_missionary_send(&g_world, g_ec, g_player, g_env_prov, g_dayno); nm = g_world.prov[g_env_prov].name; }
					if (h >= 2) {   /* 2 send, 3 trader */
						logf_("envoy %d to %s: %d", g_infowin, nm ? nm : "?", r);
						if (g_infowin == UI_IW_COLONIST && r >= 0) {     /* Send Colonist / Trader accepted (0x43f017 / 0x43f287) */
							sound_play("colony_build");
							if (r != ENV_SENT) sound_play(r == ENV_SUCCESS ? "colony_success" : "colony_failure");
						}
						fmt_date(date, sizeof date);
						if (g_infowin == UI_IW_MISSIONARY && r >= 0) {
							snprintf(msg, sizeof msg, "Our missionary is on his way to %s. We will know in %d months.", nm ? nm : "?", env_missionary_months(&g_world, g_player, g_env_prov));
							dialog(nm ? nm : "", date, msg, ok);
						} else if (r == ENV_SENT) {                       /* on its way: the outcome is announced on arrival (env_day) */
						} else if (r == ENV_SUCCESS || r == ENV_FAILED) {     /* the game's own message types */
							const Province *ep = &g_world.prov[g_env_prov];
							const char *type = g_infowin == UI_IW_COT ? (r == ENV_SUCCESS ? "MERCSUCC" : "MERCFAIL")
							                 : h == 3 ? (r == ENV_SUCCESS ? (ep->tp > 1 ? "TPEXPANDSUCCESS" : "TPNEWSUCCESS") : "TPFAILEST")
							                 : (r == ENV_SUCCESS ? (ep->population > 100 ? "COLEXPANDSUCCESS" : "COLNEWSUCCESS") : "COLFAIL");
							msg_typed(type, g_infowin == UI_IW_COT ? g_world.cot[g_env_cot].prov : g_env_prov, nm ? nm : "?", NULL, NULL);
						} else dialog(nm ? nm : "", date, "It cannot be done.", ok);
						if (r == ENV_SUCCESS && g_infowin == UI_IW_COLONIST) refresh_map(mv);
						moved = 1;
					}
					bar = 2;
				}
				else if (g_infowin == UI_IW_BUDGET && (ev.x < mx && ev.y < g_side_h) && g_ec && g_player >= 0 && ui_budget_boost_hit(ev.x, ev.y) >= 0) {   /* one-time increase (0x83) */
					int b = ui_budget_boost_hit(ev.x, ev.y);
					if (econ_boost(g_ec, g_player, b) == 0) { logf_("budget: one-time increase of row %d", b); }
					bar = 2;
				}
				else if (g_infowin == UI_IW_BUDGET && (ev.x < mx && ev.y < g_side_h) && g_ec && g_player >= 0 && ui_budget_btn_hit(ev.x, ev.y)) {   /* 0x38 / 0x37 */
					if (ui_budget_btn_hit(ev.x, ev.y) == 1) {
						if (econ_raise_war_tax(&g_world, g_player) == 0) {   /* command 0x56 + the immediate revolt roll (0x460450, bonus 0) */
							rev_roll_country(&g_world, g_mil, g_player, 0);
							logf_("war taxes raised: 6 months");
							refresh_map(mv); moved = 1;
						}
					} else if (econ_can_take_loan(g_ec, g_player) && econ_take_loan(g_ec, g_player, g_dayno) == 0) {
						logf_("bank loan taken");
					}
					bar = 2;
				}
				else if (g_infowin != UI_IW_PROVINCE && g_infowin != UI_IW_ARMY && (ev.x < mx && ev.y < g_side_h) && ui_iw_close_hit(ev.x, ev.y)) {   /* CLOSE / BACK */
					g_infowin = g_infowin == UI_IW_DOMESTIC ? UI_IW_COUNTRY : UI_IW_PROVINCE; bar = 2;
				}
				else if ((g_infowin == UI_IW_LANDTECH || g_infowin == UI_IW_NAVALTECH) && (ev.x < mx && ev.y < g_side_h) && g_ec && g_player >= 0) {   /* maintenance level */
					int lv = ui_tech_hit(ev.x, ev.y);
					if (lv >= 0) { econ_set_maint_level(g_ec, g_player, g_infowin == UI_IW_NAVALTECH, lv); bar = 2; logf_("maintenance %s: %d", g_infowin == UI_IW_NAVALTECH ? "naval" : "land", lv); }
				}
				else if (g_infowin == UI_IW_COUNTRY && (ev.x < mx && ev.y < g_side_h) && g_player >= 0) {     /* the shield button: domestic policy (window 0x19) */
					int h = ui_country_hit(ev.x, ev.y);
					if (h == 1) { g_infowin = UI_IW_DOMESTIC; bar = 2; }
					else if (h == 3) { g_infowin = UI_IW_VASSAL; g_vas_sel = -1; bar = 2; }   /* window 0x1B */
					else if (h == 2) { g_infowin = UI_IW_RELIGION; g_rel_sel = -1; set_mapmode(mv, MM_RELIGIOUS); moved = 1; bar = 2; }   /* window 0x18 (+ the religion map, 0x478950) */
				}
				else if (g_infowin == UI_IW_DOMESTIC && (ev.x < mx && ev.y < g_side_h) && g_player >= 0) {        /* domestic window: minus / plus */
					int h = ui_domestic_hit(ev.x, ev.y);
					if (h >= 0) { domestic_step(h / 2, (h & 1) ? 1 : -1); bar = 2; }
				}
				else if (g_infowin == UI_IW_BUDGET && (ev.x < mx && ev.y < g_side_h) && g_ec && g_player >= 0) {     /* budget window: slider */
					int v, r = ui_budget_hit(ev.x, ev.y, &v);
					if (r >= 0) { econ_set_slider(g_ec, g_player, r, v); g_sl_drag = r; bar = 2; }
				}
				else if (on_map(ev.x, ev.y, mx, my)) { dragging = 1; dragdist = 0; lastx = ev.x; lasty = ev.y; band_x0 = ev.x; band_y0 = ev.y; band_on = 0; }
			} else if (ev.type == AMIGAGFX_EV_MOUSEUP && ev.code == AMIGAGFX_BUTTON_LEFT && g_sl_drag >= 0) {
				g_sl_drag = -1;
			} else if (ev.type == AMIGAGFX_EV_MOUSEUP && ev.code == AMIGAGFX_BUTTON_LEFT) {
				int was_drag = dragging, picked = 0, was_band = band_on;   /* band_end() clears band_on */
				dragging = 0;
				if (was_drag && was_band) {                              /* the band: our armies in it, else our fleets (0x52bd42) */
					int bmx, bmy, bmw, bmh, nav = 0, n, k;
					short got[64];
					band_end();
					ui_map_rect(&bmx, &bmy, &bmw, &bmh);
					n = g_player >= 0 ? units_in_rect(&g_world, mv, (band_x0 < ev.x ? band_x0 : ev.x) - bmx, (band_y0 < ev.y ? band_y0 : ev.y) - bmy,
					                                 (band_x0 > ev.x ? band_x0 : ev.x) - bmx, (band_y0 > ev.y ? band_y0 : ev.y) - bmy, g_player, got, 64, &nav) : 0;
					if (!g_shift) { g_nselA = g_nselF = 0; g_army = g_fleet = -1; }
					for (k = 0; k < n; k++) {
						if (!nav && g_nselF == 0 && g_nselA < 64) { int j, dup = 0; for (j = 0; j < g_nselA; j++) if (g_selA[j] == got[k]) dup = 1; if (!dup) g_selA[g_nselA++] = got[k]; }
						else if (nav && g_nselA == 0 && g_nselF < 64) { int j, dup = 0; for (j = 0; j < g_nselF; j++) if (g_selF[j] == got[k]) dup = 1; if (!dup) g_selF[g_nselF++] = got[k]; }
					}
					if (g_nselA) { g_army = g_selA[0]; g_fleet = -1; g_selected = g_world.land[g_army].location; g_infowin = UI_IW_PROVINCE; }   /* band selection: silent */
					else if (g_nselF) { g_fleet = g_selF[0]; g_army = -1; g_selected = g_world.naval[g_fleet].location; g_infowin = UI_IW_PROVINCE; }
					logf_("band: %d %s selected", n, nav ? "fleets" : "armies");
					moved = 1; bar = 2;
				}
				else if (was_drag && g_player >= 0 && !g_unload && g_mm.mode != MM_TRADE && g_mm.mode != MM_COLONIAL && g_mm.mode != MM_DIPLOMATIC && g_mm.mode != MM_RELATIONS && g_infowin != UI_IW_DIPLO) {
					/* a click on one of our unit sprites (0x530410): the biggest of ours there; again on the
					 * stack with one selected: the next one (stack cycling) */
					int pmx, pmy, pmw, pmh, nav = 0, loc;
					ui_map_rect(&pmx, &pmy, &pmw, &pmh);
					loc = units_pick(&g_world, mv, ev.x - pmx, ev.y - pmy, g_player, &nav);
					if (loc > 0) {
						int k, best = -1, cur = nav ? g_fleet : g_army, n = nav ? g_world.nnaval : g_world.nland;
						long bm = -1;
						if (!nav && cur >= 0 && g_nselA == 1 && g_world.land[cur].location == loc) {
							for (k = (cur + 1) % n; k != cur; k = (k + 1) % n) if (!g_world.land[k].dead && g_world.land[k].owner == g_player && g_world.land[k].location == loc) { best = k; break; }
						} else if (nav && cur >= 0 && g_nselF == 1 && g_world.naval[cur].location == loc) {
							for (k = (cur + 1) % n; k != cur; k = (k + 1) % n) if (g_world.naval[k].owner == g_player && g_world.naval[k].location == loc && g_world.naval[k].warships + g_world.naval[k].galleys + g_world.naval[k].transports > 0) { best = k; break; }
						}
						if (best < 0) for (k = 0; k < n; k++) {
							long m2;
							if (!nav) { const LandUnit *l = &g_world.land[k]; if (l->dead || l->owner != g_player || l->location != loc) continue; m2 = l->infantry + l->cavalry + l->artillery; }
							else { const NavalUnit *f2 = &g_world.naval[k]; if (f2->owner != g_player || f2->location != loc) continue; m2 = f2->warships + f2->galleys + f2->transports; if (m2 <= 0) continue; }
							if (m2 > bm) { bm = m2; best = k; }
						}
						if (best >= 0) {
							if (g_shift && !nav && g_nselF == 0) {             /* Shift: add / remove */
								int j, at = -1;
								for (j = 0; j < g_nselA; j++) if (g_selA[j] == best) at = j;
								if (at >= 0) g_selA[at] = g_selA[--g_nselA]; else if (g_nselA < 64) g_selA[g_nselA++] = (short)best;
								g_army = g_nselA ? g_selA[0] : -1;
							} else if (!nav) { g_nselF = 0; g_fleet = -1; g_selA[0] = (short)best; g_nselA = 1; g_army = best; sound_play("army_select"); }
							else { g_nselA = 0; g_army = -1; g_selF[0] = (short)best; g_nselF = 1; g_fleet = best; sound_play("navy_select"); }
							g_selected = loc; g_infowin = UI_IW_PROVINCE; bar = 2; moved = 1; picked = 1;
							logf_("picked %s %d in %d", nav ? "fleet" : "army", best, loc);
						}
					}
				}
				if (was_drag && !was_band && !picked) {                  /* a click on the map, not a drag: select */
					int mx, my, mw, mh, id;
					ui_map_rect(&mx, &my, &mw, &mh);
					id = mv_province_at(mv, ev.x - mx, ev.y - my, mw, mh);
					logf_("map click %d,%d: province %d (mode %d)", ev.x, ev.y, id, g_mm.mode);
					if (id > 0 && id < g_world.nprov && g_player >= 0 && !g_unload && (g_mm.mode == MM_TRADE || g_mm.mode == MM_COLONIAL ||
					    (g_mm.mode == MM_RELIGIOUS && g_world.prov[id].owner == g_player && g_world.prov[id].religion != g_world.country[g_player].religion))) {
						const short *pc = g_ec ? econ_prov_cots(g_ec) : NULL;            /* the envoy windows (SPEC_ENVOYS) */
						g_selected = id; g_env_prov = id; bar = 2;
						if (g_mm.mode == MM_TRADE) { int was = g_infowin == UI_IW_COT; g_env_cot = pc ? pc[id] : -1; g_infowin = g_env_cot >= 0 ? UI_IW_COT : UI_IW_PROVINCE; if (was && g_env_cot >= 0) sound_play("if_click"); }   /* 0x515358 */
						else if (g_mm.mode == MM_COLONIAL) g_infowin = g_world.prov[id].terrain < 5 ? UI_IW_COLONIST : UI_IW_PROVINCE;
						else g_infowin = UI_IW_MISSIONARY;
					} else
if (id > 0 && id < g_world.nprov && g_unload && g_fleet >= 0 && g_mil) {   /* unload the fleet's troops here */
						int n = mil_disembark(g_mil, g_fleet, id);
						g_unload = 0; moved = 1; bar = 2;
						logf_("unload fleet %d into %d: %d units", g_fleet, id, n);
						if (n) { g_selected = id; g_army = mil_land_unit_at(&g_world, id, g_player); g_fleet = -1; sound_play(move_sound()); }
						else { static const char *const ok[4] = { "OK", NULL, NULL, NULL }; char date[64]; fmt_date(date, sizeof date);
						       dialog(text_get("BUTTON_0213"), date, text_get("NOUNLOADSPACE"), ok); }
					} else
if (id > 0 && id < g_world.nprov && (g_infowin == UI_IW_DIPLO || g_mm.mode == MM_DIPLOMATIC || g_mm.mode == MM_RELATIONS)) {   /* 0x50ad5a: the action panel for the owner */
						g_selected = id;
						dip_open(mv, g_world.prov[id].owner); bar = 2; moved = 1;
						if (g_world.prov[id].owner >= 0) sound_play("if_click");   /* 0x52bcd7 */
					} else if (id > 0 && id < g_world.nprov) {
						g_selected = id; g_infowin = UI_IW_PROVINCE; bar = 2;
						if (g_army >= 0 || g_fleet >= 0) moved = 1;          /* the ring and the route go */
						g_army = g_fleet = -1; g_nselA = g_nselF = 0;         /* a province click selects the province (0x52c3f2) */
						if (g_fleet >= 0) logf_("fleet selected: %s, %d warships %d galleys %d transports", g_world.naval[g_fleet].name, g_world.naval[g_fleet].warships, g_world.naval[g_fleet].galleys, g_world.naval[g_fleet].transports);
						sound_play("if_click");                               /* a land province / sea zone (0x52c418..0x52c563) */
						if (g_army >= 0) logf_("army selected: %s, %ld inf %ld cav %ld art", g_world.land[g_army].name,
						                       g_world.land[g_army].infantry, g_world.land[g_army].cavalry, g_world.land[g_army].artillery);
					}
				}
			} else if (ev.type == AMIGAGFX_EV_MOUSEDOWN && ev.code == AMIGAGFX_BUTTON_RIGHT) {
				int mx, my, mw, mh, id;
				ui_map_rect(&mx, &my, &mw, &mh);
				id = mv_province_at(mv, ev.x - mx, ev.y - my, mw, mh);
				if (ev.x >= mx && ev.y >= my + mh - ui_log_height()) id = 0;   /* on the message log panel (0x577610) */
				if (id > 0 && id < g_world.nprov && g_army >= 0 && g_mil && !g_world.land[g_army].dead && g_world.prov[id].terrain == 5
				    && mil_fleet_at(&g_world, id, g_player) >= 0) {                       /* an army onto our fleet in that sea zone */
					int f = mil_fleet_at(&g_world, id, g_player), rc = mil_embark(g_mil, g_army, f);
					logf_("embark %d -> fleet %d: %d", g_army, f, rc);
					if (rc == MIL_RC_OK) { sound_play(move_sound()); g_army = -1; g_fleet = f; g_selected = id; }
					else { static const char *const ok[4] = { "OK", NULL, NULL, NULL }; char date[64]; fmt_date(date, sizeof date);
					       dialog(g_world.naval[f].name ? g_world.naval[f].name : "", date, rc == MIL_RC_FULL ? "The fleet cannot carry this army." : "The army must stand next to the fleet, and the fleet must be at sea and not sailing.", ok); }
					moved = 1; bar = 2;
				} else
if (id > 0 && id < g_world.nprov && g_army >= 0 && g_mil && !g_world.land[g_army].dead) {
					long days = (g_shift ? mil_order_append : mil_order_move)(g_mil, g_army, id, g_dayno);         /* right click: move order, to every selected army */
					int k;
					for (k = 0; k < g_nselA; k++) if (g_selA[k] != g_army && !g_world.land[g_selA[k]].dead) (g_shift ? mil_order_append : mil_order_move)(g_mil, g_selA[k], id, g_dayno);   /* Shift: a waypoint */
					units_set_time(amigagfx_millis()); units_order_anim(ev.x - mx, ev.y - my, days >= 0);
					logf_("order: %s to %s - %ld days", g_world.land[g_army].name, g_world.prov[id].name, days);
					if (days < 0 && !WORLD_KNOWN(&g_world, g_player, id)) {           /* terra incognita (0x530f70) */
						static const char *const ok[4] = { "OK", NULL, NULL, NULL };
						char date[64];
						fmt_date(date, sizeof date);
						dialog(g_world.prov[id].name ? g_world.prov[id].name : "", date, text_get("LANDMORON"), ok);
					}
					sound_play(days >= 0 ? move_sound() : "if_error");                    /* 0x531276 / 0x531342 */
					moved = 1; bar = 2;                                                  /* the destination mark */
				} else if (id > 0 && id < g_world.nprov && g_fleet >= 0 && g_mil && g_fleet < g_world.nnaval) {
					long days = mil_order_fleet(g_mil, g_fleet, id, g_dayno);         /* right click: sail order */
					{ int k; for (k = 0; k < g_nselF; k++) if (g_selF[k] != g_fleet) mil_order_fleet(g_mil, g_selF[k], id, g_dayno); }
					units_set_time(amigagfx_millis()); units_order_anim(ev.x - mx, ev.y - my, days >= 0);
					logf_("order: fleet %s to %s - %ld days", g_world.naval[g_fleet].name, g_world.prov[id].name, days);
					if (days < 0 && !WORLD_KNOWN(&g_world, g_player, id)) {
						static const char *const ok[4] = { "OK", NULL, NULL, NULL };
						char date[64];
						fmt_date(date, sizeof date);
						dialog(g_world.prov[id].name ? g_world.prov[id].name : "", date, text_get("NAVALMORON"), ok);
					}
					sound_play(days >= 0 ? "navy_move" : "if_error");
					moved = 1; bar = 2;
				} else if (id > 0 && id < g_world.nprov && g_player >= 0 && g_world.prov[id].terrain < 5 && WORLD_KNOWN(&g_world, g_player, id)) {
					/* the right-click menu of a province, as eu2.exe 0x52a190: on our own province every entry is
					 * there and the rules only grey them (0x5239c0 +0x242); no menu at sea or in terra incognita */
					const Province *p = &g_world.prov[id];
					enum { R_VIEW, R_ENTER, R_ARMY, R_NAVY, R_FORT, R_MANU, R_SHIPYARD, R_BARRACK, R_BAILIFF, R_COURT, R_MAYOR, R_COL, R_TRADER,
					       R_COUNTRY, R_WAR, R_PEACE, R_DIPLO, R_ATRADE, R_EMBARGO, R_LOAN, R_COT, R_SEP };
					static const char *const key[R_SEP] = { "RCDM_VIEWPROVINCE", "RCDM_ENTERPROVINCE", "RCDM_BUILDARMY", "RCDM_BUILDNAVY", "RCDM_EXPANDFORT",
					    "RCDM_BUILDMANUFAC", "RCDM_BUILDSHIPYARD", "RCDM_BUILDBARRACK", "RCDM_BUILDBAILIFF", "RCDM_BUILDCOURTHOUSE", "RCDM_BUILDCITYRIGHTS",
					    "RCDM_COLONIST", "RCDM_TRADER", "RCDM_VIEWCOUNTRY", "RCDM_DECLAREWAR", "RCDM_OFFERPEACE", "RCDM_SENDDIPLO", "RCDM_ALLOWTRADE",
					    "RCDM_REFUSETRADE", "RCDM_OFFERLOAN", "RCDM_SENDMERCHANT" };
					const char *lab[24];
					unsigned char fl[24];
					int act[24], n = 0, pick, own = p->owner == g_player, cot = g_ec ? econ_prov_cots(g_ec)[id] : -1;
					int ctl = own && p->controller == g_player, free_ = ctl && (!g_mil || mil_besieger(g_mil, id) < 0) && p->population > 0;
					const Country *me = &g_world.country[g_player];
					logf_("province %d %s, owner %s, %s, %s", id, p->name ? p->name : "?",
					      p->owner >= 0 ? g_world.country[p->owner].tag : "-", world_name(&g_world, p->religion), world_name(&g_world, p->culture));
#define ADD(a, on) do { if (n < 24) { act[n] = (a); fl[n] = (a) == R_SEP ? PM_SEP : (on) ? 0 : PM_GREY; lab[n] = (a) == R_SEP ? "" : text_get(key[a]); n++; } } while (0)
#define BLD(k) (g_mil && bld_can(&g_world, g_mil, g_player, id, (k)) == BLD_OK)
					if (own) {                                       /* table A: codes 5 / 6 / 7 (0x52ac43) */
						int colony = p->population < 1000 && !p->tp, k, manu = 0;
						for (k = 0; k < 5 && free_; k++) { int r = g_mil ? bld_can(&g_world, g_mil, g_player, id, BK_MANU1 + k) : BLD_BADPROV; if (r == BLD_OK || r == BLD_MONEY || r == BLD_TECH) manu = 1; }   /* 0x45b950: no tech / cost test here */
						ADD(R_VIEW, 1); ADD(R_ENTER, ctl); ADD(R_SEP, 0);
						ADD(R_ARMY, free_ && me->treasury_m >= 0);
						ADD(R_NAVY, free_ && mil_is_port(&g_world, id) && me->treasury_m > 0);   /* NIEPEWNE: the ship price test simplified */
						ADD(R_FORT, free_ && !colony && g_mil && p->fort < mil_fort_max(&g_world, g_player));
						ADD(R_SEP, 0);
						ADD(R_MANU, free_ && !colony && manu); ADD(R_SHIPYARD, BLD(BK_SHIPYARD)); ADD(R_BARRACK, BLD(BK_CONSCRIPT));
						ADD(R_BAILIFF, BLD(BK_TAXCOL)); ADD(R_COURT, BLD(BK_JUDGE)); ADD(R_MAYOR, BLD(BK_GOVERNOR));
						if (colony || p->tp) { ADD(R_SEP, 0); ADD(R_COL, g_mil && env_colonist_can(&g_world, g_mil, g_player, id) == ENV_OK); }
						if (p->tp) ADD(R_TRADER, env_trader_can(&g_world, g_player, id) == ENV_OK);
					} else if (p->owner < 0) {                       /* table B: code 8 */
						ADD(R_COL, g_mil && env_colonist_can(&g_world, g_mil, g_player, id) == ENV_OK);
						ADD(R_TRADER, env_trader_can(&g_world, g_player, id) == ENV_OK);
					} else {                                         /* table C: code 9; the rest only when their capital is known (0x52b5b8) */
						int o = p->owner, cap = g_world.country[o].capital;
#define DS(a) (g_dip && diplo_action_state(g_dip, g_player, o, (a), g_dayno) == DS_ON)
						ADD(R_VIEW, 1);
						if (cap > 0 && cap < g_world.nprov && WORLD_KNOWN(&g_world, g_player, cap)) {
							ADD(R_COUNTRY, 1); ADD(R_SEP, 0);
							ADD(R_WAR, DS(DA_WAR) && !g_tut); ADD(R_PEACE, mil_at_war(&g_world, g_player, o)); ADD(R_SEP, 0);
							ADD(R_DIPLO, me->diplomats_m >= 1000); ADD(R_ATRADE, DS(DA_ALLOWTRADE)); ADD(R_EMBARGO, DS(DA_EMBARGO));
							ADD(R_LOAN, me->diplomats_m >= 1000);
						}
#undef DS
					}
					if (cot >= 0 && g_world.cot[cot].prov == id) {   /* NIEPEWNE: the original has this on the CoT marker's own menu */
						ADD(R_SEP, 0); ADD(R_COT, env_merchant_can(&g_world, g_ec, g_player, cot) == ENV_OK);
					}
#undef BLD
#undef ADD
					pick = n ? popmenu(ev.x, ev.y, lab, fl, n) : -1;
					if (pick >= 0) {
						int a2 = act[pick];
						g_selected = id; g_army = g_fleet = -1; bar = 2; moved = 1;
						switch (a2) {
						case R_VIEW: case R_ENTER: g_infowin = UI_IW_PROVINCE; break;   /* windows 0xA / 9: ours is one province window */
						case R_ARMY: case R_NAVY: g_infowin = UI_IW_PURCHASE; g_pu_prov = id; g_pu_naval = a2 == R_NAVY; memset(g_pu_n, 0, sizeof g_pu_n); break;
						case R_FORT: g_infowin = UI_IW_PROVINCE; fort_window(id); break;
						case R_MANU: g_infowin = UI_IW_PROVINCE; improvements_window(id, -1); break;
						case R_SHIPYARD: improvements_window(id, BK_SHIPYARD); break;
						case R_BARRACK: improvements_window(id, BK_CONSCRIPT); break;
						case R_BAILIFF: g_infowin = UI_IW_PROVINCE; improvements_window(id, BK_TAXCOL); break;
						case R_COURT: g_infowin = UI_IW_PROVINCE; improvements_window(id, BK_JUDGE); break;
						case R_MAYOR: g_infowin = UI_IW_PROVINCE; improvements_window(id, BK_GOVERNOR); break;
						case R_COL: case R_TRADER: g_env_prov = id; g_infowin = UI_IW_COLONIST; break;
						case R_COT: g_env_prov = id; g_env_cot = cot; g_infowin = UI_IW_COT; break;
						case R_COUNTRY: case R_DIPLO: dip_open(mv, p->owner); break;
						case R_WAR: dip_open(mv, p->owner); if (g_dip_target >= 0) { g_dip_confirm = DA_WAR; dip_message(DA_WAR); } break;
						case R_PEACE: { int e2 = p->owner; if (peace_open(e2)) { g_mm.mode = MM_NORMAL; g_mm.focus = -1; refresh_map(mv); } } break;
						case R_ATRADE: case R_EMBARGO: case R_LOAN: {
							int da = a2 == R_ATRADE ? DA_ALLOWTRADE : a2 == R_EMBARGO ? DA_EMBARGO : DA_LOAN;
							dip_open(mv, p->owner);
							if (g_dip_target >= 0) { if (dip_needs_confirm(da)) { g_dip_confirm = da; dip_message(da); } else dip_run(mv, da); }
						} break;
						default: break;
						}
					} else moved = 1;
				}
			} else if (ev.type == AMIGAGFX_EV_MOUSEMOVE && !dragging && g_sl_drag < 0) {   /* rollover help over the sidebar buttons */
				g_mouse_x = ev.x; g_mouse_y = ev.y;
				int mx, my, mw, mh, hb;
				if (g_infowin == UI_IW_DIPLO && g_dip_target >= 0 && g_dip_confirm < 0) {   /* the action under the mouse turns white */
					UiState ds; int h, nh;
					memset(&ds, 0, sizeof ds);
					ds.dip_view = g_dip_view; ds.dip_target = g_dip_target;
					h = ui_dip_hit(ev.x, ev.y, &ds);
					nh = h >= UD_ACT ? h - UD_ACT : -1;
					if (nh != g_dip_hover) { g_dip_hover = nh; moved = 1; }
				}
				ui_map_rect(&mx, &my, &mw, &mh);
				hb = (ev.x < mx && ev.y < g_side_h) ? ui_hit(ev.x, ev.y) : UI_NONE;
				if (hb == UI_NONE && g_player >= 0 && ui_bar_item(ev.x, ev.y) >= 0) hb = 1000 + ui_bar_item(ev.x, ev.y);
				if (hb == UI_NONE && ev.x < mx) {                    /* the battle / peace windows' help */
					char d1[8], d2[8];
					int z = ui_window_tip(ev.x, ev.y, d1, 1, d2, 1);
					if (z >= 0) hb = 4000 + z;
				}
				if (hb == UI_NONE && (ev.x < mx && ev.y < g_side_h) && g_infowin == UI_IW_PROVINCE && g_army < 0 && g_fleet < 0 && ui_piw_hit(ev.x, ev.y) != PIW_NONE)
					hb = 3000 + ui_piw_hit(ev.x, ev.y);                    /* a building of the province window */
				if (hb == UI_NONE && (ev.x < mx && ev.y < g_side_h) && g_infowin == UI_IW_PROVINCE && g_army < 0 && g_fleet < 0 && g_selected > 0 && g_selected < g_world.nprov
				    && g_world.prov[g_selected].terrain == 5 && ui_iw_close_hit(ev.x, ev.y))
					hb = 3100;                                             /* the sea window's Commission Privateers */
				if (hb == UI_NONE && on_map(ev.x, ev.y, mx, my) && ev.y > 32 && !(g_infowin == UI_IW_DIPLO && g_dip_target >= 0 && ev.x < mx + 224)) {   /* a province on the map */
					int id = mv_province_at(mv, ev.x - mx, ev.y - my, mw, mh);
					if (id > 0 && id < g_world.nprov) hb = 2000 + id;
				}
				if (hb != tip_btn) { tip_btn = hb; tip_t = amigagfx_millis(); }
				tip_x = ev.x; tip_y = ev.y;
			} else if (ev.type == AMIGAGFX_EV_MOUSEMOVE && g_sl_drag >= 0) {           /* dragging a budget slider */
				int v, r = ui_budget_hit(ev.x, ev.y, &v);
				if (r == g_sl_drag && g_ec && g_player >= 0) { econ_set_slider(g_ec, g_player, r, v); bar = 2; }
			} else if (ev.type == AMIGAGFX_EV_MOUSEMOVE && dragging) {   /* left-drag: the rubber band (0x526fe4), no drag-scroll */
				g_mouse_x = ev.x; g_mouse_y = ev.y;
				dragdist = abs(ev.x - band_x0) + abs(ev.y - band_y0);
				if (dragdist > 16 || band_on) { band_on = 1; band_draw(band_x0, band_y0, ev.x, ev.y); }
				lastx = ev.x; lasty = ev.y;
			}
		}
		if (quit) break;
		if (want_menu) {                                      /* MENU button / Esc */
			int a = game_menu(), sl;
			if (g_world.tutorial && (a == 1 || a == 2)) ;          /* 0x55fb17: Save and Load are off in a tutorial */
			else if (a == 1 && (sl = slot_window(1)) >= 0) save_to(slot_path(sl));
			else if (a == 2 && (sl = slot_window(0)) >= 0 && !load_from(&text, &map, &cfg, slot_path(sl))) refresh_map(mv);
			else if (a == 3) { g_to_menu = 1; break; }
			else if (a == 5) break;
			else if (a == 4) msgset_window();
			mv_set(mv, cx, cy, z); moved = 1;
		}
		sfx_service();
		{	/* the playlist follows the century (0x4c93e0): clamp(year / 100 - 13, 0, 4) -> frontend, 14.txt .. 17.txt;
			   the list is changed for the next track only (sound_music_list) */
			static int last_c;
			int cen = g_world.now.year / 100;
			if (cen < 14) cen = 14;
			if (cen > 17) cen = 17;
			if (cen != last_c && cfg.sound) { char l[8]; last_c = cen; snprintf(l, sizeof l, "%d", cen); sound_music_list(l); }
		}
		if (!g_paused && amigagfx_millis() - lastday >= (unsigned long)day_ms[g_speed - 1]) {   /* game speed 1..5, 3 = 100 ms per day (NIEPEWNE) */
			unsigned long ep = g_world.owner_epoch, sig = 0;
			int k, k2;
			next_day(); lastday = amigagfx_millis(); bar = 1;
			if (cfg.debugwar[0] && g_dip && g_player >= 0) {        /* test hook: a war declared on us */
				static int done;
				int e = world_country(&g_world, cfg.debugwar);
				if (++done == 2 && e >= 0) logf_("debugwar: %s -> %d", cfg.debugwar, diplo_declare_war(g_dip, e, g_player, g_dayno));
			}
			if (cfg.debugask[0] && g_player >= 0) {               /* test hook: an AI proposal */
				static int adone;
				if (++adone == 2 && !strcmp(cfg.debugask, "PEACEPROP")) {   /* the AI's peace offer: the player's first war, its leader demanding */
					int wi = -1, k2;
					for (k2 = 0; k2 < g_world.nwar && wi < 0; k2++) if (peace_side(&g_world, k2, g_player)) wi = k2;
					if (wi >= 0) { short pv[1]; pv[0] = g_world.country[g_player].capital; logf_("debugask: PEACEPROP -> %d", peace_offer(NULL, wi, peace_side(&g_world, wi, g_player) == 1 ? 2 : 1, pv, 1, 50000L)); }
				} else if (adone == 2) logf_("debugask: %s -> %d", cfg.debugask, diplo_ask(NULL, cfg.debugask, world_country(&g_world, "FRA"), world_country(&g_world, "BUR"), world_country(&g_world, "FRA")));
			}
			if (cfg.debugpeace[0] && g_player >= 0) {             /* test hook: the peace window */
				static int pdone;
				int e = world_country(&g_world, cfg.debugpeace);
				if (++pdone == 3 && e >= 0 && peace_open(e)) { g_paused = 1; bar = 2; logf_("debugpeace: %s page %d", cfg.debugpeace, g_pw_page); }
			}
			if (cfg.debugnaval && cfg.debugwar[0] && g_mil && g_player >= 0) {   /* test hook: a naval battle */
				static int ndone;
				int e = world_country(&g_world, cfg.debugwar), fo = -1, fe = -1, z = -1, j;
				if (++ndone == 3 && e >= 0) {
					for (j = 0; j < g_world.nnaval; j++) {
						const NavalUnit *f = &g_world.naval[j];
						if (f->warships + f->galleys + f->transports <= 0) continue;
						if (f->owner == g_player && fo < 0) fo = j;
						if (f->owner == e && fe < 0) fe = j;
					}
					if (fo >= 0 && fe >= 0) {
						short nb[32];
						int n = mil_adjacent(g_mil, g_world.naval[fo].location, nb, 32), q;
						if (g_world.prov[g_world.naval[fo].location].terrain >= 5) z = g_world.naval[fo].location;
						for (q = 0; q < n && z < 0; q++) if (nb[q] > 0 && nb[q] < g_world.nprov && g_world.prov[nb[q]].terrain >= 5) z = nb[q];
						if (z > 0) { g_world.naval[fo].location = g_world.naval[fe].location = (short)z; g_world.naval[fo].npath = g_world.naval[fe].npath = 0; }
					}
					logf_("debugnaval: fleets %d / %d into %d", fo, fe, z);
				}
			}
			if (cfg.debugmanu && g_mil && g_player >= 0 && g_world.country[g_player].capital > 0) {   /* test hook: the manufactory panel */
				int cap = g_world.country[g_player].capital, kind;
				cfg.debugmanu = 0;
				g_selected = cap; g_infowin = UI_IW_PROVINCE;
				kind = manufactory_panel(cap);
				logf_("debugmanu: picked %d", kind);
			}
			if (cfg.debugsel && g_mil && g_player >= 0) {           /* test hook: show the battle window */
				static MilBattleView tv;
				for (k = 0; k < g_world.nnaval; k++)
					if (g_world.naval[k].owner == g_player && g_world.naval[k].warships + g_world.naval[k].galleys + g_world.naval[k].transports > 0
					    && mil_battle_view(g_mil, g_world.naval[k].location, &tv) == MBV_NAVAL) {
						if (g_fleet != k) { g_fleet = k; g_army = -1; g_selected = g_world.naval[k].location; g_infowin = UI_IW_PROVINCE; logf_("debugsel: fleet %d in %d", k, g_selected); if (cfg.debugsel == 1 || cfg.debugsel == 3) g_paused = 1; }
						break;
					}
				for (k = 0; k < g_world.nland && !(g_fleet >= 0 && cfg.debugnaval); k++)
					if (!g_world.land[k].dead && g_world.land[k].owner == g_player && mil_battle_view(g_mil, g_world.land[k].location, &tv) != MBV_NONE) {
						if (g_army != k) { g_army = k; g_fleet = -1; g_selected = g_world.land[k].location; g_infowin = UI_IW_PROVINCE; logf_("debugsel: army %d in %d kind %d", k, g_selected, tv.kind); if (cfg.debugsel == 1) g_paused = 1; }
						break;
					}
			}
			check_messages();
			check_other_wars();                              /* (before the state check: it re-primes on a new / loaded game) */
			check_state_messages();
			if (g_player >= 0 && !g_game_over && !g_world.tutorial && (k = victory_check(&cfg)) != -2) {   /* the victory condition (FEVIC_*) */
				static const char *const ok[4] = { "OK", NULL, NULL, NULL };
				char t[200], date[64];
				int rank = 1;
				for (k2 = 0; k2 < g_world.ncountry; k2++) if (k2 != g_player && g_world.country[k2].nprov_owned > 0 && g_world.country[k2].vp > g_world.country[g_player].vp) rank++;
				g_game_over = 1; g_paused = 1;
				snprintf(t, sizeof t, text_get("GAME_NORMAL"), (int)g_world.country[g_player].vp, text_get((k >= 0 ? k == g_player : rank == 1) ? "GAME_WON" : "GAME_LOST"));
				if (k >= 0 && k != g_player) { size_t l = strlen(t); snprintf(t + l, sizeof t - l, " (%s)", text_get(g_world.country[k].tag)); }
				else if (k < 0) { size_t l = strlen(t); snprintf(t + l, sizeof t - l, " (#%d)", rank); }
				fmt_date(date, sizeof date);
				logf_("game over: %s", t);
				dialog(text_get("EYRVP"), date, t, ok);
				moved = 1;
			}
			{	/* AUTOSAVE: on the 1st of January every 1 / 3 / 5 / 10 years (FEOPT_AUTOSAVE) */
				int ny = opts_autosave_years(&g_opts);
				if (ny > 0 && !cfg.bench && !g_world.tutorial && g_world.now.month == 1 && g_world.now.day == 0 && g_world.now.year % ny == 0) {
					int rc;
					eu2_mkdir("PROGDIR:save");
					rc = save_game(AUTOSAVE, &g_world, g_ec, g_ev, g_mil, g_peace, g_dip);
					logf_("autosave %d: %s", g_world.now.year, rc ? "FAILED" : "ok");
				}
			}
			if (g_world.owner_epoch != ep) { refresh_map(mv); moved = 1; }
			else if (g_mm.mode == MM_DIPLOMATIC || g_mm.mode == MM_RELATIONS) {               /* wars / alliances changed: recolour */
				static int lw = -1, la = -1;
				if (g_world.nwar != lw || g_world.nally != la) { lw = g_world.nwar; la = g_world.nally; refresh_map(mv); moved = 1; }
			}
			for (k = 0; k < g_world.nland; k++) sig = sig * 31 + (unsigned long)g_world.land[k].location * 2 + g_world.land[k].dead;
			if (sig != g_mil_epoch) { g_mil_epoch = sig; moved = 1; fog_update(mv); }               /* an army moved or died: redraw */
		}
		if (g_mw_dirty) { g_mw_dirty = 0; moved = 1; }                   /* a message window came (also paused) */
		if (g_goto_x >= 0) { cx = g_goto_x; cy = g_goto_y; g_goto_x = -1; moved = 1; }   /* a Goto */
		if (moved || bar) tip_state = 0;                      /* the sidebar is drawn anew below: the help box with it */
		if (moved) { mv_set(mv, cx, cy, z); redraw(mv, "move"); }
		else if (bar) {
			int mx, my, mw, mh;
			ui_map_rect(&mx, &my, &mw, &mh);
			draw_bar();
			scr_blit(0, 0, amigagfx_game_width(), BAR_H);                       /* top bar */
			if (bar == 2) scr_blit(0, 0, mx, amigagfx_game_height());          /* sidebar (selection) */
		}
		{	/* rollover help: the instant text at once, the delayed one after a second (buttons.txt) */
			int want = tip_btn == UI_NONE ? 0 : amigagfx_millis() - tip_t >= 1000 ? 2 : tip_btn >= 2000 && tip_btn < 4000 ? 0 : 1;   /* map tips only after the delay */
			if (want != tip_state || (want && bar)) {
				int mx, my, mw, mh;
				ui_map_rect(&mx, &my, &mw, &mh);
				if (tip_btn >= 1000 || tip_over_map) {           /* a top-bar box lies over the map: the whole screen */
					mv_set(mv, cx, cy, z); redraw(mv, "tip");
					tip_over_map = 0;
					if (want && tip_btn >= 1000) {
						static char t1[900], t2[600];
						if (tip_btn >= 4000) ui_window_tip(tip_x, tip_y, t1, sizeof t1, t2, sizeof t2);   /* battle / peace window */
						else if (tip_btn == 3100) {                        /* SEA_PIRB (0x507ba0): why not, or the price */
							long cost = 0;
							int r = g_mil ? mil_privateer_state(g_mil, g_player, g_selected, &cost) : 3;
							if (r == 3) snprintf(t1, sizeof t1, "%s", text_get("SEA_NOTNAVAL"));
							else if (r == 2) snprintf(t1, sizeof t1, "%s", text_get("SEA_ALREADY"));
							else snprintf(t1, sizeof t1, text_get("SEA_PRIV"), (int)(cost / 1000));
							t2[0] = 0;
						}
						else if (tip_btn >= 3000) {                        /* PIW buildings: the button texts BUTTON_0055.. */
							static const char *bk[8] = { "BUTTON_0055", "BUTTON_0056", "BUTTON_0057", "BUTTON_0060", "BUTTON_0058", "BUTTON_0059", "BUTTON_0061", "PIW_RESOURCE" };
							int b = tip_btn - 3000;
							if (b == 7 && g_selected > 0) snprintf(t1, sizeof t1, "%s", world_name(&g_world, g_world.prov[g_selected].goods) ? world_name(&g_world, g_world.prov[g_selected].goods) : "");   /* the trade good */
							else snprintf(t1, sizeof t1, text_get(bk[b >= 0 && b < 8 ? b : 0]), g_player >= 0 ? (int)(g_world.country[g_player].merchants_m / 1000) : 0);
							t2[0] = 0;
						} else if (tip_btn >= 2000) {                      /* the map tooltip: name, then owner and the header's line 2 (0x532713) */
							const Province *p = &g_world.prov[tip_btn - 2000];
							char l2[64];
							l2[0] = 0;
							if (p->terrain == 5) snprintf(l2, sizeof l2, "%s", text_get(p->ice && p->winter ? "SEA_ICEY" : p->storm_now ? "SEA_STORMY" : "SEA_CLEAR"));
							else if (p->tp) snprintf(l2, sizeof l2, text_get("HEAD_TP"), (int)p->tp);
							else if (p->population > 0) snprintf(l2, sizeof l2, text_get("HEAD_CITY"), (int)p->population);
							snprintf(t1, sizeof t1, "%s", p->name ? p->name : "");
							snprintf(t2, sizeof t2, "%s%s%s", p->owner >= 0 ? text_get(g_world.country[p->owner].tag) : "", p->owner >= 0 && l2[0] ? "\\n" : "", l2);
						} else bar_tip(tip_btn - 1000, t1, t2, 200);
						if (ui_tip_text(amigagfx_chunky(), amigagfx_pitch(), tip_x, tip_y, t1, want == 2 && t2[0] ? t2 : NULL)) tip_over_map = 1;
						else want = 0;
						scr_blit(0, 0, amigagfx_game_width(), amigagfx_game_height());
					}
				} else {
					draw_bar();
					if (want && !ui_tooltip_draw(amigagfx_chunky(), amigagfx_pitch(), tip_x, tip_y, want == 2)) want = 0;
					scr_blit(0, 0, mx, amigagfx_game_height());
				}
				tip_state = want;
			}
		}
		if (cfg.autoexit_ms && amigagfx_millis() - t0 > (unsigned long)cfg.autoexit_ms) { logf_("eu2: autoexit"); AmigaMemProbe("autoexit"); break; }
		{	/* edge scroll (0x475910): the outermost pixel, 32 screen px a tick at every zoom */
			int gw = amigagfx_game_width(), gh = amigagfx_game_height(), ex = 0, ey = 0;
			if (!dragging && g_mouse_x >= 0) {
				if (g_mouse_x <= 0) ex = -1; else if (g_mouse_x >= gw - 1) ex = 1;
				if (g_mouse_y <= 0) ey = -1; else if (g_mouse_y >= gh - 1) ey = 1;
			}
			if (ex || ey) {
				long ecx, ecy; int ez;
				mv_get(mv, &ecx, &ecy, &ez);
				mv_set(mv, ecx + ex * (32L << (ez - 1)), ecy + ey * (32L << (ez - 1)), ez);
				tut_scrolled(g_tut);
				redraw(mv, "edge");
			}
		}
		if (units_anim_active()) { units_set_time(amigagfx_millis()); redraw(mv, "anim"); }
		else if (cfg.anim && !dragging) {                     /* the units' A-STAND / A-WALK frames (eu2setup: Unit animation) */
			static unsigned long last_ua;
			unsigned long now = amigagfx_millis();
			if (now - last_ua >= 100) { last_ua = now; units_set_time(now); redraw(mv, "uanim"); }
		}
		cursor_choose(mv, dragging);
		if (!moved && !bar) eu2_sleep_ms(20);                 /* never spin: MorphOS users reported 100 % CPU in AmiSC */
	}
	if (g_to_menu && !cfg.bench) {                           /* Exit in the game menu: back to the main menu (user 2026-11-11) */
		g_to_menu = 0;
		if (g_tut) { tut_free(g_tut); g_tut = NULL; ui_btn_disable(0); env_always_success(-1); }
		g_fe_screen = 0; cfg.menu = 1; cfg.intro = 0; cfg.scenario[0] = 0;
		g_game_over = 0; g_hist_loaded = 0; quit_now = 0;
		goto front_again;
	}
	if (g_tut_end) {                                         /* EndTutorial: no victory screen, back to the chapter list (0x5b4a20) */
		tut_free(g_tut); g_tut = NULL; g_tut_end = 0; ui_btn_disable(0); env_always_success(-1);
		g_fe_screen = 2; cfg.menu = 1; cfg.intro = 0; cfg.scenario[0] = 0;
		g_game_over = 0; g_hist_loaded = 0; quit_now = 0;
		goto front_again;
	}
	sound_free();
	amigagfx_close();
	mv_destroy(mv);
	sim_stop();
	if (g_have_font) font_free(&g_font);
	ui_free();
	pak_close(&gfx); pak_close(&text); pak_close(&map); pak_close(&sfxp);
	logf_("eu2: bye");
	return 0;
}
