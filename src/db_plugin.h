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

/** \file
 *
 * This header declares the db_plugin class.  It describes a
 * plugin API for databases of song metadata.
 */

#ifndef MPD_DB_PLUGIN_H
#define MPD_DB_PLUGIN_H

#include "log.h"
#include "macros.h"
#include "util/yref.h"
#include "util/list.h"
#include "dcache.h"

#include <glib.h>
#include <assert.h>
#include <stdbool.h>

struct config_param;
struct db_selection;
struct db_visitor;

struct dir_child {
	char *name;
	enum child_type {
		CHLD_FILE, /* Regular file */
		CHLD_SYM, /* Symbolic link */
		CHLD_DIR, /* Directory */
	} type;
	struct list_head siblings;
};

struct db_plugin {
	const char *name;

	//Note: the void *data passed to all these function can be
	//db specific data assigned to any directories.

	void (*lock)(void *data);

	void (*unlock)(void *data);

	/**
	 * Close database.
         */
	void (*close)(void *data);

	/*
         * Open the database.  Read it into memory if applicable.
	 */
	void *(*open)(const struct config_param *param);

	/*
	 * Create a new node on a given path.
	 * If one of the path component is missing, this should fail
	 */
	void (*mknod)(const char *path, enum dir_name_type type, void *entry);

	/*
	 * Create a new node under a given directory, with name #name,
	 */
	void (*mknodat)(void *data, const char *name,
			enum dir_name_type type, void *entry);
	/*
	 * lookup a sub-directory.
	 */
	yref_ret_t (*lookup)(const struct d_entry *data, const char *name);

	/*
	 * Read directory contents
	 *
	 * #head should be filled with entries of struct dir_name
	 */
	void (*readdir)(void *data, struct list_head *head);

	int (*stat)(void *data, const char *name, void *dst, size_t *len);

	/*
	 * Map internal path to an uri.
	 */
	char *(*realpath)(void *data);

	/*
	 * Map real path to internal path.
	 */
	char *(*vpath)(const char *path);

	/*
	 * Free plugin specific data
	 */
	void (*free)(void *data);

	/*
	 * Get child uris from a given uri (not from the db,
	 * but *actual* child uris, used for updating.
	 *
	 * Should put a list of sturct dir_child into list #h.
	 *
	 * #uri is guarenteed to be from a previous realpath call,
	 * #follow indicates if link should be dereferenced, if #follow is true,
	 * dir_child->type can't be DIR_SYM.
	 */
	void (*get_child)(const char *uri, struct list_head *h, bool follow);

	/*
	 * Sync a database to disk
	 */
	void (*sync)(void *data);

	/**
         * Look up a song (including tag data) in the database.
	 *
	 * @param the URI of the song within the music directory
	 * (UTF-8)
	 */
	struct song *(*get_song)(void *data, const char *uri);

	/**
	 * Visit the selected entities.
	 */
	int (*visit)(void *data, const struct db_selection *selection,
		      const struct db_visitor *visitor, void *ctx);
};

#endif
