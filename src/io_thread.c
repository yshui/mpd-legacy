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

#include "io_thread.h"
#include "err.h"
#include "c11thread.h"

#include <assert.h>

static struct {
	mtx_t mutex;
	cnd_t cond;

	GMainContext *context;
	GMainLoop *loop;
	thrd_t thread;
} io;

void
io_thread_run(void)
{
	assert(io_thread_inside());
	assert(io.context != NULL);
	assert(io.loop != NULL);

	g_main_loop_run(io.loop);
}

static int
io_thread_func(void *arg)
{
	/* lock+unlock to synchronize with io_thread_start(), to be
	   sure that io.thread is set */
	io_thread_run();
	return 0;
}

void
io_thread_init(void)
{
	assert(io.context == NULL);
	assert(io.loop == NULL);

	mtx_init(&io.mutex, mtx_plain);
	cnd_init(&io.cond);
	io.context = g_main_context_new();
	io.loop = g_main_loop_new(io.context, false);
}

int
io_thread_start(void)
{
	assert(io.context != NULL);
	assert(io.loop != NULL);

	int ret = thrd_create(&io.thread, io_thread_func, NULL);
	if (ret != thrd_success)
		return -MPD_UNKNOWN;

	return MPD_SUCCESS;
}

void
io_thread_quit(void)
{
	assert(io.loop != NULL);

	g_main_loop_quit(io.loop);
}

void
io_thread_deinit(void)
{
	io_thread_quit();

	thrd_join(io.thread, NULL);

	if (io.loop != NULL)
		g_main_loop_unref(io.loop);

	if (io.context != NULL)
		g_main_context_unref(io.context);

	cnd_destroy(&io.cond);
	mtx_destroy(&io.mutex);
}

GMainContext *
io_thread_context(void)
{
	return io.context;
}

bool
io_thread_inside(void)
{
	return thrd_current() == io.thread;
}

guint
io_thread_idle_add(GSourceFunc function, gpointer data)
{
	GSource *source = g_idle_source_new();
	g_source_set_callback(source, function, data, NULL);
	guint id = g_source_attach(source, io.context);
	g_source_unref(source);
	return id;
}

GSource *
io_thread_timeout_add(guint interval_ms, GSourceFunc function, gpointer data)
{
	GSource *source = g_timeout_source_new(interval_ms);
	g_source_set_callback(source, function, data, NULL);
	g_source_attach(source, io.context);
	return source;
}

GSource *
io_thread_timeout_add_seconds(guint interval,
			      GSourceFunc function, gpointer data)
{
	GSource *source = g_timeout_source_new_seconds(interval);
	g_source_set_callback(source, function, data, NULL);
	g_source_attach(source, io.context);
	return source;
}

struct call_data {
	GThreadFunc function;
	gpointer data;
	bool done;
	gpointer result;
};

static gboolean
io_thread_call_func(gpointer _data)
{
	struct call_data *data = _data;

	gpointer result = data->function(data->data);

	data->done = true;
	data->result = result;
	cnd_broadcast(&io.cond);

	return false;
}

gpointer
io_thread_call(GThreadFunc function, gpointer _data)
{
	if (io_thread_inside())
		/* we're already in the I/O thread - no
		   synchronization needed */
		return function(_data);

	struct call_data data = {
		.function = function,
		.data = _data,
		.done = false,
	};

	io_thread_idle_add(io_thread_call_func, &data);

	mtx_lock(&io.mutex);
	while (!data.done)
		cnd_wait(&io.cond, &io.mutex);
	mtx_unlock(&io.mutex);

	return data.result;
}
