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

#pragma once

#include "compiler.h"

#include <stdlib.h>

struct sockaddr;
struct addrinfo;

/**
 * Converts the specified socket address into a string in the form
 * "IP:PORT".  The return value must be freed with free() when you
 * don't need it anymore.
 *
 * @param sa the sockaddr struct
 * @param length the length of #sa in bytes
 */
MPD_MALLOC
char *
sockaddr_to_string(const struct sockaddr *sa, size_t length);

/**
 * Resolve a specification in the form "host", "host:port",
 * "[host]:port".  This is a convenience wrapper for getaddrinfo().
 *
 * @param default_port a default port number that will be used if none
 * is given in the string (if applicable); pass 0 to go without a
 * default
 * @return an #addrinfo linked list that must be freed with
 * freeaddrinfo(), or error code
 */
struct addrinfo *
resolve_host_port(const char *host_port, unsigned default_port,
		  int flags, int socktype);
