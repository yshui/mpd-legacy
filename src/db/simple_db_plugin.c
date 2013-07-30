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
#include "simple_db_plugin.h"
#include "song.h"
#include "db_internal.h"
#include "db_error.h"
#include "db_selection.h"
#include "db_visitor.h"
#include "db_lock.h"
#include "conf.h"
#include "tag.h"
#include "tag_internal.h"
#include "text_file.h"
#include "directory.h"
#include "string_util.h"
#include "playlist_vector.h"
#include "path.h"

#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_DB_COMPRESS
#include <zlib.h>
#endif

#define DIRECTORY_INFO_BEGIN "info_begin"
#define DIRECTORY_INFO_END "info_end"
#define DB_FORMAT_PREFIX "format: "
#define DIRECTORY_MPD_VERSION "mpd_version: "
#define DIRECTORY_FS_CHARSET "fs_charset: "
#define DB_TAG_PREFIX "tag: "
#define DIRECTORY_DIR "directory: "
#define DIRECTORY_MTIME "mtime: "
#define DIRECTORY_BEGIN "begin: "
#define DIRECTORY_END "end: "
#define SONG_BEGIN "song_begin: "
#define SONG_MTIME "mtime"
#define SONG_END "song_end"
#define PLAYLIST_META_BEGIN "playlist_begin: "

struct simple_db {
	struct db base;

	char *path;

	struct directory *root;

	time_t mtime;
};

enum {
	DB_FORMAT = 1,
};

#ifdef HAVE_DB_COMPRESS
typedef gzFile db_file;

static inline db_file db_open(const char *path_fs, const char *flags){
	return gzopen(path_fs, flags);
}

static inline int db_printf(db_file file, const char *fmt, ...){
	char *res;
	va_list args;
	int ret;

	va_start(args, fmt);
	vasprintf(&res, fmt, args);
	va_end(args);

	ret = gzputs(file, res);
	free(res);
	return ret;
}

static inline char *db_read_text_line(db_file file, GString *buf){
	return gz_read_text_line(file, buf);
}

static inline int db_close(db_file file){
	return gzclose(file);
}

static inline int db_error(db_file file){
	int errnum;
	gzerror(file, &errnum);
	if(errnum == Z_ERRNO)
		return errno;
	return errnum;
}
#else
typedef FILE *db_file;

static inline db_file db_open(const char *path_fs, const char *flags){
	return fopen(path_fs, flags);
}

static inline int db_printf(db_file file, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	int ret = vfprintf(file, fmt, args);
	va_end(args);
	return ret;
}

static inline char *db_read_text_line(db_file file, GString *buf){
	return read_text_line(file, buf);
}

static inline int db_close(db_file file){
	return fclose(file);
}

static inline int db_error(db_file file){
	return ferror(file);
}
#endif

G_GNUC_CONST
static inline GQuark
simple_db_quark(void)
{
	return g_quark_from_static_string("simple_db");
}

static inline void
simple_db_tag_save(db_file file, const struct tag *tag)
{
	if (tag->time >= 0)
		db_printf(file, SONG_TIME "%i\n", tag->time);

	if (tag->has_playlist)
		db_printf(file, "Playlist: yes\n");

	for (unsigned i = 0; i < tag->num_items; i++)
		db_printf(file, "%s: %s\n",
			tag_item_names[tag->items[i]->type],
			tag->items[i]->value);
}

static inline void
simple_db_song_save(db_file fp, const struct song *song)
{
	db_printf(fp, SONG_BEGIN "%s\n", song->uri);

	if (song->end_ms > 0)
		db_printf(fp, "Range: %u-%u\n", song->start_ms, song->end_ms);
	else if (song->start_ms > 0)
		db_printf(fp, "Range: %u-\n", song->start_ms);

	if (song->tag != NULL)
		simple_db_tag_save(fp, song->tag);

	db_printf(fp, SONG_MTIME ": %li\n", (long)song->mtime);
	db_printf(fp, SONG_END "\n");
}

static inline struct song *
simple_db_song_load(db_file fp, struct directory *parent, const char *uri,
	  GString *buffer, GError **error_r)
{
	struct song *song = parent != NULL
		? song_file_new(uri, parent)
		: song_remote_new(uri);
	char *line, *colon;
	enum tag_type type;
	const char *value;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, SONG_END) != 0) {
		colon = strchr(line, ':');
		if (colon == NULL || colon == line) {
			if (song->tag != NULL)
				tag_end_add(song->tag);
			song_free(song);

			g_set_error(error_r, simple_db_quark(), 0,
				    "song_save: unknown line in db: %s", line);
			return NULL;
		}

		*colon++ = 0;
		value = strchug_fast_c(colon);

		if ((type = tag_name_parse(line)) != TAG_NUM_OF_ITEM_TYPES) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			tag_add_item(song->tag, type, value);
		} else if (strcmp(line, "Time") == 0) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			song->tag->time = atoi(value);
		} else if (strcmp(line, "Playlist") == 0) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			song->tag->has_playlist = strcmp(value, "yes") == 0;
		} else if (strcmp(line, SONG_MTIME) == 0) {
			song->mtime = atoi(value);
		} else if (strcmp(line, "Range") == 0) {
			char *endptr;

			song->start_ms = strtoul(value, &endptr, 10);
			if (*endptr == '-')
				song->end_ms = strtoul(endptr + 1, NULL, 10);
		} else {
			if (song->tag != NULL)
				tag_end_add(song->tag);
			song_free(song);

			g_set_error(error_r, simple_db_quark(), 0,
				    "song_save: unknown line in db: %s", line);
			return NULL;
		}
	}

	if (song->tag != NULL)
		tag_end_add(song->tag);

	return song;
}

static inline void
simple_db_playlist_vector_save(db_file fp, const struct list_head *pv)
{
	struct playlist_metadata *pm;
	playlist_vector_for_each(pm, pv)
		db_printf(fp, PLAYLIST_META_BEGIN "%s\n"
			"mtime: %li\n"
			"playlist_end\n",
			pm->name, (long)pm->mtime);
}

static inline bool
playlist_metadata_load(db_file fp, struct list_head *pv, const char *name,
		       GString *buffer, GError **error_r)
{
	struct playlist_metadata pm = {
		.mtime = 0,
	};
	char *line, *colon;
	const char *value;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, "playlist_end") != 0) {
		colon = strchr(line, ':');
		if (colon == NULL || colon == line) {
			g_set_error(error_r, simple_db_quark(), 0,
				    "playlist: unknown line in db: %s", line);
			return false;
		}

		*colon++ = 0;
		value = strchug_fast_c(colon);

		if (strcmp(line, "mtime") == 0)
			pm.mtime = strtol(value, NULL, 10);
		else {
			g_set_error(error_r, simple_db_quark(), 0,
				    "playlist: unknown line in db: %s", line);
			return false;
		}
	}

	playlist_vector_update_or_add(pv, name, pm.mtime);
	return true;
}

static void
simple_db_directory_save(db_file fp, const struct directory *directory)
{
	if (!directory_is_root(directory)) {
		db_printf(fp, DIRECTORY_MTIME "%lu\n",
			(unsigned long)directory->mtime);

		db_printf(fp, "%s%s\n", DIRECTORY_BEGIN,
			directory_get_path(directory));
	}

	struct directory *cur;
	directory_for_each_child(cur, directory) {
		char *base = g_path_get_basename(cur->path);

		db_printf(fp, DIRECTORY_DIR "%s\n", base);
		g_free(base);

		simple_db_directory_save(fp, cur);

		if (db_error(fp))
			return;
	}

	struct song *song;
	directory_for_each_song(song, directory)
		simple_db_song_save(fp, song);

	simple_db_playlist_vector_save(fp, &directory->playlists);

	if (!directory_is_root(directory))
		db_printf(fp, DIRECTORY_END "%s\n",
			directory_get_path(directory));
}

static struct directory *
simple_db_directory_load_subdir(db_file , struct directory *, const char *, GString *,
	       GError **);

static bool
simple_db_directory_load(db_file fp, struct directory *directory,
	       GString *buffer, GError **error)
{
	const char *line;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       !g_str_has_prefix(line, DIRECTORY_END)) {
		if (g_str_has_prefix(line, DIRECTORY_DIR)) {
			struct directory *subdir =
				simple_db_directory_load_subdir(fp, directory,
						      line + sizeof(DIRECTORY_DIR) - 1,
						      buffer, error);
			if (subdir == NULL)
				return false;
		} else if (g_str_has_prefix(line, SONG_BEGIN)) {
			const char *name = line + sizeof(SONG_BEGIN) - 1;
			struct song *song;

			if (directory_get_song(directory, name) != NULL) {
				g_set_error(error, simple_db_quark(), 0,
					    "directory: Duplicate song '%s'", name);
				return NULL;
			}

			song = simple_db_song_load(fp, directory, name,
					 buffer, error);
			if (song == NULL)
				return false;

			directory_add_song(directory, song);
		} else if (g_str_has_prefix(line, PLAYLIST_META_BEGIN)) {
			/* duplicate the name, because
			   playlist_metadata_load() will overwrite the
			   buffer */
			char *name = g_strdup(line + sizeof(PLAYLIST_META_BEGIN) - 1);

			if (!playlist_metadata_load(fp, &directory->playlists,
						    name, buffer, error)) {
				g_free(name);
				return false;
			}

			g_free(name);
		} else {
			g_set_error(error, simple_db_quark(), 0,
				    "Malformed line: %s", line);
			return false;
		}
	}

	return true;
}

static struct directory *
simple_db_directory_load_subdir(db_file fp, struct directory *parent, const char *name,
		      GString *buffer, GError **error_r)
{
	const char *line;
	bool success;

	if (directory_get_child(parent, name) != NULL) {
		g_set_error(error_r, simple_db_quark(), 0,
			    "directory: Duplicate subdirectory '%s'", name);
		return NULL;
	}

	struct directory *directory = directory_new_child(parent, name);

	line = db_read_text_line(fp, buffer);
	if (line == NULL) {
		g_set_error(error_r, simple_db_quark(), 0,
			    "directory: Unexpected end of file");
		directory_delete(directory);
		return NULL;
	}

	if (g_str_has_prefix(line, DIRECTORY_MTIME)) {
		directory->mtime =
			g_ascii_strtoull(line + sizeof(DIRECTORY_MTIME) - 1,
					 NULL, 10);

		line = db_read_text_line(fp, buffer);
		if (line == NULL) {
			g_set_error(error_r, simple_db_quark(), 0,
				    "directory: Unexpected end of file");
			directory_delete(directory);
			return NULL;
		}
	}

	if (!g_str_has_prefix(line, DIRECTORY_BEGIN)) {
		g_set_error(error_r, simple_db_quark(), 0,
			    "directory: Malformed line: %s", line);
		directory_delete(directory);
		return NULL;
	}

	success = simple_db_directory_load(fp, directory, buffer, error_r);
	if (!success) {
		directory_delete(directory);
		return NULL;
	}

	return directory;
}

static inline void
simple_db_save_internal(db_file fp, const struct directory *music_root)
{
	assert(music_root != NULL);

	db_printf(fp, "%s\n", DIRECTORY_INFO_BEGIN);
	db_printf(fp, DB_FORMAT_PREFIX "%u\n", DB_FORMAT);
	db_printf(fp, "%s%s\n", DIRECTORY_MPD_VERSION, VERSION);
	db_printf(fp, "%s%s\n", DIRECTORY_FS_CHARSET, path_get_fs_charset());

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i)
		if (!ignore_tag_items[i])
			db_printf(fp, DB_TAG_PREFIX "%s\n", tag_item_names[i]);

	db_printf(fp, "%s\n", DIRECTORY_INFO_END);

	simple_db_directory_save(fp, music_root);
}

static inline bool
simple_db_load_internal(db_file fp, struct directory *music_root, GError **error)
{
	GString *buffer = g_string_sized_new(1024);
	char *line;
	int format = 0;
	bool found_charset = false, found_version = false;
	bool success;
	bool tags[TAG_NUM_OF_ITEM_TYPES];

	assert(music_root != NULL);

	/* get initial info */
	line = db_read_text_line(fp, buffer);
	if (line == NULL || strcmp(DIRECTORY_INFO_BEGIN, line) != 0) {
		g_set_error(error, db_quark(), 0, "Database corrupted");
		g_string_free(buffer, true);
		return false;
	}

	memset(tags, false, sizeof(tags));

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, DIRECTORY_INFO_END) != 0) {
		if (g_str_has_prefix(line, DB_FORMAT_PREFIX)) {
			format = atoi(line + sizeof(DB_FORMAT_PREFIX) - 1);
		} else if (g_str_has_prefix(line, DIRECTORY_MPD_VERSION)) {
			if (found_version) {
				g_set_error(error, db_quark(), 0,
					    "Duplicate version line");
				g_string_free(buffer, true);
				return false;
			}

			found_version = true;
		} else if (g_str_has_prefix(line, DIRECTORY_FS_CHARSET)) {
			const char *new_charset, *old_charset;
			if (found_charset) {
				g_set_error(error, db_quark(), 0,
					    "Duplicate charset line");
				g_string_free(buffer, true);
				return false;
			}

			found_charset = true;

			new_charset = line + sizeof(DIRECTORY_FS_CHARSET) - 1;
			old_charset = path_get_fs_charset();
			if (old_charset != NULL
			    && strcmp(new_charset, old_charset)) {
				g_set_error(error, db_quark(), 0,
					    "Existing database has charset "
					    "\"%s\" instead of \"%s\"; "
					    "discarding database file",
					    new_charset, old_charset);
				g_string_free(buffer, true);
				return false;
			}
		} else if (g_str_has_prefix(line, DB_TAG_PREFIX)) {
			const char *name = line + sizeof(DB_TAG_PREFIX) - 1;
			enum tag_type tag = tag_name_parse(name);
			if (tag == TAG_NUM_OF_ITEM_TYPES) {
				g_set_error(error, db_quark(), 0,
					    "Unrecognized tag '%s', "
					    "discarding database file",
					    name);
				return false;
			}

			tags[tag] = true;
		} else {
			g_set_error(error, db_quark(), 0,
				    "Malformed line: %s", line);
			g_string_free(buffer, true);
			return false;
		}
	}

	if (format != DB_FORMAT) {
		g_set_error(error, db_quark(), 0,
			    "Database format mismatch, "
			    "discarding database file");
		return false;
	}

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i) {
		if (!ignore_tag_items[i] && !tags[i]) {
			g_set_error(error, db_quark(), 0,
				    "Tag list mismatch, "
				    "discarding database file");
			return false;
		}
	}

	g_debug("reading DB");

	db_lock();
	success = simple_db_directory_load(fp, music_root, buffer, error);
	db_unlock();
	g_string_free(buffer, true);

	return success;
}

MPD_PURE
static const struct directory *
simple_db_lookup_directory(const struct simple_db *db, const char *uri)
{
	assert(db != NULL);
	assert(db->root != NULL);
	assert(uri != NULL);

	db_lock();
	struct directory *directory =
		directory_lookup_directory(db->root, uri);
	db_unlock();
	return directory;
}

static struct db *
simple_db_init(const struct config_param *param, GError **error_r)
{
	struct simple_db *db = g_malloc(sizeof(*db));
	db_base_init(&db->base, &simple_db_plugin);

	GError *error = NULL;
	db->path = config_dup_block_path(param, "path", &error);
	if (db->path == NULL) {
		g_free(db);
		if (error != NULL)
			g_propagate_error(error_r, error);
		else
			g_set_error(error_r, simple_db_quark(), 0,
				    "No \"path\" parameter specified");
		return NULL;
	}

	return &db->base;
}

static void
simple_db_finish(struct db *_db)
{
	struct simple_db *db = (struct simple_db *)_db;

	free(db->path);
	g_free(db);
}

static bool
simple_db_check(struct simple_db *db, GError **error_r)
{
	assert(db != NULL);
	assert(db->path != NULL);

	/* Check if the file exists */
	if (access(db->path, F_OK)) {
		/* If the file doesn't exist, we can't check if we can write
		 * it, so we are going to try to get the directory path, and
		 * see if we can write a file in that */
		char *dirPath = g_path_get_dirname(db->path);

		/* Check that the parent part of the path is a directory */
		struct stat st;
		if (stat(dirPath, &st) < 0) {
			g_free(dirPath);
			g_set_error(error_r, simple_db_quark(), errno,
				    "Couldn't stat parent directory of db file "
				    "\"%s\": %s",
				    db->path, g_strerror(errno));
			return false;
		}

		if (!S_ISDIR(st.st_mode)) {
			g_free(dirPath);
			g_set_error(error_r, simple_db_quark(), 0,
				    "Couldn't create db file \"%s\" because the "
				    "parent path is not a directory",
				    db->path);
			return false;
		}

		/* Check if we can write to the directory */
		if (access(dirPath, X_OK | W_OK)) {
			g_set_error(error_r, simple_db_quark(), errno,
				    "Can't create db file in \"%s\": %s",
				    dirPath, g_strerror(errno));
			g_free(dirPath);
			return false;
		}

		g_free(dirPath);

		return true;
	}

	/* Path exists, now check if it's a regular file */
	struct stat st;
	if (stat(db->path, &st) < 0) {
		g_set_error(error_r, simple_db_quark(), errno,
			    "Couldn't stat db file \"%s\": %s",
			    db->path, g_strerror(errno));
		return false;
	}

	if (!S_ISREG(st.st_mode)) {
		g_set_error(error_r, simple_db_quark(), 0,
			    "db file \"%s\" is not a regular file",
			    db->path);
		return false;
	}

	/* And check that we can write to it */
	if (access(db->path, R_OK | W_OK)) {
		g_set_error(error_r, simple_db_quark(), errno,
			    "Can't open db file \"%s\" for reading/writing: %s",
			    db->path, g_strerror(errno));
		return false;
	}

	return true;
}

static bool
simple_db_load(struct simple_db *db, GError **error_r)
{
	assert(db != NULL);
	assert(db->path != NULL);
	assert(db->root != NULL);

	db_file fp = db_open(db->path, "r");
	if (fp == NULL) {
		g_set_error(error_r, simple_db_quark(), errno,
			    "Failed to open database file \"%s\": %s",
			    db->path, g_strerror(errno));
		return false;
	}

	if (!simple_db_load_internal(fp, db->root, error_r)) {
		db_close(fp);
		return false;
	}

	db_close(fp);

	struct stat st;
	if (stat(db->path, &st) == 0)
		db->mtime = st.st_mtime;

	return true;
}

static bool
simple_db_open(struct db *_db, G_GNUC_UNUSED GError **error_r)
{
	struct simple_db *db = (struct simple_db *)_db;

	db->root = directory_new_root();
	db->mtime = 0;

	GError *error = NULL;
	if (!simple_db_load(db, &error)) {
		directory_free(db->root);

		g_warning("Failed to load database: %s", error->message);
		g_error_free(error);

		if (!simple_db_check(db, error_r))
			return false;

		db->root = directory_new_root();
	}

	return true;
}

static void
simple_db_close(struct db *_db)
{
	struct simple_db *db = (struct simple_db *)_db;

	assert(db->root != NULL);

	directory_free(db->root);
}

static struct song *
simple_db_get_song(struct db *_db, const char *uri, GError **error_r)
{
	struct simple_db *db = (struct simple_db *)_db;

	assert(db->root != NULL);

	db_lock();
	struct song *song = directory_lookup_song(db->root, uri);
	db_unlock();
	if (song == NULL)
		g_set_error(error_r, db_quark(), DB_NOT_FOUND,
			    "No such song: %s", uri);

	return song;
}

static bool
simple_db_visit(struct db *_db, const struct db_selection *selection,
		const struct db_visitor *visitor, void *ctx,
		GError **error_r)
{
	const struct simple_db *db = (const struct simple_db *)_db;
	const struct directory *directory =
		simple_db_lookup_directory(db, selection->uri);
	if (directory == NULL) {
		struct song *song;
		if (visitor->song != NULL &&
		    (song = simple_db_get_song(_db, selection->uri, NULL)) != NULL)
			return visitor->song(song, ctx, error_r);

		g_set_error(error_r, db_quark(), DB_NOT_FOUND,
			    "No such directory");
		return false;
	}

	if (selection->recursive && visitor->directory != NULL &&
	    !visitor->directory(directory, ctx, error_r))
		return false;

	db_lock();
	bool ret = directory_walk(directory, selection->recursive,
				  visitor, ctx, error_r);
	db_unlock();
	return ret;
}

const struct db_plugin simple_db_plugin = {
	.name = "simple",
	.init = simple_db_init,
	.finish = simple_db_finish,
	.open = simple_db_open,
	.close = simple_db_close,
	.get_song = simple_db_get_song,
	.visit = simple_db_visit,
};

struct directory *
simple_db_get_root(struct db *_db)
{
	struct simple_db *db = (struct simple_db *)_db;

	assert(db != NULL);
	assert(db->root != NULL);

	return db->root;
}

bool
simple_db_save(struct db *_db, GError **error_r)
{
	struct simple_db *db = (struct simple_db *)_db;
	struct directory *music_root = db->root;

	db_lock();

	g_debug("removing empty directories from DB");
	directory_prune_empty(music_root);

	g_debug("sorting DB");
	directory_sort(music_root);

	db_unlock();

	g_debug("writing DB");

	db_file fp = db_open(db->path, "w");
	if (!fp) {
		g_set_error(error_r, simple_db_quark(), errno,
			    "unable to write to db file \"%s\": %s",
			    db->path, g_strerror(errno));
		return false;
	}

	simple_db_save_internal(fp, music_root);

	if (db_error(fp)) {
		g_set_error(error_r, simple_db_quark(), errno,
			    "Failed to write to database file: %s",
			    g_strerror(errno));
		db_close(fp);
		return false;
	}

	db_close(fp);

	struct stat st;
	if (stat(db->path, &st) == 0)
		db->mtime = st.st_mtime;

	return true;
}

time_t
simple_db_get_mtime(const struct db *_db)
{
	const struct simple_db *db = (const struct simple_db *)_db;

	assert(db != NULL);
	assert(db->root != NULL);

	return db->mtime;
}
