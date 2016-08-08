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

#include "compiler.h"
#include "c11thread.h"

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

struct input_stream {
	/**
	 * the plugin which implements this input stream
	 */
	const struct input_plugin *plugin;

	/**
	 * The absolute URI which was used to open this stream.  May
	 * be NULL if this is unknown.
	 */
	char *uri;

	/**
	 * A mutex that protects the mutable attributes of this object
	 * and its implementation.  It must be locked before calling
	 * any of the public methods.
	 *
	 * This object is allocated by the client, and the client is
	 * responsible for freeing it.
	 */
	mtx_t mutex;

	/**
	 * A cond that gets signalled when the state of this object
	 * changes from the I/O thread.  The client of this object may
	 * wait on it.  Optional, may be NULL.
	 *
	 * This object is allocated by the client, and the client is
	 * responsible for freeing it.
	 */
	cnd_t cond;

	/**
	 * indicates whether the stream is ready for reading and
	 * whether the other attributes in this struct are valid
	 */
	bool ready;

	/**
	 * if true, then the stream is fully seekable
	 */
	bool seekable;

	/**
	 * the size of the resource, or -1 if unknown
	 */
	off_t size;

	/**
	 * the current offset within the stream
	 */
	off_t offset;

	/**
	 * the MIME content type of the resource, or NULL if unknown
	 */
	char *mime;
};

/**
 * Opens a new input stream.  You may not access it until the "ready"
 * flag is set.
 *
 * @param mutex a mutex that is used to protect this object; must be
 * locked before calling any of the public methods
 * @param cond a cond that gets signalled when the state of
 * this object changes; may be NULL if the caller doesn't want to get
 * notifications
 * @return an #input_stream object on success, NULL on error
 */
MPD_MALLOC
struct input_stream *
input_stream_open(const char *uri);

/**
 * Close the input stream and free resources.
 *
 * The caller must not lock the mutex.
 */
void
input_stream_close(struct input_stream *is);

static inline void
input_stream_lock(struct input_stream *is)
{
	mtx_lock(&is->mutex);
}

static inline void
input_stream_unlock(struct input_stream *is)
{
	mtx_unlock(&is->mutex);
}

/**
 * Check for errors that may have occurred in the I/O thread.
 *
 * @return false on error
 */
int
input_stream_check(struct input_stream *is);

/**
 * Update the public attributes.  Call before accessing attributes
 * such as "ready" or "offset".
 */
void
input_stream_update(struct input_stream *is);

/**
 * Wait until the stream becomes ready.
 *
 * The caller must lock the mutex.
 */
void
input_stream_wait_ready(struct input_stream *is);

/**
 * Wrapper for input_stream_wait_locked() which locks and unlocks the
 * mutex; the caller must not be holding it already.
 */
void
input_stream_lock_wait_ready(struct input_stream *is);

/**
 * Seeks to the specified position in the stream.  This will most
 * likely fail if the "seekable" flag is false.
 *
 * The caller must lock the mutex.
 *
 * @param is the input_stream object
 * @param offset the relative offset
 * @param whence the base of the seek, one of SEEK_SET, SEEK_CUR, SEEK_END
 */
int
input_stream_seek(struct input_stream *is, off64_t offset, int whence);

/**
 * Wrapper for input_stream_seek() which locks and unlocks the
 * mutex; the caller must not be holding it already.
 */
int
input_stream_lock_seek(struct input_stream *is, off64_t offset, int whence);

/**
 * Returns true if the stream has reached end-of-file.
 *
 * The caller must lock the mutex.
 */
MPD_PURE
bool input_stream_eof(struct input_stream *is);

/**
 * Wrapper for input_stream_eof() which locks and unlocks the mutex;
 * the caller must not be holding it already.
 */
MPD_PURE
bool
input_stream_lock_eof(struct input_stream *is);

/**
 * Reads the tag from the stream.
 *
 * The caller must lock the mutex.
 *
 * @return a tag object which must be freed with tag_free(), or NULL
 * if the tag has not changed since the last call
 */
MPD_MALLOC
struct tag *
input_stream_tag(struct input_stream *is);

/**
 * Wrapper for input_stream_tag() which locks and unlocks the
 * mutex; the caller must not be holding it already.
 */
MPD_MALLOC
struct tag *
input_stream_lock_tag(struct input_stream *is);

/**
 * Returns true if the next read operation will not block: either data
 * is available, or end-of-stream has been reached, or an error has
 * occurred.
 *
 * The caller must lock the mutex.
 */
MPD_PURE
bool
input_stream_available(struct input_stream *is);

/**
 * Reads data from the stream into the caller-supplied buffer.
 * Returns 0 on error or eof (check with input_stream_eof()).
 *
 * The caller must lock the mutex.
 *
 * @param is the input_stream object
 * @param ptr the buffer to read into
 * @param size the maximum number of bytes to read
 * @return the number of bytes read
 */
ssize_t
input_stream_read(struct input_stream *is, void *ptr, ssize_t size);

/**
 * Wrapper for input_stream_tag() which locks and unlocks the
 * mutex; the caller must not be holding it already.
 */
ssize_t
input_stream_lock_read(struct input_stream *is, void *ptr, ssize_t size);
