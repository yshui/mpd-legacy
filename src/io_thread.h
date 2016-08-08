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

#include <glib.h>
#include <stdbool.h>

void
io_thread_init(void);

int
io_thread_start(void);

/**
 * Run the I/O event loop synchronously in the current thread.  This
 * can be called instead of io_thread_start().  For testing purposes
 * only.
 */
void
io_thread_run(void);

/**
 * Ask the I/O thread to quit, but does not wait for it.  Usually, you
 * don't need to call this function, because io_thread_deinit()
 * includes this.
 */
void
io_thread_quit(void);

void
io_thread_deinit(void);

MPD_PURE
GMainContext *
io_thread_context(void);

/**
 * Is the current thread the I/O thread?
 */
MPD_PURE
bool
io_thread_inside(void);

guint
io_thread_idle_add(GSourceFunc function, gpointer data);

MPD_MALLOC
GSource *
io_thread_timeout_add(guint interval_ms, GSourceFunc function, gpointer data);

MPD_MALLOC
GSource *
io_thread_timeout_add_seconds(guint interval,
			      GSourceFunc function, gpointer data);

/**
 * Call a function synchronously in the I/O thread.
 */
gpointer
io_thread_call(GThreadFunc function, gpointer data);
