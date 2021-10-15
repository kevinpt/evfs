/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Tarfs VFS
  A VFS wrapper for accessing TAR files as a filesystem

  Filesystem is initialized by passing an open EvfsFile object for the TAR
  file. It is closed when the filesystem is unregistered.
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/tar_iter.h"
#include "evfs/tar_fs.h"

#include "evfs/util/dhash.h"
#include "evfs/util/range_strings.h"


///////////////////////////////////////////////////////////////////////////////////

// Shared buffer needs lock if threading is enabled
// We also need a lock for normal read operations in a threaded system
#if defined EVFS_USE_THREADING
#  define USE_TARFS_LOCK
#endif


#ifdef USE_TARFS_LOCK
#  define LOCK()    evfs__lock(&fs_data->tfs_lock)
#  define UNLOCK()  evfs__unlock(&fs_data->tfs_lock)
#else
#  define LOCK()
#  define UNLOCK()
#endif


typedef struct EvfsTarEntry {
  evfs_off_t header_offset;
  evfs_off_t file_size;
} EvfsTarEntry;


typedef struct EvfsTarIndex {
  dhash hash_table; // Manages index of hashed key/value pairs

  // Storage for file path keys
  char *keys;
} EvfsTarIndex;



typedef struct TarfsData {
  EvfsFile *tar_file;
  EvfsTarIndex tar_index;

  char      cur_dir[EVFS_MAX_PATH];
#ifdef EVFS_USE_TARFS_SHARED_BUFFER
  char      abs_path[EVFS_MAX_PATH]; // Shared buffer for building absolute paths
#endif
#ifdef USE_TARFS_LOCK
  EvfsLock  tfs_lock; // Serialize access to shared abs_path buffer
#endif

} TarfsData;

typedef struct TarfsFile {
  EvfsFile base;
  TarfsData *fs_data;

  evfs_off_t header_offset;   // Offset within tar file
  evfs_off_t file_size;       // Size of current archived file
  evfs_off_t read_pos;        // Current read position

  bool      is_open;
} TarfsFile;



// Helper to convert relative paths into absolute for the tfs API
// Returned path must be freed

static int make_absolute_path(Evfs *vfs, const char *path, char **absolute, bool force_malloc) {
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;
  *absolute = NULL;

  size_t abs_size;
  char *abs_path = NULL;

#ifdef EVFS_USE_TARFS_SHARED_BUFFER
  if(!force_malloc) {
    abs_size = COUNT_OF(fs_data->abs_path);
    LOCK(); // Unlocked by FREE_ABS() macro
    abs_path = fs_data->abs_path;
  }
#endif

  if(force_malloc || !abs_path) {
    abs_size = strlen(path) + 1;
    *absolute = NULL;

    if(!evfs_vfs_path_is_absolute(vfs, path)) // Need space for join with cur_dir
      abs_size += strlen(fs_data->cur_dir) + 1;

    abs_path = evfs_malloc(abs_size);
    if(MEM_CHECK(abs_path)) return EVFS_ERR_ALLOC;
  }


  AppendRange abs_path_r;
  range_init(&abs_path_r, abs_path, abs_size);

  int status = evfs_vfs_path_absolute(vfs, path, (StringRange *)&abs_path_r);
  if(status == EVFS_OK)
    *absolute = abs_path;

  return status;
}


// Prepare a buffer for the absolute path
#define MAKE_ABS(path, abs_path) char *abs_path; do { \
 int status = make_absolute_path(vfs, path, &abs_path, false); \
 if(status != EVFS_OK) \
    return status; \
} while(0)


#ifdef EVFS_USE_TARFS_SHARED_BUFFER
#  define FREE_ABS(abs_path)  UNLOCK()
#else
#  define FREE_ABS(abs_path)  evfs_free(abs_path)
#endif


// ******************** Tar index ********************



static void destroy_hashed_file(dhKey key, void *value, void *ctx) {
}


static int tarfs__index_hash_init(EvfsTarIndex *ht, int total_files, size_t total_path_len) {
  dhConfig s_hash_init = {
    .init_buckets = total_files,
    .value_size   = sizeof(EvfsTarEntry),

    .destroy_item = destroy_hashed_file,
    .gen_hash     = dh_gen_hash_string,
    .is_equal     = dh_equal_hash_keys_string
  };

  ht->keys = evfs_malloc(total_path_len);
  if(MEM_CHECK(ht->keys)) return EVFS_ERR_ALLOC;


  int err = dh_init(&ht->hash_table, &s_hash_init, NULL) ? EVFS_OK : EVFS_ERR;
  return err;
}


static void tarfs__index_hash_free(EvfsTarIndex *ht) {
  dh_free(&ht->hash_table);
  evfs_free(ht->keys);
  ht->keys = NULL;
}


static int tarfs__build_index(TarFileIterator *tar_it, EvfsTarIndex *ht) {
  if(!tar_iter_begin(tar_it)) return EVFS_ERR;

  size_t total_path_len = 0;
  int total_files = 0;
  // Scan the tar file to count files and determine total string storage for keys
  do {
    // Only index plain files and directories
    if(tar_it->cur_header.type_flag != TAR_TYPE_NORMAL_FILE &&
       tar_it->cur_header.type_flag != TAR_TYPE_DIRECTORY) continue;

    total_files++;
    total_path_len += strlen((char *)tar_it->cur_header.file_name) +
                      strlen((char *)tar_it->cur_header.file_prefix) + 1;

  } while(tar_iter_next(tar_it));


  // Prepare the hash index
  int err = tarfs__index_hash_init(ht, total_files, total_path_len);
  if(err != EVFS_OK) return err;


  AppendRange keys_r;
  range_init(&keys_r, ht->keys, total_path_len);

  // Build the index
  tar_iter_begin(tar_it);

  do {
    // Only index plain files and directories
    if(tar_it->cur_header.type_flag != TAR_TYPE_NORMAL_FILE &&
       tar_it->cur_header.type_flag != TAR_TYPE_DIRECTORY) continue;


    dhKey key;

    key.data = keys_r.start;
    key.length = 0;
    if(tar_it->cur_header.file_prefix[0] != '\0')
      key.length += range_cat_str(&keys_r, (char *)tar_it->cur_header.file_prefix);

    key.length += range_cat_str(&keys_r, (char *)tar_it->cur_header.file_name);
    range_cat_char(&keys_r,'\0');

    EvfsTarEntry entry;

    if(tar_it->cur_header.type_flag == TAR_TYPE_NORMAL_FILE) {
      entry.header_offset = tar_it->header_offset;
      entry.file_size = tar_it->file_size;
    } else {  // Directory entry
      entry.header_offset = -1;
      entry.file_size = -1;

      // Removing trailing slash
      ((char *)key.data)[--key.length] = '\0';
    }

    if(!dh_insert(&ht->hash_table, key, &entry))
      return EVFS_ERR;

  } while(tar_iter_next(tar_it));


  return EVFS_OK;
}


static inline bool tarfs__lookup_path(EvfsTarIndex *ht, const char *path, EvfsTarEntry *entry) {
  if(path[0] != '/') return false; // All paths must be absolute

  dhKey key;

  key.data = &path[1];
  key.length = strlen(key.data);

  return dh_lookup(&ht->hash_table, key, entry);
}



// ******************** File access methods ********************

static int tarfs__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  return EVFS_ERR_NO_SUPPORT;
}

static int tarfs__file_close(EvfsFile *fh) {
  TarfsFile *fil = (TarfsFile *)fh;

  fil->is_open = false;
  return EVFS_OK;
}

static ptrdiff_t tarfs__file_read(EvfsFile *fh, void *buf, size_t size) {
  TarfsFile *fil = (TarfsFile *)fh;
  TarfsData *fs_data = (TarfsData *)fil->fs_data;

  if(!fil->is_open) return EVFS_ERR_NOT_OPEN;

  evfs_off_t remaining = fil->file_size - fil->read_pos;
  if(remaining <= 0) return 0;

  if((evfs_off_t)size > remaining)
    size = remaining;

  LOCK();
  evfs_file_seek(fs_data->tar_file, fil->header_offset + TAR_BLOCK_SIZE + fil->read_pos, EVFS_SEEK_TO);
  fil->read_pos += size;
  ptrdiff_t rval = evfs_file_read(fs_data->tar_file, buf, size);
  UNLOCK();

  return rval;
}

static ptrdiff_t tarfs__file_write(EvfsFile *fh, const void *buf, size_t size) {
  return EVFS_ERR_NO_SUPPORT;
}

static int tarfs__file_truncate(EvfsFile *fh, evfs_off_t size) {
  return EVFS_ERR_NO_SUPPORT;
}

static int tarfs__file_sync(EvfsFile *fh) {
  return EVFS_OK; // Need to report as OK because evfs_file_size() syncs
}

static evfs_off_t tarfs__file_size(EvfsFile *fh) {
  TarfsFile *fil = (TarfsFile *)fh;

  if(!fil->is_open)
    return 0;
  else
    return fil->file_size;
}

static int tarfs__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  TarfsFile *fil = (TarfsFile *)fh;

  if(!fil->is_open) return EVFS_ERR_NOT_OPEN;

  offset = evfs__absolute_offset(fh, offset, origin);

  if(ASSERT(offset >= 0, "Invalid offset")) return EVFS_ERR;

  if(offset > fil->file_size)
    offset = fil->file_size;

  fil->read_pos = offset;

  return EVFS_OK;
}

static evfs_off_t tarfs__file_tell(EvfsFile *fh) {
  TarfsFile *fil = (TarfsFile *)fh;
  if(!fil->is_open)
    return 0;
  else
    return fil->read_pos;
}

static bool tarfs__file_eof(EvfsFile *fh) {
  TarfsFile *fil = (TarfsFile *)fh;
  if(!fil->is_open)
    return true;
  else
    return fil->read_pos >= fil->file_size;
}



static EvfsFileMethods s_tarfs_methods = {
  .m_ctrl     = tarfs__file_ctrl,
  .m_close    = tarfs__file_close,
  .m_read     = tarfs__file_read,
  .m_write    = tarfs__file_write,
  .m_truncate = tarfs__file_truncate,
  .m_sync     = tarfs__file_sync,
  .m_size     = tarfs__file_size,
  .m_seek     = tarfs__file_seek,
  .m_tell     = tarfs__file_tell,
  .m_eof      = tarfs__file_eof
};



// ******************** FS access methods ********************

static int tarfs__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  int err = EVFS_OK;
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;
  TarfsFile *fil = (TarfsFile *)fh;
  EvfsTarEntry entry;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_tarfs_methods;

  if(flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND))
    return EVFS_ERR_NO_SUPPORT;


  if(evfs_vfs_path_is_absolute(vfs, path)) {
    err = tarfs__lookup_path(&fs_data->tar_index, path, &entry) ? EVFS_OK : EVFS_ERR;

  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    err = tarfs__lookup_path(&fs_data->tar_index, abs_path, &entry) ? EVFS_OK : EVFS_ERR;
    FREE_ABS(abs_path);
  }

  if(entry.header_offset < 0)
    err = EVFS_ERR_IS_DIR;

  if(err == EVFS_OK) {
    fil->header_offset = entry.header_offset;
    fil->file_size = entry.file_size;
    fil->read_pos = 0;
    fil->is_open = true;
  } else {
    fil->is_open = false;
  }

  fil->fs_data = fs_data;

  return err;
}


static int tarfs__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;
  EvfsTarEntry entry;
  int err;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    err = tarfs__lookup_path(&fs_data->tar_index, path, &entry) ? EVFS_OK : EVFS_ERR;
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    err = tarfs__lookup_path(&fs_data->tar_index, abs_path, &entry) ? EVFS_OK : EVFS_ERR;
    FREE_ABS(abs_path);
  }

  memset(info, 0, sizeof(*info));

  if(err >= 0) {
    if(entry.header_offset >= 0) {
      info->size = entry.file_size;
    } else {
      info->type |= EVFS_FILE_DIR;
    }
  }

  return err;
}



// Tarfs doesn't handle relative paths so we track the current directory in fs_data
static int tarfs__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;
  AppendRange r = *(AppendRange *)cur_dir;

  range_cat_str(&r, fs_data->cur_dir);
  range_terminate(&r);
  return EVFS_OK;
}



static int tarfs__set_cur_dir(Evfs *vfs, const char *path) {
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;

  if(evfs_vfs_path_is_absolute(vfs, path)) { // Just assign absolute paths
    // Confirm the path exists
    if(!evfs__vfs_existing_dir(vfs, path))
      return EVFS_ERR_NO_PATH;
  
    strncpy(fs_data->cur_dir, path, EVFS_MAX_PATH-1);
    fs_data->cur_dir[EVFS_MAX_PATH-1] = '\0';

  } else { // Path is relative: Join it to the existing directory
    StringRange head, tail, joined;
    
    range_init(&head, fs_data->cur_dir, EVFS_MAX_PATH);
    range_init(&tail, (char *)path, strlen(path));
    
    size_t joined_size = strlen(fs_data->cur_dir) + 1 + strlen(path) + 1;
    char *joined_path = evfs_malloc(joined_size);
    if(MEM_CHECK(joined_path)) return EVFS_ERR_ALLOC;

    range_init(&joined, joined_path, joined_size);

    evfs_vfs_path_join(vfs, &head, &tail, &joined);

    // Confirm the path exists
    if(!evfs__vfs_existing_dir(vfs, joined_path)) {
      evfs_free(joined_path);
      return EVFS_ERR_NO_PATH;
    }

    // Overwrite old cur_dir
    evfs_vfs_path_normalize(vfs, joined_path, &head);

    evfs_free(joined_path);
  }

  return EVFS_OK;
}




static int tarfs__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  TarfsData *fs_data = (TarfsData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER:
      evfs_file_close(fs_data->tar_file);
      tarfs__index_hash_free(&fs_data->tar_index);
#ifdef USE_TARFS_LOCK
      evfs__lock_destroy(&fs_data->tfs_lock);
#endif
      evfs_free(vfs);
      return EVFS_OK; break;

    case EVFS_CMD_GET_STAT_FIELDS:
      {
        unsigned *v = (unsigned *)arg;
        *v = EVFS_INFO_SIZE | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    case EVFS_CMD_GET_DIR_FIELDS:
      {
        unsigned *v = (unsigned *)arg;
        *v = 0;
      }
      return EVFS_OK; break;

    default: return EVFS_ERR_NO_SUPPORT; break;
  }
}



// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])

/*
Register a Tar FS instance

Args:
  vfs_name:      Name of new VFS
  tar_file:      EVFS file of Tar data
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_tar_fs(const char *vfs_name, EvfsFile *tar_file, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(tar_file)) return EVFS_ERR_BAD_ARG;
  Evfs *new_vfs;
  TarfsData *fs_data;

  // Construct a new VFS
  // We have three objects allocated together [Evfs][TarfsData][char[]]
  size_t alloc_size = sizeof(*new_vfs) + sizeof(*fs_data) + strlen(vfs_name)+1;
  new_vfs = evfs_malloc(alloc_size);
  if(MEM_CHECK(new_vfs)) return EVFS_ERR_ALLOC;
  memset(new_vfs, 0, alloc_size);

  // Prepare new objects
  fs_data = (TarfsData *)NEXT_OBJ(new_vfs);
  
  new_vfs->vfs_name = (char *)NEXT_OBJ(fs_data);
  strcpy((char *)new_vfs->vfs_name, vfs_name);

  // Init FS data

  strncpy(fs_data->cur_dir, "/", 2); // Start in root dir
  fs_data->tar_file = tar_file;

  TarFileIterator tar_it;
  tar_iter_init(&tar_it, fs_data->tar_file);
  tarfs__build_index(&tar_it, &fs_data->tar_index);

  // Init VFS
  new_vfs->vfs_file_size = sizeof(TarfsFile);
  new_vfs->vfs_dir_size = 0;
  new_vfs->fs_data = fs_data;

  // Required methods
  new_vfs->m_open = tarfs__open;
  new_vfs->m_stat = tarfs__stat;
  
  // Optional methods
  new_vfs->m_get_cur_dir = tarfs__get_cur_dir;
  new_vfs->m_set_cur_dir = tarfs__set_cur_dir;
  new_vfs->m_vfs_ctrl = tarfs__vfs_ctrl;


#ifdef USE_TARFS_LOCK
  if(evfs__lock_init(&fs_data->tfs_lock) != EVFS_OK) {
    evfs_free(new_vfs);
    THROW(EVFS_ERR_INIT);
  }
#endif

  return evfs_register(new_vfs, default_vfs);
}


