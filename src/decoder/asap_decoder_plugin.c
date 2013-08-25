/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com */

#include "config.h"
#include "../decoder_api.h"
#include "audio_check.h"
#include "uri.h"
#include "tag_handler.h"

#include <sys/stat.h>
#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>
#include <glib.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <asap.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "asap"
#define ASAP_BUFFER_LEN 4096

#define SUBTUNE_PREFIX "tune_"
/**
 * returns the file path stripped of any /tune_xxx.* subtune
 * suffix
 */
static char *
get_container_name(const char *path_fs)
{
	const char *subtune_suffix = uri_get_suffix(path_fs);
	char *path_container = g_strdup(path_fs);
	char *pat = g_strconcat("*/" SUBTUNE_PREFIX "???.", subtune_suffix, NULL);
	GPatternSpec *path_with_subtune = g_pattern_spec_new(pat);
	g_free(pat);
	if (!g_pattern_match(path_with_subtune,
			     strlen(path_container), path_container, NULL)) {
		g_pattern_spec_free(path_with_subtune);
		return path_container;
	}

	char *ptr = g_strrstr(path_container, "/" SUBTUNE_PREFIX);
	if (ptr != NULL)
		*ptr='\0';

	g_pattern_spec_free(path_with_subtune);
	return path_container;
}

/**
 * returns tune number from file.nsf/tune_xxx.* style path or 0 if no subtune
 * is appended.
 */
static int
get_song_num(const char *path_fs)
{
	const char *subtune_suffix = uri_get_suffix(path_fs);
	char *pat = g_strconcat("*/" SUBTUNE_PREFIX "???.", subtune_suffix, NULL);
	GPatternSpec *path_with_subtune = g_pattern_spec_new(pat);
	g_free(pat);

	if (g_pattern_match(path_with_subtune,
			    strlen(path_fs), path_fs, NULL)) {
		char *sub = g_strrstr(path_fs, "/" SUBTUNE_PREFIX);
		g_pattern_spec_free(path_with_subtune);
		if(!sub)
			return 0;

		sub += strlen("/" SUBTUNE_PREFIX);
		int song_num = strtol(sub, NULL, 10);

		return song_num - 1;
	} else {
		g_pattern_spec_free(path_with_subtune);
		return 0;
	}
}
static char *
asap_container_scan(const char *path_fs, const unsigned int tnum)
{
	ASAPInfo *asap_info;
	unsigned int num_songs;
	char *tname = strdup(path_fs);
	const char *name = basename(tname);

	struct stat st_buf;
	if(stat(path_fs, &st_buf) != 0){
		g_warning("Failed to stat %s (%s)\n", path_fs, strerror(errno));
		free(tname);
		return NULL;
	}

	FILE *f = fopen(path_fs, "r");
	unsigned char *buf = malloc(st_buf.st_size);
	int len = fread(buf, 1, st_buf.st_size, f);
	if(len < 0){
		free(buf);
		g_warning("Failed to read %s", tname);
		free(tname);
		return NULL;
	}

	asap_info = ASAPInfo_New();
	if(!ASAPInfo_Load(asap_info, name, buf, len)){
		free(buf);
		ASAPInfo_Delete(asap_info);
		g_warning("Cannot load %s\n", path_fs);
		free(tname);
		return false;
	}
	free(buf);

	num_songs = ASAPInfo_GetSongs(asap_info);
	ASAPInfo_Delete(asap_info);
	free(tname);
	/* if it only contains a single tune, don't treat as container */
	if (num_songs < 2)
		return NULL;

	const char *subtune_suffix = uri_get_suffix(path_fs);
	if (tnum <= num_songs){
		char *subtune = g_strdup_printf(
			SUBTUNE_PREFIX "%03u.%s", tnum, subtune_suffix);
		return subtune;
	} else
		return NULL;
}

static void
asap_file_decode(struct decoder *decoder, const char *path_fs)
{
	ASAPInfo *asap_info;
	char *tname = get_container_name(path_fs);

	struct stat st_buf;
	if(stat(tname, &st_buf) != 0){
		g_warning("Failed to stat %s (%s)\n", tname, strerror(errno));
		return;
	}

	FILE *f = fopen(tname, "r");
	unsigned char *buf = malloc(st_buf.st_size);
	int len = fread(buf, 1, st_buf.st_size, f);
	if(len < 0){
		free(buf);
		g_warning("Failed to read %s", tname);
		return;
	}

	asap_info = ASAPInfo_New();
	ASAP *asap = ASAP_New();
	if(!ASAPInfo_Load(asap_info, tname, buf, len) ||
	   !ASAP_Load(asap, tname, buf, len)){
		free(buf);
		g_free(tname);
		ASAP_Delete(asap);
		ASAPInfo_Delete(asap_info);
		g_warning("Cannot load %s\n", path_fs);
		return;
	}
	g_free(tname);

	int track_id = get_song_num(path_fs);
	if(!track_id)
		track_id = ASAPInfo_GetDefaultSong(asap_info);
	ASAPInfo_SetLoop(asap_info, track_id, false);
	int length = ASAPInfo_GetDuration(asap_info, track_id);

	float song_len;
	struct audio_format audio_format;
	enum decoder_command cmd;
	unsigned char asap_buf[ASAP_BUFFER_LEN];

	if(length > 0){
		song_len = length / 1000.0;
		ASAP_PlaySong(asap, track_id, length);
	}else{
		song_len = -1;
		ASAP_PlaySong(asap, track_id, -1);
		ASAP_DetectSilence(asap, 5);
	}

	/* initialize the MPD decoder */

	GError *error = NULL;
	if (!audio_format_init_checked(&audio_format, ASAP_SAMPLE_RATE,
				       SAMPLE_FORMAT_S16, ASAPInfo_GetChannels(asap_info),
				       &error)) {
		g_warning("%s", error->message);
		g_error_free(error);
		free(buf);
		ASAP_Delete(asap);
		ASAPInfo_Delete(asap_info);
		return;
	}

	decoder_initialized(decoder, &audio_format, true, song_len);

	/* play */
	do {
		if(!ASAP_Generate(asap, asap_buf, ASAP_BUFFER_LEN, ASAPSampleFormat_S16_L_E))
			break;
		cmd = decoder_data(decoder, NULL, asap_buf, sizeof(asap_buf), 0);

		if(cmd == DECODE_COMMAND_SEEK) {
			fprintf(stderr, "asap Seek \n");
			float where = decoder_seek_where(decoder);
			ASAP_Seek(asap, where*1000);
			decoder_command_finished(decoder);
		}

	} while(cmd != DECODE_COMMAND_STOP);
	fprintf(stderr, "asap decode end due to %d\n", cmd);

	ASAP_Delete(asap);
	ASAPInfo_Delete(asap_info);
	free(buf);
}

static bool
asap_scan_file(const char *path_fs,
	      const struct tag_handler *handler, void *handler_ctx)
{
	ASAPInfo *asap_info;
	char *tname = get_container_name(path_fs);

	struct stat st_buf;
	if(stat(tname, &st_buf) != 0){
		g_warning("Failed to stat %s (%s)\n", tname, strerror(errno));
		return false;
	}

	FILE *f = fopen(tname, "r");
	unsigned char *buf = malloc(st_buf.st_size);
	int len = fread(buf, 1, st_buf.st_size, f);
	if(len < 0){
		free(buf);
		g_warning("Failed to read %s", tname);
		return false;
	}

	asap_info = ASAPInfo_New();
	if(!ASAPInfo_Load(asap_info, tname, buf, len)){
		free(buf);
		free(tname);
		ASAPInfo_Delete(asap_info);
		g_warning("Cannot load %s\n", path_fs);
		return false;
	}
	free(tname);

	int track_id = get_song_num(path_fs);
	if(track_id == 0)
		track_id = ASAPInfo_GetDefaultSong(asap_info);

	int tmp;
	const char *stmp;
	if((tmp = ASAPInfo_GetDuration(asap_info, track_id)) > 0)
		tag_handler_invoke_duration(handler, handler_ctx,
					    ((float)tmp)/1000.0);
	if((stmp = ASAPInfo_GetTitleOrFilename(asap_info)) != NULL)
		tag_handler_invoke_tag(handler, handler_ctx,
				       TAG_TITLE, stmp);
	if((stmp = ASAPInfo_GetAuthor(asap_info)) != NULL)
		tag_handler_invoke_tag(handler, handler_ctx,
				       TAG_ARTIST, stmp);

	free(buf);
	ASAPInfo_Delete(asap_info);
	return true;
}

static const char *const asap_suffixes[] = {
	"sap", "cmc", "cm3", "cmr", "cms", "dmc", "dlt", "mpt", "mpd", "rmt",
	"tmc", "tm8", "tm2", "fc",
	NULL
};

extern const struct decoder_plugin asap_decoder_plugin;
const struct decoder_plugin asap_decoder_plugin = {
	.name = "asap",
	.file_decode = asap_file_decode,
	.scan_file = asap_scan_file,
	.suffixes = asap_suffixes,
	.container_scan = asap_container_scan,
};
