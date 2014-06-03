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

#define LOG_DOMAIN "input"

#include "log.h"
#include "config.h"
#include "input_init.h"
#include "input_plugin.h"
#include "input_registry.h"
#include "conf.h"

#include <assert.h>
#include <string.h>

/**
 * Find the "input" configuration block for the specified plugin.
 *
 * @param plugin_name the name of the input plugin
 * @return the configuration block, or NULL if none was configured
 */
static const struct config_param *
input_plugin_config(const char *plugin_name)
{
	const struct config_param *param = NULL;

	while ((param = config_get_next_param(CONF_INPUT, param)) != NULL) {
		const char *name =
			config_get_block_string(param, "plugin", NULL);
		if (name == NULL) {
			log_err("input configuration without 'plugin' name in line %d",
				    param->line);
			return ERR_PTR(-MPD_MISS_VALUE);
		}

		if (strcmp(name, plugin_name) == 0)
			return param;
	}

	return NULL;
}

int
input_stream_global_init(void)
{
	for (unsigned i = 0; input_plugins[i] != NULL; ++i) {
		const struct input_plugin *plugin = input_plugins[i];

		assert(plugin->name != NULL);
		assert(*plugin->name != 0);
		assert(plugin->open != NULL);

		const struct config_param *param =
			input_plugin_config(plugin->name);
		if (IS_ERR(param))
			return PTR_ERR(param);

		if (!config_get_block_bool(param, "enabled", true))
			/* the plugin is disabled in mpd.conf */
			continue;

		if (plugin->init == NULL)
			input_plugins_enabled[i] = true;
		else {
			int ret = plugin->init(param);
			if (ret != MPD_SUCCESS) {
				log_err("Failed to initialize input plugin '%s': ",
					   plugin->name);
				return ret;
			}
		}
	}

	return MPD_SUCCESS;
}

void input_stream_global_finish(void)
{
	int i;
	for(i = 0; input_plugins[i]; i++) {
		if (input_plugins_enabled[i] &&
		    input_plugins[i]->finish != NULL)
			input_plugins[i]->finish();
	}
}
