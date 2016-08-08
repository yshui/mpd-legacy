/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
 * Copyright (C) 2016 Yuxuan Shui <yshuiv7@gmail.com>
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

#include "pipe.h"
#include "buffer.h"
#include "c11thread.h"
#include "chunk.h"
#include "compiler.h"
#include "config.h"
#include "macros.h"
#include "poison.h"
#include "sem.h"

#include <assert.h>

struct audio_pipe {
	/** the first chunk */
	struct audio_chunk *head;

	/** a pointer to the tail of the chunk */
	struct audio_chunk **tail_r;

	size_t size, capacity;

	mtx_t mutex;

	xsem_t sem;

	struct audio_chunk *available, *chunk_pool;

	struct audio_chunk *current;

	struct audio_format *format;
};

static inline void
audio_chunk_init(struct audio_chunk *chunk)
{
	chunk->other = NULL;
	chunk->length = 0;
	chunk->tag = NULL;
	chunk->replay_gain_serial = 0;
}

static inline void
audio_chunk_free(struct audio_chunk *chunk)
{
	if (chunk->tag != NULL)
		tag_free(chunk->tag);
}

void
audio_pipe_flush(struct audio_pipe *p) {
	if (audio_chunk_is_empty(p->current))
		// Current chunk is empty
		// Nothing needed to be done
		return;

	mtx_lock(&p->mutex);

	p->current->next = NULL;
	*p->tail_r = p->current;
	p->tail_r = &p->current->next;

	p->size++;

	p->current = NULL;
	mtx_unlock(&p->mutex);
}

/*
 * Write data to the end of the pipe
 * If there's no free chunk available, block until there is
 *
 * Written might not be immediately available to outputs, call
 * audio_pipe_flush if needed.
 */
void
audio_pipe_write_sync(struct audio_pipe *p, float time, strcut audio_format *fmt,
                      uint16_t bit_rate, size_t length, void *data) {
	const size_t frame_size = audio_format_frame_size(audio_format);
	size_t num_frames;

	assert(audio_format_equals(fmt, p->format));

	bool retry = false;
	do {
		if (!p->current) {
			if (!p->available)
				xsem_wait(&p->sem);
			mtx_lock(&p->mutex);
			assert(p->available);
			p->current = p->available;
			audio_chunk_init(p->current);
			p->available = p->available->next;
			mtx_unlock(&p->mutex);
		}

		struct audio_chunk *chunk = p->current;

		if (chunk->length == 0) {
			/* if the chunk is empty, nobody has set bitRate and
			   times yet */

			chunk->bit_rate = bit_rate;
			chunk->times = time;
		}

		assert(frame_size <= sizeof(chunk->data));
		assert(length > frame_size);
		num_frames = (sizeof(chunk->data) - chunk->length) / frame_size;

		if (num_frames == 0) {
			// Current chunk if full, flush it and try again
			audio_pipe_flush(p);
			retry = true;
		}
	} while(retry);

	if (num_frames > length / frame_size)
		num_frames = length / frame_size;

	size_t ret = num_frames * frame_size;

	if (data)
		memcpy(chunk->data+chunk->length, data, ret);
	else
		memset(chunk->data+chunk->length, 0, ret);

	chunk->length += ret;

	return ret;
}

struct audio_pipe *audio_pipe_new(size_t nchunks) {
	struct audio_pipe *mp = tmalloc(struct audio_pipe, 1);

	mp->head = NULL;
	mp->tail_r = &mp->head;
	mp->size = 0;
	mtx_init(&mp->mutex, mtx_plain);

	mp->chunk_pool = tmalloc(struct music_chunk, nchunks);
	mp->capacity = nchunks;

	// Chain the chunks
	for (size_t i = 0; i < nchunks - 1; i++)
		mp->chunk_pool[i].next = &mp->chunk_pool[i + 1];

	mp->chunk_pool[nchunks - 1].next = NULL;
	mp->available = &mp->chunk_pool[0];

	return mp;
}

void audio_pipe_free(struct audio_pipe *p) {
	if (p->head)
		audio_pipe_clear(p);

	mtx_destroy(&p->mutex);
	free(p->chunk_pool);
	free(p);
}

#ifndef NDEBUG

bool audio_pipe_check_format(const struct audio_pipe *pipe,
                             const struct audio_format *audio_format) {
	assert(pipe != NULL);
	assert(audio_format != NULL);

	return !audio_format_defined(&pipe->audio_format) ||
	       audio_format_equals(&pipe->audio_format, audio_format);
}

bool audio_pipe_contains(struct audio_pipe *mp, const struct music_chunk *chunk) {
	mtx_lock(&mp->mutex);

	for (const struct music_chunk *i = mp->head; i != NULL; i = i->next) {
		if (i == chunk) {
			mtx_unlock(&mp->mutex);
			return true;
		}
	}

	mtx_unlock(&mp->mutex);

	return false;
}

#endif

/** Remove one chunk from head and free it
 *  p->mutex should be held
 */
static void audio_pipe_shift(struct audio_pipe *p) {
	struct music_chunk *chunk;

	chunk = mp->head;
	if (chunk != NULL) {
		assert(!music_chunk_is_empty(chunk));

		mp->head = chunk->next;
		--mp->size;

		if (mp->head == NULL) {
			assert(mp->size == 0);
			assert(mp->tail_r == &chunk->next);

			mp->tail_r = &mp->head;
		} else {
			assert(mp->size > 0);
			assert(mp->tail_r != &chunk->next);
		}

		if (chunk->other != NULL) {
			audio_chunk_free(chunk->other);
			poison_undefined(chunk->other, sizeof(*chunk->other));
			chunk->other->next = p->available;
			p->available = chunk->other;
		}

		audio_chunk_free(chunk);
		poison_undefined(chunk, sizeof(*chunk));
		chunk->next = mp->available;
		mp->available = chunk;

		xsem_post(p->sem);
	}
}

void audio_pipe_clear(struct audio_pipe *p) {
	mtx_lock(&p->mutex);
	p->size = 0;

	int freed = 0;
	struct audio_chunk *chunk = p->head;
	while (chunk != NULL) {
		if (chunk->other) {
			audio_chunk_free(chunk->other);
			chunk->other->next = p->available;
			p->available = chunk->other;
			chunk->other = NULL;
			freed++;
		}
		audio_chunk_free(chunk);
		chunk = chunk->next;
		freed++;
	}

	// Attach the whole pipe onto the available list
	*p->tail_r = p->available;
	p->available = p->head;

	p->head = NULL;
	p->tail_r = &p->head;

	xsem_post_n(&p->sem, freed);
	mtx_unlock(&p->mutex);
}

size_t audio_pipe_capacity(const struct audio_pipe *mp) { return mp->capacity; }

unsigned audio_pipe_size(struct audio_pipe *mp) {
	mtx_lock(&mp->mutex);
	unsigned size = mp->size;
	mtx_unlock(&mp->mutex);
	return size;
}

struct audio_chunk *audio_pipe_next(struct audio_pipe *p, struct audio_chunk *c) {
	if (!c)
		return audio_pipe_get_head(p);

	mtx_lock(&p->mutex);

	c->ref_count--;
	if (c->next)
		c->next->ref_count++;

	if (p->head->ref_count == 0)
		audio_pipe_shift(p);

	mtx_unlock(&p->mutex);
	return c->next;
}

// Return the current head of the pipe
struct audio_chunk *audio_pipe_get_head(struct audio_pipe *p) {
	mtx_lock(&p->mutex);
	if (p->head)
		p->head->ref_count++;
	mtx_unlock(&p->mutex);

	return p->head;
}
