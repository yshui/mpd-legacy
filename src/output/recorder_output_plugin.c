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

#define LOG_DOMAIN "output: recorder"

#include "log.h"
#include "config.h"
#include "recorder_output_plugin.h"
#include "output_api.h"
#include "encoder_plugin.h"
#include "encoder_list.h"
#include "fd_util.h"
#include "open.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "recorder"

struct recorder_output {
	struct audio_output base;

	/**
	 * The configured encoder plugin.
	 */
	struct encoder *encoder;

	/**
	 * The destination file name.
	 */
	const char *path;

	/**
	 * The destination file descriptor.
	 */
	int fd;

	/**
	 * The buffer for encoder_read().
	 */
	char buffer[32768];
};

static struct audio_output *
recorder_output_init(const struct config_param *param)
{
	struct recorder_output *recorder = tmalloc(struct recorder_output, 1);
	int ret = ao_base_init(&recorder->base, &recorder_output_plugin, param);
	if (ret != MPD_SUCCESS) {
		free(recorder);
		return ERR_PTR(ret);
	}

	/* read configuration */

	const char *encoder_name =
		config_get_block_string(param, "encoder", "vorbis");
	const struct encoder_plugin *encoder_plugin =
		encoder_plugin_get(encoder_name);
	if (encoder_plugin == NULL) {
		log_err("No such encoder: %s", encoder_name);
		ret = -MPD_INVAL;
		goto failure;
	}

	recorder->path = config_get_block_string(param, "path", NULL);
	if (recorder->path == NULL) {
		log_err("'path' not configured");
		ret = -MPD_MISS_VALUE;
		goto failure;
	}

	/* initialize encoder */

	recorder->encoder = encoder_init(encoder_plugin, param);
	if (IS_ERR(recorder->encoder)) {
		ret = PTR_ERR(recorder->encoder);
		goto failure;
	}

	return &recorder->base;

failure:
	ao_base_finish(&recorder->base);
	free(recorder);
	return ERR_PTR(ret);
}

static void
recorder_output_finish(struct audio_output *ao)
{
	struct recorder_output *recorder = (struct recorder_output *)ao;

	encoder_finish(recorder->encoder);
	ao_base_finish(&recorder->base);
	g_free(recorder);
}

static int
recorder_write_to_file(struct recorder_output *recorder,
		       const void *_data, size_t length)
{
	assert(length > 0);

	const int fd = recorder->fd;

	const uint8_t *data = (const uint8_t *)_data, *end = data + length;

	while (true) {
		ssize_t nbytes = write(fd, data, end - data);
		if (nbytes > 0) {
			data += nbytes;
			if (data == end)
				return true;
		} else if (nbytes == 0) {
			/* shouldn't happen for files */
			log_err("write() returned 0");
			return -MPD_ACCESS;
		} else if (errno != EINTR) {
			log_err("Failed to write to '%s': %s",
				    recorder->path, strerror(errno));
			return -MPD_ACCESS;
		}
	}
}

/**
 * Writes pending data from the encoder to the output file.
 */
static int
recorder_output_encoder_to_file(struct recorder_output *recorder)
{
	assert(recorder->fd >= 0);

	while (true) {
		/* read from the encoder */

		size_t size = encoder_read(recorder->encoder, recorder->buffer,
					   sizeof(recorder->buffer));
		if (size == 0)
			return MPD_SUCCESS;

		/* write everything into the file */

		int ret = recorder_write_to_file(recorder, recorder->buffer, size);
		if (ret != MPD_SUCCESS)
			return ret;
	}
}

static int
recorder_output_open(struct audio_output *ao,
		     struct audio_format *audio_format)
{
	struct recorder_output *recorder = (struct recorder_output *)ao;

	/* create the output file */

	recorder->fd = open_cloexec(recorder->path,
				    O_CREAT|O_WRONLY|O_TRUNC|O_BINARY,
				    0666);
	if (recorder->fd < 0) {
		log_err("Failed to create '%s': %s",
			    recorder->path, strerror(errno));
		return -MPD_ACCESS;
	}

	/* open the encoder */

	int ret = encoder_open(recorder->encoder, audio_format);
	if (ret != MPD_SUCCESS) {
		close(recorder->fd);
		unlink(recorder->path);
		return ret;
	}

	ret = recorder_output_encoder_to_file(recorder);
	if (ret != MPD_SUCCESS) {
		encoder_close(recorder->encoder);
		close(recorder->fd);
		unlink(recorder->path);
		return ret;
	}

	return true;
}

static void
recorder_output_close(struct audio_output *ao)
{
	struct recorder_output *recorder = (struct recorder_output *)ao;

	/* flush the encoder and write the rest to the file */

	if (encoder_end(recorder->encoder) == MPD_SUCCESS)
		recorder_output_encoder_to_file(recorder);
	else
		log_err("Encoder error");

	/* now really close everything */

	encoder_close(recorder->encoder);

	close(recorder->fd);
}

static size_t
recorder_output_play(struct audio_output *ao, const void *chunk, size_t size)
{
	struct recorder_output *recorder = (struct recorder_output *)ao;

	return encoder_write(recorder->encoder, chunk, size) >= 0 &&
		recorder_output_encoder_to_file(recorder) == MPD_SUCCESS
		? size : 0;
}

const struct audio_output_plugin recorder_output_plugin = {
	.name = "recorder",
	.init = recorder_output_init,
	.finish = recorder_output_finish,
	.open = recorder_output_open,
	.close = recorder_output_close,
	.play = recorder_output_play,
};
