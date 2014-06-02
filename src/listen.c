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

#define LOG_DOMAIN "listen"

#include "log.h"
#include "config.h"
#include "listen.h"
#include "server_socket.h"
#include "client.h"
#include "conf.h"
#include "main.h"

#include <string.h>
#include <assert.h>

#ifdef ENABLE_SYSTEMD_ACTIVATION
#include <systemd/sd-daemon.h>
#endif


#define DEFAULT_PORT	6600

static struct server_socket *listen_socket;
int listen_port;

static void
listen_callback(int fd, const struct sockaddr *address,
		size_t address_length, int uid, void *ctx)
{
	client_new(global_player_control, fd, address, address_length, uid);
}

static bool
listen_add_config_param(unsigned int port,
			const struct config_param *param)
{
	assert(param != NULL);

	if (0 == strcmp(param->value, "any"))
		return server_socket_add_port(listen_socket, port);
	else if (param->value[0] == '/')
		return server_socket_add_path(listen_socket, param->value);
	else
		return server_socket_add_host(listen_socket, param->value,
					      port);
}

static int
listen_systemd_activation(void)
{
#ifdef ENABLE_SYSTEMD_ACTIVATION
	int n = sd_listen_fds(true);
	if (n <= 0) {
		if (n < 0) {
			log_warning("sd_listen_fds() failed: %s",
				  strerror(-n));
			return -MPD_ACCESS;
		}
		return -MPD_NIMPL;
	}

	for (int i = SD_LISTEN_FDS_START, end = SD_LISTEN_FDS_START + n;
	     i != end; ++i) {
		int ret = server_socket_add_fd(listen_socket, i);
		if (ret != MPD_SUCCESS)
			return ret;
	}

	return MPD_SUCCESS;
#else
	return -MPD_NIMPL;
#endif
}

int
listen_global_init(void)
{
	int port = config_get_positive(CONF_PORT, DEFAULT_PORT);
	const struct config_param *param =
		config_get_next_param(CONF_BIND_TO_ADDRESS, NULL);

	listen_socket = server_socket_new(listen_callback, NULL);

	int ret = listen_systemd_activation();
	if (ret == MPD_SUCCESS)
		return MPD_SUCCESS;

	if (ret != -MPD_NIMPL)
		return ret;

	if (param != NULL) {
		/* "bind_to_address" is configured, create listeners
		   for all values */

		do {
			ret = listen_add_config_param(port, param);
			if (ret != MPD_SUCCESS) {
				log_err("Failed to listen on %s (line %i)",
							   param->value, param->line);
				return ret;
			}

			param = config_get_next_param(CONF_BIND_TO_ADDRESS,
						      param);
		} while (param != NULL);
	} else {
		/* no "bind_to_address" configured, bind the
		   configured port on all interfaces */

		ret = server_socket_add_port(listen_socket, port);
		if (ret != MPD_SUCCESS) {
			log_err("Failed to listen on *:%d", port);
			return ret;
		}
	}

	if ((ret = server_socket_open(listen_socket)) != MPD_SUCCESS)
		return ret;

	listen_port = port;
	return MPD_SUCCESS;
}

void listen_global_finish(void)
{
	log_debug("listen_global_finish called");

	assert(listen_socket != NULL);

	server_socket_free(listen_socket);
}
