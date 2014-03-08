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

#define LOG_DOMAIN "audio_format: check"

#include "log.h"
#include "audio_check.h"
#include "audio_format.h"

#include <assert.h>

int
audio_check_sample_rate(unsigned long sample_rate)
{
	if (!audio_valid_sample_rate(sample_rate)) {
		log_err("Invalid sample rate: %lu", sample_rate);
		return -MPD_INVAL;
	}

	return MPD_SUCCESS;
}

int
audio_check_sample_format(enum sample_format sample_format)
{
	if (!audio_valid_sample_format(sample_format)) {
		log_err("Invalid sample format: %u", sample_format);
		return -MPD_INVAL;
	}

	return MPD_SUCCESS;
}

int
audio_check_channel_count(unsigned channels)
{
	if (!audio_valid_channel_count(channels)) {
		log_err("Invalid channel count: %u", channels);
		return -MPD_INVAL;
	}

	return MPD_SUCCESS;
}

int
audio_format_init_checked(struct audio_format *af, unsigned long sample_rate,
			  enum sample_format sample_format, unsigned channels)
{
	int ret = audio_check_sample_rate(sample_rate);
	if (ret != MPD_SUCCESS)
		return ret;

	ret = audio_check_sample_format(sample_format);
	if (ret != MPD_SUCCESS)
		return ret;

	ret = audio_check_channel_count(channels);
	if (ret != MPD_SUCCESS)
		return ret;

	audio_format_init(af, sample_rate, sample_format, channels);
	assert(audio_format_valid(af));
	return MPD_SUCCESS;
}
