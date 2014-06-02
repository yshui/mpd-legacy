/*
 * Copyright (C) 2011 The Music Player Daemon Project
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
#include "input/despotify.h"
#include "input_internal.h"
#include "input_plugin.h"
#include "tag.h"
#include "despotify_utils.h"

#include <glib.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <despotify.h>

#include <stdio.h>

struct input_despotify {
	struct input_stream base;

	struct despotify_session *session;
	struct ds_track *track;
	struct tag *tag;
	struct ds_pcm_data pcm;
	size_t len_available;
	bool eof;
};


static void
refill_buffer(struct input_despotify *ctx)
{
	/* Wait until there is data */
	while (1) {
		int rc = despotify_get_pcm(ctx->session, &ctx->pcm);

		if (rc == 0 && ctx->pcm.len) {
			ctx->len_available = ctx->pcm.len;
			break;
		}
		if (ctx->eof == true)
			break;

		if (rc < 0) {
			log_debug("despotify_get_pcm error\n");
			ctx->eof = true;
			break;
		}

		/* Wait a while until next iteration */
		usleep(50 * 1000);
	}
}

static void callback(struct despotify_session* ds,
		int sig, void* data, void* callback_data)
{
	struct input_despotify *ctx = (struct input_despotify *)callback_data;

	switch (sig) {
	case DESPOTIFY_NEW_TRACK:
		break;

	case DESPOTIFY_TIME_TELL:
		break;

	case DESPOTIFY_TRACK_PLAY_ERROR:
		log_debug("Track play error\n");
		ctx->eof = true;
		ctx->len_available = 0;
		break;

	case DESPOTIFY_END_OF_PLAYLIST:
		ctx->eof = true;
		log_debug("End of playlist: %d\n", ctx->eof);
		break;
	}
}


static struct input_stream *
input_despotify_open(const char *url,
		     GMutex *mutex, GCond *cond)
{
	struct input_despotify *ctx;
	struct despotify_session *session;
	struct ds_link *ds_link;
	struct ds_track *track;

	if (!g_str_has_prefix(url, "spt://"))
		return NULL;

	session = mpd_despotify_get_session();
	if (IS_ERR(session))
		return (void *)session;

	ds_link = despotify_link_from_uri(url + 6);
	if (!ds_link) {
		log_debug("Can't find %s\n", url);
		return ERR_PTR(-MPD_INVAL);
	}
	if (ds_link->type != LINK_TYPE_TRACK) {
		despotify_free_link(ds_link);
		return ERR_PTR(-MPD_INVAL);
	}

	ctx = tmalloc(struct input_despotify, 1);
	memset(ctx, 0, sizeof(*ctx));

	track = despotify_link_get_track(session, ds_link);
	despotify_free_link(ds_link);
	if (!track) {
		free(ctx);
		return ERR_PTR(-MPD_3RD);
	}

	input_stream_init(&ctx->base, &input_plugin_despotify, url,
			  mutex, cond);
	ctx->session = session;
	ctx->track = track;
	ctx->tag = mpd_despotify_tag_from_track(track);
	ctx->eof = false;
	/* Despotify outputs pcm data */
	ctx->base.mime = g_strdup("audio/x-mpd-cdda-pcm");
	ctx->base.ready = true;
	ctx->base.seekable = false;

	if (!mpd_despotify_register_callback(callback, ctx)) {
		despotify_free_link(ds_link);

		return NULL;
	}

	if (despotify_play(ctx->session, ctx->track, false) == false) {
		despotify_free_track(ctx->track);
		free(ctx);
		return -MPD_3RD;
	}

	return &ctx->base;
}

static ssize_t
input_despotify_read(struct input_stream *is, void *ptr, size_t size)
{
	struct input_despotify *ctx = (struct input_despotify *)is;
	size_t to_cpy = size;

	if (ctx->len_available == 0)
		refill_buffer(ctx);

	if (ctx->len_available < size)
		to_cpy = ctx->len_available;
	memcpy(ptr, ctx->pcm.buf, to_cpy);
	ctx->len_available -= to_cpy;

	is->offset += to_cpy;

	return to_cpy;
}

static void
input_despotify_close(struct input_stream *is)
{
	struct input_despotify *ctx = (struct input_despotify *)is;

	if (ctx->tag != NULL)
		tag_free(ctx->tag);

	mpd_despotify_unregister_callback(callback);
	despotify_free_track(ctx->track);
	input_stream_deinit(&ctx->base);
	g_free(ctx);
}

static bool
input_despotify_eof(struct input_stream *is)
{
	struct input_despotify *ctx = (struct input_despotify *)is;

	return ctx->eof;
}

static struct tag *
input_despotify_tag(struct input_stream *is)
{
	struct input_despotify *ctx = (struct input_despotify *)is;
	struct tag *tag = ctx->tag;

	ctx->tag = NULL;

	return tag;
}

const struct input_plugin input_plugin_despotify = {
	.name = "spt",
	.open = input_despotify_open,
	.close = input_despotify_close,
	.read = input_despotify_read,
	.eof = input_despotify_eof,
	.tag = input_despotify_tag,
};
