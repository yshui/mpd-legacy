/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

#pragma once

#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
# define MPD_PURE __attribute__((__pure__))
# define MPD_MALLOC __attribute__((__malloc__))
# define MPD_CONST __attribute__((__const__))
#else
# define MPD_PURE
# define MPD_MALLOC
# define MPD_CONST
#endif


#define tmalloc(type, nmemb) calloc((nmemb), sizeof(type))
