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

#define LOG_DOMAIN "encoder: lame"

#include "log.h"
#include "config.h"
#include "encoder_api.h"
#include "encoder_plugin.h"
#include "audio_format.h"

#include <lame/lame.h>
#include <assert.h>
#include <string.h>

struct lame_encoder {
	struct encoder encoder;

	struct audio_format audio_format;
	float quality;
	int bitrate;

	lame_global_flags *gfp;

	unsigned char buffer[32768];
	size_t buffer_length;
};

extern const struct encoder_plugin lame_encoder_plugin;

static int
lame_encoder_configure(struct lame_encoder *encoder,
		       const struct config_param *param)
{
	const char *value;
	char *endptr;

	value = config_get_block_string(param, "quality", NULL);
	if (value != NULL) {
		/* a quality was configured (VBR) */

		encoder->quality = g_ascii_strtod(value, &endptr);

		if (*endptr != '\0' || encoder->quality < -1.0 ||
		    encoder->quality > 10.0) {
			log_err("quality \"%s\" is not a number in the "
				    "range -1 to 10, line %i",
				    value, param->line);
			return -MPD_INVAL;
		}

		if (config_get_block_string(param, "bitrate", NULL) != NULL) {
			log_err("quality and bitrate are "
				    "both defined (line %i)",
				    param->line);
			return -MPD_INVAL;
		}
	} else {
		/* a bit rate was configured */

		value = config_get_block_string(param, "bitrate", NULL);
		if (value == NULL) {
			log_err("neither bitrate nor quality defined "
				    "at line %i",
				    param->line);
			return -MPD_MISS_VALUE;
		}

		encoder->quality = -2.0;
		encoder->bitrate = g_ascii_strtoll(value, &endptr, 10);

		if (*endptr != '\0' || encoder->bitrate <= 0) {
			log_err("bitrate at line %i should be a positive integer",
				    param->line);
			return -MPD_INVAL;
		}
	}

	return MPD_SUCCESS;
}

static struct encoder *
lame_encoder_init(const struct config_param *param)
{
	struct lame_encoder *encoder;

	encoder = g_new(struct lame_encoder, 1);
	encoder_struct_init(&encoder->encoder, &lame_encoder_plugin);

	/* load configuration from "param" */
	int ret = lame_encoder_configure(encoder, param);
	if (ret != MPD_SUCCESS) {
		/* configuration has failed, roll back and return error */
		free(encoder);
		return ERR_PTR(ret);
	}

	return &encoder->encoder;
}

static void
lame_encoder_finish(struct encoder *_encoder)
{
	struct lame_encoder *encoder = (struct lame_encoder *)_encoder;

	/* the real liblame cleanup was already performed by
	   lame_encoder_close(), so no real work here */
	g_free(encoder);
}

static int
lame_encoder_setup(struct lame_encoder *encoder)
{
	if (encoder->quality >= -1.0) {
		/* a quality was configured (VBR) */

		if (0 != lame_set_VBR(encoder->gfp, vbr_rh)) {
			log_err("error setting lame VBR mode");
			return -MPD_3RD;
		}
		if (0 != lame_set_VBR_q(encoder->gfp, encoder->quality)) {
			log_err("error setting lame VBR quality");
			return -MPD_3RD;
		}
	} else {
		/* a bit rate was configured */

		if (0 != lame_set_brate(encoder->gfp, encoder->bitrate)) {
			log_err("error setting lame bitrate");
			return -MPD_3RD;
		}
	}

	if (0 != lame_set_num_channels(encoder->gfp,
				       encoder->audio_format.channels)) {
		log_err("error setting lame num channels");
		return -MPD_3RD;
	}

	if (0 != lame_set_in_samplerate(encoder->gfp,
					encoder->audio_format.sample_rate)) {
		log_err("error setting lame sample rate");
		return -MPD_3RD;
	}

	if (0 != lame_set_out_samplerate(encoder->gfp,
					 encoder->audio_format.sample_rate)) {
		log_err("error setting lame out sample rate");
		return -MPD_3RD;
	}

	if (0 > lame_init_params(encoder->gfp)) {
		log_err("error initializing lame params");
		return -MPD_3RD;
	}

	return MPD_SUCCESS;
}

static int
lame_encoder_open(struct encoder *_encoder, struct audio_format *audio_format)
{
	struct lame_encoder *encoder = (struct lame_encoder *)_encoder;

	audio_format->format = SAMPLE_FORMAT_S16;
	audio_format->channels = 2;

	encoder->audio_format = *audio_format;

	encoder->gfp = lame_init();
	if (encoder->gfp == NULL) {
		log_err("lame_init() failed");
		return -MPD_3RD;
	}

	int ret = lame_encoder_setup(encoder);
	if (ret != MPD_SUCCESS) {
		lame_close(encoder->gfp);
		return ret;
	}

	encoder->buffer_length = 0;

	return MPD_SUCCESS;
}

static void
lame_encoder_close(struct encoder *_encoder)
{
	struct lame_encoder *encoder = (struct lame_encoder *)_encoder;

	lame_close(encoder->gfp);
}

static ssize_t
lame_encoder_write(struct encoder *_encoder,
		   const void *data, size_t length)
{
	struct lame_encoder *encoder = (struct lame_encoder *)_encoder;
	unsigned num_frames;
	float *left, *right;
	const int16_t *src = (const int16_t*)data;
	unsigned int i;
	int bytes_out;

	assert(encoder->buffer_length == 0);

	num_frames =
		length / audio_format_frame_size(&encoder->audio_format);
	left = malloc(sizeof(left[0]) * num_frames);
	right = malloc(sizeof(right[0]) * num_frames);

	/* this is for only 16-bit audio */

	for (i = 0; i < num_frames; i++) {
		left[i] = *src++;
		right[i] = *src++;
	}

	bytes_out = lame_encode_buffer_float(encoder->gfp, left, right,
					     num_frames, encoder->buffer,
					     sizeof(encoder->buffer));

	g_free(left);
	g_free(right);

	if (bytes_out < 0) {
		log_err("lame encoder failed");
		return -MPD_3RD;
	}

	encoder->buffer_length = (size_t)bytes_out;
	return bytes_out;
}

static size_t
lame_encoder_read(struct encoder *_encoder, void *dest, size_t length)
{
	struct lame_encoder *encoder = (struct lame_encoder *)_encoder;

	if (length > encoder->buffer_length)
		length = encoder->buffer_length;

	memcpy(dest, encoder->buffer, length);

	encoder->buffer_length -= length;
	memmove(encoder->buffer, encoder->buffer + length,
		encoder->buffer_length);

	return length;
}

static const char *
lame_encoder_get_mime_type(struct encoder *_encoder)
{
	return "audio/mpeg";
}

const struct encoder_plugin lame_encoder_plugin = {
	.name = "lame",
	.init = lame_encoder_init,
	.finish = lame_encoder_finish,
	.open = lame_encoder_open,
	.close = lame_encoder_close,
	.write = lame_encoder_write,
	.read = lame_encoder_read,
	.get_mime_type = lame_encoder_get_mime_type,
};
