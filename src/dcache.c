#include "dcache.h"
#include "vfs.h"
#include "util/uthash.h"
#include "util/ythread.h"
#include "db/simple_db_plugin.h"

static d_entry *d_hash_table = NULL;
static mtx_t d_mutex;
static d_entry *d_root = NULL;
/* d_unused: Stored d_entries with refcount = 0,
 * using lru to free old entries. */
static struct list_head d_unused;
static int d_unused_count = 0;
static const int max_unused = 100;

yref_def_scope_out(d_entry, refcount);

static yref_ret_t
d_path_lookup(const char *name) {
	char *tmp_name = strdup(name);
	//Trim trailing slashes
	char *slash = tmp_name+strlen(tmp_name)-1;
	while(*slash == '/') {
		*slash = 0;
		slash--;
	}
	//First, we find the lowest ancestor on the path
	struct d_entry *d;
	while(tmp_name[0]) {
		yref_return_get(d_get(tmp_name), d, refcount);
		if (d)
			break;
		slash = strrchr(tmp_name, '/');
		while(*slash == '/') {
			*slash = 0;
			slash--;
		}
	}

	char *name_part2 = strdup(name+strlen(tmp_name));
	char *pos = name_part2;
	free(tmp_name);

	//Then, we try to lookup the rest of the path using db plugin
	while(*pos == '/')
		pos++;
	while(*pos) {
		struct d_entry *tmpd;
		slash = strchr(pos, '/');
		*slash = 0;
		yref_return_get(d->dops->lookup(d->db_data, pos),
				tmpd, refcount);
		d_put(&d);
		if (!tmpd) {
			free(name_part2);
			yref_ret_null;
		}
		yref_move(tmpd, d, refcount);
		pos = slash+1;
		while(*pos == '/')
			pos++;
	}
	free(name_part2);
	yref_return(d, refcount);
}

static inline void _d_destroy(struct d_entry *d) {
	d->dops->free(d->db_data);
	free(d->name);
	struct dir_name *dn, *tdn;
	list_for_each_entry_safe(dn, tdn,
	    &d->subentry, names) {
		list_del(&dn->names);
		free(dn->name);
		free(dn);
	}
	free(d);
}

static void d_drop(void *_d) {
	struct d_entry *d = _d;
	mtx_lock(&d_mutex);
	//We won't add an invalid dentry to d_unused,
	//we simply destroy it.
	if (d_unused_count >= max_unused) {
		struct d_entry *first_d =
		    list_first_entry(&d_unused, struct d_entry, unused);
		list_del(&first_d->unused);
		if (!d->invalid)
			list_add_tail(&d->unused, &d_unused);
		else {
			HASH_DEL(d_hash_table, d);
			d_unused_count--;
		}

		HASH_DEL(d_hash_table, first_d);
		mtx_unlock(&d_mutex);

		_d_destroy(first_d);
	} else {
		if (!d->invalid) {
			list_add_tail(&d->unused, &d_unused);
			d_unused_count++;
		} else
			HASH_DEL(d_hash_table, d);
		mtx_unlock(&d_mutex);
	}
	if (d->invalid)
		_d_destroy(d);
}

static inline void
d_invalidate_locked(struct d_entry *d) {
	int refcount = yref_get(&d->refcount);
	if (refcount != 0) {
		//Somebody else is still holding this dentry,
		//can't drop it now.
		d->invalid = true;
		return;
	}
	//We found this dentry in the hash, and the refcount is 0,
	//so this dentry must be in unused list.
	list_del(&d->unused);
	HASH_DEL(d_hash_table, d);
	_d_destroy(d);
}

void d_invalidate_by_name(const char *name) {
	mtx_lock(&d_mutex);
	struct d_entry *d = NULL;
	HASH_FIND_STR(d_hash_table, name, d);
	if(d)
		d_invalidate_locked(d);
	mtx_unlock(&d_mutex);
}

void d_invalidate(struct d_entry **d) {
	char *tmp = strdup((*d)->name);
	d_put(d);
	d_invalidate_by_name(tmp);
	free(tmp);
}

static inline char *
prune_slashes(const char *path) {
	char *tpath = strdup(path);
	char *i = tpath, *j = tpath;
	while(*j) {
		*(i++) = *j;
		if (*j == '/')
			while(*j == '/')
				j++;
		else
			j++;
	}
	if (*(i-1) == '/')
		*(i-1) = 0;
	return tpath;
}

yref_ret_t d_new(const char *_name, const struct db_plugin *dops) {
	assert(_name);
	assert(dops);

	char *name = prune_slashes(_name);
	struct d_entry *s = tmalloc(struct d_entry, 1);
	s->name = name;
	s->dops = dops;
	s->db_data = NULL;
	yref_init(s, &s->refcount, d_drop);
	mtx_lock(&d_mutex);
	HASH_ADD_STR(d_hash_table, name, s);
	mtx_unlock(&d_mutex);

	yref_ref(s, s, refcount);
	yref_return(s, refcount);
}

yref_ret_t d_get(const char *_name) {
	struct d_entry *s = NULL;
	char *name = prune_slashes(_name);
	mtx_lock(&d_mutex);
	HASH_FIND_STR(d_hash_table, name, s);
	if (!s) {
		mtx_unlock(&d_mutex);
		free(name);
		yref_return_get(d_path_lookup(name), s, refcount);
	} else {
		int rc = yref_ref(s, s, refcount);
		if (rc == 1)
			//This d_entry had 0 refcount before we referenced it.
			//Which means it's in the unused list.
			list_del(&s->unused);
		mtx_unlock(&d_mutex);
	}
	free(name);
	yref_return(s, refcount);
}

void d_put(struct d_entry **d) {
	_yref_unref((void **)d, &(*d)->refcount);
	*d = NULL;
}

void d_cache_init(const struct config_param *param) {
	mtx_init(&d_mutex, 0);

	INIT_LIST_HEAD(&d_unused);
	yref_return_get(d_new("/", &simple_db_plugin), d_root, refcount);
	simple_db_plugin.open(param, d_root);
}
