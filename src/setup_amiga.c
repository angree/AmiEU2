/* setup_amiga.c - eu2setup, the AmiEU2 settings tool (GadTools window on the Workbench).
 *
 * Writes PROGDIR:eu2.cfg - the keys below, every other line of the file kept as it was - and can
 * start the game (PROGDIR:eu2). One 68k binary for AmigaOS 3.x, AROS 68k and MorphOS:
 *   display   AGA 640x480 or RTG, and on RTG the screen size, read straight out of the display
 *             database (the way the AmiTTD port does it: BestCModeIDTagList answered INVALID_ID to
 *             every question on a PiStorm, the database is what the ScreenMode requester shows).
 *             Every size AND depth is listed (640x480 = the AGA layout, 800x600 and up = the
 *             original's): 8 bit (LUT8) the game draws into directly, 15/16/24/32 bit it converts at
 *             the copy (WriteLUTPixelArray). On MorphOS only the deep ones (its 8-bit modes are
 *             unreliable or missing) - and no AGA there, MorphOS has no chipset.
 *   titlebar  the Intuition screen bar "AmiEU2 68K" above the game
 *   pointer   game (drawn by the game) or system (the Intuition pointer, shaped like the game's)
 *   sound     off / Paula (audio.device) / AHI (ahi.device; the only one offered on MorphOS)
 *   intro     the intro stills before the menu
 * Keys: gfx=aga|rtg rtgmode=WxH rtgdepth=8|15|16|24|32 titlebar=0|1 pointer=game|system sound=0|1 audio=paula|ahi intro=0|1 anim=0|1 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <clib/alib_protos.h>   /* NewList - amiga.lib */
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "version.h"

unsigned long __stack = 65536UL;
struct Library *CyberGfxBase;

#define CFG "PROGDIR:eu2.cfg"

/* ---- the settings ---- */
static int s_rtg, s_rtg_w = 800, s_rtg_h = 600, s_rtg_bits = 0, s_bar, s_sysptr, s_sound = 1, s_ahi, s_intro = 1, s_anim = -1, s_mapbuf = 0;   /* s_anim -1: not in eu2.cfg, on for RTG */

static int is_morphos(void) { return FindResident((CONST_STRPTR)"MorphOS") != NULL; }
static int has_aga(void) { return !is_morphos() && (GfxBase->ChipRevBits0 & GFXF_AA_ALICE) != 0; }

/* ---- RTG modes from the display database ---- */
typedef struct { int w, h, bits; char label[32]; struct Node node; } Mode;
#define MAXM 96
static Mode g_m[MAXM];
static int g_nm;
static struct List g_mlist;

static void scan_modes(void)
{
	ULONG id = (ULONG)INVALID_ID;
	int i, j, guard = 0;
	g_nm = 0;
	NewList(&g_mlist);
	CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 41L);
	if (CyberGfxBase == NULL) return;
	while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID && guard++ < 1000) {
		ULONG w, h, bpp, depth, fmt;
		int bits;
		if (!IsCyberModeID(id) || ModeNotAvailable(id) != 0) continue;
		w = GetCyberIDAttr(CYBRIDATTR_WIDTH, id);  h = GetCyberIDAttr(CYBRIDATTR_HEIGHT, id);
		bpp = GetCyberIDAttr(CYBRIDATTR_BPPIX, id); depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
		fmt = GetCyberIDAttr(CYBRIDATTR_PIXFMT, id);
		if (!((w >= 800 && h >= 600) || (w == 640 && h == 480)) || w > 4096 || h > 4096) continue;   /* the two layouts */
		if (bpp < 1 || bpp > 4 || depth == 0 || fmt == (ULONG)~0UL) continue;
		if (bpp == 1 && (depth != 8 || fmt != PIXFMT_LUT8)) continue;
		if (bpp == 1 && is_morphos()) continue;                            /* MorphOS: its 8-bit modes are unreliable */
		bits = (int)bpp * 8;
		if (bits == 16 && depth == 15) bits = 15;
		for (i = 0; i < g_nm; i++) if (g_m[i].w == (int)w && g_m[i].h == (int)h && g_m[i].bits == bits) break;
		if (i < g_nm) continue;
		if (g_nm >= MAXM) continue;
		g_m[g_nm].w = (int)w; g_m[g_nm].h = (int)h; g_m[g_nm].bits = bits; g_nm++;
	}
	for (i = 1; i < g_nm; i++) {                     /* smallest first, 8 bit before the deep ones */
		Mode t = g_m[i];
		for (j = i - 1; j >= 0 && (g_m[j].w > t.w || (g_m[j].w == t.w && (g_m[j].h > t.h || (g_m[j].h == t.h && g_m[j].bits > t.bits)))); j--) g_m[j + 1] = g_m[j];
		g_m[j + 1] = t;
	}
	for (i = 0; i < g_nm; i++) {
		static const int ar[5][2] = { { 16, 9 }, { 16, 10 }, { 4, 3 }, { 5, 4 }, { 21, 9 } };
		const char *tag = "";
		static char t[8];
		int k;
		for (k = 0; k < 5 && !*tag; k++) {      /* within 1 %: 1366x768 counts as 16:9 */
			long a = (long)g_m[i].w * ar[k][1] * 100, b = (long)g_m[i].h * ar[k][0] * 100, d = a - b;
			if (d < 0) d = -d;
			if (d * 100 <= b) { snprintf(t, sizeof t, " %d:%d", ar[k][0], ar[k][1]); tag = t; }
		}
		snprintf(g_m[i].label, sizeof g_m[i].label, "%d x %d, %d bit%s", g_m[i].w, g_m[i].h, g_m[i].bits, tag);
		g_m[i].node.ln_Name = g_m[i].label;
		AddTail(&g_mlist, &g_m[i].node);
	}
	CloseLibrary(CyberGfxBase); CyberGfxBase = NULL;
}

/* ---- eu2.cfg: our keys parsed, every other line kept ---- */
#define MAXL 64
static char *g_other[MAXL];
static int g_nother;

static int our_key(const char *k)
{
	static const char *const keys[] = { "gfx", "rtgmode", "rtgdepth", "titlebar", "pointer", "sound", "audio", "intro", "anim", "mapbuf", NULL };
	int i;
	for (i = 0; keys[i]; i++) if (!strcmp(k, keys[i])) return 1;
	return 0;
}

static void read_cfg(void)
{
	FILE *f = fopen(CFG, "r");
	char line[160], key[32];
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *v = strchr(line, '='), *e;
		size_t kl;
		for (e = line + strlen(line); e > line && (e[-1] == '\n' || e[-1] == '\r'); ) *--e = 0;
		kl = v ? (size_t)(v - line) : 0;
		if (v && kl < sizeof key) {
			memcpy(key, line, kl); key[kl] = 0;
			if (our_key(key)) {
				v++;
				if (!strcmp(key, "gfx")) s_rtg = !strcmp(v, "rtg");
				else if (!strcmp(key, "rtgmode")) { const char *x = strchr(v, 'x'); s_rtg_w = atoi(v); s_rtg_h = x ? atoi(x + 1) : 0; }
				else if (!strcmp(key, "rtgdepth")) s_rtg_bits = atoi(v);
				else if (!strcmp(key, "titlebar")) s_bar = atoi(v) != 0;
				else if (!strcmp(key, "pointer")) s_sysptr = !strcmp(v, "system");
				else if (!strcmp(key, "sound")) s_sound = atoi(v) != 0;
				else if (!strcmp(key, "audio")) s_ahi = !strcmp(v, "ahi");
				else if (!strcmp(key, "intro")) s_intro = atoi(v) != 0;
				else if (!strcmp(key, "anim")) s_anim = atoi(v) != 0;
				else if (!strcmp(key, "mapbuf")) s_mapbuf = atoi(v) != 0;
				continue;
			}
		}
		if (line[0] && g_nother < MAXL) {
			size_t n = strlen(line) + 1;
			if ((g_other[g_nother] = malloc(n)) != NULL) memcpy(g_other[g_nother++], line, n);
		}
	}
	fclose(f);
}

static int write_cfg(void)
{
	FILE *f = fopen(CFG, "w");
	int i;
	if (!f) return -1;
	fprintf(f, "gfx=%s\n", s_rtg ? "rtg" : "aga");
	fprintf(f, "rtgmode=%dx%d\n", s_rtg_w, s_rtg_h);
	fprintf(f, "rtgdepth=%d\n", s_rtg_bits);
	fprintf(f, "titlebar=%d\n", s_bar);
	fprintf(f, "pointer=%s\n", s_sysptr ? "system" : "game");
	fprintf(f, "sound=%d\n", s_sound);
	fprintf(f, "audio=%s\n", s_ahi ? "ahi" : "paula");
	fprintf(f, "intro=%d\n", s_intro);
	fprintf(f, "anim=%d\n", s_anim);
	fprintf(f, "mapbuf=%d\n", s_mapbuf);
	for (i = 0; i < g_nother; i++) fprintf(f, "%s\n", g_other[i]);
	fclose(f);
	return 0;
}

/* ---- test rig (never needed by players): PROGDIR:eu2setup.test makes the tool log what it found, dump its
 * own window as PROGDIR:eu2setup.ppm (read back from the window's RastPort), press Save and quit, so the
 * window can be checked without anyone clicking on the host ---- */
static void test_dump(struct Window *w)
{
	FILE *f = fopen("PROGDIR:eu2setup.ppm", "wb"), *lg = fopen("PROGDIR:eu2setup.log", "w");
	struct Screen *sc = w->WScreen;
	int x, y, deep = 0, i;
	struct Library *cgx = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 41L);
	if (lg) {
		fprintf(lg, "eu2setup: %s, AGA %d, %d RTG modes\n", is_morphos() ? "MorphOS" : "AmigaOS/AROS", has_aga(), g_nm);
		for (i = 0; i < g_nm; i++) fprintf(lg, "  %s\n", g_m[i].label);
		fclose(lg);
	}
	if (cgx) { CyberGfxBase = cgx; deep = GetCyberMapAttr(sc->RastPort.BitMap, CYBRMATTR_DEPTH) > 8; }
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", w->Width, w->Height);
		for (y = 0; y < w->Height; y++)
			for (x = 0; x < w->Width; x++) {
				unsigned char c[3];
				if (deep) {
					ULONG v = ReadRGBPixel(w->RPort, (UWORD)x, (UWORD)y);
					c[0] = (unsigned char)(v >> 16); c[1] = (unsigned char)(v >> 8); c[2] = (unsigned char)v;
				} else {
					ULONG t[3];
					LONG pen = ReadPixel(w->RPort, x, y);
					GetRGB32(sc->ViewPort.ColorMap, (ULONG)(pen < 0 ? 0 : pen), 1, t);
					c[0] = (unsigned char)(t[0] >> 24); c[1] = (unsigned char)(t[1] >> 24); c[2] = (unsigned char)(t[2] >> 24);
				}
				fwrite(c, 1, 3, f);
			}
		fclose(f);
	}
	if (cgx) { CloseLibrary(cgx); CyberGfxBase = NULL; }
}

/* ---- the window ---- */
enum { G_DISPLAY = 1, G_MODES, G_BAR, G_POINTER, G_SOUND, G_INTRO, G_ANIM, G_MAPBUF, G_SAVE, G_PLAY, G_CANCEL, G_N };
static struct Gadget *g_gad[G_N];
static const char *g_disp_lab[3], *g_ptr_lab[3], *g_snd_lab[4];
static int g_snd_map[3];                 /* cycle index -> 0 off, 1 Paula, 2 AHI */
static int g_disp_rtg_only;

static int snd_index(void)
{
	int want = !s_sound ? 0 : s_ahi ? 2 : 1, i;
	for (i = 0; g_snd_lab[i]; i++) if (g_snd_map[i] == want) return i;
	return g_snd_lab[1] ? 1 : 0;
}

static int mode_index(void)
{
	int i;
	for (i = 0; i < g_nm; i++) if (g_m[i].w == s_rtg_w && g_m[i].h == s_rtg_h && (g_m[i].bits == s_rtg_bits || (s_rtg_bits == 0 && g_m[i].bits == 8))) return i;
	for (i = 0; i < g_nm; i++) if (g_m[i].w == s_rtg_w && g_m[i].h == s_rtg_h) return i;   /* that size, another depth */
	return -1;
}

int main(void)
{
	struct Screen *scr;
	struct Window *win = NULL;
	struct Gadget *glist = NULL, *g;
	struct NewGadget ng;
	APTR vi = NULL;
	int fh, lw, x0, y, done = 0, play = 0, i, modes_rows = 6;
	static char title[64];

	if (GadToolsBase == NULL || ((struct Library *)IntuitionBase)->lib_Version < 37) return 20;
	read_cfg();
	scan_modes();
	if (is_morphos()) { s_rtg = 1; if (s_sound) s_ahi = 1; }
	if (g_nm == 0) s_rtg = 0;
	if (s_anim < 0) s_anim = s_rtg;              /* unit animation: a full map redraw 10 times a second, too much for AGA */
	if (mode_index() < 0 && g_nm > 0) {           /* the stored size is not on this machine: 800x600 or the smallest */
		int k = 0;
		for (i = g_nm - 1; i >= 0; i--) if (g_m[i].w == 800 && g_m[i].h == 600) k = i;
		s_rtg_w = g_m[k].w; s_rtg_h = g_m[k].h;
	}
	if (g_nm > 0) s_rtg_bits = g_m[mode_index()].bits;   /* the listed depth of that line */

	/* the choices this machine has */
	g_disp_rtg_only = !has_aga() && g_nm > 0;
	i = 0;
	if (!g_disp_rtg_only) g_disp_lab[i++] = "AGA 640 x 480";
	if (g_nm > 0) g_disp_lab[i++] = "RTG (graphics card)";
	g_disp_lab[i] = NULL;
	g_ptr_lab[0] = "Game cursor"; g_ptr_lab[1] = "System pointer"; g_ptr_lab[2] = NULL;
	i = 0;
	g_snd_lab[i] = "Off"; g_snd_map[i++] = 0;
	if (!is_morphos()) { g_snd_lab[i] = "Paula (audio.device)"; g_snd_map[i++] = 1; }
	g_snd_lab[i] = "AHI (ahi.device)"; g_snd_map[i++] = 2;
	g_snd_lab[i] = NULL;

	if ((scr = LockPubScreen(NULL)) == NULL) return 20;
	if ((vi = GetVisualInfo(scr, TAG_END)) == NULL) { UnlockPubScreen(NULL, scr); return 20; }
	fh = scr->Font ? scr->Font->ta_YSize : 8;
	lw = 24 * 8 + 8;                                 /* label column (topaz-ish; labels are short) */
	x0 = scr->WBorLeft + 8 + lw;
	y = scr->WBorTop + fh + 1 + 6;

	g = CreateContext(&glist);
	memset(&ng, 0, sizeof ng);
	ng.ng_VisualInfo = vi; ng.ng_TextAttr = scr->Font;
	ng.ng_LeftEdge = x0; ng.ng_Width = 220; ng.ng_Height = fh + 6; ng.ng_Flags = PLACETEXT_LEFT;

	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Display"; ng.ng_GadgetID = G_DISPLAY;
	g = g_gad[G_DISPLAY] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)g_disp_lab,
	                                    GTCY_Active, (ULONG)(g_disp_rtg_only ? 0 : s_rtg), TAG_END);
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"RTG screen"; ng.ng_GadgetID = G_MODES;
	ng.ng_Height = (fh + 2) * modes_rows + 4;
	g = g_gad[G_MODES] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&g_mlist,
	                                  GTLV_ShowSelected, 0UL, GTLV_Selected, (ULONG)(LONG)mode_index(),
	                                  GTLV_MakeVisible, (ULONG)(LONG)(mode_index() < 0 ? 0 : mode_index()),
	                                  GA_Disabled, (ULONG)(!s_rtg || g_nm == 0), TAG_END);
	y += ng.ng_Height + 6;
	ng.ng_Height = fh + 6;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Title bar"; ng.ng_GadgetID = G_BAR; ng.ng_Width = 26;
	g = g_gad[G_BAR] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)s_bar, GTCB_Scaled, TRUE, TAG_END);
	ng.ng_Width = 220;
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Mouse pointer"; ng.ng_GadgetID = G_POINTER;
	g = g_gad[G_POINTER] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)g_ptr_lab, GTCY_Active, (ULONG)s_sysptr, TAG_END);
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Sound"; ng.ng_GadgetID = G_SOUND;
	g = g_gad[G_SOUND] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)g_snd_lab, GTCY_Active, (ULONG)snd_index(), TAG_END);
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Intro"; ng.ng_GadgetID = G_INTRO; ng.ng_Width = 26;
	g = g_gad[G_INTRO] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)s_intro, GTCB_Scaled, TRUE, TAG_END);
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Unit animation"; ng.ng_GadgetID = G_ANIM;
	g = g_gad[G_ANIM] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)s_anim, GTCB_Scaled, TRUE, TAG_END);
	y += fh + 10;
	ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)"Buffer province repaint"; ng.ng_GadgetID = G_MAPBUF;
	g = g_gad[G_MAPBUF] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)s_mapbuf, GTCB_Scaled, TRUE, TAG_END);
	y += fh + 16;
	ng.ng_Flags = PLACETEXT_IN; ng.ng_Width = 100; ng.ng_Height = fh + 8;
	ng.ng_TopEdge = y; ng.ng_LeftEdge = scr->WBorLeft + 8; ng.ng_GadgetText = (UBYTE *)"Save"; ng.ng_GadgetID = G_SAVE;
	g = g_gad[G_SAVE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
	ng.ng_LeftEdge = scr->WBorLeft + 8 + (lw + 220 - 100) / 2; ng.ng_GadgetText = (UBYTE *)"Play"; ng.ng_GadgetID = G_PLAY;
	g = g_gad[G_PLAY] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
	ng.ng_LeftEdge = scr->WBorLeft + 8 + lw + 220 - 100; ng.ng_GadgetText = (UBYTE *)"Cancel"; ng.ng_GadgetID = G_CANCEL;
	g = g_gad[G_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
	y += ng.ng_Height + 8;

	if (g != NULL) {
		snprintf(title, sizeof title, "AmiEU2 68K settings %s", EU2_VERSION);
		win = OpenWindowTags(NULL,
		                     WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
		                     WA_Left, 40UL, WA_Top, 30UL,
		                     WA_Width, (ULONG)(lw + 220 + 16 + scr->WBorLeft + scr->WBorRight),
		                     WA_Height, (ULONG)(y + scr->WBorBottom),
		                     WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
		                     WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
		                     WA_IDCMP, (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
		                                       CYCLEIDCMP | LISTVIEWIDCMP | CHECKBOXIDCMP | BUTTONIDCMP),
		                     TAG_END);
	}
	UnlockPubScreen(NULL, scr);
	if (win == NULL) { FreeGadgets(glist); FreeVisualInfo(vi); return 20; }
	GT_RefreshWindow(win, NULL);
	{
		BPTR l = Lock((CONST_STRPTR)"PROGDIR:eu2setup.test", ACCESS_READ);
		if (l) {                                  /* the file holding "play" presses Play instead of Save */
			FILE *t = fopen("PROGDIR:eu2setup.test", "r");
			char w4[8] = "";
			UnLock(l);
			if (t) { if (!fgets(w4, sizeof w4, t)) w4[0] = 0; fclose(t); }
			Delay(50); test_dump(win); write_cfg(); done = 1; play = !strncmp(w4, "play", 4);
		}
	}

	while (!done) {
		struct IntuiMessage *m;
		WaitPort(win->UserPort);
		while (!done && (m = GT_GetIMsg(win->UserPort)) != NULL) {
			ULONG cls = m->Class;
			UWORD code = m->Code;
			struct Gadget *gg = (struct Gadget *)m->IAddress;
			GT_ReplyIMsg(m);
			if (cls == IDCMP_CLOSEWINDOW) done = 1;
			else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
			else if (cls == IDCMP_VANILLAKEY && code == 27) done = 1;
			else if (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN || cls == IDCMP_MOUSEMOVE) {
				switch (gg->GadgetID) {
				case G_DISPLAY:
					s_rtg = g_disp_rtg_only ? 1 : code == 1;
					GT_SetGadgetAttrs(g_gad[G_MODES], win, NULL, GA_Disabled, (ULONG)!s_rtg, TAG_END);
					break;
				case G_MODES:
					if (code < g_nm) { s_rtg_w = g_m[code].w; s_rtg_h = g_m[code].h; s_rtg_bits = g_m[code].bits; }
					break;
				case G_BAR: s_bar = (gg->Flags & GFLG_SELECTED) != 0; break;
				case G_POINTER: s_sysptr = code == 1; break;
				case G_SOUND: s_sound = g_snd_map[code] != 0; if (s_sound) s_ahi = g_snd_map[code] == 2; break;
				case G_INTRO: s_intro = (gg->Flags & GFLG_SELECTED) != 0; break;
				case G_ANIM: s_anim = (gg->Flags & GFLG_SELECTED) != 0; break;
				case G_MAPBUF: s_mapbuf = (gg->Flags & GFLG_SELECTED) != 0; break;
				case G_SAVE: write_cfg(); done = 1; break;
				case G_PLAY: write_cfg(); done = 1; play = 1; break;
				case G_CANCEL: done = 1; break;
				}
			}
		}
	}
	CloseWindow(win);
	FreeGadgets(glist);
	FreeVisualInfo(vi);
	for (i = 0; i < g_nother; i++) free(g_other[i]);
	if (play) {
		/* the shell System() starts has a PROGDIR: of its own: the game is named by its full path, and
		 * the shell starts in our drawer */
		static char dir[256], cmd[300];
		BPTR in, out, old;
		LONG r;
		if (!NameFromLock(GetProgramDir(), (STRPTR)dir, sizeof dir)) return 0;
		snprintf(cmd, sizeof cmd, "\"%s%seu2\"", dir, dir[0] && dir[strlen(dir) - 1] != ':' ? "/" : "");
		in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE); out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
		old = CurrentDir(GetProgramDir());
		r = SystemTags((CONST_STRPTR)cmd, SYS_Input, (ULONG)in, SYS_Output, (ULONG)out, TAG_END);
		CurrentDir(old);
		if (r == -1) { if (in) Close(in); if (out) Close(out); }
	}
	return 0;
}
