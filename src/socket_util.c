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

#define LOG_DOMAIN "socket"

#include "log.h"
#include "config.h"
#include "socket_util.h"
#include "fd_util.h"

#include <errno.h>
#include <unistd.h>

#ifndef G_OS_WIN32
#include <sys/socket.h>
#else /* G_OS_WIN32 */
#include <ws2tcpip.h>
#include <winsock.h>
#endif /* G_OS_WIN32 */

#ifdef HAVE_IPV6
#include <string.h>
#endif

int
socket_bind_listen(int domain, int type, int protocol,
		   const struct sockaddr *address, size_t address_length,
		   int backlog)
{
	int fd, ret;
	const int reuse = 1;

	fd = socket_cloexec_nonblock(domain, type, protocol);
	if (fd < 0) {
		log_err("Failed to create socket: %s", strerror(errno));
		return -errno;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			 (const char *) &reuse, sizeof(reuse));
	if (ret < 0) {
		log_err("setsockopt() failed: %s", strerror(errno));
		close_socket(fd);
		return -errno;
	}

	ret = bind(fd, address, address_length);
	if (ret < 0) {
		log_err("%s", strerror(errno));
		close_socket(fd);
		return -errno;
	}

	ret = listen(fd, backlog);
	if (ret < 0) {
		log_err("listen() failed: %s", strerror(errno));
		close_socket(fd);
		return -errno;
	}

#ifdef HAVE_STRUCT_UCRED
	setsockopt(fd, SOL_SOCKET, SO_PASSCRED,
		   (const char *) &reuse, sizeof(reuse));
#endif

	return fd;
}

int
socket_keepalive(int fd)
{
	const int reuse = 1;

	return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
			  (const char *)&reuse, sizeof(reuse));
}
