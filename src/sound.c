/* sound.c - see sound.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"
#include "script.h"
#include "amiga_audio.h"
#include <dos/dos.h>
#include <proto/dos.h>
#include "amiga_gfx.h"

#define MAXSFX 72
typedef struct { char key[32], file[40]; void *chip; unsigned long bytes; long rate; int tried; } Sfx;
static Sfx g_sfx[MAXSFX]; static int g_nsfx, g_on, g_next_ch;
static Sfx *g_ch_sfx[4];                                   /* what each channel last started */
static Pak *g_pak, *g_text;
static char *g_index, *g_list_buf; static char *g_list[96]; static int g_nlist, g_cur = -1;
typedef struct { BPTR fh; long rate, left; } PcmStream;   /* EUM1: "EUM1" rate samples, then signed 8-bit */
static PcmStream g_ps, *g_stream;
static unsigned long g_seed = 1;

/* refill: plain DOS Read straight into the buffer, nothing to decode (every 68k cycle counts) */
static int music_refill(void *ud, signed char *dst, int max)
{
	PcmStream *s = (PcmStream *)ud;
	long n;
	if (!g_stream || s->left <= 0) return 0;
	if (max > s->left) max = (int)s->left;
	n = Read(s->fh, dst, max);
	if (n <= 0) { s->left = 0; return 0; }
	s->left -= n;
	return (int)n;
}

static PcmStream *pcm_open(const char *path)
{
	unsigned char h[12];
	if (!(g_ps.fh = Open((STRPTR)path, MODE_OLDFILE))) return NULL;
	if (Read(g_ps.fh, h, 12) != 12 || memcmp(h, "EUM1", 4)) { Close(g_ps.fh); g_ps.fh = 0; return NULL; }
	g_ps.rate = BE32(h + 4); g_ps.left = BE32(h + 8);
	if (g_ps.rate < 4000) { Close(g_ps.fh); g_ps.fh = 0; return NULL; }
	return &g_ps;
}

static void pcm_close(PcmStream *s) { if (s && s->fh) { Close(s->fh); s->fh = 0; } }

int sound_init(Pak *sfx, Pak *text)
{
	unsigned long sz;
	char *buf, *cur, *line;
	g_pak = sfx; g_text = text;
	g_on = AmigaAudio_Open();
	if (!g_on) return 0;
	if ((buf = (char *)pak_load(text, "config/sfx.txt", &sz)) != NULL) {     /* key = "file" */
		cur = buf;
		while ((line = csv_line(&cur)) != NULL && g_nsfx < MAXSFX) {
			char *eq = strchr(line, '='), *q1, *q2, *k = line, *e;
			if (!eq || line[0] == '#') continue;
			*eq = 0;
			while (*k == ' ' || *k == '\t') k++;
			for (e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
			q1 = strchr(eq + 1, '"'); q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (!q1 || !q2 || !*k) continue;
			*q2 = 0;
			snprintf(g_sfx[g_nsfx].key, sizeof g_sfx[0].key, "%s", k);
			snprintf(g_sfx[g_nsfx].file, sizeof g_sfx[0].file, "%s", q1 + 1);
			g_nsfx++;
		}
		free(buf);
	}
	{	/* two ids only the exe has (0x3F / 0x40, SPEC_SOUND 1): the army move order sounds */
		static const char *const x[2] = { "if_move_army", "if_move_cav" };
		int k;
		for (k = 0; k < 2 && g_nsfx < MAXSFX; k++) { snprintf(g_sfx[g_nsfx].key, sizeof g_sfx[0].key, "%s", x[k]); snprintf(g_sfx[g_nsfx].file, sizeof g_sfx[0].file, "%s", x[k]); g_nsfx++; }
	}
	g_index = (char *)pak_load(text, "music/index.txt", &sz);   /* not in a pack: read the file */
	if (!g_index) {
		FILE *f = fopen("PROGDIR:data/music/index.txt", "r");
		if (f) {
			long n;
			fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
			g_index = malloc((size_t)n + 1);
			if (g_index) { n = (long)fread(g_index, 1, (size_t)n, f); g_index[n] = 0; }
			fclose(f);
		}
	}
	return 0;
}

static Sfx *find(const char *name)
{
	int i;
	for (i = 0; i < g_nsfx; i++) if (script_ieq(g_sfx[i].key, name)) return &g_sfx[i];
	return NULL;
}

void sound_play(const char *name)
{
	Sfx *s;
	int ch, tries;
	if (!g_on || !(s = find(name))) return;
	if (!s->chip && !s->tried) {                         /* first use: EUS1 -> Chip RAM */
		char path[80];
		unsigned long sz;
		unsigned char *d;
		s->tried = 1;
		snprintf(path, sizeof path, "sfx/wave/%s.wav", s->file);
		if ((d = pak_load(g_pak, path, &sz)) != NULL && sz > 12 && !memcmp(d, "EUS1", 4)) {
			s->rate = BE32(d + 4); s->bytes = BE32(d + 8);
			if (s->bytes > 131070) s->bytes = 131070;
			if ((s->chip = AmigaAudio_AllocSample(s->bytes)) != NULL) memcpy(s->chip, d + 12, s->bytes);
		}
		free(d);
	}
	if (!s->chip || s->rate <= 0) return;
	/* effects use channels 0/1 while music plays, all four otherwise; an effect still playing restarts on its own
	   channel (the exe: one buffer per id, SPEC_SOUND 1); else a free channel, else steal round-robin */
	{	int nch = AmigaAudio_MusicActive() ? 2 : 4;
		for (ch = 0; ch < nch; ch++) if (g_ch_sfx[ch] == s && !AmigaAudio_ChannelIdle(ch)) break;
		if (ch == nch)
			for (tries = 0; tries < 4; tries++) {
				ch = g_next_ch++ % nch;
				if (AmigaAudio_ChannelIdle(ch)) break;
			}
		g_ch_sfx[ch] = s;
	}
	{
		long period = (long)amigagfx_paula_clock() / s->rate;
		if (period < 124) period = 124;
		AmigaAudio_Play(ch, s->chip, s->bytes, (int)period, 64);
	}
}

static const char *track_file(const char *base)
{
	static char f[80];
	char *p = g_index;
	size_t bl = strlen(base);
	while (p && *p) {                                    /* "mNN.wav <base>" per line */
		char *sp = strchr(p, ' '), *nl = strchr(p, '\n');
		if (sp && (!nl || sp < nl) && !strncmp(sp + 1, base, bl) && (sp[1 + bl] == '\n' || sp[1 + bl] == '\r' || !sp[1 + bl])) {
			snprintf(f, sizeof f, "PROGDIR:data/music/%.*s", (int)(sp - p), p);
			return f;
		}
		if (!nl) break;
		p = nl + 1;
	}
	return NULL;
}

static void next_track(void)
{
	int tries;
	if (g_stream) { AmigaAudio_MusicStop(); pcm_close(g_stream); g_stream = NULL; }
	for (tries = 0; tries < 8 && g_nlist; tries++) {
		const char *f;
		g_seed = g_seed * 1103515245UL + 12345UL;
		g_cur = (int)((g_seed >> 16) % (unsigned long)g_nlist);
		if ((f = track_file(g_list[g_cur])) != NULL && (g_stream = pcm_open(f)) != NULL) {
			int period = (int)((amigagfx_paula_clock() + (unsigned long)g_stream->rate / 2) / (unsigned long)g_stream->rate);
			if (AmigaAudio_MusicStart(period < 124 ? 124 : period, 8192, music_refill, g_stream)) {
				printf("music: %s (%s), %d Hz\n", g_list[g_cur], f, (int)g_stream->rate); fflush(stdout);
				return;
			}
			pcm_close(g_stream); g_stream = NULL;
		}
	}
}

void sound_music_list(const char *list)
{
	char path[48], *cur, *line;
	unsigned long sz;
	if (!g_on) return;
	free(g_list_buf); g_list_buf = NULL; g_nlist = 0;
	snprintf(path, sizeof path, "music/%s.txt", list);
	if (!(g_list_buf = (char *)pak_load(g_text, path, &sz))) return;    /* playlists live in text.pak */
	cur = g_list_buf;
	while ((line = csv_line(&cur)) != NULL && g_nlist < 96) if (line[0]) g_list[g_nlist++] = line;
	printf("music list %s: %d tracks\n", list, g_nlist); fflush(stdout);
	if (g_seed == 1) g_seed = amigagfx_millis() | 1;
	if (!g_stream) next_track();                          /* else the new list starts with the next track (0x4c93c0) */
}

void sound_service(void)
{
	if (!g_on) return;
	AmigaAudio_MusicService();
	if (g_stream && AmigaAudio_MusicFinished()) next_track();
}

void sound_free(void)
{
	int i;
	if (!g_on) return;
	printf("music: %lu buffer underruns this session\n", AmigaAudio_MusicUnderruns()); fflush(stdout);
	if (g_stream) { AmigaAudio_MusicStop(); pcm_close(g_stream); g_stream = NULL; }
	for (i = 0; i < g_nsfx; i++) AmigaAudio_FreeSample(g_sfx[i].chip);
	AmigaAudio_Close();
	free(g_index); free(g_list_buf);
	g_on = 0;
}
