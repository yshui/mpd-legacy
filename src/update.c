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
#include "update.h"
#include "update_queue.h"
#include "update_walk.h"
#include "update_remove.h"
#include "update.h"
#include "database.h"
#include "mapper.h"
#include "playlist.h"
#include "event_pipe.h"
#include "update.h"
#include "idle.h"
#include "stats.h"
#include "main.h"
#include "mpd_error.h"

#include <glib.h>

#include <assert.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "update"

static enum update_progress {
	UPDATE_PROGRESS_IDLE = 0,
	UPDATE_PROGRESS_RUNNING = 1,
	UPDATE_PROGRESS_DONE = 2
} progress;

static bool modified;

static GThread *update_thr;

static const unsigned update_task_id_max = 1 << 15;

static unsigned update_task_id;

/* XXX this flag is passed to update_task() */
static bool discard;

unsigned
isUpdatingDB(void)
{
	return (progress != UPDATE_PROGRESS_IDLE) ? update_task_id : 0;
}

static void * update_task(void *_path)
{
	const char *path = _path;

	if (*path != 0)
		log_debug("starting: %s", path);
	else
		log_debug("starting");

	modified = update_walk(path, discard);

	if (modified || !db_exists()) {
		int ret = db_save();
		if (ret != MPD_SUCCESS)
			log_warning("Failed to save database");
	}

	if (*path != 0)
		log_debug("finished: %s", path);
	else
		log_debug("finished");
	free(_path);

	progress = UPDATE_PROGRESS_DONE;
	event_pipe_emit(PIPE_EVENT_UPDATE);
	return NULL;
}

static void
spawn_update_task(const char *path)
{
	GError *e = NULL;

	assert(g_thread_self() == main_task);

	progress = UPDATE_PROGRESS_RUNNING;
	modified = false;

	if(path == NULL)
		path = "";

	update_thr = g_thread_create(update_task, strdup(path), TRUE, &e);
	if (update_thr == NULL)
		MPD_ERROR("Failed to spawn update task: %s", e->message);

	if (++update_task_id > update_task_id_max)
		update_task_id = 1;
	log_debug("spawned thread for update job id %i", update_task_id);
}

unsigned
update_enqueue(const char *path, bool _discard)
{
	assert(g_thread_self() == main_task);

	if (!mapper_has_music_directory())
		return 0;

	if (progress != UPDATE_PROGRESS_IDLE) {
		unsigned next_task_id =
			update_queue_push(path, discard, update_task_id);
		if (next_task_id == 0)
			return 0;

		return next_task_id > update_task_id_max ?  1 : next_task_id;
	}

	discard = _discard;
	spawn_update_task(path);

	idle_add(IDLE_UPDATE);

	return update_task_id;
}

/**
 * Called in the main thread after the database update is finished.
 */
static void update_finished_event(void)
{
	char *path;

	assert(progress == UPDATE_PROGRESS_DONE);

	g_thread_join(update_thr);

	idle_add(IDLE_UPDATE);

	if (modified) {
		/* send "idle" events */
		playlist_increment_version_all(&g_playlist);
		idle_add(IDLE_DATABASE);
	}

	path = update_queue_shift(&discard);
	if (path != NULL) {
		/* schedule the next path */
		spawn_update_task(path);
		free(path);
	} else {
		progress = UPDATE_PROGRESS_IDLE;

		stats_update();
	}
}

void update_global_init(void)
{
	event_pipe_register(PIPE_EVENT_UPDATE, update_finished_event);

	update_remove_global_init();
	update_walk_global_init();
}

void update_global_finish(void)
{
	update_walk_global_finish();
	update_remove_global_finish();
}
