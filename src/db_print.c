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
#include "db_print.h"
#include "db_selection.h"
#include "db_visitor.h"
#include "locate.h"
#include "directory.h"
#include "database.h"
#include "client.h"
#include "song.h"
#include "song_print.h"
#include "playlist_vector.h"
#include "tag.h"
#include "strset.h"
#include "macros.h"

#include <glib.h>

typedef struct _ListCommandItem {
	int8_t tagType;
	const struct locate_item_list *criteria;
} ListCommandItem;

typedef struct _SearchStats {
	const struct locate_item_list *criteria;
	int numberOfSongs;
	unsigned long playTime;
} SearchStats;

static int
print_visitor_directory(const struct directory *directory, void *data)
{
	struct client *client = data;

	if (!directory_is_root(directory))
		client_printf(client, "directory: %s\n", directory_get_path(directory));

	return MPD_SUCCESS;
}

static void
print_playlist_in_directory(struct client *client,
			    const struct directory *directory,
			    const char *name_utf8)
{
	if (directory_is_root(directory))
		client_printf(client, "playlist: %s\n", name_utf8);
	else
		client_printf(client, "playlist: %s/%s\n",
			      directory_get_path(directory), name_utf8);
}

static int
print_visitor_song(struct song *song, void *data)
{
	assert(song != NULL);
	assert(song->parent != NULL);

	struct client *client = data;
	song_print_uri(client, song);

	if (song->tag != NULL && song->tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(client, song->parent,
					    song->uri);

	return MPD_SUCCESS;
}

static int
print_visitor_song_info(struct song *song, void *data)
{
	assert(song != NULL);
	assert(song->parent != NULL);

	struct client *client = data;
	song_print_info(client, song);

	if (song->tag != NULL && song->tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(client, song->parent,
					    song->uri);

	return MPD_SUCCESS;
}

static int
print_visitor_playlist(const struct playlist_metadata *playlist,
		       const struct directory *directory, void *ctx)
{
	struct client *client = ctx;
	print_playlist_in_directory(client, directory, playlist->name);
	return MPD_SUCCESS;
}

static int
print_visitor_playlist_info(const struct playlist_metadata *playlist,
			    const struct directory *directory,
			    void *ctx)
{
	struct client *client = ctx;
	print_playlist_in_directory(client, directory, playlist->name);

#ifndef G_OS_WIN32
	struct tm tm;
#endif
	char timestamp[32];
	time_t t = playlist->mtime;
	strftime(timestamp, sizeof(timestamp),
#ifdef G_OS_WIN32
		 "%Y-%m-%dT%H:%M:%SZ",
		 gmtime(&t)
#else
		 "%FT%TZ",
		 gmtime_r(&t, &tm)
#endif
		 );
	client_printf(client, "Last-Modified: %s\n", timestamp);

	return MPD_SUCCESS;
}

static const struct db_visitor print_visitor = {
	.directory = print_visitor_directory,
	.song = print_visitor_song,
	.playlist = print_visitor_playlist,
};

static const struct db_visitor print_info_visitor = {
	.directory = print_visitor_directory,
	.song = print_visitor_song_info,
	.playlist = print_visitor_playlist_info,
};

int
db_selection_print(struct client *client, const struct db_selection *selection,
		   bool full)
{
	return db_visit(selection, full ? &print_info_visitor : &print_visitor,
			client);
}

struct search_data {
	struct client *client;
	const struct locate_item_list *criteria;
};

static int
search_visitor_song(struct song *song, void *_data)
{
	struct search_data *data = _data;

	if (locate_song_search(song, data->criteria))
		song_print_info(data->client, song);

	return MPD_SUCCESS;
}

static const struct db_visitor search_visitor = {
	.song = search_visitor_song,
};

int
searchForSongsIn(struct client *client, const char *name,
		 const struct locate_item_list *criteria)
{
	struct locate_item_list *new_list
		= locate_item_list_casefold(criteria);
	struct search_data data;

	data.client = client;
	data.criteria = new_list;

	int ret = db_walk(name, &search_visitor, &data);

	locate_item_list_free(new_list);

	return ret;
}

static int
find_visitor_song(struct song *song, void *_data)
{
	struct search_data *data = _data;

	if (locate_song_match(song, data->criteria))
		song_print_info(data->client, song);

	return MPD_SUCCESS;
}

static const struct db_visitor find_visitor = {
	.song = find_visitor_song,
};

int
findSongsIn(struct client *client, const char *name,
	    const struct locate_item_list *criteria)
{
	struct search_data data;

	data.client = client;
	data.criteria = criteria;

	return db_walk(name, &find_visitor, &data);
}

static void printSearchStats(struct client *client, SearchStats *stats)
{
	client_printf(client, "songs: %i\n", stats->numberOfSongs);
	client_printf(client, "playtime: %li\n", stats->playTime);
}

static int
stats_visitor_song(struct song *song, void *data)
{
	SearchStats *stats = data;

	if (locate_song_match(song, stats->criteria)) {
		stats->numberOfSongs++;
		stats->playTime += song_get_duration(song);
	}

	return MPD_SUCCESS;
}

static const struct db_visitor stats_visitor = {
	.song = stats_visitor_song,
};

int
searchStatsForSongsIn(struct client *client, const char *name,
		      const struct locate_item_list *criteria)
{
	SearchStats stats;

	stats.criteria = criteria;
	stats.numberOfSongs = 0;
	stats.playTime = 0;

	int ret = db_walk(name, &stats_visitor, &stats);
	if (ret != MPD_SUCCESS)
		return ret;

	printSearchStats(client, &stats);
	return MPD_SUCCESS;
}

int
printAllIn(struct client *client, const char *uri_utf8)
{
	struct db_selection selection;
	db_selection_init(&selection, uri_utf8, true);
	return db_selection_print(client, &selection, false);
}

int
printInfoForAllIn(struct client *client, const char *uri_utf8)
{
	struct db_selection selection;
	db_selection_init(&selection, uri_utf8, true);
	return db_selection_print(client, &selection, true);
}

static ListCommandItem *
newListCommandItem(int tagType, const struct locate_item_list *criteria)
{
	ListCommandItem *item = tmalloc(ListCommandItem, 1);

	item->tagType = tagType;
	item->criteria = criteria;

	return item;
}

static void freeListCommandItem(ListCommandItem * item)
{
	free(item);
}

static void
visitTag(struct client *client, struct strset *set,
	 struct song *song, enum tag_type tagType)
{
	struct tag *tag = song->tag;
	bool found = false;

	if (tagType == LOCATE_TAG_FILE_TYPE) {
		song_print_uri(client, song);
		return;
	}

	if (!tag)
		return;

	for (unsigned i = 0; i < tag->num_items; i++) {
		if (tag->items[i]->type == tagType) {
			strset_add(set, tag->items[i]->value);
			found = true;
		}
	}

	if (!found)
		strset_add(set, "");
}

struct list_tags_data {
	struct client *client;
	ListCommandItem *item;
	struct strset *set;
};

static int
unique_tags_visitor_song(struct song *song, void *_data)
{
	struct list_tags_data *data = _data;
	ListCommandItem *item = data->item;

	if (locate_song_match(song, item->criteria))
		visitTag(data->client, data->set, song, item->tagType);

	return MPD_SUCCESS;
}

static const struct db_visitor unique_tags_visitor = {
	.song = unique_tags_visitor_song,
};

int
listAllUniqueTags(struct client *client, int type,
		  const struct locate_item_list *criteria)
{
	ListCommandItem *item = newListCommandItem(type, criteria);
	struct list_tags_data data = {
		.client = client,
		.item = item,
	};

	if (type >= 0 && type <= TAG_NUM_OF_ITEM_TYPES) {
		data.set = strset_new();
	}

	int ret = db_walk("", &unique_tags_visitor, &data);
	if (ret != MPD_SUCCESS) {
		freeListCommandItem(item);
		return ret;
	}

	if (type >= 0 && type <= TAG_NUM_OF_ITEM_TYPES) {
		const char *value;

		strset_rewind(data.set);

		while ((value = strset_next(data.set)) != NULL)
			client_printf(client, "%s: %s\n",
				      tag_item_names[type],
				      value);

		strset_free(data.set);
	}

	freeListCommandItem(item);

	return MPD_SUCCESS;
}
