/* test_ui.c - host: the full main screen (map + units + sidebar + top bar) at 800x600 and 640x480,
 * written as PPM. test_ui <text.pak> <map.pak> <gfx.pak> <outprefix> */
#include <stdio.h>
#include <stdlib.h>
#include "mapview.h"
#include "units.h"
#include "eui.h"
#include "ui.h"
#include "text.h"
#include "frontend.h"
#include <string.h>

static World w;

int main(int argc, char **argv)
{
	Pak text, map, gfx;
	static unsigned char inv[EUI_INV_SIZE];
	static unsigned char fb[800 * 600];
	char err[160], name[256];
	MapView *v;
	int k;
	static const int sizes[2][2] = { { 800, 600 }, { 640, 480 } };
	if (argc < 5) return 2;
	if (pak_open(&text, argv[1]) || pak_open(&map, argv[2]) || pak_open(&gfx, argv[3])) { printf("packs\n"); return 1; }
	if (world_load_db(&w, &text, err, sizeof err) || world_load_scenario(&w, &text, "scenarios/the grand campaign.eug", err, sizeof err)) { printf("%s\n", err); return 1; }
	text_load(&text);
	if (!(v = mv_create(&text, &map, &w, 0, err, sizeof err))) { printf("%s\n", err); return 1; }
	eui_build_inverse(mv_palette(v), 256, inv);
	units_init(&text, &gfx, inv);
	for (k = 0; k < 2; k++) {
		int sw = sizes[k][0], sh = sizes[k][1], mx, my, mw, mh, i;
		UiState st = { "1 January 1419", 1, -1, 386, 1, 0, 1, 0, 1, 27, 400 };
		const unsigned char *pal = mv_palette(v);
		FILE *f;
		ui_set_font_dir("fonts/");
		ui_init(&gfx, &text, pal, inv, sw, sh);
		ui_map_rect(&mx, &my, &mw, &mh);
		mv_set(v, 9500, 1900, 2);
		mv_draw(v, fb + mx, sw, mw, mh);
		units_draw(&w, v, fb + mx, sw, mw, mh, 0);
		st.player = world_country(&w, "FRA");
		ui_draw(fb, sw, &w, &st);
		snprintf(name, sizeof name, "%s_%d.ppm", argv[4], sw);
		f = fopen(name, "wb");
		fprintf(f, "P6 %d %d 255\n", sw, sh);
		for (i = 0; i < sw * sh; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
		fclose(f);
		printf("%dx%d: map at x=%d, %dx%d; click at (40,590) -> button %d\n", sw, sh, mx, mw, mh, ui_hit(sw == 800 ? 40 : 32, sw == 800 ? 590 : 472));
		{	/* frontend: main menu and the single-player setup with the real scenario list */
			static FrontScen sc[FRONT_MAXSCEN];
			UiFront fr;
			int ns = front_scenarios(&text, sc, FRONT_MAXSCEN), q, e;
			memset(&fr, 0, sizeof fr);
			for (q = 0; q < ns; q++) fr.scen_name[q] = sc[q].name;
			fr.nscen = ns; fr.scen = ns > 2 ? 2 : 0; fr.sel = sc[fr.scen].sel; fr.nsel = sc[fr.scen].nsel; fr.country = 0;
			fr.has_save = 1; fr.hover = UF_SINGLE; fr.country_name = text_get(sc[fr.scen].sel[0]); fr.info = "Start 1419";
			printf("  frontend: %d scenarios, the third: %s (%d), %d selectable\n", ns, ns > 2 ? sc[2].name : "-", ns > 2 ? sc[2].startyear : 0, fr.nsel);
			for (e = 0; e < 2; e++) {
				fr.screen = e;
				ui_front_draw(fb, sw, &fr);
				printf("  frontend %d: hit (400,%d) -> %d\n", e, sw == 800 ? 345 : 276, ui_front_hit(sw == 800 ? 400 : 320, sw == 800 ? 345 : 276, &fr));
				snprintf(name, sizeof name, "%s_front%d_%d.ppm", argv[4], e, sw);
				f = fopen(name, "wb");
				fprintf(f, "P6 %d %d 255\n", sw, sh);
				for (i = 0; i < sw * sh; i++) fwrite((ui_front_palette() ? ui_front_palette() : pal) + fb[i] * 3, 1, 3, f);
				fclose(f);
			}
		}
		{	/* budget window with made-up figures */
			EconBudget b;
			static const short sl[ECON_SL_N] = { 200, 100, 150, 150, 150, 250 };
			int q, v = -1, row;
			for (q = 0; q < ECON_SL_N; q++) { b.slider[q] = sl[q]; b.progress_m[q] = 1234000L * (q + 1); b.cost_m[q] = q == 5 ? 0 : 9000000L; }
			b.income_m = 31400;
			st.infowin = UI_IW_BUDGET; st.budget = &b;
			ui_draw(fb, sw, &w, &st);
			row = ui_budget_hit(sw == 800 ? 120 : 96, sw == 800 ? 103 + 30 + 47 + 4 : 82 + 24 + 37 + 3, &v);
			printf("  budget: hit row %d value %d\n", row, v);
			snprintf(name, sizeof name, "%s_budget_%d.ppm", argv[4], sw);
			f = fopen(name, "wb");
			fprintf(f, "P6 %d %d 255\n", sw, sh);
			for (i = 0; i < sw * sh; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
			fclose(f);
			st.infowin = UI_IW_DOMESTIC; st.domestic_note = "Next change in 1429";
			ui_draw(fb, sw, &w, &st);
			printf("  domestic: hit at minus of row 2 -> %d\n", ui_domestic_hit(sw == 800 ? 20 : 16, sw == 800 ? 103 + 16 + 72 + 12 : 82 + 13 + 58 + 10));
			snprintf(name, sizeof name, "%s_domestic_%d.ppm", argv[4], sw);
			f = fopen(name, "wb");
			fprintf(f, "P6 %d %d 255\n", sw, sh);
			for (i = 0; i < sw * sh; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
			fclose(f);
			st.infowin = UI_IW_PROVINCE; st.budget = NULL;
		}
		{	/* event windows: 3 choices (hover on B) and a single OK */
			static const char *const a3[4] = { "ACTIONNAME3850A", "ACTIONNAME3850B", "ACTIONNAME3850B", NULL };
			const char *t3[4];
			int e, q, hx = -1, hy = -1;
			for (e = 0; e < 2; e++) {
				mv_draw(v, fb + mx, sw, mw, mh);
				units_draw(&w, v, fb + mx, sw, mw, mh, 0);
				ui_draw(fb, sw, &w, &st);
				for (q = 0; q < 4; q++) t3[q] = e == 0 ? (a3[q] ? text_get(a3[q]) : NULL) : (q == 0 ? text_get("OK") : NULL);
				if (e == 0) t3[2] = "Third choice";
				ui_event_draw(fb, sw, text_get(e == 0 ? "EVENTNAME3850" : "EVENTNAME101"), "1 May 1420",
				              text_get(e == 0 ? "EVENTHIST3850" : "EVENTHIST101"), t3, e == 0 ? 1 : -1, 0);
				for (hy = 0; hy < sh && hx < 0; hy += 4)       /* find a pixel on button 0 */
					for (q = 0; q < sw; q += 4) if (ui_event_hit(q, hy) == 0) { hx = q; break; }
				printf("  event %d: first pixel hitting action A at (%d,%d)\n", e, hx, hy - 4);
				hx = hy = -1;
				snprintf(name, sizeof name, "%s_ev%d_%d.ppm", argv[4], e, sw);
				f = fopen(name, "wb");
				fprintf(f, "P6 %d %d 255\n", sw, sh);
				for (i = 0; i < sw * sh; i++) fwrite(pal + fb[i] * 3, 1, 3, f);
				fclose(f);
			}
		}
		ui_free();
	}
	mv_destroy(v);
	world_free(&w);
	return 0;
}
