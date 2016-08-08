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

#define LOG_DOMAIN "dc"

#include "log.h"

#include "config.h"
#include "decoder_control.h"
#include "pipe.h"
#include "macros.h"

#include <assert.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "decoder_control"

struct decoder_control *
dc_new(struct player_control *pc)
{
	struct decoder_control *dc = tmalloc(struct decoder_control, 1);

	dc->pc = pc;

	cnd_init(&dc->cond);
	mtx_init(&dc->mutex, mtx_plain);
	cnd_init(&dc->client_cond);
	mtx_init(&dc->client_mutex, mtx_plain);

	dc->state = DECODE_STATE_STOP;
	dc->command = DECODE_COMMAND_NONE;

	dc->replay_gain_db = 0;
	dc->replay_gain_prev_db = 0;
	dc->mixramp_start = NULL;
	dc->mixramp_end = NULL;
	dc->mixramp_prev_end = NULL;

	return dc;
}

void
dc_free(struct decoder_control *dc)
{
	mtx_destroy(&dc->mutex);
	cnd_destroy(&dc->cond);
	mtx_destroy(&dc->client_mutex);
	cnd_destroy(&dc->client_cond);

	free(dc->mixramp_start);
	free(dc->mixramp_end);
	free(dc->mixramp_prev_end);
	free(dc);
}

static void
dc_command(struct decoder_control *dc, enum decoder_command cmd)
{
	log_err("Before dc->mutex x %lu", thrd_current());
	mtx_lock(&dc->mutex);
	log_err("After dc->mutex x %lu", thrd_current());
	dc->command = cmd;
	mtx_lock(&dc->client_mutex);
	dc->state = DECODE_STATE_PENDING;
	log_err("After dc->client_mutex x %lu", thrd_current());
	mtx_unlock(&dc->mutex);
	log_err("After unlock dc->mutex x %lu", thrd_current());

	cnd_signal(&dc->cond);

	while (dc->state == DECODE_STATE_PENDING) {
		log_err("Start to wait x %lu", thrd_current());
		cnd_wait(&dc->client_cond, &dc->client_mutex);
	}
	mtx_unlock(&dc->client_mutex);
}

static void
dc_command_async(struct decoder_control *dc, enum decoder_command cmd)
{
	mtx_lock(&dc->mutex);

	dc->command = cmd;
	cnd_signal(&dc->cond);

	mtx_unlock(&dc->mutex);
}

void
dc_start(struct decoder_control *dc, struct song *song,
	 unsigned start_ms, unsigned end_ms,
	 struct music_buffer *buffer, struct music_pipe *pipe)
{
	assert(song != NULL);
	assert(buffer != NULL);
	assert(pipe != NULL);
	assert(music_pipe_empty(pipe));

	dc->song = song;
	dc->start_ms = start_ms;
	dc->end_ms = end_ms;
	dc->buffer = buffer;
	dc->pipe = pipe;
	dc_command(dc, DECODE_COMMAND_START);
}

void
dc_stop(struct decoder_control *dc)
{
	if (dc->command != DECODE_COMMAND_NONE)
		/* Attempt to cancel the current command.  If it's too
		   late and the decoder thread is already executing
		   the old command, we'll call STOP again in this
		   function (see below). */
		dc_command(dc, DECODE_COMMAND_STOP);

	if (dc->state != DECODE_STATE_STOP && dc->state != DECODE_STATE_ERROR)
		dc_command(dc, DECODE_COMMAND_STOP);
}

bool
dc_seek(struct decoder_control *dc, double where)
{
	assert(dc->state != DECODE_STATE_START);
	assert(where >= 0.0);

	if (dc->state == DECODE_STATE_STOP ||
	    dc->state == DECODE_STATE_ERROR || !dc->seekable)
		return false;

	dc->seek_where = where;
	dc->seek_error = false;
	dc_command(dc, DECODE_COMMAND_SEEK);

	if (dc->seek_error)
		return false;

	return true;
}

void
dc_quit(struct decoder_control *dc)
{
	dc->quit = true;
	dc_command_async(dc, DECODE_COMMAND_STOP);

	thrd_join(dc->thread, NULL);
}

void
dc_mixramp_start(struct decoder_control *dc, char *mixramp_start)
{
	assert(dc != NULL);

	free(dc->mixramp_start);
	dc->mixramp_start = mixramp_start;
}

void
dc_mixramp_end(struct decoder_control *dc, char *mixramp_end)
{
	assert(dc != NULL);

	free(dc->mixramp_end);
	dc->mixramp_end = mixramp_end;
}

void
dc_mixramp_prev_end(struct decoder_control *dc, char *mixramp_prev_end)
{
	assert(dc != NULL);

	free(dc->mixramp_prev_end);
	dc->mixramp_prev_end = mixramp_prev_end;
}
