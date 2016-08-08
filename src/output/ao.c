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

#define LOG_DOMAIN "decoder: ao"

#include "log.h"
#include "config.h"
#include "ao.h"
#include "output_api.h"

#include <ao/ao.h>
#include <glib.h>

/* An ao_sample_format, with all fields set to zero: */
static const ao_sample_format OUR_AO_FORMAT_INITIALIZER;

static unsigned ao_output_ref;

struct ao_data {
	struct audio_output base;

	size_t write_size;
	int driver;
	ao_option *options;
	ao_device *device;
} AoData;

static void
ao_output_error(void)
{
	const char *error;

	switch (errno) {
	case AO_ENODRIVER:
		error = "No such libao driver";
		break;

	case AO_ENOTLIVE:
		error = "This driver is not a libao live device";
		break;

	case AO_EBADOPTION:
		error = "Invalid libao option";
		break;

	case AO_EOPENDEVICE:
		error = "Cannot open the libao device";
		break;

	case AO_EFAIL:
		error = "Generic libao failure";
		break;

	default:
		error = strerror(errno);
	}
	log_err("%s", error);
}

static struct audio_output *
ao_output_init(const struct config_param *param)
{
	struct ao_data *ad = g_new(struct ao_data, 1);

	int ret = ao_base_init(&ad->base, &ao_output_plugin, param);
	if (ret != MPD_SUCCESS) {
		free(ad);
		return ERR_PTR(ret);
	}

	ao_info *ai;
	const char *value;

	ad->options = NULL;

	ad->write_size = config_get_block_unsigned(param, "write_size", 1024);

	if (ao_output_ref == 0) {
		ao_initialize();
	}
	ao_output_ref++;

	value = config_get_block_string(param, "driver", "default");
	if (0 == strcmp(value, "default"))
		ad->driver = ao_default_driver_id();
	else
		ad->driver = ao_driver_id(value);

	if (ad->driver < 0) {
		log_err("\"%s\" is not a valid ao driver",
			    value);
		ao_base_finish(&ad->base);
		free(ad);
		return ERR_PTR(-MPD_INVAL);
	}

	if ((ai = ao_driver_info(ad->driver)) == NULL) {
		log_err("problems getting driver info");
		ao_base_finish(&ad->base);
		free(ad);
		return ERR_PTR(-MPD_INVAL);
	}

	log_debug("using ao driver \"%s\" for \"%s\"\n", ai->short_name,
		config_get_block_string(param, "name", NULL));

	value = config_get_block_string(param, "options", NULL);
	if (value != NULL) {
		gchar **options = g_strsplit(value, ";", 0);

		for (unsigned i = 0; options[i] != NULL; ++i) {
			gchar **key_value = g_strsplit(options[i], "=", 2);

			if (key_value[0] == NULL || key_value[1] == NULL) {
				log_err("problems parsing options \"%s\"",
					    options[i]);
				ao_base_finish(&ad->base);
				free(ad);
				return ERR_PTR(-MPD_INVAL);
			}

			ao_append_option(&ad->options, key_value[0],
					 key_value[1]);

			g_strfreev(key_value);
		}

		g_strfreev(options);
	}

	return &ad->base;
}

static void
ao_output_finish(struct audio_output *ao)
{
	struct ao_data *ad = (struct ao_data *)ao;

	ao_free_options(ad->options);
	ao_base_finish(&ad->base);
	g_free(ad);

	ao_output_ref--;

	if (ao_output_ref == 0)
		ao_shutdown();
}

static void
ao_output_close(struct audio_output *ao)
{
	struct ao_data *ad = (struct ao_data *)ao;

	ao_close(ad->device);
}

static int
ao_output_open(struct audio_output *ao, struct audio_format *audio_format)
{
	ao_sample_format format = OUR_AO_FORMAT_INITIALIZER;
	struct ao_data *ad = (struct ao_data *)ao;

	switch (audio_format->format) {
	case SAMPLE_FORMAT_S8:
		format.bits = 8;
		break;

	case SAMPLE_FORMAT_S16:
		format.bits = 16;
		break;

	default:
		/* support for 24 bit samples in libao is currently
		   dubious, and until we have sorted that out,
		   convert everything to 16 bit */
		audio_format->format = SAMPLE_FORMAT_S16;
		format.bits = 16;
		break;
	}

	format.rate = audio_format->sample_rate;
	format.byte_format = AO_FMT_NATIVE;
	format.channels = audio_format->channels;

	ad->device = ao_open_live(ad->driver, &format, ad->options);

	if (ad->device == NULL) {
		ao_output_error();
		return -MPD_3RD;
	}

	return MPD_SUCCESS;
}

/**
 * For whatever reason, libao wants a non-const pointer.  Let's hope
 * it does not write to the buffer, and use the union deconst hack to
 * work around this API misdesign.
 */
static int ao_play_deconst(ao_device *device, const void *output_samples,
			   uint_32 num_bytes)
{
	union {
		const void *in;
		void *out;
	} u;

	u.in = output_samples;
	return ao_play(device, u.out, num_bytes);
}

static size_t
ao_output_play(struct audio_output *ao, const void *chunk, size_t size)
{
	struct ao_data *ad = (struct ao_data *)ao;

	if (size > ad->write_size)
		size = ad->write_size;

	if (ao_play_deconst(ad->device, chunk, size) == 0) {
		ao_output_error();
		return 0;
	}

	return size;
}

const struct audio_output_plugin ao_output_plugin = {
	.name = "ao",
	.init = ao_output_init,
	.finish = ao_output_finish,
	.open = ao_output_open,
	.close = ao_output_close,
	.play = ao_output_play,
};
