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

#define LOG_DOMAIN "input: file"

#include "log.h"
#include "config.h" /* must be first for large file support */
#include "input/file.h"
#include "input_internal.h"
#include "input_plugin.h"
#include "fd_util.h"
#include "open.h"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <glib.h>

struct file_input_stream {
	struct input_stream base;

	int fd;
};

static struct input_stream *
input_file_open(const char *filename)
{
	int fd, ret;
	struct stat st;
	struct file_input_stream *fis;

	if (!g_path_is_absolute(filename))
		return NULL;

	fd = open_cloexec(filename, O_RDONLY|O_BINARY, 0);
	if (fd < 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			log_err("Failed to open \"%s\": %s",
				    filename, strerror(errno));
		return ERR_PTR(-MPD_ACCESS);
	}

	ret = fstat(fd, &st);
	if (ret < 0) {
		log_err("Failed to stat \"%s\": %s",
			    filename, strerror(errno));
		close(fd);
		return ERR_PTR(-MPD_ACCESS);
	}

	if (!S_ISREG(st.st_mode)) {
		log_err("Not a regular file: %s", filename);
		close(fd);
		return ERR_PTR(-MPD_INVAL);
	}

#ifdef POSIX_FADV_SEQUENTIAL
	posix_fadvise(fd, (off_t)0, st.st_size, POSIX_FADV_SEQUENTIAL);
#endif

	fis = g_new(struct file_input_stream, 1);
	input_stream_init(&fis->base, &input_plugin_file, filename);

	fis->base.size = st.st_size;
	fis->base.seekable = true;
	fis->base.ready = true;

	fis->fd = fd;

	return &fis->base;
}

static int
input_file_seek(struct input_stream *is, off64_t offset, int whence)
{
	struct file_input_stream *fis = (struct file_input_stream *)is;

	offset = (goffset)lseek(fis->fd, (off_t)offset, whence);
	if (offset < 0) {
		log_err("Failed to seek: %s", strerror(errno));
		return -MPD_ACCESS;
	}

	is->offset = offset;
	return MPD_SUCCESS;
}

static ssize_t
input_file_read(struct input_stream *is, void *ptr, size_t size)
{
	struct file_input_stream *fis = (struct file_input_stream *)is;
	ssize_t nbytes;

	nbytes = read(fis->fd, ptr, size);
	if (nbytes < 0) {
		log_err("Failed to read: %s", strerror(errno));
		return -MPD_ACCESS;
	}

	is->offset += nbytes;
	return (size_t)nbytes;
}

static void
input_file_close(struct input_stream *is)
{
	struct file_input_stream *fis = (struct file_input_stream *)is;

	close(fis->fd);
	input_stream_deinit(&fis->base);
	g_free(fis);
}

static bool
input_file_eof(struct input_stream *is)
{
	return is->offset >= is->size;
}

const struct input_plugin input_plugin_file = {
	.name = "file",
	.open = input_file_open,
	.close = input_file_close,
	.read = input_file_read,
	.eof = input_file_eof,
	.seek = input_file_seek,
};
