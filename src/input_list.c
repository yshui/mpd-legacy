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
#include "input_list.h"
#include "input/file.h"

#include <glib.h>

extern const struct input_plugin input_plugin_file;
extern const struct input_plugin input_plugin_archive;
extern const struct input_plugin input_plugin_curl;
extern const struct input_plugin input_plugin_ffmpeg;
extern const struct input_plugin input_plugin_mms;
extern const struct input_plugin input_plugin_cdio_paranoia;
extern const struct input_plugin input_plugin_despotify;

const struct input_plugin *const input_plugins[] = {
	&input_plugin_file,
#ifdef ENABLE_ARCHIVE
	&input_plugin_archive,
#endif
#ifdef ENABLE_CURL
	&input_plugin_curl,
#endif
#ifdef ENABLE_FFMPEG
	&input_plugin_ffmpeg,
#endif
#ifdef ENABLE_MMS
	&input_plugin_mms,
#endif
#ifdef ENABLE_CDIO_PARANOIA
	&input_plugin_cdio_paranoia,
#endif
#ifdef ENABLE_DESPOTIFY
	&input_plugin_despotify,
#endif
	NULL
};

bool input_plugins_enabled[G_N_ELEMENTS(input_plugins) - 1];
