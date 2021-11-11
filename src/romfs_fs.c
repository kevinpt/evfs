/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Romfs VFS
  A VFS implementation of Linux Romfs.
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdalign.h>

#include "evfs.h"
#include "evfs_internal.h"

#ifdef EVFS_USE_ROMFS_FAST_INDEX
#  include "evfs/util/dhash.h"
#endif

#include "evfs/romfs_common.h"
#include "evfs/romfs_fs.h"


///////////////////////////////////////////////////////////////////////////////////

// Shared buffer needs lock if threading is enabled
#if defined EVFS_USE_ROMFS_SHARED_BUFFER && defined EVFS_USE_THREADING
#  define USE_ROMFS_LOCK
#endif


#ifdef USE_ROMFS_LOCK
#  define LOCK()    evfs__lock(&fs_data->romfs_lock)
#  define UNLOCK()  evfs__unlock(&fs_data->romfs_lock)
#else
#  define LOCK()
#  define UNLOCK()
#endif




typedef struct RomfsData {
  Evfs       *vfs;

  char      cur_dir[EVFS_MAX_PATH];
#ifdef EVFS_USE_ROMFS_SHARED_BUFFER
  char      abs_path[EVFS_MAX_PATH]; // Shared buffer for building absolute paths
#endif
#ifdef USE_ROMFS_LOCK
  EvfsLock  romfs_lock; // Serialize access to shared abs_path buffer
#endif

  Romfs romfs;

  // VFS config options
  unsigned cfg_no_dir_dots :1; // EVFS_CMD_SET_NO_DIR_DOTS
} RomfsData;


typedef struct RomfsFile {
  EvfsFile      base;
  RomfsData    *fs_data;

  RomfsFileHead hdr;
  evfs_off_t    read_pos;
} RomfsFile;


typedef struct RomfsDir {
  EvfsDir       base;
  RomfsData    *fs_data;

  evfs_off_t    dir_pos;
  evfs_off_t    cur_file_offset; // Iterator position
  RomfsFileHead cur_file;
  bool          is_reset;
} RomfsDir;


// Helper to convert relative paths into absolute for the Romfs API
// Returned path must be freed
static int make_absolute_path(Evfs *vfs, const char *path, char **absolute, bool force_malloc) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;
  *absolute = NULL;

  size_t abs_size;
  char *abs_path = NULL;

#ifdef EVFS_USE_ROMFS_SHARED_BUFFER
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


#ifdef EVFS_USE_ROMFS_SHARED_BUFFER
#  define FREE_ABS(abs_path)  UNLOCK()
#else
#  define FREE_ABS(abs_path)  evfs_free(abs_path)
#endif




// ******************** File access methods ********************

ptrdiff_t romfs_read_rsrc(Romfs *fs, evfs_off_t offset, void *buf, size_t size);

static int romfs__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  RomfsFile *fil = (RomfsFile *)fh;
  RomfsData *fs_data = fil->fs_data;

  switch(cmd) {
    case EVFS_CMD_GET_RSRC_ADDR:
      {
        // NOTE: This is only valid for in-memory resource Romfs
        if(fs_data->romfs.read_data != romfs_read_rsrc)
          return EVFS_ERR_NO_SUPPORT;

        uint8_t **v = (uint8_t **)arg;

        *v = (uint8_t *)fs_data->romfs.ctx + FILE_OFFSET(&fil->hdr) + fil->hdr.header_len;
      }
      return EVFS_OK; break;

    default: return EVFS_ERR_NO_SUPPORT; break;
  }

}


static int romfs__file_close(EvfsFile *fh) {
  RomfsFile *fil = (RomfsFile *)fh;

  memset(&fil->hdr, 0, sizeof(fil->hdr));
  fil->read_pos = 0;

  return EVFS_OK;
}

static ptrdiff_t romfs__file_read(EvfsFile *fh, void *buf, size_t size) {
  RomfsFile *fil = (RomfsFile *)fh;
  RomfsData *fs_data = (RomfsData *)fil->fs_data;

  evfs_off_t remaining = fil->hdr.size - fil->read_pos;
  if(remaining <= 0) return 0;

  if((evfs_off_t)size > remaining)
    size = remaining;

  LOCK();
  evfs_off_t data_offset = FILE_OFFSET(&fil->hdr) + fil->hdr.header_len + fil->read_pos;
  ptrdiff_t rval = romfs_read(&fs_data->romfs, data_offset, buf, size);
  fil->read_pos += size;
  UNLOCK();

  //DPRINT("## READ: @ %ld,  %ld  '%02X'", fil->read_pos-size, size, *((uint8_t *)buf));

  return rval;
}

static ptrdiff_t romfs__file_write(EvfsFile *fh, const void *buf, size_t size) {
  return EVFS_ERR_NO_SUPPORT;
}

static int romfs__file_truncate(EvfsFile *fh, evfs_off_t size) {
  return EVFS_ERR_NO_SUPPORT;
}

static int romfs__file_sync(EvfsFile *fh) {
  return EVFS_OK;
}

static evfs_off_t romfs__file_size(EvfsFile *fh) {
  RomfsFile *fil = (RomfsFile *)fh;
  return fil->hdr.size;
}

static int romfs__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  RomfsFile *fil = (RomfsFile *)fh;

  offset = evfs__absolute_offset(fh, offset, origin);
  if(ASSERT(offset >= 0, "Invalid offset")) return EVFS_ERR_INVALID;

  if(offset > (evfs_off_t)fil->hdr.size)
    offset = fil->hdr.size;

  fil->read_pos = offset;

  return EVFS_OK;
}

static evfs_off_t romfs__file_tell(EvfsFile *fh) {
  RomfsFile *fil = (RomfsFile *)fh;
  return fil->read_pos;
}

static bool romfs__file_eof(EvfsFile *fh) {
  RomfsFile *fil = (RomfsFile *)fh;
  return fil->read_pos >= (evfs_off_t)fil->hdr.size;
}



static EvfsFileMethods s_romfs_methods = {
  .m_ctrl     = romfs__file_ctrl,
  .m_close    = romfs__file_close,
  .m_read     = romfs__file_read,
  .m_write    = romfs__file_write,
  .m_truncate = romfs__file_truncate,
  .m_sync     = romfs__file_sync,
  .m_size     = romfs__file_size,
  .m_seek     = romfs__file_seek,
  .m_tell     = romfs__file_tell,
  .m_eof      = romfs__file_eof
};



// ******************** Directory access methods ********************


static int romfs__dir_close(EvfsDir *dh) {
  RomfsDir *dir = (RomfsDir *)dh;

  memset(&dir->cur_file, 0, sizeof(dir->cur_file));
  dir->dir_pos = 0;

  return EVFS_OK;
}


static int romfs__dir_read(EvfsDir *dh, EvfsInfo *info) {
  RomfsDir *dir = (RomfsDir *)dh;
  RomfsData *fs_data = (RomfsData *)dir->fs_data;

  int status;
  evfs_off_t next_entry;

  // Advance to next directory
  if(dir->is_reset) { // Init iterator
    romfs_read_file_header(&fs_data->romfs, dir->dir_pos, &dir->cur_file);
    next_entry = dir->cur_file.spec_info;
    dir->is_reset = false;

    if(fs_data->cfg_no_dir_dots) { // Skip over hard links
      romfs_read_file_header(&fs_data->romfs, next_entry, &dir->cur_file); // Skip "."
      romfs_read_file_header(&fs_data->romfs, FILE_OFFSET(&dir->cur_file), &dir->cur_file); // Skip ".."
      next_entry = FILE_OFFSET(&dir->cur_file);
    }

  } else { // Already initialized
    next_entry = FILE_OFFSET(&dir->cur_file);
  }

  if(next_entry > 0) {
    dir->cur_file_offset = next_entry;
    status = romfs_read_file_header(&fs_data->romfs, next_entry, &dir->cur_file) ? EVFS_OK : EVFS_DONE;

  } else { // Iteration complete
    status = EVFS_DONE;
  }


  memset(info, 0, sizeof(*info));

  if(status == EVFS_OK) {
    info->name = dir->cur_file.file_name;
    info->size = dir->cur_file.size;

    int file_type = FILE_TYPE(&dir->cur_file);
    if(file_type == FILE_TYPE_DIRECTORY || file_type == FILE_TYPE_HARD_LINK)
      info->type |= EVFS_FILE_DIR;
  }

  return status;
}


static int romfs__dir_rewind(EvfsDir *dh) {
  RomfsDir *dir = (RomfsDir *)dh;

  dir->is_reset = true;
  return EVFS_OK;
}


static EvfsDirMethods s_romfs_dir_methods = {
  .m_close    = romfs__dir_close,
  .m_read     = romfs__dir_read,
  .m_rewind   = romfs__dir_rewind
};




// ******************** FS access methods ********************


static inline int romfs__lookup_path(Evfs *vfs, RomfsData *fs_data, const char *path,
                                    RomfsFileHead *hdr) {
  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {
    status = romfs_lookup_abs_path(&fs_data->romfs, path, hdr);

  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = romfs_lookup_abs_path(&fs_data->romfs, abs_path, hdr);
    FREE_ABS(abs_path);
  }

  return status;
}


static int romfs__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;
  RomfsFile *fil = (RomfsFile *)fh;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_romfs_methods;

  if(flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND))
    return EVFS_ERR_NO_SUPPORT;


  fil->fs_data = fs_data;

  int status = romfs__lookup_path(vfs, fs_data, path, &fil->hdr);

  // Must be a plain file
  int file_type = FILE_TYPE(&fil->hdr);
  if(file_type != FILE_TYPE_REGULAR_FILE) {
    status = EVFS_ERR_NO_FILE;
  }

  return status;
}


static int romfs__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;

  RomfsFileHead hdr;

  int status = romfs__lookup_path(vfs, fs_data, path, &hdr);

  memset(info, 0, sizeof(*info));

  if(status >= 0) {
    info->size = hdr.size;

    int file_type = FILE_TYPE(&hdr);
    if(file_type == FILE_TYPE_DIRECTORY || file_type == FILE_TYPE_HARD_LINK)
      info->type |= EVFS_FILE_DIR;
  }

  return status;
}



static int romfs__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;
  RomfsDir  *dir = (RomfsDir *)dh;

  memset(dir, 0, sizeof(*dir));
  dh->methods = &s_romfs_dir_methods;
  dir->fs_data = fs_data;

  RomfsFileHead hdr;

  int status = romfs__lookup_path(vfs, fs_data, path, &hdr);

  int file_type = FILE_TYPE(&hdr);
  if(status == EVFS_OK && file_type != FILE_TYPE_DIRECTORY) {
    status = EVFS_ERR_NO_PATH;
  }

  if(status == EVFS_OK) {
    dir->dir_pos = FILE_OFFSET(&hdr);
    dir->is_reset = true;
  }

  return status;
}


static int romfs__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;
  AppendRange r = *(AppendRange *)cur_dir;

  range_cat_str(&r, fs_data->cur_dir);
  range_terminate(&r);
  return EVFS_OK;
}



static int romfs__set_cur_dir(Evfs *vfs, const char *path) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;

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

    // We must normalize now because the fast index can't handle denormal paths
    range_init(&joined, joined_path, joined_size);
    evfs_vfs_path_normalize(vfs, joined_path, &joined);

    // Confirm the path exists
    if(!evfs__vfs_existing_dir(vfs, joined_path)) {
      evfs_free(joined_path);
      return EVFS_ERR_NO_PATH;
    }

    // Overwrite old cur_dir
    range_init(&head, fs_data->cur_dir, EVFS_MAX_PATH);
    range_cat_str((AppendRange *)&head, joined_path);

    evfs_free(joined_path);
  }

  return EVFS_OK;
}



static int romfs__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER:
      romfs_unmount(&fs_data->romfs);
#ifdef USE_ROMFS_LOCK
      evfs__lock_destroy(&fs_data->romfs_lock);
#endif

      evfs_free(vfs);
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


static int evfs__register_romfs_cfg(const char *vfs_name, RomfsConfig *cfg, bool default_vfs) {
  Evfs      *new_vfs;
  RomfsData *fs_data;

  // Construct a new VFS
  // We have three objects allocated together [Evfs][RomfsData][char[]]
  size_t alloc_size = sizeof(*new_vfs) + sizeof(*fs_data) + strlen(vfs_name)+1;
  new_vfs = evfs_malloc(alloc_size);
  if(MEM_CHECK(new_vfs)) return EVFS_ERR_ALLOC;
  memset(new_vfs, 0, alloc_size);

  // Prepare new objects
  fs_data = (RomfsData *)NEXT_OBJ(new_vfs);
  
  new_vfs->vfs_name = (char *)NEXT_OBJ(fs_data);
  strcpy((char *)new_vfs->vfs_name, vfs_name);

  // Init FS data
  fs_data->vfs = new_vfs;

  strncpy(fs_data->cur_dir, "/", 2); // Start in root dir

  // Init VFS
  new_vfs->vfs_file_size = sizeof(RomfsFile);
  new_vfs->vfs_dir_size = sizeof(RomfsDir);
  new_vfs->fs_data = fs_data;

  // Required methods
  new_vfs->m_open = romfs__open;
  new_vfs->m_stat = romfs__stat;
  
  // Optional methods
  new_vfs->m_open_dir = romfs__open_dir;
  new_vfs->m_get_cur_dir = romfs__get_cur_dir;
  new_vfs->m_set_cur_dir = romfs__set_cur_dir;
  new_vfs->m_vfs_ctrl = romfs__vfs_ctrl;

#ifdef USE_ROMFS_LOCK
  if(evfs__lock_init(&fs_data->romfs_lock) != EVFS_OK) {
    evfs_free(new_vfs);
    THROW(EVFS_ERR_INIT);
  }
#endif


/*  RomfsConfig cfg = {
    .ctx        = image,
    .total_size = evfs_file_size(image),
    .read_data  = romfs_read_image,
    .unmount    = romfs_unmount_image
  };*/
  int status = romfs_init(&fs_data->romfs, cfg);
  if(status != EVFS_OK)
    return status;

  return evfs_register(new_vfs, default_vfs);
}



// Callbacks for image based Romfs
static void romfs_unmount_image(Romfs *fs) {
  EvfsFile *image = (EvfsFile *)fs->ctx;
  evfs_file_close(image);
}

static ptrdiff_t romfs_read_image(Romfs *fs, evfs_off_t offset, void *buf, size_t size) {
  EvfsFile *image = (EvfsFile *)fs->ctx;
  evfs_file_seek(image, offset, EVFS_SEEK_TO);
  return evfs_file_read(image, buf, size);
}


/*
Register a Romfs instance using an image file

Args:
  vfs_name:      Name of new VFS
  image:         Mounted Romfs image
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_romfs(const char *vfs_name, EvfsFile *image, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(image)) return EVFS_ERR_BAD_ARG;

  RomfsConfig cfg = {
    .ctx        = image,
    .total_size = evfs_file_size(image),
    .read_data  = romfs_read_image,
    .unmount    = romfs_unmount_image
  };

  return evfs__register_romfs_cfg(vfs_name, &cfg, default_vfs);
}



// Callbacks for resource based Romfs
static void romfs_unmount_rsrc(Romfs *fs) {
}

ptrdiff_t romfs_read_rsrc(Romfs *fs, evfs_off_t offset, void *buf, size_t size) {
  uint8_t *fs_base = (uint8_t *)fs->ctx;

  if(offset >= fs->total_size || offset < 0)
    return EVFS_ERR_OVERFLOW;

  evfs_off_t remain = fs->total_size - offset;
  size = MIN((evfs_off_t)size, remain);

  memcpy(buf, fs_base + offset, size);

  return size;
}


/*
Register a Romfs instance using an in-memory resource array

Args:
  vfs_name:      Name of new VFS
  resource:      Array of Romfs resource data
  resource_len:  Length of the resource array
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_rsrc_romfs(const char *vfs_name, const uint8_t *resource, size_t resource_len, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(resource)) return EVFS_ERR_BAD_ARG;

  RomfsConfig cfg = {
    .ctx        = (void *)resource,
    .total_size = resource_len,
    .read_data  = romfs_read_rsrc,
    .unmount    = romfs_unmount_rsrc
  };

  return evfs__register_romfs_cfg(vfs_name, &cfg, default_vfs);
}

