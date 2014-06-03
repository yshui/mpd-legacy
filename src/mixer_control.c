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

#define LOG_DOMAIN "mixer_control"

#include "config.h"
#include "log.h"
#include "mixer_control.h"
#include "mixer_api.h"

#include <assert.h>
#include <stddef.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "mixer"

struct mixer *
mixer_new(const struct mixer_plugin *plugin, void *ao,
	  const struct config_param *param)
{
	struct mixer *mixer;

	assert(plugin != NULL);

	mixer = plugin->init(ao, param);

	assert(mixer == NULL || mixer->plugin == plugin);

	return mixer;
}

void
mixer_free(struct mixer *mixer)
{
	assert(mixer != NULL);
	assert(mixer->plugin != NULL);
	assert(mixer->mutex != NULL);

	/* mixers with the "global" flag set might still be open at
	   this point (see mixer_auto_close()) */
	mixer_close(mixer);

	g_mutex_free(mixer->mutex);

	mixer->plugin->finish(mixer);
}

int
mixer_open(struct mixer *mixer)
{
	int ret;

	assert(mixer != NULL);
	assert(mixer->plugin != NULL);

	g_mutex_lock(mixer->mutex);

	if (mixer->open)
		ret = MPD_SUCCESS;
	else if (mixer->plugin->open == NULL){
		mixer->open = true;
		ret = MPD_SUCCESS;
	} else {
		ret = mixer->plugin->open(mixer);
		mixer->open = ret == MPD_SUCCESS;
	}

	mixer->failed = ret != MPD_SUCCESS;

	g_mutex_unlock(mixer->mutex);

	return ret;
}

static void
mixer_close_internal(struct mixer *mixer)
{
	assert(mixer != NULL);
	assert(mixer->plugin != NULL);
	assert(mixer->open);

	if (mixer->plugin->close != NULL)
		mixer->plugin->close(mixer);

	mixer->open = false;
}

void
mixer_close(struct mixer *mixer)
{
	assert(mixer != NULL);
	assert(mixer->plugin != NULL);

	g_mutex_lock(mixer->mutex);

	if (mixer->open)
		mixer_close_internal(mixer);

	g_mutex_unlock(mixer->mutex);
}

void
mixer_auto_close(struct mixer *mixer)
{
	if (!mixer->plugin->global)
		mixer_close(mixer);
}

/*
 * Close the mixer due to failure.  The mutex must be locked before
 * calling this function.
 */
static void
mixer_failed(struct mixer *mixer)
{
	assert(mixer->open);

	mixer_close_internal(mixer);

	mixer->failed = true;
}

int
mixer_get_volume(struct mixer *mixer)
{
	int volume;

	assert(mixer != NULL);

	int ret = mixer_open(mixer);
	if (mixer->plugin->global && !mixer->failed &&
	    ret != MPD_SUCCESS)
		return ret;

	g_mutex_lock(mixer->mutex);

	if (mixer->open) {
		volume = mixer->plugin->get_volume(mixer);
		if (volume < 0) {
			log_err("Failed to get volume.");
			mixer_failed(mixer);
			goto out;
		}
	} else
		volume = -1;

out:
	g_mutex_unlock(mixer->mutex);

	return volume;
}

int
mixer_set_volume(struct mixer *mixer, unsigned volume)
{
	assert(mixer != NULL);
	assert(volume <= 100);

	int ret = mixer_open(mixer);
	if (mixer->plugin->global && !mixer->failed &&
	    ret != MPD_SUCCESS)
		return ret;

	g_mutex_lock(mixer->mutex);

	if (mixer->open) {
		ret = mixer->plugin->set_volume(mixer, volume);
	} else
		ret = -MPD_NIMPL;

	g_mutex_unlock(mixer->mutex);

	return ret;
}
