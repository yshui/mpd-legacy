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

#define LOG_DOMAIN "pcm_resmaple: libsamplerate"

#include "log.h"
#include "config.h"
#include "pcm_resample_internal.h"
#include "conf.h"

#include <glib.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int lsr_converter = SRC_SINC_FASTEST;

static bool
lsr_parse_converter(const char *s)
{
	assert(s != NULL);

	if (*s == 0)
		return true;

	char *endptr;
	long l = strtol(s, &endptr, 10);
	if (*endptr == 0 && src_get_name(l) != NULL) {
		lsr_converter = l;
		return true;
	}

	size_t length = strlen(s);
	for (int i = 0;; ++i) {
		const char *name = src_get_name(i);
		if (name == NULL)
			break;

		if (g_ascii_strncasecmp(s, name, length) == 0) {
			lsr_converter = i;
			return true;
		}
	}

	return false;
}

int
pcm_resample_lsr_global_init(const char *converter)
{
	if (!lsr_parse_converter(converter)) {
		log_err("unknown samplerate converter '%s'", converter);
		return -MPD_INVAL;
	}

	log_debug("libsamplerate converter '%s'",
		src_get_name(lsr_converter));

	return MPD_SUCCESS;
}

void
pcm_resample_lsr_init(struct pcm_resample_state *state)
{
	memset(state, 0, sizeof(*state));

	pcm_buffer_init(&state->in);
	pcm_buffer_init(&state->out);
	pcm_buffer_init(&state->buffer);
}

void
pcm_resample_lsr_deinit(struct pcm_resample_state *state)
{
	if (state->state != NULL)
		state->state = src_delete(state->state);

	pcm_buffer_deinit(&state->in);
	pcm_buffer_deinit(&state->out);
	pcm_buffer_deinit(&state->buffer);
}

void
pcm_resample_lsr_reset(struct pcm_resample_state *state)
{
	if (state->state != NULL)
		src_reset(state->state);
}

static int
pcm_resample_set(struct pcm_resample_state *state,
		 unsigned channels, unsigned src_rate, unsigned dest_rate)
{
	SRC_DATA *data = &state->data;

	/* (re)set the state/ratio if the in or out format changed */
	if (channels == state->prev.channels &&
	    src_rate == state->prev.src_rate &&
	    dest_rate == state->prev.dest_rate)
		return true;

	state->error = 0;
	state->prev.channels = channels;
	state->prev.src_rate = src_rate;
	state->prev.dest_rate = dest_rate;

	if (state->state)
		state->state = src_delete(state->state);

	state->state = src_new(lsr_converter, channels, &state->error);
	if (!state->state) {
		log_err("libsamplerate initialization has failed: %s",
			    src_strerror(state->error));
		return -MPD_3RD;
	}

	data->src_ratio = (double)dest_rate / (double)src_rate;
	log_debug("setting samplerate conversion ratio to %.2lf",
		data->src_ratio);
	src_set_ratio(state->state, data->src_ratio);

	return MPD_SUCCESS;
}

static int
lsr_process(struct pcm_resample_state *state)
{
	if (state->error == 0)
		state->error = src_process(state->state, &state->data);
	if (state->error) {
		log_err("libsamplerate has failed: %s",
			    src_strerror(state->error));
		return -MPD_3RD;
	}

	return MPD_SUCCESS;
}

static float *
deconst_float_buffer(const float *in)
{
	union {
		const float *in;
		float *out;
	} u = { .in = in };
	return u.out;
}

const float *
pcm_resample_lsr_float(struct pcm_resample_state *state,
		       unsigned channels,
		       unsigned src_rate,
		       const float *src_buffer, size_t src_size,
		       unsigned dest_rate, size_t *dest_size_r)
{
	assert((src_size % (sizeof(*src_buffer) * channels)) == 0);

	int ret = pcm_resample_set(state, channels, src_rate, dest_rate);
	if (ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	SRC_DATA *data = &state->data;
	data->input_frames = src_size / sizeof(*src_buffer) / channels;
	data->data_in = deconst_float_buffer(src_buffer);

	data->output_frames = (src_size * dest_rate + src_rate - 1) / src_rate;
	size_t data_out_size = data->output_frames * sizeof(float) * channels;
	data->data_out = pcm_buffer_get(&state->out, data_out_size);

	ret = lsr_process(state);
	if (ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	*dest_size_r = data->output_frames_gen *
		sizeof(*data->data_out) * channels;
	return data->data_out;
}

const int16_t *
pcm_resample_lsr_16(struct pcm_resample_state *state,
		    unsigned channels,
		    unsigned src_rate,
		    const int16_t *src_buffer, size_t src_size,
		    unsigned dest_rate, size_t *dest_size_r)
{
	SRC_DATA *data = &state->data;
	size_t data_in_size;
	size_t data_out_size;
	int16_t *dest_buffer;

	assert((src_size % (sizeof(*src_buffer) * channels)) == 0);

	int ret = pcm_resample_set(state, channels, src_rate, dest_rate);
	if (ret != MPD_SUCCESS)
		return NULL;

	data->input_frames = src_size / sizeof(*src_buffer) / channels;
	data_in_size = data->input_frames * sizeof(float) * channels;
	data->data_in = pcm_buffer_get(&state->in, data_in_size);

	data->output_frames = (src_size * dest_rate + src_rate - 1) / src_rate;
	data_out_size = data->output_frames * sizeof(float) * channels;
	data->data_out = pcm_buffer_get(&state->out, data_out_size);

	src_short_to_float_array(src_buffer, data->data_in,
				 data->input_frames * channels);

	ret = lsr_process(state);
	if (ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	*dest_size_r = data->output_frames_gen *
		sizeof(*dest_buffer) * channels;
	dest_buffer = pcm_buffer_get(&state->buffer, *dest_size_r);
	src_float_to_short_array(data->data_out, dest_buffer,
				 data->output_frames_gen * channels);

	return dest_buffer;
}

#ifdef HAVE_LIBSAMPLERATE_NOINT

/* libsamplerate introduced these functions in v0.1.3 */

static void
src_int_to_float_array(const int *in, float *out, int len)
{
	while (len-- > 0)
		*out++ = *in++ / (float)(1 << (24 - 1));
}

static void
src_float_to_int_array (const float *in, int *out, int len)
{
	while (len-- > 0)
		*out++ = *in++ * (float)(1 << (24 - 1));
}

#endif

const int32_t *
pcm_resample_lsr_32(struct pcm_resample_state *state,
		    unsigned channels,
		    unsigned src_rate,
		    const int32_t *src_buffer, size_t src_size,
		    unsigned dest_rate, size_t *dest_size_r)
{
	SRC_DATA *data = &state->data;
	size_t data_in_size;
	size_t data_out_size;
	int32_t *dest_buffer;

	assert((src_size % (sizeof(*src_buffer) * channels)) == 0);

	int ret = pcm_resample_set(state, channels, src_rate, dest_rate);
	if (ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	data->input_frames = src_size / sizeof(*src_buffer) / channels;
	data_in_size = data->input_frames * sizeof(float) * channels;
	data->data_in = pcm_buffer_get(&state->in, data_in_size);

	data->output_frames = (src_size * dest_rate + src_rate - 1) / src_rate;
	data_out_size = data->output_frames * sizeof(float) * channels;
	data->data_out = pcm_buffer_get(&state->out, data_out_size);

	src_int_to_float_array(src_buffer, data->data_in,
			       data->input_frames * channels);

	ret = lsr_process(state);
	if (ret != MPD_SUCCESS)
		return ERR_PTR(ret);

	*dest_size_r = data->output_frames_gen *
		sizeof(*dest_buffer) * channels;
	dest_buffer = pcm_buffer_get(&state->buffer, *dest_size_r);
	src_float_to_int_array(data->data_out, dest_buffer,
			       data->output_frames_gen * channels);

	return dest_buffer;
}
