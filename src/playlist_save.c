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
#include "playlist_save.h"
#include "playlist.h"
#include "stored_playlist.h"
#include "queue.h"
#include "song.h"
#include "mapper.h"
#include "path.h"
#include "uri.h"
#include "database.h"
#include "idle.h"
#include "glib_compat.h"

#include <glib.h>

#include <stdlib.h>
#include <string.h>

void
playlist_print_song(FILE *file, const struct song *song)
{
	if (playlist_saveAbsolutePaths && song_in_database(song)) {
		char *path = map_song_fs(song);
		if (path != NULL) {
			fprintf(file, "%s\n", path);
			free(path);
		}
	} else {
		char *uri = song_get_uri(song), *uri_fs;

		uri_fs = utf8_to_fs_charset(uri);
		free(uri);

		fprintf(file, "%s\n", uri_fs);
		free(uri_fs);
	}
}

void
playlist_print_uri(FILE *file, const char *uri)
{
	char *s;

	if (playlist_saveAbsolutePaths && !uri_has_scheme(uri) &&
	    !g_path_is_absolute(uri))
		s = map_uri_fs(uri);
	else
		s = utf8_to_fs_charset(uri);

	if (s != NULL) {
		fprintf(file, "%s\n", s);
		free(s);
	}
}

int
spl_save_queue(const char *name_utf8, const struct queue *queue)
{
	char *path_fs;
	FILE *file;

	if (map_spl_path() == NULL)
		return -PLAYLIST_DISABLED;

	if (!spl_valid_name(name_utf8))
		return -PLAYLIST_BAD_NAME;

	path_fs = map_spl_utf8_to_fs(name_utf8);
	if (path_fs == NULL)
		return -PLAYLIST_BAD_NAME;

	if (g_file_test(path_fs, G_FILE_TEST_EXISTS)) {
		free(path_fs);
		return -PLAYLIST_LIST_EXISTS;
	}

	file = fopen(path_fs, "w");
	free(path_fs);

	if (file == NULL)
		return -PLAYLIST_ERRNO;

	for (unsigned i = 0; i < queue_length(queue); i++)
		playlist_print_song(file, queue_get(queue, i));

	fclose(file);

	idle_add(IDLE_STORED_PLAYLIST);
	return MPD_SUCCESS;
}

int
spl_save_playlist(const char *name_utf8, const struct playlist *playlist)
{
	return spl_save_queue(name_utf8, &playlist->queue);
}

int
playlist_load_spl(struct playlist *playlist, struct player_control *pc,
		  const char *name_utf8,
		  unsigned start_index, unsigned end_index)
{
	struct str_list_head *list;

	list = spl_load(name_utf8);
	if (IS_ERR(list))
		return PTR_ERR(list);

	size_t i = 0;
	struct str_list_entry *e;
	SIMPLEQ_FOREACH(e, list, next) {
		if (i++ >= end_index)
			break;

		const char *temp = e->str;
		if ((playlist_append_uri(playlist, pc, temp, NULL)) != MPD_SUCCESS) {
			/* for windows compatibility, convert slashes */
			char *temp2 = strdup(temp);
			char *p = temp2;
			while (*p) {
				if (*p == '\\')
					*p = '/';
				p++;
			}
			if ((playlist_append_uri(playlist, pc, temp2,
						 NULL)) != MPD_SUCCESS) {
				log_warning("can't add file \"%s\"", temp2);
			}
			free(temp2);
		}
	}

	str_list_free(list, true);
	return MPD_SUCCESS;
}
