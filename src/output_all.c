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
#include "output_all.h"
#include "output_internal.h"
#include "output_control.h"
#include "chunk.h"
#include "conf.h"
#include "pipe.h"
#include "player_control.h"
#include "mpd_error.h"
#include "notify.h"

#ifndef NDEBUG
#include "chunk.h"
#endif

#include <assert.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "output"

static struct audio_format input_audio_format;

static struct audio_output **audio_outputs;
static unsigned int num_audio_outputs;

/**
 * The #audio_pipe object which feeds all audio outputs.
 */
static struct audio_pipe *g_p;

/**
 * The "elapsed_time" stamp of the most recently finished chunk.
 */
static float audio_output_all_elapsed_time = -1.0;

unsigned int audio_output_count(void)
{
	return num_audio_outputs;
}

struct audio_output *
audio_output_get(unsigned i)
{
	assert(i < num_audio_outputs);

	assert(audio_outputs[i] != NULL);

	return audio_outputs[i];
}

struct audio_output *
audio_output_find(const char *name)
{
	for (unsigned i = 0; i < num_audio_outputs; ++i) {
		struct audio_output *ao = audio_output_get(i);

		if (strcmp(ao->name, name) == 0)
			return ao;
	}

	/* name not found */
	return NULL;
}

static unsigned
audio_output_config_count(void)
{
	unsigned int nr = 0;
	const struct config_param *param = NULL;

	while ((param = config_get_next_param(CONF_AUDIO_OUTPUT, param)))
		nr++;
	if (!nr)
		nr = 1; /* we'll always have at least one device  */
	return nr;
}

void
audio_output_all_init(struct player_control *pc)
{
	const struct config_param *param = NULL;
	unsigned int i;

	notify_init(&audio_output_client_notify);

	num_audio_outputs = audio_output_config_count();
	audio_outputs = tmalloc(struct audio_output *, num_audio_outputs);

	for (i = 0; i < num_audio_outputs; i++)
	{
		unsigned int j;

		param = config_get_next_param(CONF_AUDIO_OUTPUT, param);

		/* only allow param to be NULL if there just one audioOutput */
		assert(param || (num_audio_outputs == 1));

		struct audio_output *output = audio_output_new(param, pc);
		if (IS_ERR_OR_NULL(output)) {
			if (param != NULL)
				MPD_ERROR("at line %i", param->line);
			else
				MPD_ERROR("Failed to parse audio output");
		}

		audio_outputs[i] = output;

		/* require output names to be unique: */
		for (j = 0; j < i; j++) {
			if (!strcmp(output->name, audio_outputs[j]->name)) {
				MPD_ERROR("output devices with identical "
					  "names: %s\n", output->name);
			}
		}
	}
}

void
audio_output_all_finish(void)
{
	unsigned int i;

	for (i = 0; i < num_audio_outputs; i++) {
		audio_output_disable(audio_outputs[i]);
		audio_output_finish(audio_outputs[i]);
	}

	free(audio_outputs);
	audio_outputs = NULL;
	num_audio_outputs = 0;

	notify_deinit(&audio_output_client_notify);
}

void
audio_output_all_enable_disable(void)
{
	for (unsigned i = 0; i < num_audio_outputs; i++) {
		struct audio_output *ao = audio_outputs[i];
		bool enabled;

		g_mutex_lock(ao->mutex);
		enabled = ao->really_enabled;
		g_mutex_unlock(ao->mutex);

		if (ao->enabled != enabled) {
			if (ao->enabled)
				audio_output_enable(ao);
			else
				audio_output_disable(ao);
		}
	}
}

/**
 * Determine if all (active) outputs have finished the current
 * command.
 */
static bool
audio_output_all_finished(void)
{
	for (unsigned i = 0; i < num_audio_outputs; ++i) {
		struct audio_output *ao = audio_outputs[i];
		bool not_finished;

		g_mutex_lock(ao->mutex);
		not_finished = audio_output_is_open(ao) &&
			!audio_output_command_is_finished(ao);
		g_mutex_unlock(ao->mutex);

		if (not_finished)
			return false;
	}

	return true;
}

static void audio_output_wait_all(void)
{
	while (!audio_output_all_finished())
		notify_wait(&audio_output_client_notify);
}

/**
 * Signals all audio outputs which are open.
 */
static void
audio_output_allow_play_all(void)
{
	for (unsigned i = 0; i < num_audio_outputs; ++i)
		audio_output_allow_play(audio_outputs[i]);
}

static void
audio_output_reset_reopen(struct audio_output *ao)
{
	g_mutex_lock(ao->mutex);

	if (!ao->open && ao->fail_timer != NULL) {
		g_timer_destroy(ao->fail_timer);
		ao->fail_timer = NULL;
	}

	g_mutex_unlock(ao->mutex);
}

/**
 * Resets the "reopen" flag on all audio devices.  MPD should
 * immediately retry to open the device instead of waiting for the
 * timeout when the user wants to start playback.
 */
static void
audio_output_all_reset_reopen(void)
{
	for (unsigned i = 0; i < num_audio_outputs; ++i) {
		struct audio_output *ao = audio_outputs[i];

		audio_output_reset_reopen(ao);
	}
}

/**
 * Opens all output devices which are enabled, but closed.
 *
 * @return true if there is at least open output device which is open
 */
static bool
audio_output_all_update(void)
{
	unsigned int i;
	bool ret = false;

	if (!audio_format_defined(&input_audio_format))
		return false;

	for (i = 0; i < num_audio_outputs; ++i)
		ret = audio_output_update(audio_outputs[i],
					  &input_audio_format, g_p) || ret;

	return ret;
}

bool
audio_output_all_open(const struct audio_format *audio_format,
		      struct audio_pipe *p)
{
	bool ret = false, enabled = false;
	unsigned int i;

	assert(audio_format != NULL);
	assert((g_p == NULL) || (g_p == p));

	if (g_p == NULL)
		g_p = p;

	input_audio_format = *audio_format;

	audio_output_all_reset_reopen();
	audio_output_all_enable_disable();
	audio_output_all_update();

	for (i = 0; i < num_audio_outputs; ++i) {
		if (audio_outputs[i]->enabled)
			enabled = true;

		if (audio_outputs[i]->open)
			ret = true;
	}

	if (!enabled)
		log_warning("All audio outputs are disabled");

	if (!ret)
		/* close all devices if there was an error */
		audio_output_all_close();

	return ret;
}

#if 0
/**
 * There's only one chunk left in the pipe (#g_p), and all audio
 * outputs have consumed it already.  Clear the reference.
 */
static void
clear_tail_chunk(const struct audio_chunk *chunk, bool *locked)
{
	assert(chunk->next == NULL);

	for (unsigned i = 0; i < num_audio_outputs; ++i) {
		struct audio_output *ao = audio_outputs[i];

		/* this mutex will be unlocked by the caller when it's
		   ready */
		g_mutex_lock(ao->mutex);
		locked[i] = ao->open;

		if (!locked[i]) {
			g_mutex_unlock(ao->mutex);
			continue;
		}

		assert(ao->chunk == chunk);
		assert(ao->chunk_finished);
		ao->chunk = NULL;
	}
}

 * all_check() no longer needed because now chunks are returned to pipe->availabe
 * as soon as they are consumed

unsigned
audio_output_all_check(void)
{
	const struct audio_chunk *chunk;
	bool is_tail;
	struct audio_chunk *shifted;
	bool locked[num_audio_outputs];

	assert(g_music_buffer != NULL);
	assert(g_mp != NULL);

	while ((chunk = audio_pipe_peek(g_mp)) != NULL) {
		assert(!audio_pipe_empty(g_mp));

		if (!chunk_is_consumed(chunk))
			/* at least one output is not finished playing
			   this chunk */
			return audio_pipe_size(g_mp);

		if (chunk->length > 0 && chunk->times >= 0.0)
			/* only update elapsed_time if the chunk
			   provides a defined value */
			audio_output_all_elapsed_time = chunk->times;

		is_tail = chunk->next == NULL;
		if (is_tail)
			/* this is the tail of the pipe - clear the
			   chunk reference in all outputs */
			clear_tail_chunk(chunk, locked);

		/* remove the chunk from the pipe */
		shifted = audio_pipe_shift(g_mp);
		assert(shifted == chunk);

		if (is_tail)
			/* unlock all audio outputs which were locked
			   by clear_tail_chunk() */
			for (unsigned i = 0; i < num_audio_outputs; ++i)
				if (locked[i])
					g_mutex_unlock(audio_outputs[i]->mutex);

		/* return the chunk to the buffer */
		music_buffer_return(g_music_buffer, shifted);
	}

	return 0;
}
#endif

bool
audio_output_all_wait(struct player_control *pc, unsigned threshold)
{
	player_lock(pc);

	if (audio_output_all_check() < threshold) {
		player_unlock(pc);
		return true;
	}

	player_wait(pc);
	player_unlock(pc);

	return audio_output_all_check() < threshold;
}

void
audio_output_all_pause(void)
{
	unsigned int i;

	audio_output_all_update();

	for (i = 0; i < num_audio_outputs; ++i)
		audio_output_pause(audio_outputs[i]);

	audio_output_wait_all();
}

void
audio_output_all_drain(void)
{
	for (unsigned i = 0; i < num_audio_outputs; ++i)
		audio_output_drain_async(audio_outputs[i]);

	audio_output_wait_all();
}

void
audio_output_all_cancel(void)
{
	unsigned int i;

	/* send the cancel() command to all audio outputs */

	for (i = 0; i < num_audio_outputs; ++i)
		audio_output_cancel(audio_outputs[i]);

	audio_output_wait_all();

	/* clear the music pipe and return all chunks to the buffer */

	if (g_p != NULL)
		audio_pipe_clear(g_p);

	/* the audio outputs are now waiting for a signal, to
	   synchronize the cleared music pipe */

	audio_output_allow_play_all();

	/* invalidate elapsed_time */

	audio_output_all_elapsed_time = -1.0;
}

void
audio_output_all_close(void)
{
	unsigned int i;

	for (i = 0; i < num_audio_outputs; ++i)
		audio_output_close(audio_outputs[i]);

	if (g_p != NULL) {
		audio_pipe_free(g_p);
		g_p = NULL;
	}

	audio_format_clear(&input_audio_format);

	audio_output_all_elapsed_time = -1.0;
}

void
audio_output_all_release(void)
{
	unsigned int i;

	for (i = 0; i < num_audio_outputs; ++i)
		audio_output_release(audio_outputs[i]);

	if (g_p != NULL) {
		audio_pipe_free(g_p);
		g_p = NULL;
	}

	audio_format_clear(&input_audio_format);

	audio_output_all_elapsed_time = -1.0;
}

void
audio_output_all_song_border(void)
{
	/* clear the elapsed_time pointer at the beginning of a new
	   song */
	audio_output_all_elapsed_time = 0.0;
}

float
audio_output_all_get_elapsed_time(void)
{
	return audio_output_all_elapsed_time;
}
