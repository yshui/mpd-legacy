#include <assert.h>
#include <sys/stat.h>
#include "simple_db_load.h"
#include "simple_db_io.h"

#include "song.h"
#include "tag.h"
#include "tag_internal.h"
#include "string_util.h"
#include "playlist_vector.h"
#include "directory.h"
#include "path.h"
#include "text_file.h"
#ifdef COMPRESS_DB
#include <zlib.h>
typedef gzFile db_file;

static inline db_file db_open(const char *path_fs, const char *flags){
	return gzopen(path_fs, flags);
}

static inline int db_printf(db_file file, const char *fmt, ...){
	char *res;
	va_list args;
	int ret;

	va_start(args, fmt);
	vasprintf(&res, fmt, args);
	va_end(args);

	ret = gzputs(file, res);
	free(res);
	return ret;
}

static inline char *db_read_text_line(db_file file, GString *buf){
	return gz_read_text_line(file, buf);
}

static inline int db_close(db_file file){
	return gzclose(file);
}

static inline int db_error(db_file file){
	int errnum;
	gzerror(file, &errnum);
	if(errnum == Z_ERRNO)
		return errno;
	return errnum;
}
#else
typedef FILE *db_file;

static inline db_file db_open(const char *path_fs, const char *flags){
	return fopen(path_fs, flags);
}

static inline int db_printf(db_file file, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	int ret = vfprintf(file, fmt, args);
	va_end(args);
	return ret;
}

static inline char *db_read_text_line(db_file file, GString *buf){
	return read_text_line(file, buf);
}

static inline int db_close(db_file file){
	return fclose(file);
}

static inline int db_error(db_file file){
	return ferror(file);
}
#endif
/* ======================================================= *
 * 			Database Load
 * ======================================================= */
static inline struct song *
simple_db_song_load(db_file fp, struct directory *parent, const char *uri,
	  GString *buffer)
{
	struct song *song = parent != NULL
		? song_file_new(uri, parent)
		: song_remote_new(uri);
	char *line, *colon;
	enum tag_type type;
	const char *value;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, SONG_END) != 0) {
		colon = strchr(line, ':');
		if (colon == NULL || colon == line) {
			if (song->tag != NULL)
				tag_end_add(song->tag);
			song_free(song);

			log_err("song_save: unknown line in db: %s", line);
			return ERR_PTR(-DB_MALFORM);
		}

		*colon++ = 0;
		value = strchug_fast_c(colon);

		if ((type = tag_name_parse(line)) != TAG_NUM_OF_ITEM_TYPES) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			tag_add_item(song->tag, type, value);
		} else if (strcmp(line, "Time") == 0) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			song->tag->time = atoi(value);
		} else if (strcmp(line, "Playlist") == 0) {
			if (!song->tag) {
				song->tag = tag_new();
				tag_begin_add(song->tag);
			}

			song->tag->has_playlist = strcmp(value, "yes") == 0;
		} else if (strcmp(line, SONG_MTIME) == 0) {
			song->mtime = atoi(value);
		} else if (strcmp(line, "Range") == 0) {
			char *endptr;

			song->start_ms = strtoul(value, &endptr, 10);
			if (*endptr == '-')
				song->end_ms = strtoul(endptr + 1, NULL, 10);
		} else {
			if (song->tag != NULL)
				tag_end_add(song->tag);
			song_free(song);

			log_err("song_save: unknown line in db: %s", line);
			return ERR_PTR(-DB_MALFORM);
		}
	}

	if (song->tag != NULL)
		tag_end_add(song->tag);

	return song;
}
static inline int
playlist_metadata_load(db_file fp, struct list_head *pv, const char *name,
		       GString *buffer)
{
	struct playlist_metadata pm = {
		.mtime = 0,
	};
	char *line, *colon;
	const char *value;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, "playlist_end") != 0) {
		colon = strchr(line, ':');
		if (colon == NULL || colon == line) {
			log_err("playlist: unknown line in db: %s", line);
			return -DB_MALFORM;
		}

		*colon++ = 0;
		value = strchug_fast_c(colon);

		if (strcmp(line, "mtime") == 0)
			pm.mtime = strtol(value, NULL, 10);
		else {
			log_err("playlist: unknown line in db: %s", line);
			return -DB_MALFORM;
		}
	}

	playlist_vector_update_or_add(pv, name, pm.mtime);
	return MPD_SUCCESS;
}
static struct directory *
simple_db_directory_load_subdir(db_file , struct directory *, const char *, GString *);

static int
simple_db_directory_load(db_file fp, struct directory *directory,
	       GString *buffer)
{
	const char *line;

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       !g_str_has_prefix(line, DIRECTORY_END)) {
		if (g_str_has_prefix(line, DIRECTORY_DIR)) {
			struct directory *subdir = simple_db_directory_load_subdir(fp, directory,
					      line + sizeof(DIRECTORY_DIR) - 1, buffer);
			if (IS_ERR(subdir))
				return PTR_ERR(subdir);
		} else if (g_str_has_prefix(line, SONG_BEGIN)) {
			const char *name = line + sizeof(SONG_BEGIN) - 1;
			struct song *song;

			if (directory_get_song(directory, name) != NULL) {
				log_err("directory: Duplicate song '%s'", name);
				return -DB_DUP;
			}

			song = simple_db_song_load(fp, directory, name,
					 buffer);
			if (IS_ERR(song))
				return PTR_ERR(song);

			directory_add_song(directory, song);
		} else if (g_str_has_prefix(line, PLAYLIST_META_BEGIN)) {
			/* duplicate the name, because
			   playlist_metadata_load() will overwrite the
			   buffer */
			char *name = g_strdup(line + sizeof(PLAYLIST_META_BEGIN) - 1);

			int ret = playlist_metadata_load(fp, &directory->playlists,
						    name, buffer);
			if (ret != MPD_SUCCESS) {
				free(name);
				return ret;
			}

			free(name);
		} else {
			log_err("Malformed line: %s", line);
			return -DB_MALFORM;
		}
	}

	return MPD_SUCCESS;
}

static struct directory *
simple_db_directory_load_subdir(db_file fp, struct directory *parent, const char *name,
		      GString *buffer)
{
	const char *line;
	if (directory_get_child(parent, name) != NULL) {
		log_err("directory: Duplicate subdirectory '%s'", name);
		return ERR_PTR(-DB_DUP);
	}

	struct directory *directory = directory_new_child(parent, name);

	line = db_read_text_line(fp, buffer);
	if (line == NULL) {
		log_err("directory: Unexpected end of file");
		directory_delete(directory);
		return ERR_PTR(-DB_CORRUPT);
	}

	if (g_str_has_prefix(line, DIRECTORY_MTIME)) {
		directory->mtime =
			g_ascii_strtoull(line + sizeof(DIRECTORY_MTIME) - 1,
					 NULL, 10);

		line = db_read_text_line(fp, buffer);
		if (line == NULL) {
			log_err("directory: Unexpected end of file");
			directory_delete(directory);
			return ERR_PTR(-DB_CORRUPT);
		}
	}

	if (!g_str_has_prefix(line, DIRECTORY_BEGIN)) {
		log_err("directory: Malformed line: %s", line);
		directory_delete(directory);
		return ERR_PTR(-DB_MALFORM);
	}

	int ret = simple_db_directory_load(fp, directory, buffer);
	if (ret != MPD_SUCCESS) {
		directory_delete(directory);
		return ERR_PTR(ret);
	}

	return directory;
}
static inline int
simple_db_load_internal(db_file fp, struct simple_db *db)
{
	struct directory *music_root = db->root;
	GString *buffer = g_string_sized_new(1024);
	char *line;
	int format = 0;
	bool found_charset = false, found_version = false;
	bool tags[TAG_NUM_OF_ITEM_TYPES];

	assert(music_root != NULL);

	/* get initial info */
	line = db_read_text_line(fp, buffer);
	if (line == NULL || strcmp(DIRECTORY_INFO_BEGIN, line) != 0) {
		log_err("Database corrupted");
		g_string_free(buffer, true);
		return -DB_CORRUPT;
	}

	memset(tags, false, sizeof(tags));

	while ((line = db_read_text_line(fp, buffer)) != NULL &&
	       strcmp(line, DIRECTORY_INFO_END) != 0) {
		if (g_str_has_prefix(line, DB_FORMAT_PREFIX)) {
			format = atoi(line + sizeof(DB_FORMAT_PREFIX) - 1);
		} else if (g_str_has_prefix(line, DIRECTORY_MPD_VERSION)) {
			if (found_version) {
				log_err("Duplicate version line");
				g_string_free(buffer, true);
				return -DB_MALFORM;
			}

			found_version = true;
		} else if (g_str_has_prefix(line, DIRECTORY_FS_CHARSET)) {
			const char *new_charset, *old_charset;
			if (found_charset) {
				log_err("Duplicate charset line");
				g_string_free(buffer, true);
				return -DB_MALFORM;
			}

			found_charset = true;

			new_charset = line + sizeof(DIRECTORY_FS_CHARSET) - 1;
			old_charset = path_get_fs_charset();
			if (old_charset != NULL
			    && strcmp(new_charset, old_charset)) {
				log_err("Existing database has charset "
					    "\"%s\" instead of \"%s\"; "
					    "discarding database file",
					    new_charset, old_charset);
				g_string_free(buffer, true);
				return -DB_MALFORM;
			}
		} else if (g_str_has_prefix(line, DB_TAG_PREFIX)) {
			const char *name = line + sizeof(DB_TAG_PREFIX) - 1;
			enum tag_type tag = tag_name_parse(name);
			if (tag == TAG_NUM_OF_ITEM_TYPES) {
				log_err("Unrecognized tag '%s', "
					    "discarding database file",
					    name);
				return -DB_MALFORM;
			}

			tags[tag] = true;
		} else {
			log_err("Malformed line: %s", line);
			g_string_free(buffer, true);
			return -DB_MALFORM;
		}
	}

	if (format != DB_FORMAT) {
		log_err("Database format mismatch, "
			    "discarding database file");
		return -DB_MALFORM;
	}

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i) {
		if (!ignore_tag_items[i] && !tags[i]) {
			log_err("Tag list mismatch, "
				    "discarding database file");
			return -DB_MALFORM;
		}
	}

	log_debug("reading DB");

	_simple_db_lock(db);
	int ret = simple_db_directory_load(fp, music_root, buffer);
	_simple_db_unlock(db);
	g_string_free(buffer, true);

	return ret;
}

int
simple_db_load(struct simple_db *db)
{
	assert(db != NULL);
	assert(db->path != NULL);
	assert(db->root != NULL);

	db_file fp = db_open(db->path, "r");
	if (fp == NULL) {
		log_err("Failed to open database file \"%s\": %s",
			    db->path, strerror(errno));
		return -DB_ERRNO;
	}

	int ret = simple_db_load_internal(fp, db);
	if (ret != MPD_SUCCESS) {
		db_close(fp);
		return ret;
	}

	db_close(fp);

	struct stat st;
	if (stat(db->path, &st) == 0)
		db->mtime = st.st_mtime;

	return MPD_SUCCESS;
}

/* ======================================================= *
 * 			Database Save
 * ======================================================= */
static inline void
simple_db_tag_save(db_file file, const struct tag *tag)
{
	if (tag->time >= 0)
		db_printf(file, SONG_TIME "%i\n", tag->time);

	if (tag->has_playlist)
		db_printf(file, "Playlist: yes\n");

	for (unsigned i = 0; i < tag->num_items; i++)
		db_printf(file, "%s: %s\n",
			tag_item_names[tag->items[i]->type],
			tag->items[i]->value);
}

static inline void
simple_db_song_save(db_file fp, const struct song *song)
{
	db_printf(fp, SONG_BEGIN "%s\n", song->uri);

	if (song->end_ms > 0)
		db_printf(fp, "Range: %u-%u\n", song->start_ms, song->end_ms);
	else if (song->start_ms > 0)
		db_printf(fp, "Range: %u-\n", song->start_ms);

	if (song->tag != NULL)
		simple_db_tag_save(fp, song->tag);

	db_printf(fp, SONG_MTIME ": %li\n", (long)song->mtime);
	db_printf(fp, SONG_END "\n");
}


static inline void
simple_db_playlist_vector_save(db_file fp, const struct list_head *pv)
{
	struct playlist_metadata *pm;
	playlist_vector_for_each(pm, pv)
		db_printf(fp, PLAYLIST_META_BEGIN "%s\n"
			"mtime: %li\n"
			"playlist_end\n",
			pm->name, (long)pm->mtime);
}


static void
simple_db_directory_save(db_file fp, const struct directory *directory)
{
	if (!directory_is_root(directory)) {
		db_printf(fp, DIRECTORY_MTIME "%lu\n",
			(unsigned long)directory->mtime);

		db_printf(fp, "%s%s\n", DIRECTORY_BEGIN,
			directory_get_path(directory));
	}

	struct directory *cur;
	directory_for_each_child(cur, directory) {
		char *base = g_path_get_basename(cur->path);

		db_printf(fp, DIRECTORY_DIR "%s\n", base);
		g_free(base);

		simple_db_directory_save(fp, cur);

		if (db_error(fp))
			return;
	}

	struct song *song;
	directory_for_each_song(song, directory)
		simple_db_song_save(fp, song);

	simple_db_playlist_vector_save(fp, &directory->playlists);

	if (!directory_is_root(directory))
		db_printf(fp, DIRECTORY_END "%s\n",
			directory_get_path(directory));
}


static inline void
simple_db_save_internal(db_file fp, const struct directory *music_root)
{
	assert(music_root != NULL);

	db_printf(fp, "%s\n", DIRECTORY_INFO_BEGIN);
	db_printf(fp, DB_FORMAT_PREFIX "%u\n", DB_FORMAT);
	db_printf(fp, "%s%s\n", DIRECTORY_MPD_VERSION, VERSION);
	db_printf(fp, "%s%s\n", DIRECTORY_FS_CHARSET, path_get_fs_charset());

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i)
		if (!ignore_tag_items[i])
			db_printf(fp, DB_TAG_PREFIX "%s\n", tag_item_names[i]);

	db_printf(fp, "%s\n", DIRECTORY_INFO_END);

	simple_db_directory_save(fp, music_root);
}

int
simple_db_save(struct simple_db *db)
{
	struct directory *music_root = db->root;

	_simple_db_lock(db);

	log_debug("removing empty directories from DB");
	directory_prune_empty(music_root);

	log_debug("sorting DB");
	directory_sort(music_root);

	_simple_db_unlock(db);

	log_debug("writing DB");

	db_file fp = db_open(db->path, "w");
	if (!fp) {
		log_err("unable to write to db file \"%s\": %s",
			    db->path, strerror(errno));
		return -DB_ACCESS;
	}

	simple_db_save_internal(fp, music_root);

	if (db_error(fp)) {
		log_err("Failed to write to database file: %s",
			    strerror(errno));
		db_close(fp);
		return -DB_ACCESS;
	}

	db_close(fp);

	struct stat st;
	if (stat(db->path, &st) == 0)
		db->mtime = st.st_mtime;

	return MPD_SUCCESS;
}


