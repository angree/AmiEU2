/* savegame.c - see savegame.h. The world part is written in the scenario format the loader already
 * reads (a save game of the original has the same shape); everything the port needs to resume
 * exactly goes into `amieu2 = { ... }` sub-blocks and `amieu2_*` top-level blocks as raw integers,
 * which the original's format simply does not have. Portable C, no floats. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "savegame.h"
#include "envoys.h"
#include "revolts.h"

static void put_m(FILE *f, const char *key, long v)           /* x1000 value as "12.345" */
{
	unsigned long a = (unsigned long)(v < 0 ? -v : v);
	fprintf(f, "%s = %s%lu.%03lu ", key, v < 0 ? "-" : "", a / 1000, a % 1000);
}

static void put_date(FILE *f, const char *key, const Date *d)
{
	static const char *mn[12] = { "january", "february", "march", "april", "may", "june", "july", "august",
	                              "september", "october", "november", "december" };
	fprintf(f, "%s = { year = %d month = %s day = %d } ", key, d->year, mn[d->month >= 1 && d->month <= 12 ? d->month - 1 : 0], d->day);
}

static void put_list(FILE *f, const char *key, const World *w, int c, int what)   /* 0 owned 1 controlled 2 cores */
{
	int p, k, n = 0;
	fprintf(f, "\t%s = {", key);
	for (p = 0; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		int in = 0;
		if (pv->id < 0) continue;
		if (what == 0) in = pv->owner == c;
		else if (what == 1) in = pv->controller == c && pv->owner != c;
		else for (k = 0; k < 8; k++) if (pv->cores[k] == c) in = 1;
		if (in) { fprintf(f, "%s%d", n++ % 20 ? " " : "\n\t\t", p); }
	}
	fprintf(f, " }\n");
}

/* a country that is not in the game but controls land or has units (NAT after a native uprising, REB, an occupier
 * without land of its own): saved too, or its provinces come back uncontrolled and its units vanish */
static int holds_something(const World *w, int c)
{
	int i;
	if (c == w->reb || c == w->nat || c == w->pir || w->country[c].vpt_m != 0) return 1;   /* their VP, PIR's tech */
	for (i = 0; i < w->nprov; i++) if (w->prov[i].id >= 0 && w->prov[i].controller == c) return 1;
	for (i = 0; i < w->nland; i++) if (w->land[i].owner == c) return 1;
	for (i = 0; i < w->nnaval; i++) if (w->naval[i].owner == c) return 1;
	return 0;
}

static void save_country(FILE *f, const World *w, int c)
{
	static const char *pk[8] = { "aristocracy", "centralization", "innovative", "mercantilism", "offensive", "land", "quality", "serfdom" };
	const Country *co = &w->country[c];
	int i, k, p;
	fprintf(f, "country = {\n\ttag = %s\n", co->tag);
	if (co->ai_file[0]) fprintf(f, "\tai = \"%s\"\n", co->ai_file);
	fprintf(f, "\tmajor = %s\n\t", co->major ? "yes" : "no");
	if (!co->present) fprintf(f, "amieu2absent = yes\n\t");
	put_m(f, "treasury", co->treasury_m); put_m(f, "merchants", co->merchants_m); put_m(f, "colonists", co->colonists_m);
	put_m(f, "diplomats", co->diplomats_m); put_m(f, "badboy", co->badboy_m); put_m(f, "warexhaustion", co->we_m);
	fprintf(f, "\n\tstability = %d\n\tpolicy = {", co->stability);
	for (k = 0; k < 8; k++) fprintf(f, " %s = %d", pk[k], co->policy[k]);
	fprintf(f, " }\n");
	if (co->religion >= 0) fprintf(f, "\treligion = { type = %s }\n", world_name(w, co->religion));
	if (co->loansize) fprintf(f, "\tloansize = %d\n", co->loansize);
	if (co->flagname[0]) fprintf(f, "\tflagname = \"%s\"\n", co->flagname);
	if (co->culture >= 0) {
		int k;
		fprintf(f, "\tculture = { type = %s", world_name(w, co->culture));
		for (k = 0; k < co->nacc; k++) fprintf(f, " type = %s", world_name(w, co->acc[k]));
		fprintf(f, " }\n");
	}
	{	/* the Ledger's books */
		int b2, k2;
		for (b2 = 0; b2 < 4; b2++) {
			const long *src = b2 == 0 ? co->yinc_m : b2 == 1 ? co->yexp_m : b2 == 2 ? co->linc_m : co->lexp_m;
			int mx = (b2 & 1) ? 16 : 12;
			fprintf(f, "\t%s = {", b2 == 0 ? "yi" : b2 == 1 ? "ye" : b2 == 2 ? "li" : "le");
			for (k2 = 0; k2 < mx; k2++) fprintf(f, " %ld", src[k2]);
			fprintf(f, " }\n");
		}
	}
	fprintf(f, "\tcapital = %d\n", co->capital);
	fprintf(f, "\ttechnology = { land = { level = %d } naval = { level = %d } infra = { level = %d } trade = { level = %d } }\n",
	        co->land_tech, co->naval_tech, co->infra_tech, co->trade_tech);
	fprintf(f, "\tdiplomacy = {\n");
	for (i = 0; i < w->ncountry; i++) {
		long r = world_rel(w, c, i);
		unsigned char fl = w->dipflag[c][i];
		if (i == c || (!fl && (r == 0 || i < c))) continue;             /* one relation entry per pair is enough */
		fprintf(f, "\t\trelation = { tag = %s ", w->country[i].tag);
		put_m(f, "value", r);
		if (fl & DF_CB_PERM) fprintf(f, "casusbelli = { type = permanent } ");
		if (fl & DF_ACCESS) fprintf(f, "access = yes ");
		if (fl & DF_NOTRADE) fprintf(f, "refusetrade = yes ");
		if (fl & DF_WARNED) fprintf(f, "amieu2_warned = yes ");
		if (fl & DF_TRADEAGR) fprintf(f, "tradeagreement = yes ");
		fprintf(f, "}\n");
	}
	fprintf(f, "\t}\n");
	if (co->human) {                                   /* the player's map knowledge (the AI's is not kept) */
		int p, n = 0;
		fprintf(f, "\tknownprovinces = {");
		for (p = 1; p < w->nprov; p++) if (WORLD_KNOWN(w, c, p)) { fprintf(f, "%s%d", n % 20 ? " " : "\n\t\t", p); n++; }
		fprintf(f, " }\n");
	}
	put_list(f, "ownedprovinces", w, c, 0);
	put_list(f, "controlledprovinces", w, c, 1);
	put_list(f, "nationalprovinces", w, c, 2);
	for (p = 0; p < w->nprov; p++) {
		const Province *pv = &w->prov[p];
		if (pv->id < 0 || pv->owner != c) continue;
		fprintf(f, "\tcity = { location = %d population = %ld fortress = { level = %d }%s }\n", p, pv->population, pv->fort,
		        co->capital == p ? " capital = yes" : "");
	}
	for (i = 0; i < w->nland; i++) {
		const LandUnit *u = &w->land[i];
		if (u->owner != c) continue;                     /* dead ones too: the unit order drives the AI (savegame keeps it) */
		fprintf(f, "\tlandunit = { id = { type = 4713 id = %ld } name = \"%s\" location = %d infantry = %ld cavalry = %ld artillery = %ld\n"
		           "\t\tamieu2 = { idx = %d dead = %d aboard = %d lead = { %d %d %d %d %d %d %d } morale = %ld from = %d pathpos = %d arrive = %ld path = {", u->id, u->name ? u->name : "", u->location,
		        u->infantry, u->cavalry, u->artillery, i, u->dead, u->aboard, u->l_id, u->l_rank, u->l_move, u->l_fire, u->l_shock, u->l_siege, u->l_type,
		        u->morale_m, u->from, u->pathpos, u->arrive_day);
		for (k = 0; k < u->npath; k++) fprintf(f, " %d", u->path[k]);
		fprintf(f, " } } }\n");
	}
	for (i = 0; i < w->nnaval; i++) {
		const NavalUnit *u = &w->naval[i];
		if (u->owner != c) continue;
		fprintf(f, "\tnavalunit = { id = { type = 4714 id = %ld } name = \"%s\" location = %d warships = %d galleys = %d transports = %d\n"
		           "\t\tamieu2 = { idx = %d lead = { %d %d %d %d %d } morale = %ld sea = %ld att = %ld pathpos = %d arrive = %ld path = {",
		        u->id, u->name ? u->name : "", u->location, u->warships, u->galleys, u->transports, i, u->l_id, u->l_move, u->l_type, u->l_fire, u->l_shock, u->morale_m, u->sea_since, u->att_m, u->pathpos, u->arrive_day);
		for (k = 0; k < u->npath; k++) fprintf(f, " %d", u->path[k]);
		fprintf(f, " } } }\n");
	}
	fprintf(f, "\tamieu2 = { human = %d vp = %ld manpower = %ld missionaries = %ld inflation = %ld policyday = %ld"
	           " invland = %ld invnaval = %ld invinfra = %ld invtrade = %ld defender = %d convday = %ld bankrupt = %ld wartax = %d bank = %d stock = %d trk = %d evst = { %d %d %d %ld %ld %ld } evrr = { %d %ld } inc = %ld maint = %ld colrate = %ld merrate = %ld vpt = %ld vpc = { %ld %ld %ld %ld %ld %ld %ld } tol = {",
	        co->human, co->vp, co->manpower_m, co->missionaries_m, co->inflation_u, co->policy_day,
	        co->inv_land_m, co->inv_naval_m, co->inv_infra_m, co->inv_trade_m, co->defender, co->conv_day, co->bankrupt_day, co->war_tax, co->bank, co->stock, co->tracked, co->ev_stat[0], co->ev_stat[1], co->ev_stat[2], co->ev_stat_until[0], co->ev_stat_until[1], co->ev_stat_until[2], co->ev_rr, co->ev_rr_until, co->inc_m, co->maint_m, co->col_rate_m, co->mer_rate_m, co->vpt_m,
	        co->vpc_m[0], co->vpc_m[1], co->vpc_m[2], co->vpc_m[3], co->vpc_m[4], co->vpc_m[5], co->vpc_m[6]);
	{ int r; for (r = 0; r < 11; r++) fprintf(f, " %d", co->tol[r]); }
	fprintf(f, " }");
	if (!co->human) {                                     /* the AI's map knowledge, a bitmap */
		int i, n = (w->nprov + 7) / 8;
		fprintf(f, " known = k");
		for (i = 0; i < n; i++) fprintf(f, "%02x", w->known[c][i]);
	}
	fprintf(f, " }\n}\n");
}

static void save_globaldata(FILE *f, const World *w)
{
	static const char *aty[4] = { "", "militaryalliance", "vassalization", "dynasticalliance" };
	int i, j, k;
	fprintf(f, "globaldata = {\n\t");
	put_date(f, "startdate", &w->now);
	put_date(f, "enddate", &w->end);
	{	int k, any = 0;
		for (k = 0; k < 8; k++) if (w->gflag[k]) { fprintf(f, any ? " %d" : " flags = { %d", k); any = 1; }
		if (any) fprintf(f, " }");
		if (w->spec_used) fprintf(f, " amieu2spec = %lu", w->spec_used);
		fprintf(f, " emperor = %s", w->emperor >= 0 && w->emperor < w->ncountry ? w->country[w->emperor].tag : "---");
		if (w->ntcb) { int i; fprintf(f, " amieu2cb = {"); for (i = 0; i < w->ntcb; i++) fprintf(f, " %d %d %ld", w->tcb[i].a, w->tcb[i].b, w->tcb[i].until); fprintf(f, " }"); }
		if (w->nswaps) { int i; fprintf(f, " amieu2swaps = {"); for (i = 0; i < w->nswaps; i++) fprintf(f, " %d %d", w->swaps[2 * i], w->swaps[2 * i + 1]); fprintf(f, " }"); }
		if (w->nleadev) { int i; fprintf(f, " amieu2leadev = {"); for (i = 0; i < w->nleadev; i++) fprintf(f, " %ld %ld %d", w->leadev_id[i], w->leadev_day[i], w->leadev_kind[i]); fprintf(f, " }"); }
		if (w->nmonev) { int i; fprintf(f, " amieu2monev = {"); for (i = 0; i < w->nmonev; i++) fprintf(f, " %d %ld %ld %d", w->monev_c[i], w->monev_id[i], w->monev_day[i], w->monev_kind[i]); fprintf(f, " }"); }
		{	int i, n = (int)sizeof w->lead_killed;
			while (n > 0 && !w->lead_killed[n - 1]) n--;
			if (n) { fprintf(f, " amieu2killed = k"); for (i = 0; i < n; i++) fprintf(f, "%02x", w->lead_killed[i]); }
		}
	}
	fprintf(f, "\n");
	for (i = 0; i < w->nally; i++) {
		const Alliance *al = &w->ally[i];
		fprintf(f, "\talliance = { type = %s ", aty[al->type < 4 ? al->type : 0]);
		put_date(f, "expirydate", &al->expiry);
		fprintf(f, "participant = {");
		for (k = 0; k < al->n; k++) fprintf(f, " %s", w->country[al->member[k]].tag);
		fprintf(f, " } }\n");
	}
	for (i = 0; i < w->nwar; i++) {
		const War *wr = &w->war[i];
		fprintf(f, "\twar = { ");
		put_date(f, "date", &wr->began);
		fprintf(f, "\n\t\tattackers = { type = war participant = {");
		for (k = 0; k < wr->natt; k++) fprintf(f, " %s", w->country[wr->att[k]].tag);
		fprintf(f, " } }\n\t\tdefenders = { type = war participant = {");
		for (k = 0; k < wr->ndef; k++) fprintf(f, " %s", w->country[wr->def[k]].tag);
		fprintf(f, " } }\n\t\tamieu2 = { start = %ld last = %ld bpa = {", wr->start_day, wr->last_combat);
		for (j = 0; j < wr->natt; j++) for (k = 0; k < wr->ndef; k++) if (wr->bp_att[j][k]) fprintf(f, " %d %d %d", j, k, wr->bp_att[j][k]);
		fprintf(f, " } bpd = {");
		for (j = 0; j < wr->ndef; j++) for (k = 0; k < wr->natt; k++) if (wr->bp_def[j][k]) fprintf(f, " %d %d %d", j, k, wr->bp_def[j][k]);
		fprintf(f, " } }\n\t}\n");
	}
	fprintf(f, "}\n");
}

static void (*g_extra_save)(FILE *f);
static void (*g_extra_restore)(const SNode *root);
void save_set_extra(void (*save)(FILE *f), void (*restore)(const SNode *root)) { g_extra_save = save; g_extra_restore = restore; }

int save_world(FILE *f, const World *w)
{
	int i, c;
	fprintf(f, "# AmiEU2 save game - scenario format plus amieu2 blocks (integers, exact)\n");
	if (w->has_events) fprintf(f, "include = \"db\\events.txt\"\n");   /* the events (absent in the tutorials) */
	save_globaldata(f, w);
	for (i = 0; i < w->nprov; i++) {
		const Province *p = &w->prov[i];
		if (p->id < 0) continue;
		fprintf(f, "province = { id = %d", i);
		if (p->religion >= 0) fprintf(f, " religion = { type = %s }", world_name(w, p->religion));
		if (p->culture >= 0) fprintf(f, " culture = { type = %s }", world_name(w, p->culture));
		fprintf(f, " natives = %ld amieu2 = { income = %ld manpower = %ld revolt = %ld population = %ld popfrac = %ld fort = %d tp = %d bf = %d manu = %d acq = %d ctl = %ld nmax = %d cq = %d wx = %d lt = %ld lc = %d lo = %d ch = %d tc = %d } }\n",
		        p->natives, p->income_m, p->manpower_m, p->revoltrisk_m, p->population, p->pop_frac_m, p->fort, p->tp, p->bflags, p->manu, p->acq_year, p->ctl_day, p->maxnatives, p->conq_done, p->wind | p->storm_now << 4 | p->winter << 5, p->looted ? p->loot_until : 0L, p->last_ctl != p->controller ? p->last_ctl + 2 : 0, p->last_owner != p->owner ? p->last_owner + 2 : 0, p->cot_hist, p->tcot > 0 ? p->tcot : 0);
	}
	for (c = 0; c < w->ncountry; c++)
		if (w->country[c].present || w->country[c].nprov_owned > 0 || holds_something(w, c)) save_country(f, w, c);
	for (i = 0; i < w->ncot; i++) {                      /* centres of trade with their merchants */
		int k;
		fprintf(f, "cot = { location = %d failure = %ld.%03ld amieu2fail = %ld", w->cot[i].prov, w->cot[i].fail / 12000, w->cot[i].fail % 12000 / 12, w->cot[i].fail);
		for (k = 0; k < w->cot[i].n; k++) fprintf(f, " merchant = { tag = %s level = %d }", w->country[w->cot[i].tag[k]].tag, w->cot[i].level[k]);
		fprintf(f, " }\n");
	}
	fprintf(f, "header = { name = \"%s\"", w->scenario_name);
	if (w->tutorial) fprintf(f, " tutorial = yes tutorialfile = \"%s\"", w->tutfile);
	if (w->optmode) fprintf(f, " optionmode = 1 set_difficulty = %d set_ai_aggresive = %d set_gamespeed = %d set_basevp = %d set_missions = %d",
	                        w->set_opt[0], w->set_opt[1], w->set_opt[2], w->set_opt[3], w->set_opt[4]);
	fprintf(f, " }\n");
	fprintf(f, "amieu2_buildq = {");                       /* the build queue in its order (the order drives builds_day):
	                                                         * tag prov kind(0 troops, 1..3 ships, 4 fort, 5 mission) inf cav art done */
	for (i = 0; i < w->nbuild; i++) {
		const Build *b = &w->build[i];
		fprintf(f, "\n\t%s %d %d %ld %ld %ld %ld", w->country[b->country].tag, b->prov, b->ship, b->inf, b->cav, b->art, b->done);
	}
	fprintf(f, " }\n");
	return ferror(f) ? -1 : 0;
}

int save_game(const char *path, const World *w, const Economy *ec, const Events *ev, const Military *mil, const Peace *pc, const Diplo *dp)
{
	FILE *f = fopen(path, "w");
	int rc;
	if (!f) return -1;
	rc = save_world(f, w);
	if (ec) econ_save(ec, f);
	if (ev) events_save(ev, f);
	if (mil) mil_save(mil, f);
	if (pc) peace_save(pc, f);
	if (dp) diplo_save(dp, f);
	env_save(f);
	rev_save(f);
	if (g_extra_save) g_extra_save(f);
	if (ferror(f)) rc = -1;
	if (fclose(f)) rc = -1;
	return rc;
}

int restore_game(const char *path, Economy *ec, Events *ev, Military *mil, Peace *pc, Diplo *dp)
{
	unsigned long sz;
	char *buf = world_read_file(path, &sz), err[80];
	Arena *a;
	SNode *root;
	if (!buf) return -1;
	if (!(a = arena_new(65536))) { free(buf); return -1; }
	if (!(root = script_parse(buf, sz, a, err, sizeof err))) { arena_free(a); free(buf); return -1; }
	if (ec) econ_restore(ec, root);
	if (ev) events_restore(ev, root);
	if (mil) mil_restore(mil, root);
	if (pc) peace_restore(pc, root);
	if (dp) diplo_restore(dp, root);
	env_restore(root);
	rev_restore(root);
	if (g_extra_restore) g_extra_restore(root);
	arena_free(a);
	free(buf);
	return 0;
}
