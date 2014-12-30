/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

/* No barriers guarenteed for atomic operations */

#pragma once

#include <stdint.h>

#include "util/ydef.h"
#include "compiler.h"
#include "util/ythread.h"

#if __has_include(<stdatomic.h>) || \
	(defined(__C11__) && GCC_CHECK_VERSION(4, 9) && \
	 !defined(__STDC_NO_ATOMICS__))

# include <stdatomic.h>
typedef _Atomic(int32_t) atomic_t;
# define yatomic_get(x) (atomic_load(x))
# define yatomic_set(x, v) (atomic_store(x, v))
# define yatomic_inc(x) (atomic_fetch_add(x, 1))
# define yatomic_dec(x) (atomic_fetch_add(x, -1))
# define yatomic_add(x, y) (atomic_fetch_add(x, y))
# define yatomic_init(x) (*(x) = ATOMIC_VAR_INIT(0))

#elif __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4

typedef volatile int32_t atomic_t __attribute__((aligned(4)));
# define yatomic_get(x) (*x)
# define yatomic_set(x, v) (*(x) = v)
# define yatomic_inc(x) (__sync_fetch_and_add(x, 1))
# define yatomic_dec(x) (__sync_fetch_and_sub(x, 1))
# define yatomic_add(x, y) (__sync_fetch_and_add(x, y))
# define yatomic_init(x) (*(x) = 0)

#else

/* No atomic support from compiler */
/* XXX use locks to gurantee atomic behavior */
typedef struct _atomic_t {
	volatile int32_t val __attribute__((aligned(4)));
	mtx_t mtx;
} atomic_t;
static inline void yatomic_init(atomic_t *x) {
	mtx_init(&x->mtx);
	x->val = 0;
}
static inline void yatomic_set(atomic_t *x, int32_t y) {
	mtx_lock(&x->mtx);
	x->val = y;
	mtx_unlock(&x->mtx);
}
static inline int32_t yatomic_fetch_and_add(atomic_t *x, int32_t y) {
	int32_t tmp;
	mtx_lock(&x->mtx);
	tmp = x->val;
	x->val = tmp+y;
	mtx_unlock(&x->mtx);
	return tmp;
}
# define yatomic_get(x) (x->val)
# define yatomic_inc(x) (yatomic_fetch_and_add(x, 1))
# define yatomic_dec(x) (yatomic_fetch_and_add(x, -1))

#endif

