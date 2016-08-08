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
#include "playlist/lastfm.h"
#include "playlist_plugin.h"
#include "playlist_list.h"
#include "conf.h"
#include "uri.h"
#include "song.h"
#include "input_stream.h"

#include <glib.h>

#include <assert.h>
#include <string.h>

struct lastfm_playlist {
	struct playlist_provider base;

	struct input_stream *is;

	struct playlist_provider *xspf;
};

static struct {
	char *user;
	char *md5;
} lastfm_config;

static bool
lastfm_init(const struct config_param *param)
{
	const char *user = config_get_block_string(param, "user", NULL);
	const char *passwd = config_get_block_string(param, "password", NULL);

	if (user == NULL || passwd == NULL) {
		log_debug("disabling the last.fm playlist plugin "
			"because account is not configured");
		return false;
	}

	lastfm_config.user = g_uri_escape_string(user, NULL, false);

	if (strlen(passwd) != 32)
		lastfm_config.md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5,
								  passwd, strlen(passwd));
	else
		lastfm_config.md5 = g_strdup(passwd);

	return true;
}

static void
lastfm_finish(void)
{
	g_free(lastfm_config.user);
	g_free(lastfm_config.md5);
}

/**
 * Simple data fetcher.
 * @param url path or url of data to fetch.
 * @return data fetched, or NULL on error. Must be freed with g_free.
 */
static char *
lastfm_get(const char *url)
{
	struct input_stream *input_stream;
	char buffer[4096];
	size_t length = 0;
	ssize_t nbytes;

	input_stream = input_stream_open(url);
	if (IS_ERR(input_stream))
		return (char *)input_stream;

	mtx_lock(&input_stream->mutex);

	input_stream_wait_ready(input_stream);

	do {
		nbytes = input_stream_read(input_stream, buffer + length,
					   sizeof(buffer) - length);
		if (nbytes <= 0) {
			if (input_stream_eof(input_stream))
				break;

			/* I/O error */
			mtx_unlock(&input_stream->mutex);
			input_stream_close(input_stream);
			return ERR_PTR(-MPD_ACCESS);
		}

		length += nbytes;
	} while (length < sizeof(buffer));

	mtx_unlock(&input_stream->mutex);

	input_stream_close(input_stream);
	return g_strndup(buffer, length);
}

/**
 * Ini-style value fetcher.
 * @param response data through which to search.
 * @param name name of value to search for.
 * @return value for param name in param response or NULL on error. Free with free.
 */
static char *
lastfm_find(const char *response, const char *name)
{
	size_t name_length = strlen(name);

	while (true) {
		const char *eol = strchr(response, '\n');
		if (eol == NULL)
			return NULL;

		if (strncmp(response, name, name_length) == 0 &&
		    response[name_length] == '=') {
			response += name_length + 1;
			return g_strndup(response, eol - response);
		}

		response = eol + 1;
	}
}

static struct playlist_provider *
lastfm_open_uri(const char *uri)
{
	struct lastfm_playlist *playlist;
	char *p, *q, *response, *session;

	/* handshake */

	p = g_strconcat("http://ws.audioscrobbler.com/radio/handshake.php?"
			"version=1.1.1&platform=linux&"
			"username=", lastfm_config.user, "&"
			"passwordmd5=", lastfm_config.md5, "&"
			"debug=0&partner=", NULL);
	response = lastfm_get(p);
	g_free(p);
	if (response == NULL)
		return NULL;

	/* extract session id from response */

	session = lastfm_find(response, "session");
	g_free(response);
	if (session == NULL) {
		log_warning("last.fm handshake failed");
		return NULL;
	}

	q = g_uri_escape_string(session, NULL, false);
	g_free(session);
	session = q;

	log_debug("session='%s'", session);

	/* "adjust" last.fm radio */

	if (strlen(uri) > 9) {
		char *escaped_uri;

		escaped_uri = g_uri_escape_string(uri, NULL, false);

		p = g_strconcat("http://ws.audioscrobbler.com/radio/adjust.php?"
				"session=", session, "&url=", escaped_uri, "&debug=0",
				NULL);
		g_free(escaped_uri);

		response = lastfm_get(p);
		g_free(response);
		g_free(p);

		if (response == NULL) {
			g_free(session);
			return NULL;
		}
	}

	/* create the playlist object */

	playlist = g_new(struct lastfm_playlist, 1);
	playlist_provider_init(&playlist->base, &lastfm_playlist_plugin);

	/* open the last.fm playlist */

	p = g_strconcat("http://ws.audioscrobbler.com/radio/xspf.php?"
			"sk=", session, "&discovery=0&desktop=1.5.1.31879",
			NULL);
	g_free(session);

	playlist->is = input_stream_open(p);
	g_free(p);

	if (IS_ERR(playlist->is)) {
		log_warning("Failed to load XSPF playlist");
		free(playlist);
		return (void *)playlist->is;
	}

	mtx_lock(&playlist->is->mutex);

	input_stream_wait_ready(playlist->is);

	/* last.fm does not send a MIME type, we have to fake it here
	   :-( */
	free(playlist->is->mime);
	playlist->is->mime = g_strdup("application/xspf+xml");

	mtx_unlock(&playlist->is->mutex);

	/* parse the XSPF playlist */

	playlist->xspf = playlist_list_open_stream(playlist->is, NULL);
	if (playlist->xspf == NULL) {
		input_stream_close(playlist->is);
		g_free(playlist);
		log_warning("Failed to parse XSPF playlist");
		return NULL;
	}

	return &playlist->base;
}

static void
lastfm_close(struct playlist_provider *_playlist)
{
	struct lastfm_playlist *playlist = (struct lastfm_playlist *)_playlist;

	playlist_plugin_close(playlist->xspf);
	input_stream_close(playlist->is);
	g_free(playlist);
}

static struct song *
lastfm_read(struct playlist_provider *_playlist)
{
	struct lastfm_playlist *playlist = (struct lastfm_playlist *)_playlist;

	return playlist_plugin_read(playlist->xspf);
}

static const char *const lastfm_schemes[] = {
	"lastfm",
	NULL
};

const struct playlist_plugin lastfm_playlist_plugin = {
	.name = "lastfm",

	.init = lastfm_init,
	.finish = lastfm_finish,
	.open_uri = lastfm_open_uri,
	.close = lastfm_close,
	.read = lastfm_read,

	.schemes = lastfm_schemes,
};
