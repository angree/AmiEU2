/* test_econ.c - Grand Campaign with events + economy, day by day. First prints France's first month
 * next to the worked example of docs/SPEC_TIME_ECONOMY.md §12, then a table every 20 years.
 * Host: test_econ <text.pak> <years>   Amiga: PROGDIR:data/text.pak, 50 years, log file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "world.h"
#include "events.h"
#include "economy.h"
#include "calendar.h"

static World w;
static FILE *out;

#define M3(x) (long)((x) / 1000), (long)(((x) < 0 ? -(x) : (x)) % 1000)

static void table(Economy *ec, int year)
{
	static const char *tags[] = { "FRA", "ENG", "CAS", "TUR", "MOS", "VEN", "POR" };
	int k;
	fprintf(out, "-- %d: tag  treasury  infl%%   stab  land nav inf trd  manpower  tax/m   prod/m  tolls/m  trade/m  maint/m  capital pop  loans/bankr\n", year);
	for (k = 0; k < 7; k++) {
		int c = world_country(&w, tags[k]);
		EconReport r;
		const Country *co;
		if (c < 0) continue;
		co = &w.country[c];
		econ_report(ec, c, &r);
		fprintf(out, "   %s %8ld %3ld.%02ld %5d  %3d %3d %3d %3d %6ld.%01ld %4ld.%03ld %4ld.%03ld %4ld.%03ld %4ld.%03ld %4ld.%03ld  %7ld  %d/%ld\n", co->tag,
		        co->treasury_m / 1000, co->inflation_u / 10000, (co->inflation_u / 100) % 100, co->stability,
		        co->land_tech, co->naval_tech, co->infra_tech, co->trade_tech, co->manpower_m / 1000, (co->manpower_m % 1000) / 100,
		        M3(r.tax), M3(r.production), M3(r.tolls), M3(r.trade), M3(r.maintenance),
		        co->capital > 0 ? w.prov[co->capital].population : 0, r.loans, r.bankruptcies);
	}
}

int main(int argc, char **argv)
{
	Pak t;
	char err[160];
	Events *ev;
	Economy *ec;
	long day, start, end;
	int years = argc > 2 ? atoi(argv[2]) : 50, lastm, lasty;
	const char *path = argc > 1 ? argv[1] : "PROGDIR:data/text.pak";
	clock_t c0;
	out = stdout;
	if (argc <= 1) { remove("PROGDIR:test_econ.log"); out = fopen("PROGDIR:test_econ.log", "w"); if (!out) return 20; }
	if (pak_open(&t, path)) { fprintf(out, "cannot open %s\n", path); return 20; }
	if (world_load_db(&w, &t, err, sizeof err) || world_load_scenario(&w, &t, "scenarios/the grand campaign.eug", err, sizeof err)) {
		fprintf(out, "world: %s\n", err); return 20;
	}
	start = cal_day(w.start.year, w.start.month, w.start.day);
	if (!(ev = events_load(&w, &t, start, 12345, err, sizeof err))) { fprintf(out, "events: %s\n", err); return 20; }
	if (!(ec = econ_load(&w, &t, err, sizeof err))) { fprintf(out, "economy: %s\n", err); return 20; }
	table(ec, w.start.year);
	end = start + (long)years * 360;
	lastm = w.start.month; lasty = w.start.year;
	c0 = clock();
	for (day = start; day < end; day++) {
		int y, m, d;
		cal_date(day, &y, &m, &d);
		w.now.year = (short)y; w.now.month = (unsigned char)m; w.now.day = (unsigned char)d;
		if (m != lastm) {
			econ_month(ec);
			if (y != lasty) econ_year(ec);
			if (day == start + 30) {                     /* France after its first month (§12) */
				int c = world_country(&w, "FRA");
				EconReport r;
				econ_report(ec, c, &r);
				fprintf(out, "FRA month 1: tax %ld.%03ld (spec 7.83), production %ld.%03ld (spec 9.4/12 = 0.78 if the policy polarity holds),"
				        " manpower rate %ld.%03ld/yr (spec 13.39), maintenance %ld.%03ld (spec 2.25+1.225+1.04+0.03 = 4.55)\n",
				        M3(r.tax), M3(r.production), M3(r.manpower_rate_m), M3(r.maintenance));
			}
			if (y != lasty && (y - w.start.year) % 20 == 0) table(ec, y);
			lastm = m; lasty = y;
		}
		events_day(ev, day);
	}
	fprintf(out, "%d years simulated in %ld ms\n", years, (long)((clock() - c0) * 1000 / CLOCKS_PER_SEC));
	econ_free(ec);
	events_free(ev);
	world_free(&w);
	pak_close(&t);
	if (out != stdout) fclose(out);
	return 0;
}
