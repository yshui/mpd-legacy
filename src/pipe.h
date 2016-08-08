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

#include "replay_gain_info.h"
#include "compiler.h"
#include "sem.h"

#include <stdint.h>
#include <stdbool.h>

#ifndef NDEBUG
struct audio_format;
#endif

#define CHUNK_SIZE (4096)

struct audio_format;

/**
 * A chunk of music data.  Its format is defined by the
 * audio_pipe_append() caller.
 */
struct audio_chunk {

	/** number of output thread using this chunk
	 *  output threads will hold read only reference
	 *  to chunks
	 */
	int ref_count;

	/** the next chunk in a linked list */
	struct audio_chunk *next;

	/**
	 * An optional chunk which should be mixed into this chunk.
	 * This is used for cross-fading.
	 */
	struct audio_chunk *other;

	/**
	 * The current mix ratio for cross-fading: 1.0 means play 100%
	 * of this chunk, 0.0 means play 100% of the "other" chunk.
	 */
	float mix_ratio;

	/** number of bytes stored in this chunk */
	uint16_t length;

	/** current bit rate of the source file */
	uint16_t bit_rate;

	/** the time stamp within the song */
	float times;

	/**
	 * An optional tag associated with this chunk (and the
	 * following chunks); appears at song boundaries.  The tag
	 * object is owned by this chunk, and must be freed when this
	 * chunk is deinitialized in audio_chunk_free()
	 */
	struct tag *tag;

	/**
	 * Replay gain information associated with this chunk.
	 * Only valid if the serial is not 0.
	 */
	struct replay_gain_info replay_gain_info;

	/**
	 * A serial number for checking if replay gain info has
	 * changed since the last chunk.  The magic value 0 indicates
	 * that there is no replay gain info available.
	 */
	unsigned replay_gain_serial;

	/** the data (probably PCM) */
	char data[CHUNK_SIZE];
};
/**
 * A queue of #audio_chunk objects.  One party appends chunks at the
 * tail, and the other consumes them from the head.
 */
struct audio_pipe;

/**
 * Creates a new #audio_pipe object.  It is empty.
 */
MPD_MALLOC
struct audio_pipe *audio_pipe_new(void);

/**
 * Frees the object.  It must be empty now.
 */
void audio_pipe_free(struct audio_pipe *p);

#ifndef NDEBUG
/**
 * Checks if the specified chunk is enqueued in the music pipe.
 */
bool audio_pipe_contains(struct audio_pipe *mp, const struct audio_chunk *chunk);

#endif

static inline bool
audio_chunk_is_empty(const struct audio_chunk *chunk)
{
	return chunk->length == 0 && chunk->tag == NULL;
}

/******************************************************************************
 *                      Functions for the output threads
 ******************************************************************************/

/**
 * Returns the first #audio_chunk from the pipe.  Returns NULL if the
 * pipe is empty.
 */
MPD_PURE
const struct audio_chunk *audio_pipe_get_head(struct audio_pipe *p);

/**
 * Returns the number of chunks currently in this pipe.
 */
MPD_PURE
unsigned audio_pipe_size(struct audio_pipe *p);

MPD_PURE
static inline bool audio_pipe_empty(struct audio_pipe *p) {
	return audio_pipe_size(p) == 0;
}

MPD_PURE const struct audio_chunk *
audio_pipe_next(struct audio_pipe *, const struct audio_chunk *);


/******************************************************************************
 *                      Functions for the decode threads
 ******************************************************************************/

/**
 * Prepares appending to the pipe. The data might be buffered before
 * passing onto the outputs. Call audio_pipe_flush() if needed.
 *
 * @param p the audio_pipe object
 * @param audio_format the audio format for the appended data; must
 * stay the same for the life cycle of this chunk
 * @param data_time the time within the song
 * @param bit_rate the current bit rate of the source file
 * @param length the length of total data
 * @param data the data to be written, if NULL, write silence
 * @return number of bytes actually written
 */
size_t
audio_pipe_write_sync(struct audio_pipe *p,
		  const struct audio_format *audio_format,
		  float data_time, uint16_t bit_rate,
		  size_t length, void *data);


/**
 * Flush the buffered chunk in the pipe
 */
void audio_pipe_flush(struct audio_pipe *p);

/**
 * Clears the whole pipe and returns the chunks to the buffer.
 *
 * @param buffer the buffer object to return the chunks to
 */
void audio_pipe_clear(struct audio_pipe *p);

void audio_pipe_free(struct audio_pipe *p);
