/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2014 Yuxuan Shui, yshuiv7@gmail.com */

#pragma once

#include "macros.h"

#include <sys/types.h>
#include <stdbool.h>

#define MAX_ERR 4095

#define _IS_ERR(x) ((x) >= (unsigned long)-MAX_ERR)

//In the future we could just use errno defined in errno.h,
//and add some more of ours.
enum mpd_err {
	/* SUCCESS is 0, but you should check against * 
	 * MPD_SUCCES anyway for better readability   */
	MPD_SUCCESS = 0,
	MPD_ERR_FIRST,
	MPD_PERM,	//Permission denied
	MPD_ACCESS,	//Failed to access
	MPD_MISS_VALUE,	//Missing value
	MPD_INVAL,	//Invalid value
	MPD_DUP,	//Something shouldn't be duplicated is duplicated
	MPD_NOENT,	//Not exists
	MPD_3RD,	//Error in 3rd party libraries
	MPD_NIMPL,	//Not implemented
	MPD_DISABLED,	//Feature disabled
	MPD_UNKNOWN,
	PLAYLIST_ERRNO,
	PLAYLIST_DENIED,
	PLAYLIST_NO_SUCH_SONG,
	PLAYLIST_NO_SUCH_LIST,
	PLAYLIST_LIST_EXISTS,
	PLAYLIST_BAD_NAME,
	PLAYLIST_BAD_RANGE,
	PLAYLIST_NOT_PLAYING,
	PLAYLIST_TOO_LARGE,
	PLAYLIST_DISABLED,
	PLAYLIST_LAST,
	DB_FIRST,
	/* The database is disabled, i.e. none is configured in this *
	 * MPD instance.					     */
	DB_DISABLED,
	/* Access error, read/write error, file doesn't exist, and/or *
	 * can't be created.					      */
	DB_ACCESS,
	DB_CORRUPT,	//Database corrupted
	DB_MALFORM,	//Malformed database
	DB_DUP,		//Duplicate entries in database
	DB_ERRNO,	//Returns an errno
	DB_NOENT,	//Not found in database
	CMD_QUOTE,	//Invalid unquoted character
	MPD_ERR_LAST,
};

static inline void *ERR_PTR(long error){
	return (void *)error;
}

static inline long PTR_ERR(const void *ptr){
	return (long)ptr;
}

static inline bool IS_ERR(const void *ptr){
	return _IS_ERR((unsigned long)ptr);
}

static inline bool IS_ERR_OR_NULL(const void *ptr){
	return !ptr || IS_ERR(ptr);
}
