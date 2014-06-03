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

#define LOG_DOMAIN "output: fifo"

#include "log.h"
#include "config.h"
#include "fifo_output_plugin.h"
#include "output_api.h"
#include "utils.h"
#include "timer.h"
#include "fd_util.h"
#include "open.h"

#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>


#define FIFO_BUFFER_SIZE 65536 /* pipe capacity on Linux >= 2.6.11 */

struct fifo_data {
	struct audio_output base;

	char *path;
	int input;
	int output;
	bool created;
	struct timer *timer;
};

static struct fifo_data *fifo_data_new(void)
{
	struct fifo_data *ret;

	ret = g_new(struct fifo_data, 1);

	ret->path = NULL;
	ret->input = -1;
	ret->output = -1;
	ret->created = false;

	return ret;
}

static void fifo_data_free(struct fifo_data *fd)
{
	free(fd->path);
	g_free(fd);
}

static void fifo_delete(struct fifo_data *fd)
{
	log_debug("Removing FIFO \"%s\"", fd->path);

	if (unlink(fd->path) < 0) {
		log_warning("Could not remove FIFO \"%s\": %s",
			  fd->path, strerror(errno));
		return;
	}

	fd->created = false;
}

static void
fifo_close(struct fifo_data *fd)
{
	struct stat st;

	if (fd->input >= 0) {
		close(fd->input);
		fd->input = -1;
	}

	if (fd->output >= 0) {
		close(fd->output);
		fd->output = -1;
	}

	if (fd->created && (stat(fd->path, &st) == 0))
		fifo_delete(fd);
}

static int
fifo_make(struct fifo_data *fd)
{
	if (mkfifo(fd->path, 0666) < 0) {
		log_err("Couldn't create FIFO \"%s\": %s",
			    fd->path, strerror(errno));
		return -MPD_ACCESS;
	}

	fd->created = true;

	return MPD_SUCCESS;
}

static int
fifo_check(struct fifo_data *fd)
{
	struct stat st;

	if (stat(fd->path, &st) < 0) {
		if (errno == ENOENT) {
			/* Path doesn't exist */
			return fifo_make(fd);
		}

		log_err("Failed to stat FIFO \"%s\": %s",
			    fd->path, strerror(errno));
		return -MPD_ACCESS;
	}

	if (!S_ISFIFO(st.st_mode)) {
		log_err("\"%s\" already exists, but is not a FIFO",
			    fd->path);
		return -MPD_INVAL;
	}

	return MPD_SUCCESS;
}

static int
fifo_open(struct fifo_data *fd)
{
	if (!fifo_check(fd))
		return -MPD_INVAL;

	fd->input = open_cloexec(fd->path, O_RDONLY|O_NONBLOCK|O_BINARY, 0);
	if (fd->input < 0) {
		log_err("Could not open FIFO \"%s\" for reading: %s",
			    fd->path, strerror(errno));
		fifo_close(fd);
		return -MPD_ACCESS;
	}

	fd->output = open_cloexec(fd->path, O_WRONLY|O_NONBLOCK|O_BINARY, 0);
	if (fd->output < 0) {
		log_err("Could not open FIFO \"%s\" for writing: %s",
			    fd->path, strerror(errno));
		fifo_close(fd);
		return -MPD_ACCESS;
	}

	return MPD_SUCCESS;
}

static struct audio_output *
fifo_output_init(const struct config_param *param)
{
	struct fifo_data *fd;

	char *path = config_dup_block_path(param, "path");
	if (!path) {
		log_err("No \"path\" parameter specified");
		return ERR_PTR(-MPD_MISS_VALUE);
	}

	fd = fifo_data_new();
	fd->path = path;

	int ret = ao_base_init(&fd->base, &fifo_output_plugin, param);
	if (ret != MPD_SUCCESS) {
		fifo_data_free(fd);
		return ERR_PTR(ret);
	}

	ret = fifo_open(fd);
	if (ret != MPD_SUCCESS) {
		ao_base_finish(&fd->base);
		fifo_data_free(fd);
		return ERR_PTR(ret);
	}

	return &fd->base;
}

static void
fifo_output_finish(struct audio_output *ao)
{
	struct fifo_data *fd = (struct fifo_data *)ao;

	fifo_close(fd);
	ao_base_finish(&fd->base);
	fifo_data_free(fd);
}

static int
fifo_output_open(struct audio_output *ao, struct audio_format *audio_format)
{
	struct fifo_data *fd = (struct fifo_data *)ao;

	fd->timer = timer_new(audio_format);

	return MPD_SUCCESS;
}

static void
fifo_output_close(struct audio_output *ao)
{
	struct fifo_data *fd = (struct fifo_data *)ao;

	timer_free(fd->timer);
}

static void
fifo_output_cancel(struct audio_output *ao)
{
	struct fifo_data *fd = (struct fifo_data *)ao;
	char buf[FIFO_BUFFER_SIZE];
	int bytes = 1;

	timer_reset(fd->timer);

	while (bytes > 0 && errno != EINTR)
		bytes = read(fd->input, buf, FIFO_BUFFER_SIZE);

	if (bytes < 0 && errno != EAGAIN) {
		log_warning("Flush of FIFO \"%s\" failed: %s",
			  fd->path, strerror(errno));
	}
}

static unsigned
fifo_output_delay(struct audio_output *ao)
{
	struct fifo_data *fd = (struct fifo_data *)ao;

	return fd->timer->started
		? timer_delay(fd->timer)
		: 0;
}

static size_t
fifo_output_play(struct audio_output *ao, const void *chunk, size_t size)
{
	struct fifo_data *fd = (struct fifo_data *)ao;
	ssize_t bytes;

	if (!fd->timer->started)
		timer_start(fd->timer);
	timer_add(fd->timer, size);

	while (true) {
		bytes = write(fd->output, chunk, size);
		if (bytes > 0)
			return (size_t)bytes;

		if (bytes < 0) {
			switch (errno) {
			case EAGAIN:
				/* The pipe is full, so empty it */
				fifo_output_cancel(&fd->base);
				continue;
			case EINTR:
				continue;
			}

			log_err("Failed to write to FIFO %s: %s",
				    fd->path, strerror(errno));
			return 0;
		}
	}
}

const struct audio_output_plugin fifo_output_plugin = {
	.name = "fifo",
	.init = fifo_output_init,
	.finish = fifo_output_finish,
	.open = fifo_output_open,
	.close = fifo_output_close,
	.delay = fifo_output_delay,
	.play = fifo_output_play,
	.cancel = fifo_output_cancel,
};
