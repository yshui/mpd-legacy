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
#include "player_control.h"
#include "decoder_control.h"
#include "path.h"
#include "log.h"
#include "tag.h"
#include "song.h"
#include "idle.h"
#include "pcm/pcm_volume.h"
#include "main.h"
#include "utils.h"
#include "macros.h"

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

struct player_control *
pc_new(unsigned buffer_chunks, unsigned int buffered_before_play)
{
	struct player_control *pc = tmalloc(struct player_control, 1);

	pc->buffer_chunks = buffer_chunks;
	pc->buffered_before_play = buffered_before_play;

	mtx_init(&pc->mutex, mtx_plain);
	cnd_init(&pc->cond);
	mtx_init(&pc->client_mutex, mtx_plain);
	cnd_init(&pc->client_cond);

	pc->command = PLAYER_COMMAND_NONE;
	pc->error = PLAYER_ERROR_NOERROR;
	pc->state = PLAYER_STATE_STOP;
	pc->cross_fade_seconds = 0;
	pc->mixramp_db = 0;
	pc->mixramp_delay_seconds = nanf("");

	return pc;
}

void
pc_free(struct player_control *pc)
{
	cnd_destroy(&pc->cond);
	mtx_destroy(&pc->mutex);
	free(pc);
}

void
pc_song_deleted(struct player_control *pc, const struct song *song)
{
	if (pc->errored_song == song) {
		pc->error = PLAYER_ERROR_NOERROR;
		pc->errored_song = NULL;
	}
}

static void
player_command(struct player_control *pc, enum player_command cmd) {
	player_lock(pc);
	assert(pc->command == PLAYER_COMMAND_NONE);
	pc->command = cmd;
	player_unlock(pc);

	mtx_lock(&pc->client_mutex);
	pc->error = PLAYER_ERROR_PENDING;
	player_signal(pc);
	while (pc->error == PLAYER_ERROR_PENDING)
		cnd_wait(&pc->client_cond, &pc->client_mutex);

	mtx_unlock(&pc->client_mutex);

	player_lock(pc);
	pc->command = PLAYER_COMMAND_NONE;
	player_unlock(pc);
}

void
pc_play(struct player_control *pc, struct song *song)
{
	assert(song != NULL);

	if (pc->state != PLAYER_STATE_STOP)
		player_command(pc, PLAYER_COMMAND_STOP);

	assert(pc->next_song == NULL);

	pc_enqueue_song(pc, song);

	assert(pc->next_song == NULL);
}

void
pc_cancel(struct player_control *pc)
{
	player_command(pc, PLAYER_COMMAND_CANCEL);
	assert(pc->next_song == NULL);
}

void
pc_stop(struct player_control *pc)
{
	player_command(pc, PLAYER_COMMAND_CLOSE_AUDIO);
	assert(pc->next_song == NULL);

	idle_add(IDLE_PLAYER);
}

void
pc_update_audio(struct player_control *pc)
{
	player_command(pc, PLAYER_COMMAND_UPDATE_AUDIO);
}

void
pc_kill(struct player_control *pc)
{
	player_command(pc, PLAYER_COMMAND_EXIT);

	thrd_join(pc->thread, NULL);

	idle_add(IDLE_PLAYER);
}

void
pc_pause(struct player_control *pc)
{
	if (pc->state != PLAYER_STATE_STOP) {
		player_command(pc, PLAYER_COMMAND_PAUSE);
		idle_add(IDLE_PLAYER);
	}
}

void
pc_set_pause(struct player_control *pc, bool pause_flag)
{
	switch (pc->state) {
	case PLAYER_STATE_STOP:
		break;

	case PLAYER_STATE_PLAY:
		if (pause_flag)
			pc_pause(pc);
		break;

	case PLAYER_STATE_PAUSE:
		if (!pause_flag)
			pc_pause(pc);
		break;
	}
}

void
pc_set_border_pause(struct player_control *pc, bool border_pause)
{
	pc->border_pause = border_pause;
}

void
pc_get_status(struct player_control *pc, struct player_status *status)
{
	player_command(pc, PLAYER_COMMAND_REFRESH);

	status->state = pc->state;

	if (pc->state != PLAYER_STATE_STOP) {
		status->bit_rate = pc->bit_rate;
		status->audio_format = pc->audio_format;
		status->total_time = pc->total_time;
		status->elapsed_time = pc->elapsed_time;
	}
}

enum player_state
pc_get_state(struct player_control *pc)
{
	return pc->state;
}

void
pc_clear_error(struct player_control *pc)
{
	mtx_lock(&pc->client_mutex);
	pc->error = PLAYER_ERROR_NOERROR;
	pc->errored_song = NULL;
	mtx_unlock(&pc->client_mutex);
}

enum player_error
pc_get_error(struct player_control *pc)
{
	return pc->error;
}

static char *
pc_errored_song_uri(struct player_control *pc)
{
	return song_get_uri(pc->errored_song);
}

char *
pc_get_error_message(struct player_control *pc)
{
	char *error;
	char *uri;

	switch (pc->error) {
	case PLAYER_ERROR_NOERROR:
		return NULL;

	case PLAYER_ERROR_FILENOTFOUND:
		uri = pc_errored_song_uri(pc);
		error = strdup_printf("file \"%s\" does not exist or is inaccessible", uri);
		free(uri);
		return error;

	case PLAYER_ERROR_FILE:
		uri = pc_errored_song_uri(pc);
		error = strdup_printf("problems decoding \"%s\"", uri);
		free(uri);
		return error;

	case PLAYER_ERROR_AUDIO:
		return strdup("problems opening audio device");

	case PLAYER_ERROR_SYSTEM:
		return strdup("system error occurred");

	case PLAYER_ERROR_UNKTYPE:
		uri = pc_errored_song_uri(pc);
		error = strdup_printf("file type of \"%s\" is unknown", uri);
		free(uri);
		return error;
	case PLAYER_ERROR_PENDING:
		assert(false);
	}

	assert(false);
	return NULL;
}

void
pc_enqueue_song(struct player_control *pc, struct song *song)
{
	assert(song != NULL);
	assert(pc->next_song == NULL);

	pc->next_song = song;
	player_command(pc, PLAYER_COMMAND_QUEUE);
}

bool
pc_seek(struct player_control *pc, struct song *song, float seek_time)
{
	assert(song != NULL);

	pc->next_song = song;
	pc->seek_where = seek_time;
	player_command(pc, PLAYER_COMMAND_SEEK);

	assert(pc->next_song == NULL);

	idle_add(IDLE_PLAYER);

	return true;
}

float
pc_get_cross_fade(const struct player_control *pc)
{
	return pc->cross_fade_seconds;
}

void
pc_set_cross_fade(struct player_control *pc, float cross_fade_seconds)
{
	if (cross_fade_seconds < 0)
		cross_fade_seconds = 0;
	pc->cross_fade_seconds = cross_fade_seconds;

	idle_add(IDLE_OPTIONS);
}

float
pc_get_mixramp_db(const struct player_control *pc)
{
	return pc->mixramp_db;
}

void
pc_set_mixramp_db(struct player_control *pc, float mixramp_db)
{
	pc->mixramp_db = mixramp_db;

	idle_add(IDLE_OPTIONS);
}

float
pc_get_mixramp_delay(const struct player_control *pc)
{
	return pc->mixramp_delay_seconds;
}

void
pc_set_mixramp_delay(struct player_control *pc, float mixramp_delay_seconds)
{
	pc->mixramp_delay_seconds = mixramp_delay_seconds;

	idle_add(IDLE_OPTIONS);
}

double
pc_get_total_play_time(const struct player_control *pc)
{
	return pc->total_play_time;
}
