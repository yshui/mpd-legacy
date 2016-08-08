/* Semaphore implemented using C11 thread primitives */
#pragma once

#include <stdlib.h>
#include <assert.h>

#include "c11thread.h"

struct xsem_t {
	cnd_t cnd;
	mtx_t mtx;

	size_t cnt;
};

static inline void xsem_init(struct xsem_t *sem, size_t cnt) {
	mtx_init(&sem->mtx, mtx_plain);
	cnd_init(&sem->cnd);
	sem->cnt = cnt;
}

static inline void xsem_destroy(struct xsem_t *sem) {
	mtx_destroy(&sem->mtx);
	cnd_destroy(&sem->cnd);
}

static inline void xsem_wait(struct xsem_t *sem) {
	mtx_lock(&sem->mtx);

	while(!sem->cnt)
		cnd_wait(&sem->cnd, &sem->mtx);

	mtx_unlock(&sem->mtx);
}

static inline void xsem_post_n(struct xsem_t *sem, int n) {
	assert(n > 0);
	mtx_lock(&sem->mtx);

	sem->cnt += n;
	cnd_signal(&sem->cnd);

	mtx_unlock(&sem->mtx);
}

static inline void xsem_post(struct xsem_t *sem) {
	xsem_post_n(sem, 1);
}
