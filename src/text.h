/* text.h - the game's string database (config/text.csv: KEY;English;...). Portable C. */
#ifndef EU2_TEXT_H
#define EU2_TEXT_H
#include "pak.h"
int  text_load(Pak *text);                 /* 0 = ok */
void text_free(void);
/* the English string for key (case-insensitive), or key itself when there is none */
const char *text_get(const char *key);
int  text_count(void);
#endif
