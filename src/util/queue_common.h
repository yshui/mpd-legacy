#pragma once

// Definition for common list types

#include <assert.h>
#include <stdbool.h>

#include "memory.h"
#include "queue.h"
#include "compiler.h"

struct str_list_entry {
	const char *str;
	SIMPLEQ_ENTRY(str_list_entry) next;
};

struct str_list_head {
	size_t len;
	struct str_list_entry *sqh_first, **sqh_last;
};

static inline void
str_list_free(struct str_list_head *h, bool deep) {
	struct str_list_entry *e, *tmpe;
	SIMPLEQ_FOREACH_SAFE(e, tmpe, h, next) {
		if (deep)
			free_s(e->str);
		free(e);
	}
	free(h);
}

static inline void
str_list_insert_head(struct str_list_head *h, const char *str) {
	auto e = tmalloc(struct str_list_entry, 1);

	e->str = str;
	SIMPLEQ_INSERT_HEAD(h, e, next);

	h->len++;
}

static inline void
str_list_insert_tail(struct str_list_head *h, const char *str) {
	auto e = tmalloc(struct str_list_entry, 1);

	e->str = str;
	SIMPLEQ_INSERT_TAIL(h, e, next);

	h->len++;
}

static inline struct str_list_entry **
str_list_get_nextptr(struct str_list_head *h, unsigned idx) {
	struct str_list_entry **pptr = &h->sqh_first;

	while(idx-- && *pptr)
		pptr = &(*pptr)->next.sqe_next;

	return pptr;
}

static inline void
str_list_insert_at(struct str_list_head *h, unsigned idx, const char *str) {
	assert(idx <= h->len);

	struct str_list_entry **pptr = str_list_get_nextptr(h, idx);

	auto tmp = *pptr;
	*pptr = tmalloc(struct str_list_entry, 1);
	if (!tmp)
		h->sqh_last = &(*pptr)->next.sqe_next;

	(*pptr)->str = str;
	(*pptr)->next.sqe_next = tmp;

	h->len++;
}

static inline const char *
str_list_remove_index(struct str_list_head *h, unsigned idx) {
	assert(idx < h->len);

	struct str_list_entry **pptr = str_list_get_nextptr(h, idx);

	auto ret = (*pptr)->str;
	auto tmp = (*pptr)->next.sqe_next;
	free(*pptr);

	*pptr = tmp;

	if (!tmp)
		h->sqh_last = pptr;

	h->len--;
	return ret;
}
