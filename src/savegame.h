/* savegame.h - saving and loading the game. The world goes out in the scenario format, so loading is
 * world_load_db + world_load_scenario(path on disk) like a new game; every module then restores its
 * own amieu2_* block with restore_game (after the modules were created as for a new game). */
#ifndef EU2_SAVEGAME_H
#define EU2_SAVEGAME_H
#include <stdio.h>
#include "world.h"
#include "economy.h"
#include "events.h"
#include "military.h"
#include "peace.h"
#include "diplo.h"

int save_world(FILE *f, const World *w);      /* 0 = ok */
/* any module pointer may be NULL (not saved / not restored) */
int save_game(const char *path, const World *w, const Economy *ec, const Events *ev, const Military *mil, const Peace *pc, const Diplo *dp);
int restore_game(const char *path, Economy *ec, Events *ev, Military *mil, Peace *pc, Diplo *dp);
/* extra blocks of the game front end (the Ledger history): written after the modules, read back on load */
void save_set_extra(void (*save)(FILE *f), void (*restore)(const SNode *root));
#endif
