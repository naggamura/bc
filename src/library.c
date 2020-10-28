/*
 * *****************************************************************************
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018-2020 Gavin D. Howard and contributors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * *****************************************************************************
 *
 * The public functions for libbc.
 *
 */

#if BC_ENABLE_LIBRARY

#include <setjmp.h>

#include <string.h>

#include <bc.h>

#include "library.h"
#include "num.h"
#include "vm.h"

static void bcl_num_destruct(void *num);

void bcl_handleSignal(void) {

	// Signal already in flight, or bc is not executing.
	if (vm.sig || !vm.running) return;

	vm.sig = 1;

	assert(vm.jmp_bufs.len);

	if (!vm.sig_lock) BC_VM_JMP;
}

BcError bcl_init(void) {

	BcError e = BC_ERROR_SUCCESS;

	vm.refs += 1;

	if (vm.refs > 1) return e;

	BC_FUNC_HEADER_LOCK(err);

	vm.ctxts.v = NULL;
	vm.jmp_bufs.v = NULL;
	vm.out.v = NULL;

	vm.abrt = false;

	bc_vm_init();

	bc_vec_init(&vm.ctxts, sizeof(BcContext), NULL);
	bc_vec_init(&vm.jmp_bufs, sizeof(sigjmp_buf), NULL);
	bc_vec_init(&vm.out, sizeof(uchar), NULL);
	bc_rand_init(&vm.rng);

err:
	if (BC_ERR(vm.err)) {
		if (vm.out.v != NULL) bc_vec_free(&vm.out);
		if (vm.jmp_bufs.v != NULL) bc_vec_free(&vm.jmp_bufs);
		if (vm.ctxts.v != NULL) bc_vec_free(&vm.ctxts);
	}

	BC_FUNC_FOOTER_UNLOCK(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcError bcl_pushContext(BcContext ctxt) {

	BcError e = BC_ERROR_SUCCESS;

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_push(&vm.ctxts, &ctxt);

err:
	BC_FUNC_FOOTER_UNLOCK(e);
	return e;
}

void bcl_popContext(void) {
	if (vm.ctxts.len) bc_vec_pop(&vm.ctxts);
}

BcContext bcl_context(void) {
	if (!vm.ctxts.len) return NULL;
	return *((BcContext*) bc_vec_top(&vm.ctxts));
}

void bcl_free(void) {

	BC_SIG_LOCK;

	vm.refs -= 1;

	if (vm.refs) return;

#ifndef NDEBUG
	bc_rand_free(&vm.rng);
	bc_vec_free(&vm.out);
	bc_vec_free(&vm.jmp_bufs);

	{
		size_t i;

		for (i = 0; i < vm.ctxts.len; ++i) {
			BcContext ctxt = *((BcContext*) bc_vec_item(&vm.ctxts, i));
			bcl_ctxt_free(ctxt);
		}
	}

	bc_vec_free(&vm.ctxts);
#endif // NDEBUG

	bc_vm_shutdown();

	memset(&vm, 0, sizeof(BcVm));

	BC_SIG_UNLOCK;

	assert(!vm.running && !vm.sig && !vm.sig_lock);
}

void bcl_gc(void) {
	bc_vm_freeTemps();
}

bool bcl_abortOnFatalError(void) {
	return vm.abrt;
}

void bcl_setAbortOnFatalError(bool abrt) {
	vm.abrt = abrt;
}

BcContext bcl_ctxt_create(void) {

	BcContext ctxt = NULL;

	BC_FUNC_HEADER_LOCK(err);

	ctxt = bc_vm_malloc(sizeof(BcContext));

	bc_vec_init(&ctxt->nums, sizeof(BcNum), bcl_num_destruct);
	bc_vec_init(&ctxt->free_nums, sizeof(BcNumber), NULL);

	ctxt->scale = 0;
	ctxt->ibase = 10;
	ctxt->obase= 10;

err:
	if (BC_ERR(vm.err && ctxt != NULL)) {
		if (ctxt->nums.v != NULL) bc_vec_free(&ctxt->nums);
		free(ctxt);
		ctxt = NULL;
	}

	BC_FUNC_FOOTER_NO_ERR;

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return ctxt;
}

void bcl_ctxt_free(BcContext ctxt) {
	bc_vec_free(&ctxt->free_nums);
	bc_vec_free(&ctxt->nums);
	free(ctxt);
}

void bcl_ctxt_freeAll(BcContext ctxt) {
	bc_vec_npop(&ctxt->nums, ctxt->nums.len);
	bc_vec_npop(&ctxt->free_nums, ctxt->free_nums.len);
}

size_t bcl_ctxt_scale(BcContext ctxt) {
	return ctxt->scale;
}

void bcl_ctxt_setScale(BcContext ctxt, size_t scale) {
	ctxt->scale = scale;
}

size_t bcl_ctxt_ibase(BcContext ctxt) {
	return ctxt->ibase;
}

void bcl_ctxt_setIbase(BcContext ctxt, size_t ibase) {
	ctxt->ibase = ibase;
}

size_t bcl_ctxt_obase(BcContext ctxt) {
	return ctxt->obase;
}

void bcl_ctxt_setObase(BcContext ctxt, size_t obase) {
	ctxt->obase = obase;
}

BcError bcl_num_error(const BcNumber n) {

	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	if (n >= ctxt->nums.len) {
		if (n > 0 - (BcNumber) BC_ERROR_NELEMS) return (BcError) (0 - n);
		else return BC_ERROR_INVALID_NUM;
	}
	else return BC_ERROR_SUCCESS;
}

static BcNumber bcl_num_insert(BcContext ctxt, BcNum *restrict n) {

	BcNumber idx;

	if (ctxt->free_nums.len) {

		BcNum *ptr;

		idx = *((BcNumber*) bc_vec_top(&ctxt->free_nums));

		bc_vec_pop(&ctxt->free_nums);

		ptr = bc_vec_item(&ctxt->nums, idx);
		memcpy(ptr, n, sizeof(BcNum));
	}
	else {
		idx = ctxt->nums.len;
		bc_vec_push(&ctxt->nums, n);
	}

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcNumber bcl_num_init(void) {
	return bcl_num_initReq(BC_NUM_DEF_SIZE);
}

BcNumber bcl_num_initReq(size_t req) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum n;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	bc_num_init(&n, req);

err:
	BC_FUNC_FOOTER_UNLOCK(e);
	BC_MAYBE_SETUP(ctxt, e, n, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

static void bcl_num_dtor(BcContext ctxt, BcNumber n, BcNum *restrict num) {

	BC_SIG_ASSERT_LOCKED;

	assert(num != NULL && num->num != NULL);

	bcl_num_destruct(num);
	bc_vec_push(&ctxt->free_nums, &n);
}

void bcl_num_free(BcNumber n) {

	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	BC_SIG_LOCK;

	assert(n < ctxt->nums.len);

	num = BC_NUM(ctxt, n);

	bcl_num_dtor(ctxt, n, num);

	BC_SIG_UNLOCK;
}

BcError bcl_num_copy(const BcNumber d, const BcNumber s) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *dest, *src;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	assert(d < ctxt->nums.len && s < ctxt->nums.len);

	dest = BC_NUM(ctxt, d);
	src = BC_NUM(ctxt, s);

	assert(dest != NULL && src != NULL);
	assert(dest->num != NULL && src->num != NULL);

	bc_num_copy(dest, src);

err:
	BC_FUNC_FOOTER_UNLOCK(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcNumber bcl_num_dup(const BcNumber s) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *src, dest;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(s < ctxt->nums.len);

	src = BC_NUM(ctxt, s);

	assert(src != NULL && src->num != NULL);

	bc_num_clear(&dest);

	bc_num_createCopy(&dest, src);

err:
	BC_FUNC_FOOTER_UNLOCK(e);
	BC_MAYBE_SETUP(ctxt, e, dest, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

static void bcl_num_destruct(void *num) {

	BcNum *n = (BcNum*) num;

	assert(n != NULL);

	if (n->num == NULL) return;

	bc_num_free(num);
	bc_num_clear(num);
}

bool bcl_num_neg(const BcNumber n) {

	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	num = BC_NUM(ctxt, n);

	assert(num != NULL && num->num != NULL);

	return num->neg;
}

size_t bcl_num_scale(const BcNumber n) {

	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	num = BC_NUM(ctxt, n);

	assert(num != NULL && num->num != NULL);

	return bc_num_scale(num);
}

size_t bcl_num_len(const BcNumber n) {

	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	num = BC_NUM(ctxt, n);

	assert(num != NULL && num->num != NULL);

	return bc_num_len(num);
}

BcError bcl_num_bigdig(const BcNumber n, BcBigDig *result) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	assert(n < ctxt->nums.len);
	assert(result != NULL);

	num = BC_NUM(ctxt, n);

	assert(num != NULL && num->num != NULL);

	bc_num_bigdig(num, result);

err:
	BC_FUNC_FOOTER_UNLOCK(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcNumber bcl_num_bigdig2num(const BcBigDig val) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum n;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	bc_num_createFromBigdig(&n, val);

err:
	BC_FUNC_FOOTER_UNLOCK(e);
	BC_MAYBE_SETUP(ctxt, e, n, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_bigdig2num_err(const BcNumber n, const BcBigDig val) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *num;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER_LOCK(err);

	assert(n < ctxt->nums.len);

	num = BC_NUM(ctxt, n);

	assert(num != NULL && num->num != NULL);

	bc_num_bigdig2num(num, val);

err:
	BC_FUNC_FOOTER_UNLOCK(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

static BcNumber bcl_num_binary(const BcNumber a, const BcNumber b,
                               const BcNumBinaryOp op,
                               const BcNumBinaryOpReq req)
{
	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr;
	BcNum c;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_CHECK_NUM(ctxt, a);
	BC_CHECK_NUM(ctxt, b);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && bptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL);

	bc_num_clear(&c);

	bc_num_init(&c, req(aptr, bptr, ctxt->scale));

	BC_SIG_UNLOCK;

	op(aptr, bptr, &c, ctxt->scale);

err:
	BC_SIG_MAYLOCK;
	bcl_num_dtor(ctxt, a, aptr);
	if (b != a) bcl_num_dtor(ctxt, b, bptr);
	BC_MAYBE_SETUP(ctxt, e, c, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

static BcError bcl_num_binary_err(const BcNumber a, const BcNumber b,
                                  const BcNumber c, const BcNumBinaryOp op)
{
	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr, *cptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);
	BC_CHECK_NUM_ERR(ctxt, c);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len && c < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);
	cptr = BC_NUM(ctxt, c);

	assert(aptr->num != NULL && bptr->num != NULL && cptr->num != NULL);

	op(aptr, bptr, cptr, ctxt->scale);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcNumber bcl_num_add(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_add, bc_num_addReq);
}

BcError bcl_num_add_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_add);
}

BcNumber bcl_num_sub(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_sub, bc_num_addReq);
}

BcError bcl_num_sub_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_sub);
}

BcNumber bcl_num_mul(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_mul, bc_num_mulReq);
}

BcError bcl_num_mul_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_mul);
}

BcNumber bcl_num_div(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_div, bc_num_divReq);
}

BcError bcl_num_div_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_div);
}

BcNumber bcl_num_mod(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_mod, bc_num_divReq);
}

BcError bcl_num_mod_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_mod);
}

BcNumber bcl_num_pow(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_pow, bc_num_powReq);
}

BcError bcl_num_pow_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_pow);
}

#if BC_ENABLE_EXTRA_MATH
BcNumber bcl_num_places(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_places, bc_num_placesReq);
}

BcError bcl_num_places_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_places);
}

BcNumber bcl_num_lshift(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_lshift, bc_num_placesReq);
}

BcError bcl_num_lshift_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_lshift);
}

BcNumber bcl_num_rshift(const BcNumber a, const BcNumber b) {
	return bcl_num_binary(a, b, bc_num_rshift, bc_num_placesReq);
}

BcError bcl_num_rshift_err(const BcNumber a, const BcNumber b, const BcNumber c) {
	return bcl_num_binary_err(a, b, c, bc_num_lshift);
}
#endif // BC_ENABLE_EXTRA_MATH

BcNumber bcl_num_sqrt(const BcNumber a) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr;
	BcNum b;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_CHECK_NUM(ctxt, a);

	BC_FUNC_HEADER(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(a < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);

	bc_num_sqrt(aptr, &b, ctxt->scale);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);
	bcl_num_dtor(ctxt, a, aptr);
	BC_MAYBE_SETUP(ctxt, e, b, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_sqrt_err(const BcNumber a, const BcNumber b) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && bptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL);

	bc_num_sr(aptr, bptr, ctxt->scale);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcError bcl_num_divmod(const BcNumber a, const BcNumber b,
                       BcNumber *c, BcNumber *d)
{
	BcError e = BC_ERROR_SUCCESS;
	size_t req;
	BcNum *aptr, *bptr;
	BcNum cnum, dnum;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 2);

	assert(c != NULL && d != NULL);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && bptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL);

	bc_num_clear(&cnum);
	bc_num_clear(&dnum);

	req = bc_num_divReq(aptr, bptr, ctxt->scale);

	bc_num_init(&cnum, req);
	bc_num_init(&dnum, req);

	BC_SIG_UNLOCK;

	bc_num_divmod(aptr, bptr, &cnum, &dnum, ctxt->scale);

err:
	BC_SIG_MAYLOCK;

	bcl_num_dtor(ctxt, a, aptr);
	if (b != a) bcl_num_dtor(ctxt, b, bptr);

	if (BC_ERR(vm.err)) {
		if (cnum.num != NULL) bc_num_free(&cnum);
		if (dnum.num != NULL) bc_num_free(&dnum);
	}
	else {
		*c = bcl_num_insert(ctxt, &cnum);
		*d = bcl_num_insert(ctxt, &dnum);
	}

	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcError bcl_num_divmod_err(const BcNumber a, const BcNumber b,
                           const BcNumber c, const BcNumber d)
{
	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr, *cptr, *dptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);
	BC_CHECK_NUM_ERR(ctxt, c);
	BC_CHECK_NUM_ERR(ctxt, d);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);
	assert(c < ctxt->nums.len && d < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);
	cptr = BC_NUM(ctxt, c);
	dptr = BC_NUM(ctxt, d);

	assert(aptr != NULL && bptr != NULL && cptr != NULL && dptr != NULL);
	assert(aptr != cptr && aptr != dptr && bptr != cptr && bptr != dptr);
	assert(cptr != dptr);
	assert(aptr->num != NULL && bptr->num != NULL);
	assert(cptr->num != NULL && dptr->num != NULL);

	bc_num_divmod(aptr, bptr, cptr, dptr, ctxt->scale);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcNumber bcl_num_modexp(const BcNumber a, const BcNumber b, const BcNumber c) {

	BcError e = BC_ERROR_SUCCESS;
	size_t req;
	BcNum *aptr, *bptr, *cptr;
	BcNum d;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_CHECK_NUM(ctxt, a);
	BC_CHECK_NUM(ctxt, b);
	BC_CHECK_NUM(ctxt, c);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(a < ctxt->nums.len && b < ctxt->nums.len && c < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);
	cptr = BC_NUM(ctxt, c);

	assert(aptr != NULL && bptr != NULL && cptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL && cptr->num != NULL);

	bc_num_clear(&d);

	req = bc_num_divReq(aptr, cptr, 0);

	bc_num_init(&d, req);

	BC_SIG_UNLOCK;

	bc_num_modexp(aptr, bptr, cptr, &d);

err:
	BC_SIG_MAYLOCK;

	bcl_num_dtor(ctxt, a, aptr);
	if (b != a) bcl_num_dtor(ctxt, b, bptr);
	if (c != a && c != b) bcl_num_dtor(ctxt, c, cptr);

	BC_MAYBE_SETUP(ctxt, e, d, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_modexp_err(const BcNumber a, const BcNumber b,
                           const BcNumber c, const BcNumber d)
{
	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr, *cptr, *dptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);
	BC_CHECK_NUM_ERR(ctxt, c);
	BC_CHECK_NUM_ERR(ctxt, d);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len && c < ctxt->nums.len);
	assert(d < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);
	cptr = BC_NUM(ctxt, c);
	dptr = BC_NUM(ctxt, d);

	assert(aptr != NULL && bptr != NULL && cptr != NULL && dptr != NULL);
	assert(aptr != dptr && bptr != dptr && cptr != dptr);
	assert(aptr->num != NULL && bptr->num != NULL && cptr->num != NULL);
	assert(dptr->num != NULL);

	bc_num_modexp(aptr, bptr, cptr, dptr);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

static size_t bcl_num_req(const BcNumber a, const BcNumber b, const BcReqOp op)
{
	BcNum *aptr, *bptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && bptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL);

	return op(aptr, bptr, ctxt->scale);
}

size_t bcl_num_addReq(const BcNumber a, const BcNumber b) {
	return bcl_num_req(a, b, bc_num_addReq);
}

size_t bcl_num_mulReq(const BcNumber a, const BcNumber b) {
	return bcl_num_req(a, b, bc_num_mulReq);
}

size_t bcl_num_divReq(const BcNumber a, const BcNumber b) {
	return bcl_num_req(a, b, bc_num_divReq);
}

size_t bcl_num_powReq(const BcNumber a, const BcNumber b) {
	return bcl_num_req(a, b, bc_num_powReq);
}

#if BC_ENABLE_EXTRA_MATH
size_t bcl_num_placesReq(const BcNumber a, const BcNumber b) {
	return bcl_num_req(a, b, bc_num_placesReq);
}
#endif // BC_ENABLE_EXTRA_MATH

BcError bcl_num_setScale(const BcNumber n, size_t scale) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	if (scale > nptr->scale) bc_num_extend(nptr, scale - nptr->scale);
	else if (scale < nptr->scale) bc_num_truncate(nptr, nptr->scale - scale);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

ssize_t bcl_num_cmp(const BcNumber a, const BcNumber b) {

	BcNum *aptr, *bptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && bptr != NULL);
	assert(aptr->num != NULL && bptr->num != NULL);

	return bc_num_cmp(aptr, bptr);
}

void bcl_num_zero(const BcNumber n) {

	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_zero(nptr);
}

void bcl_num_one(const BcNumber n) {

	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_one(nptr);
}

ssize_t bcl_num_cmpZero(const BcNumber n) {

	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	return bc_num_cmpZero(nptr);
}

BcNumber bcl_num_parse(const char *restrict val, const BcBigDig base) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum n;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(val != NULL);

	bc_num_clear(&n);

	bc_num_init(&n, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bc_num_parse(&n, val, base);

err:
	BC_SIG_MAYLOCK;
	BC_MAYBE_SETUP(ctxt, e, n, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_parse_err(const BcNumber n, const char *restrict val,
                          const BcBigDig base)
{
	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(val != NULL);
	assert(n < ctxt->nums.len);

	if (!bc_num_strValid(val)) {
		vm.err = BC_ERROR_PARSE_INVALID_NUM;
		goto err;
	}

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_parse(nptr, val, base);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

char* bcl_num_string(const BcNumber n, const BcBigDig base) {

	BcNum *nptr;
	char *str = NULL;
	BcContext ctxt;

	BC_CHECK_CTXT_ASSERT(ctxt);

	if (BC_ERR(n >= ctxt->nums.len)) return str;

	BC_FUNC_HEADER(err);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_print(nptr, base, false);

	str = bc_vm_strdup(vm.out.v);

err:
	BC_SIG_MAYLOCK;

	bcl_num_dtor(ctxt, n, nptr);

	BC_FUNC_FOOTER_NO_ERR;

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return str;
}

BcError bcl_num_string_err(const BcNumber n, const BcBigDig base, char **str) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(str != NULL);
	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_print(nptr, base, false);

	*str = bc_vm_strdup(vm.out.v);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

#if BC_ENABLE_EXTRA_MATH
BcNumber bcl_num_irand(const BcNumber a) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr;
	BcNum b;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_CHECK_NUM(ctxt, a);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(a < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);

	assert(aptr != NULL && aptr->num != NULL);

	bc_num_clear(&b);

	bc_num_init(&b, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bc_num_irand(aptr, &b, &vm.rng);

err:
	BC_SIG_MAYLOCK;
	bcl_num_dtor(ctxt, a, aptr);
	BC_MAYBE_SETUP(ctxt, e, b, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_irand_err(const BcNumber a, const BcNumber b) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && aptr->num != NULL);
	assert(bptr != NULL && bptr->num != NULL);

	bc_num_irand(aptr, bptr, &vm.rng);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

static void bcl_num_frandHelper(BcNum *restrict b, size_t places) {

	BcNum exp, pow, ten;
	BcDig exp_digs[BC_NUM_BIGDIG_LOG10];
	BcDig ten_digs[BC_NUM_BIGDIG_LOG10];

	bc_num_setup(&exp, exp_digs, BC_NUM_BIGDIG_LOG10);
	bc_num_setup(&ten, ten_digs, BC_NUM_BIGDIG_LOG10);

	ten.num[0] = 10;
	ten.len = 1;

	bc_num_bigdig2num(&exp, (unsigned long) places);

	bc_num_clear(&pow);

	BC_SIG_LOCK;

	BC_SETJMP_LOCKED(err);

	bc_num_init(&pow, bc_num_powReq(&ten, &exp, 0));

	BC_SIG_UNLOCK;

	bc_num_pow(&ten, &exp, &pow, 0);

	bc_num_irand(&pow, b, &vm.rng);

	bc_num_shiftRight(b, places);

err:
	BC_SIG_MAYLOCK;
	bc_num_free(&pow);
	BC_LONGJMP_CONT;
}

BcNumber bcl_num_frand(size_t places) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum n;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	bc_num_clear(&n);

	bc_num_init(&n, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bcl_num_frandHelper(&n, places);

err:
	BC_SIG_MAYLOCK;
	BC_MAYBE_SETUP(ctxt, e, n, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_frand_err(const BcNumber n, size_t places) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bcl_num_frandHelper(nptr, places);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

static void bcl_num_ifrandHelper(BcNum *restrict a, BcNum *restrict b,
                                 size_t places)
{
	BcNum ir, fr;

	bc_num_clear(&ir);
	bc_num_clear(&fr);

	BC_SIG_LOCK;

	BC_SETJMP_LOCKED(err);

	bc_num_init(&ir, BC_NUM_DEF_SIZE);
	bc_num_init(&fr, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bc_num_irand(a, &ir, &vm.rng);
	bcl_num_frandHelper(&fr, places);

	bc_num_add(&ir, &fr, b, 0);

err:
	BC_SIG_MAYLOCK;
	bc_num_free(&fr);
	bc_num_free(&ir);
	BC_LONGJMP_CONT;
}

BcNumber bcl_num_ifrand(const BcNumber a, size_t places) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr;
	BcNum b;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_CHECK_NUM(ctxt, a);

	BC_FUNC_HEADER_LOCK(err);

	bc_vec_grow(&ctxt->nums, 1);

	assert(a < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);

	assert(aptr != NULL && aptr->num != NULL);

	bc_num_clear(&b);

	bc_num_init(&b, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bcl_num_ifrandHelper(aptr, &b, places);

err:
	BC_SIG_MAYLOCK;
	bcl_num_dtor(ctxt, a, aptr);
	BC_MAYBE_SETUP(ctxt, e, b, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_ifrand_err(const BcNumber a, size_t places, const BcNumber b) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *aptr, *bptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, a);
	BC_CHECK_NUM_ERR(ctxt, b);

	BC_FUNC_HEADER(err);

	assert(a < ctxt->nums.len && b < ctxt->nums.len);

	aptr = BC_NUM(ctxt, a);
	bptr = BC_NUM(ctxt, b);

	assert(aptr != NULL && aptr->num != NULL);
	assert(bptr != NULL && bptr->num != NULL);

	bcl_num_ifrandHelper(aptr, bptr, places);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcError bcl_num_seedWithNum(const BcNumber n) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_rng(nptr, &vm.rng);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcError bcl_num_seed(unsigned char seed[BC_SEED_SIZE])
{
	BcError e = BC_ERROR_SUCCESS;
	size_t i;
	unsigned long vals[BC_SEED_ULONGS];

	BC_FUNC_HEADER(err);

	for (i = 0; i < BC_SEED_SIZE; ++i) {
		vals[i / sizeof(long)] |= seed[i] << (CHAR_BIT * (i % sizeof(long)));
	}

	bc_rand_seed(&vm.rng, vals[0], vals[1], vals[2], vals[3]);

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);
	return e;
}

BcError bcl_num_reseed(void) {

	BcError e = BC_ERROR_SUCCESS;

	BC_FUNC_HEADER(err);

	bc_rand_srand(bc_vec_top(&vm.rng.v));

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcNumber bcl_num_seed2num(void) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum n;
	BcNumber idx;
	BcContext ctxt;

	BC_CHECK_CTXT(ctxt);

	BC_FUNC_HEADER_LOCK(err);

	bc_num_clear(&n);

	bc_num_init(&n, BC_NUM_DEF_SIZE);

	BC_SIG_UNLOCK;

	bc_num_createFromRNG(&n, bc_vec_top(&vm.rng.v));

err:
	BC_SIG_MAYLOCK;
	BC_MAYBE_SETUP(ctxt, e, n, idx);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return idx;
}

BcError bcl_num_seed2num_err(const BcNumber n) {

	BcError e = BC_ERROR_SUCCESS;
	BcNum *nptr;
	BcContext ctxt;

	BC_CHECK_CTXT_ERR(ctxt);

	BC_CHECK_NUM_ERR(ctxt, n);

	BC_FUNC_HEADER(err);

	assert(n < ctxt->nums.len);

	nptr = BC_NUM(ctxt, n);

	assert(nptr != NULL && nptr->num != NULL);

	bc_num_createFromRNG(nptr, bc_vec_top(&vm.rng.v));

err:
	BC_SIG_MAYLOCK;
	BC_FUNC_FOOTER(e);

	assert(!vm.running && !vm.sig && !vm.sig_lock);

	return e;
}

BcRandInt bcl_rand_int(void) {
	return (BcRandInt) bc_rand_int(&vm.rng);
}

BcRandInt bcl_rand_bounded(BcRandInt bound) {
	return (BcRandInt) bc_rand_bounded(&vm.rng, (BcRand) bound);
}

#endif // BC_ENABLE_EXTRA_MATH

#endif // BC_ENABLE_LIBRARY