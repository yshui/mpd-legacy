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

#ifndef MPD_DB_UTILS_H
#define MPD_DB_UTILS_H


#include <glib.h>
#include <stdbool.h>

struct locate_item_list;
struct player_control;

int
addAllIn(struct player_control *pc, const char *uri);

int
addAllInToStoredPlaylist(const char *uri_utf8, const char *path_utf8);

int
findAddIn(struct player_control *pc, const char *name,
	  const struct locate_item_list *criteria);

int
search_add_songs(struct player_control *pc, const char *uri,
		 const struct locate_item_list *criteria);

int
search_add_to_playlist(const char *uri, const char *path_utf8,
		       const struct locate_item_list *criteria);

#endif
