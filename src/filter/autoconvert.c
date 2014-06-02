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

#include "config.h"
#include "filter/autoconvert.h"
#include "filter/convert.h"
#include "filter_plugin.h"
#include "filter_internal.h"
#include "filter_registry.h"
#include "conf.h"
#include "pcm/pcm_convert.h"
#include "audio_format.h"
#include "poison.h"

#include <assert.h>
#include <string.h>

struct autoconvert_filter {
	struct filter base;

	/**
	 * The audio format being fed to the underlying filter.  This
	 * plugin actually doesn't need this variable, we have it here
	 * just so our open() method doesn't return a stack pointer.
	 */
	struct audio_format in_audio_format;

	/**
	 * The underlying filter.
	 */
	struct filter *filter;

	/**
	 * A convert_filter, just in case conversion is needed.  NULL
	 * if unused.
	 */
	struct filter *convert;
};

static void
autoconvert_filter_finish(struct filter *_filter)
{
	struct autoconvert_filter *filter =
		(struct autoconvert_filter *)_filter;

	filter_free(filter->filter);
	free(filter);
}

static const struct audio_format *
autoconvert_filter_open(struct filter *_filter,
			struct audio_format *in_audio_format)
{
	struct autoconvert_filter *filter =
		(struct autoconvert_filter *)_filter;
	const struct audio_format *out_audio_format;

	assert(audio_format_valid(in_audio_format));

	/* open the "real" filter */

	filter->in_audio_format = *in_audio_format;

	out_audio_format = filter_open(filter->filter,
				       &filter->in_audio_format);
	if (IS_ERR(out_audio_format))
		return out_audio_format;

	/* need to convert? */

	if (!audio_format_equals(&filter->in_audio_format, in_audio_format)) {
		/* yes - create a convert_filter */
		struct audio_format audio_format2 = *in_audio_format;
		const struct audio_format *audio_format3;

		filter->convert = filter_new(&convert_filter_plugin, NULL);
		if (IS_ERR(filter->convert)) {
			filter_close(filter->filter);
			return (void *)filter->convert;
		}

		audio_format3 = filter_open(filter->convert, &audio_format2);
		if (IS_ERR(audio_format3)) {
			filter_free(filter->convert);
			filter_close(filter->filter);
			return audio_format3;
		}

		assert(audio_format_equals(&audio_format2, in_audio_format));

		convert_filter_set(filter->convert, &filter->in_audio_format);
	} else
		/* no */
		filter->convert = NULL;

	return out_audio_format;
}

static void
autoconvert_filter_close(struct filter *_filter)
{
	struct autoconvert_filter *filter =
		(struct autoconvert_filter *)_filter;

	if (filter->convert != NULL) {
		filter_close(filter->convert);
		filter_free(filter->convert);
	}

	filter_close(filter->filter);
}

static const void *
autoconvert_filter_filter(struct filter *_filter, const void *src,
			  size_t src_size, size_t *dest_size_r)
{
	struct autoconvert_filter *filter =
		(struct autoconvert_filter *)_filter;

	if (filter->convert != NULL) {
		src = filter_filter(filter->convert, src, src_size, &src_size);
		if (IS_ERR(src))
			return src;
	}

	return filter_filter(filter->filter, src, src_size, dest_size_r);
}

static const struct filter_plugin autoconvert_filter_plugin = {
	.name = "convert",
	.finish = autoconvert_filter_finish,
	.open = autoconvert_filter_open,
	.close = autoconvert_filter_close,
	.filter = autoconvert_filter_filter,
};

struct filter *
autoconvert_filter_new(struct filter *_filter)
{
	struct autoconvert_filter *filter =
		g_new(struct autoconvert_filter, 1);

	filter_init(&filter->base, &autoconvert_filter_plugin);
	filter->filter = _filter;

	return &filter->base;
}
