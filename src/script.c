/* script.c - see script.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "script.h"

/* ---------------------------------------------------------------- arena */
typedef struct ABlock { struct ABlock *next; unsigned long used, size; } ABlock;
struct Arena { ABlock *head; unsigned long block, total; };

Arena *arena_new(unsigned long block)
{
	Arena *a = calloc(1, sizeof *a);
	if (a) a->block = block < 4096 ? 4096 : block;
	return a;
}

void *arena_alloc(Arena *a, unsigned long n)
{
	ABlock *b = a->head;
	n = (n + sizeof(void *) - 1) & ~(unsigned long)(sizeof(void *) - 1);   /* pointer alignment (8 on the host) */
	if (!b || b->used + n > b->size) {
		unsigned long sz = n > a->block ? n : a->block;
		b = malloc(sizeof *b + sz);
		if (!b) return NULL;
		b->next = a->head; b->used = 0; b->size = sz;
		a->head = b;
	}
	b->used += n;
	a->total += n;
	return (char *)(b + 1) + b->used - n;
}

void arena_free(Arena *a)
{
	ABlock *b, *n;
	if (!a) return;
	for (b = a->head; b; b = n) { n = b->next; free(b); }
	free(a);
}

unsigned long arena_used(const Arena *a) { return a->total; }

/* ------------------------------------------------------------- tokeniser */
/* Two passes. Pass 1 turns the whole buffer into a token array and only THEN writes the NULs
 * (a delimiter right after a word, "tag=ADE", is recorded as its own token before its byte is
 * overwritten), so nothing ever re-reads a byte that has been changed. Pass 2 builds the tree. */
enum { T_WORD, T_EQ, T_OPEN, T_CLOSE };
typedef struct { char *s; unsigned short type; unsigned short pad; unsigned line; } Tok;

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0; }
static int is_delim(char c) { return c == '=' || c == '{' || c == '}' || c == '#' || c == '"'; }

static Tok *tokenise(char *p, char *end, unsigned long *ntok, char *err, int errlen)
{
	unsigned long cap = (unsigned long)(end - p) / 4 + 64, n = 0;
	unsigned line = 1;
	Tok *t = malloc(cap * sizeof *t);
	char **zero;               /* bytes to NUL after the scan */
	unsigned long nz = 0;
	if (!t) { snprintf(err, errlen, "out of memory"); return NULL; }
	zero = malloc(cap * sizeof *zero);
	if (!zero) { free(t); snprintf(err, errlen, "out of memory"); return NULL; }
#define PUSH(ty, ptr) do { t[n].type = (ty); t[n].s = (ptr); t[n].line = line; n++; } while (0)
	for (;;) {
		if (n + 4 > cap || nz + 4 > cap) {
			Tok *nt; char **nzz;
			cap *= 2;
			nt = realloc(t, cap * sizeof *t); nzz = realloc(zero, cap * sizeof *zero);
			if (!nt || !nzz) { free(nt ? nt : t); free(nzz ? nzz : zero); snprintf(err, errlen, "out of memory"); return NULL; }
			t = nt; zero = nzz;
		}
		while (p < end && is_space(*p)) { if (*p == '\n') line++; p++; }
		if (p >= end) break;
		if (*p == '#') { while (p < end && *p != '\n') p++; continue; }
		if (*p == '=') { PUSH(T_EQ, p); p++; continue; }
		if (*p == '{') { PUSH(T_OPEN, p); p++; continue; }
		if (*p == '}') { PUSH(T_CLOSE, p); p++; continue; }
		if (*p == '"') {
			char *s = ++p;
			while (p < end && *p != '"') { if (*p == '\n') line++; p++; }
			PUSH(T_WORD, s);
			if (p < end) { zero[nz++] = p; p++; }        /* closing quote becomes the terminator */
			else { snprintf(err, errlen, "line %u: unterminated string", line); free(t); free(zero); return NULL; }
			continue;
		}
		{
			char *s = p;
			while (p < end && !is_space(*p) && !is_delim(*p)) p++;
			if (p - s == 1 && *s == '(') { PUSH(T_OPEN, s); continue; }    /* revolt.txt: "minimum = ( 45 46 }" */
			PUSH(T_WORD, s);
			if (p < end) zero[nz++] = p;     /* whitespace or a delimiter already tokenised... */
			else zero[nz++] = NULL;          /* ...or end of buffer: the caller's extra byte */
		}
	}
#undef PUSH
	{	/* a word that ended on a delimiter: its terminator byte is that delimiter, which is already
		 * a token of its own - safe to overwrite now. A word that ends the buffer uses end[0]
		 * (pak_load always allocates one spare byte and sets it to 0). */
		unsigned long i;
		for (i = 0; i < nz; i++) { if (zero[i]) *zero[i] = 0; else *end = 0; }
	}
	free(zero);
	*ntok = n;
	return t;
}

static SNode *new_node(Arena *a, const char *k, const char *v, unsigned line)
{
	SNode *n = arena_alloc(a, sizeof *n);
	if (n) { n->key = k; n->val = v; n->child = n->next = NULL; n->line = line; }
	return n;
}

static unsigned long g_warnings;
unsigned long script_warnings(void) { return g_warnings; }

SNode *script_parse(char *text, unsigned long len, Arena *a, char *err, int errlen)
{
	enum { MAXDEPTH = 64 };
	SNode *stack[MAXDEPTH], *last[MAXDEPTH];
	int depth = 0;
	unsigned long nt, i;
	Tok *t = tokenise(text, text + len, &nt, err, errlen);
	SNode *root = new_node(a, NULL, NULL, 1);
	if (!t) return NULL;
	if (!root) { free(t); snprintf(err, errlen, "out of memory"); return NULL; }
	stack[0] = root; last[0] = NULL;
#define ADD(n) do { if (last[depth]) last[depth]->next = (n); else stack[depth]->child = (n); last[depth] = (n); } while (0)
#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); free(t); return NULL; } while (0)
	for (i = 0; i < nt; i++) {
		Tok *k = &t[i];
		SNode *n;
		if (k->type == T_CLOSE) {
			if (depth == 0) { g_warnings++; continue; }       /* stray '}' (revolt.txt): the original skipped it */
			depth--;
			continue;
		}
		if (k->type == T_OPEN) {                                /* anonymous block: { { ... } { ... } } */
			if (!(n = new_node(a, NULL, NULL, k->line)) || depth + 1 >= MAXDEPTH) FAIL("line %u: too deep / no memory", k->line);
			ADD(n); depth++; stack[depth] = n; last[depth] = NULL;
			continue;
		}
		if (k->type == T_EQ) { g_warnings++; continue; }        /* '=' with no key: skip it */
		/* word */
		if (i + 1 < nt && t[i + 1].type == T_EQ) {
			if (i + 2 >= nt) { g_warnings++; break; }
			if (t[i + 2].type == T_OPEN) {
				if (!(n = new_node(a, k->s, NULL, k->line)) || depth + 1 >= MAXDEPTH) FAIL("line %u: too deep / no memory", k->line);
				ADD(n); depth++; stack[depth] = n; last[depth] = NULL;
				i += 2;
				continue;
			}
			if (t[i + 2].type == T_WORD && !(i + 3 < nt && t[i + 3].type == T_EQ)) {
				if (!(n = new_node(a, k->s, t[i + 2].s, k->line))) FAIL("out of memory");
				ADD(n);
				i += 2;
				continue;
			}
			/* "location = \n negotiation = 0" (leaders.txt): empty value, next word starts a new pair */
			if (!(n = new_node(a, k->s, "", k->line))) FAIL("out of memory");
			ADD(n);
			g_warnings++;
			i += 1;
			continue;
		}
		if (!(n = new_node(a, NULL, k->s, k->line))) FAIL("out of memory");   /* bare list value */
		ADD(n);
	}
#undef ADD
#undef FAIL
	if (depth != 0) g_warnings++;                           /* missing '}' at end of file: closed implicitly */
	free(t);
	return root;
}

/* ------------------------------------------------------------- queries */
int script_ieq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return 0;
	}
	return *a == *b;
}

SNode *script_get(const SNode *b, const char *key)
{
	SNode *n;
	if (!b) return NULL;
	for (n = b->child; n; n = n->next) if (n->key && script_ieq(n->key, key)) return n;
	return NULL;
}

SNode *script_next(const SNode *after, const char *key)
{
	SNode *n;
	for (n = after ? after->next : NULL; n; n = n->next) if (n->key && script_ieq(n->key, key)) return n;
	return NULL;
}

const char *script_str(const SNode *b, const char *key, const char *dflt)
{
	SNode *n = script_get(b, key);
	return n && n->val ? n->val : dflt;
}

long script_int(const SNode *b, const char *key, long dflt)
{
	SNode *n = script_get(b, key);
	return n && n->val ? parse_int(n->val) : dflt;
}

long script_milli(const SNode *b, const char *key, long dflt)
{
	SNode *n = script_get(b, key);
	return n && n->val ? parse_milli(n->val) : dflt;
}

/* -------------------------------------------------------------- numbers */
long parse_int(const char *s)
{
	long v = 0;
	int neg = 0;
	while (*s == ' ') s++;
	if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

long parse_milli(const char *s)
{
	long ip = 0, frac = 0;
	int neg = 0, digits = 0, round = 0;
	while (*s == ' ') s++;
	if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
	if (*s == '.' || *s == ',') {
		s++;
		while (*s >= '0' && *s <= '9') {
			if (digits < 3) { frac = frac * 10 + (*s - '0'); digits++; }
			else if (digits == 3) { round = *s >= '5'; digits++; }
			s++;
		}
	}
	while (digits < 3) { frac *= 10; digits++; }
	{
		long v = ip * 1000 + frac + round;
		return neg ? -v : v;
	}
}

int parse_yes(const char *s) { return s && (s[0] == 'y' || s[0] == 'Y'); }

/* ---------------------------------------------------------------- CSV */
char *csv_line(char **cursor)
{
	char *p = *cursor, *s;
	if (!p || !*p) return NULL;
	s = p;
	while (*p && *p != '\n') p++;
	if (*p) { *p = 0; *cursor = p + 1; } else *cursor = p;
	if (p > s && p[-1] == '\r') p[-1] = 0;
	return s;
}

int csv_split(char *line, char **f, int max)
{
	int n = 0;
	if (max <= 0) return 0;
	f[n++] = line;
	for (; *line; line++)
		if (*line == ';') { *line = 0; if (n < max) f[n++] = line + 1; else break; }
	return n;
}
