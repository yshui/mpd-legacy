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

#define LOG_DOMAIN "filter: chain"

#include "log.h"
#include "config.h"
#include "conf.h"
#include "filter/chain.h"
#include "filter_plugin.h"
#include "filter_internal.h"
#include "filter_registry.h"
#include "audio_format.h"

#include <assert.h>

struct filter_chain {
	/** the base class */
	struct filter base;

	GSList *children;
};

static struct filter *
chain_filter_init(const struct config_param *param)
{
	struct filter_chain *chain = g_new(struct filter_chain, 1);

	filter_init(&chain->base, &chain_filter_plugin);
	chain->children = NULL;

	return &chain->base;
}

static void
chain_free_child(gpointer data, gpointer user_data)
{
	struct filter *filter = data;

	filter_free(filter);
}

static void
chain_filter_finish(struct filter *_filter)
{
	struct filter_chain *chain = (struct filter_chain *)_filter;

	g_slist_foreach(chain->children, chain_free_child, NULL);
	g_slist_free(chain->children);

	g_free(chain);
}

/**
 * Close all filters in the chain until #until is reached.  #until
 * itself is not closed.
 */
static void
chain_close_until(struct filter_chain *chain, const struct filter *until)
{
	GSList *i = chain->children;
	struct filter *filter;

	while (true) {
		/* this assertion fails if #until does not exist
		   (anymore) */
		assert(i != NULL);

		if (i->data == until)
			/* don't close this filter */
			break;

		/* close this filter */
		filter = i->data;
		filter_close(filter);

		i = g_slist_next(i);
	}
}

static const struct audio_format *
chain_open_child(struct filter *filter,
		 const struct audio_format *prev_audio_format)
{
	struct audio_format conv_audio_format = *prev_audio_format;
	const struct audio_format *next_audio_format;

	next_audio_format = filter_open(filter, &conv_audio_format);
	if (IS_ERR(next_audio_format))
		return next_audio_format;

	if (!audio_format_equals(&conv_audio_format, prev_audio_format)) {
		struct audio_format_string s;

		filter_close(filter);
		log_err("Audio format not supported by filter '%s': %s",
			    filter->plugin->name,
			    audio_format_to_string(prev_audio_format, &s));
		return ERR_PTR(-MPD_NIMPL);
	}

	return next_audio_format;
}

static const struct audio_format *
chain_filter_open(struct filter *_filter, struct audio_format *in_audio_format)
{
	struct filter_chain *chain = (struct filter_chain *)_filter;
	const struct audio_format *audio_format = in_audio_format;

	for (GSList *i = chain->children; i != NULL; i = g_slist_next(i)) {
		struct filter *filter = i->data;

		audio_format = chain_open_child(filter, audio_format);
		if (IS_ERR(audio_format)) {
			/* rollback, close all children */
			chain_close_until(chain, filter);
			return audio_format;
		}
	}

	/* return the output format of the last filter */
	return audio_format;
}

static void
chain_close_child(gpointer data, gpointer user_data)
{
	struct filter *filter = data;

	filter_close(filter);
}

static void
chain_filter_close(struct filter *_filter)
{
	struct filter_chain *chain = (struct filter_chain *)_filter;

	g_slist_foreach(chain->children, chain_close_child, NULL);
}

static const void *
chain_filter_filter(struct filter *_filter,
		    const void *src, size_t src_size,
		    size_t *dest_size_r)
{
	struct filter_chain *chain = (struct filter_chain *)_filter;

	for (GSList *i = chain->children; i != NULL; i = g_slist_next(i)) {
		struct filter *filter = i->data;

		/* feed the output of the previous filter as input
		   into the current one */
		src = filter_filter(filter, src, src_size, &src_size);
		if (IS_ERR(src))
			return src;
	}

	/* return the output of the last filter */
	*dest_size_r = src_size;
	return src;
}

const struct filter_plugin chain_filter_plugin = {
	.name = "chain",
	.init = chain_filter_init,
	.finish = chain_filter_finish,
	.open = chain_filter_open,
	.close = chain_filter_close,
	.filter = chain_filter_filter,
};

struct filter *
filter_chain_new(void)
{
	struct filter *filter = filter_new(&chain_filter_plugin, NULL);
	/* chain_filter_init() never fails */
	assert(!IS_ERR(filter));

	return filter;
}

void
filter_chain_append(struct filter *_chain, struct filter *filter)
{
	struct filter_chain *chain = (struct filter_chain *)_chain;

	chain->children = g_slist_append(chain->children, filter);
}

