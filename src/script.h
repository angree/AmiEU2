/* script.h - parser for EU2's text format (Paradox "key = value", "{ }" blocks, "#" comments)
 * and for its ';'-separated CSV tables. Portable C, no floats.
 *
 * script_parse() tokenises IN PLACE: it writes NULs into the text buffer and every key/value
 * points into it, so the buffer must outlive the tree. Nodes come from an Arena that is freed in
 * one go - parse a file, turn it into game structures, free both. */
#ifndef EU2_SCRIPT_H
#define EU2_SCRIPT_H

typedef struct Arena Arena;
Arena *arena_new(unsigned long block);
void  *arena_alloc(Arena *a, unsigned long n);
void   arena_free(Arena *a);
unsigned long arena_used(const Arena *a);

typedef struct SNode {
	const char *key;           /* NULL for a bare value inside a list: { 357 358 359 } */
	const char *val;           /* scalar value, or NULL when the node is a block */
	struct SNode *child;       /* first child of a block */
	struct SNode *next;        /* next sibling */
	unsigned line;
} SNode;

/* returns a synthetic root block (key NULL) whose children are the file's top-level entries;
 * NULL on error with a message in err ("line N: ...") */
SNode *script_parse(char *text, unsigned long len, Arena *a, char *err, int errlen);

/* data-quirk repairs made so far (stray braces, empty values, missing closing brace) */
unsigned long script_warnings(void);

int   script_ieq(const char *a, const char *b);            /* ASCII case-insensitive equality */
SNode *script_get(const SNode *block, const char *key);   /* first child named key, or NULL */
SNode *script_next(const SNode *after, const char *key);  /* next sibling named key after 'after' */
const char *script_str(const SNode *block, const char *key, const char *dflt);
long  script_int(const SNode *block, const char *key, long dflt);
long  script_milli(const SNode *block, const char *key, long dflt);   /* value x1000 */

/* "0.083333" -> 83, "-1.5" -> -1500, "12" -> 12000; rounds half away from zero at 3 decimals.
 * Never uses the C library number parsers (locale.library decimal comma, no FPU). */
long  parse_milli(const char *s);
long  parse_int(const char *s);
int   parse_yes(const char *s);                            /* yes/no -> 1/0 */

/* CSV: splits one line in place on ';' into up to max fields; returns the field count.
 * *cursor walks the buffer line by line; returns NULL at the end. */
char *csv_line(char **cursor);
int   csv_split(char *line, char **fields, int max);
#endif
