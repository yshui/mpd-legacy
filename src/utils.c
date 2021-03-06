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

#define LOG_DOMAIN "utils"

#include "log.h"
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

#if WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#if !WIN32
#include <pwd.h>
#include <sys/socket.h>
#endif

char *
parsePath(const char *path)
{
	assert(path != NULL);

#ifndef WIN32
	if (!g_path_is_absolute(path) && path[0] != '~') {
		log_err("not an absolute path: %s", path);
		return NULL;
	} else if (path[0] == '~') {
		const char *home;

		if (path[1] == '/' || path[1] == '\0') {
			const char *user = config_get_string(CONF_USER, NULL);
			if (user != NULL) {
				struct passwd *passwd = getpwnam(user);
				if (!passwd) {
					log_err("no such user: %s", user);
					return NULL;
				}

				home = passwd->pw_dir;
			} else {
				home = g_get_home_dir();
				if (home == NULL) {
					log_err("problems getting home for current user");
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
				log_err("no such user: %s", user);
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

char *strdup_printf(const char *fmt, ...){
	va_list args;
	char *buf;
	va_start(args, fmt);
	vasprintf(&buf, fmt, args);
	return buf;
}
