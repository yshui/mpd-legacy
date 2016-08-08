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

#ifndef MPD_PLAYER_H
#define MPD_PLAYER_H

#include "audio_format.h"
#include "c11thread.h"

#include <stdint.h>

struct decoder_control;

enum player_state {
	PLAYER_STATE_STOP = 0,
	PLAYER_STATE_PAUSE,
	PLAYER_STATE_PLAY
};

enum player_command {
	PLAYER_COMMAND_NONE = 0,
	PLAYER_COMMAND_EXIT,
	PLAYER_COMMAND_STOP,
	PLAYER_COMMAND_PAUSE,
	PLAYER_COMMAND_SEEK,
	PLAYER_COMMAND_CLOSE_AUDIO,

	/**
	 * At least one audio_output.enabled flag has been modified;
	 * commit those changes to the output threads.
	 */
	PLAYER_COMMAND_UPDATE_AUDIO,

	/** player_control.next_song has been updated */
	PLAYER_COMMAND_QUEUE,

	/**
	 * cancel pre-decoding player_control.next_song; if the player
	 * has already started playing this song, it will completely
	 * stop
	 */
	PLAYER_COMMAND_CANCEL,

	/**
	 * Refresh status information in the #player_control struct,
	 * e.g. elapsed_time.
	 */
	PLAYER_COMMAND_REFRESH,
};

enum player_error {
	PLAYER_ERROR_NOERROR = 0,
	PLAYER_ERROR_FILE,
	PLAYER_ERROR_AUDIO,
	PLAYER_ERROR_SYSTEM,
	PLAYER_ERROR_UNKTYPE,
	PLAYER_ERROR_FILENOTFOUND,
	PLAYER_ERROR_PENDING,
};

struct player_status {
	enum player_state state;
	uint16_t bit_rate;
	struct audio_format audio_format;
	float total_time;
	float elapsed_time;
};

struct player_control {
	unsigned buffer_chunks;

	unsigned int buffered_before_play;

	/** the handle of the player thread, or NULL if the player
	    thread isn't running */
	thrd_t thread;

	/**
	 * This lock protects #command.
	 */
	mtx_t mutex;

	/**
	 * cv for signalling new commands or audio status change (XXX messy, but...)
	 */
	cnd_t cond;

	/**
	 * cv used for signalling results
	 * protecting #state and #error
	 */
	mtx_t client_mutex;
	cnd_t client_cond;

	enum player_command command;
	enum player_state state;
	enum player_error error;
	uint16_t bit_rate;
	struct audio_format audio_format;
	float total_time;
	float elapsed_time;
	struct song *next_song;
	const struct song *errored_song;
	double seek_where;
	float cross_fade_seconds;
	float mixramp_db;
	float mixramp_delay_seconds;
	double total_play_time;

	/**
	 * If this flag is set, then the player will be auto-paused at
	 * the end of the song, before the next song starts to play.
	 *
	 * This is a copy of the queue's "single" flag most of the
	 * time.
	 */
	bool border_pause;
};

struct player_control *
pc_new(unsigned buffer_chunks, unsigned buffered_before_play);

void
pc_free(struct player_control *pc);

/**
 * Locks the #player_control object.
 */
static inline void
player_lock(struct player_control *pc)
{
	mtx_lock(&pc->mutex);
}

/**
 * Unlocks the #player_control object.
 */
static inline void
player_unlock(struct player_control *pc)
{
	mtx_unlock(&pc->mutex);
}

/**
 * Waits for a signal on the #player_control object.  This function is
 * only valid in the player thread.  The object must be locked prior
 * to calling this function.
 */
static inline void
player_wait(struct player_control *pc)
{
	cnd_wait(&pc->cond, &pc->mutex);
}

/**
 * Signals the #player_control object.  The object should be locked
 * prior to calling this function.
 */
static inline void
player_signal(struct player_control *pc)
{
	cnd_signal(&pc->cond);
}

/**
 * Call this function when the specified song pointer is about to be
 * invalidated.  This makes sure that player_control.errored_song does
 * not point to an invalid pointer.
 */
void
pc_song_deleted(struct player_control *pc, const struct song *song);

void
pc_play(struct player_control *pc, struct song *song);

/**
 * see PLAYER_COMMAND_CANCEL
 */
void
pc_cancel(struct player_control *pc);

void
pc_set_pause(struct player_control *pc, bool pause_flag);

void
pc_pause(struct player_control *pc);

/**
 * Set the player's #border_pause flag.
 */
void
pc_set_border_pause(struct player_control *pc, bool border_pause);

void
pc_kill(struct player_control *pc);

void
pc_get_status(struct player_control *pc, struct player_status *status);

enum player_state
pc_get_state(struct player_control *pc);

void
pc_clear_error(struct player_control *pc);

/**
 * Returns the human-readable message describing the last error during
 * playback, NULL if no error occurred.  The caller has to free the
 * returned string.
 */
char *
pc_get_error_message(struct player_control *pc);

enum player_error
pc_get_error(struct player_control *pc);

void
pc_stop(struct player_control *pc);

void
pc_update_audio(struct player_control *pc);

void
pc_enqueue_song(struct player_control *pc, struct song *song);

/**
 * Makes the player thread seek the specified song to a position.
 *
 * @return true on success, false on failure (e.g. if MPD isn't
 * playing currently)
 */
bool
pc_seek(struct player_control *pc, struct song *song, float seek_time);

void
pc_set_cross_fade(struct player_control *pc, float cross_fade_seconds);

float
pc_get_cross_fade(const struct player_control *pc);

void
pc_set_mixramp_db(struct player_control *pc, float mixramp_db);

float
pc_get_mixramp_db(const struct player_control *pc);

void
pc_set_mixramp_delay(struct player_control *pc, float mixramp_delay_seconds);

float
pc_get_mixramp_delay(const struct player_control *pc);

double
pc_get_total_play_time(const struct player_control *pc);

#endif
