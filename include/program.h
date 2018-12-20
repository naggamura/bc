﻿/*
 * *****************************************************************************
 *
 * Copyright 2018 Gavin D. Howard
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * *****************************************************************************
 *
 * Definitions for bc programs.
 *
 */

#ifndef BC_PROGRAM_H
#define BC_PROGRAM_H

#include <stddef.h>

#include <status.h>
#include <parse.h>
#include <lang.h>
#include <num.h>

typedef struct BcProgram {

	size_t scale;

	BcNum ib;
	size_t ib_t;
	BcNum ob;
	size_t ob_t;

	BcNum hexb;

#if DC_ENABLED
	BcNum strmb;
#endif // DC_ENABLED

	BcVec results;
	BcVec stack;

	BcVec fns;
	BcVec fn_map;

	BcVec vars;
	BcVec var_map;

	BcVec arrs;
	BcVec arr_map;

	BcVec strs;
	BcVec str_map;

	BcVec consts;
	BcVec const_map;

	BcNum last;
	BcNum zero;
	BcNum one;

	size_t nchars;

} BcProgram;

#define BC_PROG_STACK(s, n) ((s)->len >= ((size_t) (n)))

#define BC_PROG_MAIN (0)
#define BC_PROG_READ (1)

#if DC_ENABLED
#define BC_PROG_REQ_FUNCS (2)
#else
// For bc, 'pop' and 'copy' are always false.
#define bc_program_pushVar(p, code, bgn, pop, copy) \
	bc_program_pushVar(p, code, bgn)
#endif // DC_ENABLED

#define BC_PROG_STR(n) (!(n)->num && !(n)->cap)
#define BC_PROG_NUM(r, n) \
	((r)->t != BC_RESULT_ARRAY && (r)->t != BC_RESULT_STR && !BC_PROG_STR(n))

typedef unsigned long (*BcProgramBuiltIn)(BcNum*);

// ** Exclude start. **
// ** Busybox exclude start. **
void bc_program_init(BcProgram *p);
void bc_program_free(BcProgram *program);

#ifndef NDEBUG
#if BC_ENABLED && DC_ENABLED
void bc_program_code(BcProgram *p);
void bc_program_printInst(BcProgram *p, char *code, size_t *bgn);
#endif // BC_ENABLED && DC_ENABLED
#endif // NDEBUG
// ** Busybox exclude end. **
// ** Exclude end. **

size_t bc_program_addId(char* data, BcVec* map, BcVec* vec);
size_t bc_program_addFunc(BcProgram *p, char *name);
BcStatus bc_program_reset(BcProgram *p, BcStatus s);
BcStatus bc_program_exec(BcProgram *p);

// ** Exclude start. **
// ** Busybox exclude start. **
extern const BcNumBinaryOp bc_program_ops[];
extern const char bc_program_exprs_name[];
extern const char bc_program_stdin_name[];
extern const char bc_program_ready_msg[];
extern const size_t bc_program_ready_len;
// ** Busybox exclude end. **
// ** Exclude end. **

#endif // BC_PROGRAM_H
