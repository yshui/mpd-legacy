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
#include "input_internal.h"
#include "input_stream.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

void
input_stream_init(struct input_stream *is, const struct input_plugin *plugin,
		  const char *uri)
{
	assert(is != NULL);
	assert(plugin != NULL);
	assert(uri != NULL);

	is->plugin = plugin;
	is->uri = strdup(uri);
	mtx_init(&is->mutex, mtx_plain);
	cnd_init(&is->cond);
	is->ready = false;
	is->seekable = false;
	is->size = -1;
	is->offset = 0;
	is->mime = NULL;
}

void
input_stream_deinit(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	free(is->uri);
	free(is->mime);

	mtx_destroy(&is->mutex);
	cnd_destroy(&is->cond);
}

void
input_stream_signal_client(struct input_stream *is)
{
	cnd_broadcast(&is->cond);
}

void
input_stream_set_ready(struct input_stream *is)
{
	mtx_lock(&is->mutex);

	if (!is->ready) {
		is->ready = true;
		input_stream_signal_client(is);
	}

	mtx_unlock(&is->mutex);
}
