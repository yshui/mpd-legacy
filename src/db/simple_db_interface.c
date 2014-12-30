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

#define LOG_DOMAIN "database: simple_db"

#include "config.h"
#include "simple_db_interface.h"
#include "simple_db_io.h"
#include "db_internal.h"
#include "db_error.h"
#include "db_selection.h"
#include "db_visitor.h"
#include "conf.h"
#include "directory.h"
#include "string_util.h"
#include "playlist_vector.h"
#include "path.h"
#include "util/ythread.h"

#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static yref_ret_t
simple_db_lookup(const struct d_entry *d, const char *name) {
	struct simple_db_dir *dd = d->db_data;
	struct directory *dir = dd->dir;

	dir = directory_get_child(dir, name);
	dir->external_ref++;

	char *path = tmalloc(char, strlen(d->name)+strlen(name)+2);
	strcpy(path, d->name);
	path[strlen(path)] = '/';
	strcat(path, name);

	struct d_entry *nd = NULL;
	yref_return_get(d_new(path, &simple_db_plugin), nd, refcount);

	struct simple_db_dir *ndd = tmalloc(struct simple_db_dir, 1);
	ndd->dir = dir;
	ndd->db = dd->db;
	ndd->db->refcount++;

	nd->db_data = ndd;

	yref_return(nd, refcount);
}

static int
simple_db_check(struct simple_db *db)
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
			free(dirPath);
			log_err("Couldn't stat parent directory of db file "
				    "\"%s\": %s",
				    db->path, strerror(errno));
			return -DB_ACCESS;
		}

		if (!S_ISDIR(st.st_mode)) {
			free(dirPath);
			log_err("Couldn't create db file \"%s\" because the "
				    "parent path is not a directory",
				    db->path);
			return -DB_ACCESS;
		}

		/* Check if we can write to the directory */
		if (access(dirPath, X_OK | W_OK)) {
			log_err("Can't create db file in \"%s\": %s",
				    dirPath, strerror(errno));
			free(dirPath);
			return -DB_ACCESS;
		}

		free(dirPath);

		return MPD_SUCCESS;
	}

	/* Path exists, now check if it's a regular file */
	struct stat st;
	if (stat(db->path, &st) < 0) {
		log_err("Couldn't stat db file \"%s\": %s",
			    db->path, strerror(errno));
		return -DB_ACCESS;
	}

	if (!S_ISREG(st.st_mode)) {
		log_err("db file \"%s\" is not a regular file",
			    db->path);
		return -DB_ACCESS;
	}

	/* And check that we can write to it */
	if (access(db->path, R_OK | W_OK)) {
		log_err("Can't open db file \"%s\" for reading/writing: %s",
			    db->path, strerror(errno));
		return -DB_ACCESS;
	}

	return MPD_SUCCESS;
}

static void *
simple_db_open(const struct config_param *param)
{
	struct simple_db *db = malloc(sizeof(*db));

	db->path = config_dup_block_path(param, "path");
	if (db->path == NULL) {
		free(db);
			log_err("No \"path\" parameter specified");
		return NULL;
	}

	db->root = directory_new_root();
	db->mtime = 0;

	int ret = simple_db_load(db);
	if (ret != MPD_SUCCESS) {
		directory_free(db->root);

		log_warning("Failed to load database.");

		ret = simple_db_check(db);
		if (ret != MPD_SUCCESS)
			return NULL;

		db->root = directory_new_root();
	}

	struct simple_db_dir *dd = tmalloc(struct simple_db_dir, 1);
	dd->dir = db->root;
	dd->db = db;
	db->refcount = 1;

	return dd;
}

static void
simple_db_close(void *data)
{
	struct simple_db_dir *dd = data;

	//Make sure no one holds any dentry from
	//this database.
	dd->db->refcount--;
	assert(!dd->db->refcount);

	directory_free(dd->db->root);
	free(dd->db->path);
	free(dd->db);
	free(dd);
}

static struct song *
simple_db_get_song(void *data, const char *uri)
{
	struct simple_db_dir *dd = (struct simple_db_dir *)data;
	struct simple_db *db = dd->db;

	assert(db->root != NULL);

	_simple_db_lock(db);
	struct song *song = directory_lookup_song(db->root, uri);
	if (song == NULL)
		log_err("No such song: %s", uri);

	return song;
}

static int
simple_db_visit(struct db *_db, const struct db_selection *selection,
		const struct db_visitor *visitor, void *ctx)
{
	const struct simple_db *db = (const struct simple_db *)_db;
	const struct directory *directory =
		simple_db_lookup_directory(db, selection->uri);
	if (directory == NULL) {
		struct song *song;
		if (visitor->song != NULL &&
		    (song = simple_db_get_song(_db, selection->uri)) != NULL)
			return visitor->song(song, ctx);

		log_err("No such directory");
		return -DB_NOENT;
	}

	int ret;
	if (selection->recursive && visitor->directory != NULL) {
		ret = visitor->directory(directory, ctx);
		if (ret != MPD_SUCCESS)
			return ret;
	}

	_simple_db_lock(db);
	ret = directory_walk(directory, selection->recursive,
				  visitor, ctx);
	_simple_db_lock(db);
	return ret;
}

static void simple_db_free(void *data) {
	struct simple_db_dir *dd = data;
	dd->dir->external_ref--;
	if (dd->dir->external_ref == 0 &&
	    dd->dir->deleted)
		free(dd->dir);
	dd->db->refcount--;
	free(dd);
}

static void simple_db_lock(void *data) {
	struct simple_db_dir *dd = data;
	_simple_db_lock(dd->db);
}

static void simple_db_unlock(void *data) {
	struct simple_db_dir *dd = data;
	_simple_db_unlock(dd->db);
}

static void simple_db_sync(void *data) {
	struct simple_db_dir *dd = data;
	simple_db_save(dd->db);
}

const struct db_plugin simple_db_plugin = {
	.name = "simple",
	.open = simple_db_open,
	.close = simple_db_close,
	.lookup = simple_db_lookup,
	.get_song = simple_db_get_song,
	.free = simple_db_free,
	.lock = simple_db_lock,
	.unlock = simple_db_unlock,
	.sync = simple_db_sync,
	.visit = simple_db_visit,
};
