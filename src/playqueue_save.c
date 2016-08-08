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
#include "playqueue_save.h"
#include "playqueue.h"
#include "song.h"
#include "uri.h"
#include "database.h"
#include "text_file.h"

#include <stdlib.h>

#define PRIO_LABEL "Prio: "

static void
queue_save_song(FILE *fp, int idx, const struct song *song)
{
	char *uri = song_get_uri(song);

	fprintf(fp, "%i:%s\n", idx, uri);
	free(uri);
}

void
queue_save(FILE *fp, const struct queue *queue)
{
	for (unsigned i = 0; i < queue_length(queue); i++) {
		uint8_t prio = queue_get_priority_at_position(queue, i);
		if (prio != 0)
			fprintf(fp, PRIO_LABEL "%u\n", prio);

		queue_save_song(fp, i, queue_get(queue, i));
	}
}

static struct song *
get_song(const char *uri)
{
	return uri_has_scheme(uri)
		? song_remote_new(uri)
		: db_get_song(uri);
}

void
queue_load_song(FILE *fp, GString *buffer, const char *line,
		struct queue *queue)
{
	struct song *song;

	if (queue_is_full(queue))
		return;

	uint8_t priority = 0;
	if (g_str_has_prefix(line, PRIO_LABEL)) {
		priority = strtoul(line + sizeof(PRIO_LABEL) - 1, NULL, 10);

		line = read_text_line(fp, buffer);
		if (line == NULL)
			return;
	}

	char *endptr;
	long ret = strtol(line, &endptr, 10);
	if (ret < 0 || *endptr != ':' || endptr[1] == 0) {
		log_warning("Malformed playlist line in state file");
		return;
	}

	line = endptr + 1;

	song = get_song(line);
	if (song == NULL)
		return;

	queue_append(queue, song, priority);
}
