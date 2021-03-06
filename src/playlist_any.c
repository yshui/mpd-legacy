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
#include "log.h"
#include "playlist_any.h"
#include "playlist_list.h"
#include "playlist_mapper.h"
#include "uri.h"
#include "input_stream.h"

#include <assert.h>

static struct playlist_provider *
playlist_open_remote(const char *uri, struct input_stream **is_r)
{
	assert(uri_has_scheme(uri));

	struct playlist_provider *playlist =
		playlist_list_open_uri(uri);
	if (playlist != NULL) {
		*is_r = NULL;
		return playlist;
	}

	struct input_stream *is = input_stream_open(uri);
	if (IS_ERR(is)) {
		log_warning("Failed to open %s", uri);

		return (void *)is;
	}

	playlist = playlist_list_open_stream(is, uri);
	if (playlist == NULL) {
		input_stream_close(is);
		return NULL;
	}

	*is_r = is;
	return playlist;
}

struct playlist_provider *
playlist_open_any(const char *uri, struct input_stream **is_r)
{
	return uri_has_scheme(uri)
		? playlist_open_remote(uri, is_r)
		: playlist_mapper_open(uri, is_r);
}
