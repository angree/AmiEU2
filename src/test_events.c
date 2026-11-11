/* test_events.c - loads the Grand Campaign + all events and runs the event system day by day.
 * Host: test_events <text.pak> <years> [quiet]   Amiga: PROGDIR:data/text.pak, 20 years, log file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "world.h"
#include "events.h"
#include "calendar.h"
#include "text.h"

static World w;
static FILE *out;
static int quiet;
static unsigned long hsum = 0;

static void logfn(void *u, long today, int c, long id, int act, const char *name)
{
	int y, m, d;
	(void)u;
	hsum = hsum * 31 + (unsigned long)id * 7 + (unsigned long)c + (unsigned long)act;
	if (quiet) return;
	cal_date(today, &y, &m, &d);
	fprintf(out, "%04d-%02d-%02d %s event %ld action %c %s\n", y, m, d + 1, w.country[c].tag, id, 'A' + act, text_get(name));
}

int main(int argc, char **argv)
{
	Pak t;
	char err[160];
	Events *e;
	EventStats st;
	long day, start, end;
	int years = argc > 2 ? atoi(argv[2]) : 20, i;
	const char *path = argc > 1 ? argv[1] : "PROGDIR:data/text.pak";
	clock_t c0;
	out = stdout;
	quiet = argc > 3;
	if (argc <= 1) { remove("PROGDIR:test_events.log"); out = fopen("PROGDIR:test_events.log", "w"); if (!out) return 20; }
	if (pak_open(&t, path)) { fprintf(out, "cannot open %s\n", path); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, "scenarios/the grand campaign.eug", err, sizeof err)) {
		fprintf(out, "world: %s\n", err); return 20;
	}
	text_load(&t);
	fprintf(out, "text: %d strings\n", text_count());
	start = cal_day(w.start.year, w.start.month, w.start.day);
	c0 = clock();
	e = events_load(&w, &t, start, 12345, err, sizeof err);
	if (!e) { fprintf(out, "events: %s\n", err); return 20; }
	events_stats(e, &st);
	fprintf(out, "events: %d files, %d events (%d random, %d dated queued), load %ld ms\n", st.files, st.events, st.random, st.dated,
	        (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC));
	events_set_log(e, logfn, NULL);
	end = start + (long)years * 360;
	c0 = clock();
	for (day = start; day < end; day++) events_day(e, day);
	events_stats(e, &st);
	fprintf(out, "%d years: fired %ld (random %ld), retries %ld, dropped cmds %ld, executed %ld, not yet implemented %ld, cap hits %ld, %ld ms\n",
	        years, st.fired, st.fired_random, st.retries, st.dropped_cmds, st.executed_cmds, st.unimplemented_cmds, st.cap_hits,
	        (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC));
	fprintf(out, "checksum %08lx\n", hsum & 0xFFFFFFFFUL);
	for (i = 0; i < w.ncountry; i++)
		if (!strcmp(w.country[i].tag, "FRA") || !strcmp(w.country[i].tag, "ENG") || !strcmp(w.country[i].tag, "CAS"))
			fprintf(out, "%s: stability %d, treasury %ld, inflation %ld.%02ld%%, provinces %d\n", w.country[i].tag, w.country[i].stability,
			        w.country[i].treasury_m / 1000, w.country[i].inflation_u / 10000, (w.country[i].inflation_u / 100) % 100, w.country[i].nprov_owned);
	events_free(e);
	world_free(&w);
	pak_close(&t);
	if (out != stdout) fclose(out);
	return 0;
}
