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

#ifdef HAVE_STRUCT_UCRED
#define _GNU_SOURCE 1
#endif

#define LOG_DOMAIN "server_socket"

#include "log.h"
#include "server_socket.h"
#include "socket_util.h"
#include "resolver.h"
#include "fd_util.h"
#include "glib_socket.h"
#include "macros.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#ifdef WIN32
#include <ws2tcpip.h>
#include <winsock.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#endif

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "listen"

#define DEFAULT_PORT	6600

struct one_socket {
	struct one_socket *next;
	struct server_socket *parent;

	unsigned serial;

	int fd;
	guint source_id;

	char *path;

	size_t address_length;
	struct sockaddr address;
};

struct server_socket {
	server_socket_callback_t callback;
	void *callback_ctx;

	struct one_socket *sockets, **sockets_tail_r;
	unsigned next_serial;
};

struct server_socket *
server_socket_new(server_socket_callback_t callback, void *callback_ctx)
{
	struct server_socket *ss = tmalloc(struct server_socket, 1);
	ss->callback = callback;
	ss->callback_ctx = callback_ctx;
	ss->sockets = NULL;
	ss->sockets_tail_r = &ss->sockets;
	ss->next_serial = 1;
	return ss;
}

void
server_socket_free(struct server_socket *ss)
{
	server_socket_close(ss);

	while (ss->sockets != NULL) {
		struct one_socket *s = ss->sockets;
		ss->sockets = s->next;

		assert(s->fd < 0);

		free(s->path);
		free(s);
	}

	free(ss);
}

/**
 * Wraper for sockaddr_to_string() which never fails.
 */
static char *
one_socket_to_string(const struct one_socket *s)
{
	char *p = sockaddr_to_string(&s->address, s->address_length);
	if (p == NULL)
		p = strdup("[unknown]");
	return p;
}

static int
get_remote_uid(int fd)
{
#ifdef HAVE_STRUCT_UCRED
	struct ucred cred;
	socklen_t len = sizeof (cred);

	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
		return 0;

	return cred.uid;
#else
#ifdef HAVE_GETPEEREID
	uid_t euid;
	gid_t egid;

	if (getpeereid(fd, &euid, &egid) == 0)
		return euid;
#else
	(void)fd;
#endif
	return -1;
#endif
}

static gboolean
server_socket_in_event(GIOChannel *source,
		       GIOCondition condition,
		       gpointer data)
{
	struct one_socket *s = data;

	struct sockaddr_storage address;
	size_t address_length = sizeof(address);
	int fd = accept_cloexec_nonblock(s->fd, (struct sockaddr*)&address,
					 &address_length);
	if (fd >= 0) {
		if (socket_keepalive(fd))
			log_warning("Could not set TCP keepalive option: %s",
				  strerror(errno));
		s->parent->callback(fd, (const struct sockaddr*)&address,
				    address_length, get_remote_uid(fd),
				    s->parent->callback_ctx);
	} else {
		log_warning("accept() failed: %s", strerror(errno));
	}

	return true;
}

static void
set_fd(struct one_socket *s, int fd)
{
	assert(s != NULL);
	assert(s->fd < 0);
	assert(fd >= 0);

	s->fd = fd;

	GIOChannel *channel = g_io_channel_new_socket(s->fd);
	s->source_id = g_io_add_watch(channel, G_IO_IN,
				      server_socket_in_event, s);
	g_io_channel_unref(channel);
}

int
server_socket_open(struct server_socket *ss)
{
	struct one_socket *good = NULL;

	for (struct one_socket *s = ss->sockets; s != NULL; s = s->next) {
		assert(s->serial > 0);
		assert(good == NULL || s->serial >= good->serial);
		assert(s->fd < 0);

		int fd = socket_bind_listen(s->address.sa_family,
					    SOCK_STREAM, 0,
					    &s->address, s->address_length, 5);
		if (fd < 0) {
			if (good != NULL && good->serial == s->serial) {
				char *address_string = one_socket_to_string(s);
				char *good_string = one_socket_to_string(good);
				log_warning("bind to '%s' failed: %s "
					  "(continuing anyway, because "
					  "binding to '%s' succeeded)",
					  address_string, strerror(-fd),
					  good_string);
				free(address_string);
				free(good_string);
			} else  {
				char *address_string = one_socket_to_string(s);
				log_err("Failed to bind to '%s', errno %s", address_string, strerror(-fd));
				free(address_string);
			}
			continue;
		}

		/* allow everybody to connect */

		if (s->path != NULL)
			chmod(s->path, 0666);

		/* register in the GLib main loop */

		set_fd(s, fd);

		/* mark this socket as "good" */

		good = s;
	}

	if (good == NULL) {
		server_socket_close(ss);
		return -MPD_ACCESS;
	}

	return MPD_SUCCESS;
}

void
server_socket_close(struct server_socket *ss)
{
	for (struct one_socket *s = ss->sockets; s != NULL; s = s->next) {
		if (s->fd < 0)
			continue;

		g_source_remove(s->source_id);
		close_socket(s->fd);
		s->fd = -1;
	}
}

static struct one_socket *
one_socket_new(unsigned serial, const struct sockaddr *address,
	       size_t address_length)
{
	assert(address != NULL);
	assert(address_length > 0);

	struct one_socket *s = malloc(sizeof(*s) - sizeof(s->address) +
					address_length);
	s->next = NULL;
	s->serial = serial;
	s->fd = -1;
	s->path = NULL;
	s->address_length = address_length;
	memcpy(&s->address, address, address_length);

	return s;
}

int
server_socket_add_fd(struct server_socket *ss, int fd)
{
	assert(ss != NULL);
	assert(ss->sockets_tail_r != NULL);
	assert(*ss->sockets_tail_r == NULL);
	assert(fd >= 0);

	struct sockaddr_storage address;
	socklen_t address_length;
	if (getsockname(fd, (struct sockaddr *)&address,
			&address_length) < 0) {
		log_err("Failed to get socket address: %s",
			    strerror(errno));
		return -MPD_ACCESS;
	}

	struct one_socket *s = one_socket_new(ss->next_serial,
					      (struct sockaddr *)&address,
					      address_length);
	s->parent = ss;
	*ss->sockets_tail_r = s;
	ss->sockets_tail_r = &s->next;

	set_fd(s, fd);

	return MPD_SUCCESS;
}

static struct one_socket *
server_socket_add_address(struct server_socket *ss,
			  const struct sockaddr *address,
			  size_t address_length)
{
	assert(ss != NULL);
	assert(ss->sockets_tail_r != NULL);
	assert(*ss->sockets_tail_r == NULL);

	struct one_socket *s = one_socket_new(ss->next_serial,
					      address, address_length);
	s->parent = ss;
	*ss->sockets_tail_r = s;
	ss->sockets_tail_r = &s->next;

	return s;
}


/**
 * Add a listener on a port on all IPv4 interfaces.
 *
 * @param port the TCP port
 */
static void
server_socket_add_port_ipv4(struct server_socket *ss, unsigned port)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	server_socket_add_address(ss, (const struct sockaddr *)&sin,
				  sizeof(sin));
}

#ifdef AF_INET6
/**
 * Add a listener on a port on all IPv6 interfaces.
 *
 * @param port the TCP port
 */
static void
server_socket_add_port_ipv6(struct server_socket *ss, unsigned port)
{
	struct sockaddr_in6 sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin6_port = htons(port);
	sin.sin6_family = AF_INET6;

	server_socket_add_address(ss, (const struct sockaddr *)&sin,
				  sizeof(sin));
}
#endif /* AF_INET6 */

int
server_socket_add_port(struct server_socket *ss, unsigned port)
{
	if (port == 0 || port > 0xffff) {
		log_err("Invalid TCP port");
		return -MPD_ACCESS;
	}

#ifdef AF_INET6
	server_socket_add_port_ipv6(ss, port);
#endif
	server_socket_add_port_ipv4(ss, port);

	++ss->next_serial;

	return MPD_SUCCESS;
}

int
server_socket_add_host(struct server_socket *ss, const char *hostname,
		       unsigned port)
{
	struct addrinfo *ai = resolve_host_port(hostname, port,
						AI_PASSIVE, SOCK_STREAM);
	if (IS_ERR(ai))
		return PTR_ERR(ai);

	for (const struct addrinfo *i = ai; i != NULL; i = i->ai_next)
		server_socket_add_address(ss, i->ai_addr, i->ai_addrlen);

	freeaddrinfo(ai);

	++ss->next_serial;

	return MPD_SUCCESS;
}

int
server_socket_add_path(struct server_socket *ss, const char *path)
{
#ifdef AF_UNIX
	struct sockaddr_un s_un;

	size_t path_length = strlen(path);
	if (path_length >= sizeof(s_un.sun_path)) {
		log_err("UNIX socket path is too long");
		return -MPD_INVAL;
	}

	unlink(path);

	s_un.sun_family = AF_UNIX;
	memcpy(s_un.sun_path, path, path_length + 1);

	struct one_socket *s =
		server_socket_add_address(ss, (const struct sockaddr *)&s_un,
					  sizeof(s_un));
	s->path = strdup(path);

	return MPD_SUCCESS;
#else
	(void)ss;
	(void)path;

	log_warning("UNIX domain socket support is disabled");
	return -MPD_INVAL;
#endif
}

