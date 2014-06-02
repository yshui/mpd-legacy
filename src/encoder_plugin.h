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

#ifndef MPD_ENCODER_PLUGIN_H
#define MPD_ENCODER_PLUGIN_H

#include "err.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

struct encoder_plugin;
struct audio_format;
struct config_param;
struct tag;

#define encoder_plugins_for_each(plugin) \
	for (const struct encoder_plugin *plugin, \
		*const*encoder_plugin_iterator = &encoder_plugins[0]; \
		(plugin = *encoder_plugin_iterator) != NULL; \
		++encoder_plugin_iterator)

struct encoder {
	const struct encoder_plugin *plugin;

#ifndef NDEBUG
	bool open, pre_tag, tag, end;
#endif
};

struct encoder_plugin {
	const char *name;

	struct encoder *(*init)(const struct config_param *param);

	void (*finish)(struct encoder *encoder);

	int (*open)(struct encoder *encoder,
		     struct audio_format *audio_format);

	void (*close)(struct encoder *encoder);

	int (*end)(struct encoder *encoder);

	int (*flush)(struct encoder *encoder);

	int (*pre_tag)(struct encoder *encoder);

	int (*tag)(struct encoder *encoder, const struct tag *tag);

	ssize_t (*write)(struct encoder *encoder,
		      const void *data, size_t length);

	size_t (*read)(struct encoder *encoder, void *dest, size_t length);

	const char *(*get_mime_type)(struct encoder *encoder);
};

extern const struct encoder_plugin *const encoder_plugins[];

/**
 * Initializes an encoder object.  This should be used by encoder
 * plugins to initialize their base class.
 */
static inline void
encoder_struct_init(struct encoder *encoder,
		    const struct encoder_plugin *plugin)
{
	encoder->plugin = plugin;

#ifndef NDEBUG
	encoder->open = false;
#endif
}

/**
 * Creates a new encoder object.
 *
 * @param plugin the encoder plugin
 * @param param optional configuration
 * @param error location to store the error occurring, or NULL to ignore errors.
 * @return an encoder object on success, NULL on failure
 */
static inline struct encoder *
encoder_init(const struct encoder_plugin *plugin,
	     const struct config_param *param)
{
	return plugin->init(param);
}

/**
 * Frees an encoder object.
 *
 * @param encoder the encoder
 */
static inline void
encoder_finish(struct encoder *encoder)
{
	assert(!encoder->open);

	encoder->plugin->finish(encoder);
}

/**
 * Opens an encoder object.  You must call this prior to using it.
 * Before you free it, you must call encoder_close().  You may open
 * and close (reuse) one encoder any number of times.
 *
 * After this function returns successfully and before the first
 * encoder_write() call, you should invoke encoder_read() to obtain
 * the file header.
 *
 * @param encoder the encoder
 * @param audio_format the encoder's input audio format; the plugin
 * may modify the struct to adapt it to its abilities
 * @param error location to store the error occurring, or NULL to ignore errors.
 * @return true on success
 */
static inline int
encoder_open(struct encoder *encoder, struct audio_format *audio_format)
{
	assert(!encoder->open);

	int ret = encoder->plugin->open(encoder, audio_format);
#ifndef NDEBUG
	encoder->open = ret == MPD_SUCCESS;
	encoder->pre_tag = encoder->tag = encoder->end = false;
#endif
	return ret;
}

/**
 * Closes an encoder object.  This disables the encoder, and readies
 * it for reusal by calling encoder_open() again.
 *
 * @param encoder the encoder
 */
static inline void
encoder_close(struct encoder *encoder)
{
	assert(encoder->open);

	if (encoder->plugin->close != NULL)
		encoder->plugin->close(encoder);

#ifndef NDEBUG
	encoder->open = false;
#endif
}

/**
 * Ends the stream: flushes the encoder object, generate an
 * end-of-stream marker (if applicable), make everything which might
 * currently be buffered available by encoder_read().
 *
 * After this function has been called, the encoder may not be usable
 * for more data, and only encoder_read() and encoder_close() can be
 * called.
 *
 * @param encoder the encoder
 * @return error code
 */
static inline int
encoder_end(struct encoder *encoder)
{
	assert(encoder->open);
	assert(!encoder->end);

#ifndef NDEBUG
	encoder->end = true;
#endif

	/* this method is optional */
	return encoder->plugin->end != NULL
		? encoder->plugin->end(encoder)
		: MPD_SUCCESS;
}

/**
 * Flushes an encoder object, make everything which might currently be
 * buffered available by encoder_read().
 *
 * @param encoder the encoder
 * @param error location to store the error occurring, or NULL to ignore errors.
 * @return true on success
 */
static inline int
encoder_flush(struct encoder *encoder)
{
	assert(encoder->open);
	assert(!encoder->pre_tag);
	assert(!encoder->tag);
	assert(!encoder->end);

	/* this method is optional */
	return encoder->plugin->flush != NULL
		? encoder->plugin->flush(encoder)
		: MPD_SUCCESS;
}

/**
 * Prepare for sending a tag to the encoder.  This is used by some
 * encoders to flush the previous sub-stream, in preparation to begin
 * a new one.
 *
 * @param encoder the encoder
 * @param tag the tag object
 * @return true on success
 */
static inline int
encoder_pre_tag(struct encoder *encoder)
{
	assert(encoder->open);
	assert(!encoder->pre_tag);
	assert(!encoder->tag);
	assert(!encoder->end);

	/* this method is optional */
	int ret = encoder->plugin->pre_tag != NULL
		? encoder->plugin->pre_tag(encoder)
		: MPD_SUCCESS;

#ifndef NDEBUG
	encoder->pre_tag = ret == MPD_SUCCESS;
#endif
	return ret;
}

/**
 * Sends a tag to the encoder.
 *
 * Instructions: call encoder_pre_tag(); then obtain flushed data with
 * encoder_read(); finally call encoder_tag().
 *
 * @param encoder the encoder
 * @param tag the tag object
 * @return error code 
 */
static inline bool
encoder_tag(struct encoder *encoder, const struct tag *tag)
{
	assert(encoder->open);
	assert(!encoder->pre_tag);
	assert(encoder->tag);
	assert(!encoder->end);

#ifndef NDEBUG
	encoder->tag = false;
#endif

	/* this method is optional */
	return encoder->plugin->tag != NULL
		? encoder->plugin->tag(encoder, tag)
		: MPD_SUCCESS;
}

/**
 * Writes raw PCM data to the encoder.
 *
 * @param encoder the encoder
 * @param data the buffer containing PCM samples
 * @param length the length of the buffer in bytes
 * @return true on success
 */
static inline bool
encoder_write(struct encoder *encoder, const void *data, size_t length)
{
	assert(encoder->open);
	assert(!encoder->pre_tag);
	assert(!encoder->tag);
	assert(!encoder->end);

	return encoder->plugin->write(encoder, data, length);
}

/**
 * Reads encoded data from the encoder.
 *
 * Call this repeatedly until no more data is returned.
 *
 * @param encoder the encoder
 * @param dest the destination buffer to copy to
 * @param length the maximum length of the destination buffer
 * @return the number of bytes written to #dest
 */
static inline size_t
encoder_read(struct encoder *encoder, void *dest, size_t length)
{
	assert(encoder->open);
	assert(!encoder->pre_tag || !encoder->tag);

#ifndef NDEBUG
	if (encoder->pre_tag) {
		encoder->pre_tag = false;
		encoder->tag = true;
	}
#endif

	return encoder->plugin->read(encoder, dest, length);
}

/**
 * Get mime type of encoded content.
 *
 * @param plugin the encoder plugin
 * @return an constant string, NULL on failure
 */
static inline const char *
encoder_get_mime_type(struct encoder *encoder)
{
	/* this method is optional */
	return encoder->plugin->get_mime_type != NULL
		? encoder->plugin->get_mime_type(encoder)
		: NULL;
}

/**
 * Looks up an encoder plugin by its name.
 *
 * @param name the encoder name to look for
 * @return the encoder plugin with the specified name, or NULL if none
 * was found
 */
static const struct encoder_plugin *
encoder_plugin_get(const char *name)
{
	encoder_plugins_for_each(plugin)
		if (strcmp(plugin->name, name) == 0)
			return plugin;

	return NULL;
}

#endif
