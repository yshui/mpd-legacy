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

#define LOG_DOMAIN "input_stream"

#include "log.h"
#include "config.h"
#include "input_stream.h"
#include "input_list.h"
#include "input_plugin.h"
#include "input/rewind.h"

#include <glib.h>
#include <assert.h>

struct input_stream *
input_stream_open(const char *url)
{
	int i;
	for(i = 0; input_plugins[i]; i++) {
		if (!input_plugins_enabled[i])
			continue;
		struct input_stream *is;

		is = input_plugins[i]->open(url);
		if (!IS_ERR_OR_NULL(is)) {
			assert(is->plugin != NULL);
			assert(is->plugin->close != NULL);
			assert(is->plugin->read != NULL);
			assert(is->plugin->eof != NULL);
			assert(!is->seekable || is->plugin->seek != NULL);
		}
		return is;
	}

	log_err("Unrecognized URI");
	return ERR_PTR(-MPD_INVAL);
}

int
input_stream_check(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	if (is->plugin->check == NULL)
		return MPD_SUCCESS;
	return is->plugin->check(is);
}

void
input_stream_update(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	if (is->plugin->update != NULL)
		is->plugin->update(is);
}

void
input_stream_wait_ready(struct input_stream *is)
{
	assert(is != NULL);

	while (true) {
		input_stream_update(is);
		if (is->ready)
			break;

		cnd_wait(&is->cond, &is->mutex);
	}
}

void
input_stream_lock_wait_ready(struct input_stream *is)
{
	assert(is != NULL);

	mtx_lock(&is->mutex);
	input_stream_wait_ready(is);
	mtx_unlock(&is->mutex);
}

int
input_stream_seek(struct input_stream *is, off64_t offset, int whence)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	if (!is->seekable)
		return -MPD_NIMPL;

	assert(is->plugin->seek);

	return is->plugin->seek(is, offset, whence);
}

int
input_stream_lock_seek(struct input_stream *is, off64_t offset, int whence)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	if (is->plugin->seek == NULL)
		return false;

	mtx_lock(&is->mutex);
	int ret = input_stream_seek(is, offset, whence);
	mtx_unlock(&is->mutex);
	return ret;
}

struct tag *
input_stream_tag(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	return is->plugin->tag != NULL
		? is->plugin->tag(is)
		: NULL;
}

struct tag *
input_stream_lock_tag(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	if (is->plugin->tag == NULL)
		return false;

	mtx_lock(&is->mutex);
	struct tag *tag = input_stream_tag(is);
	mtx_unlock(&is->mutex);
	return tag;
}

bool
input_stream_available(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	return is->plugin->available != NULL
		? is->plugin->available(is)
		: true;
}

ssize_t
input_stream_read(struct input_stream *is, void *ptr, ssize_t size)
{
	assert(ptr != NULL);
	assert(size > 0);

	return is->plugin->read(is, ptr, size);
}

ssize_t
input_stream_lock_read(struct input_stream *is, void *ptr, ssize_t size)
{
	assert(ptr != NULL);
	assert(size > 0);

	mtx_lock(&is->mutex);
	ssize_t nbytes = input_stream_read(is, ptr, size);
	mtx_unlock(&is->mutex);
	return nbytes;
}

void input_stream_close(struct input_stream *is)
{
	is->plugin->close(is);
}

bool input_stream_eof(struct input_stream *is)
{
	return is->plugin->eof(is);
}

bool
input_stream_lock_eof(struct input_stream *is)
{
	assert(is != NULL);
	assert(is->plugin != NULL);

	mtx_lock(&is->mutex);
	bool eof = input_stream_eof(is);
	mtx_unlock(&is->mutex);
	return eof;
}

