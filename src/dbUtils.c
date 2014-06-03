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
#include "dbUtils.h"
#include "locate.h"
#include "database.h"
#include "db_visitor.h"
#include "playlist.h"
#include "stored_playlist.h"

#include <glib.h>

static int
add_to_queue_song(struct song *song, void *ctx)
{
	struct player_control *pc = ctx;

	int result =
		playlist_append_song(&g_playlist, pc, song, NULL);
	if (result != MPD_SUCCESS) {
		log_err("Failed to add song to queue.");
		return result;
	}

	return MPD_SUCCESS;
}

static const struct db_visitor add_to_queue_visitor = {
	.song = add_to_queue_song,
};

int
addAllIn(struct player_control *pc, const char *uri)
{
	return db_walk(uri, &add_to_queue_visitor, pc);
}

struct add_data {
	const char *path;
};

static int
add_to_spl_song(struct song *song, void *ctx)
{
	struct add_data *data = ctx;
	return spl_append_song(data->path, song);
}

static const struct db_visitor add_to_spl_visitor = {
	.song = add_to_spl_song,
};

int
addAllInToStoredPlaylist(const char *uri_utf8, const char *path_utf8)
{
	struct add_data data = {
		.path = path_utf8,
	};

	return db_walk(uri_utf8, &add_to_spl_visitor, &data);
}

struct find_add_data {
	struct player_control *pc;
	const struct locate_item_list *criteria;
};

static int
find_add_song(struct song *song, void *ctx)
{
	struct find_add_data *data = ctx;

	if (!locate_song_match(song, data->criteria))
		return true;

	int result =
		playlist_append_song(&g_playlist, data->pc,
				     song, NULL);
	if (result != MPD_SUCCESS) {
		log_err("Failed to add songs to queue.");
		return result;
	}

	return MPD_SUCCESS;
}

static const struct db_visitor find_add_visitor = {
	.song = find_add_song,
};

int
findAddIn(struct player_control *pc, const char *name,
	  const struct locate_item_list *criteria)
{
	struct find_add_data data;
	data.pc = pc;
	data.criteria = criteria;

	return db_walk(name, &find_add_visitor, &data);
}

static int
searchadd_visitor_song(struct song *song, void *_data)
{
	struct find_add_data *data = _data;

	if (!locate_song_search(song, data->criteria))
		return MPD_SUCCESS;

	return playlist_append_song(&g_playlist, data->pc, song, NULL);
}

static const struct db_visitor searchadd_visitor = {
	.song = searchadd_visitor_song,
};

int
search_add_songs(struct player_control *pc, const char *uri,
		 const struct locate_item_list *criteria)
{
	struct locate_item_list *new_list =
		locate_item_list_casefold(criteria);
	struct find_add_data data = {
		.pc = pc,
		.criteria = new_list,
	};

	int success = db_walk(uri, &searchadd_visitor, &data);

	locate_item_list_free(new_list);

	return success;
}

struct search_add_playlist_data {
	const char *playlist;
	const struct locate_item_list *criteria;
};

static int
searchaddpl_visitor_song(struct song *song, void *_data)
{
	struct search_add_playlist_data *data = _data;

	if (!locate_song_search(song, data->criteria))
		return MPD_SUCCESS;

	return spl_append_song(data->playlist, song);
}

static const struct db_visitor searchaddpl_visitor = {
	.song = searchaddpl_visitor_song,
};

int
search_add_to_playlist(const char *uri, const char *path_utf8,
		       const struct locate_item_list *criteria)
{
	struct locate_item_list *new_list
		= locate_item_list_casefold(criteria);
	struct search_add_playlist_data data = {
		.playlist = path_utf8,
		.criteria = new_list,
	};

	int success = db_walk(uri, &searchaddpl_visitor, &data);

	locate_item_list_free(new_list);

	return success;
}
