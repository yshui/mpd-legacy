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

#include <stddef.h>
#include <string.h>

#include "output_plugin.h"
#include "output_list.h"
#include "config.h"

extern const struct audio_output_plugin shout_output_plugin;
extern const struct audio_output_plugin null_output_plugin;
extern const struct audio_output_plugin fifo_output_plugin;
extern const struct audio_output_plugin alsa_output_plugin;
extern const struct audio_output_plugin roar_output_plugin;
extern const struct audio_output_plugin ao_output_plugin;
extern const struct audio_output_plugin oss_output_plugin;
extern const struct audio_output_plugin openal_output_plugin;
extern const struct audio_output_plugin osx_output_plugin;
extern const struct audio_output_plugin solaris_output_plugin;
extern const struct audio_output_plugin pulse_output_plugin;
extern const struct audio_output_plugin mvp_output_plugin;
extern const struct audio_output_plugin jack_output_plugin;
extern const struct audio_output_plugin httpd_output_plugin;
extern const struct audio_output_plugin winmm_output_plugin;
extern const struct audio_output_plugin recorder_output_plugin;
extern const struct audio_output_plugin ffado_output_plugin;
extern const struct audio_output_plugin pipe_output_plugin;

const struct audio_output_plugin *const audio_output_plugins[] = {
#ifdef ENABLE_SHOUT
	&shout_output_plugin,
#endif
	&null_output_plugin,
#ifdef HAVE_FIFO
	&fifo_output_plugin,
#endif
	&pipe_output_plugin,
#ifdef ENABLE_ALSA
	&alsa_output_plugin,
#endif
#ifdef ENABLE_ROAR
	&roar_output_plugin,
#endif
#ifdef ENABLE_AO
	&ao_output_plugin,
#endif
#ifdef ENABLE_OSS
	&oss_output_plugin,
#endif
#ifdef ENABLE_OPENAL
	&openal_output_plugin,
#endif
#ifdef ENABLE_OSX
	&osx_output_plugin,
#endif
#ifdef ENABLE_SOLARIS
	&solaris_output_plugin,
#endif
#ifdef ENABLE_PULSE
	&pulse_output_plugin,
#endif
	&mvp_output_plugin,
#ifdef ENABLE_JACK
	&jack_output_plugin,
#endif
	&httpd_output_plugin,
	&recorder_output_plugin,
#ifdef ENABLE_WINMM
	&winmm_output_plugin,
#endif
#ifdef ENABLE_FFADO
	&ffado_output_plugin,
#endif
	NULL
};

const struct audio_output_plugin *
audio_output_plugin_get(const char *name)
{
	audio_output_plugins_for_each(plugin)
		if (strcmp(plugin->name, name) == 0)
			return plugin;

	return NULL;
}
