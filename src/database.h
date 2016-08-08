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

#pragma once

#include "compiler.h"
#include "db_error.h"

#include <glib.h>

#include <sys/time.h>
#include <stdbool.h>

struct config_param;
struct directory;
struct db_selection;
struct db_visitor;

/**
 * Initialize the database library.
 *
 * @param path the absolute path of the database file
 */
int
db_init(const struct config_param *path);

void
db_finish(void);

/**
 * Returns the root directory object.  Returns NULL if there is no
 * configured music directory.
 */
MPD_PURE
struct directory *
db_get_root(void);

/**
 * Caller must lock the #db_mutex.
 */
MPD_PURE
struct directory *
db_get_directory(const char *name);

MPD_PURE
struct song *
db_get_song(const char *file);

int
db_visit(const struct db_selection *selection,
	 const struct db_visitor *visitor, void *ctx);

int
db_walk(const char *uri,
	const struct db_visitor *visitor, void *ctx);

int
db_save(void);

int
db_load(void);

MPD_PURE
time_t
db_get_mtime(void);

/**
 * Returns true if there is a valid database file on the disk.
 */
MPD_PURE
static inline bool
db_exists(void)
{
	/* mtime is set only if the database file was loaded or saved
	   successfully */
	return db_get_mtime() > 0;
}
