/* tutorial.c - the scripted tutorial (docs/SPEC_TUTORIAL.md). The exe is read as a specification only.
 * One record per CSV line (Action;Bool All;Num;Level;D1..D19), at most one current record; a record
 * that completes is dropped and the next one starts on the next tick; a waiting one is tested again
 * every tick, nothing times out. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tutorial.h"

enum { A_SHOWMESSAGE, A_SETNATIVES, A_DISCOVER, A_CHECKNUMARMIES, A_CHECKNUMNAVIES, A_COLONIZATION, A_NATIVESCLEARED,
       A_ALWAYSSUCCESS, A_ADDARROWS, A_ADDARROWSPOS, A_ADDARROWSPID, A_REMOVEARROWS, A_CREATECOT, A_ADDMERCHANTS,
       A_CHECKMERCHANT, A_CHECKTRADELEVEL, A_SETPROVINCEIW, A_CHECKMILALLIANCE, A_DISCOVERCAPITAL, A_CHECKFORCITIES,
       A_CHECKFORARMIES, A_DECLAREWAR, A_CHECKIFANYLOADEDUNITS, A_ENABLECAVALRY, A_CHECKNUMTRADINGPOSTS,
       A_DISABLEBUTTONS, A_ENABLEBUTTONS, A_ISBUTTONPRESS, A_WAITFORSCROLL, A_ENDTUTORIAL, A_BUILDARMY, A_STOPTIME,
       A_STARTTIME, A_ARMYSELECTED, A_FLEETSELECTED, A_ARMYIN, A_FLEETIN, A_GAMESPEED, A_SELECTARMY,
       A_DISABLESELECTIONS, A_NOENEMYIN, A_WECONTROL, A_ISPROVINCESELECTED, A_LOCKSCROLL, A_OFFERROYALMARRIAGE,
       A_CENTERMAP, A_ADDMESSAGE, A_REMOVEMESSAGE, A_OFFERALLIANCE, A_ISIW, A_N };

static const char *const g_act[A_N] = {   /* the table at 0x635074, in id order */
	"ShowMessage", "SetNatives", "Discover", "CheckNumArmies", "CheckNumNavies", "Colonization", "NativesCleared",
	"AlwaysSuccess", "AddArrows", "AddArrowsPos", "AddArrowsPid", "RemoveArrows", "CreateCot", "AddMerchants",
	"CheckMerchant", "CheckTradeLevel", "SetProvinceIW", "CheckMilAlliance", "DiscoverCapital", "CheckForCities",
	"CheckForArmies", "DeclareWar", "CheckIfAnyLoadedUnits", "EnableCavalry", "CheckNumTradingPosts",
	"DisableButtons", "EnableButtons", "IsButtonPress", "WaitForScroll", "EndTutorial", "BuildArmy", "StopTime",
	"StartTime", "ArmySelected", "FleetSelected", "ArmyIn", "FleetIn", "GameSpeed", "SelectArmy",
	"DisableSelections", "NoEnemyIn", "WeControl", "IsProvinceSelected", "LockScroll", "OfferRoyalMarriage",
	"CenterMap", "AddMessage", "RemoveMessage", "OfferAlliance", "IsIW" };

static const char *const g_btn[TB_N] = {
	"BUTTON_DIPLOMAT", "BUTTON_MERCHANT", "BUTTON_COLONIST", "BUTTON_MISSION", "BUTTON_VIEW_STATISTICS",
	"BUTTON_LAND_TECHNOLOGY", "BUTTON_NAVAL_TECHNOLOGY", "BUTTON_VIEW_COUNTRY", "BUTTON_SET_BUDGET",
	"BUTTON_MISSIONARY", "BUTTON_VP", "BUTTON_VIEW_LAND", "BUTTON_VIEW_NAVAL", "BUTTON_VIEW_FINANCIAL",
	"BUTTON_CHURCH", "BUTTON_BUILD_ARMY", "BUTTON_BUILD_NAVY", "BUTTON_BUILD_MANUFACTORY", "BUTTON_BUILD_BAILIFF",
	"BUTTON_BUILD_CITYRIGHTS", "BUTTON_BUILD_COURTHOUSE", "BUTTON_EXPAND_FORTRESS", "BUTTON_ASSAULT",
	"BUTTON_MAPMODE_NORMAL", "BUTTON_MAPMODE_POLITICAL", "BUTTON_MAPMODE_RESOURCE" };

#define TUT_MAXV 20
#define TUT_MAXARROW 16
#define TUT_MAXMSG 8
typedef struct {
	short id;                 /* A_*, -1 unknown (blocks forever, as in the exe) */
	unsigned char all;        /* Bool All */
	long num, level;
	short n, v[TUT_MAXV];     /* provinces / pairs / buttons / countries */
	char key[48];             /* ShowMessage / AddMessage: the key base */
} TutRec;

struct Tutorial {
	World *w;
	int player;
	TutHost h;
	TutRec *rec;
	int nrec, next, cur, step;
	unsigned long disabled;   /* 1 << TB_* */
	int last_btn, scroll, city, merchant_hit;
	unsigned char always, cavalry, lockscroll, changed, ended;
	TutArrow arrow[TUT_MAXARROW]; int narrow;
	char msg[TUT_MAXMSG][48]; int nmsg;
};

static int ieq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
		if (x != y) return 0;
	}
	return *a == *b;
}

static long num(const char *s) { return strtol(s, NULL, 10); }   /* atoi: text and empty read as 0 */

static void trim(char *s)
{
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
	while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
}

static void parse_line(Tutorial *t, char *line)
{
	char *f[23];
	int nf = 0, i;
	TutRec *r;
	char *p = line;
	for (i = 0; i < 23; i++) f[i] = "";
	while (nf < 23) {
		char *e = strchr(p, ';');
		f[nf++] = p;
		if (!e) break;
		*e = 0; p = e + 1;
	}
	for (i = 0; i < nf; i++) trim(f[i]);
	if (!f[0][0]) return;
	r = &t->rec[t->nrec++];
	memset(r, 0, sizeof *r);
	r->id = -1;
	for (i = 0; i < A_N; i++) if (ieq(f[0], g_act[i])) { r->id = (short)i; break; }
	r->all = num(f[1]) != 0;
	r->num = num(f[2]);
	r->level = num(f[3]);
	switch (r->id) {
	case A_SHOWMESSAGE: case A_ADDMESSAGE:
		snprintf(r->key, sizeof r->key, "%s", f[4]);
		break;
	case A_SETNATIVES: case A_DISCOVER: case A_COLONIZATION: case A_NATIVESCLEARED: case A_ALWAYSSUCCESS:
	case A_ADDARROWSPID: case A_CREATECOT: case A_ADDMERCHANTS: case A_SETPROVINCEIW: case A_CHECKMERCHANT:
	case A_CHECKFORCITIES: case A_BUILDARMY:
		for (i = 4; i < 23; i++) { long v = num(f[i]); if (v && r->n < TUT_MAXV) r->v[r->n++] = (short)v; }
		break;
	case A_ADDARROWSPOS:
		for (i = 4; i + 1 < 23 && r->n + 2 <= 18; i += 2) {
			long x = num(f[i]), y = num(f[i + 1]);
			if (!x || !y) break;
			r->v[r->n++] = (short)x; r->v[r->n++] = (short)y;
		}
		break;
	case A_DISABLEBUTTONS: case A_ENABLEBUTTONS: case A_ISBUTTONPRESS: case A_ADDARROWS:
		for (i = 4; i < 23; i++) {
			int k;
			for (k = 0; k < TB_N; k++) if (ieq(f[i], g_btn[k])) { if (r->n < TUT_MAXV) r->v[r->n++] = (short)k; break; }
		}
		break;
	case A_CHECKMILALLIANCE: case A_DISCOVERCAPITAL: case A_DECLAREWAR: case A_OFFERROYALMARRIAGE: case A_OFFERALLIANCE:
		for (i = 4; i < 23; i++) { int c = f[i][0] ? world_country(t->w, f[i]) : -1; if (c >= 0 && r->n < TUT_MAXV) r->v[r->n++] = (short)c; }
		break;
	default: break;
	}
}

Tutorial *tut_load(Pak *text, const char *csv, World *w, int player, const TutHost *h, char *err, int errlen)
{
	Tutorial *t;
	char path[96], *buf, *p, *e;
	unsigned long sz;
	int i, lines = 0;
	snprintf(path, sizeof path, "%s", csv && csv[0] ? csv : "db/tutorial.csv");
	for (i = 0; path[i]; i++) { if (path[i] == '\\') path[i] = '/'; if (path[i] >= 'A' && path[i] <= 'Z') path[i] += 32; }
	if (!(buf = (char *)pak_load(text, path, &sz))) { snprintf(err, (size_t)errlen, "no %s", path); return NULL; }
	buf = (char *)realloc(buf, sz + 1); buf[sz] = 0;
	for (p = buf; *p; p++) if (*p == '\n') lines++;
	t = (Tutorial *)calloc(1, sizeof *t);
	t->rec = (TutRec *)calloc((size_t)lines + 2, sizeof *t->rec);
	t->w = w; t->player = player; t->h = *h; t->cur = -1; t->last_btn = -1; t->city = -1;
	for (p = buf, i = 0; *p; p = e, i++) {
		for (e = p; *e && *e != '\n'; e++) ;
		if (*e) *e++ = 0;
		if (i == 0) continue;                         /* the header line */
		if (t->nrec <= lines) parse_line(t, p);
	}
	free(buf);
	for (i = 0; i < t->nrec; i++) if (t->rec[i].id < 0) { snprintf(err, (size_t)errlen, "line %d: unknown action (blocks)", i + 2); break; }
	t->changed = 1;
	return t;
}

void tut_free(Tutorial *t) { if (t) { free(t->rec); free(t); } }

/* units in a province: ours / others' */
static void units_in(const Tutorial *t, int prov, int *ours, int *others)
{
	const World *w = t->w;
	int i;
	*ours = *others = 0;
	for (i = 0; i < w->nland; i++) if (!w->land[i].dead && w->land[i].location == prov) { if (w->land[i].owner == t->player) ++*ours; else ++*others; }
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].location == prov && w->naval[i].warships + w->naval[i].galleys + w->naval[i].transports > 0) { if (w->naval[i].owner == t->player) ++*ours; else ++*others; }
}

static int prov_ok(const Tutorial *t, int p) { return p > 0 && p < t->w->nprov; }

/* one Data item of the "Bool All" checks */
static int item_ok(Tutorial *t, const TutRec *r, int k)
{
	const World *w = t->w;
	int p = r->v[k], o, x;
	switch (r->id) {
	case A_DISCOVER: return prov_ok(t, p) && WORLD_KNOWN(w, t->player, p);
	case A_COLONIZATION: return prov_ok(t, p) && (w->prov[p].population > 0 || w->prov[p].tp > 0) && w->prov[p].owner == t->player && w->prov[p].controller == t->player;
	case A_NATIVESCLEARED: if (!prov_ok(t, p) || w->prov[p].natives > 0) return 0; units_in(t, p, &o, &x); return x == 0;
	case A_CHECKFORCITIES:
		if (prov_ok(t, p) && w->prov[p].population > 0 && w->prov[p].owner == t->player && w->prov[p].controller == t->player) { if (t->city < 0) t->city = p; return 1; }
		return 0;
	case A_CHECKMILALLIANCE: {
		int i, m, a, b;
		for (i = 0; i < w->nally; i++) {
			if (w->ally[i].type != ALLY_MILITARY) continue;
			for (a = b = 0, m = 0; m < w->ally[i].n; m++) { if (w->ally[i].member[m] == t->player) a = 1; if (w->ally[i].member[m] == p) b = 1; }
			if (a && b) return 1;
		}
		return 0; }
	case A_CHECKMERCHANT: return p == t->merchant_hit;
	default: return 0;
	}
}

static int bool_all(Tutorial *t, const TutRec *r)
{
	int k, n = 0;
	for (k = 0; k < r->n; k++) n += item_ok(t, r, k);
	return r->all ? n == r->n && r->n > 0 : n > 0;
}

/* runs / tests r: 1 = done */
static int run(Tutorial *t, TutRec *r)
{
	World *w = t->w;
	const TutHost *h = &t->h;
	int k, o, x;
	switch (r->id) {
	case A_SHOWMESSAGE: {
		char nk[64], dk[64];
		snprintf(nk, sizeof nk, "%s_NAME", r->key); snprintf(dk, sizeof dk, "%s_DESC", r->key);
		if (h->show) h->show(h->u, nk, dk);           /* modal here: the popup is closed when it returns */
		return 1; }
	case A_SETNATIVES:                                  /* the natives cap (0x482a20 clamps the current size to it) */
		for (k = 0; k < r->n; k++) if (prov_ok(t, r->v[k])) { Province *p = &w->prov[r->v[k]]; if (p->natives > r->num) p->natives = r->num; }
		return 1;
	case A_DISCOVER: case A_COLONIZATION: case A_NATIVESCLEARED: case A_CHECKFORCITIES: case A_CHECKMILALLIANCE:
		return bool_all(t, r);
	case A_CHECKMERCHANT:
		if (t->merchant_hit <= 0) return 0;
		k = bool_all(t, r); t->merchant_hit = 0;
		return k;
	case A_CHECKNUMARMIES: case A_CHECKNUMNAVIES: {
		int n = 0;
		if (r->id == A_CHECKNUMARMIES) { for (k = 0; k < w->nland; k++) if (w->land[k].owner == t->player && w->land[k].dead != 1) n++; }
		else for (k = 0; k < w->nnaval; k++) if (w->naval[k].owner == t->player && w->naval[k].warships + w->naval[k].galleys + w->naval[k].transports > 0) n++;
		return n >= r->num; }
	case A_ALWAYSSUCCESS: t->always = 1; return 1;
	case A_ADDARROWS: return 1;                         /* never worked in the exe (NIEPEWNE), unused */
	case A_ADDARROWSPOS:
		for (k = 0; k + 1 < r->n && t->narrow < TUT_MAXARROW; k += 2) { TutArrow *a = &t->arrow[t->narrow++]; a->pid = 0; a->x = r->v[k]; a->y = r->v[k + 1]; }
		t->changed = 1; return 1;
	case A_ADDARROWSPID:
		for (k = 0; k < r->n && t->narrow < TUT_MAXARROW; k++) if (prov_ok(t, r->v[k])) { TutArrow *a = &t->arrow[t->narrow++]; a->pid = r->v[k]; a->x = a->y = 0; }
		t->changed = 1; return 1;
	case A_REMOVEARROWS: t->narrow = 0; t->changed = 1; return 1;
	case A_CREATECOT:                                   /* unused by the scripts */
		for (k = 0; k < r->n; k++) {
			int p = r->v[k], i, has = 0;
			if (!prov_ok(t, p)) continue;
			for (i = 0; i < w->ncot; i++) if (w->cot[i].prov == p) has = 1;
			if (!has && w->ncot < MAX_COT) { memset(&w->cot[w->ncot], 0, sizeof w->cot[0]); w->cot[w->ncot].prov = (short)p; w->ncot++; }
		}
		return 1;
	case A_ADDMERCHANTS: return 1;                      /* unused; its first call is NIEPEWNE */
	case A_CHECKTRADELEVEL: return w->country[t->player].trade_tech >= r->level;
	case A_SETPROVINCEIW: if (r->n && prov_ok(t, r->v[0]) && h->province_iw) h->province_iw(h->u, r->v[0]); return 1;
	case A_DISCOVERCAPITAL:
		for (k = 0; k + 1 < r->n; k += 2) { int cap = w->country[r->v[k + 1]].capital; if (prov_ok(t, cap)) WORLD_DISCOVER(w, r->v[k], cap); }
		return 1;
	case A_CHECKFORARMIES: {
		long men = 0;
		if (t->city <= 0) return 0;
		for (k = 0; k < w->nland; k++) if (!w->land[k].dead && w->land[k].owner == t->player && w->land[k].location == t->city) men += w->land[k].infantry + w->land[k].cavalry + w->land[k].artillery;
		return men >= r->num; }
	case A_DECLAREWAR: for (k = 0; k + 1 < r->n; k += 2) if (h->declare) h->declare(h->u, r->v[k], r->v[k + 1]); return 1;
	case A_CHECKIFANYLOADEDUNITS:
		for (k = 0; k < w->nland; k++) if (w->land[k].owner == t->player && w->land[k].dead == LAND_ABOARD) return 1;
		return 0;
	case A_ENABLECAVALRY: t->cavalry = 1; return 1;
	case A_CHECKNUMTRADINGPOSTS: {
		int n = 0;
		for (k = 1; k < w->nprov; k++) if (w->prov[k].owner == t->player && w->prov[k].tp > 0) n++;
		return n >= r->num; }
	case A_DISABLEBUTTONS: for (k = 0; k < r->n; k++) t->disabled |= 1UL << r->v[k]; t->changed = 1; return 1;
	case A_ENABLEBUTTONS: for (k = 0; k < r->n; k++) t->disabled &= ~(1UL << r->v[k]); t->changed = 1; return 1;
	case A_ISBUTTONPRESS: for (k = 0; k < r->n; k++) if (t->last_btn == r->v[k]) return 1; return 0;   /* never cleared (as the exe) */
	case A_WAITFORSCROLL: if (t->scroll >= r->num) { t->scroll = 0; return 1; } return 0;
	case A_ENDTUTORIAL: t->ended = 1; if (h->end) h->end(h->u); return 1;
	case A_BUILDARMY: for (k = 0; k < r->n; k++) if (prov_ok(t, r->v[k]) && h->build_army) h->build_army(h->u, t->player, r->v[k], r->num); return 1;
	case A_STOPTIME: if (h->pause) h->pause(h->u, 1); return 1;
	case A_STARTTIME: if (h->pause) h->pause(h->u, 0); return 1;
	case A_ARMYSELECTED: return h->sel_armies && h->sel_armies(h->u) >= r->num;
	case A_FLEETSELECTED: return h->sel_fleets && h->sel_fleets(h->u) >= r->num;
	case A_ARMYIN: return h->sel_army_at && h->sel_army_at(h->u, (int)r->num);
	case A_FLEETIN: return h->sel_fleet_at && h->sel_fleet_at(h->u, (int)r->num);
	case A_SELECTARMY: return 1;                        /* broken in the exe (NIEPEWNE), unused */
	case A_NOENEMYIN: if (!prov_ok(t, (int)r->num)) return 0; units_in(t, (int)r->num, &o, &x); return x == 0;
	case A_WECONTROL: return prov_ok(t, (int)r->num) && w->prov[r->num].controller == t->player;
	case A_ISPROVINCESELECTED: return 0;                /* unused; needs the selection (NIEPEWNE) */
	case A_LOCKSCROLL: t->lockscroll = 1; return 1;     /* nothing reads it in v1.09 */
	case A_OFFERROYALMARRIAGE: for (k = 0; k + 1 < r->n; k += 2) if (h->marriage) h->marriage(h->u, r->v[k], r->v[k + 1]); return 1;
	case A_CENTERMAP: if (prov_ok(t, (int)r->num) && h->center) h->center(h->u, (int)r->num); return 1;
	case A_ADDMESSAGE:
		if (t->nmsg < TUT_MAXMSG) snprintf(t->msg[t->nmsg++], sizeof t->msg[0], "%s", r->key);
		t->changed = 1; return 1;
	case A_REMOVEMESSAGE: t->nmsg = 0; t->changed = 1; return 1;
	case A_OFFERALLIANCE: return 1;                     /* unused by the scripts */
	case A_ISIW: return h->infowin && h->infowin(h->u) == r->num;
	default: return 0;                                  /* GameSpeed, DisableSelections, unknown: no handler, blocks */
	}
}

/* the exe steps one record per interface update (message 0x18); one of our passes can cost a full redraw (hundreds of ms on
   a slow machine), so the records that finish at once run together, up to one that waits (32 at most) */
void tut_tick(Tutorial *t)
{
	int k;
	for (k = 0; k < 32; k++) {
		if (!t || t->ended) return;
		if (t->cur < 0) {
			if (t->next >= t->nrec) return;
			t->cur = t->next++;
			t->step++;
		}
		if (!run(t, &t->rec[t->cur])) return;                 /* waits */
		t->cur = -1;
	}
}

int  tut_btn_ok(const Tutorial *t, int tb) { return !t || tb < 0 || !(t->disabled & (1UL << tb)); }
void tut_btn_pressed(Tutorial *t, int tb) { if (t && tb >= 0) t->last_btn = tb; }
void tut_scrolled(Tutorial *t) { if (t) t->scroll++; }
void tut_merchant_arrived(Tutorial *t, int cot_prov) { if (t && t->cur >= 0 && t->rec[t->cur].id == A_CHECKMERCHANT) { t->merchant_hit = cot_prov; if (run(t, &t->rec[t->cur])) t->cur = -1; } }
int  tut_always_success(const Tutorial *t) { return t && t->always; }
int  tut_cavalry(const Tutorial *t) { return !t || t->cavalry; }
int  tut_step(const Tutorial *t) { return t ? t->step : 0; }

void tut_restart_at(Tutorial *t, int step)
{
	int i;
	if (!t || step <= 1) return;
	/* the records before step N are thrown away (0x567ab0): their effects are not replayed, except the
	 * state that lives in this object, which the scripts set up front (NIEPEWNE: the exe keeps none) */
	for (i = 0; i < step - 1 && i < t->nrec; i++) {
		const TutRec *r = &t->rec[i];
		int k;
		if (r->id == A_DISABLEBUTTONS) for (k = 0; k < r->n; k++) t->disabled |= 1UL << r->v[k];
		else if (r->id == A_ENABLEBUTTONS) for (k = 0; k < r->n; k++) t->disabled &= ~(1UL << r->v[k]);
	}
	t->next = step - 1 < t->nrec ? step - 1 : t->nrec;
	t->step = step - 1; t->cur = -1; t->changed = 1;
}

int tut_arrows(const Tutorial *t, TutArrow *out, int max)
{
	int i;
	if (!t) return 0;
	for (i = 0; i < t->narrow && i < max; i++) out[i] = t->arrow[i];
	return i;
}

int tut_messages(const Tutorial *t, const char **keys, int max)
{
	int i;
	if (!t) return 0;
	for (i = 0; i < t->nmsg && i < max; i++) keys[i] = t->msg[i];
	return i;
}

int tut_changed(Tutorial *t) { int c = t && t->changed; if (t) t->changed = 0; return c; }
