/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "utils.h"
#include "glib_compat.h"
#include "conf.h"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef WIN32
#include <pwd.h>
#endif

#if HAVE_IPV6 && WIN32
#include <winsock2.h>
#endif 

#if HAVE_IPV6 && ! WIN32
#include <sys/socket.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

G_GNUC_CONST
static inline GQuark
parse_path_quark(void)
{
	return g_quark_from_static_string("path");
}

char *
parsePath(const char *path, G_GNUC_UNUSED GError **error_r)
{
	assert(path != NULL);
	assert(error_r == NULL || *error_r == NULL);

#ifndef WIN32
	if (!g_path_is_absolute(path) && path[0] != '~') {
		g_set_error(error_r, parse_path_quark(), 0,
			    "not an absolute path: %s", path);
		return NULL;
	} else if (path[0] == '~') {
		const char *home;

		if (path[1] == '/' || path[1] == '\0') {
			const char *user = config_get_string(CONF_USER, NULL);
			if (user != NULL) {
				struct passwd *passwd = getpwnam(user);
				if (!passwd) {
					g_set_error(error_r, parse_path_quark(), 0,
						    "no such user: %s", user);
					return NULL;
				}

				home = passwd->pw_dir;
			} else {
				home = g_get_home_dir();
				if (home == NULL) {
					g_set_error_literal(error_r, parse_path_quark(), 0,
							    "problems getting home "
							    "for current user");
					return NULL;
				}
			}

			++path;
		} else {
			++path;

			const char *slash = strchr(path, '/');
			char *user = slash != NULL
				? strndup(path, slash - path)
				: strdup(path);

			struct passwd *passwd = getpwnam(user);
			if (!passwd) {
				g_set_error(error_r, parse_path_quark(), 0,
					    "no such user: %s", user);
				free(user);
				return NULL;
			}

			free(user);

			home = passwd->pw_dir;
			path = slash;
		}

		char *res = malloc(strlen(home) + strlen(path) + 1);
		sprintf(res, "%s%s", home, path);
		return res;
	} else {
#endif
		return strdup(path);
#ifndef WIN32
	}
#endif
}

char *
build_filename(const char *c1, ...){
	va_list va_args;
	int len = strlen(c1);
	const char *component;
	va_start(va_args, c1);

	while(component = va_arg(va_args, const char *))
		len += strlen(component) + 1;

	va_end(va_args);

	char *res = malloc(len + 1);

	va_start(va_args, c1);
	strcpy(res, c1);
	while(component = va_arg(va_args, const char *)){
#ifndef WIN32
		strcat(res, "/");
#else
		strcat(res, "\\");
#endif
		strcat(res, component);
	}

	return res;
}

bool is_alpha(char c){
	return (c>='A' && c<='Z') || (c>='a' && c<='z');
}

char *strdup_basename (const char *file_name)
{
	gssize base;
	gssize last_nonslash;
	gsize len;
	gchar *retval;

	if (file_name == NULL)
		return NULL;


	if (file_name[0] == '\0')
		return strdup (".");

	last_nonslash = strlen (file_name) - 1;

#ifdef WIN32
# define IS_SLASH(c) ((c) == '\\' || (c) == '/')
# define SLASH "\\"
#else
# define IS_SLASH(c) ((c) == '/')
# define SLASH "/"
#endif

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
#undef SLASH
#undef IS_SLASH
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
