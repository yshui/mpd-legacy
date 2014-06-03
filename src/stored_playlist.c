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
#include "stored_playlist.h"
#include "playlist_save.h"
#include "text_file.h"
#include "song.h"
#include "mapper.h"
#include "path.h"
#include "uri.h"
#include "database.h"
#include "idle.h"
#include "conf.h"
#include "glib_compat.h"
#include "macros.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static const char PLAYLIST_COMMENT = '#';

static unsigned playlist_max_length;
bool playlist_saveAbsolutePaths = DEFAULT_PLAYLIST_SAVE_ABSOLUTE_PATHS;

void
spl_global_init(void)
{
	playlist_max_length = config_get_positive(CONF_MAX_PLAYLIST_LENGTH,
						  DEFAULT_PLAYLIST_MAX_LENGTH);

	playlist_saveAbsolutePaths =
		config_get_bool(CONF_SAVE_ABSOLUTE_PATHS,
				DEFAULT_PLAYLIST_SAVE_ABSOLUTE_PATHS);
}

bool
spl_valid_name(const char *name_utf8)
{
	/*
	 * Not supporting '/' was done out of laziness, and we should
	 * really strive to support it in the future.
	 *
	 * Not supporting '\r' and '\n' is done out of protocol
	 * limitations (and arguably laziness), but bending over head
	 * over heels to modify the protocol (and compatibility with
	 * all clients) to support idiots who put '\r' and '\n' in
	 * filenames isn't going to happen, either.
	 */

	return strchr(name_utf8, '/') == NULL &&
		strchr(name_utf8, '\n') == NULL &&
		strchr(name_utf8, '\r') == NULL;
}

static const char *
spl_map(void)
{
	const char *r = map_spl_path();
	if (r == NULL){
		log_err("Stored playlists are disabled");
		return ERR_PTR(-PLAYLIST_DISABLED);
	}

	return r;
}

static int
spl_check_name(const char *name_utf8)
{
	if (!spl_valid_name(name_utf8)) {
		log_err("Bad playlist name");
		return PLAYLIST_BAD_NAME;
	}

	return MPD_SUCCESS;
}

static char *
spl_map_to_fs(const char *name_utf8)
{
	const char *path;
	int ret = MPD_SUCCESS;
	path = spl_map();
	if(!IS_ERR(path))
		ret = spl_check_name(name_utf8);
	if(ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	char *path_fs = map_spl_utf8_to_fs(name_utf8);
	if (path_fs == NULL)
		log_err("Bad playlist name");
		return ERR_PTR(-PLAYLIST_BAD_NAME);

	return path_fs;
}

/**
 * Create a GError for the current errno.
 */
static int
playlist_errno(void)
{
	switch (errno) {
	case ENOENT:
		return -PLAYLIST_NO_SUCH_LIST;
	default:
		log_err(strerror(errno));
		return -PLAYLIST_ERRNO;
	}
}

static struct stored_playlist_info *
load_playlist_info(const char *parent_path_fs, const char *name_fs)
{
	size_t name_length = strlen(name_fs);
	char *path_fs, *name, *name_utf8;
	int ret;
	struct stat st;
	struct stored_playlist_info *playlist;

	if (name_length < sizeof(PLAYLIST_FILE_SUFFIX) ||
	    memchr(name_fs, '\n', name_length) != NULL)
		return NULL;

	if (!g_str_has_suffix(name_fs, PLAYLIST_FILE_SUFFIX))
		return NULL;

	path_fs = build_db_filename(parent_path_fs, name_fs, NULL);
	ret = stat(path_fs, &st);
	free(path_fs);
	if (ret < 0 || !S_ISREG(st.st_mode))
		return NULL;

	name = strndup(name_fs,
			 name_length + 1 - sizeof(PLAYLIST_FILE_SUFFIX));
	name_utf8 = fs_charset_to_utf8(name);
	free(name);
	if (name_utf8 == NULL)
		return NULL;

	playlist = tmalloc(struct stored_playlist_info, 1);
	playlist->name = name_utf8;
	playlist->mtime = st.st_mtime;
	return playlist;
}

GPtrArray *
spl_list(void)
{
	const char *parent_path_fs;
	DIR *dir;
	struct dirent *ent;
	GPtrArray *list;
	struct stored_playlist_info *playlist;

	parent_path_fs = spl_map();
	if (IS_ERR(parent_path_fs))
		return ERR_PTR(-PLAYLIST_DISABLED);

	dir = opendir(parent_path_fs);
	if (dir == NULL) {
		log_err(strerror(errno));
		return ERR_PTR(-PLAYLIST_ERRNO);
	}

	list = g_ptr_array_new();

	while ((ent = readdir(dir)) != NULL) {
		playlist = load_playlist_info(parent_path_fs, ent->d_name);
		if (playlist != NULL)
			g_ptr_array_add(list, playlist);
	}

	closedir(dir);
	return list;
}

void
spl_list_free(GPtrArray *list)
{
	for (unsigned i = 0; i < list->len; ++i) {
		struct stored_playlist_info *playlist =
			g_ptr_array_index(list, i);
		free(playlist->name);
		free(playlist);
	}

	g_ptr_array_free(list, true);
}

static int
spl_save(GPtrArray *list, const char *utf8path)
{
	FILE *file;

	assert(utf8path != NULL);

	if (IS_ERR(spl_map()))
		return -PLAYLIST_DISABLED;

	char *path_fs = spl_map_to_fs(utf8path);
	if (IS_ERR(path_fs))
		return PTR_ERR(path_fs);

	file = fopen(path_fs, "w");
	free(path_fs);
	if (file == NULL)
		return playlist_errno();

	for (unsigned i = 0; i < list->len; ++i) {
		const char *uri = g_ptr_array_index(list, i);
		playlist_print_uri(file, uri);
	}

	fclose(file);
	return MPD_SUCCESS;
}

GPtrArray *
spl_load(const char *utf8path)
{
	FILE *file;
	GPtrArray *list;
	char *path_fs;

	if (IS_ERR(spl_map()))
		return ERR_PTR(-PLAYLIST_DISABLED);

	path_fs = spl_map_to_fs(utf8path);
	if (IS_ERR(path_fs))
		return (void *)path_fs;

	file = fopen(path_fs, "r");
	free(path_fs);
	if (file == NULL)
		return ERR_PTR(playlist_errno());

	list = g_ptr_array_new();

	GString *buffer = g_string_sized_new(1024);
	char *s;
	while ((s = read_text_line(file, buffer)) != NULL) {
		if (*s == 0 || *s == PLAYLIST_COMMENT)
			continue;

		if (!uri_has_scheme(s)) {
			char *path_utf8;

			path_utf8 = map_fs_to_utf8(s);
			if (path_utf8 == NULL)
				continue;

			s = path_utf8;
		} else
			s = strdup(s);

		g_ptr_array_add(list, s);

		if (list->len >= playlist_max_length)
			break;
	}

	fclose(file);
	return list;
}

void
spl_free(GPtrArray *list)
{
	for (unsigned i = 0; i < list->len; ++i) {
		char *uri = g_ptr_array_index(list, i);
		free(uri);
	}

	g_ptr_array_free(list, true);
}

static char *
spl_remove_index_internal(GPtrArray *list, unsigned idx)
{
	char *uri;

	assert(idx < list->len);

	uri = g_ptr_array_remove_index(list, idx);
	assert(uri != NULL);
	return uri;
}

static void
spl_insert_index_internal(GPtrArray *list, unsigned idx, char *uri)
{
	assert(idx <= list->len);

	g_ptr_array_add(list, uri);

	memmove(list->pdata + idx + 1, list->pdata + idx,
		(list->len - idx - 1) * sizeof(list->pdata[0]));
	g_ptr_array_index(list, idx) = uri;
}

int
spl_move_index(const char *utf8path, unsigned src, unsigned dest) {
	char *uri;

	if (src == dest)
		/* this doesn't check whether the playlist exists, but
		   what the hell.. */
		return MPD_SUCCESS;

	GPtrArray *list = spl_load(utf8path);
	if (IS_ERR(list))
		return PTR_ERR(list);

	if (src >= list->len || dest >= list->len) {
		spl_free(list);
		log_err("Bad range");
		return PLAYLIST_BAD_RANGE;
	}

	uri = spl_remove_index_internal(list, src);
	spl_insert_index_internal(list, dest, uri);

	int ret = spl_save(list, utf8path);

	spl_free(list);

	idle_add(IDLE_STORED_PLAYLIST);
	return ret;
}

int
spl_clear(const char *utf8path)
{
	FILE *file;

	if (IS_ERR(spl_map()))
		return -PLAYLIST_DISABLED;

	char *path_fs = spl_map_to_fs(utf8path);
	if (IS_ERR(path_fs))
		return PTR_ERR(path_fs);

	file = fopen(path_fs, "w");
	free(path_fs);
	if (file == NULL)
		return playlist_errno();

	fclose(file);

	idle_add(IDLE_STORED_PLAYLIST);
	return MPD_SUCCESS;
}

int
spl_delete(const char *name_utf8)
{
	char *path_fs;
	int ret;

	path_fs = spl_map_to_fs(name_utf8);
	if (IS_ERR(path_fs))
		return PTR_ERR(path_fs);

	ret = unlink(path_fs);
	free(path_fs);
	if (ret < 0)
		return playlist_errno();

	idle_add(IDLE_STORED_PLAYLIST);
	return MPD_SUCCESS;
}

int
spl_remove_index(const char *utf8path, unsigned pos)
{
	char *uri;

	GPtrArray *list = spl_load(utf8path);
	if (IS_ERR(list))
		return PTR_ERR(list);

	if (pos >= list->len) {
		spl_free(list);
		log_err("Bad range");
		return -PLAYLIST_BAD_RANGE;
	}

	uri = spl_remove_index_internal(list, pos);
	free(uri);
	int ret = spl_save(list, utf8path);

	spl_free(list);

	idle_add(IDLE_STORED_PLAYLIST);
	return ret;
}

int
spl_append_song(const char *utf8path, struct song *song)
{
	FILE *file;
	struct stat st;

	if (IS_ERR(spl_map()))
		return -PLAYLIST_DISABLED;

	char *path_fs = spl_map_to_fs(utf8path);
	if (IS_ERR(path_fs))
		return PTR_ERR(path_fs);

	file = fopen(path_fs, "a");
	free(path_fs);
	if (file == NULL)
		return playlist_errno();

	if (fstat(fileno(file), &st) < 0) {
		fclose(file);
		return playlist_errno();
	}

	if (st.st_size / (MPD_PATH_MAX + 1) >= (off_t)playlist_max_length) {
		fclose(file);
		log_err("Stored playlist is too large");
		return -PLAYLIST_TOO_LARGE;
	}

	playlist_print_song(file, song);

	fclose(file);

	idle_add(IDLE_STORED_PLAYLIST);
	return MPD_SUCCESS;
}

int
spl_append_uri(const char *url, const char *utf8file)
{
	struct song *song;

	if (uri_has_scheme(url)) {
		song = song_remote_new(url);
		int ret = spl_append_song(utf8file, song);
		song_free(song);
		return ret;
	} else {
		song = db_get_song(url);
		if (song == NULL) {
			log_err("No such song");
			return -PLAYLIST_NO_SUCH_SONG;
		}

		return spl_append_song(utf8file, song);
	}
}

static int
spl_rename_internal(const char *from_path_fs, const char *to_path_fs)
{
	if (!g_file_test(from_path_fs, G_FILE_TEST_IS_REGULAR)) {
		log_err("No such playlist");
		return -PLAYLIST_NO_SUCH_LIST;
	}

	if (g_file_test(to_path_fs, G_FILE_TEST_EXISTS)) {
		log_err("Playlist exists already");
		return -PLAYLIST_LIST_EXISTS;
	}

	if (rename(from_path_fs, to_path_fs) < 0)
		return playlist_errno();

	idle_add(IDLE_STORED_PLAYLIST);
	return MPD_SUCCESS;
}

int
spl_rename(const char *utf8from, const char *utf8to)
{
	if (IS_ERR(spl_map()))
		return -PLAYLIST_DISABLED;

	char *from_path_fs = spl_map_to_fs(utf8from);
	if (IS_ERR(from_path_fs))
		return PTR_ERR(from_path_fs);

	char *to_path_fs = spl_map_to_fs(utf8to);
	if (IS_ERR(to_path_fs)) {
		free(from_path_fs);
		return PTR_ERR(to_path_fs);
	}

	int ret = spl_rename_internal(from_path_fs, to_path_fs);

	free(from_path_fs);
	free(to_path_fs);

	return ret;
}
