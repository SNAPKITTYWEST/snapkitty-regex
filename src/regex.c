/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * snapkitty-regex
 * Copyright (C) 2026 SnapKitty Collective
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * snapkitty-regex core: Thompson NFA construction + parallel simulation.
 *
 * Follows Russ Cox's "Regular Expression Matching Can Be Simple And Fast"
 * (https://swtch.com/~rsc/regexp/regexp1.html).
 *
 *   re2post : infix -> postfix (explicit '.' concatenation operator)
 *   post2nfa: postfix -> NFA via Thompson's construction
 *   match   : parallel NFA simulation over the input
 *
 * Worst-case matching time is O(m*n) for pattern length m and text
 * length n. There are no pathological inputs.
 */

#include "regex.h"

#include <stdlib.h>
#include <string.h>

enum {
	Split = 256,	/* epsilon choice state */
	Match = 257	/* accepting state */
};

typedef struct State State;
typedef struct Frag Frag;
typedef struct Ptrlist Ptrlist;
typedef struct List List;

struct State {
	int c;		/* character, Split, or Match */
	State *out;
	State *out1;
	int lastlist;	/* dedup generation, see addstate */
};

struct Frag {
	State *start;
	Ptrlist *out;	/* dangling arrows */
};

struct Ptrlist {
	Ptrlist *next;
	State **s;	/* pointer to a State* field to patch */
};

struct List {
	State **s;
	int n;
};

/* Opaque compiled pattern. */
struct Regex {
	State *start;
	State *matchstate;
	State **states;	/* every allocated state, for teardown */
	int nstates;
	int listid;	/* dedup generation counter */
};

/* Compile-time context: tracks allocations so failures unwind cleanly. */
typedef struct Compiler {
	State **states;
	int nstates;
	int capstates;
} Compiler;

static State *
state(Compiler *cp, int c, State *out, State *out1)
{
	State *s;
	State **ns;
	int ncap;

	if (cp->nstates == cp->capstates) {
		ncap = cp->capstates ? cp->capstates * 2 : 64;
		ns = realloc(cp->states, (size_t)ncap * sizeof *ns);
		if (ns == NULL)
			return NULL;
		cp->states = ns;
		cp->capstates = ncap;
	}
	s = malloc(sizeof *s);
	if (s == NULL)
		return NULL;
	s->c = c;
	s->out = out;
	s->out1 = out1;
	s->lastlist = 0;
	cp->states[cp->nstates++] = s;
	return s;
}

static Ptrlist *
list1(State **outp)
{
	Ptrlist *l = malloc(sizeof *l);

	if (l == NULL)
		return NULL;
	l->next = NULL;
	l->s = outp;
	return l;
}

static Ptrlist *
append(Ptrlist *l1, Ptrlist *l2)
{
	Ptrlist *oldl = l1;

	if (l1 == NULL)
		return l2;
	while (l1->next)
		l1 = l1->next;
	l1->next = l2;
	return oldl;
}

/* Connect every dangling arrow in l to s, freeing the list nodes. */
static void
patch(Ptrlist *l, State *s)
{
	Ptrlist *next;

	for (; l; l = next) {
		next = l->next;
		*l->s = s;
		free(l);
	}
}

static void
ptrlist_free(Ptrlist *l)
{
	Ptrlist *next;

	for (; l; l = next) {
		next = l->next;
		free(l);
	}
}

static Frag
frag(State *start, Ptrlist *out)
{
	Frag f;

	f.start = start;
	f.out = out;
	return f;
}

/*
 * Rewrite an infix pattern to postfix with '.' as the explicit
 * concatenation operator. Backslash escapes the next character into a
 * literal (emitted as '\' + char in the postfix stream).
 * Returns a malloc'd string, or NULL on malformed pattern / OOM.
 */
static char *
re2post(const char *re)
{
	size_t len = strlen(re);
	char *buf = malloc(6 * len + 8);
	char *dst;
	int nalt, natom;
	struct { int nalt; int natom; } *paren, *p;

	if (buf == NULL)
		return NULL;
	paren = malloc((len + 1) * sizeof *paren);
	if (paren == NULL) {
		free(buf);
		return NULL;
	}

	dst = buf;
	p = paren;
	nalt = 0;
	natom = 0;
	for (; *re; re++) {
		switch (*re) {
		case '(':
			if (natom > 1) {
				--natom;
				*dst++ = '.';
			}
			p->nalt = nalt;
			p->natom = natom;
			p++;
			nalt = 0;
			natom = 0;
			break;
		case '|':
			if (natom == 0)
				goto fail;
			while (--natom > 0)
				*dst++ = '.';
			nalt++;
			break;
		case ')':
			if (p == paren || natom == 0)
				goto fail;
			while (--natom > 0)
				*dst++ = '.';
			for (; nalt > 0; nalt--)
				*dst++ = '|';
			--p;
			nalt = p->nalt;
			natom = p->natom;
			natom++;
			break;
		case '*':
		case '+':
		case '?':
			if (natom == 0)
				goto fail;
			*dst++ = *re;
			break;
		case '\\':
			if (re[1] == '\0')
				goto fail;
			if (natom > 1) {
				--natom;
				*dst++ = '.';
			}
			*dst++ = '\\';
			*dst++ = *++re;
			natom++;
			break;
		default:
			if (natom > 1) {
				--natom;
				*dst++ = '.';
			}
			/* '.' is the postfix concatenation operator: a literal
			 * dot must go through as an escaped literal. */
			if (*re == '.')
				*dst++ = '\\';
			*dst++ = *re;
			natom++;
			break;
		}
	}
	if (p != paren)
		goto fail;
	while (--natom > 0)
		*dst++ = '.';
	for (; nalt > 0; nalt--)
		*dst++ = '|';
	*dst = '\0';
	free(paren);
	return buf;

fail:
	free(paren);
	free(buf);
	return NULL;
}

/*
 * Thompson's construction: build an NFA fragment per postfix token on a
 * fragment stack. Returns the start state, or NULL on failure.
 */
static State *
post2nfa(Compiler *cp, const char *postfix, State *matchstate)
{
	Frag *stack, *stackp, e1, e2, e;
	State *s;
	Ptrlist *l;
	size_t n = strlen(postfix);
	const char *p;

#define push(f) (*stackp++ = (f))
#define pop() (*--stackp)

	if (n == 0)
		return NULL;
	stack = malloc((n + 1) * sizeof *stack);
	if (stack == NULL)
		return NULL;
	stackp = stack;

	for (p = postfix; *p; p++) {
		switch (*p) {
		default:	/* literal character */
			s = state(cp, (unsigned char)*p, NULL, NULL);
			if (s == NULL)
				goto fail;
			l = list1(&s->out);
			if (l == NULL)
				goto fail;
			push(frag(s, l));
			break;
		case '\\':	/* escaped literal */
			if (p[1] == '\0')
				goto fail;
			s = state(cp, (unsigned char)*++p, NULL, NULL);
			if (s == NULL)
				goto fail;
			l = list1(&s->out);
			if (l == NULL)
				goto fail;
			push(frag(s, l));
			break;
		case '.':	/* concatenation */
			if (stackp - stack < 2)
				goto fail;
			e2 = pop();
			e1 = pop();
			patch(e1.out, e2.start);
			push(frag(e1.start, e2.out));
			break;
		case '|':	/* alternation */
			if (stackp - stack < 2)
				goto fail;
			e2 = pop();
			e1 = pop();
			s = state(cp, Split, e1.start, e2.start);
			if (s == NULL)
				goto fail;
			push(frag(s, append(e1.out, e2.out)));
			break;
		case '?':	/* zero or one */
			if (stackp - stack < 1)
				goto fail;
			e = pop();
			s = state(cp, Split, e.start, NULL);
			if (s == NULL)
				goto fail;
			l = list1(&s->out1);
			if (l == NULL)
				goto fail;
			push(frag(s, append(e.out, l)));
			break;
		case '*':	/* zero or more */
			if (stackp - stack < 1)
				goto fail;
			e = pop();
			s = state(cp, Split, e.start, NULL);
			if (s == NULL)
				goto fail;
			patch(e.out, s);
			l = list1(&s->out1);
			if (l == NULL)
				goto fail;
			push(frag(s, l));
			break;
		case '+':	/* one or more */
			if (stackp - stack < 1)
				goto fail;
			e = pop();
			s = state(cp, Split, e.start, NULL);
			if (s == NULL)
				goto fail;
			patch(e.out, s);
			l = list1(&s->out1);
			if (l == NULL)
				goto fail;
			push(frag(e.start, l));
			break;
		}
	}

	if (stackp != stack + 1)
		goto fail;
	e = pop();
	patch(e.out, matchstate);
	free(stack);
	return e.start;

fail:
	while (stackp > stack) {
		e = pop();
		ptrlist_free(e.out);
	}
	free(stack);
	return NULL;

#undef push
#undef pop
}

/*
 * Add s to list l, following unlabeled (Split) arrows. The per-Regex
 * listid generation counter dedups in O(1) without clearing the list.
 */
static void
addstate(Regex *re, List *l, State *s)
{
	if (s == NULL || s->lastlist == re->listid)
		return;
	s->lastlist = re->listid;
	if (s->c == Split) {
		addstate(re, l, s->out);
		addstate(re, l, s->out1);
		return;
	}
	l->s[l->n++] = s;
}

static List *
startlist(Regex *re, State *s, List *l)
{
	re->listid++;
	l->n = 0;
	addstate(re, l, s);
	return l;
}

/* Advance the NFA past one input character. */
static void
step(Regex *re, List *clist, int c, List *nlist)
{
	int i;
	State *s;

	re->listid++;
	nlist->n = 0;
	for (i = 0; i < clist->n; i++) {
		s = clist->s[i];
		if (s->c == c)
			addstate(re, nlist, s->out);
	}
}

static int
ismatch(Regex *re, List *l)
{
	int i;

	for (i = 0; i < l->n; i++)
		if (l->s[i] == re->matchstate)
			return 1;
	return 0;
}

/* Parallel NFA simulation over the input. With prefix != 0, report a
 * match as soon as the accept state is reachable (pattern matches a
 * prefix of s); otherwise require it after the last character (full
 * match). */
static int
pump(Regex *re, const char *s, int prefix)
{
	State **buf1 = malloc((size_t)re->nstates * sizeof *buf1);
	State **buf2 = malloc((size_t)re->nstates * sizeof *buf2);
	List l1, l2, *clist, *nlist, *t;
	int m;

	if (buf1 == NULL || buf2 == NULL) {
		free(buf1);
		free(buf2);
		return 0;
	}
	l1.s = buf1;
	l1.n = 0;
	l2.s = buf2;
	l2.n = 0;

	clist = startlist(re, re->start, &l1);
	if (prefix && ismatch(re, clist)) {
		m = 1;
		goto done;
	}
	nlist = &l2;
	for (; *s; s++) {
		step(re, clist, (unsigned char)*s, nlist);
		t = clist;
		clist = nlist;
		nlist = t;
		if (prefix && ismatch(re, clist)) {
			m = 1;
			goto done;
		}
	}
	m = ismatch(re, clist);
done:
	free(buf1);
	free(buf2);
	return m;
}

static int
match(Regex *re, const char *s)
{
	return pump(re, s, 0);
}

Regex *
re_compile(const char *pattern)
{
	char *post;
	Compiler cp = { NULL, 0, 0 };
	Regex *re;
	State *start;
	int i;

	if (pattern == NULL || *pattern == '\0')
		return NULL;
	post = re2post(pattern);
	if (post == NULL)
		return NULL;
	re = calloc(1, sizeof *re);
	if (re == NULL) {
		free(post);
		return NULL;
	}
	re->matchstate = state(&cp, Match, NULL, NULL);
	start = re->matchstate ? post2nfa(&cp, post, re->matchstate) : NULL;
	free(post);
	if (start == NULL) {
		for (i = 0; i < cp.nstates; i++)
			free(cp.states[i]);
		free(cp.states);
		free(re);
		return NULL;
	}
	re->start = start;
	re->states = cp.states;
	re->nstates = cp.nstates;
	re->listid = 0;
	return re;
}

int
re_fullmatch(Regex *re, const char *s)
{
	if (re == NULL || s == NULL)
		return 0;
	return match(re, s);
}

int
re_match(Regex *re, const char *s)
{
	if (re == NULL || s == NULL)
		return 0;
	return pump(re, s, 1);
}

int
re_search(Regex *re, const char *s)
{
	const char *p;

	if (re == NULL || s == NULL)
		return 0;
	for (p = s; ; p++) {
		if (re_match(re, p))
			return 1;
		if (*p == '\0')
			break;
	}
	return 0;
}

void
re_free(Regex *re)
{
	int i;

	if (re == NULL)
		return;
	for (i = 0; i < re->nstates; i++)
		free(re->states[i]);
	free(re->states);
	free(re);
}
