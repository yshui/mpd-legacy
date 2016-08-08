#pragma once

#include <stdlib.h>

static inline void free_s(const void *ptr) {
	// Silent compiler warning
	union {
		const void *ptr1;
		void *ptr2;
	} magic;

	magic.ptr1 = ptr;
	free(magic.ptr2);
}
