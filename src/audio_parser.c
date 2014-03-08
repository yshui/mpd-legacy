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

/*
 * Parser functions for audio related objects.
 *
 */

#define LOG_DOMAIN "audio_parser"

#include "log.h"
#include "config.h"
#include "audio_parser.h"
#include "audio_format.h"
#include "audio_check.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

static int
parse_sample_rate(const char *src, bool mask, uint32_t *sample_rate_r,
		  const char **endptr_r)
{
	unsigned long value;
	char *endptr;
	int ret;

	if (mask && *src == '*') {
		*sample_rate_r = 0;
		*endptr_r = src + 1;
		return MPD_SUCCESS;
	}

	value = strtoul(src, &endptr, 10);
	if (endptr == src) {
		log_err("Failed to parse the sample rate");
		return -MPD_INVAL;
	} else {
		ret = audio_check_sample_rate(value);
		if (ret != MPD_SUCCESS)
			return ret;
	}

	*sample_rate_r = value;
	*endptr_r = endptr;
	return MPD_SUCCESS;
}

static int
parse_sample_format(const char *src, bool mask,
		    enum sample_format *sample_format_r,
		    const char **endptr_r)
{
	unsigned long value;
	char *endptr;
	enum sample_format sample_format;

	if (mask && *src == '*') {
		*sample_format_r = SAMPLE_FORMAT_UNDEFINED;
		*endptr_r = src + 1;
		return MPD_SUCCESS;
	}

	if (*src == 'f') {
		*sample_format_r = SAMPLE_FORMAT_FLOAT;
		*endptr_r = src + 1;
		return MPD_SUCCESS;
	}

	if (memcmp(src, "dsd", 3) == 0) {
		*sample_format_r = SAMPLE_FORMAT_DSD;
		*endptr_r = src + 3;
		return MPD_SUCCESS;
	}

	value = strtoul(src, &endptr, 10);
	if (endptr == src) {
		log_err("Failed to parse the sample format");
		return -MPD_INVAL;
	}

	switch (value) {
	case 8:
		sample_format = SAMPLE_FORMAT_S8;
		break;

	case 16:
		sample_format = SAMPLE_FORMAT_S16;
		break;

	case 24:
		if (memcmp(endptr, "_3", 2) == 0)
			/* for backwards compatibility */
			endptr += 2;

		sample_format = SAMPLE_FORMAT_S24_P32;
		break;

	case 32:
		sample_format = SAMPLE_FORMAT_S32;
		break;

	default:
		log_err("Invalid sample format: %lu", value);
		return -MPD_INVAL;
	}

	assert(audio_valid_sample_format(sample_format));

	*sample_format_r = sample_format;
	*endptr_r = endptr;
	return MPD_SUCCESS;
}

static int
parse_channel_count(const char *src, bool mask, uint8_t *channels_r,
		    const char **endptr_r)
{
	unsigned long value;
	char *endptr;
	int ret;

	if (mask && *src == '*') {
		*channels_r = 0;
		*endptr_r = src + 1;
		return MPD_SUCCESS;
	}

	value = strtoul(src, &endptr, 10);
	if (endptr == src) {
		log_err("Failed to parse the channel count");
		return -MPD_INVAL;
	} else {
		ret = audio_check_channel_count(value);
		if (ret != MPD_SUCCESS)
			return ret;
	}

	*channels_r = value;
	*endptr_r = endptr;
	return MPD_SUCCESS;
}

int
audio_format_parse(struct audio_format *dest, const char *src,
		   bool mask)
{
	uint32_t rate = 0;
	enum sample_format sample_format = SAMPLE_FORMAT_UNDEFINED;
	uint8_t channels;

	audio_format_clear(dest);

	/* parse sample rate */

	int ret = parse_sample_rate(src, mask, &rate, &src);
	if (ret != MPD_SUCCESS)
		return ret;

	if (*src++ != ':') {
		log_err("Sample format missing");
		return -MPD_MISS_VALUE;
	}

	/* parse sample format */

	ret = parse_sample_format(src, mask, &sample_format, &src);
	if (ret != MPD_SUCCESS)
		return ret;

	if (*src++ != ':') {
		log_err("Channel count missing");
		return -MPD_MISS_VALUE;
	}

	/* parse channel count */

	ret = parse_channel_count(src, mask, &channels, &src);
	if (ret != MPD_SUCCESS)
		return ret;

	if (*src != 0) {
		log_err("Extra data after channel count: %s", src);
		return -MPD_INVAL;
	}

	audio_format_init(dest, rate, sample_format, channels);
	assert(mask ? audio_format_mask_valid(dest)
	       : audio_format_valid(dest));

	return MPD_SUCCESS;
}
