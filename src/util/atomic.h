/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

/* No barriers guarenteed for atomic operations */

#pragma once

#include "macros.h"

#if __has_feature(c_atomic) || GCC_CHECK_VERSION(4, 44)

# include <stdatomic.h>
typedef _Atomic(int32_t) atomic_t;
# define atomic_get(x) (atomic_load(x))
# define atomic_set(x, v) (atomic_store(x, v))
# define atomic_inc(x) (atomic_fetch_add(x, 1))
# define atomic_dec(x) (atomic_fetch_add(x, -1))
# define atomic_init(x) (*(x) = ATOMIC_VAR_INIT(0))

#elif __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4

typedef volatile int32_t atomic_t;
# define atomic_get(x) (*x)
# define atomic_set(x, v) (*(x) = v)
# define atomic_inc(x) (__sync_fetch_and_add(x, 1))
# define atomic_dec(x) (__sync_fetch_and_sub(x, 1))
# define atomic_init(x) (*(x) = 0)

#else

/* No atomic support from compiler */
/* XXX use locks to gurantee atomic behavior */
typedef volatile int32_t atomic_t;
# define atomic_get(x) (*(x))
# define atomic_set(x, v) (*(x) = v)
# define atomic_inc(x) (*(x)++)
# define atomic_dec(x) (*(x)--)
# define atomic_init(x) (*(x)=0)

#endif

