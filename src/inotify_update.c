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

#define LOG_DOMAIN "inotify_update"

#include "config.h" /* must be first for large file support */
#include "log.h"
#include "inotify_update.h"
#include "inotify_source.h"
#include "inotify_queue.h"
#include "database.h"
#include "mapper.h"
#include "path.h"
#include "file_utils.h"
#include "macros.h"

#include <assert.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "inotify"

enum {
	IN_MASK = IN_ATTRIB|IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_DELETE_SELF
	|IN_MOVE|IN_MOVE_SELF
#ifdef IN_ONLYDIR
	|IN_ONLYDIR
#endif
};

struct watch_directory {
	struct watch_directory *parent;

	char *name;

	int descriptor;

	GList *children;
};

static struct mpd_inotify_source *inotify_source;

static unsigned inotify_max_depth;
static struct watch_directory inotify_root;
static GTree *inotify_directories;

static gint
compare(gconstpointer a, gconstpointer b)
{
	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static void
tree_add_watch_directory(struct watch_directory *directory)
{
	g_tree_insert(inotify_directories,
		      GINT_TO_POINTER(directory->descriptor), directory);
}

static void
tree_remove_watch_directory(struct watch_directory *directory)
{
	G_GNUC_UNUSED
	bool found = g_tree_remove(inotify_directories,
				   GINT_TO_POINTER(directory->descriptor));
	assert(found);
}

static struct watch_directory *
tree_find_watch_directory(int wd)
{
	return g_tree_lookup(inotify_directories, GINT_TO_POINTER(wd));
}

static void
remove_watch_directory(struct watch_directory *directory)
{
	assert(directory != NULL);

	if (directory->parent == NULL) {
		log_warning("music directory was removed - "
			  "cannot continue to watch it");
		return;
	}

	assert(directory->parent->children != NULL);

	tree_remove_watch_directory(directory);

	while (directory->children != NULL)
		remove_watch_directory(directory->children->data);

	directory->parent->children =
		g_list_remove(directory->parent->children, directory);

	mpd_inotify_source_rm(inotify_source, directory->descriptor);
	free(directory->name);
	g_slice_free(struct watch_directory, directory);
}

static char *
watch_directory_get_uri_fs(const struct watch_directory *directory)
{
	char *parent_uri, *uri;

	if (directory->parent == NULL)
		return NULL;

	parent_uri = watch_directory_get_uri_fs(directory->parent);
	if (parent_uri == NULL)
		return strdup(directory->name);

	uri = build_db_filename(parent_uri, directory->name, NULL);
	free(parent_uri);

	return uri;
}

/* we don't look at "." / ".." nor files with newlines in their name */
static bool skip_path(const char *path)
{
	return (path[0] == '.' && path[1] == 0) ||
		(path[0] == '.' && path[1] == '.' && path[2] == 0) ||
		strchr(path, '\n') != NULL;
}

static void
recursive_watch_subdirectories(struct watch_directory *directory,
			       const char *path_fs, unsigned depth)
{
	DIR *dir;
	struct dirent *ent;

	assert(directory != NULL);
	assert(depth <= inotify_max_depth);
	assert(path_fs != NULL);

	++depth;

	if (depth > inotify_max_depth)
		return;

	dir = opendir(path_fs);
	if (dir == NULL) {
		log_warning("Failed to open directory %s: %s",
			  path_fs, strerror(errno));
		return;
	}

	while ((ent = readdir(dir))) {
		char *child_path_fs;
		struct stat st;
		int ret;
		struct watch_directory *child;

		if (skip_path(ent->d_name))
			continue;

		child_path_fs = build_db_filename(path_fs, ent->d_name, NULL);
		ret = stat(child_path_fs, &st);
		if (ret < 0) {
			log_warning("Failed to stat %s: %s",
				  child_path_fs, strerror(errno));
			free(child_path_fs);
			continue;
		}

		if (!S_ISDIR(st.st_mode)) {
			free(child_path_fs);
			continue;
		}

		ret = mpd_inotify_source_add(inotify_source, child_path_fs,
					     IN_MASK);
		if (ret < 0) {
			log_warning("Failed to register %s",
				  child_path_fs);
			free(child_path_fs);
			continue;
		}

		child = tree_find_watch_directory(ret);
		if (child != NULL) {
			/* already being watched */
			free(child_path_fs);
			continue;
		}

		child = g_slice_new(struct watch_directory);
		child->parent = directory;
		child->name = strdup(ent->d_name);
		child->descriptor = ret;
		child->children = NULL;

		directory->children = g_list_prepend(directory->children,
						     child);

		tree_add_watch_directory(child);

		recursive_watch_subdirectories(child, child_path_fs, depth);
		free(child_path_fs);
	}

	closedir(dir);
}

MPD_PURE
static unsigned
watch_directory_depth(const struct watch_directory *d)
{
	assert(d != NULL);

	unsigned depth = 0;
	while ((d = d->parent) != NULL)
		++depth;

	return depth;
}

static void
mpd_inotify_callback(int wd, unsigned mask,
		     const char *name, void *ctx)
{
	struct watch_directory *directory;
	char *uri_fs;

	bool new_directory = false;
	const char *root = mapper_get_music_directory_fs();
	const char *path_fs;
	char *allocated = NULL;
	struct stat st;

	/*log_debug("wd=%d mask=0x%x name='%s'", wd, mask, name);*/

	directory = tree_find_watch_directory(wd);
	if (directory == NULL)
		return;

	uri_fs = watch_directory_get_uri_fs(directory);

	if (uri_fs != NULL)
		path_fs = allocated = build_db_filename(root, uri_fs, NULL);
	else
		path_fs = root;

	if ((mask & (IN_ATTRIB|IN_CREATE)) != 0) {
		char *new_path_fs = build_db_filename(path_fs, name, NULL);

		int ret = stat(new_path_fs, &st);
		if (ret < 0)
			log_warning("Failed to stat %s: %s",
					new_path_fs, strerror(errno));
		else
			new_directory = S_ISDIR(st.st_mode);

		free(new_path_fs);
	}

	if ((mask & (IN_DELETE_SELF|IN_MOVE_SELF)) != 0) {
		free(uri_fs);
		remove_watch_directory(directory);
		return;
	}


	if ((mask & (IN_ATTRIB|IN_CREATE|IN_MOVE)) != 0 && new_directory)
		/* a sub directory was changed: register those in
		   inotify */

		recursive_watch_subdirectories(directory, path_fs,
					       watch_directory_depth(directory));

	if ((mask & (IN_CLOSE_WRITE|IN_MOVE|IN_DELETE)) != 0 ||
	    /* at the maximum depth, we watch out for newly created directories */
	    (watch_directory_depth(directory) == inotify_max_depth && new_directory) ||
	    ((mask & IN_ISDIR) == 0 && new_directory)) {
		/* a file was changed, or a directory was
		   moved/deleted, or a new symbolic link to
		   a directory was created: queue a database
		   update */
		char *uri_utf8 = uri_fs != NULL
			? fs_charset_to_utf8(uri_fs)
			: strdup("");

		if (uri_utf8 != NULL)
			/* this function will take care of freeing
			   uri_utf8 */
			mpd_inotify_enqueue(uri_utf8);
	}

	free(uri_fs);
	free(allocated);
}

void
mpd_inotify_init(unsigned max_depth)
{
	log_debug("initializing inotify");

	const char *path = mapper_get_music_directory_fs();
	if (path == NULL) {
		log_debug("no music directory configured");
		return;
	}

	inotify_source = mpd_inotify_source_new(mpd_inotify_callback, NULL);
	if (IS_ERR(inotify_source)) {
		log_warning("Failed to create inotify source.");
		return;
	}

	inotify_max_depth = max_depth;

	inotify_root.name = strdup(path);
	inotify_root.descriptor = mpd_inotify_source_add(inotify_source, path,
							 IN_MASK);
	if (inotify_root.descriptor < 0) {
		log_warning("Failed to add root inotify source");
		mpd_inotify_source_free(inotify_source);
		inotify_source = NULL;
		return;
	}

	inotify_directories = g_tree_new(compare);
	tree_add_watch_directory(&inotify_root);

	recursive_watch_subdirectories(&inotify_root, path, 0);

	mpd_inotify_queue_init();

	log_debug("watching music directory");
}

static gboolean
free_watch_directory(gpointer key, gpointer value,
		     gpointer data)
{
	struct watch_directory *directory = value;

	free(directory->name);
	g_list_free(directory->children);

	if (directory != &inotify_root)
		g_slice_free(struct watch_directory, directory);

	return false;
}

void
mpd_inotify_finish(void)
{
	if (inotify_source == NULL)
		return;

	mpd_inotify_queue_finish();
	mpd_inotify_source_free(inotify_source);

	g_tree_foreach(inotify_directories, free_watch_directory, NULL);
	g_tree_destroy(inotify_directories);
}
