/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

#pragma once

#include <stdlib.h>

#define tmalloc(type, nmemb) ((type *)calloc((nmemb), sizeof(type)))

#define check_and_return(ret, log_level, ...) { \
	int __tmp = ret; \
	if (__tmp != MPD_SUCCESS) { \
		log_meta(log_level, __VA_ARGS__); \
		return __tmp; \
	} \
}
