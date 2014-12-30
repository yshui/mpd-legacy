#pragma once

#include "util/list.h"
#include "util/yref.h"
#include "util/uthash.h"
#include "db_plugin.h"

enum dir_name_type {
	DIR_SONG,
	DIR_PLAYLIST,
	DIR_DIR
};

struct dir_name {
	char *name;
	enum dir_name_type type;
	struct list_head names;
};

typedef struct d_entry {
	char *name;
	void *db_data;
	bool invalid;
	const struct db_plugin *dops;
	yref_t refcount;
	struct list_head subentry;
	struct list_head unused;
	UT_hash_handle hh;
}d_entry;

yref_def_scope_out_proto(d_entry);

yref_ret_t d_get(const char *name);
yref_ret_t d_new(const char *name, const struct db_plugin *dops);
void d_cache_init(const struct config_param *param);
void d_put(struct d_entry **);
void d_invalidate_by_name(const char *name);

/*
 * Invalidate a dentry, will also unref the dentry.
 */
void d_invalidate(struct d_entry **d);
