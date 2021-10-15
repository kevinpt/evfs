/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Littlefs VFS
  A VFS wrapper for the Littlefs API.
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "lfs.h"
#include "evfs/littlefs_fs.h"

///////////////////////////////////////////////////////////////////////////////////

// Shared buffer needs lock if threading is enabled
#if defined EVFS_USE_LITTLEFS_SHARED_BUFFER && defined EVFS_USE_THREADING
#  define USE_LFS_LOCK
#endif


#ifdef USE_LFS_LOCK
#  define LOCK()    evfs__lock(&fs_data->lfs_lock)
#  define UNLOCK()  evfs__unlock(&fs_data->lfs_lock)
#else
#  define LOCK()
#  define UNLOCK()
#endif


typedef struct LittlefsData_s {
  lfs_t    *lfs;
  struct lfs_info info;
  char      cur_dir[LFS_NAME_MAX];
#ifdef EVFS_USE_LITTLEFS_SHARED_BUFFER
  char      abs_path[EVFS_MAX_PATH]; // Shared buffer for building absolute paths
#endif
#ifdef USE_LFS_LOCK
  EvfsLock  lfs_lock; // Serialize access to shared abs_path buffer
#endif

  // VFS config options
  unsigned cfg_readonly    :1; // EVFS_CMD_SET_READONLY
  unsigned cfg_no_dir_dots :1; // EVFS_CMD_SET_NO_DIR_DOTS
} LittlefsData;

typedef struct LittlefsFile_s {
  EvfsFile base;
  LittlefsData *fs_data;
  lfs_t *lfs;
  lfs_file_t fil;
} LittlefsFile;


typedef struct LittlefsDir_s {
  EvfsDir base;
  LittlefsData *fs_data;
  lfs_dir_t dir;
  struct lfs_info info; // Need to hold entry names
} LittlefsDir;



// Common error code conversions
static ptrdiff_t translate_error(int err) {
  if(err > 0) // Pass positive return values through
    return err;

  switch(err) {
    case LFS_ERR_OK:          return EVFS_OK; break;
    case LFS_ERR_IO:          return EVFS_ERR_IO; break;
    case LFS_ERR_CORRUPT:     return EVFS_ERR_CORRUPTION; break;
    case LFS_ERR_NOENT:       return EVFS_ERR_NO_FILE; break;
    case LFS_ERR_EXIST:       return EVFS_ERR_EXISTS; break;
    case LFS_ERR_NOTDIR:      return EVFS_ERR_NO_PATH; break;
    case LFS_ERR_ISDIR:       return EVFS_ERR_IS_DIR; break;
    case LFS_ERR_NOTEMPTY:    return EVFS_ERR_NOT_EMPTY; break;
    case LFS_ERR_BADF:        return EVFS_ERR; break; // Not used by lfs
    case LFS_ERR_FBIG:        return EVFS_ERR_OVERFLOW; break;
    case LFS_ERR_INVAL:       return EVFS_ERR_BAD_ARG; break;
    case LFS_ERR_NOSPC:       return EVFS_ERR_FS_FULL; break;
    case LFS_ERR_NOMEM:       return EVFS_ERR_ALLOC; break;
    case LFS_ERR_NOATTR:      return EVFS_ERR; break; // Not used by evfs
    case LFS_ERR_NAMETOOLONG: return EVFS_ERR_TOO_LONG; break;
    default:                  return EVFS_ERR; break;
  }
}

#define simple_error(e)  ((e) >= LFS_ERR_OK ? EVFS_OK : EVFS_ERR)


// Helper to convert relative paths into absolute for the lfs API
// Returned path must be freed
static int make_absolute_path(Evfs *vfs, const char *path, char **absolute, bool force_malloc) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;
  *absolute = NULL;

  size_t abs_size;
  char *abs_path = NULL;

#ifdef EVFS_USE_LITTLEFS_SHARED_BUFFER
  if(!force_malloc) {
    abs_size = COUNT_OF(fs_data->abs_path);
    LOCK();
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
 int status_ma = make_absolute_path(vfs, path, &abs_path, false); \
 if(status_ma != EVFS_OK) \
    return status_ma; \
} while(0)


#ifdef EVFS_USE_LITTLEFS_SHARED_BUFFER
#  define FREE_ABS(abs_path)  UNLOCK()
#else
#  define FREE_ABS(abs_path)  evfs_free(abs_path)
#endif


// ******************** File access methods ********************

static int littlefs__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  //LittlefsFile *fil = (LittlefsFile *)fh;
  
  return EVFS_OK;
}

static int littlefs__file_close(EvfsFile *fh) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  lfs_file_close(fil->lfs, &fil->fil);
  return EVFS_OK;
}

static ptrdiff_t littlefs__file_read(EvfsFile *fh, void *buf, size_t size) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  lfs_ssize_t read = lfs_file_read(fil->lfs, &fil->fil, buf, size);
  return translate_error(read);
}

static ptrdiff_t littlefs__file_write(EvfsFile *fh, const void *buf, size_t size) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  LittlefsData *fs_data = (LittlefsData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  lfs_ssize_t wrote = lfs_file_write(fil->lfs, &fil->fil, buf, size);
  return translate_error(wrote);
}

static int littlefs__file_truncate(EvfsFile *fh, evfs_off_t size) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  LittlefsData *fs_data = (LittlefsData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  return simple_error(lfs_file_truncate(fil->lfs, &fil->fil, size));
}

static int littlefs__file_sync(EvfsFile *fh) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  return simple_error(lfs_file_sync(fil->lfs, &fil->fil));
}

static evfs_off_t littlefs__file_size(EvfsFile *fh) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  lfs_soff_t size = lfs_file_size(fil->lfs, &fil->fil);
  return size >= 0 ? size : 0;
}

static int littlefs__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  LittlefsFile *fil = (LittlefsFile *)fh;
  int sorg = LFS_SEEK_SET;

  switch(origin) {
    case EVFS_SEEK_TO:   sorg = LFS_SEEK_SET; break;
    case EVFS_SEEK_REL:  sorg = LFS_SEEK_CUR; break;
    case EVFS_SEEK_REV:  sorg = LFS_SEEK_END; break;
    default:
      break;
  }

  return simple_error(lfs_file_seek(fil->lfs, &fil->fil, offset, sorg));
}

static evfs_off_t littlefs__file_tell(EvfsFile *fh) {
  LittlefsFile *fil = (LittlefsFile *)fh;

  lfs_soff_t pos = lfs_file_tell(fil->lfs, &fil->fil);
  return pos >= 0 ? pos : 0;
}

static bool littlefs__file_eof(EvfsFile *fh) {
  return littlefs__file_tell(fh) >= littlefs__file_size(fh);
}



static EvfsFileMethods s_littlefs_methods = {
  .m_ctrl     = littlefs__file_ctrl,
  .m_close    = littlefs__file_close,
  .m_read     = littlefs__file_read,
  .m_write    = littlefs__file_write,
  .m_truncate = littlefs__file_truncate,
  .m_sync     = littlefs__file_sync,
  .m_size     = littlefs__file_size,
  .m_seek     = littlefs__file_seek,
  .m_tell     = littlefs__file_tell,
  .m_eof      = littlefs__file_eof
};



// ******************** Directory access methods ********************


static int littlefs__dir_close(EvfsDir *dh) {
  LittlefsDir *dir = (LittlefsDir *)dh;
  return simple_error(lfs_dir_close(dir->fs_data->lfs, &dir->dir));
}


static int littlefs__dir_read(EvfsDir *dh, EvfsInfo *info) {
  LittlefsDir *dir = (LittlefsDir *)dh;
  LittlefsData *fs_data = (LittlefsData *)dir->fs_data;

  int status = lfs_dir_read(dir->fs_data->lfs, &dir->dir, &dir->info);

  // Skip over dir dots if configured
  if(status >= 0 && fs_data->cfg_no_dir_dots) {
    while(status >= 0 && (!strcmp(dir->info.name, ".") || !strcmp(dir->info.name, ".."))) {
      status = lfs_dir_read(dir->fs_data->lfs, &dir->dir, &dir->info);
    }
  }


  memset(info, 0, sizeof(*info));

  if(status >= 0) {
    info->name = dir->info.name;
    info->size = dir->info.size;

    if(dir->info.type & LFS_TYPE_DIR)
      info->type |= EVFS_FILE_DIR;
  } else {
    info->name = NULL;
  }

  return status > 0 ? EVFS_OK : EVFS_DONE;
}


static int littlefs__dir_rewind(EvfsDir *dh) {
  LittlefsDir *dir = (LittlefsDir *)dh;
  lfs_dir_rewind(dir->fs_data->lfs, &dir->dir);
  return EVFS_OK;
}


static EvfsDirMethods s_littlefs_dir_methods = {
  .m_close    = littlefs__dir_close,
  .m_read     = littlefs__dir_read,
  .m_rewind   = littlefs__dir_rewind
};




// ******************** FS access methods ********************

static int littlefs__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;
  LittlefsFile *fil = (LittlefsFile *)fh;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_littlefs_methods;

  if((flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND)) && fs_data->cfg_readonly)
    return EVFS_ERR_DISABLED;

  int lfs_flags = 0;

  if(flags & EVFS_READ)        lfs_flags |= LFS_O_RDONLY; 
  if(flags & EVFS_WRITE)       lfs_flags |= LFS_O_WRONLY;
  if(flags & EVFS_OPEN_OR_NEW) lfs_flags |= LFS_O_CREAT;
  if(flags & EVFS_NO_EXIST)    lfs_flags |= LFS_O_EXCL;
  if(flags & EVFS_OVERWRITE)   lfs_flags |= (LFS_O_TRUNC | LFS_O_CREAT);
  if(flags & EVFS_APPEND)      lfs_flags |= LFS_O_APPEND;

  fil->fs_data = fs_data;
  fil->lfs = fs_data->lfs;
  
  int status;
  
  if(evfs_vfs_path_is_absolute(vfs, path)) {
    status = lfs_file_open(fil->lfs, &fil->fil, path, lfs_flags);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = lfs_file_open(fil->lfs, &fil->fil, abs_path, lfs_flags);
    FREE_ABS(abs_path);
  }

  return translate_error(status);  
}


static int littlefs__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    status = lfs_stat(fs_data->lfs, path, &fs_data->info);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = lfs_stat(fs_data->lfs, abs_path, &fs_data->info);
    FREE_ABS(abs_path);
  }

  memset(info, 0, sizeof(*info));

  if(status >= 0) {  
    info->size = fs_data->info.size;
    if(fs_data->info.type & LFS_TYPE_DIR)
      info->type |= EVFS_FILE_DIR;

  }

  return translate_error(status);
}

static int littlefs__delete(Evfs *vfs, const char *path) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    status = lfs_remove(fs_data->lfs, path);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = lfs_remove(fs_data->lfs, abs_path);
    FREE_ABS(abs_path);
  }

  return simple_error(status);
}


static int littlefs__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, old_path) && evfs_vfs_path_is_absolute(vfs, new_path)) {
    status = lfs_rename(fs_data->lfs, old_path, new_path);
  } else { // Convert relative path
    MAKE_ABS(old_path, abs_old_path);

    // Can't use the macro because of extra cleanup requirement
    char *abs_new_path;
    status = make_absolute_path(vfs, new_path, &abs_new_path, /*force_malloc*/ true);
    if(status != EVFS_OK) {
      FREE_ABS(abs_old_path);
      return status;
    }

    status = lfs_rename(fs_data->lfs, abs_old_path, abs_new_path);
    FREE_ABS(abs_old_path);
    evfs_free(abs_new_path);
  }

  return simple_error(status);
}



static int littlefs__make_dir(Evfs *vfs, const char *path) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    status = lfs_mkdir(fs_data->lfs, path);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = lfs_mkdir(fs_data->lfs, abs_path);
    FREE_ABS(abs_path);
  }

  return translate_error(status);
}


static int littlefs__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  LittlefsDir *dir = (LittlefsDir *)dh;

  memset(dir, 0, sizeof(*dir));
  dh->methods = &s_littlefs_dir_methods;
  dir->fs_data = fs_data;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    status = lfs_dir_open(fs_data->lfs, &dir->dir, path);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = lfs_dir_open(fs_data->lfs, &dir->dir, abs_path);
    FREE_ABS(abs_path);
  }

  return translate_error(status);

}


// Littlefs doesn't handle relative paths so we track the current directory in fs_data
static int littlefs__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;
  AppendRange r = *(AppendRange *)cur_dir;

  range_cat_str(&r, fs_data->cur_dir);
  range_terminate(&r);
  return EVFS_OK;
}



static int littlefs__set_cur_dir(Evfs *vfs, const char *path) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  if(evfs_vfs_path_is_absolute(vfs, path)) { // Just assign absolute paths
    // Confirm the path exists
    if(!evfs__vfs_existing_dir(vfs, path))
      return EVFS_ERR_NO_PATH;
  
    strncpy(fs_data->cur_dir, path, LFS_NAME_MAX-1);
    fs_data->cur_dir[LFS_NAME_MAX-1] = '\0';

  } else { // Path is relative: Join it to the existing directory
    StringRange head, tail, joined;
    
    range_init(&head, fs_data->cur_dir, LFS_NAME_MAX);
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



static int littlefs__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  LittlefsData *fs_data = (LittlefsData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER:
#ifdef USE_LFS_LOCK
      evfs__lock_destroy(&fs_data->lfs_lock);
#endif
      evfs_free(vfs);
      return EVFS_OK; break;

    case EVFS_CMD_SET_READONLY:
      {
        unsigned *v = (unsigned *)arg;
        fs_data->cfg_readonly = !!*v;
      }
      return EVFS_OK; break;

    case EVFS_CMD_SET_NO_DIR_DOTS:
      {
        unsigned *v = (unsigned *)arg;
        fs_data->cfg_no_dir_dots = !!*v;
      }
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
        *v = EVFS_INFO_NAME | EVFS_INFO_SIZE | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    default: return EVFS_ERR_NO_SUPPORT; break;
  }
}



// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])

/*
Register a Littlefs instance

Args:
  vfs_name:      Name of new VFS
  lfs:           Mounted littlefs object
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_littlefs(const char *vfs_name, lfs_t *lfs, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(lfs)) return EVFS_ERR_BAD_ARG;
  Evfs *new_vfs;
  LittlefsData *fs_data;

  // Construct a new VFS
  // We have three objects allocated together [Evfs][LittlefsData][char[]]
  size_t alloc_size = sizeof(*new_vfs) + sizeof(*fs_data) + strlen(vfs_name)+1;
  new_vfs = evfs_malloc(alloc_size);
  if(MEM_CHECK(new_vfs)) return EVFS_ERR_ALLOC;
  memset(new_vfs, 0, alloc_size);

  // Prepare new objects
  fs_data = (LittlefsData *)NEXT_OBJ(new_vfs);
  
  new_vfs->vfs_name = (char *)NEXT_OBJ(fs_data);
  strcpy((char *)new_vfs->vfs_name, vfs_name);

  // Init FS data
  fs_data->lfs = lfs;
  strncpy(fs_data->cur_dir, "/", 2); // Start in root dir

  // Init VFS
  new_vfs->vfs_file_size = sizeof(LittlefsFile);
  new_vfs->vfs_dir_size = sizeof(LittlefsDir);
  new_vfs->fs_data = fs_data;

  // Required methods
  new_vfs->m_open = littlefs__open;
  new_vfs->m_stat = littlefs__stat;
  
  // Optional methods
  new_vfs->m_delete = littlefs__delete;
  new_vfs->m_rename = littlefs__rename;
  new_vfs->m_make_dir = littlefs__make_dir;
  new_vfs->m_open_dir = littlefs__open_dir;
  new_vfs->m_get_cur_dir = littlefs__get_cur_dir;
  new_vfs->m_set_cur_dir = littlefs__set_cur_dir;
  new_vfs->m_vfs_ctrl = littlefs__vfs_ctrl;

#ifdef USE_LFS_LOCK
  if(evfs__lock_init(&fs_data->lfs_lock) != EVFS_OK) {
    evfs_free(new_vfs);
    THROW(EVFS_ERR_INIT);
  }
#endif

  return evfs_register(new_vfs, default_vfs);
}


