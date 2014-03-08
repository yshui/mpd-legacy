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

#define LOG_DOMAIN "output: pipe"

#include "log.h"
#include "config.h"
#include "pipe_output_plugin.h"
#include "output_api.h"

#include <stdio.h>
#include <errno.h>

struct pipe_output {
	struct audio_output base;

	char *cmd;
	FILE *fh;
};

static struct audio_output *
pipe_output_init(const struct config_param *param)
{
	struct pipe_output *pd = g_new(struct pipe_output, 1);

	int ret = ao_base_init(&pd->base, &pipe_output_plugin, param);
	if (ret != MPD_SUCCESS) {
		free(pd);
		return ERR_PTR(ret);
	}

	pd->cmd = config_dup_block_string(param, "command", NULL);
	if (pd->cmd == NULL) {
		log_err("No \"command\" parameter specified");
		return ERR_PTR(-MPD_MISS_VALUE);
	}

	return &pd->base;
}

static void
pipe_output_finish(struct audio_output *ao)
{
	struct pipe_output *pd = (struct pipe_output *)ao;

	free(pd->cmd);
	ao_base_finish(&pd->base);
	g_free(pd);
}

static int
pipe_output_open(struct audio_output *ao,
		 struct audio_format *audio_format)
{
	struct pipe_output *pd = (struct pipe_output *)ao;

	pd->fh = popen(pd->cmd, "w");
	if (pd->fh == NULL) {
		log_err("Error opening pipe \"%s\": %s",
			    pd->cmd, strerror(errno));
		return -MPD_ACCESS;
	}

	return MPD_SUCCESS;
}

static void
pipe_output_close(struct audio_output *ao)
{
	struct pipe_output *pd = (struct pipe_output *)ao;

	pclose(pd->fh);
}

static size_t
pipe_output_play(struct audio_output *ao, const void *chunk, size_t size)
{
	struct pipe_output *pd = (struct pipe_output *)ao;
	size_t ret;

	ret = fwrite(chunk, 1, size, pd->fh);
	if (ret == 0)
		log_err("Write error on pipe: %s", strerror(errno));

	return ret;
}

const struct audio_output_plugin pipe_output_plugin = {
	.name = "pipe",
	.init = pipe_output_init,
	.finish = pipe_output_finish,
	.open = pipe_output_open,
	.close = pipe_output_close,
	.play = pipe_output_play,
};
