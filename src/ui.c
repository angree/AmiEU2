/* ui.c - see ui.h. Coordinates are those of the 800x600 original (SPEC_UI.md §2), multiplied by
 * g_scale/1000. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mapmode.h"
#include "ui.h"
#include "peace.h"
#include "leaders.h"
#include "religion.h"
#include "units.h"
#include "sprite.h"
#include "font.h"
#include "text.h"
#include "eui.h"
#include "script.h"

#define S(v) ((v) * g_scale / 1000)

typedef struct { const char *file; int frames, x, y, id; Sprite s; const char *tip1, *tip2; } Btn;

static int g_scale = 1000, g_sw, g_sh;
typedef struct { int w, h; unsigned char *lv; } ShMask;          /* multiply shadow mask, see shmask_load */
static ShMask g_fbtn_shm, g_bbig_shm;
static const unsigned char *g_mappal, *g_shade_pal, *g_light_pal;
static void shade_tables(const unsigned char *pal);
static int shmask_load(ShMask *m, const char *path);
static void shmask_draw(const ShMask *m, unsigned char *dst, int pitch, int x0, int y0);
static void shmask_free(ShMask *m);
static Sprite g_header, g_mask, g_iw_none, g_banner1, g_bottom, g_minimap, g_win8[9];
static int g_tbx;                     /* top bar x offset on screens wider than 800 (ui_init) */
static Sprite g_ic_merchant, g_ic_colonist, g_ic_diplomat, g_ic_missionary, g_ic_soldier, g_ic_treasury, g_pause;
static Font g_fbar, g_fside, g_fsmall;
static Font g_ftight;                     /* the small face with 1 px less spacing (text_at) */
static int g_have_ftight;
static int g_have_fbar, g_have_fside, g_have_fsmall;
static const char *g_fontdir = "PROGDIR:fonts/";
void ui_set_font_dir(const char *d) { g_fontdir = d; }
static unsigned char g_shadow[256];
static int g_ucmd[6];                     /* the army / fleet window's commands as last drawn (5: KILL_NATIVES) */
static int g_sbtn;                        /* the siege buttons are on screen */
static int g_pu_on;                       /* the purchase window is on screen (2: with its build button) */
static int g_rel_on;                      /* the religion window is on screen */
static int g_lo_on;                       /* the loans window is on screen */
static int g_vas_on, g_cow_vas;           /* the vassal window / the Create Vassal button are on screen */
static int g_pen_white, g_pen_black, g_pen_gold, g_pen_orange;

/* sidebar buttons (SPEC_UI §2.2): frame 0 drawn */
static Btn g_btn[] = {      /* the last two: buttons.txt upper (instant) / lower (delayed) rollover keys */
	{ "btn_army.bmp", 4, 38, 9, UI_VIEW_LAND, {0}, "BUTTON_0032", "BUTTON_0033" },
	{ "btn_navy.bmp", 4, 6, 28, UI_VIEW_NAVAL, {0}, "BUTTON_0034", "BUTTON_0035" },
	{ "btn_treasury.bmp", 4, 156, 9, UI_TREASURY, {0}, "BUTTON_0028", "BUTTON_0029" },
	{ "btn_budget.bmp", 4, 190, 27, UI_FINANCIAL, {0}, "BUTTON_0030", "BUTTON_0031" },
	{ "btn_missionary.bmp", 4, 28, 442, UI_MISSIONARY, {0}, "BUTTON_0998", NULL },
	{ "btn_diplomat.bmp", 4, 70, 444, UI_DIPLOMAT, {0}, "BUTTON_0062", NULL },
	{ "btn_merchant.bmp", 4, 118, 445, UI_MERCHANT, {0}, "BUTTON_0063", NULL },
	{ "btn_colonist.bmp", 4, 162, 441, UI_COLONIST, {0}, "BUTTON_0064", NULL },
	{ "btn_mapmode_normal.bmp", 4, 58, 480, UI_MAPMODE_NORMAL, {0}, "BUTTON_0040", NULL },
	{ "btn_mapmode_political.bmp", 4, 94, 480, UI_MAPMODE_POLITICAL, {0}, "BUTTON_0039", NULL },
	{ "btn_mapmode_resource.bmp", 4, 138, 480, UI_MAPMODE_RESOURCE, {0}, "BUTTON_0038", NULL },
	{ "btn_missions.bmp", 4, 0, 487, UI_MISSIONS, {0}, "BUTTON_0036", "BUTTON_0037" },
	{ "btn_eyr.bmp", 4, 186, 486, UI_LEDGER, {0}, "BUTTON_0042", NULL },
	{ "btn_game_setting.bmp", 4, 88, 571, UI_MENU, {0}, "BUTTON_0041", NULL },
	{ "btn_zoom_in.bmp", 4, 20, 579, UI_ZOOM_IN, {0}, "BUTTON_0043", NULL },
	{ "btn_zoom_out.bmp", 4, 168, 579, UI_ZOOM_OUT, {0}, "BUTTON_0044", NULL },
};
#define NBTN ((int)(sizeof g_btn / sizeof g_btn[0]))

static void piw_init(Pak *text);
static void piw_free(void);
static Pak *g_gfx;
static const unsigned char *g_inv;
static void piw_draw(unsigned char *dst, int pitch, const World *w, int id);
static void budget_draw(unsigned char *dst, int pitch, const UiState *st);
static int g_bud_on, g_bud_wt, g_bud_loan; static long g_bud_lsize;
static int g_dom_on, g_dom_c, g_dom_closed; static const World *g_dom_w; static char g_dom_note_txt[160];
static Sprite g_boost; static unsigned char g_bud_boost[5];
static void domestic_draw(unsigned char *dst, int pitch, const World *w, int c, const char *note);
static void army_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void unit_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int naval);
static void finance_draw(unsigned char *dst, int pitch, const UiState *st);
static void tech_draw(unsigned char *dst, int pitch, const UiState *st, int naval);
static void country_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void hprov_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int id);
static void sea_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int id);
static void dip_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void cot_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void colonist_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void missionary_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void missions_draw(unsigned char *dst, int pitch, const UiState *st);
static void battle_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void peace_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void purchase_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void religion_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void resource_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void loans_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static void vassal_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
static MilBattleView g_tip_bv; static int g_tip_bv_on, g_tip_gauge;
static UiPeace g_tip_pw; static int g_tip_pw_on;
static const World *g_tip_world;
static UiPurchase g_tip_pu; static EconGoods g_tip_res; static int g_tip_res_on, g_tip_res_id;
static Sprite g_track, g_knob;
static void ui_free_lazy(void);
static void put(const Sprite *s, int f, unsigned char *dst, int pitch, int x, int y);
int ui_btn_disabled(int id);
static void text_at(Font *f, int have, unsigned char *dst, int pitch, int x, int y, const char *s, int pen, int centre);
static void text_btn_c(unsigned char *dst, int pitch, int cx, int y, const char *label);

static int nearest(const unsigned char *pal, int r, int g, int b)
{
	int i, best = 0;
	long bd = 0x7fffffffL;
	for (i = 0; i < 256; i++) {
		long dr = r - pal[i * 3], dg = g - pal[i * 3 + 1], db = b - pal[i * 3 + 2], d;
		if (i >= 17 && i <= 19) continue;
		d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

static void load(Sprite *s, Pak *gfx, const char *path, const unsigned char *inv, int frames)
{
	if (sprite_from_bmp(s, gfx, path, inv, frames, g_scale)) memset(s, 0, sizeof *s);
}

int ui_init(Pak *gfx, Pak *text, const unsigned char *pal, const unsigned char *inv, int sw, int sh)
{
	static const char *w8[9] = { "tl", "t", "tr", "cl", "c", "cr", "bl", "b", "br" };
	char path[96];
	int i;
	g_sw = sw; g_sh = sh; g_mappal = pal;
	g_scale = sw >= 800 ? 1000 : sw * 1000 / 800;
	g_tbx = g_scale >= 1000 && sw > 800 ? (sw - 800) / 2 : 0;   /* the top bar stays centred over the map (232..792 of 224..800 at 800) */
	for (i = 0; i < 256; i++) g_shadow[i] = (unsigned char)nearest(pal, pal[i * 3] / 2, pal[i * 3 + 1] / 2, pal[i * 3 + 2] / 2);
	sprite_set_shadow_table(g_shadow);
	g_pen_white = nearest(pal, 255, 255, 255); g_pen_black = nearest(pal, 0, 0, 0); g_pen_gold = nearest(pal, 255, 189, 0);
	font_set_code_pens(nearest(pal, 255, 255, 0), g_pen_white);
	g_pen_orange = nearest(pal, 255, 192, 0);
	g_gfx = gfx; g_inv = inv;
	piw_init(text);
	load(&g_header, gfx, "gfx/interface/sidebar_header.bmp", inv, 1);
	load(&g_mask, gfx, "gfx/interface/sidebar_mask.bmp", inv, 1);
	load(&g_iw_none, gfx, "gfx/interface/iw_none.bmp", inv, 1);
	load(&g_banner1, gfx, "gfx/interface/sidebar_banner1.bmp", inv, 1);
	load(&g_bottom, gfx, "gfx/interface/sidebar_bottom.bmp", inv, 1);
	load(&g_minimap, gfx, "gfx/map/minimapfull.bmp", inv, 1);
	for (i = 0; i < 9; i++) { snprintf(path, sizeof path, "gfx/interface/win8_%s.bmp", w8[i]); load(&g_win8[i], gfx, path, inv, 1); }
	load(&g_ic_merchant, gfx, "gfx/interface/button area/btn_bar_merchant.bmp", inv, 1);
	load(&g_ic_colonist, gfx, "gfx/interface/button area/btn_bar_colonist.bmp", inv, 1);
	load(&g_ic_diplomat, gfx, "gfx/interface/diplomat_icon.bmp", inv, 1);
	load(&g_ic_missionary, gfx, "gfx/interface/missionar_icon.bmp", inv, 1);
	load(&g_ic_soldier, gfx, "gfx/interface/button area/btn_bar_soldier.bmp", inv, 1);
	load(&g_ic_treasury, gfx, "gfx/interface/button area/btn_bar_treasury.bmp", inv, 1);
	load(&g_pause, gfx, "gfx/interface/button area/pause_button.bmp", inv, 4);
	for (i = 0; i < NBTN; i++) {
		snprintf(path, sizeof path, "gfx/interface/button area/%s", g_btn[i].file);
		load(&g_btn[i].s, gfx, path, inv, g_btn[i].frames);
	}
	/* fonts do not scale: the 640x480 screen uses the next smaller faces */
	font_set_cc0(g_fontdir, g_scale >= 1000);      /* every face: Not Jam (CC0), see font.c */
	{ char p[96]; snprintf(p, sizeof p, "%sfx5x8.euf", g_fontdir); g_have_ftight = font_load_euf(&g_ftight, p) == 0; }
	if (g_scale >= 1000) {                          /* 800x600 */
		g_have_fbar = font_load(&g_fbar, gfx, "garamond20bold") == 0;
		g_have_fside = font_load(&g_fside, gfx, "garamond14bold") == 0;
		g_have_fsmall = font_load(&g_fsmall, gfx, "garamond12") == 0;
	} else {                                        /* 640x480: EB Garamond (OFL) made for this size, the game's smaller faces as a fallback */
		char p[96];
		snprintf(p, sizeof p, "%sfx6x10.euf", g_fontdir); g_have_fbar = font_load_euf(&g_fbar, p) == 0 || font_load(&g_fbar, gfx, "garamond14bold") == 0;
		snprintf(p, sizeof p, "%sfx6x9.euf", g_fontdir); g_have_fside = font_load_euf(&g_fside, p) == 0 || font_load(&g_fside, gfx, "garamond12bold") == 0;
		snprintf(p, sizeof p, "%sfx6x9.euf", g_fontdir); g_have_fsmall = font_load_euf(&g_fsmall, p) == 0 || font_load(&g_fsmall, gfx, "arial10") == 0;
	}
	return 0;
}

void ui_free(void)
{
	int i;
	sprite_free(&g_header); sprite_free(&g_mask); sprite_free(&g_iw_none); sprite_free(&g_banner1);
	sprite_free(&g_bottom); sprite_free(&g_minimap);
	for (i = 0; i < 9; i++) sprite_free(&g_win8[i]);
	sprite_free(&g_ic_merchant); sprite_free(&g_ic_colonist); sprite_free(&g_ic_diplomat);
	sprite_free(&g_ic_missionary); sprite_free(&g_ic_soldier); sprite_free(&g_ic_treasury); sprite_free(&g_pause);
	for (i = 0; i < NBTN; i++) sprite_free(&g_btn[i].s);
	if (g_have_fbar) font_free(&g_fbar);
	if (g_have_fside) font_free(&g_fside);
	if (g_have_fsmall) font_free(&g_fsmall);
	piw_free();
	ui_free_lazy();
}

int ui_sidebar_h(void) { return S(600); }
void ui_map_rect(int *x, int *y, int *w, int *h) { *x = S(224); *y = 0; *w = g_sw - S(224); *h = g_sh; }

static void put(const Sprite *s, int f, unsigned char *dst, int pitch, int x, int y) { sprite_draw(s, f, dst, pitch, g_sw, g_sh, x, y); }

/* a win8 framed box (SPEC_UI §1 popup windows): 8x8 tiles, edges tiled, centre tiled */
static void win8_box(unsigned char *dst, int pitch, int x, int y, int w, int h)
{
	int t = g_win8[0].w ? g_win8[0].w : 8, xx, yy;
	for (yy = y + t; yy < y + h - t; yy += t)
		for (xx = x + t; xx < x + w - t; xx += t) put(&g_win8[4], 0, dst, pitch, xx, yy);
	for (xx = x + t; xx < x + w - t; xx += t) { put(&g_win8[1], 0, dst, pitch, xx, y); put(&g_win8[7], 0, dst, pitch, xx, y + h - t); }
	for (yy = y + t; yy < y + h - t; yy += t) { put(&g_win8[3], 0, dst, pitch, x, yy); put(&g_win8[5], 0, dst, pitch, x + w - t, yy); }
	put(&g_win8[0], 0, dst, pitch, x, y); put(&g_win8[2], 0, dst, pitch, x + w - t, y);
	put(&g_win8[6], 0, dst, pitch, x, y + h - t); put(&g_win8[8], 0, dst, pitch, x + w - t, y + h - t);
}

/* a line that would leave its area (the sidebar, 224 wide, or the screen) is drawn with the tight
 * face (the same letters, 1 px less spacing) when the face is the small one */


static void text_at(Font *f, int have, unsigned char *dst, int pitch, int x, int y, const char *s, int pen, int centre)
{
	int w;
	if (!have) return;
	w = font_width(f, s);
	if (g_have_ftight && f->cell <= g_ftight.cell + 1 && f != &g_ftight) {
		int x0 = x < S(224) ? 0 : S(224), x1 = x < S(224) ? S(224) : g_sw;
		int over = centre ? (x - w / 2 < x0 + 2 || x + (w + 1) / 2 > x1 - 2) : x + w > x1 - 2;
		if (over) { f = &g_ftight; w = font_width(f, s); }
	}
	if (centre) x -= w / 2;
	font_draw(f, dst, pitch, g_sw, g_sh, x, y, s, pen);
}

/* a left-aligned label in a column maxw wide: the tight face when it does not fit, then cut */
static void text_lim(Font *f, int have, unsigned char *dst, int pitch, int x, int y, const char *s, int pen, int maxw)
{
	char b[96];
	int n;
	if (!have) return;
	if (font_width(f, s) > maxw && g_have_ftight && f->cell <= g_ftight.cell + 1) f = &g_ftight;
	snprintf(b, sizeof b, "%s", s);
	for (n = (int)strlen(b); n > 1 && font_width(f, b) > maxw; n--) b[n - 1] = 0;
	font_draw(f, dst, pitch, g_sw, g_sh, x, y, b, pen);
}

/* a centred line of the sidebar's banner (176 px): the small face when the bold one does not fit */
static void text_fit(unsigned char *dst, int pitch, int x, int y, const char *s, int pen)
{
	if (g_have_fside && font_width(&g_fside, s) <= S(176)) text_at(&g_fside, 1, dst, pitch, x, y, s, pen, 1);
	else text_at(&g_fsmall, g_have_fsmall, dst, pitch, x, y + (g_fside.cell - g_fsmall.cell) / 2, s, pen, 1);
}

/* centred, word-wrapped text; text.csv uses a literal "\n" for line breaks */
static void text_block(unsigned char *dst, int pitch, int cx, int y, int maxw, const char *s, int pen)
{
	Font *f = g_scale < 1000 && g_have_fsmall ? &g_fsmall : &g_fside;   /* 640x480: the smaller face fits the panel */
	char line[160];
	int n = 0, lh = f->cell;
	if (!g_have_fside) return;
	while (*s) {
		const char *word = s;
		int wl = 0, brk = 0;
		while (s[wl] && s[wl] != ' ' && !(s[wl] == '\\' && s[wl + 1] == 'n')) wl++;
		if (wl > 60) wl = 60;
		{	/* does line + word still fit? */
			char tmp[160];
			snprintf(tmp, sizeof tmp, "%.*s%s%.*s", n, line, n ? " " : "", wl, word);
			if (n && font_width(f, tmp) > maxw) {
				line[n] = 0; text_at(f, 1, dst, pitch, cx, y, line, pen, 1); y += lh; n = 0;
			}
		}
		if (n && n < (int)sizeof line - 2) line[n++] = ' ';
		if (n + wl < (int)sizeof line - 1) { memcpy(line + n, word, (size_t)wl); n += wl; }
		s += wl;
		if (*s == ' ') s++;
		else if (*s == '\\' && s[1] == 'n') { s += 2; brk = 1; }
		if (brk) { line[n] = 0; text_at(f, 1, dst, pitch, cx, y, line, pen, 1); y += lh; n = 0; }
	}
	if (n) { line[n] = 0; text_at(f, 1, dst, pitch, cx, y, line, pen, 1); }
}


/* ------------------------------------------------------- province window (SPEC_UI §3) */
#define NCACHE 192
typedef struct { char path[96]; int state; Sprite s; } SCache;     /* state 1 ok, -1 missing */
static SCache g_cache[NCACHE];
static char g_frames_name[64][24]; static unsigned char g_frames_n[64]; static int g_nframes;
static char *g_layout[2]; static unsigned long g_layout_len[2];     /* inland.txt, coastal.txt */
static Pak *g_text;

static int frames_of(const char *base)        /* PIW/Buildings/filelist.txt */
{
	int i;
	/* filelist.txt says 23 for resource.bmp, but the strip is 1176 px = 21 frames of 56 (SPEC_UI §3.2) */
	if (!strcmp(base, "resource.bmp")) return 21;
	for (i = 0; i < g_nframes; i++) if (!strcmp(g_frames_name[i], base)) return g_frames_n[i];
	return 1;
}

/* the shield's tag: <tag><flagname> (shield_engUK, shield_fraREV) when the event set one and the file exists */
static const char *ftag(const World *w, int c)
{
	static char buf[4][12];
	static int k;
	char p[64];
	const Country *co = &w->country[c];
	k = (k + 1) & 3;
	if (co->flagname[0]) {
		snprintf(buf[k], sizeof buf[k], "%s%s", co->tag, co->flagname);
		snprintf(p, sizeof p, "gfx/map/shields/shield_%s.bmp", buf[k]);
		if (pak_find(g_gfx, p)) return buf[k];
	}
	snprintf(buf[k], sizeof buf[k], "%s", co->tag);
	return buf[k];
}

static const Sprite *cached(const char *path, int frames)
{
	int i, free_slot = -1;
	for (i = 0; i < NCACHE; i++) {
		if (g_cache[i].state && !strcmp(g_cache[i].path, path)) return g_cache[i].state > 0 ? &g_cache[i].s : NULL;
		if (!g_cache[i].state && free_slot < 0) free_slot = i;
	}
	if (free_slot < 0) return NULL;
	snprintf(g_cache[free_slot].path, sizeof g_cache[free_slot].path, "%s", path);
	g_cache[free_slot].state = sprite_from_bmp(&g_cache[free_slot].s, g_gfx, path, g_inv, frames, g_scale) == 0 ? 1 : -1;
	return g_cache[free_slot].state > 0 ? &g_cache[free_slot].s : NULL;
}

static void piw_init(Pak *text)
{
	unsigned long sz;
	char *buf, *cur, *line, *f[3];
	g_text = text;
	if ((buf = (char *)pak_load(text, "gfx/interface/piw/buildings/filelist.txt", &sz)) != NULL) {
		cur = buf;
		while ((line = csv_line(&cur)) != NULL && g_nframes < 64) {
			char *b;
			int i;
			if (csv_split(line, f, 3) < 1 || !strchr(f[0], 44)) continue;   /* "path,frames" */
			b = strrchr(f[0], 92); b = b ? b + 1 : f[0];   /* 92 = backslash */
			for (i = 0; b[i] && b[i] != ',' && i < 23; i++) g_frames_name[g_nframes][i] = (char)((b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i]);
			g_frames_name[g_nframes][i] = 0;
			g_frames_n[g_nframes] = (unsigned char)atoi(b[i] == ',' ? b + i + 1 : f[1]);
			g_nframes++;
		}
		free(buf);
	}
	g_layout[0] = (char *)pak_load(text, "gfx/interface/piw/inland.txt", &g_layout_len[0]);
	g_layout[1] = (char *)pak_load(text, "gfx/interface/piw/coastal.txt", &g_layout_len[1]);
}

static void piw_free(void)
{
	int i;
	for (i = 0; i < NCACHE; i++) { if (g_cache[i].state > 0) sprite_free(&g_cache[i].s); g_cache[i].state = 0; }
	free(g_layout[0]); free(g_layout[1]); g_layout[0] = g_layout[1] = NULL;
	g_nframes = 0;
}

static const char *culture_dir(const World *w, int owner)
{
	const Country *c;
	if (owner < 0) return "latin";
	c = &w->country[owner];
	if (script_ieq(world_name(w, c->religion), "orthodox")) return "orthodox";
	if (!strcmp(c->city_nat, "MUS")) return "muslim";
	if (!strcmp(c->city_nat, "CHI")) return "china";
	if (!strcmp(c->city_nat, "MOG")) return "hindu";
	if (!strcmp(c->city_nat, "BAN")) return "africa";
	if (!strcmp(c->city_nat, "AZT")) return "aztec";
	if (!strcmp(c->city_nat, "OHI")) return "indian";
	return "latin";             /* NIEPEWNE: mapping of City Nationality to the 8 building styles */
}

static int religion_frame(const char *r)        /* church frames follow the religion order (NIEPEWNE) */
{
	static const char *n[11] = { "protestant", "reformed", "counterreform", "catholic", "orthodox", "sunni", "shiite",
	                             "pagan", "confucianism", "buddhism", "hinduism" };
	int i;
	for (i = 0; i < 11; i++) if (script_ieq(r, n[i])) return i;
	return 3;
}

static struct { int x, y, w, h, id; } g_piw[16];   /* clickable buildings of the last drawn PIW, in draw order */
static int g_npiw;

int ui_piw_hit(int x, int y)
{
	int i;
	for (i = 0; i < g_npiw; i++)                /* the small resource icon first: the church's box covers it */
		if (g_piw[i].id == PIW_RESOURCE && x >= g_piw[i].x && x < g_piw[i].x + g_piw[i].w && y >= g_piw[i].y && y < g_piw[i].y + g_piw[i].h) return PIW_RESOURCE;
	for (i = g_npiw - 1; i >= 0; i--)          /* the last drawn lies on top */
		if (x >= g_piw[i].x && x < g_piw[i].x + g_piw[i].w && y >= g_piw[i].y && y < g_piw[i].y + g_piw[i].h && y < S(417)) return g_piw[i].id;
	return PIW_NONE;
}

static void piw_draw(unsigned char *dst, int pitch, const World *w, int id)
{
	const Province *p = &w->prov[id];
	int wx = 0, wy = S(103), lay = p->kind == 1 ? 1 : 0;
	char path[96];
	const Sprite *s;
	static const char *bg[5] = { "piwbg_plains", "piwbg_forest1", "piwbg_mountain", "piwbg_desert", "piwbg_marsh" };
	g_npiw = 0;
	if ((s = cached("gfx/interface/iw_province.bmp", 1)) != NULL) put(s, 0, dst, pitch, wx, wy);
	snprintf(path, sizeof path, "gfx/interface/piw/backgrounds/%s.bmp", bg[p->terrain < 5 ? p->terrain : 0]);
	if ((s = cached(path, 1)) != NULL) put(s, 0, dst, pitch, wx + S(4), wy);
	if (lay && (s = cached("gfx/interface/piw/backgrounds/coast.bmp", 1)) != NULL) put(s, 0, dst, pitch, wx + S(4), wy + S(224));
	if (g_layout[lay]) {         /* buildings in file order (our parser would tokenise in place: use a copy) */
		char *copy = malloc(g_layout_len[lay] + 1);
		Arena *a = arena_new(8192);
		char err[64];
		SNode *root, *n;
		memcpy(copy, g_layout[lay], g_layout_len[lay]);
		root = script_parse(copy, g_layout_len[lay], a, err, sizeof err);
		for (n = root ? root->child : NULL; n; n = n->next) {
			char key[32];
			int i, x, y, frame = 0, draw = 1;
			const char *dir = "";
			if (!n->key || !n->child) continue;
			for (i = 0; n->key[i] && i < 31; i++) key[i] = (char)((n->key[i] >= 'A' && n->key[i] <= 'Z') ? n->key[i] + 32 : n->key[i]);
			key[i] = 0;
			x = (int)script_int(n, "x", 0); y = (int)script_int(n, "y", 0);
			if (!strncmp(key, "house", 5)) {
				static const long lim[4] = { 1000, 5000, 10000, 20000 };
				int k = key[5] - '1';
				dir = culture_dir(w, p->owner); draw = k >= 0 && k < 4 && p->population >= lim[k];
			} else if (!strcmp(key, "church")) { dir = culture_dir(w, p->owner); frame = religion_frame(world_name(w, p->religion)); draw = p->population > 0; }
			else if (!strcmp(key, "fort")) { draw = p->owner >= 0; frame = p->fort > 6 ? 6 : p->fort; }   /* 7 frames = levels 0..6 (frame 0 for no fort: NIEPEWNE) */
			else if (!strcmp(key, "port")) { draw = lay == 1; frame = (p->bflags & 0x01) ? 2 : 0; }   /* frame 2: a shipyard */
			else if (!strcmp(key, "barracks")) frame = (p->bflags & 0x02) ? 1 : 0;           /* frame 1: a conscription centre */
			else if (!strcmp(key, "native")) draw = p->natives > 0;
			else if (!strcmp(key, "resource")) {
				/* frame = the market id (+4, 0x41af17; SPEC_MARKET 1): frame 0 empty, then ids 1..20 (checked on the strip) */
				static const char *gl[21] = { "clo", "gold", "cot", "fish", "furs", "grai", "ivor", "metal", "mineral", "orient",
				                              "navs", "salt", "slav", "spic", "sug", "tob", "wine", "wool", "tea", "coffee", "nothing" };
				const char *gn = world_name(w, p->goods);
				draw = 0;
				for (i = 0; i < 21; i++) if (script_ieq(gn, gl[i])) { frame = i + 1; draw = i < 20; }
			} else if (!strcmp(key, "blockade")) draw = lay == 1 && p->blockaded;
			else if (!strcmp(key, "siege")) draw = p->besieged;
			else if (!strcmp(key, "fortutbyggd") || !strcmp(key, "trading")) draw = 0;
			if (!draw) continue;
			if (*dir) snprintf(path, sizeof path, "gfx/interface/piw/buildings/%s/%s.bmp", dir, key);
			else snprintf(path, sizeof path, "gfx/interface/piw/buildings/%s.bmp", key);
			{
				char base[32];
				snprintf(base, sizeof base, "%s.bmp", key);
				if ((s = cached(path, frames_of(base))) != NULL) {
					static const char *hk[8] = { "barracks", "port", "fort", "church", "bank", "embassy", "merchant", "resource" };
					sprite_draw(s, frame, dst + (long)wy * pitch, pitch, S(220), S(344), wx + S(x), S(y));
					for (i = 0; i < 8; i++) if (!strcmp(key, hk[i]) && g_npiw < 16) {
						g_piw[g_npiw].x = wx + S(x); g_piw[g_npiw].y = wy + S(y);
						g_piw[g_npiw].w = s->w; g_piw[g_npiw].h = s->h; g_piw[g_npiw].id = i; g_npiw++;
					}
				}
			}
		}
		arena_free(a);
		free(copy);
	}
	if ((s = cached("gfx/interface/crest.bmp", 1)) != NULL) put(s, 0, dst, pitch, wx + S(52), wy);
	{	/* trade good icon in the crest (82,4), same strip and frame order as the "resource" building */
		static const char *gl[20] = { "clo", "gold", "cot", "fish", "furs", "grai", "ivor", "metal", "mineral", "orient",
		                              "navs", "salt", "slav", "spic", "sug", "tob", "wine", "wool", "tea", "coffee" };
		const char *gn = world_name(w, p->goods);
		int i;
		for (i = 0; i < 20; i++)
			if (script_ieq(gn, gl[i]) && (s = cached("gfx/interface/piw/buildings/resource.bmp", 21)) != NULL) put(s, i + 1, dst, pitch, wx + S(82), wy + S(4));
	}
	if (p->owner >= 0) {
		snprintf(path, sizeof path, "gfx/map/shields/shield_%s.bmp", ftag(w, p->owner));
		if ((s = cached(path, 4)) != NULL) put(s, 0, dst, pitch, wx + S(168), wy + S(6));
	}
	{	/* the two orange numbers left and right of the resource icon: base tax and manpower (NIEPEWNE) */
		char v[16];
		int orange = g_pen_orange;
		snprintf(v, sizeof v, "%ld", p->income_m / 1000);
		text_at(&g_fside, g_have_fside, dst, pitch, wx + S(74), wy + S(18), v, orange, 1);
		snprintf(v, sizeof v, "%ld", p->manpower_m / 1000);
		text_at(&g_fside, g_have_fside, dst, pitch, wx + S(153), wy + S(18), v, orange, 1);
	}
	if ((s = cached("gfx/interface/sidebar_banner2.bmp", 1)) != NULL) put(s, 0, dst, pitch, 0, S(417));
}

void ui_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	char s[64];
	int i;
	/* sidebar (§2.1) */
	g_npiw = 0; g_tip_bv_on = g_tip_pw_on = 0; memset(g_ucmd, 0, sizeof g_ucmd); g_sbtn = 0; g_pu_on = 0; g_rel_on = 0; g_lo_on = 0; g_vas_on = 0; g_cow_vas = 0; g_tip_res_on = 0; g_bud_on = 0; g_dom_on = 0;
	put(&g_header, 0, dst, pitch, 0, 0);
	if (st->infowin == UI_IW_BUDGET && st->budget) budget_draw(dst, pitch, st);
	else if (st->infowin == UI_IW_FINANCE) finance_draw(dst, pitch, st);
	else if (st->infowin == UI_IW_LANDTECH || st->infowin == UI_IW_NAVALTECH) tech_draw(dst, pitch, st, st->infowin == UI_IW_NAVALTECH);
	else if (st->infowin == UI_IW_COUNTRY) country_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_DIPLO) dip_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_COT && st->env_cot >= 0 && st->env_cot < w->ncot) cot_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_COLONIST && st->env_prov > 0) colonist_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_MISSIONARY && st->env_prov > 0) missionary_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_MISSIONS) missions_draw(dst, pitch, st);
	else if (st->infowin == UI_IW_DOMESTIC) domestic_draw(dst, pitch, w, st->player, st->domestic_note);
	else if (st->infowin == UI_IW_VASSAL) vassal_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_LOANS) loans_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_RESOURCE && st->goods) resource_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_RELIGION && st->religion && st->player >= 0) religion_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_PURCHASE && st->purchase) purchase_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_PEACE && st->peace) peace_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_BATTLE && st->battle && st->battle->kind != MBV_NONE) battle_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_ARMY && st->army >= 0 && st->army < w->nland) army_draw(dst, pitch, w, st);
	else if (st->infowin == UI_IW_NAVY && st->fleet >= 0 && st->fleet < w->nnaval) unit_draw(dst, pitch, w, st, 1);
	else if (st->selected > 0 && st->selected < w->nprov && w->prov[st->selected].terrain == 5) sea_draw(dst, pitch, w, st, st->selected);
	else if (st->selected > 0 && st->selected < w->nprov && (st->player < 0 || w->prov[st->selected].owner != st->player || st->mapmode == MM_ECONOMIC))
		hprov_draw(dst, pitch, w, st, st->selected);   /* window 0xA: foreign, or any province on the economic map (0x52c54b, 0x52e128) */
	else if (st->selected > 0 && st->selected < w->nprov) piw_draw(dst, pitch, w, st->selected);
	else put(&g_iw_none, 0, dst, pitch, 0, S(103));
	put(&g_banner1, 0, dst, pitch, 0, S(103));
	put(&g_bottom, 0, dst, pitch, 0, S(447));
	put(&g_minimap, 0, dst, pitch, S(32), S(504));
	if (st->view_zoom > 0 && g_minimap.w) {      /* 0x47b7f0 / 0x47b8b0: the view as a white 1 px frame, clamped to the minimap */
		int mw = g_minimap.w, mh = g_minimap.h, x0 = S(32), y0 = S(504), sh = st->view_zoom - 1;
		long cx = st->view_cx * mw / 18944 + x0, cy = st->view_cy * mh / 7296 + y0;
		long hw = ((long)st->view_w * mw / 18944 / 2) << sh, hh = ((long)st->view_h * mh / 7296 / 2) << sh;
		long l = cx - hw, t = cy - hh, r = cx + hw, b = cy + hh, i;
		if (l < x0) l = x0;
		if (t < y0) t = y0;
		if (r > x0 + mw - 1) r = x0 + mw - 1;
		if (b > y0 + mh - 1) b = y0 + mh - 1;
		if (l <= r && t <= b) {
			for (i = l; i <= r; i++) { dst[t * pitch + i] = (unsigned char)g_pen_white; dst[b * pitch + i] = (unsigned char)g_pen_white; }
			for (i = t; i <= b; i++) { dst[i * pitch + l] = (unsigned char)g_pen_white; dst[i * pitch + r] = (unsigned char)g_pen_white; }
		}
	}
	if (st->player >= 0) {        /* player's shield in the header hole, under the mask (BUTTON_VP at 84,3) */
		char sp[64];
		const Sprite *sh;
		snprintf(sp, sizeof sp, "gfx/map/shields/shield_%s.bmp", ftag(w, st->player));
		if ((sh = cached(sp, 4)) != NULL) put(sh, 0, dst, pitch, S(84), S(3));
	}
	put(&g_mask, 0, dst, pitch, S(80), 0);
	for (i = 0; i < NBTN; i++) {
		static const int mode_btn[7] = { UI_MAPMODE_NORMAL, UI_MAPMODE_POLITICAL, UI_MAPMODE_RESOURCE, UI_MISSIONARY, UI_DIPLOMAT, UI_MERCHANT, UI_COLONIST };
		int f = st->mapmode >= 0 && st->mapmode < 7 && mode_btn[st->mapmode] == g_btn[i].id ? 2 : 0;   /* frame 2 = pressed (NIEPEWNE) */
		if (ui_btn_disabled(g_btn[i].id)) f = 3;
		put(&g_btn[i].s, f < g_btn[i].s.frames ? f : 0, dst, pitch, S(g_btn[i].x), S(g_btn[i].y));
	}
	/* sidebar texts (§2.1): selected object lines; the empty info window says PLEASESELECT */
	if (st->infowin == UI_IW_ARMY || st->infowin == UI_IW_NAVY) {         /* (unit name, location) 0x4f2f5f / 0x4f353b */
		const char *nm = st->infowin == UI_IW_ARMY ? w->land[st->army].name : w->naval[st->fleet].name;
		int loc = st->infowin == UI_IW_ARMY ? w->land[st->army].location : w->naval[st->fleet].location;
		text_fit(dst, pitch, S(112), S(70), nm ? nm : "", g_pen_black);
		text_fit(dst, pitch, S(112), S(84), loc > 0 && loc < w->nprov && w->prov[loc].name ? w->prov[loc].name : "", g_pen_black);
	}
	else if (st->infowin == UI_IW_VASSAL && st->player >= 0) {   /* HEAD_VASSAL "Grant Independence" + the player */
		text_fit(dst, pitch, S(112), S(70), text_get(w->country[st->player].tag), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get("HEAD_VASSAL"), g_pen_black);
	}
	else if (st->infowin == UI_IW_LOANS && st->player >= 0) {   /* HEAD_LOA "Loans" + the player */
		text_fit(dst, pitch, S(112), S(70), text_get(w->country[st->player].tag), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get("HEAD_LOA"), g_pen_black);
	}
	else if (st->infowin == UI_IW_RESOURCE) {               /* HEAD_RES "Resource Information" + the good */
		const char *gn = world_name(w, (short)st->goods_id);
		text_fit(dst, pitch, S(112), S(70), text_get("HEAD_RES"), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get(gn ? gn : ""), g_pen_black);
	}
	else if (st->infowin == UI_IW_RELIGION && st->player >= 0) {   /* HEAD_REL "Religion" + the player */
		text_fit(dst, pitch, S(112), S(70), text_get(w->country[st->player].tag), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get("HEAD_REL"), g_pen_black);
	}
	else if (st->infowin == UI_IW_PURCHASE && st->purchase) {   /* HEAD_ARC / HEAD_NAC + the province (0x4f4957, 0x4f5a77) */
		int pv = st->purchase->prov;
		text_fit(dst, pitch, S(112), S(70), text_get(st->purchase->naval ? "HEAD_NAC" : "HEAD_ARC"), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), pv > 0 && pv < w->nprov && w->prov[pv].name ? w->prov[pv].name : "", g_pen_black);
	}
	else if (st->infowin == UI_IW_PEACE && st->peace) {     /* HEAD_PEA "Peace Resolution" + the enemy (0x510ad9) */
		text_fit(dst, pitch, S(112), S(70), text_get(w->country[st->peace->enemy].tag), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get("HEAD_PEA"), g_pen_black);
	}
	else if (st->infowin == UI_IW_BATTLE && st->battle) {   /* HEAD_LAN "Land Battle" / HEAD_SIE "Siege" + the province (0x4ff2d0, 0x500ac0) */
		int pv = st->army >= 0 && st->army < w->nland ? w->land[st->army].location : -1;
		if (st->battle->kind == MBV_NAVAL) pv = st->fleet >= 0 && st->fleet < w->nnaval ? w->naval[st->fleet].location : -1;
		text_fit(dst, pitch, S(112), S(70), text_get(st->battle->kind == MBV_SIEGE ? "HEAD_SIE" : st->battle->kind == MBV_NAVAL ? "HEAD_NAV" : "HEAD_LAN"), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), pv > 0 && pv < w->nprov && w->prov[pv].name ? w->prov[pv].name : "", g_pen_black);
	}
	else if (st->infowin == UI_IW_COT || st->infowin == UI_IW_COLONIST || st->infowin == UI_IW_MISSIONARY) {   /* (province, HEAD_COT / COLP / MSW) */
		int pv = st->infowin == UI_IW_COT && st->env_cot >= 0 && st->env_cot < w->ncot ? w->cot[st->env_cot].prov : st->env_prov;
		text_fit(dst, pitch, S(112), S(70), pv > 0 && pv < w->nprov && w->prov[pv].name ? w->prov[pv].name : "", g_pen_black);
		text_fit(dst, pitch, S(112), S(84), text_get(st->infowin == UI_IW_COT ? "HEAD_COT" : st->infowin == UI_IW_COLONIST ? "HEAD_COLP" : "HEAD_MSW"), g_pen_black);
	}
	else if (st->infowin != UI_IW_PROVINCE) {      /* HEAD_* title + the country (0x5171cb, 0x5185c0) */
		static const char *head[9] = { NULL, "HEAD_BUD", "HEAD_DOMESTIC", NULL, "HEAD_FIN", "MILLAND", "MILNAVAL", "HEAD_CIW", "HEAD_DIP" };
		const char *h = st->infowin == UI_IW_MISSIONS ? text_get("HEAD_MIS") : st->infowin >= 0 && st->infowin < 9 && head[st->infowin] ? text_get(head[st->infowin]) : "";
		if (st->player >= 0) text_fit(dst, pitch, S(112), S(70), text_get(w->country[st->player].tag), g_pen_black);
		text_fit(dst, pitch, S(112), S(84), h, g_pen_black);
	}
	else if (st->selected > 0 && st->selected < w->nprov) {
		const Province *p = &w->prov[st->selected];
		text_fit(dst, pitch, S(112), S(70), p->name ? p->name : "", g_pen_black);
		char l2[64];                                   /* line 2 (0x52d190): HEAD_CITY with the population; sea: the weather */
		l2[0] = 0;
		if (p->terrain == 5) snprintf(l2, sizeof l2, "%s", text_get(p->ice ? "SEA_ICEY" : p->storm ? "SEA_STORMY" : "SEA_CLEAR"));
		else if (p->tp) snprintf(l2, sizeof l2, text_get("HEAD_TP"), (int)p->tp);
		else if (p->population > 0 && p->population < 1000) snprintf(l2, sizeof l2, text_get("HEAD_COL"), (int)(p->population / 100 + 1));   /* level: NIEPEWNE */
		else if (p->population > 0) snprintf(l2, sizeof l2, text_get("HEAD_CITY"), (int)p->population);
		text_fit(dst, pitch, S(112), S(84), l2, g_pen_black);
	}
	else text_block(dst, pitch, S(112), S(103 + 120), S(190), text_get("PLEASESELECT"), g_pen_gold);
	/* top bar (§2.3): frame 560x24 at (232,4), icons, values at y=6 */
	win8_box(dst, pitch, S(232) + g_tbx, S(4), S(560), S(24));
	put(&g_ic_merchant, 0, dst, pitch, S(242) + g_tbx, S(5));
	put(&g_ic_colonist, 0, dst, pitch, S(284) + g_tbx, S(5));
	put(&g_ic_diplomat, 0, dst, pitch, S(344) + g_tbx, S(7));
	put(&g_ic_missionary, 0, dst, pitch, S(406) + g_tbx, S(7));
	put(&g_ic_soldier, 0, dst, pitch, S(644) + g_tbx, S(6));
	put(&g_ic_treasury, 0, dst, pitch, S(712) + g_tbx, S(5));
	put(&g_pause, st->paused ? 2 : 0, dst, pitch, S(768) + g_tbx, S(8));
#define VAL(x, v) do { snprintf(s, sizeof s, "%ld", (long)(v)); text_at(&g_fbar, g_have_fbar, dst, pitch, S(x) + g_tbx, S(6), s, g_pen_white, 0); } while (0)
	VAL(262, st->merchants); VAL(316, st->colonists); VAL(372, st->diplomats); VAL(432, st->missionaries);
	VAL(684, st->manpower); VAL(736, st->treasury);
#undef VAL
	snprintf(s, sizeof s, st->stability > 0 ? "+%ld" : "%ld", st->stability);
	text_at(&g_fbar, g_have_fbar, dst, pitch, S(624) + g_tbx, S(6), s, g_pen_white, 0);
	text_at(&g_fbar, g_have_fbar, dst, pitch, S(512) + g_tbx, S(6), st->date ? st->date : "", st->paused ? nearest(g_mappal, 0x80, 0x80, 0x80) : g_pen_white, 1);   /* manual p.12: shadowed while paused */
}

/* minimap (32,504), 160x72 for the whole 18944x7296 map (0x47b650 scales 0x4a00 / 0x1c80 onto it) */
int ui_minimap_hit(int x, int y, long *fx, long *fy)
{
	int x0 = S(32), y0 = S(504), w = g_minimap.w ? g_minimap.w : S(160), h = g_minimap.h ? g_minimap.h : S(72);
	if (x < x0 || x >= x0 + w || y < y0 || y >= y0 + h) return 0;
	*fx = (long)(x - x0) * 18944L / w;
	*fy = (long)(y - y0) * 7296L / h;
	return 1;
}

static int wrap(Font *f, const char *s, int maxw, char out[][96], int maxl);
static int tip_box(unsigned char *dst, int pitch, int x, int y, const char *t1, const char *t2, int xmax);
/* rollover help of the sidebar button under (x,y) (SPEC_UI 2.3: box = text width + 23, max 220,
 * pushed inside the screen with an 8 px margin, gold line between the instant and the delayed text).
 * The box stays inside the sidebar so only the sidebar has to be redrawn. Returns 0 if none. */
int ui_tooltip_draw(unsigned char *dst, int pitch, int x, int y, int delayed)
{
	const Btn *b = NULL;
	const char *t1, *t2;
	int i;
	for (i = 0; i < NBTN; i++)
		if (x >= S(g_btn[i].x) && x < S(g_btn[i].x) + g_btn[i].s.w && y >= S(g_btn[i].y) && y < S(g_btn[i].y) + g_btn[i].s.h) { b = &g_btn[i]; break; }
	if (!b && x >= S(84) && x < S(140) && y >= S(3) && y < S(60)) { static const Btn vp = { "", 0, 84, 3, UI_VP, {0}, "BUTTON_0027", NULL }; b = &vp; }
	if (!b || !b->tip1 || !g_have_fside) return 0;
	t1 = text_get(b->tip1);
	t2 = delayed && b->tip2 ? text_get(b->tip2) : NULL;
	return tip_box(dst, pitch, x, y, t1, t2, S(224));
}

/* the help box: t1 (instant), gold line, t2 (delayed); kept left of xmax */
static int tip_box(unsigned char *dst, int pitch, int x, int y, const char *t1, const char *t2, int xmax)
{
	char lines[24][96];
	int i, n = 0, n1, w, h, bx, by, lh, yy;
	Font *f = g_scale < 1000 && g_have_fsmall ? &g_fsmall : &g_fside;
	if (!g_have_fside || !t1) return 0;
	lh = f->cell;
	n1 = n = wrap(f, t1, S(220) - S(23), lines, 16);            /* (the domestic stats block has up to 15 lines) */
	if (t2 && *t2) n += wrap(f, t2, S(220) - S(23), lines + n, 24 - n);
	for (i = 0, w = 0; i < n; i++) { int lw = font_width(f, lines[i]); if (lw > w) w = lw; }
	w += S(23); if (w > S(220)) w = S(220);
	h = n * lh + S(16) + (n > n1 ? S(6) : 0);
	bx = x + S(12); by = y + S(16);
	if (bx + w > xmax - S(2)) bx = xmax - S(2) - w;
	if (bx < S(2)) bx = S(2);
	if (by + h > g_sh - S(8)) by = y - h - S(4);
	if (by < S(8)) by = S(8);
	win8_box(dst, pitch, bx, by, w, h);
	for (i = 0, yy = by + S(8); i < n; i++) {
		if (i == n1) {                                   /* the gold separator */
			int k;
			for (k = bx + S(6); k < bx + w - S(6); k++) dst[(long)(yy + S(2)) * pitch + k] = (unsigned char)g_pen_gold;
			yy += S(6);
		}
		text_at(f, 1, dst, pitch, bx + w / 2, yy, lines[i], g_pen_white, 1);
		yy += lh;
	}
	return 1;
}

/* top bar items (SPEC_UI 2.3): merchants, colonists, diplomats, missionaries, stability, manpower,
 * treasury - from the icon to the next item */
int ui_bar_item(int x, int y)
{
	static const int x0[8] = { 242, 284, 344, 406, 612, 644, 712, 766 };
	int i;
	if (y < S(4) || y >= S(28)) return -1;
	x -= g_tbx;
	for (i = 0; i < 7; i++) if (x >= S(x0[i]) && x < S(x0[i + 1])) return i;
	return -1;
}

int ui_tip_text(unsigned char *dst, int pitch, int x, int y, const char *t1, const char *t2) { return tip_box(dst, pitch, x, y, t1, t2, g_sw); }

int ui_hit(int x, int y)
{
	int i;
	for (i = 0; i < NBTN; i++) {
		const Btn *b = &g_btn[i];
		if (x >= S(b->x) && x < S(b->x) + b->s.w && y >= S(b->y) && y < S(b->y) + b->s.h) return b->id;
	}
	if (x >= S(768) + g_tbx && x < S(768) + g_tbx + g_pause.w && y >= S(8) && y < S(8) + g_pause.h) return UI_PAUSE;
	if (x >= S(84) && x < S(140) && y >= S(3) && y < S(60)) return UI_VP;     /* BUTTON_VP: the shield in the header */
	return UI_NONE;
}

/* ------------------------------------------- event window (SPEC_UI §7, choices ours) */
/* §7 gives the message window (bgbox_event 360x328, title at y=24, text from y=68). How v1.09
 * shows the A-D choices is NIEZNANE (§14 q1), so the choices are text buttons (tb_* pieces,
 * 16 + 8n + 16 x 38, §5) at the bottom of the same box: one row of up to two, two rows for 3-4. */
static Sprite g_evbox, g_tb[3][3];                  /* [plain/over/down][l/c/r] */
static int g_ev_ok, g_evx, g_evy, g_ev_n, g_ev_bx[4], g_ev_by[4], g_ev_bw[4], g_ev_a[4];

static void ev_load(void)
{
	static const char *st[3] = { "plain", "over", "down" }, *pc[3] = { "l", "c", "r" };
	char p[64];
	int i, k;
	if (g_ev_ok) return;
	load(&g_evbox, g_gfx, "gfx/interface/bgbox_event.bmp", g_inv, 1);
	for (i = 0; i < 3; i++)
		for (k = 0; k < 3; k++) { snprintf(p, sizeof p, "gfx/interface/tb_%s_%s.bmp", st[i], pc[k]); load(&g_tb[i][k], g_gfx, p, g_inv, 1); }
	g_ev_ok = 1;
}

static int g_tb_grey;                     /* the next text_button is disabled: grey label */
static void text_button(unsigned char *dst, int pitch, int x, int y, int w, const char *label, int state)
{
	const Sprite *l = &g_tb[state][0], *c = &g_tb[state][1], *r = &g_tb[state][2];
	int xx, cw = c->w ? c->w : S(8);
	put(l, 0, dst, pitch, x, y);
	for (xx = x + l->w; xx < x + w - r->w; xx += cw) put(c, 0, dst, pitch, xx, y);
	put(r, 0, dst, pitch, x + w - r->w, y);
	{	/* the label in the side face, else the small / tight one when it does not fit the button */
		Font *f = &g_fside; int hf = g_have_fside;
		if (hf && font_width(f, label) > w - S(10) && g_have_fsmall) f = &g_fsmall;
		if (hf && font_width(f, label) > w - S(10) && g_have_ftight) f = &g_ftight;
		text_at(f, hf, dst, pitch, x + w / 2, y + (l->h - f->cell) / 2, label, g_tb_grey ? nearest(g_mappal, 128, 128, 128) : g_pen_white, 1);
	}
	g_tb_grey = 0;
}

/* word wrap into at most maxl lines of maxw pixels (text.csv "\n" = break); returns the count */
#define EVL 48
static int wrap(Font *f, const char *s, int maxw, char out[][96], int maxl)
{
	int n = 0, len = 0;
	char line[96];
	while (*s && n < maxl) {
		const char *word = s;
		int wl = 0, brk = 0;
		while (s[wl] && s[wl] != ' ' && s[wl] != '\n' && !(s[wl] == '\\' && s[wl + 1] == 'n')) wl++;
		if (wl > 60) wl = 60;
		{
			char tmp[160];
			snprintf(tmp, sizeof tmp, "%.*s%s%.*s", len, line, len ? " " : "", wl, word);
			if (len && font_width(f, tmp) > maxw) { line[len] = 0; memcpy(out[n++], line, (size_t)len + 1); len = 0; if (n >= maxl) break; }
		}
		if (len && len < (int)sizeof line - 2) line[len++] = ' ';
		if (len + wl < (int)sizeof line - 1) { memcpy(line + len, word, (size_t)wl); len += wl; }
		s += wl;
		if (*s == ' ') s++;
		else if (*s == '\\' && s[1] == 'n') { s += 2; brk = 1; }
		else if (*s == '\n') { s++; brk = 1; }
		if (brk) { line[len] = 0; memcpy(out[n++], line, (size_t)len + 1); len = 0; }
	}
	if (len && n < maxl) { line[len] = 0; memcpy(out[n++], line, (size_t)len + 1); }
	return n;
}

static Sprite g_pgl, g_pgr, g_pgbox;
static int g_pg_x[2], g_pg_y, g_pg_on;

int ui_event_draw(unsigned char *dst, int pitch, const char *title, const char *date, const char *text,
                  const char *const action[4], int hover, int page)
{
	static char lines[EVL][96];
	int i, n = 0, idx[4], mx, my, mw, mh, cols = 2, rows, top, lh, per, nl, pages, y;
	ev_load();
	if (!g_pgl.w) {
		load(&g_pgl, g_gfx, "gfx/interface/btn_eventpage_left.bmp", g_inv, 4);
		load(&g_pgr, g_gfx, "gfx/interface/btn_eventpage_right.bmp", g_inv, 4);
		load(&g_pgbox, g_gfx, "gfx/interface/box_pagenumber.bmp", g_inv, 1);
	}
	ui_map_rect(&mx, &my, &mw, &mh);
	g_evx = mx + (mw - g_evbox.w) / 2; g_evy = (mh - g_evbox.h) / 2;
	if (g_evy < S(32)) g_evy = S(32);
	put(&g_evbox, 0, dst, pitch, g_evx, g_evy);
	text_at(&g_fbar, g_have_fbar, dst, pitch, g_evx + S(180), g_evy + S(24), title, g_pen_gold, 1);
	if (date) text_at(&g_fside, g_have_fside, dst, pitch, g_evx + S(180), g_evy + S(50), date, g_pen_white, 1);
	/* buttons: two columns when every label fits half the width, else one per row */
	for (i = 0; i < 4; i++) if (action[i]) {
		idx[n++] = i;
		if (g_have_fside && font_width(&g_fside, action[i]) > S(156) - S(28)) cols = 1;
	}
	if (n == 1) cols = 1;
	rows = (n + cols - 1) / cols;
	top = g_evy + g_evbox.h - S(18) - rows * S(42);
	g_ev_n = 0;
	for (i = 0; i < n; i++) {
		int w = cols == 1 ? S(n == 1 ? 200 : 320) : S(156);
		int x = cols == 1 ? g_evx + S(180) - w / 2 : g_evx + S(i % 2 ? 184 : 20), yb = top + (i / cols) * S(42);
		g_ev_bx[i] = x; g_ev_by[i] = yb; g_ev_bw[i] = w; g_ev_a[i] = idx[i];
		text_button(dst, pitch, x, yb, w, action[idx[i]], hover == idx[i] ? 1 : 0);
		g_ev_n++;
	}
	/* text pages between the date and the buttons (pager row when it does not fit) */
	lh = g_have_fside ? g_fside.cell : 16;
	nl = g_have_fside ? wrap(&g_fside, text ? text : "", S(290), lines, EVL) : 0;
	per = (top - S(6) - (g_evy + S(72))) / lh;
	g_pg_on = 0;
	if (nl > per) { per = (top - S(28) - (g_evy + S(72))) / lh; g_pg_on = 1; }
	if (per < 1) per = 1;
	pages = (nl + per - 1) / per;
	if (page >= pages) page = pages - 1;
	if (page < 0) page = 0;
	for (i = page * per, y = g_evy + S(72); i < nl && i < (page + 1) * per; i++, y += lh)
		text_at(&g_fside, 1, dst, pitch, g_evx + S(180), y, lines[i], g_pen_white, 1);
	if (g_pg_on) {                                   /* §7: left (142,229), number (172,229), right (204,229), moved above the buttons */
		char pn[8];
		g_pg_y = top - S(22);
		g_pg_x[0] = g_evx + S(142); g_pg_x[1] = g_evx + S(204);
		put(&g_pgl, page > 0 ? 0 : 3, dst, pitch, g_pg_x[0], g_pg_y);
		put(&g_pgbox, 0, dst, pitch, g_evx + S(172), g_pg_y - S(1));
		put(&g_pgr, page < pages - 1 ? 0 : 3, dst, pitch, g_pg_x[1], g_pg_y);
		snprintf(pn, sizeof pn, "%d", page + 1);
		text_at(&g_fside, g_have_fside, dst, pitch, g_evx + S(182), g_pg_y, pn, g_pen_white, 1);
	}
	return pages;
}

int ui_event_hit(int x, int y)
{
	int i, h = g_tb[0][0].h ? g_tb[0][0].h : S(38);
	for (i = 0; i < g_ev_n; i++)
		if (x >= g_ev_bx[i] && x < g_ev_bx[i] + g_ev_bw[i] && y >= g_ev_by[i] && y < g_ev_by[i] + h) return g_ev_a[i];
	if (g_pg_on && y >= g_pg_y && y < g_pg_y + g_pgl.h) {
		if (x >= g_pg_x[0] && x < g_pg_x[0] + g_pgl.w) return UI_EV_PREV;
		if (x >= g_pg_x[1] && x < g_pg_x[1] + g_pgr.w) return UI_EV_NEXT;
	}
	return -1;
}

/* ------------------------------------------------ budget window (iw_treasury, SPEC_UI §4.1) */
static Sprite g_iwt;
static void text_btn_c(unsigned char *dst, int pitch, int cx, int y, const char *label);
#define TW_X(v) S(v)
#define TW_Y(v) (S(103) + S(v))

static void budget_draw(unsigned char *dst, int pitch, const UiState *st)
{
	const EconBudget *b = st->budget;
	static const char *lab[ECON_SL_N] = { "TWLANDTECH", "TWNAVALTECH", "TWSTABILITY", "TWTRADE", "TWPRODUCTION", "TWTREASURY" };
	int i;
	char s[24];
	if (!g_iwt.w) {
		load(&g_iwt, g_gfx, "gfx/interface/iw_treasury.bmp", g_inv, 1);
		load(&g_track, g_gfx, "gfx/interface/sprite_slider.bmp", g_inv, 1);
		load(&g_knob, g_gfx, "gfx/interface/button area/btn_slider.bmp", g_inv, 4);
	}
	put(&g_iwt, 0, dst, pitch, 0, S(103));
	for (i = 0; i < ECON_SL_N; i++) {
		int y = 47 * i, kx;
		text_at(&g_fside, g_have_fside, dst, pitch, TW_X(20), TW_Y(16 + y) - g_fside.cell / 2, text_get(lab[i]), g_pen_white, 0);
		put(&g_track, 0, dst, pitch, TW_X(62), TW_Y(30 + y));
		kx = 62 + (int)((long)b->slider[i] * (132 - 16) / 1000);
		put(&g_knob, 0, dst, pitch, TW_X(kx), TW_Y(30 + y) + (g_track.h - g_knob.h) / 2);
		snprintf(s, sizeof s, "%d%%", (b->slider[i] + 5) / 10);
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, TW_X(194), TW_Y(39 + y) - g_fsmall.cell / 2, s, g_pen_white, 0);
		if (b->cost_m[i] > 0) {                  /* progress toward the next level (the progress bars' place is NIEPEWNE) */
			snprintf(s, sizeof s, "%ld/%ld", b->progress_m[i] / 1000, b->cost_m[i] / 1000);
			text_at(&g_fsmall, g_have_fsmall, dst, pitch, TW_X(66), TW_Y(46 + y) - g_fsmall.cell / 2, s, g_pen_gold, 0);   /* ours: under the track, clear of the boost button */
		}
	}
	g_bud_on = 1; g_bud_wt = st->wartax_state; g_bud_loan = st->loan_ok; g_bud_lsize = st->loan_size;
	if (!g_boost.w) load(&g_boost, g_gfx, "gfx/interface/button area/btn_boost.bmp", g_inv, 4);
	for (i = 0; i < 5; i++) {                                /* the one-time increase buttons at x 26, y 27 + 47 row (0x51c651..0x51c806) */
		g_bud_boost[i] = st->boost_ok[i];
		put(&g_boost, st->boost_ok[i] ? 0 : 3, dst, pitch, TW_X(26), TW_Y(27 + 47 * i));
	}
	ev_load();
	g_tb_grey = st->wartax_state != 0; text_button(dst, pitch, TW_X(66) - S(44), TW_Y(299), S(88), text_get("BUTTON_0209"), 0);    /* Raise War Taxes */
	g_tb_grey = !st->loan_ok;          text_button(dst, pitch, TW_X(158) - S(44), TW_Y(299), S(88), text_get("BUTTON_0200"), 0);   /* Take Bank Loan */
}

int ui_budget_boost_hit(int x, int y)
{
	int i, w = g_boost.w ? g_boost.w : S(24), h = g_boost.h ? g_boost.h : S(24);
	if (!g_bud_on) return -1;
	for (i = 0; i < 5; i++) if (x >= TW_X(26) && x < TW_X(26) + w && y >= TW_Y(27 + 47 * i) && y < TW_Y(27 + 47 * i) + h) return i;
	return -1;
}

/* two buttons of 88 px centred at x 66 and 158 (0x51c833 / 0x51c8a2; the width is ours: half the gap) */
int ui_budget_btn_hit(int x, int y)
{
	int h = g_tb[0][0].h ? g_tb[0][0].h : S(38);
	if (!g_bud_on || y < TW_Y(299) || y >= TW_Y(299) + h) return 0;
	if (x >= TW_X(66) - S(44) && x < TW_X(66) + S(44)) return 1;
	if (x >= TW_X(158) - S(44) && x < TW_X(158) + S(44)) return 2;
	return 0;
}

int ui_budget_hit(int x, int y, int *value)
{
	int i;
	for (i = 0; i < ECON_SL_N; i++) {
		int ty = TW_Y(30 + 47 * i), v;
		if (y < ty - S(8) || y > ty + S(14) || x < TW_X(56) || x > TW_X(200)) continue;
		v = (int)((long)(x - TW_X(62) - S(8)) * 1000 / S(116));
		*value = v < 0 ? 0 : v > 1000 ? 1000 : v;
		return i;
	}
	return -1;
}

/* ---------------------------------------------- domestic policy (iw_domestic, SPEC_UI §4.2) */
/* docs/SPEC_DOMESTIC.md 3: names at y = 16 + 36 i (top of the text), left at x = 16, right right-aligned at 208; minus at
 * (28, 34 + 36 i), track at (46, 34 + 36 i), plus at (180, 34 + 36 i). Slider 0 = the left name (DOMNAME_*_L). The buttons are
 * off (frame 3) at the ends and while the 10-year gate is closed; the knob is not draggable. */
static Sprite g_iwd, g_minus, g_plus;
static const char *dom_key[8] = { "ARI", "CEN", "INN", "MER", "OFF", "LAN", "QUA", "SER" };
#define DM_ROW(i) (S(103) + S(16 + 36 * (i)))
#define DM_BTN_Y(i) (S(103) + S(34 + 36 * (i)))

static void domestic_draw(unsigned char *dst, int pitch, const World *w, int c, const char *note)
{
	int i, closed = note && note[0];
	g_dom_on = c >= 0; g_dom_c = c; g_dom_closed = closed; g_dom_w = w;
	snprintf(g_dom_note_txt, sizeof g_dom_note_txt, "%s", closed ? note : "");
	char k[24];
	if (!g_iwd.w) {
		load(&g_iwd, g_gfx, "gfx/interface/iw_domestic.bmp", g_inv, 1);
		load(&g_minus, g_gfx, "gfx/interface/button area/btn_minus.bmp", g_inv, 4);
		load(&g_plus, g_gfx, "gfx/interface/button area/btn_plus.bmp", g_inv, 4);
		if (!g_track.w) load(&g_track, g_gfx, "gfx/interface/sprite_slider.bmp", g_inv, 1);
		if (!g_knob.w) load(&g_knob, g_gfx, "gfx/interface/button area/btn_slider.bmp", g_inv, 4);
	}
	put(&g_iwd, 0, dst, pitch, 0, S(103));
	for (i = 0; i < 8 && c >= 0; i++) {
		int v = w->country[c].policy[i], ty = DM_BTN_Y(i) + (g_minus.h - g_track.h) / 2;
		const char *l, *r;
		snprintf(k, sizeof k, "DOMNAME_%s_L", dom_key[i]); l = text_get(k);
		snprintf(k, sizeof k, "DOMNAME_%s_R", dom_key[i]); r = text_get(k);
		if (g_have_fsmall && font_width(&g_fsmall, l) + font_width(&g_fsmall, r) + S(8) > S(192)) {   /* too long: first words */
			static char l1[32], r1[32];
			char *p;
			snprintf(l1, sizeof l1, "%s", l); if ((p = strchr(l1, ' ')) != NULL) *p = 0;
			snprintf(r1, sizeof r1, "%s", r); if ((p = strchr(r1, ' ')) != NULL) *p = 0;
			l = l1; r = r1;
		}
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(16), DM_ROW(i), l, g_pen_white, 0);
		if (g_have_fsmall) text_at(&g_fsmall, 1, dst, pitch, S(208) - font_width(&g_fsmall, r), DM_ROW(i), r, g_pen_white, 0);
		put(&g_minus, closed || v <= 0 ? 3 : 0, dst, pitch, S(28), DM_BTN_Y(i));
		put(&g_track, 0, dst, pitch, S(46), ty);
		put(&g_knob, 0, dst, pitch, S(46) + (v < 0 ? 0 : v > 10 ? 10 : v) * (g_track.w - g_knob.w) / 10, ty + (g_track.h - g_knob.h) / 2);
		put(&g_plus, closed || v >= 10 ? 3 : 0, dst, pitch, S(180), DM_BTN_Y(i));
	}
}

/* §5 0x42a130: the stats block of slider i at value s (absolute effects, d = s - 5), lines DOMESTIC_<x> */
static void dom_line(char *o, int n, const char *key, long v100, int intval)
{
	const char *f = text_get(key), *p;
	int k = 0;
	char num[24];
	long a = v100 < 0 ? -v100 : v100;
	for (p = f; *p && k < n - 1; ) {
		if (p[0] == '%' && p[1] == 's') { if (v100 > 0 && k < n - 1) o[k++] = '+'; p += 2; continue; }
		if (p[0] == '%' && (p[1] == 'd' || (p[1] == '.' && p[3] == 'f'))) {
			int dec = p[1] == 'd' ? 0 : p[2] - '0';
			if (intval || dec == 0) snprintf(num, sizeof num, "%s%ld", v100 < 0 ? "-" : "", intval ? a / 100 : (a + 50) / 100);
			else if (dec == 1) snprintf(num, sizeof num, "%s%ld.%ld", v100 < 0 ? "-" : "", a / 100, (a % 100 + 5) / 10 > 9 ? 9 : (a % 100 + 5) / 10);
			else snprintf(num, sizeof num, "%s%ld.%02ld", v100 < 0 ? "-" : "", a / 100, a % 100);
			if (!strcmp(num, "-0") || !strcmp(num, "-0.0") || !strcmp(num, "-0.00")) memmove(num, num + 1, strlen(num));
			snprintf(o + k, (size_t)(n - k), "%s", num); k += (int)strlen(o + k);
			p += p[1] == 'd' ? 2 : 4;
			continue;
		}
		if (p[0] == '\\' && p[1] == 'n') { o[k++] = '\\'; if (k < n - 1) o[k++] = 'n'; p += 2; continue; }
		o[k++] = *p++;
	}
	o[k] = 0;
}
static void dom_stats(char *o, int n, int i, int s)
{
	static const struct { short slider; const char *key; short c100; unsigned char kind; } L[] = {   /* kind: 0 plain, 1 max(0,.), 2 halved when < 0, 3 int */
		{ 0, "DOMESTIC_A_CAV", -140, 0 }, { 0, "DOMESTIC_A_WAR", 200, 0 }, { 0, "DOMESTIC_DIP", 40, 0 }, { 0, "DOMESTIC_A_TE", -100, 0 }, { 0, "DOMESTIC_S_PRO", -100, 0 },
		{ 1, "DOMESTIC_C_REB", 120, 0 }, { 1, "DOMESTIC_C_WAR", 40, 0 }, { 1, "DOMESTIC_C_TECH", -200, 0 }, { 1, "DOMESTIC_C_PRO", 200, 0 },
		{ 2, "DOMESTIC_I_STAB", 500, 0 }, { 2, "DOMESTIC_I_WAR", 40, 0 }, { 2, "DOMESTIC_I_TECH", -300, 0 }, { 2, "DOMESTIC_I_MISS", -40, 0 }, { 2, "DOMESTIC_I_COL", -40, 0 },
		{ 3, "DOMESTIC_M_COL", -20, 0 }, { 3, "DOMESTIC_M_MER", -40, 0 }, { 3, "DOMESTIC_M_REF", 200, 1 }, { 3, "DOMESTIC_M_COS", -1000, 0 },
		{ 4, "DOMESTIC_O_ART", 200, 0 }, { 4, "DOMESTIC_O_SHO", 25, 3 }, { 4, "DOMESTIC_O_SIE", -25, 3 }, { 4, "DOMESTIC_O_MOR", 4, 0 },
		{ 5, "DOMESTIC_L_INF", -60, 0 }, { 5, "DOMESTIC_L_CAV", -100, 0 }, { 5, "DOMESTIC_L_WAR", 200, 0 }, { 5, "DOMESTIC_L_GAL", 60, 0 }, { 5, "DOMESTIC_L_TRA", 100, 0 },
		{ 5, "DOMESTIC_L_LAN", 10, 1 }, { 5, "DOMESTIC_L_NAV", -10, 1 }, { 5, "DOMESTIC_L_COL", -40, 2 }, { 5, "DOMESTIC_A_TE", -100, 0 }, { 5, "DOMESTIC_L_ADJ", 200, 0 },
		{ 5, "DOMESTIC_S_PRO", 100, 0 }, { 5, "DOMESTIC_Q_MAN", 500, 0 },
		{ 6, "DOMESTIC_Q_INF", 40, 0 }, { 6, "DOMESTIC_Q_CAV", 40, 0 }, { 6, "DOMESTIC_Q_LAN", 5, 0 }, { 6, "DOMESTIC_Q_FIR", 25, 3 }, { 6, "DOMESTIC_Q_MAN", -500, 0 },
		{ 7, "DOMESTIC_S_INF", -40, 0 }, { 7, "DOMESTIC_S_PRO", -200, 0 }, { 7, "DOMESTIC_S_STA", -1000, 0 }, { 7, "DOMESTIC_S_LAN", -5, 0 } };
	int k, d = s - 5, len = 0;
	o[0] = 0;
	for (k = 0; k < (int)(sizeof L / sizeof L[0]); k++) {
		long v;
		if (L[k].slider != i) continue;
		v = (long)L[k].c100 * d;
		if (L[k].kind == 1 && v < 0) v = 0;
		if (L[k].kind == 2 && v < 0) v /= 2;
		if (L[k].kind == 3) v = v / 100 * 100;              /* int(), toward zero */
		dom_line(o + len, n - len, L[k].key, v, L[k].kind == 3);
		len = (int)strlen(o);
		if (i == 4 && k + 1 < (int)(sizeof L / sizeof L[0]) && L[k + 1].slider != 4) {   /* "%s: %s%.1f\%" Fortress, 5 x the raw slider */
			snprintf(o + len, (size_t)(n - len), "%s: %s%d.0\\%%", text_get("EYRTSFORTLEVEL"), s > 0 ? "+" : "", 5 * s);
			len = (int)strlen(o);
		}
	}
}

int ui_domestic_hit(int x, int y)
{
	int i;
	for (i = 0; i < 8; i++) {
		int by = DM_BTN_Y(i);
		if (y < by || y >= by + (g_minus.h ? g_minus.h : S(16))) continue;
		if (x >= S(28) && x < S(28) + (g_minus.w ? g_minus.w : S(16))) return 2 * i;
		if (x >= S(180) && x < S(180) + (g_plus.w ? g_plus.w : S(16))) return 2 * i + 1;
	}
	return -1;
}

/* ------------------------------------------- sidebar windows decoded from eu2.exe (re/layouts_iw.md)
 * Financial summary iw_budget (draw 0x5185c0), land / naval technology iw_tech (0x513ea0), country &
 * monarch iw_country (0x517180). Positions are window-relative, (0,103) on screen. y is the top of
 * the text. The values our simulation does not have yet are left out, not invented. */
static Font g_f14, g_f24;
static int g_have_f14 = -1, g_have_f24 = -1, g_pen_green, g_pen_red;
static Sprite g_iwb, g_iwtech, g_iwc, g_religions, g_relbtn, g_icon3[3];
#define IW_X(v) S(v)
#define IW_Y(v) (S(103) + S(v))

static void iw_fonts(void)
{
	if (g_have_f14 >= 0) return;
	if (g_scale >= 1000) {
		g_have_f14 = font_load(&g_f14, g_gfx, "garamond14") == 0;
		g_have_f24 = font_load(&g_f24, g_gfx, "garamond24bold") == 0;
	} else {
		char p[96];
		snprintf(p, sizeof p, "%sfx6x9.euf", g_fontdir); g_have_f14 = font_load_euf(&g_f14, p) == 0 || font_load(&g_f14, g_gfx, "garamond12") == 0;   /* 14 x 0.8 = 11 px */
		snprintf(p, sizeof p, "%sfx6x10.euf", g_fontdir); g_have_f24 = font_load_euf(&g_f24, p) == 0 || font_load(&g_f24, g_gfx, "garamond14bold") == 0;
	}
	g_pen_green = nearest(g_mappal, 40, 200, 40); g_pen_red = nearest(g_mappal, 220, 30, 30);
}

void ui_unit_count(unsigned char *dst, int pitch, int sw, int sh, int x, int y, long n)
{
	static unsigned char half[256];
	static int half_ok;
	char s[12];
	int w, h, i, j;
	iw_fonts();
	if (!g_have_f14) return;
	if (!half_ok) { for (i = 0; i < 256; i++) half[i] = (unsigned char)nearest(g_mappal, g_mappal[3 * i] / 2, g_mappal[3 * i + 1] / 2, g_mappal[3 * i + 2] / 2); half_ok = 1; }
	snprintf(s, sizeof s, "%ld", n);
	w = font_width(&g_f14, s) + 2; h = g_f14.cell;
	for (j = y < 0 ? 0 : y; j < y + h && j < sh; j++) {            /* 0x4024c0: (c >> 1) & 0x7bef, i.e. half */
		unsigned char *r = dst + (long)j * pitch;
		for (i = x < 0 ? 0 : x; i < x + w && i < sw; i++) r[i] = half[r[i]];
	}
	font_draw(&g_f14, dst, pitch, sw, sh, x + 1, y + 1, s, g_pen_white);
}

static void text_right(Font *f, int have, unsigned char *dst, int pitch, int xr, int y, const char *s, int pen)
{
	if (have) font_draw(f, dst, pitch, g_sw, g_sh, xr - font_width(f, s), y, s, pen);
}

/* ducats x1000 as "%.1f" without floats (locale-free), sign kept */
static void fmt1(char *s, int n, long m)
{
	long a = m < 0 ? -m : m, t = (a + 50) / 100;
	snprintf(s, (size_t)n, "%s%ld.%ld", m < 0 && t ? "-" : "", t / 10, t % 10);
}

static void iw_close(unsigned char *dst, int pitch, const char *key)
{
	const char *l = text_get(key);
	int w = (g_have_fside ? font_width(&g_fside, l) : 40) + S(32);
	ev_load();
	text_button(dst, pitch, IW_X(112) - w / 2, IW_Y(319), w, l, 0);
}

int ui_iw_close_hit(int x, int y)
{
	return x >= IW_X(60) && x < IW_X(164) && y >= IW_Y(315) && y < IW_Y(315) + S(40);
}

static void finance_draw(unsigned char *dst, int pitch, const UiState *st)
{
	const EconReport *r = st->report;
	const EconBudget *b = st->budget;
	char v[24];
	long inc, fixed, inv, pool;
	int i;
	static const char *ik[5] = { "IWBLTECH", "IWBNTECH", "IWBSTB", "IWBPE", "IWBTE" };
	static const int is[5] = { ECON_SL_LAND, ECON_SL_NAVAL, ECON_SL_STAB, ECON_SL_PROD, ECON_SL_TRADE };
	iw_fonts();
	if (!g_iwb.w) load(&g_iwb, g_gfx, "gfx/interface/iw_budget.bmp", g_inv, 1);
	put(&g_iwb, 0, dst, pitch, 0, S(103));
	if (!r || !b) return;
#define LAB(y, key, f, hf, pen) text_at(f, hf, dst, pitch, IW_X(25), IW_Y(y), text_get(key), pen, 0)
#define AMT(y, val, pen) do { fmt1(v, sizeof v, val); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(y), v, pen); } while (0)
	LAB(16, "IWBINCOME", &g_fside, g_have_fside, g_pen_white);
	LAB(32, "IWBTAXATION", &g_f14, g_have_f14, g_pen_gold);   AMT(32, r->tax + r->tolls + r->census, g_pen_white);   /* NIEPEWNE: which of our parts are T */
	LAB(46, "IWBPRODUCTION", &g_f14, g_have_f14, g_pen_gold); AMT(46, r->production, g_pen_white);
	LAB(60, "IWBTRADE", &g_f14, g_have_f14, g_pen_gold);      AMT(60, r->trade, g_pen_white);
	LAB(74, "IWBGOLD", &g_f14, g_have_f14, g_pen_gold);       AMT(74, r->gold, g_pen_white);
	inc = r->tax + r->tolls + r->census + r->production + r->trade + r->gold;
	LAB(88, "IWBTOTAL", &g_fside, g_have_fside, g_pen_gold);  AMT(88, inc, g_pen_green);
	LAB(110, "IWBFEXPENSE", &g_fside, g_have_fside, g_pen_white);
	LAB(126, "IWBMAINTEN", &g_f14, g_have_f14, g_pen_gold);   AMT(126, -r->maintenance, g_pen_white);
	LAB(140, "IWBINTEREST", &g_f14, g_have_f14, g_pen_gold);  AMT(140, -st->interest_m, g_pen_white);
	fixed = r->maintenance + st->interest_m;
	LAB(154, "IWBTOTAL", &g_fside, g_have_fside, g_pen_gold); AMT(154, -fixed, fixed > 0 ? g_pen_red : g_pen_gold);
	LAB(171, "IWBBEXP", &g_fside, g_have_fside, g_pen_white);
	pool = b->income_m;
	for (i = 0, inv = 0; i < 5; i++) {
		long part = pool / 1000 * b->slider[is[i]] + pool % 1000 * b->slider[is[i]] / 1000;   /* 32-bit safe */
		inv += part;
		LAB(187 + 14 * i, ik[i], &g_f14, g_have_f14, g_pen_gold); AMT(187 + 14 * i, part, g_pen_white);
	}
	LAB(257, "IWBTOTAL", &g_fside, g_have_fside, g_pen_gold); AMT(257, -inv, -inv < 0 ? g_pen_red : g_pen_gold);
	LAB(281, "IWBMONTHLY", &g_f14, g_have_f14, g_pen_white);
	{ long bal = pool / 1000 * b->slider[ECON_SL_TREAS] + pool % 1000 * b->slider[ECON_SL_TREAS] / 1000; AMT(281, bal, bal > 0 ? g_pen_green : g_pen_red); }   /* the treasury share of the pool */
	LAB(305, "IWBINFLATION", &g_f14, g_have_f14, g_pen_white);
	{ long u = st->inflation_u, t = (u + 500) / 1000; snprintf(v, sizeof v, "%ld.%ld%%", t / 10, t % 10); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(305), v, g_pen_white); }
#undef LAB
#undef AMT
	iw_close(dst, pitch, "CLOSE");
}

/* land (naval = 0/1): title, level, research progress, totals, maintenance and its slider (0.5 .. 1.0) */
#define TECH_SLX 48
#define TECH_SLY 207
static void tech_draw(unsigned char *dst, int pitch, const UiState *st, int naval)
{
	const EconBudget *b = st->budget;
	char v[48];
	int row = naval ? ECON_SL_NAVAL : ECON_SL_LAND, kx;
	iw_fonts();
	if (!g_iwtech.w) load(&g_iwtech, g_gfx, "gfx/interface/iw_tech.bmp", g_inv, 1);
	if (!g_track.w) load(&g_track, g_gfx, "gfx/interface/sprite_slider.bmp", g_inv, 1);
	if (!g_knob.w) load(&g_knob, g_gfx, "gfx/interface/button area/btn_slider.bmp", g_inv, 4);
	put(&g_iwtech, 0, dst, pitch, 0, S(103));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(20), text_get(naval ? "MILNAVAL" : "MILLAND"), g_pen_gold, 1);
	snprintf(v, sizeof v, "%d", st->tech_level);                          /* the era name of LAND/NAVAL.csv: NIEPEWNE, the level for now */
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(37), v, g_pen_white, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(101), text_get("MILWRESEARCH"), g_pen_gold, 0);
	if (b && b->cost_m[row] > 0) snprintf(v, sizeof v, "(%ld$ / %ld$)", b->progress_m[row] / 1000, b->cost_m[row] / 1000);
	else snprintf(v, sizeof v, "---");
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(140), IW_Y(101), v, g_pen_white, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(131), text_get(naval ? "MILNAVALMA" : "MILLANDMA"), g_pen_gold, 0);
	if (st->support >= 0) snprintf(v, sizeof v, "%ld", st->support); else snprintf(v, sizeof v, "---");   /* "%d" 0x45ab50 */
	text_right(&g_f14, g_have_f14, dst, pitch, IW_X(196), IW_Y(131), v, g_pen_white);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(150), text_get("MILTOTALS"), g_pen_gold, 0);
	snprintf(v, sizeof v, "%ld", st->units[0]); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(106), IW_Y(150), v, g_pen_white);
	snprintf(v, sizeof v, "%ld", st->units[1]); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(150), IW_Y(150), v, g_pen_white);
	snprintf(v, sizeof v, "%ld", st->units[2]); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(196), IW_Y(150), v, g_pen_white);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(169), text_get("MILCOST"), g_pen_gold, 0);
	fmt1(v, sizeof v, st->maint_month); strcat(v, "$");
	text_right(&g_f14, g_have_f14, dst, pitch, IW_X(198), IW_Y(169), v, g_pen_white);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(189), text_get("MILMAIN"), g_pen_gold, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(22), IW_Y(203), "50%", g_pen_gold, 0);
	text_right(&g_f14, g_have_f14, dst, pitch, IW_X(210), IW_Y(203), "100%", g_pen_gold);
	put(&g_track, 0, dst, pitch, IW_X(TECH_SLX), IW_Y(TECH_SLY));
	kx = (int)((long)(st->maint_level - 500) * (g_track.w - g_knob.w) / 500);
	put(&g_knob, 0, dst, pitch, IW_X(TECH_SLX) + kx, IW_Y(TECH_SLY) + (g_track.h - g_knob.h) / 2);
	iw_close(dst, pitch, "CLOSE");
}

/* the maintenance slider under (x,y): level x1000 (500..1000), or -1 */
int ui_tech_hit(int x, int y)
{
	int x0 = IW_X(TECH_SLX), w = g_track.w ? g_track.w : S(132), kw = g_knob.w ? g_knob.w : S(16), ty = IW_Y(TECH_SLY), v;
	if (y < ty - S(8) || y > ty + S(14) || x < x0 - S(8) || x > x0 + w + S(8)) return -1;
	v = 500 + (int)((long)(x - x0 - kw / 2) * 500 / (w - kw));
	return v < 500 ? 500 : v > 1000 ? 1000 : v;
}

static void country_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const Country *c;
	char v[48];
	const Sprite *s;
	iw_fonts();
	if (!g_iwc.w) {
		load(&g_iwc, g_gfx, "gfx/interface/iw_country.bmp", g_inv, 1);
		load(&g_religions, g_gfx, "gfx/interface/religions.bmp", g_inv, 11);
		load(&g_relbtn, g_gfx, "gfx/interface/button area/btn_religion_shield.bmp", g_inv, 4);
		load(&g_icon3[0], g_gfx, "gfx/interface/icon_diplomatic.bmp", g_inv, 1);
		load(&g_icon3[1], g_gfx, "gfx/interface/icon_admin.bmp", g_inv, 1);
		load(&g_icon3[2], g_gfx, "gfx/interface/icon_military.bmp", g_inv, 1);
	}
	put(&g_iwc, 0, dst, pitch, 0, S(103));
	g_cow_vas = st->nvas > 0;                          /* COW_VASSAL "Create Vassal" (112,117), when there is a candidate (0x43d020) */
	if (g_cow_vas) { ev_load(); text_btn_c(dst, pitch, IW_X(112), IW_Y(117), text_get("COW_VASSAL")); }
	if (st->player < 0) return;
	c = &w->country[st->player];
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(13), text_get(c->tag), g_pen_gold, 1);
	snprintf(v, sizeof v, "gfx/map/shields/small/smallshield_%s.bmp", c->tag);
	if ((s = cached(v, 4)) != NULL && s->w) put(s, 0, dst, pitch, IW_X(28), IW_Y(63));
	else { snprintf(v, sizeof v, "gfx/map/shields/shield_%s.bmp", ftag(w, (int)(c - w->country))); if ((s = cached(v, 4)) != NULL) put(s, 0, dst, pitch, IW_X(28), IW_Y(63)); }
	put(&g_relbtn, 0, dst, pitch, IW_X(22), IW_Y(54));
	put(&g_religions, religion_frame(world_name(w, c->religion) ? world_name(w, c->religion) : ""), dst, pitch, IW_X(172), IW_Y(61));
	put(&g_relbtn, 0, dst, pitch, IW_X(162), IW_Y(54));
	if (st->rank > 0) { snprintf(v, sizeof v, "#%d", st->rank); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(61), v, g_pen_white, 1); }
	snprintf(v, sizeof v, "%ld", c->vp); text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(94), v, g_pen_white, 1);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(148), text_get((int)(c - w->country) == w->emperor ? "COWEMP" : "COWMONARCH"), g_pen_gold, 1);   /* 0x43ce10 */
	if (c->m_name) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(164), c->m_name, g_pen_white, 1);
	put(&g_icon3[0], 0, dst, pitch, IW_X(42), IW_Y(181));
	put(&g_icon3[1], 0, dst, pitch, IW_X(94), IW_Y(181));
	put(&g_icon3[2], 0, dst, pitch, IW_X(146), IW_Y(181));
	{	/* skill pips (prickar.bmp), 1 + trunc(4.999 v / 9) clamped 1..5, centred at x = 58 / 112 / 168, y = 211; DIP ADM MIL (NIEPEWNE order) */
		static Sprite pip;
		const int vx[3] = { 58, 112, 168 }, val[3] = { c->m_dip ? c->m_dip : 3, c->m_adm ? c->m_adm : 3, c->m_mil ? c->m_mil : 3 };
		int k, j;
		if (!pip.w) load(&pip, g_gfx, "gfx/interface/prickar.bmp", g_inv, 5);   /* 5 frames: frame N-1 drawn N times (0x514c50) */
		for (k = 0; k < 3 && pip.w; k++) {
			int n = 1 + 4999 * val[k] / 9000;
			if (n > 5) n = 5;
			for (j = 0; j < n; j++) put(&pip, n - 1, dst, pitch, IW_X(vx[k]) - n * (pip.w + 2) / 2 + j * (pip.w + 2), IW_Y(211));
		}
	}
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(32), IW_Y(231), text_get("COWSTABILITY"), g_pen_gold, 0);
	snprintf(v, sizeof v, c->stability > 0 ? "+%d" : "%d", c->stability); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(231), v, g_pen_white);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(32), IW_Y(245), text_get("COWLAND"), g_pen_gold, 0);
	snprintf(v, sizeof v, "%d", c->land_tech); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(245), v, g_pen_white);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(32), IW_Y(259), text_get("COWNAVAL"), g_pen_gold, 0);
	snprintf(v, sizeof v, "%d", c->naval_tech); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(259), v, g_pen_white);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(283), text_get("COWCULTURE"), g_pen_gold, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(297), world_name(w, c->culture) ? world_name(w, c->culture) : "", g_pen_white, 1);
	iw_close(dst, pitch, "BACK");
}

/* foreign province (iw_hprovince, SPEC_UI 4.6) and sea zone (iw_sea, 4.7), window-relative */
static Sprite g_iwh, g_iwsea, g_sea_pic[3], g_cot, g_anim_value;

static const Sprite *small_shield(const World *w, int c)
{
	char p[64];
	const Sprite *s;
	if (c < 0) return NULL;
	snprintf(p, sizeof p, "gfx/map/shields/small/smallshield_%s.bmp", w->country[c].tag);
	s = cached(p, 4);
	return s && s->w ? s : NULL;
}

/* "Very Low (%d)" style tiers of HPWTAX / HPWTRA; key text from text.csv holds the %d */
static void tier_text(char *out, int n, long v, const long *lim, const char *const *key, int nk)
{
	int i;
	for (i = 0; i < nk - 1 && v >= lim[i]; i++) ;
	snprintf(out, (size_t)n, text_get(key[i]), (int)v);
}

static void hprov_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int id)
{
	const Province *p = &w->prov[id];
	const Sprite *s;
	char v[64];
	int lh;
	static const long inc_lim[5] = { 1, 6, 11, 21, 51 };
	static const char *const inc_key[6] = { "HPNONE", "HPVLOW", "HPLOW", "HPMEDIUM", "HPHIGH", "HPVHIGH" };
	iw_fonts();
	if (!g_iwh.w) {
		load(&g_iwh, g_gfx, "gfx/interface/iw_hprovince.bmp", g_inv, 1);
		load(&g_cot, g_gfx, "gfx/interface/cot.bmp", g_inv, 1);
		load(&g_anim_value, g_gfx, "gfx/interface/anim_value.bmp", g_inv, 6);
		if (!g_religions.w) load(&g_religions, g_gfx, "gfx/interface/religions.bmp", g_inv, 11);
	}
	put(&g_iwh, 0, dst, pitch, 0, S(103));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(21), p->name ? p->name : "", g_pen_gold, 1);
	if ((s = small_shield(w, p->controller)) != NULL) put(s, 0, dst, pitch, IW_X(24), IW_Y(19));
	if ((s = small_shield(w, p->owner)) != NULL) put(s, 0, dst, pitch, IW_X(172), IW_Y(19));
	lh = g_fside.cell;
	if (p->owner >= 0) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(21) + lh, text_get(w->country[p->owner].tag), g_pen_gold, 1);
#define ROW(y, key, val) do { text_lim(&g_f14, g_have_f14, dst, pitch, IW_X(66), IW_Y(y), text_get(key), g_pen_gold, IW_X(146) - IW_X(66) - 2); \
                              text_at(&g_f14, g_have_f14, dst, pitch, IW_X(146), IW_Y(y), val, g_pen_gold, 0); } while (0)
	snprintf(v, sizeof v, "%ld", p->population); ROW(54, "HPWPOP", v);                      /* growth %: not shown (NIEZNANE here) */
	snprintf(v, sizeof v, "%d%%", st->revolt); ROW(68, "HPWREVOLT", v);
	fmt1(v, sizeof v, p->manpower_m); ROW(82, "HPWMANPW", v);
	{ char k[12]; snprintf(k, sizeof k, "EYRFORT%d", p->fort >= 1 && p->fort <= 6 ? p->fort : 0); ROW(96, "HPWFORT", text_get(k)); }
	text_lim(&g_f14, g_have_f14, dst, pitch, IW_X(66), IW_Y(110), text_get("HPWSUPPLY"), g_pen_gold, IW_X(146) - IW_X(66) - 2);
	snprintf(v, sizeof v, "%ld", st->supply);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(146), IW_Y(110), v, st->supply_col == 2 ? g_pen_green : st->supply_col == 1 ? nearest(g_mappal, 255, 255, 0) : g_pen_red, 0);
	snprintf(v, sizeof v, "%ld%%", st->attrition); ROW(124, "HPWATTRITION", v);
#undef ROW
	put(&g_religions, religion_frame(world_name(w, p->religion) ? world_name(w, p->religion) : ""), dst, pitch, IW_X(28), IW_Y(100));
	if (world_name(w, p->culture))
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(28) + g_religions.w / 2, IW_Y(100) + g_religions.h, world_name(w, p->culture), g_pen_gold, 1);   /* centred under the icon (alignment NIEPEWNE) */
	if (st->cot_name) {
		put(&g_cot, 0, dst, pitch, IW_X(24), IW_Y(150));
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(132), IW_Y(160), text_get("HPWCOT"), g_pen_gold, 1);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(132), IW_Y(177), st->cot_name, g_pen_gold, 1);
	}
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(132), IW_Y(216), text_get("HPWTAX"), g_pen_gold, 1);
	{
		long inc = world_income_value_m(w, id) / 1000;   /* 0x47d970 (ours: base tax, less the pirates off an unfortified coast) */
		int t = 0;
		while (t < 5 && inc >= inc_lim[t]) t++;
		tier_text(v, sizeof v, inc, inc_lim, inc_key, 6);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(132), IW_Y(235), v, g_pen_gold, 1);
		put(&g_anim_value, t, dst, pitch, IW_X(16), IW_Y(205));
	}
	{	/* trade good icon (resource.bmp frame = good, as in the PIW) */
		static const char *gl[20] = { "clo", "gold", "cot", "fish", "furs", "grai", "ivor", "metal", "mineral", "orient",
		                              "navs", "salt", "slav", "spic", "sug", "tob", "wine", "wool", "tea", "coffee" };
		const char *gn = world_name(w, p->goods);
		int i;
		for (i = 0; i < 20 && gn; i++)
			if (script_ieq(gn, gl[i]) && (s = cached("gfx/interface/piw/buildings/resource.bmp", 21)) != NULL) {
				put(s, i + 1, dst, pitch, IW_X(16), IW_Y(258));
				if (g_npiw < 16) {                     /* a button: window 0x11 (0x5015cd) */
					g_piw[g_npiw].x = IW_X(16); g_piw[g_npiw].y = IW_Y(258); g_piw[g_npiw].w = s->w; g_piw[g_npiw].h = s->h;
					g_piw[g_npiw].id = PIW_RESOURCE; g_npiw++;
				}
			}
	}
	iw_close(dst, pitch, "BACK");
}

static void sea_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int id)
{
	const Province *p = &w->prov[id];
	const char *wk;
	int k, pic, x;
	iw_fonts();
	if (!g_iwsea.w) {
		load(&g_iwsea, g_gfx, "gfx/interface/iw_sea.bmp", g_inv, 1);
		load(&g_sea_pic[0], g_gfx, "gfx/interface/good.bmp", g_inv, 1);
		load(&g_sea_pic[1], g_gfx, "gfx/interface/bad.bmp", g_inv, 1);
		load(&g_sea_pic[2], g_gfx, "gfx/interface/seaice.bmp", g_inv, 1);
	}
	put(&g_iwsea, 0, dst, pitch, 0, S(103));
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(20), p->name ? p->name : "", g_pen_white, 1);
	pic = p->ice && p->winter ? 2 : p->storm_now ? 1 : 0;   /* ice in its winter (0x481910), the month's storm (+0x248) */
	wk = pic == 2 ? "SEA_ICEY" : pic == 1 ? "SEA_STORMY" : "SEA_CLEAR";
	put(&g_sea_pic[pic], 0, dst, pitch, IW_X(20), IW_Y(54));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(165), text_get(wk), g_pen_gold, 1);
	{	/* SEA_WIND "Wind Factor: %d m/s" = the month's wind (+0xA2) x 5 (0x507641) */
		char v[64];
		snprintf(v, sizeof v, text_get("SEA_WIND"), p->wind * 5);
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(180), v, g_pen_gold, 1);
	}
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(220), text_get("SEA_NATIONAL"), g_pen_gold, 1);
	for (k = 0, x = IW_X(16); k < st->nnat && k < 10; k++) {
		const Sprite *s = small_shield(w, st->nat[k]);
		if (s) { put(s, 0, dst, pitch, x, IW_Y(230)); x += s->w; }
	}
	if (st->pirates > 0) {
		char v[64];
		const Sprite *s = small_shield(w, world_country(w, "PIR"));
		snprintf(v, sizeof v, text_get("SEA_PIRATES"), st->pirates);
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(268), v, g_pen_gold, 1);
		for (k = 0, x = IW_X(16); s && k < st->pirates && k < 8; k++, x += s->w) put(s, 0, dst, pitch, x, IW_Y(282));
	} else text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(284), text_get("SEA_NOPIRATES"), g_pen_gold, 1);
	{	/* SEA_PIRB, centred (112,319) */
		const char *l = text_get("SEA_PIRB");
		int bw = (g_have_fside ? font_width(&g_fside, l) : 120) + S(32);
		ev_load();
		g_tb_grey = st->priv_state != 0;
		text_button(dst, pitch, IW_X(112) - bw / 2, IW_Y(319), bw, l, 0);
	}
}

/* ------------------------------------------- diplomacy (SPEC_UI 4.8): overview iw_drlw in the sidebar,
 * action panel iw_draw at screen (224,103) over the map, confirm popup at screen (74,155) */
static Sprite g_iwdrlw, g_iwdraw, g_dipconf, g_radio;
static int g_pen_dorange, g_have_arial = -1;
static Font g_arial;
static int g_dip_rows[32], g_dip_rowy[32], g_ndip_rows, g_dip_rowh;      /* action rows of the last drawn panel */

/* text with the game's colour escapes: byte 0xA7 + letter (Y = gold, W = white, others: the default)
 * and a literal backslash-n as a line break (text.csv). Word-wrapped to maxw; returns the next y. */
static int rich_width(const Font *f, const char *s, int n)
{
	int w = 0, i;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == 0xA7 && i + 1 < n) { i++; continue; }
		w += f->adv[c];
	}
	return w;
}

static int rich_block(Font *f, int have, unsigned char *dst, int pitch, int x, int y, int maxw, int maxh, const char *s, int pen0, int centre)
{
	int pen = pen0, lh = f->cell, y0 = y;
	if (!have) return y;
	while (*s && y - y0 + lh <= maxh) {
		const char *ls = s, *brk = NULL;
		int w = 0, nl = 0, n, cx, i, linepen = pen;
		/* take words while they fit */
		for (;;) {
			const char *e = s;
			while (*e && *e != ' ' && !(e[0] == '\\' && e[1] == 'n')) e++;
			if (w && w + rich_width(f, s, (int)(e - s)) > maxw) break;
			w += rich_width(f, s, (int)(e - s));
			s = e; brk = s;
			if (*s == '\\' && s[1] == 'n') { nl = 1; break; }
			if (!*s) break;
			if (w + f->adv[' '] + 1 > maxw) break;
			w += f->adv[' ']; s++;
		}
		if (!brk) { brk = s + 1; s = brk; }                      /* one word wider than the box */
		n = (int)(brk - ls);
		cx = centre ? x - rich_width(f, ls, n) / 2 : x;
		for (i = 0; i < n; i++) {
			unsigned char c = (unsigned char)ls[i];
			char one[2];
			if (c == 0xA7 && i + 1 < n) { char k = ls[++i]; linepen = k == 'Y' ? g_pen_gold : k == 'W' ? g_pen_white : pen0; continue; }
			one[0] = (char)c; one[1] = 0;
			font_draw(f, dst, pitch, g_sw, g_sh, cx, y, one, linepen);
			cx += f->adv[c];
		}
		pen = linepen;
		if (nl) s += 2;
		while (*s == ' ') s++;
		y += lh;
	}
	return y;
}

static void dip_load(void)
{
	iw_fonts();
	if (g_iwdrlw.w) return;
	load(&g_iwdrlw, g_gfx, "gfx/interface/iw_drlw.bmp", g_inv, 1);
	load(&g_iwdraw, g_gfx, "gfx/interface/iw_draw.bmp", g_inv, 1);
	load(&g_dipconf, g_gfx, "gfx/interface/dip_confirmpopup.bmp", g_inv, 1);
	load(&g_radio, g_gfx, "gfx/settings/btn_radio.bmp", g_inv, 4);
	g_pen_dorange = nearest(g_mappal, 255, 192, 0);
	if (g_scale >= 1000) g_have_arial = font_load(&g_arial, g_gfx, "arial10") == 0;
	else { char p[96]; snprintf(p, sizeof p, "%sfx6x9.euf", g_fontdir); g_have_arial = font_load_euf(&g_arial, p) == 0 || font_load(&g_arial, g_gfx, "arial10") == 0; }
	ev_load();
}

static void text_btn_c(unsigned char *dst, int pitch, int cx, int y, const char *label)   /* text button centred on cx */
{
	int w = (g_have_fside ? font_width(&g_fside, label) : 40) + S(18);
	text_button(dst, pitch, cx - w / 2, y, w, label, 0);
}

static int rel_pen(long v)
{
	return v >= 175 ? nearest(g_mappal, 0, 0x60, 0) : v >= 125 ? nearest(g_mappal, 0, 0xA4, 0) : v >= 75 ? nearest(g_mappal, 0x80, 0xFF, 0) :
	       v > -75 ? nearest(g_mappal, 0xE0, 0xE0, 0xE0) : v > -125 ? nearest(g_mappal, 0xE0, 0x80, 0) : v > -175 ? nearest(g_mappal, 0xA0, 0x40, 0) : nearest(g_mappal, 0x80, 0, 0);
}

static void dip_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const Sprite *s;
	char v[96];
	int i;
	dip_load();
	put(&g_iwdrlw, 0, dst, pitch, 0, S(103));
	for (i = 0; i < 2; i++) put(&g_radio, st->dip_view == i ? 2 : 0, dst, pitch, IW_X(26), IW_Y(20 + 18 * i));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(44), IW_Y(22), text_get("DW_VIEW_ALL"), g_pen_dorange, 0);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(44), IW_Y(40), text_get("DW_VIEW_REL"), g_pen_dorange, 0);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(73), text_get(st->dip_view ? "DIP_CURR_REL" : "DIP_CURR_ALL"), g_pen_gold, 1);
	if (st->dip_sel >= 0) {
		text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(87), text_get(w->country[st->dip_sel].tag), g_pen_white, 1);
		if ((s = small_shield(w, st->dip_sel)) != NULL) { put(s, 0, dst, pitch, IW_X(22), IW_Y(73)); put(s, 0, dst, pitch, IW_X(174), IW_Y(73)); }
	}
	if (st->dip_view == 0) {
		if (st->dip_target >= 0 && st->player >= 0) {
			long r = st->dip_rel;
			snprintf(v, sizeof v, text_get("DW_OURREL"), r < 0 ? '-' : '+', (int)(r < 0 ? -r : r));
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(116), v, g_pen_dorange, 1);
		} else {
			char k[12];
			snprintf(k, sizeof k, "BADBOY%d", st->dip_badboy_tier);
			text_at(&g_arial, g_have_arial, dst, pitch, IW_X(112), IW_Y(116), text_get(k), g_pen_white, 1);
		}
		if (st->ndip_lines == 0) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(135), text_get("NOALLIANCES"), g_pen_white, 1);
		else {
			int y = IW_Y(135);
			for (i = 0; i < st->ndip_lines && y < IW_Y(135 + 139); i++)
				y = rich_block(&g_f14, g_have_f14, dst, pitch, IW_X(24), y, S(158), IW_Y(135 + 139) - y, st->dip_lines[i], g_pen_white, 0) + S(2);
		}
	} else {
		int y = IW_Y(135), lh = g_f14.cell;
		ev_load();
		/* sort headers BTNREL / BTNNAT (g14 text buttons in the original; ours: underlined labels, the tb_ pieces are too tall here) */
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(115), text_get("BTNREL"), g_pen_gold, 0);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24 + 78), IW_Y(115), text_get("BTNNAT"), g_pen_gold, 0);
		for (i = 0; i < st->ndip_list && y + lh <= IW_Y(135 + 139); i++, y += lh) {
			long r = st->dip_listv[i];
			snprintf(v, sizeof v, r > 0 ? "+%ld" : "%ld", r);
			text_right(&g_f14, g_have_f14, dst, pitch, IW_X(24 + 52), y, v, rel_pen(r));
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24 + 78), y, text_get(w->country[st->dip_list[i]].tag), g_pen_white, 0);
		}
	}
	if (!st->dip_confirm) {
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(292), text_get("DWCLICK1"), g_pen_dorange, 1);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(306), text_get("DWCLICK2"), g_pen_dorange, 1);
		text_btn_c(dst, pitch, IW_X(st->dip_target >= 0 ? 66 : 112), IW_Y(319), text_get("BACKTOMAIN"));
	}
	if (st->dip_target >= 0) text_btn_c(dst, pitch, IW_X(158), IW_Y(319), text_get("DWGOTO"));
	/* the action panel */
	g_ndip_rows = 0;
	if (st->dip_target >= 0) {
		int px = S(224), py = S(103), y, lh = g_f14.cell;
		put(&g_iwdraw, 0, dst, pitch, px, py);
		text_at(&g_f14, g_have_f14, dst, pitch, px + S(112), py + S(17), text_get("DIP_SENDDIPLO"), g_pen_gold, 1);
		text_at(&g_f24, g_have_f24, dst, pitch, px + S(112), py + S(31), text_get(w->country[st->dip_target].tag), g_pen_white, 1);
		text_at(&g_f14, g_have_f14, dst, pitch, px + S(112), py + S(55), text_get(st->dip_view ? "DIP_MODREL" : "DIP_MODALL"), g_pen_gold, 1);
		if ((s = small_shield(w, st->dip_target)) != NULL) { put(s, 0, dst, pitch, px + S(22), py + S(17)); put(s, 0, dst, pitch, px + S(174), py + S(17)); }
		g_dip_rowh = lh;
		for (i = 0, y = py + S(72); i < st->ndip_act && y + lh <= py + S(72 + 232); i++) {
			const UiDipAct *a = &st->dip_act[i];
			if (a->id < 0) {                                       /* separator: orange line at mid-height */
				int k;
				for (k = px + S(32); k < px + S(32 + 148); k++) dst[(long)(y + S(4)) * pitch + k] = (unsigned char)g_pen_dorange;
				y += S(8);
				continue;
			}
			text_at(&g_f14, g_have_f14, dst, pitch, px + S(32), y, a->label,
			        a->state != 2 ? nearest(g_mappal, 0x7F, 0x7F, 0x7F) : i == st->dip_hover ? g_pen_white : nearest(g_mappal, 0xFF, 0xC6, 0), 0);
			if (g_ndip_rows < 32) { g_dip_rows[g_ndip_rows] = i; g_dip_rowy[g_ndip_rows] = y; g_ndip_rows++; }
			y += lh;
		}
	}
	/* the confirm popup */
	if (st->dip_confirm) {
		int cx = S(74), cy = S(155);
		put(&g_dipconf, 0, dst, pitch, cx, cy);
		text_at(&g_f24, g_have_f24, dst, pitch, cx + S(150), cy + S(54), text_get("DWMLORD"), g_pen_white, 1);
		rich_block(&g_f14, g_have_f14, dst, pitch, cx + S(50), cy + S(80), S(200), S(110) - S(20), st->dip_confirm, g_pen_white, 0);
		text_at(&g_f14, g_have_f14, dst, pitch, cx + S(150), cy + S(174), text_get("DWAREYOUSURE1"), g_pen_white, 1);
		text_at(&g_f14, g_have_f14, dst, pitch, cx + S(150), cy + S(188), text_get("DWAREYOUSURE2"), g_pen_white, 1);
		text_button(dst, pitch, cx + S(25), cy + S(211), (g_have_fside ? font_width(&g_fside, text_get("CANCEL")) : 40) + S(32), text_get("CANCEL"), 0);
		text_button(dst, pitch, cx + S(240), cy + S(211), (g_have_fside ? font_width(&g_fside, "OK") : 20) + S(32), "OK", 0);
	}
}

int ui_dip_hit(int x, int y, const UiState *st)
{
	int i;
	if (st->dip_confirm) {
		int cx = S(74), cy = S(155);
		if (y >= cy + S(211) && y < cy + S(211 + 38)) {
			if (x >= cx + S(25) && x < cx + S(25 + 90)) return UD_CANCEL;
			if (x >= cx + S(240) && x < cx + S(300)) return UD_OK;
		}
		return UD_NONE;                                          /* modal */
	}
	for (i = 0; i < 2; i++) if (x >= IW_X(26) && x < IW_X(44 + 120) && y >= IW_Y(20 + 18 * i) && y < IW_Y(20 + 18 * i + 16)) return i ? UD_RADIO_REL : UD_RADIO_ALL;
	if (y >= IW_Y(315) && y < IW_Y(315 + 40) && x < S(224)) {
		if (st->dip_target < 0) return UD_BACK;
		return x < IW_X(112) ? UD_BACK : UD_GOTO;
	}
	if (st->dip_view == 1 && y >= IW_Y(113) && y < IW_Y(133) && x < S(224)) return x < IW_X(90) ? UD_SORTREL : UD_SORTNAT;
	for (i = 0; i < g_ndip_rows; i++)
		if (x >= S(224 + 32) && x < S(224 + 32 + 148) && y >= g_dip_rowy[i] && y < g_dip_rowy[i] + g_dip_rowh) return UD_ACT + g_dip_rows[i];
	if (st->dip_target >= 0 && x >= S(224) && x < S(448) && y >= S(103) && y < S(103 + 344)) return UD_PANEL;   /* inside the panel, nothing */
	return UD_NONE;
}

/* ------------------------------------------- envoys: centre of trade (iw_cot), settler placement
 * (iw_colonist), missionary (iw_missionary) - docs/SPEC_ENVOYS.md, window-relative */
static Sprite g_iwcot, g_iwcol, g_iwmis, g_cotlvl[6], g_merch, g_native, g_mstrip[2];
static int g_env_send_on[2];                     /* which send buttons were drawn enabled (0 = left / trader, 1 = right) */

static void env_load(void)
{
	char p[64];
	int i;
	iw_fonts();
	if (g_iwcot.w) return;
	load(&g_iwcot, g_gfx, "gfx/interface/iw_cot.bmp", g_inv, 1);
	load(&g_iwcol, g_gfx, "gfx/interface/iw_colonist.bmp", g_inv, 1);
	load(&g_iwmis, g_gfx, "gfx/interface/iw_missionary.bmp", g_inv, 1);
	for (i = 0; i < 6; i++) { snprintf(p, sizeof p, "gfx/interface/cotlevel%d.bmp", i + 1); load(&g_cotlvl[i], g_gfx, p, g_inv, 1); }
	load(&g_merch, g_gfx, "gfx/interface/merchant.bmp", g_inv, 1);
	load(&g_native, g_gfx, "gfx/interface/piw/buildings/native.bmp", g_inv, 6);
	load(&g_mstrip[0], g_gfx, "gfx/interface/missionaries_strip_left.bmp", g_inv, 4);
	load(&g_mstrip[1], g_gfx, "gfx/interface/missionaries_strip_right.bmp", g_inv, 4);
	if (!g_religions.w) load(&g_religions, g_gfx, "gfx/interface/religions.bmp", g_inv, 11);
	ev_load();
}

static void env_buttons(unsigned char *dst, int pitch, const char *left, int left_on, const char *right, int right_on, int y)
{
	g_env_send_on[0] = left_on; g_env_send_on[1] = right_on;
	if (left) text_btn_c(dst, pitch, IW_X(66), IW_Y(y), left);
	if (right) text_btn_c(dst, pitch, IW_X(158), IW_Y(y), right);
	if (left && !left_on) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(66), IW_Y(y) - g_f14.cell, "-", nearest(g_mappal, 0x80, 0x80, 0x80), 1);
	if (right && !right_on) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(158), IW_Y(y) - g_f14.cell, "-", nearest(g_mappal, 0x80, 0x80, 0x80), 1);
}

static void cot_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	static const int rowy[5] = { 270, 228, 186, 145, 102 }, bary[6] = { 266, 225, 179, 137, 95, 47 };
	const Cot *t = &w->cot[st->env_cot];
	const Province *p = &w->prov[t->prov];
	int k, i;
	env_load();
	put(&g_iwcot, 0, dst, pitch, 0, S(103));
	for (k = 0; k < 6; k++) put(&g_cotlvl[k], 0, dst, pitch, IW_X(14), IW_Y(bary[k]));
	text_at(&g_fbar, g_have_fbar, dst, pitch, IW_X(80), IW_Y(15), p->name ? p->name : "", g_pen_white, 1);
	if (p->owner >= 0) text_at(&g_fside, g_have_fside, dst, pitch, IW_X(80), IW_Y(31), text_get(w->country[p->owner].tag), g_pen_gold, 1);
	{	/* the total merchants (the original shows the CoT's trade value here: not modelled per CoT) */
		char v[16];
		int tot = 0;
		for (i = 0; i < t->n; i++) tot += t->level[i];
		snprintf(v, sizeof v, "%d/20", tot);
		text_right(&g_fbar, g_have_fbar, dst, pitch, IW_X(176), IW_Y(15), v, g_pen_white);
	}
	for (k = 1; k <= 6; k++) {                        /* the shields of the countries with k merchants */
		int n = 0, x;
		for (i = 0; i < t->n; i++) if (t->level[i] == k) n++;
		for (i = 0, x = 0; i < t->n; i++) {
			const Sprite *s;
			int refused, o = t->tag[i];
			if (t->level[i] != k || !(s = small_shield(w, o))) continue;
			refused = p->owner >= 0 && p->owner != o && (w->dipflag[p->owner][o] & DF_NOTRADE);
			if (k == 6) put(s, refused ? 3 : 0, dst, pitch, IW_X(160), IW_Y(53));
			else put(s, refused ? 3 : 0, dst, pitch, IW_X(82) + x * (n > 5 ? S(92) / n : S(20)), IW_Y(rowy[k - 1]));
			x++;
		}
	}
	env_buttons(dst, pitch, text_get("COTIWBACK"), 1, text_get("COTIWSEND"), st->env_can[0] == 0, 319);
}

static void colonist_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const Province *p = &w->prov[st->env_prov];
	char v[64];
	int grey = nearest(g_mappal, 0x80, 0x80, 0x80), orange = nearest(g_mappal, 0xFF, 0xC0, 0), on = st->env_can[1] == 0;
	env_load();
	put(&g_iwcol, 0, dst, pitch, 0, S(103));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(11), text_get("CWSENDC"), orange, 1);
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(25), p->name ? p->name : "", g_pen_white, 1);
	snprintf(v, sizeof v, text_get("CWMAXTAX"), (int)(p->income_m / 1000));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(76), IW_Y(55), v, orange, 0);
	if (p->tp) snprintf(v, sizeof v, text_get("CWTP"), (int)p->tp);
	else if (p->owner >= 0 && p->population > 0) snprintf(v, sizeof v, text_get("CWCITY"), (int)p->population);
	else snprintf(v, sizeof v, "%s", text_get(p->natives > 0 ? "CWNATIVEVILL" : "CWNOSET"));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(76), IW_Y(67), v, orange, 0);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(136), IW_Y(92), text_get("CWTRADER"), orange, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(178), IW_Y(92), text_get("CWCOLONIST"), orange, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(28), IW_Y(110), text_get("CWTIME"), orange, 0);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(28), IW_Y(125), text_get("CWCHANCE"), orange, 0);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(28), IW_Y(140), text_get("CWCOST"), orange, 0);
	{	/* the trader column */
		int ton = st->tr_can == 0;
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(136), IW_Y(110), "0", ton ? orange : grey, 1);
		snprintf(v, sizeof v, "%d%%", st->tr_chance);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(136), IW_Y(125), v, !ton ? grey : st->tr_chance <= 20 ? nearest(g_mappal, 0xE0, 0, 0) :
		        st->tr_chance <= 60 ? orange : nearest(g_mappal, 0, 0xE0, 0), 1);
		snprintf(v, sizeof v, "%ld $", st->tr_cost / 1000);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(136), IW_Y(140), v, ton ? orange : grey, 1);
	}
	snprintf(v, sizeof v, "%d", st->env_months);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(178), IW_Y(110), v, on ? orange : grey, 1);
	snprintf(v, sizeof v, "%d%%", st->env_chance);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(178), IW_Y(125), v, !on ? grey : st->env_chance <= 20 ? nearest(g_mappal, 0xE0, 0, 0) :
	        st->env_chance <= 60 ? orange : nearest(g_mappal, 0, 0xE0, 0), 1);
	snprintf(v, sizeof v, "%ld $", st->env_cost / 1000);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(178), IW_Y(140), v, on ? orange : grey, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(130), IW_Y(174), text_get("CWNATIVE"), orange, 1);
	put(&g_native, p->natives > 0 ? 1 : 0, dst, pitch, IW_X(24), IW_Y(161));
	put(&g_religions, religion_frame(world_name(w, p->religion) ? world_name(w, p->religion) : ""), dst, pitch, IW_X(74), IW_Y(163));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(198), text_get("CWNATIVESTR"), orange, 0);
	snprintf(v, sizeof v, "%ld", p->natives * 100); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(192), IW_Y(198), v, orange);   /* x100 (0x4fc2a7) */
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(212), text_get("CWNATIVETOL"), orange, 0);
	{
		static const char *fk[10] = { "CWNNONE", "CWNVLOW", "CWNVLOW", "CWNLOW", "CWNLOW", "CWNMEDIUM", "CWNMEDIUM", "CWNHIGH", "CWNVHIGH", "CWNEHIGH" };
		text_right(&g_f14, g_have_f14, dst, pitch, IW_X(192), IW_Y(212), text_get(fk[p->tolerance > 9 ? 9 : p->tolerance]), orange);
	}
	if (world_name(w, p->goods)) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(136), IW_Y(238), world_name(w, p->goods), orange, 1);
	env_buttons(dst, pitch, text_get("COWSENDTRA"), st->tr_can == 0, text_get("COWSENDOL"), on, 298);
	text_btn_c(dst, pitch, IW_X(112), IW_Y(319), text_get("COWBACK"));
}

static void missionary_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const Province *p = &w->prov[st->env_prov];
	char v[64];
	int on = st->env_can[2] == 0;
	env_load();
	put(&g_iwmis, 0, dst, pitch, 0, S(103));
	put(&g_mstrip[0], 0, dst, pitch, IW_X(10), IW_Y(16));
	put(&g_mstrip[1], 0, dst, pitch, IW_X(154), IW_Y(16));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(40), text_get("MSW_SEND"), g_pen_gold, 1);
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(67), p->name ? p->name : "", g_pen_white, 1);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(112), text_get("MSW_REL"), g_pen_gold, 1);
	put(&g_religions, religion_frame(world_name(w, p->religion) ? world_name(w, p->religion) : ""), dst, pitch, IW_X(112) - g_religions.w / 2, IW_Y(128));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(160), world_name(w, p->religion) ? world_name(w, p->religion) : "", g_pen_white, 1);
#define MROW(y, key, val) do { text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(y), text_get(key), g_pen_gold, 0); \
                               text_right(&g_f14, g_have_f14, dst, pitch, IW_X(200), IW_Y(y), val, g_pen_white); } while (0)
	snprintf(v, sizeof v, "%ld", p->population); MROW(195, "MSW_POP", v);
	MROW(213, "MSW_CUL", world_name(w, p->culture) ? world_name(w, p->culture) : "");
	snprintf(v, sizeof v, "%d%%", st->env_chance); MROW(232, "MSW_SUC", v);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(250), text_get("MSW_TIME"), g_pen_gold, 0);
	snprintf(v, sizeof v, text_get("MSW_TIME3"), st->env_months); MROW(264, "MSW_TIME2", v);
	snprintf(v, sizeof v, "%ld$", st->env_cost / 1000); MROW(284, "MSW_COST", v);
#undef MROW
	env_buttons(dst, pitch, text_get("MSWBAC"), 1, on ? text_get("MSWSEN") : NULL, on, 319);
}

/* the envoy windows' buttons: 1 = back / cancel, 2 = send (right), 3 = trader (left, colonist window) */
int ui_env_hit(int x, int y, int infowin)
{
	int yb = infowin == UI_IW_COLONIST ? 298 : 319;
	if (x >= S(224)) return 0;
	if (infowin == UI_IW_COLONIST && y >= IW_Y(315) && y < IW_Y(315 + 40)) return 1;
	if (y < IW_Y(yb - 4) || y >= IW_Y(yb + 36)) return 0;
	if (x < IW_X(112)) return infowin == UI_IW_COLONIST ? (g_env_send_on[0] ? 3 : 0) : 1;
	return g_env_send_on[1] ? 2 : 0;
}

/* ------------------------------------------- the Ledger (EYR), docs/SPEC_LEDGER.md: full screen,
 * screen coordinates of the 800x600 layout scaled like the rest */
static Sprite g_eyr_top, g_eyr_bottom, g_eyr_right, g_eyr_page[4], g_eyr_bsmall, g_eyr_blarge, g_eyr_frame,
              g_eyr_back, g_eyr_fwd, g_eyr_exit, g_eyr_ch[7], g_eyr_vline, g_eyr_hline;
static Font g_f18;
static int g_have_f18 = -1;
static const int g_eyr_chx[7] = { 50, 146, 230, 314, 398, 482, 562 };

static void eyr_load(void)
{
	static const char *ch[7] = { "monarchy", "economy", "diplomacy", "technology", "military", "establishments", "history" };
	char p[64];
	int i;
	iw_fonts();
	if (g_eyr_top.w) return;
	load(&g_eyr_top, g_gfx, "gfx/interface/eyr/eyr_top.bmp", g_inv, 1);
	load(&g_eyr_bottom, g_gfx, "gfx/interface/eyr/eyr_bottom.bmp", g_inv, 1);
	load(&g_eyr_right, g_gfx, "gfx/interface/eyr/eyr_right.bmp", g_inv, 1);
	for (i = 0; i < 4; i++) { snprintf(p, sizeof p, "gfx/interface/eyr/eyr_page%d.bmp", i + 1); load(&g_eyr_page[i], g_gfx, p, g_inv, 1); }
	load(&g_eyr_bsmall, g_gfx, "gfx/interface/eyr/eyr_banner_small.bmp", g_inv, 1);
	load(&g_eyr_blarge, g_gfx, "gfx/interface/eyr/eyr_banner_large.bmp", g_inv, 1);
	load(&g_eyr_frame, g_gfx, "gfx/interface/eyr/eyr_sidenumber_frame.bmp", g_inv, 1);
	load(&g_eyr_back, g_gfx, "gfx/interface/eyr/eyr_btn_back.bmp", g_inv, 4);
	load(&g_eyr_fwd, g_gfx, "gfx/interface/eyr/eyr_btn_forward.bmp", g_inv, 4);
	load(&g_eyr_exit, g_gfx, "gfx/interface/eyr/eyr_btn_exit.bmp", g_inv, 4);
	for (i = 0; i < 7; i++) { snprintf(p, sizeof p, "gfx/interface/eyr/eyr_btn_%s.bmp", ch[i]); load(&g_eyr_ch[i], g_gfx, p, g_inv, 4); }
	load(&g_eyr_vline, g_gfx, "gfx/interface/eyr/eyr_graph_vline.bmp", g_inv, 1);
	load(&g_eyr_hline, g_gfx, "gfx/interface/eyr/eyr_graph_hline.bmp", g_inv, 1);
	if (g_scale >= 1000) g_have_f18 = font_load(&g_f18, g_gfx, "garamond18bold") == 0 || font_load(&g_f18, g_gfx, "garamond20bold") == 0;
	else { char q[96]; snprintf(q, sizeof q, "%sfx6x10.euf", g_fontdir); g_have_f18 = font_load_euf(&g_f18, q) == 0; }
}

static void hline(unsigned char *dst, int pitch, int x0, int x1, int y, int pen) { int x; if (y < 0 || y >= g_sh) return; for (x = x0 < 0 ? 0 : x0; x < x1 && x < g_sw; x++) dst[(long)y * pitch + x] = (unsigned char)pen; }
static void vline(unsigned char *dst, int pitch, int x, int y0, int y1, int pen) { int y; if (x < 0 || x >= g_sw) return; for (y = y0 < 0 ? 0 : y0; y < y1 && y < g_sh; y++) dst[(long)y * pitch + x] = (unsigned char)pen; }

static void line(unsigned char *dst, int pitch, int x0, int y0, int x1, int y1, int pen)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx - dy;
	for (;;) {
		if (x0 >= 0 && x0 < g_sw && y0 >= 0 && y0 < g_sh) { dst[(long)y0 * pitch + x0] = (unsigned char)pen; if (y0 + 1 < g_sh) dst[(long)(y0 + 1) * pitch + x0] = (unsigned char)pen; }
		if (x0 == x1 && y0 == y1) break;
		{ int e2 = 2 * e; if (e2 > -dy) { e -= dy; x0 += sx; } if (e2 < dx) { e += dx; y0 += sy; } }
	}
}

void ui_ledger_draw(unsigned char *dst, int pitch, const World *w, const UiLedger *L)
{
	int i, k, ink = g_pen_black, brown = nearest(g_mappal, 0x60, 0x40, 0x20);
	char v[32];
	eyr_load();
	memset(dst, 0, (size_t)pitch * g_sh);
	put(&g_eyr_page[L->page % 4], 0, dst, pitch, 0, S(33));
	put(&g_eyr_top, 0, dst, pitch, 0, 0);
	put(&g_eyr_right, 0, dst, pitch, S(696), S(33));
	put(&g_eyr_bottom, 0, dst, pitch, 0, S(522));
	for (i = 0; i < 7; i++) put(&g_eyr_ch[i], i == L->chapter ? 2 : 0, dst, pitch, S(g_eyr_chx[i]), S(522));
	put(&g_eyr_exit, 0, dst, pitch, S(720), S(196));
	{	/* the title banner: small under 132 px of title, else large */
		int tw = g_have_f18 ? font_width(&g_f18, L->title) : 0, big = tw >= S(132);
		put(big ? &g_eyr_blarge : &g_eyr_bsmall, 0, dst, pitch, S(big ? 178 : 180), S(46));
		text_at(&g_f18, g_have_f18, dst, pitch, S(348), S(big ? 55 : 58), L->title, ink, 1);
	}
	put(&g_eyr_frame, 0, dst, pitch, S(308), S(472));
	snprintf(v, sizeof v, "%d", L->page + 1);
	text_at(&g_f18, g_have_f18, dst, pitch, S(348), S(480), v, ink, 1);
	put(&g_eyr_back, 0, dst, pitch, S(272), S(481));
	put(&g_eyr_fwd, 0, dst, pitch, S(388), S(481));
	if (L->kind == LG_TABLE) {                     /* tablet centred at x = 346, top 110, header row 23 */
		int x[LG_MAXCOL + 1], tot = 0, lh = g_f14.cell, top = S(110), y;
		for (k = 0; k < L->ncol; k++) {
			int cw = S(L->w[k]), hw = (g_have_f14 ? font_width(&g_f14, L->hdr[k]) : 40) + S(12);
			if (cw < hw) cw = hw;
			x[k] = tot; tot += cw;
		}
		x[L->ncol] = tot;
		for (k = 0; k <= L->ncol; k++) x[k] += S(346) - tot / 2;
		hline(dst, pitch, x[0], x[L->ncol], top, brown);
		hline(dst, pitch, x[0], x[L->ncol], top + S(23), brown);
		hline(dst, pitch, x[0], x[L->ncol], top + S(23) + S(L->body_h), brown);
		for (k = 0; k <= L->ncol; k++) vline(dst, pitch, x[k], top, top + S(23) + S(L->body_h), brown);
		for (k = 0; k < L->ncol; k++) text_at(&g_f14, g_have_f14, dst, pitch, (x[k] + x[k + 1]) / 2, top + S(4), L->hdr[k], ink, 1);
		for (i = 0, y = top + S(23) + S(3); i < L->nrow && y + lh <= top + S(23) + S(L->body_h); i++, y += lh)
			for (k = 0; k < L->ncol; k++) {
				if (L->right[k]) text_right(&g_f14, g_have_f14, dst, pitch, x[k + 1] - S(4), y, L->cell[i][k], ink);
				else text_at(&g_f14, g_have_f14, dst, pitch, x[k] + S(4), y, L->cell[i][k], ink, 0);
			}
	} else if (L->kind == LG_GRAPH) {              /* axes at (100,110) / (100,377), origin (105,377) */
		int ox = S(105), oy = S(377), ph = (g_eyr_vline.h ? g_eyr_vline.h : S(267)) - S(47), pw = (g_eyr_hline.w ? g_eyr_hline.w : S(520)) - S(10);
		static const int ly[5] = { 333, 286, 239, 192, 145 }, lx[5] = { 103, 220, 335, 450, 565 };
		put(&g_eyr_vline, 0, dst, pitch, S(100), S(110));
		put(&g_eyr_hline, 0, dst, pitch, S(100), S(377));
		{                                          /* 0x4e0482: from the top, trunc(k * max * 0.2), k = 5..1; a label equal to
			                                              one above it is not drawn; years start + trunc(0.2 i (now - start)) */
			char lab[5][16];
			int j;
			for (i = 4; i >= 0; i--) {
				snprintf(lab[i], sizeof lab[i], "%ld", L->maxv * (i + 1) / 5);
				for (j = 4; j > i && strcmp(lab[j], lab[i]); j--) ;
				if (j == i) text_right(&g_f14, g_have_f14, dst, pitch, S(98), S(ly[i]), lab[i], ink);
			}
		}
		for (i = 0; i < 5; i++) {
			snprintf(v, sizeof v, "%d", L->year0 + (L->year1 - L->year0) * i / 5);
			text_at(&g_f14, g_have_f14, dst, pitch, S(lx[i]), S(384), v, ink, 0);
		}
		short pen[LG_SER];
		for (k = 0; k < L->nser && k < LG_SER; k++) pen[k] = L->ser_pen[k];
		if (L->ser_cat) {                          /* the category colours: palette entries of gfx/palette/income.bmp / expense.bmp */
			static short cpen[2][13]; static int cdone[2];
			int kind = L->ser_cat - 1;
			if (!cdone[kind]) {
				unsigned long sz;
				unsigned char *d = (unsigned char *)pak_load(g_gfx, kind ? "gfx/palette/expense.bmp" : "gfx/palette/income.bmp", &sz);
				EuiImage im;
				int j;
				for (j = 0; j < 13; j++) cpen[kind][j] = (short)g_pen_white;
				if (d && !eui_decode(d, sz, &im)) {
					for (j = 0; j < 13 && j < im.ncolors; j++) cpen[kind][j] = (short)nearest(g_mappal, im.pal[3 * j], im.pal[3 * j + 1], im.pal[3 * j + 2]);
					eui_free(&im);
				}
				free(d);
				cdone[kind] = 1;
			}
			for (k = 0; k < L->nser; k++) pen[k] = cpen[kind][k % 13];   /* 16 expense kinds, 13 colours: NIEPEWNE beyond 12 */
		}
		for (k = 0; k < L->nser; k++) {
			int px = -1, py = 0;
			for (i = 0; i < L->npts; i++) {
				int xx = ox + (L->npts > 1 ? (int)((long)pw * i / (L->npts - 1)) : 0);
				int yy = oy - (int)((long)ph * L->ser[k][i] / (L->maxv ? L->maxv : 1));
				if (px >= 0) line(dst, pitch, px, py, xx, yy, pen[k]);
				else if (L->npts == 1) line(dst, pitch, xx, yy, xx + S(4), yy, pen[k]);
				px = xx; py = yy;
			}
			text_at(&g_f14, g_have_f14, dst, pitch, S(102 + 86 * (k % 6)), S(405 + 20 * (k / 6)),
			        L->ser_cat ? L->ser_label[k] : text_get(w->country[L->ser_country[k]].tag), pen[k], 0);
		}
	} else {                                       /* text list 520 wide from (86,110) */
		int y = S(110), lh = g_f14.cell;
		for (i = 0; i < L->nlines && y + lh < S(460); i++, y += lh) text_at(&g_f14, g_have_f14, dst, pitch, S(86), y, L->lines[i], ink, 0);
	}
}

int ui_ledger_hit(int x, int y)
{
	int i;
	if (x >= S(720) && x < S(720) + (g_eyr_exit.w ? g_eyr_exit.w : S(80)) && y >= S(196) && y < S(196) + (g_eyr_exit.h ? g_eyr_exit.h : S(178))) return LH_EXIT;
	if (y >= S(481) && y < S(481 + 28)) {
		if (x >= S(272) && x < S(272 + 36)) return LH_BACK;
		if (x >= S(388) && x < S(388 + 36)) return LH_FWD;
	}
	if (y >= S(522))
		for (i = 6; i >= 0; i--) if (x >= S(g_eyr_chx[i]) && x < S(g_eyr_chx[i]) + (g_eyr_ch[i].w ? g_eyr_ch[i].w : S(84))) return LH_CHAPTER + i;
	return LH_NONE;
}

/* ------------------------------------------- the build-manufactory sub-panel (SPEC_UI 3.3): bg_buildmanufact 148x218 at
 * window (38,68); buttons at window x = 46, y = 83 / 125 / 161 / 198 / 238 (4 frames each: NIEPEWNE which is which,
 * ours 0 normal, 1 hover, 3 disabled); two-line labels at panel x = 46 (y 27/38, 70/81, 101/112, 145/156, 180/191) in
 * gold; CANCEL as the windows' bottom text button (NIEPEWNE: its place) */
static Sprite g_mp_bg, g_mp_btn[5];
static const int g_mp_by[5] = { 83, 125, 161, 198, 238 }, g_mp_ly[5] = { 27, 70, 101, 145, 180 };
void ui_manupanel_draw(unsigned char *dst, int pitch, const unsigned char can[5], int hover)
{
	static const char *bn[5] = { "btn_arts", "btn_weapons", "btn_naval", "btn_refinery", "btn_manufgoods" };
	static const char *lk[5][2] = { { "MANARTS1", "MANARTS2" }, { "MANWEAP1", "MANWEAP2" }, { "MANNAV1", "MANNAV2" }, { "MANREF1", "MANREF2" }, { "MANGOOD1", "MANGOOD2" } };
	int i;
	char path[64];
	iw_fonts();
	if (!g_mp_bg.w) {
		load(&g_mp_bg, g_gfx, "gfx/interface/bg_buildmanufact.bmp", g_inv, 1);
		for (i = 0; i < 5; i++) { snprintf(path, sizeof path, "gfx/interface/%s.bmp", bn[i]); load(&g_mp_btn[i], g_gfx, path, g_inv, 4); }
	}
	put(&g_mp_bg, 0, dst, pitch, IW_X(38), IW_Y(68));
	for (i = 0; i < 5; i++) {
		put(&g_mp_btn[i], can[i] ? (hover == i ? 1 : 0) : 3, dst, pitch, IW_X(46), IW_Y(g_mp_by[i]));
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(38 + 46), IW_Y(68 + g_mp_ly[i]), text_get(lk[i][0]), g_pen_gold, 0);
		{ const char *t2 = text_get(lk[i][1]); if (t2 != lk[i][1]) text_at(&g_fside, g_have_fside, dst, pitch, IW_X(38 + 46), IW_Y(68 + g_mp_ly[i] + 11), t2, g_pen_gold, 0); }   /* MANREF2 is not in text.csv: nothing */
	}
	iw_close(dst, pitch, "CANCEL");
}
int ui_manupanel_hit(int x, int y)
{
	int i;
	for (i = 0; i < 5; i++) {
		int bw = g_mp_btn[i].w ? g_mp_btn[i].w : S(36), bh = g_mp_btn[i].h ? g_mp_btn[i].h : S(40);
		if (x >= IW_X(46) && x < IW_X(46) + bw && y >= IW_Y(g_mp_by[i]) && y < IW_Y(g_mp_by[i]) + bh) return i;
	}
	if (ui_iw_close_hit(x, y)) return 5;
	return -1;
}

/* ------------------------------------------- missions (iw_missionlist, SPEC_MISSIONS): current list at
 * (32,42), available list at (32,136) 128x157, headings centred at x = 96, BACK at the bottom */
static Sprite g_iwml;
static int g_mis_y0, g_mis_lh;
static void missions_draw(unsigned char *dst, int pitch, const UiState *st)
{
	int i, y;
	iw_fonts();
	if (!g_iwml.w) load(&g_iwml, g_gfx, "gfx/interface/iw_missionlist.bmp", g_inv, 1);
	put(&g_iwml, 0, dst, pitch, 0, S(103));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(96), IW_Y(15), text_get("MWCURRENT"), g_pen_gold, 1);
	g_mis_lh = g_f14.cell;
	for (i = 0, y = IW_Y(42); i < st->nmis_cur && y < IW_Y(100); i++, y += g_mis_lh) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(32), y, st->mis_cur[i], g_pen_white, 0);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(96), IW_Y(106), text_get("MWAVAILABLE"), g_pen_gold, 1);
	g_mis_y0 = IW_Y(136);
	for (i = 0, y = g_mis_y0; i < st->nmis_av && y < IW_Y(136 + 157); i++, y += g_mis_lh) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(32), y, st->mis_av[i], nearest(g_mappal, 0xFF, 0xC6, 0), 0);
	iw_close(dst, pitch, "BACK");
}

int ui_mis_hit(int x, int y, const UiState *st)
{
	if (ui_iw_close_hit(x, y)) return 1;
	if (x >= IW_X(28) && x < IW_X(200) && y >= g_mis_y0 && g_mis_lh > 0) { int r = (y - g_mis_y0) / g_mis_lh; if (r < st->nmis_av) return 100 + r; }
	return -1;
}

/* country window: 1 = the shield button (domestic policy), 2 = the religion button, 0 none */
int ui_country_hit(int x, int y)
{
	int bw = g_relbtn.w ? g_relbtn.w : S(48), bh = g_relbtn.h ? g_relbtn.h : S(56);
	if (g_cow_vas && x >= IW_X(52) && x < IW_X(172) && y >= IW_Y(113) && y < IW_Y(113) + S(34)) return 3;
	if (x >= IW_X(22) && x < IW_X(22) + bw && y >= IW_Y(54) && y < IW_Y(54) + bh) return 1;
	if (x >= IW_X(162) && x < IW_X(162) + bw && y >= IW_Y(54) && y < IW_Y(54) + bh) return 2;
	return 0;
}

/* ------------------------------------------------------- frontend (SPEC_UI §8) */
/* bg_frontend full screen; btn_frontend (136x32 x4) + its shadow; buttons centred at x=400 (spec);
 * the y positions and the setup layout are ours (NIEZNANE / NIEPEWNE in the spec). */
static Sprite g_fbg, g_fstart, g_fbtn, g_fbtn_sh, g_plate, g_pl, g_pr, g_fbig, g_mpanel, g_flare, g_ftut;
static Sprite g_prick, g_fpic;                 /* the rating pips, the country picture of the setup screen */
static char g_fpic_path[64];
static ShMask g_fbig_shm;
static ShMask g_flare_m;
static int lightmask_load(ShMask *m, const char *path, int frames, int idx);
static void lightmask_draw(const ShMask *m, unsigned char *dst, int pitch, int x0, int y0);
static void light_tables(const unsigned char *pal);

/* the menus' own palette (gfx/palette/amieu2_frontend.pal from eu2conv 3) and its inverse table;
 * without it (older data) the menus use the map palette */
static unsigned char g_fpal[768], g_finv[EUI_INV_SIZE];
static int g_have_fpal = -1;
static int g_fpen_white, g_fpen_gold, g_fpen_orange, g_fpen_black, g_fpen_grey;   /* the menus' text pens */
static struct { char tag[4]; Sprite s; int state; } g_fshield[24];

static const unsigned char *front_inv(void)
{
	if (g_have_fpal < 0) {
		unsigned long sz;
		unsigned char *p = pak_load(g_gfx, "gfx/palette/amieu2_frontend.pal", &sz);
		g_have_fpal = p && sz == 768;
		if (g_have_fpal) {
			memcpy(g_fpal, p, 768); eui_build_inverse(g_fpal, 256, g_finv);
			g_fpen_white = nearest(g_fpal, 255, 255, 255); g_fpen_gold = nearest(g_fpal, 255, 189, 0);
			g_fpen_orange = nearest(g_fpal, 255, 192, 0); g_fpen_black = nearest(g_fpal, 0, 0, 0);
			g_fpen_grey = nearest(g_fpal, 120, 120, 120);
		} else { g_fpen_white = g_pen_white; g_fpen_gold = g_pen_gold; g_fpen_orange = g_pen_orange; g_fpen_black = g_pen_black; g_fpen_grey = nearest(g_mappal, 120, 120, 120); }
		free(p);
	}
	return g_have_fpal ? g_finv : g_inv;
}

int ui_still_draw(unsigned char *dst, int pitch, Pak *pak, const char *entry)
{
	Sprite st;
	int y, black;
	const unsigned char *pal = ui_front_palette();
	black = nearest(pal ? pal : g_mappal, 0, 0, 0);
	for (y = 0; y < g_sh; y++) memset(dst + (long)y * pitch, black, (size_t)g_sw);
	if (sprite_from_bmp(&st, pak, entry, front_inv(), 1, g_scale)) return -1;
	put(&st, 0, dst, pitch, (g_sw - st.w) / 2, (g_sh - st.h) / 2);
	sprite_free(&st);
	return 0;
}

const unsigned char *ui_front_palette(void) { front_inv(); return g_have_fpal ? g_fpal : NULL; }

static const Sprite *front_shield(const char *tag)
{
	int i, freei = -1;
	for (i = 0; i < 24; i++) {
		if (g_fshield[i].state && !strcmp(g_fshield[i].tag, tag)) return g_fshield[i].state > 0 ? &g_fshield[i].s : NULL;
		if (!g_fshield[i].state && freei < 0) freei = i;
	}
	if (freei < 0) { for (i = 0; i < 24; i++) if (g_fshield[i].state > 0) sprite_free(&g_fshield[i].s); memset(g_fshield, 0, sizeof g_fshield); freei = 0; }
	{
		char sp[48], t[4];
		int k;
		for (k = 0; k < 3; k++) t[k] = (char)(tag[k] >= 'A' && tag[k] <= 'Z' ? tag[k] + 32 : tag[k]);
		t[3] = 0;
		snprintf(sp, sizeof sp, "gfx/map/shields/shield_%s.bmp", t);
		memcpy(g_fshield[freei].tag, tag, 4);
		g_fshield[freei].state = sprite_from_bmp(&g_fshield[freei].s, g_gfx, sp, front_inv(), 4, g_scale) == 0 ? 1 : -1;
		return g_fshield[freei].state > 0 ? &g_fshield[freei].s : NULL;
	}
}

/* frees the menus' sprites (call when the game starts: the memory is better used by the map) */
void ui_front_free(void)
{
	Sprite *all[] = { &g_fbg, &g_fstart, &g_fbtn, &g_fbtn_sh, &g_plate, &g_pl, &g_pr, &g_fbig, &g_mpanel, &g_flare, &g_ftut, &g_prick, &g_fpic };
	g_fpic_path[0] = 0;
	unsigned i;
	for (i = 0; i < sizeof all / sizeof all[0]; i++) { sprite_free(all[i]); memset(all[i], 0, sizeof *all[i]); }
	for (i = 0; i < 24; i++) if (g_fshield[i].state > 0) sprite_free(&g_fshield[i].s);
	memset(g_fshield, 0, sizeof g_fshield);
}

static void front_load(void)
{
	const unsigned char *inv = front_inv();
	if (g_fbg.w) return;
	load(&g_fbg, g_gfx, "gfx/interface/frontend/bg_frontend.bmp", inv, 1);
	load(&g_fstart, g_gfx, "gfx/interface/frontend/bg_start.bmp", inv, 1);
	load(&g_fbtn, g_gfx, "gfx/interface/frontend/btn_frontend.bmp", inv, 4);
	if (!g_fbtn_shm.lv) shmask_load(&g_fbtn_shm, "gfx/interface/frontend/btn_frontend_shadow.bmp");
	load(&g_plate, g_gfx, "gfx/interface/frontend/plate.bmp", inv, 1);
	load(&g_pl, g_gfx, "gfx/interface/btn_eventpage_left.bmp", inv, 4);
	load(&g_pr, g_gfx, "gfx/interface/btn_eventpage_right.bmp", inv, 4);
}




static void front_load2(void)
{
	const unsigned char *inv = front_inv();
	if (g_fbig.w) return;
	load(&g_fbig, g_gfx, "gfx/interface/frontend/btn_frontend_big.bmp", inv, 4);
	load(&g_mpanel, g_gfx, "gfx/interface/frontend/bg_multiplayer.bmp", inv, 1);
	lightmask_load(&g_flare_m, "gfx/interface/frontend/shield_flare.bmp", 100, 0);
	shmask_load(&g_fbig_shm, "gfx/interface/frontend/btn_frontend_big_shadow.bmp");
}

/* a frontend button: x,y = top-left in 800x600 coordinates; big = btn_frontend_big */
static void fe_button(unsigned char *dst, int pitch, int x, int y, int big, const char *label, int frame)
{
	const Sprite *b = big ? &g_fbig : &g_fbtn;
	shade_tables(g_have_fpal > 0 ? g_fpal : g_mappal);
	shmask_draw(big ? &g_fbig_shm : &g_fbtn_shm, dst, pitch, S(x), S(y));
	put(b, frame, dst, pitch, S(x), S(y));
	text_at(&g_fside, g_have_fside, dst, pitch, S(x) + b->w / 2, S(y) + (b->h - g_fside.cell) / 2, label,
	        frame == 3 ? g_fpen_grey : g_fpen_white, 1);
}

static int fe_in(int px, int py, int x, int y, const Sprite *b) { return px >= S(x) && px < S(x) + b->w && py >= S(y) && py < S(y) + b->h; }

static const char *fmain_key[5] = { "FEMAINBTN_TUTORIAL", "FEMAINBTN_SINGLE", "FEMAINBTN_MULTIPLAYER", "FEMAINBTN_CREDITS", "FEMAINBTN_EXIT" };
static const int fmain_id[5] = { UF_TUTORIAL, UF_SINGLE, UF_MULTI, UF_CREDITS, UF_EXIT };
static const int fmain_on[5] = { 1, 1, 0, 1, 1 };                    /* multiplayer: not ported */
static const char *fset_key[5] = { "FEBTN_INFO", "FEBTN_VICTORY", "FEBTN_OPTIONS", "FEBTN_BACK", "FEBTN_START" };
static const int fset_id[5] = { UF_INFO, UF_VICTORY, UF_OPTIONS, UF_BACK, UF_START };
static const int fset_y[5] = { 367, 412, 458, 504, 552 };
#define FE_SH_X 246
#define FE_SH_DX 64
#define FE_SH_Y 27
#define FE_LIST_X 36
#define FE_LIST_Y 110
#define FE_LIST_DY 18
/* a row of the scenario list (36,110, 220 high) or of the saved games (36,420, 142 high) */
static int fe_row_y(const UiFront *f, int i)
{
	int ns = f->nscen, nv = f->nlist - f->nscen;
	if (i < ns) return FE_LIST_Y + (ns > 12 ? 216 / ns : 18) * i;
	return 420 + (nv > 7 ? 140 / nv : 18) * (i - ns);
}
static int fe_row_h(const UiFront *f, int i)
{
	int ns = f->nscen, nv = f->nlist - f->nscen;
	return i < ns ? (ns > 12 ? 216 / ns : 18) : (nv > 7 ? 140 / nv : 18);
}
#define FE_PLATE_X 260
#define FE_PLATE_Y 112

static const Sprite *front_shield(const char *tag);

/* ------------------------------------------- VICTORY / OPTIONS popups (SPEC_UI §8.3 / §8.4) */
/* VICTORY from the exe (0x58d2f0 / draw 0x58e120): header (262,128), radios x 246 y 159 + 40 i, names
 * x 262 y 161 + 40 i (white chosen / grey), descriptions x 378 322 wide, the edit box right after the
 * description text, BACK centred on 510 at y 305. OPTIONS (0x58aaa0 / draw 0x58b7ec): header (246,128),
 * OK centred on 510 at y 306; the rows' place is NIEZNANE (a row widget), so ours: label at 250, the
 * value at 470, one row per option, a click steps the value (right click back). The area behind the
 * popups is darkened (ours: what the exe paints under them is NIEZNANE). */
static Sprite g_radio, g_editbox;
static unsigned char g_shade_tab[4][256];       /* (defined with the shadow masks below) */
#define FP_X0 236
#define FP_X1 756
#define FP_Y0 118
#define FP_Y1 346

static int fopt_step(void) { int c = g_have_fsmall ? g_fsmall.cell : S(14); return c + S(2); }
static int fvic_ebx(int i)                    /* the edit box's x (800 coordinates) after the description */
{
	static const char *dk[3] = { "FEVIC_STANDARD_DESC", "FEVIC_SUPREMACY_DESC", "FEVIC_DEATHMATCH_DESC" };
	int w = g_have_fsmall ? font_width(&g_fsmall, text_get(dk[i])) * 1000 / (g_scale ? g_scale : 1000) : 300;
	if (w > 322) w = 322;
	w += 394 - 6;
	return w > 800 - 92 ? 800 - 92 : w;
}

static void front_popup_draw(unsigned char *dst, int pitch, const UiFront *f)
{
	static const char *vk[4] = { "FEVIC_STANDARD", "FEVIC_SUPREMACY", "FEVIC_DEATHMATCH", "FEVIC_MISSION" };
	static const char *dk[4] = { "FEVIC_STANDARD_DESC", "FEVIC_SUPREMACY_DESC", "FEVIC_DEATHMATCH_DESC", "FEVIC_MISSION_DESC" };
	const GameOpts *o = f->opts;
	int i, y;
	unsigned char *row;
	if (!g_radio.w) { load(&g_radio, g_gfx, "gfx/settings/btn_radio.bmp", front_inv(), 4); load(&g_editbox, g_gfx, "gfx/interface/frontend/editbox.bmp", front_inv(), 2); }
	shade_tables(g_have_fpal > 0 ? g_fpal : g_mappal);     /* darken (x 110/255) */
	for (y = S(FP_Y0); y < S(FP_Y1) && y < g_sh; y++)
		for (row = dst + (long)y * pitch + S(FP_X0), i = S(FP_X1) - S(FP_X0); i > 0; i--, row++) *row = g_shade_tab[1][*row];
	if (f->opt_popup == UF_VICTORY) {
		text_at(&g_fside, g_have_fside, dst, pitch, S(262), S(128) - S(14), text_get("FEVIC_HEADER"), g_fpen_white, 0);
		for (i = 0; i < 4; i++) {
			char ln[2][96];
			int nl, k;
			put(&g_radio, o->vmode == i ? 1 : f->hover == UF_VRADIO + i ? 2 : 0, dst, pitch, S(246), S(159 + 40 * i));
			text_at(&g_fside, g_have_fside, dst, pitch, S(262) + S(4), S(161 + 40 * i), text_get(vk[i]), o->vmode == i ? g_fpen_white : g_fpen_grey, 0);
			nl = g_have_fsmall ? wrap(&g_fsmall, text_get(dk[i]), S(322), ln, 2) : 0;
			for (k = 0; k < nl; k++) text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(378), S(161 + 40 * i) + k * (g_fsmall.cell), ln[k], g_fpen_white, 0);
			if (i < 3) {
				int ex = fvic_ebx(i), ey = 155 + 40 * i;
				put(&g_editbox, f->vfocus == i ? 1 : 0, dst, pitch, S(ex), S(ey));
				text_at(&g_fside, g_have_fside, dst, pitch, S(ex) + g_editbox.w / 2, S(ey) + (g_editbox.h - g_fside.cell) / 2, f->vtext[i] ? f->vtext[i] : "",
				        f->vfocus == i ? g_fpen_gold : o->vmode == i ? g_fpen_white : g_fpen_grey, 1);
			}
		}
		fe_button(dst, pitch, 510 - 68, 305, 0, text_get("FEBTN_BACK"), f->hover == UF_VBACK ? 1 : 0);
	} else {
		int st = fopt_step();
		text_at(&g_fside, g_have_fside, dst, pitch, S(246), S(128) - S(14), text_get("FEOPT_HEADER"), g_fpen_white, 0);
		for (i = 0, y = S(148); i < OPT_N; i++, y += st) {
			int hv = f->hover == UF_OPTROW + i;
			text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(250), y, text_get(opts_label(i)), hv ? g_fpen_orange : g_fpen_gold, 0);
			put(&g_radio, 1, dst, pitch, S(450), y + (g_fsmall.cell - g_radio.h) / 2);
			text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(470), y, text_get(opts_value(i, o->opt[i])), hv ? g_fpen_orange : g_fpen_white, 0);
		}
		fe_button(dst, pitch, 510 - 68, 306, 0, text_get("FEOPT_OK"), f->hover == UF_OPTOK ? 1 : 0);
	}
}

static int front_popup_hit(int x, int y, const UiFront *f)
{
	int i;
	if (f->opt_popup == UF_VICTORY) {
		if (fe_in(x, y, 510 - 68, 305, &g_fbtn)) return UF_VBACK;
		for (i = 0; i < 3; i++) { int ex = fvic_ebx(i); if (x >= S(ex) && x < S(ex + 88) && y >= S(155 + 40 * i) && y < S(178 + 40 * i)) return UF_VEDIT + i; }
		for (i = 0; i < 4; i++) if (x >= S(244) && x < S(376) && y >= S(155 + 40 * i) && y < S(185 + 40 * i)) return UF_VRADIO + i;
		return UF_NONE;
	}
	if (fe_in(x, y, 510 - 68, 306, &g_fbtn)) return UF_OPTOK;
	{	int st = fopt_step();
		for (i = 0; i < OPT_N; i++) if (x >= S(246) && x < S(750) && y >= S(148) + i * st && y < S(148) + (i + 1) * st) return UF_OPTROW + i; }
	return UF_NONE;
}

/* An RTG screen bigger than 800x600: the frontend keeps its 800x600 pictures and is centred on black
 * (NIEPEWNE: the original's placement at 1024x768 was not traced). FE_OX/FE_OY = the offset, 0 otherwise. */
#define FE_OX (g_scale >= 1000 && g_sw > 800 ? (g_sw - 800) / 2 : 0)
#define FE_OY (g_scale >= 1000 && g_sh > 600 ? (g_sh - 600) / 2 : 0)
static void front_draw800(unsigned char *dst, int pitch, const UiFront *f);

void ui_front_draw(unsigned char *dst, int pitch, const UiFront *f)
{
	int ox = FE_OX, oy = FE_OY, sw = g_sw, sh = g_sh, y;
	if (!ox && !oy) { front_draw800(dst, pitch, f); return; }
	front_load();
	{
		int black = nearest(g_have_fpal > 0 ? g_fpal : g_mappal, 0, 0, 0);
		for (y = 0; y < sh; y++)
			if (y < oy || y >= oy + 600) memset(dst + (long)y * pitch, black, sw);
			else { memset(dst + (long)y * pitch, black, ox); memset(dst + (long)y * pitch + ox + 800, black, sw - ox - 800); }
	}
	g_sw = 800; g_sh = 600;                 /* everything below clips to the 800x600 window */
	front_draw800(dst + (long)oy * pitch + ox, pitch, f);
	g_sw = sw; g_sh = sh;
}

static void front_draw800(unsigned char *dst, int pitch, const UiFront *f)
{
	int i;
	front_load();
	front_load2();
	if (f->screen == 2) {                                     /* the tutorial chapters (0x5875a0): bg_tutorial, 10 buttons from y 100 */
		if (!g_ftut.w) load(&g_ftut, g_gfx, "gfx/interface/frontend/bg_tutorial.bmp", front_inv(), 1);
		put(g_ftut.w ? &g_ftut : &g_fbg, 0, dst, pitch, 0, 0);
		for (i = 0; i < 10; i++) {
			char k[24];
			snprintf(k, sizeof k, "FETUTORIAL_%d", i + 1);
			fe_button(dst, pitch, 400 - 88, 100 + 40 * i, 1, text_get(k), f->hover == UF_TUTCH + i ? 1 : 0);
		}
		return;
	}
	put(f->screen == 0 && g_fstart.w ? &g_fstart : &g_fbg, 0, dst, pitch, 0, 0);
	if (f->screen == 0) {
		for (i = 0; i < 5; i++) {
			int x = 400 - 88;                                   /* 400 - width/2 (FUN_005848f0) */
			fe_button(dst, pitch, x, 320 + 40 * i, 1, text_get(fmain_key[i]), !fmain_on[i] ? 3 : f->hover == fmain_id[i] ? 1 : 0);
		}
		{	/* the port's credit, bottom left on a dark plate (user 2026-11-11) */
			static const char *cr = "port by Grzegorz Korycki";
			const unsigned char *pal = g_have_fpal > 0 ? g_fpal : g_mappal;
			int tw = g_have_fsmall ? font_width(&g_fsmall, cr) : 150, th = g_have_fsmall ? g_fsmall.cell : 10;
			int bx = S(8), by = S(600) - th - S(14), bw = tw + S(12), bh = th + S(6), xx, yy;
			int black = nearest(pal, 0, 0, 0), gold = nearest(pal, 200, 160, 60);
			for (yy = 0; yy < bh; yy++)
				for (xx = 0; xx < bw; xx++)
					dst[(long)(by + yy) * pitch + bx + xx] = (unsigned char)(yy == 0 || yy == bh - 1 || xx == 0 || xx == bw - 1 ? gold : black);
			text_at(&g_fsmall, g_have_fsmall, dst, pitch, bx + S(6), by + S(3), cr, g_fpen_white, 0);
		}
		return;
	}
	/* shields of the featured powers (FUN_0058ca80) with the flare behind the chosen one */
	for (i = 0; i < f->nfeat && i < 9; i++) {
		const Sprite *sh = front_shield(f->feat[i]);
		int x = FE_SH_X + FE_SH_DX * i;
		if (i == f->country_feat && g_flare_m.lv) {
			light_tables(g_have_fpal > 0 ? g_fpal : g_mappal);
			lightmask_draw(&g_flare_m, dst, pitch, S(x) + (sh ? sh->w : S(56)) / 2 - g_flare_m.w / 2, S(FE_SH_Y) + (sh ? sh->h : S(60)) / 2 - g_flare_m.h / 2);
		}
		if (sh) put(sh, f->hover == UF_SHIELD + i ? 1 : 0, dst, pitch, S(x), S(FE_SH_Y));
	}
	/* headings, scenarios (36,110, 134x220) and saved games (36,420, 134x142) (0x586280) */
	text_at(&g_fbar, g_have_fbar, dst, pitch, S(112), S(66) - g_fbar.cell / 2, text_get("FESTR_SCENARIOS"), g_fpen_white, 1);
	text_at(&g_fbar, g_have_fbar, dst, pitch, S(112), S(390) - g_fbar.cell / 2, text_get("FESTR_SAVEDGAMES"), g_fpen_white, 1);
	for (i = 0; i < f->nlist && i < 24; i++) {
		char cl[96];
		int n2;
		snprintf(cl, sizeof cl, "%s", f->list[i]);                 /* kept inside the plate (to x 200): cut, ".." */
		for (n2 = (int)strlen(cl); n2 > 3 && g_have_fsmall && font_width(&g_fsmall, cl) > S(164); n2--) { cl[n2 - 3] = '.'; cl[n2 - 2] = '.'; cl[n2 - 1] = 0; }
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(FE_LIST_X), S(fe_row_y(f, i)), cl,
		        i == f->sel ? g_fpen_gold : f->hover == UF_LIST + i ? g_fpen_orange : g_fpen_white, 0);
	}
	/* the log box (246,390) 354 wide: green rows (bg_multiplayer's art is already in bg_frontend) */
	for (i = 0; i < f->ninfo; i++)
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(246), S(390) + i * (g_fsmall.cell), f->info[i], nearest(g_fpal, 0, 255, 0), 0);
	/* the middle: the country panel, or what to do next */
	if (!f->opt_popup) {
		if (f->sel < 0) text_at(&g_fbar, g_have_fbar, dst, pitch, S(494), S(226) - g_fbar.cell / 2, text_get("FESTR_NOSCENARIO"), g_fpen_white, 1);
		else if (!f->panel && f->sel < f->nscen) {
			text_at(&g_fbar, g_have_fbar, dst, pitch, S(494), S(220) - g_fbar.cell / 2, text_get("FESTR_NOCOUNTRY"), g_fpen_white, 1);
			text_at(&g_fbar, g_have_fbar, dst, pitch, S(494), S(240) - g_fbar.cell / 2, text_get("FESTR_CHANGECOUNTRY"), g_fpen_white, 1);
		} else if (f->panel) {
			static const char *lab[4] = { "FESTR_DIFFICULTY", "FESTR_ECONOMY", "FESTR_MILITARY", "FESTR_DIPLOMACY" };
			static const int ry[4] = { 262, 290, 304, 318 };
			char ln[12][96], k[16];
			int n, r;
			const Sprite *sh = f->country_tag ? front_shield(f->country_tag) : NULL;
			if (sh) put(sh, 0, dst, pitch, S(521), S(123));      /* the shield shows through the picture's green window */
			if (f->p_picture && strcmp(g_fpic_path, f->p_picture)) {
				sprite_free(&g_fpic); memset(&g_fpic, 0, sizeof g_fpic);
				snprintf(g_fpic_path, sizeof g_fpic_path, "%s", f->p_picture);
				load(&g_fpic, g_gfx, g_fpic_path, front_inv(), 1);
			}
			if (g_fpic.w) put(&g_fpic, 0, dst, pitch, S(524), S(118));
			fe_button(dst, pitch, 310, 304, 0, text_get(f->p_strat ? "FEBTN_BACK" : "FEBTN_STRATEGY"), f->hover == UF_STRAT ? 1 : 0);
			hline(dst, pitch, S(524), S(764), S(280), nearest(g_fpal, 218, 185, 110));
			hline(dst, pitch, S(524), S(764), S(280) + 1, nearest(g_fpal, 137, 109, 64));
			text_at(&g_fside, g_have_fside, dst, pitch, S(246), S(128), f->p_title ? f->p_title : "", g_fpen_white, 0);
			{	static char all[48][96];
				int per = (S(118) + g_fsmall.cell / 2) / (g_fsmall.cell ? g_fsmall.cell : 1), pages, p0;
				n = g_have_fsmall && f->p_text ? wrap(&g_fsmall, f->p_text, S(238), all, 48) : 0;
				if (per < 1) per = 1;
				pages = (n + per - 1) / per;
				p0 = pages ? (f->p_page % pages) * per : 0;
				for (r = 0; r < per && p0 + r < n; r++)
					text_at(&g_fsmall, 1, dst, pitch, S(246), S(150) + r * g_fsmall.cell, all[p0 + r], g_fpen_white, 0);
				if (pages > 1) { snprintf(k, sizeof k, "%d/%d", f->p_page % pages + 1, pages); text_at(&g_fsmall, 1, dst, pitch, S(484), S(268), k, g_fpen_gold, 0); }
				(void)ln;
			}
			if (!g_prick.w) load(&g_prick, g_gfx, "gfx/interface/prickar.bmp", front_inv(), 5);
			for (r = 0; r < 4; r++) {
				int v = f->p_rate[r] < 1 ? 1 : f->p_rate[r] > 5 ? 5 : f->p_rate[r], p;
				text_at(g_scale < 1000 ? &g_fsmall : &g_fside, g_have_fside, dst, pitch, S(524), S(ry[r]), text_get(lab[r]), g_fpen_white, 0);   /* 640: the small face fits before the pips */
				for (p = 0; p < v && g_prick.w; p++) put(&g_prick, v - 1, dst, pitch, S(622 + 10 * p), S(ry[r] + 2));
				snprintf(k, sizeof k, r ? "FE_STAT%d" : "FE_DIFFI%d", v);
				text_at(&g_fsmall, g_have_fsmall, dst, pitch, S(690), S(ry[r]), text_get(k), g_fpen_white, 0);
			}
		}
	}
	for (i = 0; i < 5; i++) {
		int on = fset_id[i] == UF_BACK || fset_id[i] == UF_VICTORY || fset_id[i] == UF_OPTIONS || (fset_id[i] == UF_START && f->can_start) || (fset_id[i] == UF_INFO && f->country_tag);
		fe_button(dst, pitch, 642, fset_y[i], 0, text_get(fset_key[i]), !on ? 3 : f->hover == fset_id[i] ? 1 : 0);
	}
	if (f->opt_popup && f->opts) { front_popup_draw(dst, pitch, f); return; }
	if (f->popup) {                                           /* plate.bmp with the other countries (FUN_0058c090) */
		int n0 = f->page * UF_PAGE;
		put(&g_plate, 0, dst, pitch, S(FE_PLATE_X), S(FE_PLATE_Y));
		for (i = 0; i < UF_PAGE && n0 + i < f->nothers; i++)
			text_at(&g_fside, g_have_fside, dst, pitch, S(FE_PLATE_X + 30), S(FE_PLATE_Y + 30 + 26 * i), text_get(f->others[n0 + i]),
			        f->hover == UF_OTHER + n0 + i ? g_fpen_gold : g_fpen_white, 0);
		put(&g_pl, f->page > 0 ? 0 : 3, dst, pitch, S(FE_PLATE_X + 90), S(FE_PLATE_Y + 410));
		put(&g_pr, n0 + UF_PAGE < f->nothers ? 0 : 3, dst, pitch, S(FE_PLATE_X + 160), S(FE_PLATE_Y + 410));
	}
}

static int front_hit800(int x, int y, const UiFront *f);
int ui_front_hit(int x, int y, const UiFront *f)
{
	int ox = FE_OX, oy = FE_OY, sw = g_sw, sh = g_sh, r;
	if (!ox && !oy) return front_hit800(x, y, f);
	g_sw = 800; g_sh = 600;
	r = front_hit800(x - ox, y - oy, f);
	g_sw = sw; g_sh = sh;
	return r;
}

static int front_hit800(int x, int y, const UiFront *f)
{
	int i;
	if (f->screen == 1 && f->opt_popup && f->opts) return front_popup_hit(x, y, f);
	if (f->screen == 0) {
		for (i = 0; i < 5; i++) if (fmain_on[i] && fe_in(x, y, 312, 320 + 40 * i, &g_fbig)) return fmain_id[i];
		return UF_NONE;
	}
	if (f->screen == 2) {
		for (i = 0; i < 10; i++) if (fe_in(x, y, 312, 100 + 40 * i, &g_fbig)) return UF_TUTCH + i;
		return UF_NONE;
	}
	if (f->popup) {
		int n0 = f->page * UF_PAGE;
		if (y >= S(FE_PLATE_Y + 410) && y < S(FE_PLATE_Y + 430)) {
			if (x >= S(FE_PLATE_X + 90) && x < S(FE_PLATE_X + 90) + g_pl.w) return UF_PREV;
			if (x >= S(FE_PLATE_X + 160) && x < S(FE_PLATE_X + 160) + g_pr.w) return UF_NEXT;
		}
		for (i = 0; i < UF_PAGE && n0 + i < f->nothers; i++)
			if (x >= S(FE_PLATE_X + 20) && x < S(FE_PLATE_X + 260) && y >= S(FE_PLATE_Y + 26 + 26 * i) && y < S(FE_PLATE_Y + 52 + 26 * i)) return UF_OTHER + n0 + i;
		if (x < S(FE_PLATE_X) || x >= S(FE_PLATE_X + 280) || y < S(FE_PLATE_Y) || y >= S(FE_PLATE_Y + 448)) return UF_POPCLOSE;
		return UF_NONE;
	}
	for (i = 0; i < 5; i++)
		if ((fset_id[i] == UF_BACK || fset_id[i] == UF_VICTORY || fset_id[i] == UF_OPTIONS || (fset_id[i] == UF_START && f->can_start) || (fset_id[i] == UF_INFO && f->country_tag)) && fe_in(x, y, 642, fset_y[i], &g_fbtn)) return fset_id[i];
	for (i = 0; i < f->nfeat && i < 9; i++)
		if (x >= S(FE_SH_X + FE_SH_DX * i) && x < S(FE_SH_X + FE_SH_DX * i + 56) && y >= S(FE_SH_Y) && y < S(FE_SH_Y + 62)) return UF_SHIELD + i;
	for (i = 0; i < f->nlist && i < 24; i++)
		if (x >= S(FE_LIST_X) && x < S(FE_LIST_X + 164) && y >= S(fe_row_y(f, i)) - 2 && y < S(fe_row_y(f, i) + fe_row_h(f, i)) - 2) return UF_LIST + i;
	if (f->panel && !f->opt_popup && fe_in(x, y, 310, 304, &g_fbtn)) return UF_STRAT;
	if (f->panel && !f->opt_popup && x >= S(246) && x < S(484) && y >= S(150) && y < S(268)) return UF_PTEXT;
	return UF_NONE;
}

/* ---------------------------------------------------- army window (iw_army, SPEC_UI §6) */
/* §6 lists the bitmaps; the row positions are NIEZNANE, so the layout is ours: name in the
 * nameframe, one row per arm (icon, numberframe), the morale bar, location and march. */
/* army / fleet window (SPEC_UI 4.10): unit panel 0x4f7860 + window layer 0x4f8ed0, window-relative */
static Sprite g_iwa, g_iwn, g_utype[2][3], g_nameframe, g_numframe, g_mbar, g_mstat, g_rank, g_stat[4], g_attr, g_weight, g_shipload, g_profbig, g_rankframe;
static int g_utype_tech = -1;
static int g_unload_shown;
static Sprite g_pil[2];

static void unit_panel_load(int tech)
{
	static const char *st[4] = { "maneuver", "fire", "shock", "siege" };
	static const char *nv[3] = { "warships", "galleys", "transports" };
	char p[80];
	int i;
	if (!g_iwa.w) {
		load(&g_iwa, g_gfx, "gfx/interface/iw_army.bmp", g_inv, 1);
		load(&g_iwn, g_gfx, "gfx/interface/iw_navy.bmp", g_inv, 1);
		load(&g_nameframe, g_gfx, "gfx/interface/reorganisation/nameframe.bmp", g_inv, 1);
		load(&g_numframe, g_gfx, "gfx/interface/reorganisation/numberframe.bmp", g_inv, 1);
		load(&g_mbar, g_gfx, "gfx/interface/reorganisation/morale_bar.bmp", g_inv, 1);
		load(&g_mstat, g_gfx, "gfx/interface/reorganisation/moralestatusbar.bmp", g_inv, 1);
		load(&g_rank, g_gfx, "gfx/interface/reorganisation/rank.bmp", g_inv, 7);
		load(&g_profbig, g_gfx, "gfx/interface/profbig.bmp", g_inv, 2);                 /* 0 explorer, 1 conquistador */
		load(&g_rankframe, g_gfx, "gfx/interface/reorganisation/rankframe.bmp", g_inv, 1);
		for (i = 0; i < 4; i++) { snprintf(p, sizeof p, "gfx/interface/reorganisation/%s.bmp", st[i]); load(&g_stat[i], g_gfx, p, g_inv, 1); }
		load(&g_attr, g_gfx, "gfx/interface/reorganisation/attrition.bmp", g_inv, 4);   /* 144x12 = 4 frames */
		load(&g_weight, g_gfx, "gfx/interface/crb/unitweight.bmp", g_inv, 1);
		load(&g_shipload, g_gfx, "gfx/interface/reorganisation/niw_shipload.bmp", g_inv, 1);
		for (i = 0; i < 3; i++) { snprintf(p, sizeof p, "gfx/interface/reorganisation/%s.bmp", nv[i]); load(&g_utype[1][i], g_gfx, p, g_inv, 1); }
	}
	if (tech != g_utype_tech) {                     /* infantry%d / cavalry%d / artillery%d by the tech class (0x45b8a0) */
		static const char *lv[3] = { "infantry", "cavalry", "artillery" };
		for (i = 0; i < 3; i++) {
			if (g_utype[0][i].w) sprite_free(&g_utype[0][i]);
			snprintf(p, sizeof p, "gfx/interface/reorganisation/%s%d.bmp", lv[i], tech);
			load(&g_utype[0][i], g_gfx, p, g_inv, 1);
		}
		g_utype_tech = tech;
	}
}

static void num_frame(unsigned char *dst, int pitch, int x, int y, long v)
{
	char s[24];
	put(&g_numframe, 0, dst, pitch, IW_X(x), IW_Y(y));
	snprintf(s, sizeof s, "%ld", v);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(x) + g_numframe.w / 2, IW_Y(y) + (g_numframe.h - g_f14.cell) / 2, s, g_pen_white, 1);
}

static void stat_icon(unsigned char *dst, int pitch, int k, int x, int vx, long v)
{
	char s[16];
	put(&g_stat[k], 0, dst, pitch, IW_X(x), IW_Y(86));
	snprintf(s, sizeof s, "%ld", v);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(vx), IW_Y(87), s, g_pen_white, 1);
}

/* the shared unit panel: morale (army only), name frame, rank, stats (no leaders yet: 0), attrition,
 * weight, the three counts and pictures, where it is going, BACK */
static void unit_draw(unsigned char *dst, int pitch, const World *w, const UiState *st, int naval)
{
	char s[64];
	const char *name;
	long n[3], loc, dest = -1, men;
	int i, tech, has_leader;
	iw_fonts();
	tech = st->player >= 0 ? 1 + w->country[st->player].land_tech / 20 : 1;     /* tech class 1..3: NIEPEWNE thresholds */
	if (tech > 3) tech = 3;
	unit_panel_load(tech);
	if (naval) {
		const NavalUnit *f = &w->naval[st->fleet];
		name = f->name; loc = f->location; n[0] = f->warships; n[1] = f->galleys; n[2] = f->transports;
		if (f->npath > f->pathpos) dest = f->path[f->npath - 1];
		men = 0;
	} else {
		const LandUnit *u = &w->land[st->army];
		name = u->name; loc = u->location; n[0] = u->infantry; n[1] = u->cavalry; n[2] = u->artillery;
		if (u->npath > u->pathpos) dest = u->path[u->npath - 1];
		men = u->infantry + u->cavalry + 100 * u->artillery;
	}
	put(naval ? &g_iwn : &g_iwa, 0, dst, pitch, 0, S(103));
	if (!naval) {                                     /* morale: label, bar, value at x = 152 */
		const LandUnit *u = &w->land[st->army];
		long full = st->army_maxmorale > 0 ? st->army_maxmorale : 1, mor = u->morale_m < 0 ? 0 : u->morale_m > full ? full : u->morale_m;
		int bx = IW_X(152) - g_mstat.w / 2;
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(152), IW_Y(16), text_get("ANWMORALE"), g_pen_white, 1);
		put(&g_mstat, 0, dst, pitch, bx, IW_Y(29));
		sprite_draw(&g_mbar, 0, dst, pitch, bx + (int)(g_mbar.w * mor / full), g_sh, bx, IW_Y(29) + (g_mstat.h - g_mbar.h) / 2);
		/* under the bar the original writes a word, not the number (0x48b3b0, called at 0x4f7acb):
		 * >= 4.5 Invincible, 3.5 Very Strong, 2.5 Strong, 1.5 Disciplined, 0.75 Weak, 0.25 Breaking, else Panic */
		{	long m = u->morale_m;
			const char *k = m >= 4500 ? "INVINCIBLE" : m >= 3500 ? "VERYSTRONG" : m >= 2500 ? "STRONG" :
			                m >= 1500 ? "DISCIPLINED" : m >= 750 ? "WEAK" : m >= 250 ? "BREAKING" : "PANIC";
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(152), IW_Y(39), text_get(k), g_pen_gold, 1);
		}
	}
	put(&g_nameframe, 0, dst, pitch, IW_X(44), IW_Y(58));
	/* the leader block (0x4f7c4b..0x4f7f00): the attached leader, else the unit's built-in one (0x48c040) */
	has_leader = naval ? w->naval[st->fleet].l_id != 0 : w->land[st->army].l_id != 0;
	text_at(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(92), IW_Y(58) + (g_nameframe.h - g_fsmall.cell) / 2,
	        st->army_leader ? st->army_leader : "", g_pen_gold, 1);
	{	/* the rank frame, and the leader's profession at (142,58) when he is an explorer or a conquistador
		 * (profbig.bmp: 0 explorer, 1 conquistador; hidden for generals and admirals, 0x4f7d12) */
		const LandUnit *lu = naval ? NULL : &w->land[st->army];
		const NavalUnit *nu = naval ? &w->naval[st->fleet] : NULL;
		int r = lu && lu->l_id ? lu->l_rank : LEAD_DEFAULT_RANK, prof = -1, rk;
		/* rank -> rank.bmp frame (0x4f7ca2): 0 -> 1 (0 for type 4), 1 -> 2, 2 -> 3, 3 -> 4, under 25 -> 5, else 6 */
		rk = r <= 0 ? 1 : r == 1 ? 2 : r == 2 ? 3 : r == 3 ? 4 : r < 25 ? 5 : 6;
		put(&g_rank, rk, dst, pitch, IW_X(18), IW_Y(58));
		if (lu && lu->l_id) prof = lu->l_type == 2 ? 1 : lu->l_type == 3 ? 0 : -1;
		if (nu && nu->l_id && nu->l_type != 1) prof = 0;
		if (prof >= 0) { put(&g_rankframe, 0, dst, pitch, IW_X(142), IW_Y(58)); put(&g_profbig, prof, dst, pitch, IW_X(142), IW_Y(58)); }
	}
	put(&g_attr, st->attrition > 0 ? 1 : 0, dst, pitch, IW_X(170), IW_Y(72));   /* frame by level: NIEPEWNE */
	snprintf(s, sizeof s, "%ld", st->attrition); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(190), IW_Y(72), s, g_pen_white, 1);
	if (!has_leader) {	/* the built-in leader's values */
		stat_icon(dst, pitch, 0, 18, 38, st->dl_move); stat_icon(dst, pitch, 1, 56, 84, st->dl_fire);
		stat_icon(dst, pitch, 2, 94, 122, st->dl_shock); stat_icon(dst, pitch, 3, 130, 150, naval ? 0 : st->dl_siege);
	} else {	/* the attached leader's maneuver / fire / shock / siege */
		const LandUnit *lu = naval ? NULL : &w->land[st->army];
		const NavalUnit *nu = naval ? &w->naval[st->fleet] : NULL;
		stat_icon(dst, pitch, 0, 18, 38, lu ? lu->l_move : nu && nu->l_id ? nu->l_move : 0); stat_icon(dst, pitch, 1, 56, 84, lu ? lu->l_fire : nu && nu->l_id ? nu->l_fire : 0);
		stat_icon(dst, pitch, 2, 94, 122, lu ? lu->l_shock : nu && nu->l_id ? nu->l_shock : 0); stat_icon(dst, pitch, 3, 130, 150, lu ? lu->l_siege : 0);
	}
	{	/* army: weight; fleet: NIWNTP "%d of %d" = carried / capacity */
		const Sprite *wt = naval ? &g_shipload : &g_weight;
		put(wt, 0, dst, pitch, IW_X(206) - wt->w, IW_Y(86));
		if (naval) snprintf(s, sizeof s, text_get("NIWNTP"), (int)((st->load_m + 999) / 1000), (int)(st->cap_m / 1000));
		else snprintf(s, sizeof s, "%ld", (men + 999) / 1000);
		text_right(&g_f14, g_have_f14, dst, pitch, IW_X(202) - wt->w, IW_Y(87), s, g_pen_white);
	}
	g_unload_shown = 0;
	if (naval && st->load_m > 0) {                     /* BUTTON_UNLOAD_ARMY (0x4f3572); its place is ours */
		text_btn_c(dst, pitch, IW_X(112), IW_Y(270), text_get(st->unload_mode ? "RCDM_UNLOAD" : "BUTTON_0213"));
		g_unload_shown = 1;
		if (st->unload_mode) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(300), text_get("DWCLICK2"), g_pen_dorange, 1);
	}
	{	/* the commands (0x4f9d76..: y 319 at x 38 / 112 / 186; DISBAND's row is ours) and the stack arrows (6,243) / (204,243) */
		g_ucmd[0] = st->can_split; g_ucmd[1] = st->can_merge; g_ucmd[2] = 1; g_ucmd[3] = g_ucmd[4] = st->nstack > 1;
		g_ucmd[5] = !naval && st->can_kill_natives;
		ev_load();                                     /* the text button pieces */
		if (g_ucmd[5]) text_btn_c(dst, pitch, IW_X(112), IW_Y(299) - S(20), text_get("BUTTON_0210"));   /* BUTTON_KILL_NATIVES centred (112,299) (0x4f2fd6); ours 20 higher, clear of the row at 319 */
		if (st->can_split) text_btn_c(dst, pitch, IW_X(46), IW_Y(319), text_get("RCDM_SPLITARMY"));   /* x 38 / 186 in the exe: ours a little inward, our buttons are wider */
		if (st->can_merge) text_btn_c(dst, pitch, IW_X(178), IW_Y(319), text_get("RCDM_MERGE"));
		text_btn_c(dst, pitch, IW_X(112), IW_Y(230), text_get("BUTTON_0204"));
		if (st->nstack > 1) {
			if (!g_pil[0].w) { load(&g_pil[0], g_gfx, "gfx/interface/reorganisation/btn_pil_left.bmp", g_inv, 4); load(&g_pil[1], g_gfx, "gfx/interface/reorganisation/btn_pil_right.bmp", g_inv, 4); }
			put(&g_pil[0], 0, dst, pitch, IW_X(6), IW_Y(243)); put(&g_pil[1], 0, dst, pitch, IW_X(204), IW_Y(243));
		}
	}
	num_frame(dst, pitch, 18, 102, n[0]); num_frame(dst, pitch, 84, 102, n[1]); num_frame(dst, pitch, 150, 102, n[2]);
	for (i = 0; i < 3; i++) put(&g_utype[naval][i], 0, dst, pitch, IW_X(20 + 66 * i), IW_Y(143));
	/* ours: where it is and where it goes (the original shows that with arrows on the map) */
	snprintf(s, sizeof s, "%s", loc > 0 && loc < w->nprov && w->prov[loc].name ? w->prov[loc].name : "");
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(196), s, g_pen_gold, 1);
	if (dest > 0 && dest < w->nprov) {
		snprintf(s, sizeof s, "-> %s (%ld)", w->prov[dest].name ? w->prov[dest].name : "?", st->army_days);
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(214), s, g_pen_white, 1);
	}
	iw_close(dst, pitch, "BACK");
}

static void army_draw(unsigned char *dst, int pitch, const World *w, const UiState *st) { unit_draw(dst, pitch, w, st, 0); }

int ui_unit_cmd_hit(int x, int y)
{
	if (y >= IW_Y(315) && y < IW_Y(315) + S(40)) {
		if (g_ucmd[0] && x >= IW_X(12) && x < IW_X(64)) return 1;
		if (g_ucmd[1] && x >= IW_X(160) && x < IW_X(212)) return 2;
	}
	if (g_ucmd[2] && y >= IW_Y(226) && y < IW_Y(226) + S(36) && x >= IW_X(62) && x < IW_X(162)) return 3;
	if (g_ucmd[5] && y >= IW_Y(279) && y < IW_Y(279) + S(30) && x >= IW_X(52) && x < IW_X(172)) return 6;
	if (g_ucmd[3] && y >= IW_Y(240) && y < IW_Y(240) + S(24)) {
		if (x >= IW_X(2) && x < IW_X(30)) return 4;
		if (x >= IW_X(190) && x < IW_X(222)) return 5;
	}
	return 0;
}

/* ------------------------------------------- battle window (iw_battle, SPEC_UI §4.11) */
/* layout from the exe (land 0x4ff240 + shared 0x4fdf60 / 0x4fe110 / 0x4fe7c0, siege 0x500850);
 * window-relative, the attacker on the left. Not drawn (NIEZNANE / not ours yet): the units'
 * sprite strips on the battlefield, the leader, the siege buttons, winter backgrounds. */
int ui_siege_hit(int x, int y)
{
	if (!g_sbtn || y < IW_Y(315) || y >= IW_Y(315) + S(40)) return 0;
	return x < IW_X(76) ? 1 : x < IW_X(148) ? 2 : 3;
}
static Sprite g_iwbat, g_gauge, g_battr, g_bfire, g_bshock, g_advcav, g_slev, g_srat, g_breach, g_cbg;
static int g_cbg_kind = -1;

static void bat_text(int x, int y, const char *s, int pen, unsigned char *dst, int pitch) { text_at(&g_f14, g_have_f14, dst, pitch, IW_X(x), IW_Y(y), s, pen, 1); }

static void battle_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	static const char *bgn[6] = { "plains1", "forest1", "mountain1", "desert1", "marsh1", "sea1" };
	const MilBattleView *v = st->battle;
	char s[32];
	long a, d;
	int i, k, siege = v->kind == MBV_SIEGE, grey = nearest(g_mappal, 0xA4, 0xA4, 0xA4), grey2 = nearest(g_mappal, 0x80, 0x80, 0x80);
	iw_fonts();
	unit_panel_load(st->player >= 0 ? (1 + w->country[st->player].land_tech / 20 > 3 ? 3 : 1 + w->country[st->player].land_tech / 20) : 1);
	if (!g_iwbat.w) {
		load(&g_iwbat, g_gfx, "gfx/interface/iw_battle.bmp", g_inv, 1);
		load(&g_gauge, g_gfx, "gfx/interface/crb/winner_gauge.bmp", g_inv, 9);
		load(&g_battr, g_gfx, "gfx/interface/reorganisation/attrition.bmp", g_inv, 4);
		load(&g_bfire, g_gfx, "gfx/interface/reorganisation/fire.bmp", g_inv, 1);
		load(&g_bshock, g_gfx, "gfx/interface/reorganisation/shock.bmp", g_inv, 1);
		load(&g_advcav, g_gfx, "gfx/interface/crb/advantage_cavalry.bmp", g_inv, 1);
	}
	put(&g_iwbat, 0, dst, pitch, 0, S(103));
	/* the strength ratio gauge (0x4fecc0): men x morale, attacker / defender, x100 */
	a = (v->n[0][0] + v->n[0][1] + 100 * v->n[0][2]) / 10 * (v->morale[0] / 100 + 1);
	d = (v->n[1][0] + v->n[1][1] + 100 * v->n[1][2]) / 10 * (v->morale[1] / 100 + 1);
	if (siege) d += 1000L * v->fort * (v->hold > 0 ? v->hold : 1);   /* the fortress counts for the defender (ours, NIEPEWNE) */
	{	long r = d > 0 ? a / (d / 100 > 0 ? d / 100 : 1) : 100000;   /* x100 */
		int f = r >= 400 ? 0 : r >= 300 ? 1 : r >= 200 ? 2 : r >= 150 ? 3 : r >= 66 ? 4 : r >= 50 ? 5 : r >= 33 ? 6 : r >= 25 ? 7 : 8;
		g_tip_gauge = f; g_tip_bv = *v; g_tip_bv_on = 1;
		put(&g_gauge, f, dst, pitch, IW_X(72), IW_Y(11)); }
	for (k = 0; k < 2; k++) {                          /* shields, attrition, morale (0x4fe7c0) */
		const Sprite *sh;
		long mx = v->maxmor > 0 ? v->maxmor : 1, mo = v->morale[k] < 0 ? 0 : v->morale[k] > mx ? mx : v->morale[k];
		if (v->country[k] >= 0) {
			char sp[64];
			snprintf(sp, sizeof sp, "gfx/map/shields/shield_%s.bmp", ftag(w, v->country[k]));
			if ((sh = cached(sp, 4)) != NULL) put(sh, 0, dst, pitch, IW_X(k ? 150 : 14), IW_Y(11));
		}
		put(&g_battr, v->attr[k] > 0 ? 1 : 0, dst, pitch, IW_X(k ? 120 : 68), IW_Y(56));     /* frame by cause: NIEPEWNE */
		snprintf(s, sizeof s, "%ld", v->attr[k]); bat_text(k ? 142 : 90, 56, s, g_pen_white, dst, pitch);
		snprintf(s, sizeof s, "%ld.%ld", v->morale[k] / 1000, v->morale[k] % 1000 / 100); bat_text(k ? 164 : 60, 175, s, g_pen_white, dst, pitch);
		if (g_mbar.w) sprite_draw(&g_mbar, 0, dst, pitch, IW_X(k ? 120 : 20) + (int)(S(83) * mo / mx), g_sh, IW_X(k ? 120 : 20), IW_Y(192));
	}
	if (siege) {                                       /* siege picture, rating strip, values, breach (0x500850) */
		if (!g_slev.w) {
			load(&g_slev, g_gfx, "gfx/interface/crb/siege_levels.bmp", g_inv, 12);
			load(&g_srat, g_gfx, "gfx/interface/crb/siege_ratings.bmp", g_inv, 1);
			load(&g_breach, g_gfx, "gfx/interface/crb/breach.bmp", g_inv, 1);
		}
		k = v->distress + v->breaches; if (k > 11) k = 11;      /* the frame from our siege state: NIEPEWNE */
		put(&g_slev, k, dst, pitch, IW_X(20), IW_Y(74));
		put(&g_srat, 0, dst, pitch, IW_X(14), IW_Y(127));
		snprintf(s, sizeof s, "%d", v->rating); bat_text(22, 133, s, g_pen_white, dst, pitch);
		snprintf(s, sizeof s, "%d", v->hold); bat_text(208, 133, s, g_pen_white, dst, pitch);
		if (v->breaches > 0) put(&g_breach, 0, dst, pitch, IW_X(150), IW_Y(149));
		if (v->country[0] == st->player && st->player >= 0) {   /* BUTTON_ASSAULT / DETACH_SIEGE / CREATE_COVER, y 319, x 38 / 112 / 186 */
			ev_load();
			text_btn_c(dst, pitch, IW_X(42), IW_Y(319), text_get("BUTTON_0205"));
			text_btn_c(dst, pitch, IW_X(112), IW_Y(319), text_get("BUTTON_0206"));
			text_btn_c(dst, pitch, IW_X(182), IW_Y(319), text_get("BUTTON_0207"));
			g_sbtn = 1;
		}
	} else {                                           /* battlefield, phase icons, shares, roll (0x4fe110) */
		long tot = v->power[0] + v->power[1];
		int t = v->terrain == 1 ? 1 : v->terrain == 2 ? 2 : v->terrain == 3 ? 3 : v->terrain == 4 ? 4 : v->terrain >= 5 ? 5 : 0;
		if (t != g_cbg_kind) {
			char p[80];
			if (g_cbg.w) sprite_free(&g_cbg);
			snprintf(p, sizeof p, "gfx/interface/crb/combatbg_%s.bmp", bgn[t]);
			load(&g_cbg, g_gfx, p, g_inv, 1);
			g_cbg_kind = t;
		}
		put(&g_cbg, 0, dst, pitch, IW_X(20), IW_Y(74));
		if (v->kind == MBV_NAVAL) {                       /* the two lead ships and the smoke (0x4ffb20, SPEC_BATTLE_MERC 3) */
			static const short TA[4][2] = { { 40, 79 }, { 52, 74 }, { 24, 74 }, { 30, 72 } }, TD[4][2] = { { 120, 101 }, { 104, 96 }, { 102, 103 }, { 102, 98 } };
			static const short SA[4][4][2] = { { { 60, 94 }, { 50, 97 } }, { { 64, 102 }, { 58, 104 }, { 54, 105 } },
			                                   { { 30, 99 }, { 42, 97 }, { 52, 95 }, { 62, 92 } }, { { 30, 99 }, { 42, 97 }, { 52, 95 }, { 62, 92 } } };
			static const short SD[4][4][2] = { { { 94, 96 }, { 80, 99 } }, { { 74, 98 }, { 70, 102 }, { 64, 105 } },
			                                   { { 88, 100 }, { 80, 103 }, { 70, 105 }, { 98, 97 } }, { { 88, 100 }, { 80, 103 }, { 70, 105 }, { 98, 97 } } };
			static Sprite smk[2];
			static int frame_count;
			static int smk_tried;
			int lv[2], q, n2, tick;
			const Sprite *shp[2];
			tick = ++frame_count;
			if (!smk_tried) { smk_tried = 1; load(&smk[0], g_gfx, "gfx/interface/t-navy a-smoke d-se.bmp", g_inv, 45); load(&smk[1], g_gfx, "gfx/interface/t-navy a-smoke d-nw.bmp", g_inv, 51); }
			for (k = 0; k < 2; k++) {
				int L = v->country[k] >= 0 ? 1 + w->country[v->country[k]].naval_tech / 15 : 1;
				lv[k] = (L > 4 ? 4 : L) - 1;
				shp[k] = units_battle_sprite(w, v->country[k], k == 0, 1);
			}
			if (shp[0]) sprite_draw(shp[0], (tick / 2) % (shp[0]->frames > 0 ? shp[0]->frames : 1), dst, pitch, IW_X(204), IW_Y(172), IW_X(TA[lv[0]][0]) + shp[0]->ox, IW_Y(TA[lv[0]][1]) + shp[0]->oy);
			for (k = 0; k < 2; k++) {
				const Sprite *sm = &smk[k];
				if (!sm->w) continue;
				n2 = lv[k] + 2 > 4 ? 4 : lv[k] + 2;
				for (q = 0; q < n2; q++) {
					const short *pq = k ? SD[lv[k]][q] : SA[lv[k]][q];
					int fr = ((k ? 20 : 0) + 5 * q + tick) % (k ? 51 : 45);
					sprite_draw(sm, fr, dst, pitch, IW_X(204), IW_Y(172), IW_X(pq[0]), IW_Y(pq[1]));
				}
			}
			if (shp[1]) sprite_draw(shp[1], (tick / 2) % (shp[1]->frames > 0 ? shp[1]->frames : 1), dst, pitch, IW_X(204), IW_Y(172), IW_X(TD[lv[1]][0]) + shp[1]->ox, IW_Y(TD[lv[1]][1]) + shp[1]->oy);
		}
		for (k = 1; k >= 0 && v->kind != MBV_NAVAL; k--) {   /* the figures: the defender first, up to 3 a side, +(20,8) each (0x4ff240) */
			const Sprite *fg = units_battle_sprite(w, v->country[k], k == 0, 0);
			int nfig = v->nstack[k] > 3 ? 3 : v->nstack[k], f2, bx = k ? 88 : 84, by = k ? 81 : 98;
			if (!fg) continue;
			for (f2 = 0; f2 < nfig; f2++) {
				int o = nfig == 1 ? 1 : f2;
				sprite_draw(fg, 0, dst, pitch, IW_X(204), IW_Y(172), IW_X(bx + 20 * o) + fg->ox, IW_Y(by + 8 * o) + fg->oy);   /* the positions are the top-left corner */
			}
		}
		put(v->phase ? &g_bshock : &g_bfire, 0, dst, pitch, IW_X(20), IW_Y(158));
		put(v->phase ? &g_bshock : &g_bfire, 0, dst, pitch, IW_X(168), IW_Y(74));
		snprintf(s, sizeof s, "%ld", tot > 0 ? 10 * v->power[0] / tot : 0); text_right(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(43), IW_Y(159), s, g_pen_white);
		snprintf(s, sizeof s, "%ld", tot > 0 ? 10 * v->power[1] / tot : 0); text_right(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(199), IW_Y(75), s, g_pen_white);
		snprintf(s, sizeof s, "%s%d", v->roll[0] >= v->roll[1] ? "+" : "", v->roll[0] - v->roll[1]); text_right(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(59), IW_Y(159), s, g_pen_white);
	}
	for (i = 0; i < 3; i++) put(&g_utype[v->kind == MBV_NAVAL ? 1 : 0][i], 0, dst, pitch, IW_X(86), IW_Y(208 + (i == 0 ? 0 : i == 1 ? 36 : 71)));
	for (k = 0; k < 2; k++) {                          /* unit counts (0x4ff340 / garrison 0x500ce0) */
		static const int ry[3] = { 217, 253, 288 };
		int gar = siege && k == 1;
		if (gar) bat_text(174, 203, text_get("GARRISON"), grey2, dst, pitch);
		for (i = 0; i < 3; i++) {
			int pen = gar ? grey2 : v->kind != MBV_NAVAL && ((i == 1 && (v->terrain == 1 || v->terrain == 2 || v->terrain == 4)) || (i == 2 && v->terrain == 4)) ? grey : g_pen_white;
			snprintf(s, sizeof s, "%ld", v->n[k][i]); bat_text(k ? 174 : 50, ry[i], s, pen, dst, pitch);
		}
		if (!siege && v->cav_adv[k] && g_advcav.w) put(&g_advcav, 0, dst, pitch, IW_X(k ? 174 : 50) - g_advcav.w / 2, IW_Y(304));
	}
}

/* ------------------------------------------- peace window (iw_peace, SPEC_UI §4.12) */
/* layout from the exe (draw 0x512960, setup 0x510aa0, clicks 0x5121e8..0x51292d), window-relative.
 * Ours: the meter of page 0 sits at y 120 (the exe's y 14 would cover the shields: NIEPEWNE which
 * origin it uses), the receiver list shows who gets the provinces (not clickable), the province
 * list pages with "...". Not drawn: the leader / alone toggle (a separate peace is not ours yet). */
static Sprite g_iwpea[2], g_heav, g_pmeter, g_pbtn[3], g_kryss, g_pplus, g_pminus;
#define PEA_ROWS(f) ((83 * g_scale / 1000) / ((f)->cell))

static int pea_meter_y(int page) { return page == 1 ? 14 : page == 2 ? 117 : 120; }

static void peace_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	static const char *bf[3] = { "btn_demand", "btn_notribute", "btn_offer" };
	static const char *bl[3] = { "PODEMAND", "PONO", "POOFFER" };
	const UiPeace *p = st->peace;
	char s[96];
	int i, gold = g_pen_orange, my = pea_meter_y(p->page), mx;
	g_tip_pw = *p; g_tip_pw_on = 1; g_tip_world = w;
	iw_fonts();
	if (!g_iwpea[0].w) {
		char path[96];
		load(&g_iwpea[0], g_gfx, "gfx/interface/iw_peace.bmp", g_inv, 1);
		load(&g_iwpea[1], g_gfx, "gfx/interface/iw_peace2.bmp", g_inv, 1);
		load(&g_heav, g_gfx, "gfx/interface/peace resolution/heavens.bmp", g_inv, 1);
		load(&g_pmeter, g_gfx, "gfx/interface/peace resolution/peace_meter.bmp", g_inv, 1);
		for (i = 0; i < 3; i++) { snprintf(path, sizeof path, "gfx/interface/peace resolution/%s.bmp", bf[i]); load(&g_pbtn[i], g_gfx, path, g_inv, 4); }   /* 192x42 = 4 frames of 48x42 */
		load(&g_kryss, g_gfx, "gfx/interface/btn_kryssruta_small.bmp", g_inv, 4);
		load(&g_pplus, g_gfx, "gfx/interface/button area/btn_plus.bmp", g_inv, 4);
		load(&g_pminus, g_gfx, "gfx/interface/button area/btn_minus.bmp", g_inv, 4);
	}
	put(&g_iwpea[p->page == 1], 0, dst, pitch, 0, S(103));
	/* the war-score meter: x = 20 + 0.91 S + 91 */
	put(&g_heav, 0, dst, pitch, IW_X(20), IW_Y(my));
	mx = 20 + (91 * p->score) / 100 + 91;
	put(&g_pmeter, 0, dst, pitch, IW_X(mx), IW_Y(my));
	snprintf(s, sizeof s, "%s%d%%", p->score > 0 ? "+" : "", p->score);
	{	int tw = g_have_f14 ? font_width(&g_f14, s) : 0, cx = IW_X(mx);
		if (cx - tw / 2 < IW_X(20)) cx = IW_X(20) + tw / 2;
		if (cx + tw / 2 > IW_X(202)) cx = IW_X(202) - tw / 2;
		text_at(&g_f14, g_have_f14, dst, pitch, cx, IW_Y(my) + g_heav.h, s, gold, 1); }
	if (p->page != 1) {                                /* the two sides: shields and the three lines */
		const Sprite *sh;
		char sp[80];
		for (i = 0; i < 2; i++) {
			snprintf(sp, sizeof sp, "gfx/map/shields/small/smallshield_%s.bmp", w->country[i ? p->enemy : p->us].tag);
			if ((sh = cached(sp, 4)) != NULL) put(sh, 0, dst, pitch, IW_X(24), IW_Y(i ? 49 : 14));
		}
		/* the exe centres them on x 112 in g14b; with our faces they would cover the shields: left at 56, the small face */
		snprintf(s, sizeof s, text_get(p->us_alone ? "PONATION" : "POLEADER"), text_get(w->country[p->us].tag));
		if (g_have_fsmall && font_width(&g_fsmall, s) > S(150)) snprintf(s, sizeof s, "%s", text_get(w->country[p->us].tag));   /* too wide: the name alone */
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(56), IW_Y(22), s, gold, 0);
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(56), IW_Y(39), text_get("PONEGBLABLA"), gold, 0);
		snprintf(s, sizeof s, text_get(p->them_alone ? "PONATION" : "POLEADER"), text_get(w->country[p->enemy].tag));
		if (g_have_fsmall && font_width(&g_fsmall, s) > S(150)) snprintf(s, sizeof s, "%s", text_get(w->country[p->enemy].tag));   /* too wide: the name alone */
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, IW_X(56), IW_Y(56), s, gold, 0);
	}
	if (p->page == 0) {
		for (i = 0; i < 3; i++) {
			put(&g_pbtn[i], 0, dst, pitch, IW_X(20), IW_Y(191 + (i == 0 ? 0 : i == 1 ? 41 : 83)));
			text_at(&g_fside, g_have_fside, dst, pitch, IW_X(80), IW_Y(191 + (i == 0 ? 0 : i == 1 ? 41 : 83)) + (g_pbtn[i].h - g_fside.cell) / 2, text_get(bl[i]), g_pen_white, 0);
		}
	} else if (p->page == 1) {
		static const char *ck[3] = { "PO_VASSAL", "PO_RELIGION", "PO_ACCESS" };
		int on[3], rows = PEA_ROWS(&g_f14), y;
		on[0] = p->vassal; on[1] = p->religion; on[2] = p->access;
		if (p->demand) for (i = 0; i < 3; i++) {
			int by = i == 0 ? 62 : i == 1 ? 84 : 107;
			put(&g_kryss, on[i] ? 1 : 0, dst, pitch, IW_X(20), IW_Y(by));
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(41), IW_Y(by + 3), text_get(ck[i]), gold, 0);
		}
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(20), IW_Y(128), text_get("POPROVINCE"), g_pen_white, 0);
		text_right(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(128), text_get("POSCORE"), g_pen_white);   /* the exe: x 88 / 118 left (overlaps with our font) */
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(122), IW_Y(128), text_get("PORECIVER"), g_pen_white, 0);
		hline(dst, pitch, IW_X(20), IW_X(196), IW_Y(145), gold);
		hline(dst, pitch, IW_X(20), IW_X(196), IW_Y(232), gold);
		for (i = 0, y = IW_Y(147); i < rows && p->scroll + i < p->ncand; i++, y += g_f14.cell) {
			int k = p->scroll + i, pv = p->cand[k], pen = p->picked[k] ? gold : g_pen_white;
			char nm[24];
			snprintf(nm, sizeof nm, "%s", w->prov[pv].name ? w->prov[pv].name : "?");
			while (g_have_f14 && nm[0] && font_width(&g_f14, nm) > S(66)) nm[strlen(nm) - 1] = 0;   /* clipped before the value column */
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(20), y, nm, pen, 0);
			snprintf(s, sizeof s, "%d", peace_prov_value(w, pv)); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(112), y, s, pen);
			if (p->recv[k] >= 0) text_at(&g_f14, g_have_f14, dst, pitch, IW_X(122), y, w->country[p->recv[k]].tag, pen, 0);
		}
		if (p->ncand > rows) text_right(&g_f14, g_have_f14, dst, pitch, IW_X(196), IW_Y(232) - g_f14.cell, "...", g_pen_white);
		for (i = 0, y = IW_Y(240); i < p->nside && y < IW_Y(314) - g_f14.cell; i++, y += g_f14.cell)
			text_at(&g_f14, g_have_f14, dst, pitch, IW_X(20), y, text_get(w->country[p->side[i]].tag), g_pen_white, 0);
		snprintf(s, sizeof s, "%d%%", p->value); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(174), IW_Y(240), s, g_pen_white, 1);
		snprintf(s, sizeof s, "%ld", p->gold); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(174), IW_Y(276), s, gold, 1);
		put(&g_pplus, 0, dst, pitch, IW_X(152), IW_Y(293));
		put(&g_pminus, 0, dst, pitch, IW_X(180), IW_Y(293));
	} else if (p->msg) rich_block(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(191), S(176), S(120), p->msg, g_pen_white, 0);
	{	/* CANCEL; SEND / Annex */
		const char *l = text_get("CANCEL");
		int bw = (g_have_fside ? font_width(&g_fside, l) : 40) + S(32);
		ev_load();
		text_button(dst, pitch, IW_X(p->page == 0 ? 112 : 66) - bw / 2, IW_Y(319), bw, l, 0);
		if (p->page) {
			l = text_get(p->page == 1 ? "PCSEND" : "PCANNEX");
			bw = (g_have_fside ? font_width(&g_fside, l) : 40) + S(32);
			text_button(dst, pitch, IW_X(158) - bw / 2, IW_Y(319), bw, l, 0);
		}
	}
}

/* the message log panel (class 0x577080, draw 0x5776a0): always there, empty or not - the bottom of the
 * map from the sidebar to the right edge, P = 5 lines + one bold line high plus 16, darkened to half,
 * a grey (128,128,128) line along its top; 5 lines of garamond14 gold (255,189,0) at x + 8 + 20,
 * the newest at the bottom */
int ui_log_height(void)
{
	int lh = (g_have_fsmall ? g_fsmall.cell : S(12)) + 1;
	return 6 * lh + S(16);
}
void ui_log_draw(unsigned char *dst, int pitch, int x, int y, int w, const char *const *lines, int n)
{
	int i, j, lh = (g_have_fsmall ? g_fsmall.cell : S(12)) + 1, h = ui_log_height(), gold, grey;
	if (!g_have_fsmall) return;
	gold = nearest(g_mappal, 255, 189, 0); grey = nearest(g_mappal, 128, 128, 128);
	y -= h;
	for (j = y; j < y + h; j++) {                         /* darkened backdrop (0x4024c0) */
		unsigned char *r = dst + (long)j * pitch + x;
		if (j < 0 || j >= g_sh) continue;
		for (i = 0; i < w && x + i < g_sw; i++) r[i] = g_shadow[r[i]];
	}
	if (y >= 0 && y < g_sh) hline(dst, pitch, x, x + w - 1, y, grey);
	if (n > 5) { lines += n - 5; n = 5; }
	for (i = 0; i < n; i++) {
		char t[120];
		int ty = y + h - S(8) - (n - i) * lh;              /* the newest line at the bottom */
		snprintf(t, sizeof t, "%s", lines[i]);
		while (t[0] && font_width(&g_fsmall, t) > w - S(36)) t[strlen(t) - 1] = 0;
		font_draw(&g_fsmall, dst, pitch, g_sw, g_sh, x + S(28), ty, t, gold);
	}
}

int ui_window_tip(int x, int y, char *t1, int n1, char *t2, int n2)
{
#define IN(x0, y0, w0, h0) (x >= IW_X(x0) && x < IW_X((x0) + (w0)) && y >= IW_Y(y0) && y < IW_Y((y0) + (h0)))
	t1[0] = t2[0] = 0;
	if (g_tip_bv_on) {                                 /* 0x4fee50 / 0x4fed90 / 0x500e90 */
		const MilBattleView *v = &g_tip_bv;
		int i;
		static const char *rk[2][3] = { { "CRBINF", "CRBCAV", "CRBART" }, { "CRBWAR", "CRBGAL", "CRBTRA" } };
		static const int ry[4] = { 208, 238, 275, 311 };
		if (IN(72, 11, 80, 40)) { snprintf(t1, n1, "RATIO%d", g_tip_gauge); snprintf(t1, n1, "%s", text_get(t1)); snprintf(t2, n2, "%s", text_get("RATIO_D")); return 0; }
		if (IN(68, 54, 20, 16) || IN(120, 54, 20, 16)) { snprintf(t1, n1, "%s", text_get("CRBATTRITION")); return 1; }
		for (i = 0; i < 3; i++) if (IN(86, ry[i], 52, ry[i + 1] - ry[i])) { snprintf(t1, n1, "%s", text_get(rk[v->kind == MBV_NAVAL][i])); return 10 + i; }
		if (v->kind == MBV_SIEGE) {
			if (IN(150, 149, 40, 21) && v->breaches > 0) { snprintf(t1, n1, "%s", text_get("CRBBREACH")); return 20; }
			if (IN(14, 127, 196, 32) || IN(20, 74, 184, 98)) {   /* CRBSIEGE1..6 from the chance of the monthly test (ours, NIEPEWNE) */
				int need = 14 - v->rating + v->hold, k;
				k = need > 10 ? 1 : need >= 10 ? 2 : need >= 9 ? 3 : need >= 8 ? 4 : need >= 5 ? 5 : 6;
				snprintf(t1, n1, "CRBSIEGE%d", k); snprintf(t1, n1, "%s", text_get(t1));
				return 21;
			}
			return -1;
		}
		if (IN(20, 156, 40, 18) || IN(168, 72, 36, 18)) { snprintf(t1, n1, "%s", text_get(v->phase ? "CRBSHOCK" : "CRBFIRE")); snprintf(t2, n2, "%s", text_get(v->phase ? "CRBSHOCKI" : "CRBFIREI")); return 2; }
		if (IN(44, 156, 20, 18)) { snprintf(t1, n1, "%s", text_get(v->phase ? "CRB_ROLL_ASHOCK" : "CRB_ROLL_AFIRE")); snprintf(t2, n2, "%s", text_get("CRB_ROLL_DADA")); return 3; }
		if ((IN(32, 302, 40, 16) && v->cav_adv[0]) || (IN(156, 302, 40, 16) && v->cav_adv[1])) { snprintf(t1, n1, "%s", text_get("CRBSUPERIORITY")); return 4; }
		return -1;
	}
	if (g_dom_on && g_dom_w && g_dom_c >= 0) {         /* the domestic window (0x518090): buttons and rows */
		int h = ui_domestic_hit(x, y), i;
		static char delay[160];
		if (h >= 0) {
			int k = h / 2, s = g_dom_w->country[g_dom_c].policy[k] + ((h & 1) ? 1 : -1);
			char kk[24];
			if (g_dom_closed) { snprintf(t1, n1, "%s", g_dom_note_txt); return 80; }
			if (s < 0 || s > 10) return -1;
			snprintf(kk, sizeof kk, "DOMNAME_%s_%c", dom_key[k], (h & 1) ? 'R' : 'L');
			snprintf(delay, sizeof delay, text_get("DOMW_MODIFY"), text_get(kk));
			snprintf(t1, n1, "%s", delay);
			dom_stats(t1 + strlen(t1), n1 - (int)strlen(t1), k, s);
			snprintf(kk, sizeof kk, "POLICYDESC_%d", k); snprintf(t2, n2, "%s", text_get(kk));
			return 81 + h;
		}
		for (i = 0; i < 8; i++) if (y >= DM_ROW(i) && y < DM_ROW(i) + S(36) && x >= S(16) && x < S(208)) {
			char kk[24];
			dom_stats(t1, n1, i, g_dom_w->country[g_dom_c].policy[i]);
			snprintf(kk, sizeof kk, "POLICYDESC_%d", i); snprintf(t2, n2, "%s", text_get(kk));
			return 100 + i;
		}
	}
	if (g_bud_on) {                                    /* budget buttons: TWR_* (0x51c90e..0x51c9ff) */
		int h = ui_budget_btn_hit(x, y);
		if (h == 1) { snprintf(t1, n1, "%s", text_get(g_bud_wt == 3 ? "TWR_WARTAX3" : g_bud_wt == 1 ? "TWR_WARTAX1" : g_bud_wt == 2 ? "TWR_WARTAX2" : "TWR_WT")); return 70; }
		if (h == 2) { if (g_bud_loan) snprintf(t1, n1, text_get("TWR_TAKELOAN"), (int)g_bud_lsize); else snprintf(t1, n1, "%s", text_get("TWR_5LOANS")); return 71; }
		{	static const char *const bk[5] = { "BUTTON_0048", "BUTTON_0049", "BUTTON_0045", "BUTTON_0047", "BUTTON_0046" };   /* buttons.txt */
			int b = ui_budget_boost_hit(x, y);
			if (b >= 0) { snprintf(t1, n1, "%s", text_get(bk[b])); return 72 + b; }
		}
		return -1;
	}
	if (g_pu_on) {                                     /* purchase 0x4f5110 / 0x4f54f0 */
		static const char *pk[2][3] = { { "PURCHASEINF", "PURCHASECAV", "PURCHASEART" }, { "PURCHASEW", "PURCHASEG", "PURCHASET" } };
		int i;
		for (i = 0; i < 3; i++) if (IN(26 + 60 * i, 40, 52, 32)) { snprintf(t1, n1, "%s", text_get(pk[g_tip_pu.naval][i])); return 50 + i; }
		if (g_tip_pu.merc_shown && IN(20, 274, 184, 18)) { snprintf(t1, n1, "%s", g_tip_pu.merc_tip); return 54; }
		if (g_tip_pu.bld_ok && IN(20, 292, 184, 34)) {
			snprintf(t1, n1, text_get(g_tip_pu.naval ? "PURCHASESHIP_I1" : "PURCHASEBAR_I1"), (int)g_tip_pu.bld_cost);
			snprintf(t2, n2, "%s", text_get(g_tip_pu.naval ? "PURCHASESHIP_D" : "PURCHASEBAR_D"));
			return 53;
		}
		return -1;
	}
	if (g_tip_res_on && g_tip_world) {                 /* resource 0x51bca0 / 0x51bf70: bands from y 90 */
		const EconGoods *g = &g_tip_res;
		const char *gn = text_get(world_name(g_tip_world, (short)g_tip_res_id));
		if (x < IW_X(20) || x >= IW_X(204)) return -1;
		if (g->gold && y >= IW_Y(90) && y < IW_Y(311)) { snprintf(t1, n1, "%s", text_get("RW_I_GOLD")); snprintf(t2, n2, "%s", text_get("RW_D_GOLD")); return 60; }
		if (y >= IW_Y(90) && y < IW_Y(104)) {
			snprintf(t1, n1, text_get("RW_I_BASICPRICE"), gn, text_get(g->base <= 5 ? "RW_I_PRICE_L" : g->base <= 10 ? "RW_I_PRICE_M" : g->base <= 15 ? "RW_I_PRICE_H" : "RW_I_PRICE_VH"));
			return 61;
		}
		if (y >= IW_Y(104) && y < IW_Y(118)) { snprintf(t1, n1, text_get("RW_I_SUPPLY"), gn); snprintf(t2, n2, text_get("RW_D_SUPPLY"), g->nprov, gn); return 62; }
		if (y >= IW_Y(118) && y < IW_Y(160)) { snprintf(t1, n1, "%s", text_get("RW_I_MAIN")); return 63; }
		if (y >= IW_Y(166) && y < IW_Y(194)) { snprintf(t1, n1, text_get("RW_I_PROD"), gn); snprintf(t2, n2, "%s", text_get("RW_D_PROD")); return 64; }
		if (y >= IW_Y(194) && y < IW_Y(208)) { snprintf(t1, n1, text_get("RW_I_INFRA"), (int)(g->prod_eff / 10)); snprintf(t2, n2, "%s", text_get("RW_D_INFRA")); return 65; }
		if (y >= IW_Y(208) && y < IW_Y(222)) { snprintf(t1, n1, text_get("RW_I_OURPI"), gn); snprintf(t2, n2, "%s", text_get("RW_D_OURPI")); return 66; }
		if (y >= IW_Y(222) && y < IW_Y(236)) { snprintf(t1, n1, text_get("RW_I_LEADER"), gn); return 67; }
		if (y >= IW_Y(254) && y < IW_Y(283)) { snprintf(t1, n1, "%s", text_get("RW_I_OTRADE")); snprintf(t2, n2, "%s", text_get("RW_D_OTRADE")); return 68; }
		if (y >= IW_Y(283) && y < IW_Y(297)) { snprintf(t1, n1, text_get("RW_I_TRADE"), (int)(g->trade_eff / 10)); snprintf(t2, n2, "%s", text_get("RW_D_TRADE")); return 69; }
		if (y >= IW_Y(297) && y < IW_Y(311)) { snprintf(t1, n1, text_get("RW_I_OURTI"), gn); snprintf(t2, n2, "%s", text_get("RW_D_OURTI")); return 70; }
		return -1;
	}
	if (g_rel_on && g_tip_world && IN(20, 276, 184, 34)) {   /* religion 0x51ade0: "%s : %s" per faith, then RELW_DEFENDER_DESC */
		size_t l = 0;
		int r;
		t1[0] = 0;
		for (r = 0; r < REL_PAGAN && l + 2 < (size_t)n1; r++) {
			int dc = rel_defender(g_tip_world, r);
			if (r == REL_COUNTER) continue;
			l += (size_t)snprintf(t1 + l, (size_t)n1 - l, "%s%s : %s", l ? "\\n" : "", text_get(rel_label(-1, r)), dc >= 0 ? text_get(g_tip_world->country[dc].tag) : "NONE");
		}
		snprintf(t2, n2, "%s", text_get("RELW_DEFENDER_DESC"));
		return 80;
	}
	if (g_tip_pw_on) {                                 /* 0x5132e0 */
		const UiPeace *p = &g_tip_pw;
		const char *who = text_get(p->demand ? "POROLLDEMAND" : "POROLLOFFER");
		int my = pea_meter_y(p->page);
		if (IN(20, my, 184, 38)) {                         /* the advice texts PO_PD_A..J by the score (where the exe shows them: NIEZNANE) */
			int sc = p->score;
			char k = sc <= -75 ? 'A' : sc <= -50 ? 'B' : sc <= -25 ? 'C' : sc < 0 ? 'D' : sc < 25 ? 'E' : sc < 50 ? 'F' : sc < 75 ? 'G' : sc < 100 ? 'H' : 'I';
			snprintf(t2, n2, "PO_PD_%c", k); snprintf(t1, n1, "%s", text_get(t2)); t2[0] = 0;
			return 30;
		}
		if (p->page != 1) return -1;
		if (IN(152, 293, 16, 16)) { snprintf(t1, n1, text_get("POROLLPLUS"), who); return 31; }
		if (IN(180, 293, 16, 16)) { snprintf(t1, n1, text_get("POROLLMINUS"), who); return 32; }
		if (y >= IW_Y(147) && y < IW_Y(230) && x >= IW_X(20) && x < IW_X(196)) {
			int i = (y - IW_Y(147)) / (g_f14.cell), k = p->scroll + i;
			if (i < PEA_ROWS(&g_f14) && k < p->ncand && g_tip_world) {
				const char *nm = g_tip_world->prov[p->cand[k]].name ? g_tip_world->prov[p->cand[k]].name : "?";
				if (p->picked[k]) snprintf(t1, n1, text_get("POROLLPROV"), nm, who);
				else snprintf(t1, n1, text_get(p->demand ? "POROLLDEMPROV" : "POROLLOFFPROV"), nm);
				return 40 + k;
			}
		}
	}
	return -1;
#undef IN
}

/* 0x523c57 / 0x41f2a0: entries (255,198,0), the hovered one white, a disabled one grey (127,127,127) whatever the
 * mouse does; separators 5 pixels high in (255,189,0) (0x52db80) */
#define PM_MAXROW 32
static int g_pm_x, g_pm_y, g_pm_w, g_pm_lh, g_pm_n, g_pm_top[PM_MAXROW + 1];
static unsigned char g_pm_fl[PM_MAXROW];
void ui_popmenu_draw(unsigned char *dst, int pitch, int x, int y, const char *const *labels, const unsigned char *flags, int n, int hover)
{
	int i, j, w = 0, h, sep = S(5) < 3 ? 3 : S(5), grey = nearest(g_mappal, 127, 127, 127), gold = nearest(g_mappal, 255, 198, 0), white = nearest(g_mappal, 255, 255, 255);
	if (!g_have_fside) return;
	if (n > PM_MAXROW) n = PM_MAXROW;
	for (i = 0; i < n; i++) { int lw = flags[i] & PM_SEP ? 0 : font_width(&g_fside, labels[i]); if (lw > w) w = lw; }
	g_pm_lh = g_fside.cell + S(4); w += S(24);
	for (h = S(4), i = 0; i < n; i++) { g_pm_top[i] = h; g_pm_fl[i] = flags[i]; h += flags[i] & PM_SEP ? sep : g_pm_lh; }
	g_pm_top[n] = h; g_pm_n = n; h += S(4);
	if (x + w > g_sw - 2) x = g_sw - 2 - w;
	if (y + h > g_sh - 2) y = g_sh - 2 - h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	g_pm_x = x; g_pm_y = y; g_pm_w = w;
	for (j = y; j < y + h; j++) { unsigned char *r = dst + (long)j * pitch + x; for (i = 0; i < w; i++) r[i] = g_shadow[g_shadow[r[i]]]; }
	hline(dst, pitch, x, x + w, y, g_pen_gold); hline(dst, pitch, x, x + w, y + h - 1, g_pen_gold);
	for (j = y; j < y + h; j++) { dst[(long)j * pitch + x] = (unsigned char)g_pen_gold; dst[(long)j * pitch + x + w - 1] = (unsigned char)g_pen_gold; }
	for (i = 0; i < n; i++) {
		if (flags[i] & PM_SEP) { hline(dst, pitch, x + S(6), x + w - S(6), y + g_pm_top[i] + sep / 2, nearest(g_mappal, 255, 189, 0)); continue; }
		text_at(&g_fside, g_have_fside, dst, pitch, x + S(12), y + g_pm_top[i] + S(2), labels[i], flags[i] & PM_GREY ? grey : i == hover ? white : gold, 0);
	}
}

int ui_popmenu_hit(int x, int y, int n)
{
	int i;
	if (x < g_pm_x || x >= g_pm_x + g_pm_w || !g_pm_lh) return -1;
	if (n > g_pm_n) n = g_pm_n;
	for (i = 0; i < n; i++)
		if (y >= g_pm_y + g_pm_top[i] && y < g_pm_y + g_pm_top[i + 1]) return g_pm_fl[i] & (PM_SEP | PM_GREY) ? -1 : i;
	return -1;
}
int ui_popmenu_inside(int x, int y) { return g_pm_lh && x >= g_pm_x && x < g_pm_x + g_pm_w && y >= g_pm_y && y < g_pm_y + g_pm_top[g_pm_n] + S(4); }

int ui_peace_rows(void) { iw_fonts(); return PEA_ROWS(&g_f14); }

int ui_peace_hit(int x, int y, const UiPeace *p)
{
#define IN(x0, y0, w0, h0) (x >= IW_X(x0) && x < IW_X((x0) + (w0)) && y >= IW_Y(y0) && y < IW_Y((y0) + (h0)))
	int i;
	if (IN(p->page == 0 ? 72 : 26, 315, 80, 40)) return UP_CANCEL;
	if (p->page != 1 && p->can_toggle && IN(24, 49, 28, 28)) return UP_THEM;   /* the enemy shield: leader / alone (0x5119f0) */
	if (p->page == 0) {
		if (IN(20, 191, 192, 42)) return UP_DEMAND;
		if (IN(20, 232, 192, 42)) return UP_NOTRIB;
		if (IN(20, 274, 192, 42)) return UP_OFFER;
		return UP_NONE;
	}
	if (IN(118, 315, 80, 40)) return p->page == 1 ? UP_SEND : UP_ANNEX;
	if (p->page != 1) return UP_NONE;
	if (p->demand) {
		if (IN(20, 60, 180, 20)) return UP_VASSAL;
		if (IN(20, 82, 180, 20)) return UP_RELIGION;
		if (IN(20, 105, 180, 20)) return UP_ACCESS;
	}
	if (IN(152, 293, 16, 16)) return UP_PLUS;
	if (IN(180, 293, 16, 16)) return UP_MINUS;
	if (IN(150, 220, 50, 12)) return UP_MORE;
	if (y >= IW_Y(147) && y < IW_Y(230) && x >= IW_X(20) && x < IW_X(196)) {
		i = (y - IW_Y(147)) / (g_f14.cell);
		if (i < PEA_ROWS(&g_f14) && p->scroll + i < p->ncand) return UP_ROW + p->scroll + i;
	}
	return UP_NONE;
#undef IN
}

/* ------------------------------------- purchase window (windows4 1: draw 0x4f3d90 / 0x4f47f0 / 0x4f5950) */
static Sprite g_iwpur[2], g_pupp, g_pner;
static const int pu_x[3] = { 37, 101, 162 };               /* the arrows; the column centre is +10 */

static const char *no_esc(const char *t) { return (unsigned char)t[0] == 0xA7 && t[1] ? t + 2 : t; }   /* a leading colour code */

static void purchase_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const UiPurchase *p = st->purchase;
	char s[64];
	int i, gold = g_pen_gold;
	iw_fonts();
	unit_panel_load(st->player >= 0 ? (1 + w->country[st->player].land_tech / 20 > 3 ? 3 : 1 + w->country[st->player].land_tech / 20) : 1);
	if (!g_iwpur[0].w) {
		load(&g_iwpur[0], g_gfx, "gfx/interface/iw_purchasearmy.bmp", g_inv, 1);
		load(&g_iwpur[1], g_gfx, "gfx/interface/iw_purchasenavy.bmp", g_inv, 1);
		load(&g_pupp, g_gfx, "gfx/interface/reorganisation/btn_pil_upp.bmp", g_inv, 4);
		load(&g_pner, g_gfx, "gfx/interface/reorganisation/btn_pil_ner.bmp", g_inv, 4);
	}
	g_pu_on = 1; g_tip_pu = *p;
	put(&g_iwpur[p->naval ? 1 : 0], 0, dst, pitch, 0, S(103));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(19), no_esc(text_get("IWPTITLE")), gold, 1);
	for (i = 0; i < 3; i++) put(&g_utype[p->naval][i], 0, dst, pitch, IW_X(26 + 60 * i), IW_Y(40));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(86), no_esc(text_get("IWPAVIA")), gold, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(124), no_esc(text_get(p->naval ? "IWPCOSTNAV" : "IWPCOST")), gold, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(213), no_esc(text_get("IWPORD")), gold, 1);
	for (i = 0; i < 3; i++) {
		int cx = pu_x[i] + 10;
		snprintf(s, sizeof s, "%ld", p->avail[i]); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(cx), IW_Y(100), s, g_pen_white, 1);
		snprintf(s, sizeof s, "%ld$", p->cost[i]); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(cx), IW_Y(139), s, g_pen_white, 1);
		put(&g_pupp, p->ordered[i] > 0 ? 0 : 3, dst, pitch, IW_X(pu_x[i]), IW_Y(156));
		put(&g_pner, p->avail[i] > 0 ? 0 : 3, dst, pitch, IW_X(pu_x[i]), IW_Y(185));
		snprintf(s, sizeof s, "%ld", p->ordered[i]); text_at(&g_f14, g_have_f14, dst, pitch, IW_X(cx), IW_Y(230), s, g_pen_white, 1);
	}
	{	/* IWPTOTAL "§WTOTAL COST: §Y" + "%d$": the label white, the sum gold, centred on 112 */
		char lab[64], v[24];
		const char *src = text_get("IWPTOTAL");
		int n = 0, tw;
		for (; *src && n < (int)sizeof lab - 1; src++) { if ((unsigned char)*src == 0xA7 && src[1]) { src++; continue; } lab[n++] = *src; }
		lab[n] = 0;
		snprintf(v, sizeof v, "%ld$", p->total);
		tw = g_have_fside ? font_width(&g_fside, lab) + font_width(&g_fside, v) : 0;
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112) - tw / 2, IW_Y(258), lab, g_pen_white, 0);
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112) - tw / 2 + (g_have_fside ? font_width(&g_fside, lab) : 0), IW_Y(258), v, gold, 0);
	}
	ev_load();
	if (p->merc_shown)                                 /* RAISEMERC (112,276): a text line here, our buttons are taller than the exe's */
		text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(276), text_get("RAISEMERC"), p->merc_ok ? g_pen_white : nearest(g_mappal, 0x80, 0x80, 0x80), 1);
	if (p->bld_ok) text_btn_c(dst, pitch, IW_X(112), IW_Y(294), text_get(p->naval ? "BUILDSHIPYARD" : "BUILDBARRACK"));
	text_btn_c(dst, pitch, IW_X(66), IW_Y(319), text_get("CANCEL"));
	text_btn_c(dst, pitch, IW_X(158), IW_Y(319), "OK");
	g_pu_on = p->bld_ok ? 2 : 1;
}

int ui_purchase_hit(int x, int y)
{
	int i;
	if (!g_pu_on) return UPU_NONE;
	for (i = 0; i < 3; i++) {
		if (x >= IW_X(pu_x[i]) && x < IW_X(pu_x[i] + 20) && y >= IW_Y(156) && y < IW_Y(176)) return UPU_UP + i;
		if (x >= IW_X(pu_x[i]) && x < IW_X(pu_x[i] + 20) && y >= IW_Y(185) && y < IW_Y(205)) return UPU_DOWN + i;
	}
	if (y >= IW_Y(315) && y < IW_Y(315) + S(40)) return x < IW_X(112) ? UPU_CANCEL : UPU_OK;
	if (g_tip_pu.merc_ok && y >= IW_Y(274) && y < IW_Y(292) && x >= IW_X(20) && x < IW_X(204)) return UPU_MERC;
	if (g_pu_on == 2 && y >= IW_Y(292) && y < IW_Y(292) + S(34) && x >= IW_X(20) && x < IW_X(204)) return UPU_BUILD;
	return UPU_NONE;
}

/* ------------------------------------- religion window (windows4 2: draw 0x51a4d0, setup 0x51a530) */
static Sprite g_iwrel, g_radio2;
static int g_rel_ntarget, g_rel_nshown;

static void rel_slider(unsigned char *dst, int pitch, int y, int v)
{
	int x0 = IW_X(46), span = g_track.w - g_knob.w;
	put(&g_track, 0, dst, pitch, x0, IW_Y(y));
	put(&g_knob, 0, dst, pitch, x0 + (int)((long)(v + 100) * span / 200), IW_Y(y) + (g_track.h - g_knob.h) / 2);
}

static void religion_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	const UiReligion *r = st->religion;
	const Country *co = &w->country[st->player];
	int k, gold = g_pen_gold;
	iw_fonts();
	if (!g_iwrel.w) {
		load(&g_iwrel, g_gfx, "gfx/interface/iw_religion.bmp", g_inv, 1);
		load(&g_radio2, g_gfx, "gfx/settings/btn_radio.bmp", g_inv, 4);
		if (!g_track.w) load(&g_track, g_gfx, "gfx/interface/sprite_slider.bmp", g_inv, 1);
		if (!g_knob.w) load(&g_knob, g_gfx, "gfx/interface/button area/btn_slider.bmp", g_inv, 4);
	}
	g_rel_on = 1; g_rel_ntarget = r->ntarget; g_rel_nshown = r->nshown; g_tip_world = w;
	put(&g_iwrel, 0, dst, pitch, 0, S(103));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(16), text_get("RELWSTATE"), gold, 1);
	put(&g_radio2, r->sel == r->state ? 1 : 0, dst, pitch, IW_X(22), IW_Y(34));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(42), IW_Y(35), text_get(rel_label(-1, r->state)), gold, 0);
	for (k = 0; k < r->ntarget; k++) {
		put(&g_radio2, r->sel == r->target[k] ? 1 : 0, dst, pitch, IW_X(22), IW_Y(51 + 17 * k));
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(42), IW_Y(52 + 17 * k), text_get(rel_label(-1, r->target[k])), gold, 0);
	}
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(91), text_get("RELWTOLERANCE"), gold, 1);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(106), text_get(rel_label(-1, r->state)), gold, 1);
	rel_slider(dst, pitch, 120, co->tol[r->state]);
	for (k = 0; k < r->nshown; k++) {
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(133 + 27 * k), text_get(rel_label(r->state, r->shown[k])), gold, 1);
		rel_slider(dst, pitch, 147 + 27 * k, co->tol[r->shown[k]]);
	}
	ev_load();
	if (r->can_defend) text_btn_c(dst, pitch, IW_X(112), IW_Y(280), text_get("RELWDEFEND"));
	text_btn_c(dst, pitch, IW_X(60), IW_Y(319), text_get("BACK"));
	if (r->can_convert) text_btn_c(dst, pitch, IW_X(168), IW_Y(319), text_get("RELWCONVERT"));
}

int ui_religion_hit(int x, int y, int *v)
{
	int k, span;
	if (!g_rel_on) return URL_NONE;
	if (y >= IW_Y(315) && y < IW_Y(315) + S(40)) return x < IW_X(112) ? URL_BACK : URL_CONVERT;
	if (y >= IW_Y(276) && y < IW_Y(276) + S(34) && x >= IW_X(20) && x < IW_X(204)) return URL_DEFEND;
	if (x >= IW_X(18) && x < IW_X(200)) {
		if (y >= IW_Y(32) && y < IW_Y(49)) return URL_RADIO;
		for (k = 0; k < g_rel_ntarget; k++) if (y >= IW_Y(49 + 17 * k) && y < IW_Y(66 + 17 * k)) return URL_RADIO + 1 + k;
	}
	span = g_track.w - g_knob.w;
	if (span > 0 && x >= IW_X(46) && x < IW_X(46) + g_track.w) {
		int sx = x - IW_X(46) - g_knob.w / 2;
		*v = (int)((long)sx * 200 / span) - 100;
		if (*v < -100) *v = -100;
		if (*v > 100) *v = 100;
		if (y >= IW_Y(116) && y < IW_Y(132)) return URL_SLIDER;
		for (k = 0; k < g_rel_nshown; k++) if (y >= IW_Y(143 + 27 * k) && y < IW_Y(159 + 27 * k)) return URL_SLIDER + 1 + k;
	}
	return URL_NONE;
}

static int g_ch_n, g_ch_x, g_ch_w, g_ch_by[4], g_ch_a[4], g_ch_bh, g_ch_pg, g_ch_pgy, g_ch_pgx[2];

int ui_choice_draw(unsigned char *dst, int pitch, const char *title, const char *desc, const char *info,
                   const char *const action[4], const char *tag, int hover, int page)
{
	static char dl[EVL][96], il[6][96];
	Font *ft = &g_fbar, *fd = &g_f14, *fi = &g_fside;
	int hft = g_have_fbar, hfd, hfi = g_have_fside;
	int len = desc ? (int)strlen(desc) : 0, T = S(len > 2000 ? 600 : len > 1500 ? 500 : 400), W = T + S(64);
	int nd, ni, lh, i, k, n = 0, x0, y0, H, hb, room, per, pages, y;
	iw_fonts(); ev_load();
	hfd = g_have_f14;
	if (!g_pgl.w) {
		load(&g_pgl, g_gfx, "gfx/interface/btn_eventpage_left.bmp", g_inv, 4);
		load(&g_pgr, g_gfx, "gfx/interface/btn_eventpage_right.bmp", g_inv, 4);
		load(&g_pgbox, g_gfx, "gfx/interface/box_pagenumber.bmp", g_inv, 1);
	}
	if (W > g_sw - S(8)) { W = g_sw - S(8); T = W - S(64); }
	lh = (hfd ? fd->cell : S(14)) + 1;
	nd = hfd ? wrap(fd, desc ? desc : "", T, dl, EVL) : 0;
	ni = hfi && info ? wrap(fi, info, T, il, 6) : 0;
	hb = g_tb[0][0].h ? g_tb[0][0].h : S(38);
	for (i = 0; i < 4; i++) if (action[i]) g_ch_a[n++] = i;
	/* how many description lines fit: title, info, buttons and margins take the rest of the screen */
	room = g_sh - S(24) - (S(32) + (hft ? ft->cell : S(20)) + S(20)) - (S(21) + ni * (fi->cell)) - (S(5) + n * (hb + S(4))) - S(24);
	per = room / lh;
	if (per < 3) per = 3;
	g_ch_pg = nd > per;
	if (g_ch_pg) per = (room - S(24)) / lh;
	if (per < 1) per = 1;
	pages = (nd + per - 1) / per;
	if (page >= pages) page = pages - 1;
	if (page < 0) page = 0;
	k = nd - page * per; if (k > per) k = per;              /* lines on this page */
	H = S(32) + (hft ? ft->cell : S(20)) + S(20) + (g_ch_pg ? per : k) * lh + (g_ch_pg ? S(24) : 0) + S(21) + ni * (fi->cell) + S(5) + n * (hb + S(4)) + S(16);
	H = (H + 15) & ~15;
	x0 = (g_sw - W) / 2; y0 = (g_sh - H) / 2;
	if (y0 < 0) y0 = 0;
	win8_box(dst, pitch, x0, y0, W, H);
	if (tag) {                                           /* the player's shield, top right */
		char sp[64];
		const Sprite *sh;
		snprintf(sp, sizeof sp, "gfx/map/shields/small/smallshield_%s.bmp", tag);
		if ((sh = cached(sp, 4)) != NULL) put(sh, 0, dst, pitch, x0 + W - sh->w - S(14), y0 + S(12));
	}
	y = y0 + S(32);
	text_at(ft, hft, dst, pitch, x0 + W / 2, y, title ? title : "", g_pen_white, 1);
	y += (hft ? ft->cell : S(20)) + S(20);
	for (i = page * per; i < nd && i < (page + 1) * per; i++, y += lh) text_at(fd, hfd, dst, pitch, x0 + W / 2, y, dl[i], g_pen_gold, 1);
	if (g_ch_pg) {                                       /* the pager under the text */
		char pn[8];
		y = y0 + S(32) + (hft ? ft->cell : S(20)) + S(20) + per * lh + S(2);
		g_ch_pgy = y; g_ch_pgx[0] = x0 + W / 2 - S(40); g_ch_pgx[1] = x0 + W / 2 + S(22);
		put(&g_pgl, page > 0 ? 0 : 3, dst, pitch, g_ch_pgx[0], y);
		put(&g_pgbox, 0, dst, pitch, x0 + W / 2 - S(10), y - S(1));
		put(&g_pgr, page < pages - 1 ? 0 : 3, dst, pitch, g_ch_pgx[1], y);
		snprintf(pn, sizeof pn, "%d", page + 1);
		text_at(&g_fside, g_have_fside, dst, pitch, x0 + W / 2, y, pn, g_pen_white, 1);
		y += S(22);
	}
	y += S(21);
	for (i = 0; i < ni; i++, y += fi->cell) text_at(fi, hfi, dst, pitch, x0 + W / 2, y, il[i], g_pen_white, 1);
	y += S(5);
	g_ch_n = n; g_ch_x = x0 + S(32); g_ch_w = T; g_ch_bh = hb;
	for (i = 0; i < n; i++, y += hb + S(4)) {
		g_ch_by[i] = y;
		text_button(dst, pitch, g_ch_x, y, T, action[g_ch_a[i]], hover == g_ch_a[i] ? 1 : 0);
	}
	return pages;
}

int ui_choice_hit(int x, int y)
{
	int i;
	for (i = 0; i < g_ch_n; i++) if (x >= g_ch_x && x < g_ch_x + g_ch_w && y >= g_ch_by[i] && y < g_ch_by[i] + g_ch_bh) return g_ch_a[i];
	if (g_ch_pg && y >= g_ch_pgy && y < g_ch_pgy + g_pgl.h) {
		if (x >= g_ch_pgx[0] && x < g_ch_pgx[0] + g_pgl.w) return UI_EV_PREV;
		if (x >= g_ch_pgx[1] && x < g_ch_pgx[1] + g_pgr.w) return UI_EV_NEXT;
	}
	return -1;
}

/* ------------------------------------- trade good window (iw_Resource, windows4 4: draw 0x51b2d0 / 0x51b3a0) */
static Sprite g_iwres;
static void resource_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	static const char *gl[20] = { "clo", "gold", "cot", "fish", "furs", "grai", "ivor", "metal", "mineral", "orient",
	                              "navs", "salt", "slav", "spic", "sug", "tob", "wine", "wool", "tea", "coffee" };
	const EconGoods *g = st->goods;
	const char *gn = world_name(w, (short)st->goods_id);
	const Sprite *pic;
	char v[40];
	int i, gold = g_pen_gold, na = g->gold;
	iw_fonts();
	if (!g_iwres.w) load(&g_iwres, g_gfx, "gfx/interface/iw_resource.bmp", g_inv, 1);
	g_tip_res = *g; g_tip_res_on = 1; g_tip_res_id = st->goods_id; g_tip_world = w;
	put(&g_iwres, 0, dst, pitch, 0, S(103));
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(19), text_get(gn ? gn : ""), gold, 1);
	for (i = 0; i < 20; i++)
		if (gn && script_ieq(gn, gl[i]) && (pic = cached("gfx/interface/piw/buildings/resource.bmp", 21)) != NULL) { put(pic, i + 1, dst, pitch, IW_X(112) - pic->w / 2, IW_Y(37)); break; }
#define ROW(y, key, val) do { char lb_[64]; int room_ = IW_X(194) - (g_have_f14 ? font_width(&g_f14, val) : 0) - IW_X(26) - S(4); 		snprintf(lb_, sizeof lb_, "%s", text_get(key)); while (g_have_f14 && lb_[0] && font_width(&g_f14, lb_) > room_) lb_[strlen(lb_) - 1] = 0; 		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(26), IW_Y(y), lb_, gold, 0); text_right(&g_f14, g_have_f14, dst, pitch, IW_X(194), IW_Y(y), val, g_pen_white); } while (0)   /* the label gives way to the value */
	snprintf(v, sizeof v, "%ld$", g->base); ROW(90, "RWBASIC", na ? text_get("RW_NA") : v);
	ROW(104, "RWSUPPLY", na ? text_get("RW_NA") : "100%");      /* (the market is not simulated: NIEPEWNE) */
	ROW(118, "RWDEMAND", na ? text_get("RW_NA") : "100%");
	snprintf(v, sizeof v, "%ld.%02ld$", g->price_m / 1000, g->price_m % 1000 / 10); ROW(132, "RWCURRENT", na ? text_get("RW_NA") : v);
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(166), text_get("RWPROD"), g_pen_white, 1);
	snprintf(v, sizeof v, text_get("RWUNITS"), (int)(g->units / 10)); ROW(180, "RWOURP", v);
	snprintf(v, sizeof v, "%ld%%", g->prod_eff / 10); ROW(194, "RWPE", v);
	fmt1(v, sizeof v, g->prod_income_m); strcat(v, "$"); ROW(208, "RWPINC", v);
	ROW(222, "RWLEADING", g->leader >= 0 ? text_get(w->country[g->leader].tag) : text_get("RWNONE"));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(254), text_get("RWTRADE"), g_pen_white, 1);
	snprintf(v, sizeof v, text_get("RWUNITS"), (int)(g->trade_units / 10)); ROW(269, "RWOURT", v);
	snprintf(v, sizeof v, "%ld%%", g->trade_eff / 10); ROW(283, "RWTE", v);
	fmt1(v, sizeof v, g->trade_income_m); strcat(v, "$"); ROW(297, "RWTINC", v);
#undef ROW
	iw_close(dst, pitch, "RWBACK");
}

/* ------------------------------------- loans window (iw_loans, windows4 6.1: draw 0x50fec0, setup 0x510250) */
static Sprite g_iwloa, g_animval, g_timglas;
static const int lo_y[3] = { 58, 124, 192 }, lo_dy[3] = { 80, 146, 216 };
static void loans_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	char s[32];
	int k;
	iw_fonts();
	if (!g_iwloa.w) {
		load(&g_iwloa, g_gfx, "gfx/interface/iw_loans.bmp", g_inv, 1);
		load(&g_animval, g_gfx, "gfx/interface/anim_value.bmp", g_inv, 6);
		load(&g_timglas, g_gfx, "gfx/interface/timglas.bmp", g_inv, 12);
		if (!g_pupp.w) { load(&g_pupp, g_gfx, "gfx/interface/reorganisation/btn_pil_upp.bmp", g_inv, 4); load(&g_pner, g_gfx, "gfx/interface/reorganisation/btn_pil_ner.bmp", g_inv, 4); }
	}
	g_lo_on = 1;
	put(&g_iwloa, 0, dst, pitch, 0, S(103));
	text_at(&g_f14, g_have_f14, dst, pitch, IW_X(112), IW_Y(11), text_get("LWTITLE"), g_pen_white, 1);
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(8) + g_f14.cell, st->loan_target >= 0 ? text_get(w->country[st->loan_target].tag) : "", g_pen_white, 1);
	snprintf(s, sizeof s, "%ld", st->loan_amount); text_at(&g_f24, g_have_f24, dst, pitch, IW_X(136), IW_Y(65), s, g_pen_white, 1);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(136), IW_Y(106), text_get("LWAMOUNT"), g_pen_white, 1);
	snprintf(s, sizeof s, "%d %%", st->loan_rate); text_at(&g_f24, g_have_f24, dst, pitch, IW_X(136), IW_Y(131), s, g_pen_white, 1);
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(136), IW_Y(174), text_get("LWRATE"), g_pen_white, 1);
	snprintf(s, sizeof s, "%d %s", st->loan_months, text_get("LWMONTHS")); text_at(&g_fside, g_have_fside, dst, pitch, IW_X(136), IW_Y(203), s, g_pen_white, 1);   /* g24b would not fit the frame with our faces */
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(136), IW_Y(244), text_get("LWLENGTH"), g_pen_white, 1);
	for (k = 0; k < 3; k++) { put(&g_pupp, 0, dst, pitch, IW_X(180), IW_Y(lo_y[k])); put(&g_pner, 0, dst, pitch, IW_X(180), IW_Y(lo_dy[k])); }
	put(&g_animval, st->loan_amount / 50 > 5 ? 5 : (int)(st->loan_amount / 50), dst, pitch, IW_X(20), IW_Y(58));     /* coin piles */
	put(&g_animval, 6 * st->loan_rate / 10 > 5 ? 5 : 6 * st->loan_rate / 10, dst, pitch, IW_X(20), IW_Y(124));
	put(&g_timglas, 12 * st->loan_months / 300 > 11 ? 11 : 12 * st->loan_months / 300, dst, pitch, IW_X(34), IW_Y(187));
	ev_load();
	text_btn_c(dst, pitch, IW_X(112), IW_Y(272), text_get("BUTTON_0211"));
	text_btn_c(dst, pitch, IW_X(112), IW_Y(302), text_get("BACK"));
}

int ui_loans_hit(int x, int y)
{
	int k;
	if (!g_lo_on) return 0;
	for (k = 0; k < 3; k++) if (x >= IW_X(180) && x < IW_X(200)) {
		if (y >= IW_Y(lo_y[k]) && y < IW_Y(lo_y[k] + 20)) return 1 + k;
		if (y >= IW_Y(lo_dy[k]) && y < IW_Y(lo_dy[k] + 20)) return 11 + k;
	}
	if (x >= IW_X(40) && x < IW_X(184)) {
		if (y >= IW_Y(268) && y < IW_Y(298)) return 20;
		if (y >= IW_Y(298) && y < IW_Y(330)) return 21;
	}
	return 0;
}

/* ------------------------------------- Grant Independence (iw_Vassal, windows4 6.2: draw 0x51eae0) */
static Sprite g_iwvas;
static int g_vas_n;
static void vassal_draw(unsigned char *dst, int pitch, const World *w, const UiState *st)
{
	char s[64];
	int i, lh;
	iw_fonts();
	if (!g_iwvas.w) load(&g_iwvas, g_gfx, "gfx/interface/iw_vassal.bmp", g_inv, 1);
	g_vas_on = 1; g_vas_n = st->nvas;
	put(&g_iwvas, 0, dst, pitch, 0, S(103));
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(18), text_get("VASSAL_TIT1"), g_pen_white, 1);
	text_at(&g_f24, g_have_f24, dst, pitch, IW_X(112), IW_Y(36), text_get("VASSAL_TIT2"), g_pen_white, 1);
	lh = g_f14.cell;
	for (i = 0; i < st->nvas && (i + 1) * lh <= S(179); i++)
		text_at(&g_f14, g_have_f14, dst, pitch, IW_X(24), IW_Y(82) + i * lh, text_get(w->country[st->vas[i]].tag), i == st->vas_sel ? g_pen_gold : g_pen_white, 0);
	if (st->vas_sel >= 0 && st->vas_sel < st->nvas) snprintf(s, sizeof s, text_get("VASSAL_CURRENT"), text_get(w->country[st->vas[st->vas_sel]].tag));
	else snprintf(s, sizeof s, "%s", text_get("VASSAL_NONE"));
	text_at(&g_fside, g_have_fside, dst, pitch, IW_X(112), IW_Y(296), s, g_pen_white, 1);
	ev_load();
	text_btn_c(dst, pitch, IW_X(60), IW_Y(319), text_get("BACK"));
	if (st->vas_sel >= 0) text_btn_c(dst, pitch, IW_X(168), IW_Y(319), text_get("VASSAL_GRANT"));
}

int ui_vassal_hit(int x, int y)
{
	int lh = g_f14.cell;
	if (!g_vas_on) return -1;
	if (y >= IW_Y(315) && y < IW_Y(315) + S(40)) return x < IW_X(112) ? 101 : 100;
	if (x >= IW_X(20) && x < IW_X(180) && y >= IW_Y(82) && y < IW_Y(82) + S(179) && lh > 0) {
		int r = (y - IW_Y(82)) / lh;
		if (r < g_vas_n) return r;
	}
	return -1;
}

int ui_unload_hit(int x, int y) { return g_unload_shown && x >= IW_X(40) && x < IW_X(184) && y >= IW_Y(266) && y < IW_Y(266 + 40); }

/* ------------------------------------------------- in-game menu (SPEC_UI §9) */
/* setupscreen_bg 352x450 centred (NIEPEWNE), btn_frontend_big buttons centred on the panel (spec);
 * our rows: RESUME, Save Game, Load Game, EXIT. */
static Sprite g_mbg, g_bbig, g_bbig_sh;
static int g_mx, g_my;
static const char *menu_key[5] = { "RESUME_BUTTON", "SAVEGAME", "LOADGAME", "FEMAINBTN_EXIT", NULL };   /* the fifth: message settings (ours) */

void ui_menu_draw(unsigned char *dst, int pitch, int hover, int has_save)
{
	int i;
	if (!g_mbg.w) {
		load(&g_mbg, g_gfx, "gfx/settings/setupscreen_bg.bmp", g_inv, 1);
		load(&g_bbig, g_gfx, "gfx/interface/frontend/btn_frontend_big.bmp", g_inv, 4);
		if (!g_bbig_shm.lv) shmask_load(&g_bbig_shm, "gfx/interface/frontend/btn_frontend_big_shadow.bmp");
	}
	g_mx = (g_sw - g_mbg.w) / 2; g_my = (g_sh - g_mbg.h) / 2;
	put(&g_mbg, 0, dst, pitch, g_mx, g_my);
	for (i = 0; i < 5; i++) {
		int x = g_mx + (g_mbg.w - g_bbig.w) / 2, y = g_my + S(i == 4 ? 120 + 60 * 3 : 120 + 60 * (i == 3 ? 4 : i)), fr = i == 2 && !has_save ? 3 : hover == i ? 1 : 0;   /* Messages above Exit */
		shade_tables(g_mappal);
		shmask_draw(&g_bbig_shm, dst, pitch, x, y);
		put(&g_bbig, fr, dst, pitch, x, y);
		text_at(&g_fside, g_have_fside, dst, pitch, x + g_bbig.w / 2, y + (g_bbig.h - g_fside.cell) / 2, menu_key[i] ? text_get(menu_key[i]) : "Messages", fr == 3 ? nearest(g_mappal, 120, 120, 120) : g_pen_white, 1);   /* disabled: grey, still readable */
	}
}

int ui_menu_hit(int x, int y)
{
	int i;
	for (i = 0; i < 5; i++) {
		int bx = g_mx + (g_mbg.w - g_bbig.w) / 2, by = g_my + S(i == 4 ? 120 + 60 * 3 : 120 + 60 * (i == 3 ? 4 : i));
		if (x >= bx && x < bx + g_bbig.w && y >= by && y < by + g_bbig.h) return i;
	}
	return -1;
}

static Sprite g_kryss2, g_pil2[2];
#define MS_Y0 100
#define MS_DY 20
#define MS_BX 222                                  /* first box, then +28 */
void ui_msgset_draw(unsigned char *dst, int pitch, const char *const *titles, const unsigned char *bits, int n, int first, int hover)
{
	static const char *hk[3] = { "Box", "Log", "Pause" };   /* (the exe's column headers: NIEZNANE) */
	int i, k;
	ui_menu_draw(dst, pitch, -1, 1);
	put(&g_mbg, 0, dst, pitch, g_mx, g_my);
	if (!g_kryss2.w) {
		load(&g_kryss2, g_gfx, "gfx/interface/btn_kryssruta_small.bmp", g_inv, 4);
		load(&g_pil2[0], g_gfx, "gfx/interface/reorganisation/btn_pil_left.bmp", g_inv, 4); load(&g_pil2[1], g_gfx, "gfx/interface/reorganisation/btn_pil_right.bmp", g_inv, 4);
	}
	text_at(&g_fbar, g_have_fbar, dst, pitch, g_mx + g_mbg.w / 2, g_my + S(48), "Messages", g_pen_gold, 1);
	for (k = 0; k < 3; k++) text_at(&g_fsmall, g_have_fsmall, dst, pitch, g_mx + S(MS_BX + 28 * k + 8), g_my + S(MS_Y0 - 18), hk[k], g_pen_gold, 1);
	for (i = 0; i < UI_MSGSET_ROWS && first + i < n; i++) {
		int y = g_my + S(MS_Y0 + MS_DY * i), r = first + i;
		char t[48];
		snprintf(t, sizeof t, "%s", titles[r]);
		while (g_have_fsmall && t[0] && font_width(&g_fsmall, t) > S(MS_BX - 44)) t[strlen(t) - 1] = 0;
		text_at(&g_fsmall, g_have_fsmall, dst, pitch, g_mx + S(40), y + S(2), t, hover / 4 == r ? g_pen_orange : g_pen_white, 0);
		for (k = 0; k < 3; k++) put(&g_kryss2, (bits[r] >> k) & 1, dst, pitch, g_mx + S(MS_BX + 28 * k), y);
	}
	put(&g_pil2[0], first > 0 ? 0 : 3, dst, pitch, g_mx + S(120), g_my + S(MS_Y0 + MS_DY * UI_MSGSET_ROWS + 2));
	put(&g_pil2[1], first + UI_MSGSET_ROWS < n ? 0 : 3, dst, pitch, g_mx + S(208), g_my + S(MS_Y0 + MS_DY * UI_MSGSET_ROWS + 2));
	{	int x = g_mx + (g_mbg.w - g_bbig.w) / 2, y = g_my + g_mbg.h - S(80);
		shade_tables(g_mappal);
		shmask_draw(&g_bbig_shm, dst, pitch, x, y);
		put(&g_bbig, hover == 100 ? 1 : 0, dst, pitch, x, y);
		text_at(&g_fside, g_have_fside, dst, pitch, x + g_bbig.w / 2, y + (g_bbig.h - g_fside.cell) / 2, text_get("FEBTN_BACK"), g_pen_white, 1); }
}

int ui_msgset_hit(int x, int y, int n, int first)
{
	int i, k, bx = g_mx + (g_mbg.w - g_bbig.w) / 2, by = g_my + g_mbg.h - S(80), ay = g_my + S(MS_Y0 + MS_DY * UI_MSGSET_ROWS + 2);
	if (x >= bx && x < bx + g_bbig.w && y >= by && y < by + g_bbig.h) return 1000;
	if (y >= ay && y < ay + g_pil2[0].h) {
		if (x >= g_mx + S(120) && x < g_mx + S(120) + g_pil2[0].w) return 1001;
		if (x >= g_mx + S(208) && x < g_mx + S(208) + g_pil2[1].w) return 1002;
	}
	for (i = 0; i < UI_MSGSET_ROWS && first + i < n; i++) {
		int ry = g_my + S(MS_Y0 + MS_DY * i);
		if (y < ry || y >= ry + S(MS_DY)) continue;
		for (k = 0; k < 3; k++) if (x >= g_mx + S(MS_BX + 28 * k) && x < g_mx + S(MS_BX + 28 * k + 18)) return (first + i) * 4 + k + 1;
		if (x >= g_mx + S(36) && x < g_mx + S(MS_BX)) return (first + i) * 4;
	}
	return -1;
}

#define SLOT_Y0 96
#define SLOT_DY 26
void ui_slots_draw(unsigned char *dst, int pitch, const char *title, const char *const *rows, const int *state, int n, int hover)
{
	int i;
	ui_menu_draw(dst, pitch, -1, 1);                    /* the panel and its sprites */
	put(&g_mbg, 0, dst, pitch, g_mx, g_my);             /* ... without the four buttons */
	text_at(&g_fbar, g_have_fbar, dst, pitch, g_mx + g_mbg.w / 2, g_my + S(56), text_get(title), g_pen_gold, 1);
	for (i = 0; i < n; i++)
		text_at(&g_fside, g_have_fside, dst, pitch, g_mx + S(56), g_my + S(SLOT_Y0 + SLOT_DY * i), rows[i],
		        !state[i] ? nearest(g_mappal, 0x80, 0x80, 0x80) : hover == i ? g_pen_orange : g_pen_white, 0);
	{	int x = g_mx + (g_mbg.w - g_bbig.w) / 2, y = g_my + g_mbg.h - S(80);
		shade_tables(g_mappal);
		shmask_draw(&g_bbig_shm, dst, pitch, x, y);
		put(&g_bbig, hover == 100 ? 1 : 0, dst, pitch, x, y);
		text_at(&g_fside, g_have_fside, dst, pitch, x + g_bbig.w / 2, y + (g_bbig.h - g_fside.cell) / 2, text_get("FEBTN_BACK"), g_pen_white, 1); }
}

int ui_slots_hit(int x, int y, int n)
{
	int i, bx = g_mx + (g_mbg.w - g_bbig.w) / 2, by = g_my + g_mbg.h - S(80);
	if (x >= bx && x < bx + g_bbig.w && y >= by && y < by + g_bbig.h) return 100;
	for (i = 0; i < n; i++)
		if (x >= g_mx + S(40) && x < g_mx + g_mbg.w - S(40) && y >= g_my + S(SLOT_Y0 + SLOT_DY * i) - S(4) && y < g_my + S(SLOT_Y0 + SLOT_DY * (i + 1)) - S(4)) return i;
	return -1;
}

/* the windows' sprites are loaded on first use; ui_free releases them too (a resolution change
 * must not reuse the other size's sprites) */
static void ui_free_lazy(void)
{
	Sprite *all[] = { &g_track, &g_knob, &g_evbox, &g_tb[0][0], &g_tb[0][1], &g_tb[0][2], &g_tb[1][0], &g_tb[1][1], &g_tb[1][2],
	                  &g_tb[2][0], &g_tb[2][1], &g_tb[2][2], &g_pgl, &g_pgr, &g_pgbox, &g_iwt, &g_iwd, &g_minus, &g_plus,
	                  &g_fbg, &g_fstart, &g_fbtn, &g_fbtn_sh, &g_plate, &g_pl, &g_pr, &g_iwa, &g_iwn,
	                  &g_utype[0][0], &g_utype[0][1], &g_utype[0][2], &g_utype[1][0], &g_utype[1][1], &g_utype[1][2],
	                  &g_rank, &g_stat[0], &g_stat[1], &g_stat[2], &g_stat[3], &g_attr, &g_weight, &g_shipload,
	                  &g_nameframe, &g_numframe, &g_mbar, &g_mstat, &g_mbg, &g_bbig, &g_bbig_sh,
	                  &g_iwb, &g_iwtech, &g_iwc, &g_religions, &g_relbtn, &g_icon3[0], &g_icon3[1], &g_icon3[2],
	                  &g_iwh, &g_iwsea, &g_sea_pic[0], &g_sea_pic[1], &g_sea_pic[2], &g_cot, &g_anim_value,
	                  &g_iwdrlw, &g_iwdraw, &g_dipconf, &g_radio, &g_iwcot, &g_iwcol, &g_iwmis, &g_cotlvl[0], &g_cotlvl[1],
	                  &g_cotlvl[2], &g_cotlvl[3], &g_cotlvl[4], &g_cotlvl[5], &g_merch, &g_native, &g_mstrip[0], &g_mstrip[1],
	                  &g_iwml,
	                  &g_eyr_top, &g_eyr_bottom, &g_eyr_right, &g_eyr_page[0], &g_eyr_page[1], &g_eyr_page[2], &g_eyr_page[3],
	                  &g_eyr_bsmall, &g_eyr_blarge, &g_eyr_frame, &g_eyr_back, &g_eyr_fwd, &g_eyr_exit, &g_eyr_ch[0], &g_eyr_ch[1],
	                  &g_eyr_ch[2], &g_eyr_ch[3], &g_eyr_ch[4], &g_eyr_ch[5], &g_eyr_ch[6], &g_eyr_vline, &g_eyr_hline };
	unsigned i;
	for (i = 0; i < sizeof all / sizeof all[0]; i++) { sprite_free(all[i]); memset(all[i], 0, sizeof *all[i]); }
	g_ev_ok = 0; g_utype_tech = -1;
	if (g_have_f14 > 0) font_free(&g_f14);
	if (g_have_f24 > 0) font_free(&g_f24);
	if (g_have_arial > 0) font_free(&g_arial);
	if (g_have_f18 > 0) font_free(&g_f18);
	g_have_f18 = -1;
	g_have_f14 = g_have_f24 = g_have_arial = -1;
	ui_front_free(); g_have_fpal = -1;
	shmask_free(&g_fbtn_shm); shmask_free(&g_bbig_shm); shmask_free(&g_fbig_shm); shmask_free(&g_flare_m); g_shade_pal = NULL; g_light_pal = NULL;
	for (i = 0; i < NCACHE; i++) if (g_cache[i].state > 0) sprite_free(&g_cache[i].s);
	memset(g_cache, 0, sizeof g_cache);
}

/* ------------------------------------------------ multiply shadows (frontend *_shadow.bmp) */
/* The *_shadow bitmaps are grey masks: 0 = no shadow, otherwise the background is multiplied by
 * value/255 (64 inside, rising to ~217 at the soft edge). Drawn with four darkening tables built
 * from the palette in use; the mask is scaled with nearest neighbour at 640x480. */

static unsigned char g_shade_tab[4][256];


static void shade_tables(const unsigned char *pal)
{
	static const int mul[4] = { 64, 110, 160, 215 };
	int t, i;
	if (g_shade_pal == pal) return;
	g_shade_pal = pal;
	for (t = 0; t < 4; t++)
		for (i = 0; i < 256; i++)
			g_shade_tab[t][i] = (unsigned char)nearest(pal, pal[i * 3] * mul[t] / 255, pal[i * 3 + 1] * mul[t] / 255, pal[i * 3 + 2] * mul[t] / 255);
}

static int shmask_load(ShMask *m, const char *path)
{
	unsigned long sz;
	unsigned char *raw = pak_load(g_gfx, path, &sz);
	EuiImage im;
	int x, y;
	memset(m, 0, sizeof *m);
	if (!raw) return -1;
	if (eui_decode(raw, sz, &im)) { free(raw); return -1; }
	free(raw);
	m->w = S(im.w); m->h = S(im.h);
	if (!(m->lv = malloc((size_t)m->w * m->h + 1))) { eui_free(&im); return -1; }
	for (y = 0; y < m->h; y++)
		for (x = 0; x < m->w; x++) {
			int sx = x * im.w / m->w, sy = y * im.h / m->h, v;
			if (im.format == 1) v = im.pal[im.pix[sy * im.w + sx] * 3];                      /* grey: R */
			else { const unsigned char *p = im.pix + 2 * ((long)sy * im.w + sx); v = (p[0] & 0xF8); }   /* RGB565 red */
			m->lv[y * m->w + x] = (unsigned char)v;
		}
	eui_free(&im);
	return 0;
}

static void shmask_draw(const ShMask *m, unsigned char *dst, int pitch, int x0, int y0)
{
	int x, y;
	for (y = 0; y < m->h; y++) {
		int yy = y0 + y;
		if (yy < 0 || yy >= g_sh) continue;
		for (x = 0; x < m->w; x++) {
			int xx = x0 + x, v = m->lv[y * m->w + x], t;
			if (!v || xx < 0 || xx >= g_sw) continue;
			t = v < 87 ? 0 : v < 135 ? 1 : v < 188 ? 2 : v < 240 ? 3 : -1;
			if (t >= 0) dst[(long)yy * pitch + xx] = g_shade_tab[t][dst[(long)yy * pitch + xx]];
		}
	}
}

static void shmask_free(ShMask *m) { free(m->lv); memset(m, 0, sizeof *m); }

/* additive glow (shield_flare.bmp: black = nothing, brighter = more light), one frame of a strip */
static unsigned char g_light_tab[4][256];


static void light_tables(const unsigned char *pal)
{
	static const int add[4] = { 40, 80, 130, 190 };
	int t, i;
	if (g_light_pal == pal) return;
	g_light_pal = pal;
	for (t = 0; t < 4; t++)
		for (i = 0; i < 256; i++) {
			int r = pal[i * 3] + add[t] * (255 - pal[i * 3]) / 255, g = pal[i * 3 + 1] + add[t] * (255 - pal[i * 3 + 1]) / 255;
			int b = pal[i * 3 + 2] + add[t] * (255 - pal[i * 3 + 2]) / 300;
			g_light_tab[t][i] = (unsigned char)nearest(pal, r, g, b);
		}
}

static int lightmask_load(ShMask *m, const char *path, int frames, int idx)
{
	unsigned long sz;
	unsigned char *raw = pak_load(g_gfx, path, &sz);
	EuiImage im;
	int x, y, fw;
	memset(m, 0, sizeof *m);
	if (!raw) return -1;
	if (eui_decode(raw, sz, &im)) { free(raw); return -1; }
	free(raw);
	fw = im.w / frames;
	m->w = S(fw); m->h = S(im.h);
	if (!(m->lv = malloc((size_t)m->w * m->h + 1))) { eui_free(&im); return -1; }
	for (y = 0; y < m->h; y++)
		for (x = 0; x < m->w; x++) {
			int sx = idx * fw + x * fw / m->w, sy = y * im.h / m->h, v;
			if (im.format == 1) { const unsigned char *c = im.pal + im.pix[sy * im.w + sx] * 3; v = (c[0] + c[1] + c[2]) / 3; }
			else { const unsigned char *p = im.pix + 2 * ((long)sy * im.w + sx); unsigned w16 = (unsigned)p[0] << 8 | p[1];
			       v = (((w16 >> 11) & 31) * 8 + ((w16 >> 5) & 63) * 4 + (w16 & 31) * 8) / 3; }
			m->lv[y * m->w + x] = (unsigned char)v;
		}
	eui_free(&im);
	return 0;
}

static void lightmask_draw(const ShMask *m, unsigned char *dst, int pitch, int x0, int y0)
{
	int x, y;
	for (y = 0; y < m->h; y++) {
		int yy = y0 + y;
		if (yy < 0 || yy >= g_sh) continue;
		for (x = 0; x < m->w; x++) {
			int xx = x0 + x, v = m->lv[y * m->w + x], t;
			if (v < 24 || xx < 0 || xx >= g_sw) continue;
			t = v < 70 ? 0 : v < 130 ? 1 : v < 190 ? 2 : 3;
			dst[(long)yy * pitch + xx] = g_light_tab[t][dst[(long)yy * pitch + xx]];
		}
	}
}

/* ---- the CREDITS screen (docs/SPEC_FRONTEND_CREDITS.md; eu2.exe 0x57dbe0..0x57e060) ----
 * A 640x480 screen: back.bmp at (0,20), the text rows, bar.bmp at (0,0) and (0,460). db/credits.txt is
 * word-wrapped at 620 px into rows centred on x = 310; the rows move up 1 px per tick and loop. */
#define CR_NF 9
#define CR_SLOTS 40
typedef struct { unsigned char font; char *s; } CrRow;
static Font g_crf[CR_NF];
static int g_crf_ok[CR_NF];
static Sprite g_crback, g_crbar;
static CrRow *g_crrow;
static int g_ncr, g_crcap, g_crslot[CR_SLOTS], g_crleft, g_crnext;

static int cr_fidx(int d)                      /* #d -> one of the loaded faces */
{
	static const signed char m[CR_NF] = { 0, 1, 2, 3, 2, 3, 6, 7, 8 };
	return m[d];
}

static int cr_height(int r) { int f = r < 0 ? 2 : g_crrow[r].font; return g_crf_ok[f] ? g_crf[f].cell : 14; }

/* the escapes of the glyph renderer (0x40bb45): 0xA7 W / Y, 0xA7 or % + 3 digits (blue, green, red x 32) */
static int cr_escape(const char *s, int *pen)
{
	const unsigned char *u = (const unsigned char *)s;
	if (u[0] == 0xA7 && (u[1] == 'W' || u[1] == 'Y')) { if (pen) *pen = u[1] == 'W' ? g_fpen_white : g_fpen_gold; return 2; }
	if ((u[0] == 0xA7 || u[0] == '%') && u[1] >= '0' && u[1] <= '9' && u[2] >= '0' && u[2] <= '9' && u[3] >= '0' && u[3] <= '9') {
		if (pen) *pen = nearest(g_have_fpal > 0 ? g_fpal : g_mappal, (u[3] - '0') * 32, (u[2] - '0') * 32, (u[1] - '0') * 32);
		return 4;
	}
	return 0;
}

static void cr_strip(const char *s, char *out, int n)
{
	int k = 0, e;
	while (*s && k < n - 1) { if ((e = cr_escape(s, NULL)) != 0) { s += e; continue; } out[k++] = *s++; }
	out[k] = 0;
}

static int cr_width(const Font *f, const char *s) { char b[260]; cr_strip(s, b, sizeof b); return font_width(f, b); }

static void cr_add(int font, const char *s, int len)
{
	if (g_ncr == g_crcap) { g_crcap = g_crcap ? g_crcap * 2 : 256; g_crrow = (CrRow *)realloc(g_crrow, sizeof *g_crrow * (size_t)g_crcap); }
	g_crrow[g_ncr].font = (unsigned char)font;
	g_crrow[g_ncr].s = (char *)malloc((size_t)len + 1);
	memcpy(g_crrow[g_ncr].s, s, (size_t)len); g_crrow[g_ncr].s[len] = 0;
	g_ncr++;
}

/* the list's add-text (0x420830): words by spaces, a word that does not fit starts a new row */
static void cr_text(int font, const char *t)
{
	const Font *f = &g_crf[font];
	char row[260];
	int n = 0;
	while (*t) {
		const char *w = t;
		char cand[260];
		int wl;
		if (*t == '\r' || *t == '\n') { if (n) cr_add(font, row, n); n = 0; t++; continue; }
		while (*t && *t != ' ' && *t != '\r' && *t != '\n') t++;
		while (*t == ' ') t++;                                    /* the word keeps its trailing spaces */
		wl = (int)(t - w);
		if (n + wl >= (int)sizeof cand) wl = (int)sizeof cand - 1 - n;
		memcpy(cand, row, (size_t)n); memcpy(cand + n, w, (size_t)wl); cand[n + wl] = 0;
		if (n && g_crf_ok[font] && cr_width(f, cand) > 620) { cr_add(font, row, n); n = 0; memcpy(row, w, (size_t)wl); n = wl; }
		else { memcpy(row, cand, (size_t)(n + wl)); n += wl; }
	}
	if (n) cr_add(font, row, n);
}

int ui_credits_open(void)
{
	static const char *fn[CR_NF] = { "arial8", "arial10", "garamond14", "garamond14bold", "", "", "garamond18bold", "garamond20bold", "garamond24bold" };
	unsigned long sz;
	char *txt, *p, *e;
	int i, font = 2;
	front_inv();
	for (i = 0; i < CR_NF; i++) if (fn[i][0] && !g_crf_ok[i]) g_crf_ok[i] = !font_load(&g_crf[i], g_gfx, fn[i]);
	if (!g_crf_ok[2]) return -1;
	if (sprite_from_bmp(&g_crback, g_gfx, "gfx/interface/frontend/credits/back.bmp", front_inv(), 1, 1000)) memset(&g_crback, 0, sizeof g_crback);
	if (sprite_from_bmp(&g_crbar, g_gfx, "gfx/interface/frontend/credits/bar.bmp", front_inv(), 1, 1000)) memset(&g_crbar, 0, sizeof g_crbar);
	g_ncr = 0;
	if ((txt = (char *)pak_load(g_text, "db/credits.txt", &sz)) != NULL) {
		txt = (char *)realloc(txt, sz + 1); txt[sz] = 0;
		for (p = txt; *p; p = e) {
			char line[256];
			int n;
			const char *s = line;
			for (e = p; *e && *e != '\n'; e++) ;
			if (*e) e++;
			n = (int)(e - p); if (n > 254) n = 254;               /* fgets(buf, 255) */
			memcpy(line, p, (size_t)n); line[n] = 0;
			if (!strncmp(line, "-END", 4)) break;
			if (line[0] == '-') continue;
			if (line[0] == '#' && line[1] >= '0' && line[1] <= '8') { font = cr_fidx(line[1] - '0'); s = line + 2; if (*s == '-') continue; }
			if (!g_crf_ok[font]) font = 2;
			cr_text(font, s);
			font = 2;                                                /* back to garamond14 after a line */
		}
		free(txt);
	}
	for (i = 0; i < CR_SLOTS; i++) g_crslot[i] = -1;
	g_crleft = 0; g_crnext = 0;
	return g_ncr ? 0 : -1;
}

void ui_credits_step(void)                     /* TIMER_TICK_LOOP: 1 px up */
{
	if (g_crleft == 0) {
		memmove(g_crslot, g_crslot + 1, sizeof g_crslot[0] * (CR_SLOTS - 1));
		if (g_crnext >= 0 && g_crnext < g_ncr) g_crslot[CR_SLOTS - 1] = g_crnext; else { g_crslot[CR_SLOTS - 1] = -1; g_crnext = -1; }
		g_crleft = cr_height(g_crslot[CR_SLOTS - 1]);
		g_crnext++;
	}
	g_crleft--;
}

void ui_credits_draw(unsigned char *dst, int pitch)
{
	int ox = (g_sw - 640) / 2, oy = (g_sh - 480) / 2, y, i, black = nearest(g_have_fpal > 0 ? g_fpal : g_mappal, 0, 0, 0);
	unsigned char *o;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	o = dst + (long)oy * pitch + ox;
	for (y = 0; y < g_sh; y++) memset(dst + (long)y * pitch, black, (size_t)g_sw);
	if (g_crback.w) sprite_draw(&g_crback, 0, o, pitch, 640, 480, 0, 20);
	for (y = 480 + g_crleft, i = CR_SLOTS - 1; i >= 1; i--) {      /* S[0] is never drawn */
		int r = g_crslot[i];
		if (r >= 0 && g_crrow[r].s[0] != '!') {
			const Font *f = &g_crf[g_crrow[r].font];
			const char *s = g_crrow[r].s;
			int pen = g_fpen_gold, x = 310 - cr_width(f, s) / 2, e;
			char seg[260];
			while (*s) {                                           /* segments between the colour escapes */
				int k = 0;
				if ((e = cr_escape(s, &pen)) != 0) { s += e; continue; }
				while (*s && !cr_escape(s, NULL) && k < (int)sizeof seg - 1) seg[k++] = *s++;
				seg[k] = 0;
				if (y - f->cell < 480 && y > 0) x = font_draw(f, o, pitch, 640, 480, x, y - f->cell, seg, pen);   /* y = the row's bottom (NIEPEWNE: stacks the rows without overlap) */
				else x += font_width(f, seg);
			}
		}
		y -= cr_height(r);
	}
	if (g_crbar.w) { sprite_draw(&g_crbar, 0, o, pitch, 640, 480, 0, 0); sprite_draw(&g_crbar, 0, o, pitch, 640, 480, 0, 460); }
}

void ui_credits_close(void)
{
	int i;
	for (i = 0; i < g_ncr; i++) free(g_crrow[i].s);
	free(g_crrow); g_crrow = NULL; g_ncr = g_crcap = 0;
	sprite_free(&g_crback); sprite_free(&g_crbar);
	memset(&g_crback, 0, sizeof g_crback); memset(&g_crbar, 0, sizeof g_crbar);
}

/* ---- the tutorial's overlay (docs/SPEC_TUTORIAL.md 5): arrows and the AddMessage popups ---- */
static void tri_fill(unsigned char *dst, int pitch, int x0, int y0, int x1, int y1, int x2, int y2, int pen)
{
	int t, y;
	if (y1 < y0) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
	if (y2 < y0) { t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
	if (y2 < y1) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
	if (y2 == y0) return;
	for (y = y0; y <= y2; y++) {
		long xa = x0 + (long)(x2 - x0) * (y - y0) / (y2 - y0), xb;
		int a, b, x;
		if (y < y1) xb = y1 == y0 ? x1 : x0 + (long)(x1 - x0) * (y - y0) / (y1 - y0);
		else xb = y2 == y1 ? x1 : x1 + (long)(x2 - x1) * (y - y1) / (y2 - y1);
		a = (int)(xa < xb ? xa : xb); b = (int)(xa < xb ? xb : xa);
		if (y < 0 || y >= g_sh) continue;
		if (a < 0) a = 0;
		if (b >= g_sw) b = g_sw - 1;
		for (x = a; x <= b; x++) dst[(long)y * pitch + x] = (unsigned char)pen;
	}
}

static long isqrt_l(long v) { long r = 0, b = 1L << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

/* a chevron with its centre at (cx,cy) and its tip towards (tx,ty): tip (0,-24), tail corners (-16,24) /
 * (16,24), notch (0,12) in 800x600 pixels; shadow (+5,+5) in arrows.act colour 1, body in colour 3 */
static void tut_arrow(unsigned char *dst, int pitch, int cx, int cy, int tx, int ty)
{
	static const int px[4] = { 0, -16, 0, 16 }, py[4] = { -24, 24, 12, 24 };
	long dx = tx - cx, dy = ty - cy, len = isqrt_l(dx * dx + dy * dy), fx, fy;
	int X[4], Y[4], k, pass, pen;
	if (len <= 0) { dx = 0; dy = 1; len = 1; }
	fx = dx * 1024 / len; fy = dy * 1024 / len;              /* forward; right = (-fy, fx) */
	for (k = 0; k < 4; k++) {
		long lx = S(px[k]), ly = -(long)S(py[k]);              /* ly: along the forward direction */
		X[k] = cx + (int)((lx * -fy + ly * fx) / 1024);
		Y[k] = cy + (int)((lx * fx + ly * fy) / 1024);
	}
	for (pass = 0; pass < 2; pass++) {
		int o = pass ? 0 : S(5);
		const unsigned char *pal = g_mappal;
		pen = pass ? nearest(pal, 48, 192, 48) : nearest(pal, 0, 127, 0);
		tri_fill(dst, pitch, X[0] + o, Y[0] + o, X[1] + o, Y[1] + o, X[2] + o, Y[2] + o, pen);
		tri_fill(dst, pitch, X[0] + o, Y[0] + o, X[2] + o, Y[2] + o, X[3] + o, Y[3] + o, pen);
	}
}

void ui_tut_arrows(unsigned char *dst, int pitch, const int *cx, const int *cy, const int *tx, const int *ty, int n)
{
	int i;
	for (i = 0; i < n; i++) tut_arrow(dst, pitch, cx[i], cy[i], tx[i], ty[i]);
}

/* an 800x600 point of the scripts -> the arrow's centre and target on our screen (AddArrowsPos 0x568240):
 * centre (x+32, y-32), flipped to y+32 / x-32 near the top / right edge */
void ui_tut_pos(int x, int y, int *cx, int *cy, int *tx, int *ty)
{
	*tx = S(x); *ty = S(y);
	*cx = *tx + S(32); *cy = *ty - S(32);
	if (*cy - S(24) < 0) *cy = *ty + S(32);
	if (*cx > g_sw - 1 - S(16)) *cx = *tx - S(32);
}

/* AddMessage (0x567af0): a framed popup 384 wide, the title (garamond20bold, white) centred at y 32, the
 * text (gold) wrapped to 320 and centred below; no buttons. Placed at the default popup place, cascaded
 * (NIEPEWNE) */
/* the AddMessage boxes at the default popup place (x 240, y 48 at 800x600). Ours: the map keeps its original pixels on a
 * 640x480 screen, so a box there can hide the province an arrow shows (chapter 6: Northumberland) - then the boxes go to
 * the bottom of the map area instead (bottom = its lower edge). */
static int tut_box_h(const char *key)
{
	char k[64], lines[16][96];
	Font *ft = g_have_fbar ? &g_fbar : &g_fside, *fb = &g_fside;
	int nl, by;
	snprintf(k, sizeof k, "%s_DESC", key);
	nl = wrap(fb, text_get(k), S(320), lines, 16);
	by = S(32) + ft->cell + S(4);
	return ((nl * fb->cell + by + S(36)) & ~15) + S(16);
}
void ui_tut_messages(unsigned char *dst, int pitch, const char *const *keys, int n, const int *tx, const int *ty, int na, int bottom)
{
	int i, low = 0;
	for (i = 0; i < n && !low; i++) {
		int x = S(240) + i * S(16), y = S(48) + i * S(16), w = S(384), h = tut_box_h(keys[i]), a;
		if (x + w > g_sw) x = g_sw - w;
		for (a = 0; a < na; a++) if (tx[a] >= x && tx[a] < x + w && ty[a] >= y && ty[a] < y + h) low = 1;
	}
	for (i = 0; i < n; i++) {
		char k[64], lines[16][96];
		const char *title, *body;
		Font *ft = g_have_fbar ? &g_fbar : &g_fside, *fb = &g_fside;
		int x = S(240) + i * S(16), y = S(48) + i * S(16), w = S(384), nl, lh = fb->cell, by, h, j;
		if (!g_have_fside) return;
		snprintf(k, sizeof k, "%s_NAME", keys[i]); title = text_get(k);
		snprintf(k, sizeof k, "%s_DESC", keys[i]); body = text_get(k);
		nl = wrap(fb, body, S(320), lines, 16);
		by = S(32) + ft->cell + S(4);
		h = ((nl * lh + by + S(36)) & ~15) + S(16);
		if (x + w > g_sw) x = g_sw - w;
		if (low) y = bottom - h - S(8) - (n - 1 - i) * S(16);
		if (y + h > g_sh) y = g_sh - h;
		if (y < S(32)) y = S(32);
		win8_box(dst, pitch, x, y, w, h);
		text_at(ft, 1, dst, pitch, x + w / 2, y + S(24), title, g_pen_white, 1);
		for (j = 0; j < nl; j++) text_at(fb, 1, dst, pitch, x + w / 2, y + by + j * lh, lines[j], g_pen_gold, 1);
	}
}

/* buttons the tutorial switched off: frame 3 (0x56a9a0) and no clicks */
static unsigned long g_btn_off;
void ui_btn_disable(unsigned long mask) { g_btn_off = mask; }
int  ui_btn_disabled(int id) { return id >= 0 && id < 32 && (g_btn_off >> id) & 1; }

/* AddArrowsPid: the province's centre on screen -> the arrow; far outside the map view it is parked inside
 * it (about 32 px from the left / top, 48 from the right, 112 from the bottom; NIEPEWNE) still pointing at it */
void ui_tut_pid(int sx, int sy, int *cx, int *cy, int *tx, int *ty)
{
	int x0 = S(224) + S(32), x1 = g_sw - S(48), y0 = S(32), y1 = g_sh - S(112);
	*tx = sx; *ty = sy;
	*cx = sx + S(32); *cy = sy - S(32);
	if (*cy - S(24) < 0) *cy = sy + S(32);
	if (*cx > g_sw - 1 - S(16)) *cx = sx - S(32);
	if (*cx < x0) *cx = x0;
	if (*cx > x1) *cx = x1;
	if (*cy < y0) *cy = y0;
	if (*cy > y1) *cy = y1;
}

/* ---- the game's own mouse cursors (SPEC_UI 10, docs/SPEC_UNIT_UI.md 7): 56x41 RGB bitmaps, green
 * transparent, drawn in software over the screen like the original does (the Amiga's hardware pointer
 * has 3 colours); hot spot the gauntlet's finger tip (1,1) (NIEPEWNE) ---- */
static const char *const g_cur_file[UI_CUR_N] = { "attack_army", "attack_navy", "click", "enterport", "explore_army",
	"explore_navy", "illegal", "load", "move_army", "move_navy", "select", "send_colonist", "send_merchant", "unload", "small" };
static Sprite g_cur[2][UI_CUR_N];
static signed char g_cur_st[2][UI_CUR_N];
static int g_cur_kind = UI_CUR_SELECT, g_cur_front;

void ui_cursor_kind(int k) { if (k >= 0 && k < UI_CUR_N) g_cur_kind = k; }
int  ui_cursor_get(void) { return g_cur_kind; }
void ui_cursor_frontend(int on) { g_cur_front = on ? 1 : 0; }

static const Sprite *cur_sprite(void)
{
	int p = g_cur_front, k = g_cur_kind;
	if (!g_cur_st[p][k]) {
		char path[64];
		snprintf(path, sizeof path, "gfx/interface/cursor_%s.bmp", g_cur_file[k]);
		g_cur_st[p][k] = sprite_from_bmp(&g_cur[p][k], g_gfx, path, p ? front_inv() : g_inv, 1, g_scale) == 0 ? 1 : -1;
	}
	return g_cur_st[p][k] > 0 ? &g_cur[p][k] : NULL;
}

int ui_cursor_box(int mx, int my, int *x, int *y, int *w, int *h)
{
	const Sprite *s = cur_sprite();
	if (!s) return 0;
	*x = mx - 1; *y = my - 1; *w = s->w; *h = s->h;
	return 1;
}

/* The current cursor as a 3-colour Intuition pointer (the settings tool's "system pointer"): opaque pixels
 * -> 1..3 (pens 17-19), shadow and transparent -> 0. rgb9: when *have_rgb is 0 the three colours are made
 * here from this cursor (its pixels split by brightness into thirds, each third's average; *have_rgb = 1),
 * otherwise each pixel takes the nearest of the given three. Returns 0 and fills pix (w x h, w <= 64). */
int ui_cursor_pointer(unsigned char *pix, int *w, int *h, int *hx, int *hy, unsigned char *rgb9, int *have_rgb)
{
	const Sprite *s = cur_sprite();
	const unsigned char *pal = g_cur_front && g_have_fpal > 0 ? g_fpal : g_mappal;
	int sw, yy, k, i, cw;
	if (!s || !s->pens || !s->runs) return -1;
	sw = s->w * s->frames; cw = s->w > 64 ? 64 : s->w;
	memset(pix, 0, (size_t)cw * s->h);
	if (!*have_rgb) {                         /* brightness histogram -> thirds */
		long hist[766], acc[3][3], n[3], tot = 0, run = 0;
		int lim1 = 0, lim2 = 0, b;
		memset(hist, 0, sizeof hist); memset(acc, 0, sizeof acc); memset(n, 0, sizeof n);
		for (yy = 0; yy < s->h; yy++) {
			const unsigned short *rr = s->runs + s->rowoff[yy]; int cnt = *rr++;
			for (k = 0; k < cnt; k++, rr += 3) if (!rr[2]) for (i = rr[0]; i < rr[0] + rr[1]; i++) {
				const unsigned char *c = pal + s->pens[(long)yy * sw + i] * 3;
				hist[c[0] + c[1] + c[2]]++; tot++;
			}
		}
		for (b = 0; b < 766; b++) { run += hist[b]; if (!lim1 && run * 3 >= tot) lim1 = b; if (!lim2 && run * 3 >= tot * 2) lim2 = b; }
		for (yy = 0; yy < s->h; yy++) {
			const unsigned short *rr = s->runs + s->rowoff[yy]; int cnt = *rr++;
			for (k = 0; k < cnt; k++, rr += 3) if (!rr[2]) for (i = rr[0]; i < rr[0] + rr[1]; i++) {
				const unsigned char *c = pal + s->pens[(long)yy * sw + i] * 3;
				int l = c[0] + c[1] + c[2], t = l <= lim1 ? 0 : l <= lim2 ? 1 : 2;
				acc[t][0] += c[0]; acc[t][1] += c[1]; acc[t][2] += c[2]; n[t]++;
			}
		}
		for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) rgb9[i * 3 + k] = (unsigned char)(n[i] ? acc[i][k] / n[i] : 0);
		*have_rgb = 1;
	}
	for (yy = 0; yy < s->h; yy++) {
		const unsigned short *rr = s->runs + s->rowoff[yy]; int cnt = *rr++;
		for (k = 0; k < cnt; k++, rr += 3) if (!rr[2]) for (i = rr[0]; i < rr[0] + rr[1] && i < cw; i++) {
			const unsigned char *c = pal + s->pens[(long)yy * sw + i] * 3;
			long bd = 0x7fffffffL; int j, best = 0;
			for (j = 0; j < 3; j++) {
				long dr = c[0] - rgb9[j * 3], dg = c[1] - rgb9[j * 3 + 1], db = c[2] - rgb9[j * 3 + 2], d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
				if (d < bd) { bd = d; best = j; }
			}
			pix[yy * cw + i] = (unsigned char)(best + 1);
		}
	}
	*w = cw; *h = s->h; *hx = 1; *hy = 1;          /* drawn at (mx - 1, my - 1): the finger tip is (1,1) */
	return 0;
}

int ui_cursor_id(void) { return g_cur_front * UI_CUR_N + g_cur_kind; }

void ui_cursor_draw(unsigned char *dst, int pitch, int mx, int my)
{
	const Sprite *s = cur_sprite();
	if (s) sprite_draw(s, 0, dst, pitch, g_sw, g_sh, mx - 1 + s->ox, my - 1 + s->oy);
}


/* ------------------------------------------------ message windows (0x426610 / 0x426960 / 0x426af0)
 * 320 wide, the win16 frame (gfx/interface/win_*.bmp, 16x16 tiles); text column 160 wide centred
 * (x 80..240) from y 17: HEADER garamond14 gold, _1 garamond20bold white, _2/_4/_6 garamond14 gold,
 * _3/_5 garamond14bold white, each box lines * lineh + 5; the small shield at (12,14) and (254,14),
 * the emblem (64x70) at (8,52) and (250,52); buttons y = h - button - 16: one centred, or two at
 * w/2 -+ w/4. Height rounded up to 16 (NIEPEWNE: the exe rounds down and grows the text instead). */
static Sprite g_win16[9]; static int g_win16_ok;
static Sprite g_flarp[8]; static unsigned char g_flarp_st[8];
static Sprite g_mwshield; static char g_mwshield_tag[4]; static int g_mwshield_st;
static const char *const g_flarp_cat[8] = { "mon", "tut", "mis", "fin", "tra", "dip", "mil", "col" };
static void mw_res(const UiMsgWin *m)
{
	int i;
	ev_load();                                            /* the text button tiles */
	if (!g_win16_ok) {
		static const char *w16[9] = { "tl", "t", "tr", "cl", "c", "cr", "bl", "b", "br" };
		char p[64];
		for (i = 0; i < 9; i++) { snprintf(p, sizeof p, "gfx/interface/win_%s.bmp", w16[i]); load(&g_win16[i], g_gfx, p, g_inv, 1); }
		g_win16_ok = 1;
	}
	if (strcmp(g_mwshield_tag, m->tag)) {
		char p[64], *q;
		if (g_mwshield_st == 1) sprite_free(&g_mwshield);
		snprintf(p, sizeof p, "gfx/map/shields/small/smallshield_%s.bmp", m->tag);
		for (q = p; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
		load(&g_mwshield, g_gfx, p, g_inv, 4); g_mwshield_st = g_mwshield.w ? 1 : 2;
		snprintf(g_mwshield_tag, sizeof g_mwshield_tag, "%s", m->tag);
	}
	for (i = 0; i < 8; i++) if (!strcmp(m->cat, g_flarp_cat[i]) && !g_flarp_st[i]) {
		char p[64];
		snprintf(p, sizeof p, "gfx/map/shields/shield_flarp_%s.bmp", g_flarp_cat[i]);
		load(&g_flarp[i], g_gfx, p, g_inv, 1); g_flarp_st[i] = g_flarp[i].w ? 1 : 2;
	}
}
static Font *mw_font(int b, int *have, int *pen)
{
	iw_fonts();
	if (b == 1) { *have = g_have_fbar; *pen = g_pen_white; return &g_fbar; }
	if (b == 3 || b == 5) { *have = g_have_fside; *pen = g_pen_white; return &g_fside; }
	*have = g_have_f14; *pen = nearest(g_mappal, 255, 189, 0); return &g_f14;
}
#define MWL 12
void ui_msgwin_layout(UiMsgWin *m)
{
	char lines[MWL][96];
	int b, y = S(17), bh = g_tb[0][0].h ? g_tb[0][0].h : S(38), t = S(16) < 8 ? 8 : S(16);
	for (b = 0; b < 7; b++) {
		int have, pen, n;
		Font *f = mw_font(b, &have, &pen);
		if (!m->box[b][0] || !have) continue;
		n = wrap(f, m->box[b], S(160), lines, MWL);
		y += n * f->cell + S(5);
	}
	if (y < S(52) + S(70)) y = S(52) + S(70);             /* the emblem column */
	m->w = S(320);
	m->h = y + S(8) + bh + S(16);
	m->h = (m->h + t - 1) / t * t;
}
void ui_msgwin_draw(unsigned char *dst, int pitch, const UiMsgWin *m, int hover)
{
	char lines[MWL][96];
	int b, i, y, t, xx, yy, x = m->x, bh = g_tb[0][0].h ? g_tb[0][0].h : S(38), bw = S(120);
	mw_res(m);
	t = g_win16[0].w ? g_win16[0].w : 16;
	for (yy = m->y + t; yy < m->y + m->h - t; yy += t)
		for (xx = x + t; xx < x + m->w - t; xx += t) put(&g_win16[4], 0, dst, pitch, xx, yy);
	for (xx = x + t; xx < x + m->w - t; xx += t) { put(&g_win16[1], 0, dst, pitch, xx, m->y); put(&g_win16[7], 0, dst, pitch, xx, m->y + m->h - t); }
	for (yy = m->y + t; yy < m->y + m->h - t; yy += t) { put(&g_win16[3], 0, dst, pitch, x, yy); put(&g_win16[5], 0, dst, pitch, x + m->w - t, yy); }
	put(&g_win16[0], 0, dst, pitch, x, m->y); put(&g_win16[2], 0, dst, pitch, x + m->w - t, m->y);
	put(&g_win16[6], 0, dst, pitch, x, m->y + m->h - t); put(&g_win16[8], 0, dst, pitch, x + m->w - t, m->y + m->h - t);
	if (g_mwshield_st == 1) { put(&g_mwshield, 0, dst, pitch, x + S(12), m->y + S(14)); put(&g_mwshield, 0, dst, pitch, x + S(254), m->y + S(14)); }
	for (i = 0; i < 8; i++) if (!strcmp(m->cat, g_flarp_cat[i]) && g_flarp_st[i] == 1) {
		put(&g_flarp[i], 0, dst, pitch, x + S(8), m->y + S(52)); put(&g_flarp[i], 0, dst, pitch, x + S(250), m->y + S(52));
	}
	for (y = m->y + S(17), b = 0; b < 7; b++) {
		int have, pen, n;
		Font *f = mw_font(b, &have, &pen);
		if (!m->box[b][0] || !have) continue;
		n = wrap(f, m->box[b], S(160), lines, MWL);
		for (i = 0; i < n; i++, y += f->cell) text_at(f, 1, dst, pitch, x + S(160), y, lines[i], pen, 1);
		y += S(5);
	}
	for (i = 0; i < 2; i++) {
		int bx;
		if (!m->btn[i][0]) continue;
		bx = !m->btn[1][0] ? x + (m->w - bw) / 2 : x + m->w / 2 + (i ? m->w / 4 : -m->w / 4) - bw / 2;
		text_button(dst, pitch, bx, m->y + m->h - bh - S(16), bw, m->btn[i], hover == i ? 1 : 0);
	}
}
int ui_msgwin_hit(const UiMsgWin *m, int x, int y)
{
	int i, bh = g_tb[0][0].h ? g_tb[0][0].h : S(38), bw = S(120), by = m->y + m->h - bh - S(16);
	if (x < m->x || x >= m->x + m->w || y < m->y || y >= m->y + m->h) return -1;
	for (i = 0; i < 2; i++) {
		int bx;
		if (!m->btn[i][0]) continue;
		bx = !m->btn[1][0] ? m->x + (m->w - bw) / 2 : m->x + m->w / 2 + (i ? m->w / 4 : -m->w / 4) - bw / 2;
		if (x >= bx && x < bx + bw && y >= by && y < by + bh) return i;
	}
	return 2;
}

/* Message properties (0x575520): 300x155 win8 box at the click point, four choices (0x576840) + OK */
static int g_ms_x, g_ms_y;
void ui_msgprop_draw(unsigned char *dst, int pitch, int x, int y, const char *title, int cur, int hover)
{
	static const char *const k[4] = { "Off", "Log only", "Popup + log", "Popup + log + pause" };
	int i, w = S(300), h = S(155), lh, gold = nearest(g_mappal, 255, 189, 0), bh = g_tb[0][0].h ? g_tb[0][0].h : S(38);
	iw_fonts();
	if (x < S(224)) x = S(224);
	if (y < S(32)) y = S(32);
	if (x + w > g_sw) x = g_sw - w;
	if (y + h > g_sh) y = g_sh - h;
	g_ms_x = x; g_ms_y = y;
	win8_box(dst, pitch, x, y, w, h);
	text_at(&g_fside, g_have_fside, dst, pitch, x + w / 2, y + S(8), title, gold, 1);
	lh = S(18);
	for (i = 0; i < 4; i++)
		text_at(&g_f14, g_have_f14, dst, pitch, x + S(24), y + S(30) + i * lh, k[i], i == cur ? g_pen_white : hover == i ? g_pen_white : gold, 0),
		text_at(&g_f14, g_have_f14, dst, pitch, x + S(10), y + S(30) + i * lh, i == cur ? "*" : "", g_pen_white, 0);
	text_button(dst, pitch, x + (w - S(100)) / 2, y + h - bh - S(6), S(100), "OK", hover == 4 ? 1 : 0);
}
int ui_msgprop_hit(int x, int y)
{
	int i, w = S(300), h = S(155), lh = S(18), bh = g_tb[0][0].h ? g_tb[0][0].h : S(38), bx = g_ms_x + (w - S(100)) / 2;
	if (x < g_ms_x || x >= g_ms_x + w || y < g_ms_y || y >= g_ms_y + h) return -1;
	for (i = 0; i < 4; i++) if (y >= g_ms_y + S(30) + i * lh && y < g_ms_y + S(30) + (i + 1) * lh) return i;
	if (x >= bx && x < bx + S(100) && y >= g_ms_y + h - bh - S(6)) return 4;
	return 5;
}
static int g_mwc_x = -1, g_mwc_y;
void ui_msgwin_place(UiMsgWin *m)
{
	if (g_mwc_x < 0 || g_mwc_x > S(400)) { g_mwc_x = S(200); g_mwc_y = S(100); }
	m->x = g_mwc_x; m->y = g_mwc_y;
	if (m->y + m->h > S(500)) m->y = S(500) - m->h;
	if (m->x + m->w > g_sw) m->x = g_sw - m->w;
	if (m->y + m->h > g_sh) m->y = g_sh - m->h;
	if (m->x < 0) m->x = 0;
	if (m->y < 0) m->y = 0;
	g_mwc_x += S(8); g_mwc_y += S(8);
}
