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

#ifndef MPD_DECODER_CONTROL_H
#define MPD_DECODER_CONTROL_H

#include "decoder_command.h"
#include "audio_format.h"
#include "input_stream.h"
#include "c11thread.h"

#include <assert.h>

enum decoder_state {
	DECODE_STATE_STOP = 0,
	DECODE_STATE_START,
	DECODE_STATE_DECODE,

	/**
	 * The last "START" command failed, because there was an I/O
	 * error or because no decoder was able to decode the file.
	 * This state will only come after START; once the state has
	 * turned to DECODE, by definition no such error can occur.
	 */
	DECODE_STATE_ERROR,

	/**
	 * command is progress
	 */
	DECODE_STATE_PENDING,
};

struct decoder_control {
	/** the handle of the decoder thread, or NULL if the decoder
	    thread isn't running */
	thrd_t thread;

	/**
	 * The input stream that's feeding the decoder
	 */
	struct input_stream *is;

	/**
	 * cv used for signaling new commands/free chunks available
	 */
	mtx_t mutex;
	cnd_t cond;

	/**
	 * player control
	 */
	struct player_control *pc;

	enum decoder_state state;
	enum decoder_command command;

	bool quit;
	bool seek_error;
	bool seekable;
	double seek_where;

	/** the format of the song file */
	struct audio_format in_audio_format;

	/** the format being sent to the music pipe */
	struct audio_format out_audio_format;

	/**
	 * The song currently being decoded.  This attribute is set by
	 * the player thread, when it sends the #DECODE_COMMAND_START
	 * command.
	 */
	const struct song *song;

	/**
	 * The initial seek position (in milliseconds), e.g. to the
	 * start of a sub-track described by a CUE file.
	 *
	 * This attribute is set by dc_start().
	 */
	unsigned start_ms;

	/**
	 * The decoder will stop when it reaches this position (in
	 * milliseconds).  0 means don't stop before the end of the
	 * file.
	 *
	 * This attribute is set by dc_start().
	 */
	unsigned end_ms;

	float total_time;

	/** the #music_chunk allocator */
	struct music_buffer *buffer;

	/**
	 * The destination pipe for decoded chunks.  The caller thread
	 * owns this object, and is responsible for freeing it.
	 */
	struct music_pipe *pipe;

	float replay_gain_db;
	float replay_gain_prev_db;
	char *mixramp_start;
	char *mixramp_end;
	char *mixramp_prev_end;
};

MPD_MALLOC
struct decoder_control *
dc_new(struct player_control *);

void
dc_free(struct decoder_control *dc);

/**
 * Locks the #decoder_control object.
 */
static inline void
decoder_lock_is(struct decoder_control *dc)
{
	mtx_lock(&dc->is->mutex);
}

/**
 * Unlocks the #decoder_control object.
 */
static inline void
decoder_unlock_is(struct decoder_control *dc)
{
	mtx_unlock(&dc->is->mutex);
}

/**
 * Waits for input become available on the #decoder_control object.
 * This function is only valid in the decoder thread.  The object
 * must be locked prior to calling this function.
 */
static inline void
decoder_wait_is(struct decoder_control *dc)
{
	input_stream_lock_wait_ready(dc->is);
}

/**
 * Waits for new command on the #decoder_control object.  This function
 * is only valid in the decoder thread.  The object must be locked
 * prior to calling this function.
 */
static inline void
decoder_wait_cmd(struct decoder_control *dc) {
	cnd_wait(&dc->cond, &dc->mutex);
}

/**
 * Signals the #decoder_control object.  This function is only valid
 * in the player thread.  The object should be locked prior to calling
 * this function.
 */
static inline void
decoder_signal(struct decoder_control *dc)
{
	cnd_signal(&dc->cond);
}

static inline bool
decoder_is_idle(const struct decoder_control *dc)
{
	auto tmp = dc->state;
	return tmp == DECODE_STATE_STOP ||
		tmp == DECODE_STATE_ERROR;
}

static inline bool
decoder_is_starting(const struct decoder_control *dc)
{
	return dc->state == DECODE_STATE_START;
}

static inline bool
decoder_has_failed(const struct decoder_control *dc)
{
	assert(dc->command == DECODE_COMMAND_NONE);

	return dc->state == DECODE_STATE_ERROR;
}

static inline const struct song *
decoder_current_song(const struct decoder_control *dc)
{
	switch (dc->state) {
	case DECODE_STATE_STOP:
	case DECODE_STATE_ERROR:
		return NULL;

	case DECODE_STATE_START:
	case DECODE_STATE_DECODE:
		return dc->song;
	case DECODE_STATE_PENDING:
		assert(false);
	}

	assert(false);
	return NULL;
}

/**
 * Start the decoder.
 *
 * @param the decoder
 * @param song the song to be decoded
 * @param start_ms see #decoder_control
 * @param end_ms see #decoder_control
 * @param pipe the pipe which receives the decoded chunks (owned by
 * the caller)
 */
void
dc_start(struct decoder_control *dc, struct song *song,
	 unsigned start_ms, unsigned end_ms,
	 struct music_buffer *buffer, struct music_pipe *pipe);

void
dc_stop(struct decoder_control *dc);

bool
dc_seek(struct decoder_control *dc, double where);

void
dc_quit(struct decoder_control *dc);

void
dc_mixramp_start(struct decoder_control *dc, char *mixramp_start);

void
dc_mixramp_end(struct decoder_control *dc, char *mixramp_end);

void
dc_mixramp_prev_end(struct decoder_control *dc, char *mixramp_prev_end);

#endif
