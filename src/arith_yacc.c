/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2007
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2019-2020
 *	Harald van Dijk <harald@gigawatt.nl>.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <inttypes.h>
#include <stdlib.h>
#include <limits.h>
#include "arith_yacc.h"
#include "expand.h"
#include "shell.h"
#include "error.h"
#include "output.h"
#include "var.h"
#include "system.h"

STATIC_ASSERT(ARITH_BOR + 14 == ARITH_BORASS);
STATIC_ASSERT(ARITH_ASS + 14 == ARITH_EQ);
STATIC_ASSERT(ARITH_NOT + 14 == ARITH_NE);
STATIC_ASSERT(ARITH_LT  + 14 == ARITH_LE);
STATIC_ASSERT(ARITH_GT  + 14 == ARITH_GE);

static const char *arith_startbuf;

const char *arith_buf;
union yystype yylval;

static int last_token;

#define ARITH_PRECEDENCE(op, prec) [op - ARITH_BINOP_MIN] = prec

static const char prec[ARITH_BINOP_MAX - ARITH_BINOP_MIN] = {
	ARITH_PRECEDENCE(ARITH_MUL, 0),
	ARITH_PRECEDENCE(ARITH_DIV, 0),
	ARITH_PRECEDENCE(ARITH_REM, 0),
	ARITH_PRECEDENCE(ARITH_ADD, 1),
	ARITH_PRECEDENCE(ARITH_SUB, 1),
	ARITH_PRECEDENCE(ARITH_LSHIFT, 2),
	ARITH_PRECEDENCE(ARITH_RSHIFT, 2),
	ARITH_PRECEDENCE(ARITH_LT, 3),
	ARITH_PRECEDENCE(ARITH_LE, 3),
	ARITH_PRECEDENCE(ARITH_GT, 3),
	ARITH_PRECEDENCE(ARITH_GE, 3),
	ARITH_PRECEDENCE(ARITH_EQ, 4),
	ARITH_PRECEDENCE(ARITH_NE, 4),
	ARITH_PRECEDENCE(ARITH_BAND, 5),
	ARITH_PRECEDENCE(ARITH_BXOR, 6),
	ARITH_PRECEDENCE(ARITH_BOR, 7),
};

#define ARITH_MAX_PREC 8

static void yyerror(const char *s) attribute ((noreturn));
static void yyerror(const char *s)
{
	sh_error("arithmetic expression: %s: \"%s\"", s, arith_startbuf);
	/* NOTREACHED */
}

static inline int arith_prec(int op)
{
	return prec[op - ARITH_BINOP_MIN];
}

static inline int higher_prec(int op1, int op2)
{
	return arith_prec(op1) < arith_prec(op2);
}

static intmax_t do_binop(int op, intmax_t a, intmax_t b)
{
	switch (op) {
		int neg;
	default:
	case ARITH_REM:
	case ARITH_DIV:
		if (!b)
			yyerror("division by zero");
		if (b == -1)
			return op == ARITH_REM ? 0 : -(uintmax_t) a;
		return op == ARITH_REM ? a % b : a / b;
	case ARITH_MUL:
		return (uintmax_t) a * b;
	case ARITH_ADD:
		return (uintmax_t) a + b;
	case ARITH_SUB:
		return (uintmax_t) a - b;
	case ARITH_LSHIFT:
		if ((uintmax_t) b >= INTMAX_WIDTH)
			return 0;
		return (uintmax_t) a << b;
	case ARITH_RSHIFT:
		neg = -(a < 0);
		if ((uintmax_t) b >= INTMAX_WIDTH)
			return neg;
		return ((uintmax_t) (a ^ neg) >> b) ^ neg;
	case ARITH_LT:
		return a < b;
	case ARITH_LE:
		return a <= b;
	case ARITH_GT:
		return a > b;
	case ARITH_GE:
		return a >= b;
	case ARITH_EQ:
		return a == b;
	case ARITH_NE:
		return a != b;
	case ARITH_BAND:
		return a & b;
	case ARITH_BXOR:
		return a ^ b;
	case ARITH_BOR:
		return a | b;
	}
}

static intmax_t assignment(int var, int noeval);

static intmax_t primary(int token, union yystype *val, int op, int noeval)
{
	intmax_t result;

again:
	switch (token) {
	case ARITH_LPAREN:
		result = assignment(op, noeval);
		if (last_token != ARITH_RPAREN)
			yyerror("expecting ')'");
		last_token = yylex();
		return result;
	case ARITH_NUM:
		last_token = op;
		return val->val;
	case ARITH_VAR:
		last_token = op;
		return noeval ? val->val : lookupvarint(val->name);
	case ARITH_ADD:
		token = op;
		*val = yylval;
		op = yylex();
		goto again;
	case ARITH_SUB:
		*val = yylval;
		return -(uintmax_t) primary(op, val, yylex(), noeval);
	case ARITH_NOT:
		*val = yylval;
		return !primary(op, val, yylex(), noeval);
	case ARITH_BNOT:
		*val = yylval;
		return ~primary(op, val, yylex(), noeval);
	default:
		yyerror("expecting primary");
	}
}

static intmax_t binop2(intmax_t a, int op, int prec, int noeval)
{
	for (;;) {
		union yystype val;
		intmax_t b;
		int op2;
		int token;

		token = yylex();
		val = yylval;

		b = primary(token, &val, yylex(), noeval);

		op2 = last_token;
		if (op2 >= ARITH_BINOP_MIN && op2 < ARITH_BINOP_MAX &&
		    higher_prec(op2, op)) {
			b = binop2(b, op2, arith_prec(op), noeval);
			op2 = last_token;
		}

		a = noeval ? b : do_binop(op, a, b);

		if (op2 < ARITH_BINOP_MIN || op2 >= ARITH_BINOP_MAX ||
		    arith_prec(op2) >= prec)
			return a;

		op = op2;
	}
}

static intmax_t binop(int token, union yystype *val, int op, int noeval)
{
	intmax_t a = primary(token, val, op, noeval);

	op = last_token;
	if (op < ARITH_BINOP_MIN || op >= ARITH_BINOP_MAX)
		return a;

	return binop2(a, op, ARITH_MAX_PREC, noeval);
}

static intmax_t and(int token, union yystype *val, int op, int noeval)
{
	intmax_t a = binop(token, val, op, noeval);
	intmax_t b;

	op = last_token;
	if (op != ARITH_AND)
		return a;

	token = yylex();
	*val = yylval;

	b = and(token, val, yylex(), noeval | !a);

	return a && b;
}

static intmax_t or(int token, union yystype *val, int op, int noeval)
{
	intmax_t a = and(token, val, op, noeval);
	intmax_t b;

	op = last_token;
	if (op != ARITH_OR)
		return a;

	token = yylex();
	*val = yylval;

	b = or(token, val, yylex(), noeval | !!a);

	return a || b;
}

static intmax_t cond(int token, union yystype *val, int op, int noeval)
{
	intmax_t a = or(token, val, op, noeval);
	intmax_t b;
	intmax_t c;

	if (last_token != ARITH_QMARK)
		return a;

	b = assignment(yylex(), noeval | !a);

	if (last_token != ARITH_COLON)
		yyerror("expecting ':'");

	token = yylex();
	*val = yylval;

	c = cond(token, val, yylex(), noeval | !!a);

	return a ? b : c;
}

static intmax_t assignment(int var, int noeval)
{
	union yystype val = yylval;
	int op = yylex();
	intmax_t result;

	if (var != ARITH_VAR)
		return cond(var, &val, op, noeval);

	if (op != ARITH_ASS && (op < ARITH_ASS_MIN || op >= ARITH_ASS_MAX))
		return cond(var, &val, op, noeval);

	result = assignment(yylex(), noeval);
	if (noeval)
		return result;

	return setvarint(val.name,
			 op == ARITH_ASS ? result :
			 do_binop(op - 14, lookupvarint(val.name), result), 0);
}

intmax_t arith(const char *s)
{
	intmax_t result;

	arith_buf = arith_startbuf = s;

	result = assignment(yylex(), 0);

	if (last_token)
		yyerror("expecting EOF");

	return result;
}
