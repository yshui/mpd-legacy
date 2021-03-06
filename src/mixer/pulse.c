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
#include "pulse.h"
#include "mixer_api.h"
#include "output/pulse.h"
#include "conf.h"
#include "event_pipe.h"

#include <glib.h>

#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/stream.h>
#include <pulse/subscribe.h>
#include <pulse/error.h>

#include <assert.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "pulse_mixer"

struct pulse_mixer {
	struct mixer base;

	struct pulse_output *output;

	bool online;
	struct pa_cvolume volume;

};

static void
pulse_mixer_offline(struct pulse_mixer *pm)
{
	if (!pm->online)
		return;

	pm->online = false;

	event_pipe_emit(PIPE_EVENT_MIXER);
}

/**
 * Callback invoked by pulse_mixer_update().  Receives the new mixer
 * value.
 */
static void
pulse_mixer_volume_cb(pa_context *context, const pa_sink_input_info *i,
		      int eol, void *userdata)
{
	struct pulse_mixer *pm = userdata;

	if (eol)
		return;

	if (i == NULL) {
		pulse_mixer_offline(pm);
		return;
	}

	pm->online = true;
	pm->volume = i->volume;

	event_pipe_emit(PIPE_EVENT_MIXER);
}

static void
pulse_mixer_update(struct pulse_mixer *pm,
		   struct pa_context *context, struct pa_stream *stream)
{
	pa_operation *o;

	assert(context != NULL);
	assert(stream != NULL);
	assert(pa_stream_get_state(stream) == PA_STREAM_READY);

	o = pa_context_get_sink_input_info(context,
					   pa_stream_get_index(stream),
					   pulse_mixer_volume_cb, pm);
	if (o == NULL) {
		log_warning("pa_context_get_sink_input_info() failed: %s",
			  pa_strerror(pa_context_errno(context)));
		pulse_mixer_offline(pm);
		return;
	}

	pa_operation_unref(o);
}

void
pulse_mixer_on_connect(struct pulse_mixer *pm,
		       struct pa_context *context)
{
	pa_operation *o;

	assert(context != NULL);

	o = pa_context_subscribe(context,
				 (pa_subscription_mask_t)PA_SUBSCRIPTION_MASK_SINK_INPUT,
				 NULL, NULL);
	if (o == NULL) {
		log_warning("pa_context_subscribe() failed: %s",
			  pa_strerror(pa_context_errno(context)));
		return;
	}

	pa_operation_unref(o);
}

void
pulse_mixer_on_disconnect(struct pulse_mixer *pm)
{
	pulse_mixer_offline(pm);
}

void
pulse_mixer_on_change(struct pulse_mixer *pm,
		      struct pa_context *context, struct pa_stream *stream)
{
	pulse_mixer_update(pm, context, stream);
}

static struct mixer *
pulse_mixer_init(void *ao, const struct config_param *param)
{
	struct pulse_mixer *pm;
	struct pulse_output *po = ao;

	if (ao == NULL) {
		log_err("The pulse mixer cannot work without the audio output");
		return ERR_PTR(-MPD_INVAL);
	}

	pm = g_new(struct pulse_mixer,1);
	mixer_init(&pm->base, &pulse_mixer_plugin);

	pm->online = false;
	pm->output = po;

	pulse_output_set_mixer(po, pm);

	return &pm->base;
}

static void
pulse_mixer_finish(struct mixer *data)
{
	struct pulse_mixer *pm = (struct pulse_mixer *) data;

	pulse_output_clear_mixer(pm->output, pm);

	/* free resources */

	g_free(pm);
}

static int
pulse_mixer_get_volume(struct mixer *mixer)
{
	struct pulse_mixer *pm = (struct pulse_mixer *) mixer;
	int ret;

	pulse_output_lock(pm->output);

	ret = pm->online
		? (int)((100*(pa_cvolume_avg(&pm->volume)+1))/PA_VOLUME_NORM)
		: -MPD_DISABLED;

	pulse_output_unlock(pm->output);

	return ret;
}

static int
pulse_mixer_set_volume(struct mixer *mixer, unsigned volume)
{
	struct pulse_mixer *pm = (struct pulse_mixer *) mixer;
	struct pa_cvolume cvolume;

	pulse_output_lock(pm->output);

	if (!pm->online) {
		pulse_output_unlock(pm->output);
		log_warning("Disconnected from pulse.");
		return -MPD_3RD;
	}

	pa_cvolume_set(&cvolume, pm->volume.channels,
		       (pa_volume_t)volume * PA_VOLUME_NORM / 100 + 0.5);
	int ret = pulse_output_set_volume(pm->output, &cvolume);
	if (ret == MPD_SUCCESS)
		pm->volume = cvolume;

	pulse_output_unlock(pm->output);

	return ret;
}

const struct mixer_plugin pulse_mixer_plugin = {
	.init = pulse_mixer_init,
	.finish = pulse_mixer_finish,
	.get_volume = pulse_mixer_get_volume,
	.set_volume = pulse_mixer_set_volume,
};
