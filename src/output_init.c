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

#define LOG_DOMAIN "output_init"

#include "log.h"
#include "config.h"
#include "output_control.h"
#include "output_api.h"
#include "output_internal.h"
#include "output_list.h"
#include "audio_parser.h"
#include "mixer_control.h"
#include "mixer_type.h"
#include "mixer_list.h"
#include "mixer/software.h"
#include "filter_plugin.h"
#include "filter_registry.h"
#include "filter_config.h"
#include "filter/chain.h"
#include "filter/autoconvert.h"
#include "filter/replay_gain.h"

#include <glib.h>

#include <assert.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "output"

#define AUDIO_OUTPUT_TYPE	"type"
#define AUDIO_OUTPUT_NAME	"name"
#define AUDIO_OUTPUT_FORMAT	"format"
#define AUDIO_FILTERS		"filters"

static const struct audio_output_plugin *
audio_output_detect(void)
{
	log_warning("Attempt to detect audio output device");

	audio_output_plugins_for_each(plugin) {
		if (plugin->test_default_device == NULL)
			continue;

		log_warning("Attempting to detect a %s audio device",
			  plugin->name);
		if (ao_plugin_test_default_device(plugin))
			return plugin;
	}

	log_err("Unable to detect an audio device");
	return NULL;
}

/**
 * Determines the mixer type which should be used for the specified
 * configuration block.
 *
 * This handles the deprecated options mixer_type (global) and
 * mixer_enabled, if the mixer_type setting is not configured.
 */
static enum mixer_type
audio_output_mixer_type(const struct config_param *param)
{
	/* read the local "mixer_type" setting */
	const char *p = config_get_block_string(param, "mixer_type", NULL);
	if (p != NULL)
		return mixer_type_parse(p);

	/* try the local "mixer_enabled" setting next (deprecated) */
	if (!config_get_block_bool(param, "mixer_enabled", true))
		return MIXER_TYPE_NONE;

	/* fall back to the global "mixer_type" setting (also
	   deprecated) */
	return mixer_type_parse(config_get_string("mixer_type", "hardware"));
}

static struct mixer *
audio_output_load_mixer(struct audio_output *ao,
			const struct config_param *param,
			const struct mixer_plugin *plugin,
			struct filter *filter_chain)
{
	struct mixer *mixer;

	switch (audio_output_mixer_type(param)) {
	case MIXER_TYPE_NONE:
	case MIXER_TYPE_UNKNOWN:
		return NULL;

	case MIXER_TYPE_HARDWARE:
		if (plugin == NULL)
			return NULL;

		return mixer_new(plugin, ao, param);

	case MIXER_TYPE_SOFTWARE:
		mixer = mixer_new(&software_mixer_plugin, NULL, NULL);
		assert(!IS_ERR_OR_NULL(mixer));

		filter_chain_append(filter_chain,
				    software_mixer_get_filter(mixer));
		return mixer;
	}

	assert(false);
	return NULL;
}

int
ao_base_init(struct audio_output *ao,
	     const struct audio_output_plugin *plugin,
	     const struct config_param *param)
{
	assert(ao != NULL);
	assert(plugin != NULL);
	assert(plugin->finish != NULL);
	assert(plugin->open != NULL);
	assert(plugin->close != NULL);
	assert(plugin->play != NULL);

	if (param) {
		const char *p;

		ao->name = config_get_block_string(param, AUDIO_OUTPUT_NAME,
						   NULL);
		if (ao->name == NULL) {
			log_err("Missing \"name\" configuration");
			return -MPD_INVAL;
		}

		p = config_get_block_string(param, AUDIO_OUTPUT_FORMAT,
						 NULL);
		if (p != NULL) {
			int ret =
				audio_format_parse(&ao->config_audio_format,
						   p, true);
			if (ret != MPD_SUCCESS)
				return ret;
		} else
			audio_format_clear(&ao->config_audio_format);
	} else {
		ao->name = "default detected output";

		audio_format_clear(&ao->config_audio_format);
	}

	ao->plugin = plugin;
	ao->always_on = config_get_block_bool(param, "always_on", false);
	ao->enabled = config_get_block_bool(param, "enabled", true);
	ao->really_enabled = false;
	ao->open = false;
	ao->pause = false;
	ao->allow_play = true;
	ao->fail_timer = NULL;

	pcm_buffer_init(&ao->cross_fade_buffer);

	/* set up the filter chain */

	ao->filter = filter_chain_new();
	assert(ao->filter != NULL);

	/* create the normalization filter (if configured) */

	if (config_get_bool(CONF_VOLUME_NORMALIZATION, false)) {
		struct filter *normalize_filter =
			filter_new(&normalize_filter_plugin, NULL);
		assert(normalize_filter != NULL);

		filter_chain_append(ao->filter,
				    autoconvert_filter_new(normalize_filter));
	}

	int ret = filter_chain_parse(ao->filter,
	                   config_get_block_string(param, AUDIO_FILTERS, ""));

	// It's not really fatal - Part of the filter chain has been set up already
	// and even an empty one will work (if only with unexpected behaviour)
	if (ret != MPD_SUCCESS) {
		log_warning("Failed to initialize filter chain for '%s'",
			  ao->name);
	}

	ao->thread = NULL;
	ao->command = AO_COMMAND_NONE;
	ao->mutex = g_mutex_new();
	ao->cond = g_cond_new();

	ao->mixer = NULL;
	ao->replay_gain_filter = NULL;
	ao->other_replay_gain_filter = NULL;

	/* done */

	return MPD_SUCCESS;
}

static int
audio_output_setup(struct audio_output *ao, const struct config_param *param)
{

	/* create the replay_gain filter */

	const char *replay_gain_handler =
		config_get_block_string(param, "replay_gain_handler",
					"software");

	if (strcmp(replay_gain_handler, "none") != 0) {
		ao->replay_gain_filter = filter_new(&replay_gain_filter_plugin,
						    param);
		assert(!IS_ERR_OR_NULL(ao->replay_gain_filter));

		ao->replay_gain_serial = 0;

		ao->other_replay_gain_filter = filter_new(&replay_gain_filter_plugin,
							  param);
		assert(!IS_ERR_OR_NULL(ao->other_replay_gain_filter));

		ao->other_replay_gain_serial = 0;
	} else {
		ao->replay_gain_filter = NULL;
		ao->other_replay_gain_filter = NULL;
	}

	/* set up the mixer */

	if (ao->plugin->mixer_plugin) {
		ao->mixer = audio_output_load_mixer(ao, param,
						    ao->plugin->mixer_plugin,
						    ao->filter);
		if (IS_ERR_OR_NULL(ao->mixer))
			log_warning("Failed to initialize hardware mixer for '%s'",
				    ao->name);
	} else
		log_warning("Output type %s doesn't support hardware mixer\n",
			ao->plugin->name);

	/* use the hardware mixer for replay gain? */

	if (strcmp(replay_gain_handler, "mixer") == 0) {
		if (ao->mixer != NULL)
			replay_gain_filter_set_mixer(ao->replay_gain_filter,
						     ao->mixer, 100);
		else
			log_warning("No such mixer for output '%s'", ao->name);
	} else if (strcmp(replay_gain_handler, "software") != 0 &&
		   ao->replay_gain_filter != NULL) {
		log_err("Invalid \"replay_gain_handler\" value");
		return -MPD_INVAL;
	}

	/* the "convert" filter must be the last one in the chain */

	ao->convert_filter = filter_new(&convert_filter_plugin, NULL);
	assert(!IS_ERR_OR_NULL(ao->convert_filter));

	filter_chain_append(ao->filter, ao->convert_filter);

	return MPD_SUCCESS;
}

struct audio_output *
audio_output_new(const struct config_param *param,
		 struct player_control *pc)
{
	const struct audio_output_plugin *plugin;

	if (param) {
		const char *p;

		p = config_get_block_string(param, AUDIO_OUTPUT_TYPE, NULL);
		if (p == NULL) {
			log_err("Missing \"type\" configuration");
			return ERR_PTR(-MPD_INVAL);
		}

		plugin = audio_output_plugin_get(p);
		if (IS_ERR_OR_NULL(plugin)) {
			log_err("No such audio output plugin: %s", p);
			return ERR_PTR(-MPD_NOENT);
		}
	} else {
		log_warning("No \"%s\" defined in config file\n",
			  CONF_AUDIO_OUTPUT);

		plugin = audio_output_detect();
		if (IS_ERR_OR_NULL(plugin))
			return false;

		log_info("Successfully detected a %s audio device",
			  plugin->name);
	}

	struct audio_output *ao = ao_plugin_init(plugin, param);
	if (IS_ERR_OR_NULL(ao))
		return ao;

	int ret = audio_output_setup(ao, param);
	if (ret != MPD_SUCCESS) {
		ao_plugin_finish(ao);
		return ERR_PTR(ret);
	}

	ao->player_control = pc;
	return ao;
}
