/* options.h - the frontend's VICTORY and OPTIONS popups (SPEC_UI §8.3, §8.4): victory condition and
 * game options, kept in PROGDIR:eu2opts.txt between runs. Portable C. */
#ifndef EU2_OPTIONS_H
#define EU2_OPTIONS_H

enum { VIC_STANDARD, VIC_SUPREMACY, VIC_DEATHMATCH, VIC_MISSION };
enum { OPT_DIFFICULTY, OPT_AIAGGR, OPT_SPEED, OPT_FOG, OPT_ANNEX, OPT_MISSIONS, OPT_BASEVP, OPT_AUTOSAVE, OPT_EVENTS, OPT_N };

typedef struct {
	int vmode;                 /* VIC_* (the exe's [0x647d48]) */
	int vyear, vvp, vprov;     /* Standard: end year; Supremacy: VP; Deathmatch: new provinces */
	int opt[OPT_N];            /* index into the row's values (opts_nvalues) */
	unsigned char msg[80];     /* per message type (opts_msg_type): MSG_BOX popup, MSG_LOG log line, MSG_PAUSE pause */
} GameOpts;

void opts_default(GameOpts *o);
int  opts_load(GameOpts *o, const char *path);      /* 0 ok; missing file = defaults */
int  opts_save(const GameOpts *o, const char *path);
int  opts_nvalues(int row);
const char *opts_label(int row);                    /* text.csv keys */
const char *opts_value(int row, int v);
int  opts_clamp_num(int field, int v);              /* field 0 year, 1 VP, 2 provinces */
int  opts_autosave_years(const GameOpts *o);        /* 0 never */
/* message settings (SPEC_UI 9: per type a popup box, a log line, a pause): the types the game raises */
enum { MSG_BOX = 1, MSG_LOG = 2, MSG_PAUSE = 4 };
int  opts_msg_count(void);
const char *opts_msg_type(int i);                   /* text.csv type, e.g. "LBWIN" */
int  opts_msg_index(const char *type);              /* -1 unknown */
void opts_msg_defaults(GameOpts *o, int reset_only_ours);   /* the exe's table (0x635918) */
int  opts_msg_bits(const GameOpts *o, const char *type);   /* MSG_BOX | MSG_LOG for an unknown type */
#endif
