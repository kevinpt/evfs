/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Jail shim VFS

  This creates a virtual root in a subdirectory for an undelying VFS similar
  to how chroot() works. This only affects access via the EVFS API that passes
  through the shim. If it isn't default VFS or the underlying FS is accessed
  by name then the path restiction can be bypassed.

  The jail shim can be used as a simplified way to perform operations with
  absolute paths that map into a subdirectory.
------------------------------------------------------------------------------
*/


#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/shim/shim_jail.h"
#include "bsd/string.h"


// Shared buffer needs lock if threading is enabled
#if defined EVFS_USE_THREADING
#  define USE_JAIL_LOCK
#endif


#ifdef USE_JAIL_LOCK
#  define LOCK()    evfs__lock(&shim_data->jail_lock)
#  define UNLOCK()  evfs__unlock(&shim_data->jail_lock)
#else
#  define LOCK()
#  define UNLOCK()
#endif



// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])


typedef struct JailData_s {
  Evfs       *base_vfs;
  const char *vfs_name;
  Evfs       *shim_vfs;

  const char *jail_root;                // Absolute dir for jail in containing VFS
  char        cur_dir[EVFS_MAX_PATH];   // CWD within jailed environment

  char        tmp_path[EVFS_MAX_PATH]; // Shared buffer for unjailed paths
#ifdef USE_JAIL_LOCK
  EvfsLock    jail_lock; // Serialize access to shared tmp_path buffer
#endif
} JailData;

typedef struct JailFile_s {
  EvfsFile  base;
  JailData *shim_data;
  EvfsFile *base_file;
} JailFile;

typedef struct JailDir_s {
  EvfsDir   base;
  JailData *shim_data;
  EvfsDir  *base_dir;
} JailDir;



// Convert a jailed path into a real path on the base VFS
static void unjail_path(Evfs *vfs, const char *path, StringRange *real_path) {
  JailData *shim_data = (JailData *)vfs->fs_data;

  AppendRange real_r = *(AppendRange *)real_path;

  // Start with jail root
  range_cat_str(&real_r, shim_data->jail_root);
  range_cat_char(&real_r, EVFS_DIR_SEP);

  // Convert path to absolute within the jail subtree
  evfs_vfs_path_absolute(vfs, path, (StringRange *)&real_r);

//  DPRINT("Unjail: '%.*s'", RANGE_FMT(real_path));

  // Strip the root component from the path
  StringRange root_r = {0};
  vfs->m_path_root_component(vfs, real_r.start, &root_r);

  if(range_size(&root_r) > 0) {
    // Shift the jailed subpath over the unwanted root
    size_t jailed_len = range_strlen((StringRange *)&real_r) - range_size(&root_r);
    memmove((char *)root_r.start, root_r.end, jailed_len+1);
  }

//  DPRINT("\t\t'%.*s'", RANGE_FMT(real_path));
}



// ******************** File access methods ********************

static int jail__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  int status = fil->base_file->methods->m_ctrl(fil->base_file, cmd, arg);

  return status;
}


static int jail__file_close(EvfsFile *fh) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  int status = fil->base_file->methods->m_close(fil->base_file);

  if(status == EVFS_OK) {
    fil->base.methods = NULL;
  }

  return status;
}


static ptrdiff_t jail__file_read(EvfsFile *fh, void *buf, size_t size) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  ptrdiff_t read = fil->base_file->methods->m_read(fil->base_file, buf, size);

  return read;
}


static ptrdiff_t jail__file_write(EvfsFile *fh, const void *buf, size_t size) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  ptrdiff_t wrote = fil->base_file->methods->m_write(fil->base_file, buf, size);

  return wrote;
}


static int jail__file_truncate(EvfsFile *fh, evfs_off_t size) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  int status = fil->base_file->methods->m_truncate(fil->base_file, size);

  return status;
}


static int jail__file_sync(EvfsFile *fh) {
  JailFile *fil = (JailFile *)fh;
  //JailData *shim_data = fil->shim_data;

  int status = fil->base_file->methods->m_sync(fil->base_file);

  return status;
}



static evfs_off_t jail__file_size(EvfsFile *fh) {
  JailFile *fil = (JailFile *)fh;

  return fil->base_file->methods->m_size(fil->base_file);
}


static int jail__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  JailFile *fil = (JailFile *)fh;
  
  int status = fil->base_file->methods->m_seek(fil->base_file, offset, origin);

  return status;
}


static evfs_off_t jail__file_tell(EvfsFile *fh) {
  JailFile *fil = (JailFile *)fh;

  return fil->base_file->methods->m_tell(fil->base_file);
}


static bool jail__file_eof(EvfsFile *fh) {
  JailFile *fil = (JailFile *)fh;

  bool eof = fil->base_file->methods->m_eof(fil->base_file);

  return eof;
}


static const EvfsFileMethods s_jail_methods = {
  .m_ctrl     = jail__file_ctrl,
  .m_close    = jail__file_close,
  .m_read     = jail__file_read,
  .m_write    = jail__file_write,
  .m_truncate = jail__file_truncate,
  .m_sync     = jail__file_sync,
  .m_size     = jail__file_size,
  .m_seek     = jail__file_seek,
  .m_tell     = jail__file_tell,
  .m_eof      = jail__file_eof
};

// ******************** Directory access methods ********************

static int jail__dir_close(EvfsDir *dh) {
  JailDir *dir = (JailDir *)dh;
  //JailData *shim_data = dir->shim_data;

  int status = dir->base_dir->methods->m_close(dir->base_dir);

  if(status == EVFS_OK) {
    dir->base.methods = NULL;
  }

  return status;
}

static int jail__dir_read(EvfsDir *dh, EvfsInfo *info) {
  JailDir *dir = (JailDir *)dh;
  //JailData *shim_data = dir->shim_data;

  int status = dir->base_dir->methods->m_read(dir->base_dir, info);

  return status;
}

static int jail__dir_rewind(EvfsDir *dh) {
  JailDir *dir = (JailDir *)dh;
  //JailData *shim_data = dir->shim_data;

  int status = dir->base_dir->methods->m_rewind(dir->base_dir);

  return status;
}


static const EvfsDirMethods s_jail_dir_methods = {
  .m_close    = jail__dir_close,
  .m_read     = jail__dir_read,
  .m_rewind   = jail__dir_rewind
};

// ******************** FS access methods ********************


static int jail__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  JailFile *fil = (JailFile *)fh;
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  fil->shim_data = shim_data;

  fil->base_file = (EvfsFile *)NEXT_OBJ(fil);  // We have two objects allocated together [JailFile][<base VFS file size>]

  LOCK();
  StringRange real_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, path, &real_path_r);

  int status = base_vfs->m_open(base_vfs, shim_data->tmp_path, fil->base_file, flags);
  UNLOCK();
  
  if(status == EVFS_OK) {
    // Add methods to make this functional
    if(fil->base_file->methods) {
      fh->methods = &s_jail_methods;
    } else {
      fh->methods = NULL;
      status = EVFS_ERR_INIT;
    }
  } else { // Open failed
    fh->methods = NULL;
  }

  return status;
}



static int jail__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  LOCK();
  StringRange real_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, path, &real_path_r);

  int status = base_vfs->m_stat(base_vfs, shim_data->tmp_path, info);
  UNLOCK();

  return status;
}



static int jail__delete(Evfs *vfs, const char *path) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  LOCK();
  StringRange real_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, path, &real_path_r);

  int status = base_vfs->m_delete(base_vfs, shim_data->tmp_path);
  UNLOCK();

  return status;
}


static int jail__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  LOCK();
  StringRange real_old_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, old_path, &real_old_path_r);

  char real_new_path[EVFS_MAX_PATH];
  StringRange real_new_path_r = RANGE_FROM_ARRAY(real_new_path);
  unjail_path(vfs, new_path, &real_new_path_r);

  int status = base_vfs->m_rename(base_vfs, shim_data->tmp_path, real_new_path);
  UNLOCK();

  return status;
}


static int jail__make_dir(Evfs *vfs, const char *path) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  LOCK();
  StringRange real_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, path, &real_path_r);

  int status = base_vfs->m_make_dir(base_vfs, shim_data->tmp_path);
  UNLOCK();

  return status;
}


static int jail__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  JailDir *dir = (JailDir *)dh;
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  dir->shim_data = shim_data;
  dir->base_dir = (EvfsDir *)NEXT_OBJ(dir);   // We have two objects allocated together [JailDir][<base VFS dir size>]


  LOCK();
  StringRange real_path_r = RANGE_FROM_ARRAY(shim_data->tmp_path);
  unjail_path(vfs, path, &real_path_r);

  int status = base_vfs->m_open_dir(base_vfs, shim_data->tmp_path, dir->base_dir);
  UNLOCK();

  if(status == EVFS_OK) {
    // Add methods to make this functional
    if(dir->base_dir->methods) {
      dh->methods = &s_jail_dir_methods;
    } else {
      dh->methods = NULL;
      status = EVFS_ERR_INIT;
    }

  } else { // Open failed
    dh->methods = NULL;
  }

  return status;
}


// Track the current directory in fs_data
static int jail__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  JailData *fs_data = (JailData *)vfs->fs_data;
  AppendRange r = *(AppendRange *)cur_dir;
  
  range_cat_str(&r, fs_data->cur_dir);
  range_terminate(&r);
  return EVFS_OK;
}



static int jail__set_cur_dir(Evfs *vfs, const char *path) {
  JailData *shim_data = (JailData *)vfs->fs_data;

  if(evfs_vfs_path_is_absolute(vfs, path)) { // Just assign absolute paths
    // Confirm the path exists
    if(!evfs__vfs_existing_dir(vfs, path))
      return EVFS_ERR_NO_PATH;

    strlcpy(shim_data->cur_dir, path, sizeof(shim_data->cur_dir));
    shim_data->cur_dir[sizeof(shim_data->cur_dir)-1] = '\0';

  } else { // Path is relative: Join it to the existing directory
    StringRange head, tail, joined;

    range_init(&head, shim_data->cur_dir, sizeof(shim_data->cur_dir));
    range_init(&tail, (char *)path, strlen(path));

    // Note: evfs__vfs_existing_dir() below is going to take the lock (via jail__stat) so
    //       we have to malloc a buffer.
    size_t joined_size = strlen(shim_data->cur_dir) + 1 + strlen(path) + 1;
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


static int jail__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  // We need special handling for EVFS_CMD_UNREGISTER.
  // It can't pass through since we need to deallocate VFSs in the proper sequence
  // to avoid corrupting the registered VFS linked list.
  if(cmd == EVFS_CMD_UNREGISTER) {
#ifdef USE_JAIL_LOCK
      evfs__lock_destroy(&shim_data->jail_lock);
#endif
      evfs_free(vfs); // Free this trace VFS
      return EVFS_OK;
  }

  // Everything else passes to the underlying VFS
  int status = base_vfs->m_vfs_ctrl(base_vfs, cmd, arg);

  return status;
}


static bool jail__path_root_component(Evfs *vfs, const char *path, StringRange *root) {
  JailData *shim_data = (JailData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  bool is_absolute = base_vfs->m_path_root_component(base_vfs, path, root);

  return is_absolute;
}


/*
Register a jail filesystem shim

Args:
  vfs_name:      Name of new shim
  old_vfs_name:  Existing VFS to wrap with shim
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_jail(const char *vfs_name, const char *old_vfs_name, const char *jail_root, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(old_vfs_name)) return EVFS_ERR_BAD_ARG;

  Evfs *base_vfs, *shim_vfs;
  JailData *shim_data;

  base_vfs = evfs_find_vfs(old_vfs_name);
  if(PTR_CHECK(base_vfs)) return EVFS_ERR_NO_VFS;

  // Convert jail root to absolute
  char abs_root[EVFS_MAX_PATH];
  StringRange abs_root_r = RANGE_FROM_ARRAY(abs_root);
  evfs_vfs_path_absolute(base_vfs, jail_root, &abs_root_r);

  // Construct a new VFS
  // We have four objects allocated together [Evfs][JailData][char[]][char[]]
  size_t shim_size = sizeof(*shim_vfs) + sizeof(*shim_data) + strlen(vfs_name)+1 + strlen(abs_root)+1;
  shim_vfs = evfs_malloc(shim_size);
  if(MEM_CHECK(shim_vfs)) return EVFS_ERR_ALLOC;

  memset(shim_vfs, 0, shim_size);

  shim_data = (JailData *)NEXT_OBJ(shim_vfs);

  shim_vfs->vfs_name = (char *)NEXT_OBJ(shim_data);
  strcpy((char *)shim_vfs->vfs_name, vfs_name);

  shim_data->jail_root = shim_vfs->vfs_name + strlen(vfs_name)+1;
  strcpy((char *)shim_data->jail_root, abs_root);

  shim_data->base_vfs = base_vfs;
  shim_data->vfs_name = shim_vfs->vfs_name;
  shim_data->shim_vfs = shim_vfs;
  strncpy(shim_data->cur_dir, "/", 2); // Start in root dir

  shim_vfs->vfs_file_size = sizeof(JailFile) + base_vfs->vfs_file_size;
  shim_vfs->vfs_dir_size = sizeof(JailDir) + base_vfs->vfs_dir_size;
  shim_vfs->fs_data = shim_data;

  shim_vfs->m_open = jail__open;
  shim_vfs->m_stat = jail__stat;
  shim_vfs->m_delete = jail__delete;
  shim_vfs->m_rename = jail__rename;
  shim_vfs->m_make_dir = jail__make_dir;
  shim_vfs->m_open_dir = jail__open_dir;
  shim_vfs->m_get_cur_dir = jail__get_cur_dir;
  shim_vfs->m_set_cur_dir = jail__set_cur_dir;
  shim_vfs->m_vfs_ctrl = jail__vfs_ctrl;

  shim_vfs->m_path_root_component = jail__path_root_component;

#ifdef USE_JAIL_LOCK
  if(evfs__lock_init(&shim_data->jail_lock) != EVFS_OK) {
    evfs_free(shim_vfs);
    THROW(EVFS_ERR_INIT);
  }
#endif

  return evfs_register(shim_vfs, default_vfs);
}

