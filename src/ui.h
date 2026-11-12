/* ui.h - the main game screen of EU2 rebuilt from its own bitmaps, layout from docs/SPEC_UI.md §2:
 * left sidebar (224 px at 800x600), floating top bar, map viewport to the right. At 640x480 the
 * layout and bitmaps are scaled by 0.8 (nearest for now; a Lanczos set from the converter later).
 * Portable C. */
#ifndef EU2_UI_H
#define EU2_UI_H
#include "pak.h"
#include "world.h"
#include "economy.h"
#include "ledger.h"
#include "military.h"
#include "options.h"

/* the purchase window (iw_purchasearmy / iw_purchasenavy, windows4 1): available, price, ordered per column */
typedef struct { int naval, prov; long avail[3], cost[3], ordered[3], total, bld_cost; int can_ok, bld_ok, merc_shown, merc_ok; char merc_tip[200]; } UiPurchase;
enum { UPU_NONE = 0, UPU_OK = 1, UPU_CANCEL = 2, UPU_BUILD = 3, UPU_MERC = 4, UPU_UP = 10, UPU_DOWN = 20 };
int  ui_purchase_hit(int x, int y);
/* the loans window (iw_loans, windows4 6.1): 1..3 up (amount, rate, months), 11..13 down, 20 send, 21 back, 0 none */
int  ui_loans_hit(int x, int y);
/* the religion window (iw_religion, windows4 2): the radio choice, the shown tolerance rows */
typedef struct { int state, sel, ntarget, target[4], nshown, shown[11], can_convert, can_defend; } UiReligion;
enum { URL_NONE = -1, URL_BACK = 1, URL_CONVERT, URL_DEFEND, URL_RADIO = 10, URL_SLIDER = 30 };   /* slider: URL_SLIDER + row (0 state), value in *v */
int  ui_religion_hit(int x, int y, int *v);

/* the peace window (iw_peace, SPEC_UI §4.12): page 0 tribute direction, 1 the offer, 2 annex */
typedef struct {
	int page, demand, score, us, enemy;      /* demand: 1 Demand Tribute, 0 Offer Tribute; score -100..100 from our side */
	const short *cand; const unsigned char *picked; const short *recv; int ncand, scroll;   /* candidate provinces, chosen flags, receiver */
	int value; long gold; int vassal, religion, access, can_send;
	const char *msg;                         /* page 2 text */
	const short *side; int nside;            /* receivers: the countries of the receiving side */
	int us_alone, them_alone, can_toggle;    /* separate peace: PONATION instead of POLEADER; the enemy shield toggles */
} UiPeace;
enum { UP_NONE = -1, UP_DEMAND, UP_NOTRIB, UP_OFFER, UP_CANCEL, UP_SEND, UP_PLUS, UP_MINUS, UP_VASSAL, UP_RELIGION, UP_ACCESS, UP_MORE, UP_ANNEX, UP_THEM, UP_ROW = 100 };
int  ui_peace_hit(int x, int y, const UiPeace *p);
int  ui_peace_rows(void);                  /* province rows the list shows */
/* the army / fleet window: 1 split, 2 merge, 3 disband, 4 previous, 5 next unit in the place, 0 none */
int  ui_unit_cmd_hit(int x, int y);
/* the siege window's buttons (when the player besieges): 1 assault, 2 detach siege force, 3 detach cover force, 0 none */
int  ui_siege_hit(int x, int y);
/* the right-click menu on a province (RCDM_* texts; its look is NIEZNANE: ours, a dark box with a gold rim
 * at the mouse); hit = row or -1 */
/* the right-click menu: flags per row, PM_GREY (disabled, grey, not clickable) / PM_SEP (a separator line) */
#define PM_GREY 1
#define PM_SEP  2
void ui_popmenu_draw(unsigned char *dst, int pitch, int x, int y, const char *const *labels, const unsigned char *flags, int n, int hover);
int  ui_popmenu_hit(int x, int y, int n);   /* the row under the mouse (separators and greyed rows: -1) */
int  ui_popmenu_inside(int x, int y);
/* rollover help of the battle / peace windows as last drawn: zone id (>= 0) and its texts, -1 none */
int  ui_window_tip(int x, int y, char *t1, int n1, char *t2, int n2);
/* the message log over the bottom of the map (5 lines; the exe's 0x577080 - its place on screen is NIEZNANE) */
void ui_log_draw(unsigned char *dst, int pitch, int x, int y, int w, const char *const *lines, int n);
int  ui_log_height(void);   /* the panel's height; the map takes no clicks under its top (0x577610) */
/* a unit stack's strength on the map (0x49035a): the number in white over a box darkened by half, its top
 * left at (x, y) of the map window dst (sw x sh) */
void ui_unit_count(unsigned char *dst, int pitch, int sw, int sh, int x, int y, long n);

/* one row of the diplomacy action panel: id = DA_* (diplo.h) or -1 for a separator; state 1 greyed, 2 clickable */
typedef struct { int id; char label[64]; int state; } UiDipAct;

typedef struct {
	const char *date;          /* already formatted */
	int paused;
	int player;                /* country index or -1 */
	int selected;              /* province id or -1 */
	long merchants, colonists, diplomats, missionaries, stability, manpower, treasury;
	int infowin;               /* sidebar info window: UI_IW_PROVINCE (selection) or UI_IW_BUDGET */
	const EconBudget *budget;  /* for UI_IW_BUDGET */
	int wartax_state;          /* UI_IW_BUDGET: econ_war_tax_state (0 = the button is enabled) */
	int loan_ok;               /* UI_IW_BUDGET: a bank loan can be taken */
	long loan_size;            /* ducats */
	unsigned char boost_ok[5]; /* UI_IW_BUDGET: the one-time increase buttons enabled (rows land..production) */
	const char *domestic_note; /* for UI_IW_DOMESTIC: when the next change is possible */
	int army; long army_maxmorale, army_days;   /* for UI_IW_ARMY: land unit index, its full morale x1000, days to the next step */
	int mapmode;               /* MM_* (mapmode.h): the mode button drawn pressed */
	int fleet;                 /* UI_IW_NAVY: naval unit index */
	const MilBattleView *battle;   /* UI_IW_BATTLE: the land battle or siege of the selected army */
	const UiPeace *peace;          /* UI_IW_PEACE */
	const UiPurchase *purchase;    /* UI_IW_PURCHASE */
	const UiReligion *religion;    /* UI_IW_RELIGION */
	const EconGoods *goods; int goods_id;   /* UI_IW_RESOURCE: the good of the selected province */
	int loan_target; long loan_amount; int loan_rate, loan_months, loan_can;   /* UI_IW_LOANS (ducats, %, months) */
	const short *vas; int nvas, vas_sel;   /* UI_IW_VASSAL: candidates, the chosen row (-1); UI_IW_COUNTRY: nvas > 0 shows Create Vassal */
	int can_split, can_merge, nstack;
	int can_kill_natives;      /* UI_IW_ARMY: BUTTON_KILL_NATIVES applies (stationary, not in a battle, natives there) */   /* UI_IW_ARMY / NAVY: the commands that apply; own units in the place (arrows when > 1) */
	const char *army_leader;   /* UI_IW_ARMY / NAVY: its leader's name (the built-in one when none is attached) */
	int dl_move, dl_fire, dl_shock, dl_siege;   /* the built-in leader's values (l_id 0) */
	long interest_m;           /* UI_IW_FINANCE: the loans' monthly interest */
	long load_m, cap_m;        /* UI_IW_NAVY: carried weight / capacity (x1000) */
	int unload_mode;           /* waiting for the province to unload into */
	int env_prov, env_cot;     /* UI_IW_COT / _COLONIST / _MISSIONARY: the province, its CoT */
	long view_cx, view_cy;     /* the map view (full-resolution centre), its zoom 1..3 and window size: */
	int view_zoom, view_w, view_h;   /* the white frame on the minimap (view_zoom 0: none) */
	int env_can[3];            /* ENV_* (envoys.h) for merchant, colonist, missionary: 0 = can send */
	int env_chance, env_months; long env_cost;
	int tr_can, tr_chance; long tr_cost;       /* the trader column of the colonist window */
	const char *mis_cur[11], *mis_av[11]; int nmis_cur, nmis_av;   /* UI_IW_MISSIONS: titles of the current / available missions */
	const EconReport *report;  /* UI_IW_FINANCE: last month's figures (with budget = the pool and its split) */
	long inflation_u;
	int tech_level; long units[3]; long maint_month; int maint_level;   /* UI_IW_LAND/NAVALTECH */
	long support;              /* UI_IW_LANDTECH: the support limit in men (-1 none) */
	int rank, religion_frame;  /* UI_IW_COUNTRY */
	long supply, attrition; int supply_col;      /* foreign province: for the player; colour 2 green 1 yellow 0 red */
	int revolt;                                  /* its revolt risk (§13.1) */
	const char *cot_name;                        /* its centre of trade (province name) or NULL */
	short nat[10]; int nnat, pirates;            /* sea zone: owners of the coasts, pirate ships */
	int priv_state;                              /* sea zone: mil_privateer_state (0 = Commission Privateers enabled) */
	int dip_view, dip_sel, dip_target, dip_hover, dip_badboy_tier;   /* UI_IW_DIPLO: 0 alliances / 1 relations view */
	long dip_rel;                                /* the target's relation towards us */
	const UiDipAct *dip_act; int ndip_act;       /* the action panel (dip_target >= 0) */
	const char *dip_confirm;                     /* the confirm popup's message, NULL = closed */
	short dip_list[16]; long dip_listv[16]; int ndip_list;   /* relations view rows */
	const char *dip_lines[8]; int ndip_lines;    /* alliances view text (with the 0xA7 colour codes) */
} UiState;

enum { UI_NONE = -1, UI_ZOOM_IN, UI_ZOOM_OUT, UI_PAUSE, UI_MENU, UI_MAPMODE_NORMAL, UI_MAPMODE_POLITICAL,
       UI_MAPMODE_RESOURCE, UI_DIPLOMAT, UI_TREASURY, UI_DOMESTIC, UI_RECRUIT, UI_OTHER,
       UI_VIEW_LAND, UI_VIEW_NAVAL, UI_FINANCIAL, UI_VP, UI_MISSIONARY, UI_MERCHANT, UI_COLONIST, UI_MISSIONS, UI_LEDGER };
enum { UI_IW_PROVINCE, UI_IW_BUDGET, UI_IW_DOMESTIC, UI_IW_ARMY, UI_IW_FINANCE, UI_IW_LANDTECH, UI_IW_NAVALTECH, UI_IW_COUNTRY, UI_IW_DIPLO, UI_IW_NAVY, UI_IW_COT, UI_IW_COLONIST, UI_IW_MISSIONARY, UI_IW_MISSIONS, UI_IW_BATTLE, UI_IW_PEACE, UI_IW_PURCHASE, UI_IW_RELIGION, UI_IW_RESOURCE, UI_IW_LOANS, UI_IW_VASSAL };

int  ui_init(Pak *gfx, Pak *text, const unsigned char *pal, const unsigned char *inv, int screen_w, int screen_h);
void ui_free(void);
void ui_set_font_dir(const char *dir);          /* where the 640x480 faces (*.euf) are; default PROGDIR:fonts/ */
/* the map viewport (screen coordinates) */
void ui_map_rect(int *x, int *y, int *w, int *h);
int  ui_sidebar_h(void);                    /* the sidebar's height (S(600)): below it the map shows on taller screens */
/* draws sidebar + top bar over an already drawn map */
void ui_draw(unsigned char *dst, int pitch, const World *w, const UiState *st);
/* which sidebar/top-bar button is at a screen position (UI_NONE if none) */
int  ui_hit(int x, int y);
int  ui_tooltip_draw(unsigned char *dst, int pitch, int x, int y, int delayed);   /* button rollover help, 0 = none here */
int  ui_bar_item(int x, int y);               /* top bar value under (x,y): 0 merchants .. 6 treasury, -1 none */
int  ui_tip_text(unsigned char *dst, int pitch, int x, int y, const char *t1, const char *t2);   /* a help box anywhere */
int  ui_minimap_hit(int x, int y, long *fx, long *fy);   /* a click on the minimap: the full-map point under it */
/* province window (§3): the building under (x,y) of the last drawn PIW */
enum { PIW_NONE = -1, PIW_BARRACKS, PIW_PORT, PIW_FORT, PIW_CHURCH, PIW_BANK, PIW_EMBASSY, PIW_MERCHANT, PIW_RESOURCE };
int  ui_piw_hit(int x, int y);
/* the build-manufactory sub-panel (SPEC_UI 3.3) over the province window: can[i] = button i buildable
 * (arts, weapons, naval, refinery, goods); hit: 0..4 a button, 5 CANCEL, -1 nothing */
void ui_manupanel_draw(unsigned char *dst, int pitch, const unsigned char can[5], int hover);
int  ui_manupanel_hit(int x, int y);

/* modal event window over the map (SPEC_UI §7): title/text already looked up in text.csv,
 * action[i] = button label or NULL; hover = action under the mouse or -1; page = text page.
 * Returns the number of text pages. */
int  ui_event_draw(unsigned char *dst, int pitch, const char *title, const char *date, const char *text,
                   const char *const action[4], int hover, int page);
enum { UI_EV_PREV = -2, UI_EV_NEXT = -3 };
int  ui_event_hit(int x, int y);
/* the event choice popup (windows4 3, 0x590b80): a win8 frame T + 64 wide (T 400 / 500 / 600 by the
 * description's length), title, description (gold), the info line, actions A-D stacked full width,
 * the player's small shield top right; centred on the screen (NIEPEWNE). Returns the pages; hit as
 * ui_event_hit (action index, UI_EV_PREV / NEXT, -1). */
int  ui_choice_draw(unsigned char *dst, int pitch, const char *title, const char *desc, const char *info,
                    const char *const action[4], const char *tag, int hover, int page);
int  ui_choice_hit(int x, int y);              /* action index, UI_EV_PREV/NEXT (page arrows) or -1 */
/* budget window slider under (x,y): returns the row (ECON_SL_*) and the value 0..1000 there, or -1 */
int  ui_budget_hit(int x, int y, int *value);
int  ui_budget_knob(int x, int y, const short *slider);   /* the knob under (x,y): its row (double click = lock), or -1 */
int  ui_budget_btn_hit(int x, int y);
int  ui_budget_boost_hit(int x, int y);     /* 0..4 = the one-time increase of row land / naval / stability / trade / production, -1 none */       /* 1 = Raise War Taxes (0x38), 2 = Take Bank Loan (0x37), 0 = none */
/* domestic window: slider k minus (returns 2k) or plus (2k+1) button under (x,y), or -1 */
int  ui_domestic_hit(int x, int y);
int  ui_tech_hit(int x, int y);               /* land / naval window: maintenance slider level (500..1000) or -1 */
int  ui_tech_drag(int x);            /* the same while the knob is dragged: x only, clamped */
int  ui_country_hit(int x, int y);            /* country window: 1 domestic (shield), 2 religion, 3 Create Vassal, 0 none */
/* the vassal window: row 0.., 100 Grant, 101 Back, -1 none */
int  ui_vassal_hit(int x, int y);
int  ui_iw_close_hit(int x, int y);           /* the CLOSE / BACK button of a sidebar window */
enum { UD_NONE = -1, UD_RADIO_ALL = 1, UD_RADIO_REL, UD_BACK, UD_GOTO, UD_OK, UD_CANCEL, UD_SORTREL, UD_SORTNAT, UD_PANEL, UD_ACT = 100 };
int  ui_dip_hit(int x, int y, const UiState *st);
int  ui_unload_hit(int x, int y);
int  ui_env_hit(int x, int y, int infowin);        /* envoy windows: 1 back, 2 send, 3 trader, 0 none */
int  ui_mis_hit(int x, int y, const UiState *st);  /* missions window: 100 + available row, 1 back, -1 none */
/* the Ledger (F6): a full-screen page; hit = exit / back / forward / chapter tab */
enum { LH_NONE = -1, LH_EXIT = 1, LH_BACK, LH_FWD, LH_CHAPTER = 10 };
void ui_ledger_draw(unsigned char *dst, int pitch, const World *w, const UiLedger *L);
int  ui_ledger_hit(int x, int y);                  /* the fleet window's Unload Troops button (shown only with troops aboard) */   /* diplomacy windows: UD_*, UD_ACT + row index of dip_act */

/* ------------------------------------------------------- frontend (SPEC_UI §8) */
/* Layout decoded from eu2.exe: main menu FUN_005848f0 (bg_start, five btn_frontend_big at
 * x = 400 - w/2, y = 320 + 40 i), single player FUN_00581230 (bg_frontend; list of scenarios and
 * save games at (36,110) 134x220; the scenario's featured powers as big shields at x = 246 + 64 i,
 * y = 27, FUN_0058ca80; bg_multiplayer at (230,366) with the text box (246,390) 354x161; buttons
 * INFO / VICTORY / OPTIONS / BACK / START at x = 642, y = 367 / 412 / 458 / 504 / 552); the other
 * countries on plate.bmp, FUN_0058c090 (right click on a shield). */
typedef struct {
	int screen;                     /* 0 main menu, 1 single-player setup */
	int hover;                      /* UF_* id under the mouse */
	const char *list[24]; int nlist, sel;   /* scenarios, then saved games; sel = chosen row */
	const char (*feat)[4]; int nfeat;       /* featured powers of the chosen scenario */
	const char *country_tag;                /* chosen country (featured or from the list), NULL none */
	int country_feat;                       /* its shield index, -1 when chosen from the list */
	const char *info[10]; int ninfo;        /* the log box (246,390): victory / option changes (green) */
	int nscen;                              /* list[0..nscen-1] scenarios, the rest saved games (own list at 36,420) */
	int panel;                              /* the country panel (docs/SPEC_FRONTEND_SP.md 2): a shield is chosen */
	const char *p_title, *p_text, *p_picture;   /* upper-cased title, history / strategy text, picture path */
	int p_rate[4];                          /* difficulty, economy, military, diplomacy 1..5 (adjusted) */
	int p_strat;                            /* 1 = strategy view (BACK on the toggle) */
	int p_page;                             /* the text box's page (a click on the text turns it; ours) */
	int popup;                              /* the other-countries list is open */
	const char (*others)[4]; int nothers, page;
	int can_start;
	int opt_popup;                          /* 0, UF_VICTORY or UF_OPTIONS (SPEC_UI §8.3 / §8.4) */
	const GameOpts *opts;
	const char *vtext[3]; int vfocus;       /* the edit boxes (year, VP, provinces), focused one or -1 */
} UiFront;
enum { UF_NONE = -1, UF_TUTORIAL = 1, UF_SINGLE, UF_MULTI, UF_CREDITS, UF_EXIT, UF_INFO, UF_VICTORY, UF_OPTIONS,
       UF_BACK, UF_START, UF_PREV, UF_NEXT, UF_POPCLOSE, UF_LIST = 100, UF_SHIELD = 200, UF_OTHER = 400,
       UF_VRADIO = 500, UF_VEDIT = 510, UF_OPTROW = 520, UF_OPTOK = 540, UF_VBACK = 541, UF_STRAT = 542, UF_PTEXT = 543, UF_TUTCH = 560 };   /* UF_TUTCH + 0..8 chapters, + 9 back */
#define UF_PAGE 14                      /* rows per page of the other-countries list */
void ui_front_draw(unsigned char *dst, int pitch, const UiFront *f);
int  ui_front_hit(int x, int y, const UiFront *f);
const unsigned char *ui_front_palette(void);   /* the menus' 256 colours (NULL: use the map palette) */
void ui_front_free(void);                       /* release the menus' sprites when the game starts */
/* an intro still (the converter's frames of the Bink videos): centred on black in the menus' palette; 0 ok */
int  ui_still_draw(unsigned char *dst, int pitch, Pak *pak, const char *entry);
/* the CREDITS screen: open (0 = ok), one 1-px scroll tick, draw (640x480 centred), close */
/* the tutorial's overlay: arrows (centre -> target, screen pixels), the AddMessage popups (key bases),
 * a script point (800x600) -> centre / target; buttons switched off (1 << UI_*) */
void ui_tut_arrows(unsigned char *dst, int pitch, const int *cx, const int *cy, const int *tx, const int *ty, int n);
void ui_tut_pos(int x, int y, int *cx, int *cy, int *tx, int *ty);
void ui_tut_pid(int sx, int sy, int *cx, int *cy, int *tx, int *ty);
void ui_tut_messages(unsigned char *dst, int pitch, const char *const *keys, int n, const int *tx, const int *ty, int narrows, int bottom);
void ui_btn_disable(unsigned long mask);
int  ui_btn_disabled(int id);
/* the game's mouse cursors, drawn in software (main.c composites them at every blit) */
enum { UI_CUR_ATTACK_ARMY, UI_CUR_ATTACK_NAVY, UI_CUR_CLICK, UI_CUR_ENTERPORT, UI_CUR_EXPLORE_ARMY, UI_CUR_EXPLORE_NAVY,
       UI_CUR_ILLEGAL, UI_CUR_LOAD, UI_CUR_MOVE_ARMY, UI_CUR_MOVE_NAVY, UI_CUR_SELECT, UI_CUR_SEND_COLONIST,
       UI_CUR_SEND_MERCHANT, UI_CUR_UNLOAD, UI_CUR_SMALL, UI_CUR_N };
void ui_cursor_kind(int k);
int  ui_cursor_get(void);
void ui_cursor_frontend(int on);                  /* 1: the menus' palette */
int  ui_cursor_box(int mx, int my, int *x, int *y, int *w, int *h);
int  ui_cursor_pointer(unsigned char *pix, int *w, int *h, int *hx, int *hy, unsigned char *rgb9, int *have_rgb);   /* the cursor as a 3-colour system pointer */
int  ui_cursor_id(void);                    /* frontend * UI_CUR_N + kind: which pointer image is due */
void ui_cursor_draw(unsigned char *dst, int pitch, int mx, int my);
int  ui_credits_open(void);
void ui_credits_step(void);
void ui_credits_draw(unsigned char *dst, int pitch);
void ui_credits_close(void);

/* in-game menu: 0 resume, 1 save, 2 load (greyed without a save), 3 exit */
void ui_menu_draw(unsigned char *dst, int pitch, int hover, int has_save);
int  ui_menu_hit(int x, int y);
/* the save / load list on the menu panel (SPEC_UI 9; the exe's btn_saveload list box is replaced by rows):
 * rows[i] text, state[i] 0 off (grey), 1 on; title a text.csv key. Hit: row, 100 = Back, -1 none */
/* message settings: rows of (title, bits); the three boxes per row; hit = row * 4 + column (1 box, 2 log,
 * 3 pause), 100 Back, 101 previous page, 102 next page, -1 none */
void ui_msgset_draw(unsigned char *dst, int pitch, const char *const *titles, const unsigned char *bits, int n, int first, int hover);
int  ui_msgset_hit(int x, int y, int n, int first);
#define UI_MSGSET_ROWS 12
void ui_slots_draw(unsigned char *dst, int pitch, const char *title, const char *const *rows, const int *state, int n, int hover);
int  ui_slots_hit(int x, int y, int n);
/* a message window (the popups of 0x578d50 / 0x426610): not modal, any number at once, dragged by
 * the whole body; box[0] HEADER, box[1] _1, box[2..6] _2.._6 ("" = none); btn[1] "" = OK only */
typedef struct {
	int x, y, w, h;               /* screen; w / h from ui_msgwin_layout */
	char tag[4];                  /* the player's small shield in the top corners */
	char cat[4];                  /* the emblem shield_flarp_<cat>.bmp */
	char box[7][200];
	char btn[2][40];
} UiMsgWin;
void ui_msgwin_layout(UiMsgWin *m);
void ui_msgwin_place(UiMsgWin *m);     /* the next cascade position */
void ui_msgwin_draw(unsigned char *dst, int pitch, const UiMsgWin *m, int hover);   /* hover: button 0 / 1, -1 none */
int  ui_msgwin_hit(const UiMsgWin *m, int x, int y);                                 /* 0 / 1 a button, 2 the body, -1 outside */
void ui_msgprop_draw(unsigned char *dst, int pitch, int x, int y, const char *title, int cur, int hover);   /* Message properties (0x575520) */
int  ui_msgprop_hit(int x, int y);                                                    /* 0..3 a choice, 4 OK, 5 inside, -1 outside */

#endif
