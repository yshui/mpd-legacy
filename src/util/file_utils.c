/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>

#include "file_utils.h"
#include "macros.h"

#ifdef WIN32
# define IS_SLASH(c) ((c) == '\\' || (c) == '/')
# define SLASH "\\"
#else
# define IS_SLASH(c) ((c) == '/')
# define SLASH "/"
#endif

#define SLASH_C SLASH[0]

char *
build_db_filename(const char *c1, ...){
	va_list va_args;
	int len = c1 ? strlen(c1) : 0;
	const char *component;
	va_start(va_args, c1);

	while((component = va_arg(va_args, const char *))){
		assert(component);
		len += strlen(component) + 1;
	}

	va_end(va_args);

	char *res = malloc(len + 1);

	va_start(va_args, c1);
	if(c1)
		strcpy(res, c1);
	else
		strcpy(res, va_arg(va_args, const char *));

	while((component = va_arg(va_args, const char *))){
		strcat(res, "/");
		strcat(res, component);
	}

	return res;
}

char *strdup_basename (const char *file_name)
{
	ssize_t base;
	ssize_t last_nonslash;
	size_t len;
	char *retval;

	if (file_name == NULL)
		return NULL;


	if (file_name[0] == '\0')
		return strdup (".");

	last_nonslash = strlen (file_name) - 1;
	while (last_nonslash >= 0 && IS_SLASH(file_name[last_nonslash]))
		last_nonslash--;

	if (last_nonslash == -1)
		/* string only containing slashes */
		return strdup(SLASH);

#ifdef WIN32
	if (last_nonslash == 1 &&
			is_alpha(file_name[0]) &&
			file_name[1] == ':')
		/* string only containing slashes and a drive */
		return strdup (SLASH);
#endif
	base = last_nonslash;

	while (base >=0 && !IS_SLASH (file_name[base]))
		base--;
#ifdef WIN32
	if (base == -1 &&
			is_alpha(file_name[0]) &&
			file_name[1] == ':')
		base = 1;
#endif

	len = last_nonslash - base;
	retval = malloc (len + 1);
	memcpy(retval, file_name + base + 1, len);
	retval[len] = '\0';

	return retval;
}

char *
strdup_dirname (const char *file_name)
{
	char *base;
	size_t len;

	if(!file_name)
		return NULL;

	base = strrchr(file_name, SLASH_C);

#ifdef WIN32
	char *q;
	q = strrchr (file_name, '/');
	if(base == NULL || (q != NULL && q > base))
		base = q;
#endif

	if(!base)
	{
#ifdef WIN32
		if(is_alpha(file_name[0]) && file_name[1] == ':'){
			char drive_colon_dot[4];

			drive_colon_dot[0] = file_name[0];
			drive_colon_dot[1] = ':';
			drive_colon_dot[2] = '.';
			drive_colon_dot[3] = '\0';

			return strdup (drive_colon_dot);
		}
#endif
		/* As expected in playlist_check_translate_song */
		return NULL;
	}

	while (base > file_name && IS_SLASH(*base))
		base--;

#ifdef WIN32
	/* base points to the char before the last slash.
	 *
	 * In case file_name is the root of a drive (X:\) or a child of the
	 * root of a drive (X:\foo), include the slash.
	 *
	 * In case file_name is the root share of an UNC path
	 * (\\server\share), add a slash, returning \\server\share\ .
	 *
	 * In case file_name is a direct child of a share in an UNC path
	 * (\\server\share\foo), include the slash after the share name,
	 * returning \\server\share\ .
	 */
	if(base == file_name + 1 &&
	    is_alpha(file_name[0]) &&
	    file_name[1] == ':')
		base++;
	else if(IS_SLASH(file_name[0]) &&
		 IS_SLASH(file_name[1]) &&
		 file_name[2] &&
		 !IS_SLASH(file_name[2]) &&
		 base >= file_name+2)
	{
		const char *p = file_name+2;
		while(*p && !IS_SLASH(*p))
			p++;
		if(p == base+1){
			len = strlen(file_name)+1;
			base = tmalloc(char, len+1);
			strcpy(base, file_name);
			base[len-1] = SLASH;
			base[len] = 0;
			return base;
		}
		if(IS_SLASH(*p)){
			p++;
			while (*p && !IS_SLASH(*p))
				p++;
			if (p == base + 1)
				base++;
		}
	}
#endif

	len = 1+base-file_name;
	base = tmalloc(char, len+1);
	memmove(base, file_name, len);
	base[len] = 0;

	return base;
}
