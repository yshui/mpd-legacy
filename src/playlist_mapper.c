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
#include "playlist_mapper.h"
#include "playlist_list.h"
#include "stored_playlist.h"
#include "mapper.h"
#include "uri.h"
#include "file_utils.h"

#include <assert.h>
#include <stdlib.h>

static struct playlist_provider *
playlist_open_path(const char *path_fs, struct input_stream **is_r)
{
	struct playlist_provider *playlist;

	playlist = playlist_list_open_uri(path_fs);
	if (playlist != NULL)
		*is_r = NULL;
	else
		playlist = playlist_list_open_path(path_fs, is_r);

	return playlist;
}

/**
 * Load a playlist from the configured playlist directory.
 */
static struct playlist_provider *
playlist_open_in_playlist_dir(const char *uri, struct input_stream **is_r)
{
	char *path_fs;

	assert(spl_valid_name(uri));

	const char *playlist_directory_fs = map_spl_path();
	if (playlist_directory_fs == NULL)
		return NULL;

	path_fs = build_db_filename(playlist_directory_fs, uri, NULL);

	struct playlist_provider *playlist =
		playlist_open_path(path_fs, is_r);
	free(path_fs);

	return playlist;
}

/**
 * Load a playlist from the configured music directory.
 */
static struct playlist_provider *
playlist_open_in_music_dir(const char *uri, struct input_stream **is_r)
{
	char *path_fs;

	assert(uri_safe_local(uri));

	path_fs = map_uri_fs(uri);
	if (path_fs == NULL)
		return NULL;

	struct playlist_provider *playlist =
		playlist_open_path(path_fs, is_r);
	free(path_fs);

	return playlist;
}

struct playlist_provider *
playlist_mapper_open(const char *uri, struct input_stream **is_r)
{
	struct playlist_provider *playlist;

	if (spl_valid_name(uri)) {
		playlist = playlist_open_in_playlist_dir(uri, is_r);
		if (playlist != NULL)
			return playlist;
	}

	if (uri_safe_local(uri)) {
		playlist = playlist_open_in_music_dir(uri, is_r);
		if (playlist != NULL)
			return playlist;
	}

	return NULL;
}
