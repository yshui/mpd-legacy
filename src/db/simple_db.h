#pragma once
#include "db_plugin.h"
#include "util/ythread.h"
#define DIRECTORY_INFO_BEGIN "info_begin"
#define DIRECTORY_INFO_END "info_end"
#define DB_FORMAT_PREFIX "format: "
#define DIRECTORY_MPD_VERSION "mpd_version: "
#define DIRECTORY_FS_CHARSET "fs_charset: "
#define DB_TAG_PREFIX "tag: "
#define DIRECTORY_DIR "directory: "
#define DIRECTORY_MTIME "mtime: "
#define DIRECTORY_BEGIN "begin: "
#define DIRECTORY_END "end: "
#define SONG_FILE	"file: "
#define SONG_TIME	"Time: "
#define SONG_BEGIN "song_begin: "
#define SONG_MTIME "mtime"
#define SONG_END "song_end"
#define PLAYLIST_META_BEGIN "playlist_begin: "

struct simple_db {
	char *path;
	struct directory *root;
	time_t mtime;
	int refcount;
	mtx_t db_mutex;
};

struct simple_db_dir {
	struct simple_db *db;

	struct directory *dir;
};

enum {
	DB_FORMAT = 1,
};

static inline void
_simple_db_lock(struct simple_db *db) {
	mtx_lock(&db->db_mutex);
}

static inline void
_simple_db_unlock(struct simple_db *db) {
	mtx_unlock(&db->db_mutex);
}
