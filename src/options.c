/* options.c - see options.h. Rows and values from the exe's OPTIONS popup (0x58aaa0); the defaults
 * are ours (NIEPEWNE: the exe reads them from its settings file). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "options.h"

static const char *const lab[OPT_N] = { "FEOPT_DIFFICULTY", "FEOPT_AIAGGRE", "FEOPT_GAMESPEED", "FEOPT_FOGOFWAR",
	"FEOPT_CONQCAP", "FEOPT_MISSIONS", "FEOPT_BASEVP", "FEOPT_AUTOSAVE", "FEOPT_GAMEEVENTS" };
static const char *const v_diff[5] = { "FE_DIFFI5", "FE_DIFFI4", "FE_DIFFI3", "FE_DIFFI2", "FE_DIFFI1" };
static const char *const v_ai[5] = { "FEOPT_AI_LEVEL1", "FEOPT_AI_LEVEL2", "FEOPT_AI_LEVEL3", "FEOPT_AI_LEVEL4", "FEOPT_AI_LEVEL5" };
static const char *const v_speed[5] = { "FEOPT_GAMESPEED2", "FEOPT_GAMESPEED3", "FEOPT_GAMESPEED4", "FEOPT_GAMESPEED5", "FEOPT_GAMESPEED6" };
static const char *const v_onoff[2] = { "FEOPT_OFF", "FEOPT_ON" };
static const char *const v_auto[5] = { "FEOPT_AUTOSAVE1", "FEOPT_AUTOSAVE2", "FEOPT_AUTOSAVE3", "FEOPT_AUTOSAVE4", "FEOPT_AUTOSAVE5" };
static const char *const v_ev[4] = { "FEOPT_EVENS_NO", "FEOPT_EVENTS_RANDOM", "FEOPT_EVENTS_HISTORICAL", "FEOPT_EVENTS_BOTH" };
static const char *const key[OPT_N] = { "difficulty", "aggressiveness", "speed", "fog", "annex", "missions", "basevp", "autosave", "events" };

static const char *const msgt[] = { "DECLWAR", "LBWIN", "LBLOSS", "SBWIN", "PROVOWNOCC", "REVOLT", "MERCLOSS", "MERCSUCC", "MERCFAIL",
	"COLNEWSUCCESS", "COLEXPANDSUCCESS", "COLFAIL", "TPNEWSUCCESS", "TPEXPANDSUCCESS", "TPFAILEST", "MISSOSUCC", "MISSOFAIL", "MISSUCC",
	"MISSFAIL", "NEWMON", "LTINCREASE", "NTINCREASE", "TRADETINCREASE", "INFRATINCREASE", "STABINCREASE", "STABDROP", "MANUFINISH",
	"ARMYDESTROY", "NAVYDESTROY", "LEADERNEW", "LEADERDIES", "PEACEACCEPT", "PEACEREJECT", "WHITEPEACEUS", "LOANSACCEPT", "LOANSREJECT", "LOANSREPAY", "LOANSNOREPAY", "NBWIN", "NBLOSS", "LIBPROV", "ARMYARRIVE", "FLEETARRIVE", "DECLWAROTHER", "AUTOLOAN", "BANKRUPT",
	"LBWINOTHER", "SBWINOTHER", "REVOLTOTHER", "BANKRUPTOTHER", "JOINWAROTHER", "BROKENOTHER", "OTHERRELIGION", "NEWNATION", "DEFECTO",
	"ROYMARACCEPTOTHER", "ANNEXACCEPTOTHER", "OTHERTRADEAG", "BRKTRADEOTH", "ACCMILOTHER", "CANMILOTHE", "CLAIMTOOTH", "OTHERDEFENDER",
	"NEWMONOTHER", "MONOPOLYOTHER", "PEACEACCEPTOTHER" };   /* SPEC_SMALLRULES 5: news of other countries */
#define NMSG ((int)(sizeof msgt / sizeof msgt[0]))
int opts_msg_count(void) { return NMSG; }
const char *opts_msg_type(int i) { return i >= 0 && i < NMSG ? msgt[i] : ""; }
int opts_msg_index(const char *t) { int i; for (i = 0; i < NMSG; i++) if (!strcmp(t, msgt[i])) return i; return -1; }
int opts_msg_bits(const GameOpts *o, const char *t) { int i = opts_msg_index(t); return i < 0 ? MSG_BOX | MSG_LOG : o->msg[i]; }

void opts_default(GameOpts *o)
{
	memset(o, 0, sizeof *o);
	o->vmode = VIC_STANDARD; o->vyear = 1819; o->vvp = 500; o->vprov = 500;   /* the edit boxes' defaults (0x58d6e6..) */
	o->opt[OPT_DIFFICULTY] = 2; o->opt[OPT_AIAGGR] = 2; o->opt[OPT_SPEED] = 1;   /* speed: our default day length (Normal) */
	o->opt[OPT_FOG] = 1; o->opt[OPT_ANNEX] = 1; o->opt[OPT_MISSIONS] = 1; o->opt[OPT_BASEVP] = 1;
	o->opt[OPT_AUTOSAVE] = 1; o->opt[OPT_EVENTS] = 3;
	opts_msg_defaults(o, 0);
}

/* the exe's fresh-install table (0x635918 +8 popup / +9 log, pause off for every type): popup + log
 * unless listed here. reset_only_ours: only the types whose default we used to get wrong (an
 * eu2opts.txt written before msgdef=2 holds our old guesses, not the player's choice) */
static const char *const exe_log[] = { "MISSUCC", "MISSFAIL", "NEWMONOTHER", "MANUFINISH", "MONOPOLYOTHER", "BANKRUPTOTHER",
	"ANNEXACCEPTOTHER", "PEACEACCEPTOTHER", "ROYMARACCEPTOTHER", "DECLWAROTHER", "JOINWAROTHER", "BROKENOTHER",
	"CLAIMTOOTH", "OTHERTRADEAG", "BRKTRADEOTH", "ACCMILOTHER", "CANMILOTHE", "LIBPROV" };
static const char *const exe_none[] = { "REVOLTOTHER", "SBWINOTHER", "LBWINOTHER" };
static const char *const was_wrong[] = { "ARMYARRIVE", "FLEETARRIVE", "STABINCREASE", "STABDROP", "DEFECTO", "OTHERRELIGION",
	"OTHERDEFENDER", "LBWINOTHER", "SBWINOTHER", "REVOLTOTHER", "MISSUCC", "MISSFAIL", "MANUFINISH", "LIBPROV" };
void opts_msg_defaults(GameOpts *o, int reset_only_ours)
{
	unsigned char d[80];
	int i;
	for (i = 0; i < NMSG; i++) d[i] = MSG_BOX | MSG_LOG;
	for (i = 0; i < (int)(sizeof exe_log / sizeof exe_log[0]); i++) d[opts_msg_index(exe_log[i])] = MSG_LOG;
	for (i = 0; i < (int)(sizeof exe_none / sizeof exe_none[0]); i++) d[opts_msg_index(exe_none[i])] = 0;
	if (!reset_only_ours) { memcpy(o->msg, d, (size_t)NMSG); return; }
	for (i = 0; i < (int)(sizeof was_wrong / sizeof was_wrong[0]); i++) { int k = opts_msg_index(was_wrong[i]); o->msg[k] = d[k]; }
}

int opts_nvalues(int row)
{
	switch (row) {
	case OPT_DIFFICULTY: case OPT_AIAGGR: case OPT_SPEED: case OPT_AUTOSAVE: return 5;
	case OPT_EVENTS: return 4;
	default: return row >= 0 && row < OPT_N ? 2 : 0;
	}
}

const char *opts_label(int row) { return row >= 0 && row < OPT_N ? lab[row] : ""; }

const char *opts_value(int row, int v)
{
	if (v < 0 || v >= opts_nvalues(row)) return "";
	switch (row) {
	case OPT_DIFFICULTY: return v_diff[v];
	case OPT_AIAGGR: return v_ai[v];
	case OPT_SPEED: return v_speed[v];
	case OPT_AUTOSAVE: return v_auto[v];
	case OPT_EVENTS: return v_ev[v];
	default: return v_onoff[v];
	}
}

int opts_clamp_num(int field, int v)
{
	if (field == 0) return v < 1420 ? 1420 : v > 1820 ? 1820 : v;   /* the year: inside the game's span (ours) */
	return v < 10 ? 10 : v > 750 ? 750 : v;                          /* 0x58dc0a: 10..750 */
}

int opts_autosave_years(const GameOpts *o)
{
	static const int y[5] = { 0, 1, 3, 5, 10 };
	int v = o->opt[OPT_AUTOSAVE];
	return v >= 0 && v < 5 ? y[v] : 0;
}

int opts_load(GameOpts *o, const char *path)
{
	FILE *f;
	char line[96];
	int msgdef = 0;
	opts_default(o);
	if (!(f = fopen(path, "r"))) return 0;
	while (fgets(line, sizeof line, f)) {
		char *v = strchr(line, '=');
		int i, n;
		if (!v) continue;
		*v++ = 0;
		n = atoi(v);
		if (!strcmp(line, "victory")) o->vmode = n >= 0 && n <= VIC_MISSION ? n : VIC_STANDARD;
		else if (!strcmp(line, "endyear")) o->vyear = opts_clamp_num(0, n);
		else if (!strcmp(line, "vp")) o->vvp = opts_clamp_num(1, n);
		else if (!strcmp(line, "provinces")) o->vprov = opts_clamp_num(2, n);
		else if (!strncmp(line, "msg_", 4)) { int k = opts_msg_index(line + 4); if (k >= 0) o->msg[k] = (unsigned char)(n & 7); }
		else if (!strcmp(line, "msgdef")) msgdef = n;
		else for (i = 0; i < OPT_N; i++) if (!strcmp(line, key[i]) && n >= 0 && n < opts_nvalues(i)) o->opt[i] = n;
	}
	fclose(f);
	if (msgdef < 2) opts_msg_defaults(o, 1);              /* written with our old guessed defaults */
	return 0;
}

int opts_save(const GameOpts *o, const char *path)
{
	FILE *f = fopen(path, "w");
	int i;
	if (!f) return -1;
	fprintf(f, "victory=%d\nendyear=%d\nvp=%d\nprovinces=%d\n", o->vmode, o->vyear, o->vvp, o->vprov);
	for (i = 0; i < OPT_N; i++) fprintf(f, "%s=%d\n", key[i], o->opt[i]);
	fprintf(f, "msgdef=2\n");
	for (i = 0; i < NMSG; i++) fprintf(f, "msg_%s=%d\n", msgt[i], o->msg[i]);
	fclose(f);
	return 0;
}
