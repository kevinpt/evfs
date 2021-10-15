/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  This implements a common OOP wrapper for basic filesystem operations. It is
  used in conjunction with various backends to access files in a uniform way.
------------------------------------------------------------------------------
*/

//#include <stdio.h>
#include <string.h>

#include "evfs.h"
#include "evfs_internal.h"

#include "evfs/util/glob.h"

// List of registered VFSs
static Evfs *s_vfs_list = NULL;
static Evfs *s_default_vfs = NULL;


// ******************** Threading support ********************
#ifdef EVFS_USE_THREADING

// Library-wide lock controlling updates to VFS linked list
static EvfsLock s_evfs_lock = LOCK_INITIALIZER;

#  define LOCK()    evfs__lock(&s_evfs_lock)
#  define UNLOCK()  evfs__unlock(&s_evfs_lock)

#else
#  define LOCK()
#  define UNLOCK()
#endif

// ******************** Initialization ********************

static bool s_evfs_initialized = false;

#ifdef EVFS_USE_ATEXIT
static void evfs__lib_shutdown(void) {
  evfs_unregister_all();
#  ifdef EVFS_USE_THREADING
  evfs__lock_destroy(&s_evfs_lock);
#  endif
}
#endif

// C11 threads don't have a static initializer so we need a runtime fallback
// This is called by the thread library specific implementation of evfs__init_once().
void evfs__lib_init(void) {
#ifdef EVFS_USE_THREADING
  evfs__lock_init(&s_evfs_lock);
#endif
#ifdef EVFS_USE_ATEXIT
  atexit(evfs__lib_shutdown);
#endif

  s_evfs_initialized = true;
}


/*
Initialize the EVFS library
*/
void evfs_init(void) {
#ifdef EVFS_USE_THREADING
  evfs__init_once();
#else
  evfs__lib_init();
#endif
}



// ******************** Debugging support ********************


// Print error message
void evfs__err_printf(const char *fname, int line, const char *msg, ...) {
  va_list args;

#ifdef EVFS_USE_ANSI_COLOR
  fprintf(stderr, A_BRED "EVFS error in %s %d: ", fname, line);
#else
  fprintf(stderr, "EVFS error in %s %d: ", fname, line);
#endif
  // Note: splitting this into two prints could produce interleaved output
  // on threaded platforms
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);

#ifdef EVFS_USE_ANSI_COLOR
  fprintf(stderr, A_NONE); // No color
#endif
}

void evfs__dbg_printf(const char *msg, ...) {
  va_list args;

  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
}


// Expand the error and command X-macros into switch bodies
#define EVFS_NAME_CASE(E, V)  case E: return #E; break;

/*
Translate an error code into a string

Args:
  err: EVFS error code

Returns:
  The corresponding string or "<unknown>"
*/
const char *evfs_err_name(int err) {
  switch(err) {
    EVFS_ERR_LIST(EVFS_NAME_CASE);
    default: break;
  }

  return "<unknown>";
}


/*
Translate a command code into a string

Args:
  cmd: EVFS command code

Returns:
  The corresponding string or "<unknown>"
*/
const char *evfs_cmd_name(int cmd) {
  switch(cmd) {
    EVFS_CMD_LIST(EVFS_NAME_CASE);
    default: break;
  }

  return "<unknown>";
}


// ******************** Internal use ********************


// True when path is a directory
bool evfs__vfs_existing_dir(Evfs *vfs, const char *path) {
  EvfsInfo info;
  if(vfs->m_stat(vfs, path, &info) == EVFS_OK) {
    return info.type & EVFS_FILE_DIR;
  }

  return false;
}


// Common offset conversion routine for implementations of m_file_seek()
evfs_off_t evfs__absolute_offset(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  // Compute absolute offset from start of file
  switch(origin) {
    case EVFS_SEEK_TO: // No change
      break;

    case EVFS_SEEK_REL:
      offset += evfs_file_tell(fh);
      break;

    case EVFS_SEEK_REV:
      offset = evfs_file_size(fh) - offset;
      break;

    default:
      break;
  }

  if(offset < 0)
    offset = 0;

  return offset;
} 


// ******************** Defaults for optional VFS methods ********************

static int default__delete(Evfs *vfs, const char *path) {
  return EVFS_ERR_NO_SUPPORT;
}

static int default__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  return EVFS_ERR_NO_SUPPORT;
}

static int default__make_dir(Evfs *vfs, const char *path) {
  return EVFS_ERR_NO_SUPPORT;
}

static int default__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  return EVFS_ERR_NO_SUPPORT;
}



static int default__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  return EVFS_ERR_NO_SUPPORT;
}

static int default__set_cur_dir(Evfs *vfs, const char *path) {
  return EVFS_ERR_NO_SUPPORT;
}

static int default__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  return EVFS_ERR_NO_SUPPORT;
}


static bool default__path_root_component(Evfs *vfs, const char *path, StringRange *root) {
  // Only handles paths with one or more separators in the root position
  // Does not deal with DOS-style drive letters

  int leading_seps = strspn(path, EVFS_PATH_SEPS);

  range_init(root, (char *)path, leading_seps);

  return leading_seps > 0; // Report absolute path
}



// ******************** VFS registration ********************


/*
Search for a VFS by name

Args:
  vfs_name: VFS to search for

Returns:
  The matching VFS object or NULL
*/
Evfs *evfs_find_vfs(const char *vfs_name) {
  if(PTR_CHECK(vfs_name)) return NULL;
  Evfs *rval = NULL;

  LOCK();
  Evfs *cur_vfs = s_vfs_list;

  while(cur_vfs) {
    if(!strcmp(cur_vfs->vfs_name, vfs_name))
      rval = cur_vfs;

    cur_vfs = cur_vfs->next;
  }
  UNLOCK();

  return rval;
}


// Find a VFS by name or return the default if none found
static Evfs *evfs__get_vfs(const char *vfs_name) {
  Evfs *vfs = NULL;

  if(vfs_name)
    vfs = evfs_find_vfs(vfs_name);

  if(!vfs) // No name provided or not found in registered list
    vfs = s_default_vfs;

  return vfs;
}


/*
Get the name of a VFS object

Args:
  vfs: The VFS to get name from

Returns:
  The name this VFS is registered under
*/
const char *evfs_vfs_name(Evfs *vfs) {
  if(PTR_CHECK(vfs)) return "";
  return vfs->vfs_name;
}


/*
Get the name of the default VFS object

Returns:
  The name of the default VFS
*/
const char *evfs_default_vfs_name(void) {
  return evfs_vfs_name(evfs__get_vfs(NULL));
}


/*
Register a new VFS or change the default status of an existing VFS

If the VFS has already been registered the make_default argument will
update the status of this VFS.

If there is only one registered VFS, make_default is ignored.

This will fail if evfs_init() hasn't been called.

Args:
  vfs:           The new VFS to register
  make_default:  Set this VFS to be default or not

Returns:
  EVFS_OK on success
*/
int evfs_register(Evfs *vfs, bool make_default) {
  bool new_vfs = false;

  if(PTR_CHECK(vfs)) return EVFS_ERR_BAD_ARG;

  // Check if evfs_init() was called
  if(!s_evfs_initialized) THROW(EVFS_ERR_INIT);


  if(s_vfs_list) { // Prepend entry to existing list
    // Check if this VFS is already registered
    Evfs *existing_vfs = evfs_find_vfs(vfs->vfs_name);

    LOCK();
    if(!existing_vfs) { // Add a New VFS
      vfs->next = s_vfs_list;
      s_vfs_list = vfs;
      new_vfs = true;
    } else {
      vfs = existing_vfs; // Make sure we use the correct object already in the list
    }

    if(make_default) {
      s_default_vfs = vfs;
    } else if(s_default_vfs == vfs) {
      // Find another VFS to be the default
      Evfs *cur_vfs = s_vfs_list;
      while(cur_vfs) {
        if(cur_vfs != vfs) { // This is our new default
          s_default_vfs = cur_vfs;
          break;
        }
        cur_vfs = cur_vfs->next;
      }

      if(!cur_vfs) // No alternate VFS was found
        s_default_vfs = vfs;
    }

    if(!new_vfs) {
      UNLOCK();
    }

  } else { // First VFS entry
    LOCK();
    s_default_vfs = vfs; // First entry must be the default
    s_vfs_list = vfs;

    new_vfs = true;
  }


  if(new_vfs) {
    // Fill in any missing optional methods
    if(!vfs->m_delete)   vfs->m_delete = default__delete;
    if(!vfs->m_rename)   vfs->m_rename = default__rename;
    if(!vfs->m_make_dir) vfs->m_make_dir = default__make_dir;
    if(!vfs->m_open_dir) vfs->m_open_dir = default__open_dir;

    if(!vfs->m_get_cur_dir)  vfs->m_get_cur_dir = default__get_cur_dir;
    if(!vfs->m_set_cur_dir)  vfs->m_set_cur_dir = default__set_cur_dir;

    if(!vfs->m_vfs_ctrl)     vfs->m_vfs_ctrl = default__vfs_ctrl;

    if(!vfs->m_path_root_component) vfs->m_path_root_component = default__path_root_component;
    UNLOCK();
  }

  return EVFS_OK;
}


/*
Unregister a VFS object

Args:
  vfs: The VFS to remove

Returns:
  EVFS_OK on success
*/
int evfs_unregister(Evfs *vfs) {
  if(PTR_CHECK(vfs)) return EVFS_ERR_BAD_ARG;

  LOCK();
  Evfs *cur_vfs = s_vfs_list;
  Evfs *prev_vfs = NULL;
  int rval = EVFS_ERR_NO_VFS;

  // Find the VFS an unlink it
  while(cur_vfs) {
    if(cur_vfs == vfs) {

      // Unlink this VFS
      if(prev_vfs)
        prev_vfs->next = cur_vfs->next;
      else
        s_vfs_list = cur_vfs->next;

      cur_vfs->next = NULL;

      // Notify the VFS it is unregistered
      vfs->m_vfs_ctrl(vfs, EVFS_CMD_UNREGISTER, NULL);

      if(s_default_vfs == vfs) // Set new default if needed
        s_default_vfs = s_vfs_list;

      rval = EVFS_OK;
      break;
    }

    prev_vfs = cur_vfs;
    cur_vfs = cur_vfs->next;
  }
  UNLOCK();

  if(rval == EVFS_OK)
    return rval;

  // VFS wasn't found
  THROW(rval);
}


/*
Unregister all registered VFS objects
*/
void evfs_unregister_all(void) {
  Evfs *cur_vfs = s_vfs_list;
  Evfs *next_vfs;

  while(cur_vfs) {
    next_vfs = cur_vfs->next;
    evfs_unregister(cur_vfs);
    cur_vfs = next_vfs;
  }
}


// ******************** FS access methods ********************


/*
Open a file

Args:
  path:     Filesystem path to the file
  fh:       Handle for successfully opened file
  flags:    Open mode flags
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_open_ex(const char *path, EvfsFile **fh, int flags, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return evfs_vfs_open(vfs, path, fh, flags);
}


/*
Open a file from a VFS object

Args:
  vfs:      The VFS to open on
  path:     Filesystem path to the file
  fh:       Handle for successfully opened file
  flags:    Open mode flags

Returns:
  EVFS_OK on success
*/
int evfs_vfs_open(Evfs *vfs, const char *path, EvfsFile **fh, int flags) {
  *fh = (EvfsFile *)evfs_malloc(vfs->vfs_file_size);
  if(MEM_CHECK(*fh)) return EVFS_ERR_ALLOC;

  int rval = vfs->m_open(vfs, path, *fh, flags);

  if(rval != EVFS_OK) {
    evfs_free(*fh);
    *fh = NULL;
  }

  return rval;
}

/*
Get file or directory status

Different VFS backends may only support a partial set of EvfsInfo fields.
Use the EVFS_CMD_GET_STAT_FIELDS command with evfs_vfs_ctrl() to query
which fields are valid on a particular VFS. Unsupported fields will be 0.

Args:
  path:     Filesystem path to the file
  info:     Information reported on the file
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_stat_ex(const char *path, EvfsInfo *info, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(info)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_stat(vfs, path, info);
}



/*
Test if a file exists

Args:
  path:     Filesystem path to the file
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  true if the file exists
*/
bool evfs_existing_file_ex(const char *path, const char *vfs_name) {
  EvfsInfo info;

  int status = evfs_stat_ex(path, &info, vfs_name);
  if(status != EVFS_OK)
    return false;

  return info.type == 0;
}


/*
Test if a directory exists

Args:
  path:     Filesystem path to the directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  true if the file exists
*/
bool evfs_existing_dir_ex(const char *path, const char *vfs_name) {
  EvfsInfo info;

  int status = evfs_stat_ex(path, &info, vfs_name);
  if(status != EVFS_OK)
    return false;

  return (info.type & EVFS_FILE_DIR) != 0;
}


/*
Delete a file or directory

Args:
  path:     Filesystem path to the file
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_delete_ex(const char *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_delete(vfs, path);
}

/*
Rename a file or directory

No validation or transformation is made to the path arguments. Absolute paths
should match the same parent directory or this is likely to fail.

Args:
  old_path: Filesystem path to existing file
  new_path: New path to the file
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_rename_ex(const char *old_path, const char *new_path, const char *vfs_name) {
  if(PTR_CHECK(old_path) || PTR_CHECK(new_path)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_rename(vfs, old_path, new_path);
}

/*
Make a new directory

Args:
  path:     Filesystem path to the directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_make_dir_ex(const char *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_make_dir(vfs, path);
}


/*
Make a complete path to a nested directory

Any missing directories in the path will be created.

Args:
  path:     Filesystem path to a directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_make_path_ex(const char *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return EVFS_ERR_BAD_ARG;

  StringRange path_r;
  range_init(&path_r, (char *)path, strlen(path)+1);
  return evfs_make_path_range_ex(&path_r, vfs_name);
}


/*
Make a complete path to a nested directory

Any missing directories in the path will be created.

This variant takes a StringRange object as the path. This allows the directory
portion of a file path as generated by evfs_path_dirname() to be referenced as
a substring without making a copy.

Args:
  path:     Filesystem path to a directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_make_path_range_ex(StringRange *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  size_t path_len = range_size(path);
  int rval = EVFS_OK;

  char *cur_path = evfs_malloc(path_len+1); // Buffer to hold sub-paths
  if(MEM_CHECK(cur_path)) return EVFS_ERR_ALLOC;

  // Use string range to concatenate each path segment
  AppendRange cur_path_r;
  range_init(&cur_path_r, cur_path, path_len+1);

  size_t limit = path_len;

  // Add root separator for absolute paths
  StringRange root;
  bool is_absolute = vfs->m_path_root_component(vfs, path->start, &root);
  if(is_absolute) {
    // Skip over the root component
    path->start += range_size(&root);
    limit -= range_size(&root);
    range_cat_range(&cur_path_r, &root); // Add root component to the buffer
  }


  // Iterate over each segment of path creating any non-existant directories
  StringRange token;
  bool new_tok = range_token_limit(path->start, EVFS_PATH_SEPS, &token, &limit);
  while(new_tok) {
    range_cat_range(&cur_path_r, &token); // Append new token
    EvfsInfo info;
    int err = vfs->m_stat(vfs, cur_path, &info);

    if(err == EVFS_ERR_NO_FILE) { // Path segment missing
      err = vfs->m_make_dir(vfs, cur_path);
      if(err != EVFS_OK) { // New segment not made
        rval = err;
        goto cleanup;
      }
    } else if(err != EVFS_OK) { // Can't get status of segment
      rval = err;
      goto cleanup;
    }

    range_cat_char(&cur_path_r, EVFS_DIR_SEP);
    new_tok = range_token_limit(NULL, EVFS_PATH_SEPS, &token, &limit);
  }

cleanup:
  evfs_free(cur_path);
  return rval;
}


/*
Open a directory

Args:
  path:     Filesystem path to the directory
  dh:       Handle for successfully opened directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_open_dir_ex(const char *path, EvfsDir **dh, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(dh)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  if(vfs->m_open_dir == default__open_dir) return EVFS_ERR_NO_SUPPORT; // Avoid wasted malloc()


  *dh = (EvfsDir *)evfs_malloc(vfs->vfs_dir_size);
  if(MEM_CHECK(*dh)) return EVFS_ERR_ALLOC;

  int rval = vfs->m_open_dir(vfs, path, *dh);

  if(rval != EVFS_OK) {
    evfs_free(*dh);
    *dh = NULL;
  }

  return rval;
}


/*
Open a directory from a VFS object

Args:
  vfs:      The VFS to open on
  path:     Filesystem path to the directory
  dh:       Handle for successfully opened directory

Returns:
  EVFS_OK on success
*/
int evfs_vfs_open_dir(Evfs *vfs, const char *path, EvfsDir **dh) {
  if(vfs->m_open_dir == default__open_dir) return EVFS_ERR_NO_SUPPORT; // Avoid wasted malloc()


  *dh = (EvfsDir *)evfs_malloc(vfs->vfs_dir_size);
  if(MEM_CHECK(*dh)) return EVFS_ERR_ALLOC;

  int rval = vfs->m_open_dir(vfs, path, *dh);

  if(rval != EVFS_OK) {
    evfs_free(*dh);
    *dh = NULL;
  }

  return rval;
}


/*
Get the current working directory for a VFS

Note that EVFS does not have any mechanism for handling DOS/Windows-style
drive volumes. The reported working directory will be for the active volume.

The returned StringRange should not be modified as it may point into internal
EVFS data structures.

Args:
  cur_dir:  Reference to the current working directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_get_cur_dir_ex(StringRange *cur_dir, const char *vfs_name) {
  if(PTR_CHECK(cur_dir)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_get_cur_dir(vfs, cur_dir);
}


/*
Set the current working directory for a VFS

Relative paths will be based in this directory until it is changed.

Args:
  path:     Path to the new working directory
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_set_cur_dir_ex(const char *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_set_cur_dir(vfs, path);
}


/*
Generic configuration control for a VFS

See the definition of commands in evfs.h for the expected type to pass as arg

Args:
  cmd:      Command number for the operation to perform
  arg:      Variable argument data to write or read associated with the command
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_vfs_ctrl_ex(int cmd, void *arg, const char *vfs_name) {
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_vfs_ctrl(vfs, cmd, arg);
}


// Minimum size of internal transfer buffer for copied data
#define MIN_COPY_BUF_SIZE 64

/*
Copy contents of an open file to a new file

This allows you to transfer files across different VFSs.

Args:
  dest_path:  Path to the new copy
  fh:         Open file to copy from
  buf:        Buffer to use for transfers. Use NULL to malloc a temp buffer.
  buf_size:   Size of buf array. When buf is NULL this is the size to allocate.
  vfs_name:   VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_copy_to_file_ex(const char *dest_path, EvfsFile *fh, char *buf, size_t buf_size, const char *vfs_name) {
  if(PTR_CHECK(dest_path) || PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;

  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  EvfsFile *dest_fh;
  int flags = EVFS_WRITE | EVFS_NO_EXIST;

  int rval = evfs_vfs_open(vfs, dest_path, &dest_fh, flags);
  if(rval != EVFS_OK) return rval;


  bool alloc_buf = false;

  if(!buf) { // Allocate a buffer
    buf_size = MAX(buf_size, MIN_COPY_BUF_SIZE);
    buf = evfs_malloc(buf_size);
    if(MEM_CHECK(buf)) {
      rval = EVFS_ERR_ALLOC;
      goto cleanup;
    }

    alloc_buf = true;
  }

  size_t copy_size = evfs_file_size(fh);

  while(copy_size > 0) {
    size_t read_size = MIN(copy_size, buf_size);
    int read = evfs_file_read(fh, buf, read_size);
    if(read <= 0) {
      rval = read < 0 ? read : EVFS_ERR_IO;
      break;
    }

    int wrote = evfs_file_write(dest_fh, buf, read);
    if(wrote != read) {
      rval = wrote < 0 ? wrote : EVFS_ERR_IO;
      break;
    }

    copy_size -= read;
  }


  if(alloc_buf)
    evfs_free(buf);

cleanup:
  evfs_file_close(dest_fh);

  return rval;
}


// ******************** Path operations ********************

/*
Get the root portion of a path

This is a virtual method that calls into a VFS specific implementation to
handle different filesystem path formats. On POSIX style systems the
root component of absolute paths is a leading sequence of one or more '/'
chars and nothing for relative paths. On DOS/Windows filesystems the root
component may also have a colon separated volume letter or number on
absolute or relative paths.

Args:
  path:     Path to extract root from
  root:     Substring of path corresponding to root or empty if none found
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  true when the path is absolute
*/
bool evfs_path_root_component_ex(const char *path, StringRange *root, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(root)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return vfs->m_path_root_component(vfs, path, root);
}


/*
Get the directory portion of a path

This copies the behavior of Python os.path.dirname()

Args:
  path:     Path to extract dirname from
  head:     Substring of path corresponding to the dirname
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_path_dirname_ex(const char *path, StringRange *head, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(head)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return evfs_vfs_path_dirname(vfs, path, head);
}


/*
Join two paths

Args:
  head:     Substring of left portion to join
  tail:     Substring of right portion to join
  joined:   Substring of output string. This can be the same as the head substring.
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_path_join_ex(StringRange *head, StringRange *tail, StringRange *joined, const char *vfs_name) {
  if(PTR_CHECK(head) || PTR_CHECK(tail) || PTR_CHECK(joined)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return evfs_vfs_path_join(vfs, head, tail, joined);
}

/*
Join two paths using char strings

Args:
  head:     Substring of left portion to join
  tail:     Substring of right portion to join
  joined:   Substring of output string. This can be the same as the head path.
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_path_join_str_ex(const char *head, const char *tail, StringRange *joined, const char *vfs_name) {
  if(PTR_CHECK(head) || PTR_CHECK(tail) || PTR_CHECK(joined)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  StringRange head_r, tail_r;
  range_init(&head_r, head, strlen(head));
  range_init(&tail_r, tail, strlen(tail));

  return evfs_vfs_path_join(vfs, &head_r, &tail_r, joined);
}

/*
Normalize a path

This applies the following transformations:
  Any root component is reduced to its minimal form.
  Consecutive separators are merged into one
  All separators after root component are converted to EVFS_DIR_SEP
  ./ segments are removed
  ../ segments are removed with the preceeding segment
  Trailing slashes are removed

Args:
  path:       Path to be normalized
  normalized: Normalized output path. This can be the same string as path
  vfs_name:   VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_path_normalize_ex(const char *path, StringRange *normalized, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(normalized)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return evfs_vfs_path_normalize(vfs, path, normalized);
}


/*
Convert a path to absolute form

If the path is already absolute it is normalized. Otherwise it is joined
to the current working directory and normalized.

Args:
  path:     Path to become absolute
  absolute: Absolute output path. This can be the same string as path
  vfs_name: VFS to work on. Use default VFS if NULL

Returns:
  EVFS_OK on success
*/
int evfs_path_absolute_ex(const char *path, StringRange *absolute, const char *vfs_name) {
  if(PTR_CHECK(path) || PTR_CHECK(absolute)) return EVFS_ERR_BAD_ARG;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) THROW(EVFS_ERR_NO_VFS);

  return evfs_vfs_path_absolute(vfs, path, absolute);
}

/*
Detect if a path is absolute on the supplied VFS

Args:
  path:       Path to test
  vfs_name:   VFS to work on. Use default VFS if NULL

Returns:
  true if path is absolute
*/
bool evfs_path_is_absolute_ex(const char *path, const char *vfs_name) {
  if(PTR_CHECK(path)) return false;
  Evfs *vfs = evfs__get_vfs(vfs_name);
  if(!vfs) return false;

  StringRange root;
  return vfs->m_path_root_component(vfs, path, &root);
}

/*
Detect if a path is absolute on the supplied VFS

This variant is for fs wrappers to use without a name lookup

Args:
  vfs:      The VFS for the path
  path:     Path to test

Returns:
  true if path is absolute
*/
bool evfs_vfs_path_is_absolute(Evfs *vfs, const char *path) {
  if(PTR_CHECK(path)) return false;

  StringRange root;
  return vfs->m_path_root_component(vfs, path, &root);
}



// ******************** File access methods ********************

/*
Generic configuration control for a file object

See the definition of commands in evfs.h for the expected type to pass as arg

Args:
  fh:  The file to receive the command
  cmd: Command number for the operation to perform
  arg: Variable argument data to write or read associated with the command

Returns:
  EVFS_OK on success
*/
int evfs_file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  return fh->methods->m_ctrl(fh, cmd, arg);
}


/*
Close a file

Args:
  fh:  The file to close

Returns:
  EVFS_OK on success
*/
int evfs_file_close(EvfsFile *fh) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  int rval = fh->methods->m_close(fh);

  evfs_free(fh);

  return rval;
}


/*
Read data from a file

Args:
  fh:   The file to read
  buf:  Buffer for read data
  size: Size of buf

Returns:
  Number of bytes read on success or negative error code on failure
*/
size_t evfs_file_read(EvfsFile *fh, void *buf, size_t size) {
  if(PTR_CHECK(fh) || PTR_CHECK(buf)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_read(fh, buf, size);
}

/*
Write data to a file

Args:
  fh:   The file to write
  buf:  Buffer for write data
  size: Size of buf

Returns:
  Number of bytes written on success or negative error code on failure
*/
size_t evfs_file_write(EvfsFile *fh, const void *buf, size_t size) {
  if(PTR_CHECK(fh) || PTR_CHECK(buf)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_write(fh, buf, size);
}

/*
Truncate the length of a file

Args:
  fh:   The file to truncate
  size: New truncated size

Returns:
  EVFS_OK on success
*/
int evfs_file_truncate(EvfsFile *fh, evfs_off_t size) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_truncate(fh, size);
}

/*
Sync a file to the underlying filesystem

Args:
  fh:   The file to sync

Returns:
  EVFS_OK on success
*/
int evfs_file_sync(EvfsFile *fh) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_sync(fh);
}

/*
Get the size of a file

This will perform a sync to guarantee that intermediate write buffers
are emptied before checking the size.

Args:
  fh:   The file to size up

Returns:
  Size of the open file
*/
evfs_off_t evfs_file_size(EvfsFile *fh) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;

  fh->methods->m_sync(fh); // Need to flush internal FS buffers

  return fh->methods->m_size(fh);
}

/*
Seek to a new offset in a file

Origin is one of the following:

  EVFS_SEEK_TO    Absolute position from the start of the file
  EVFS_SEEK_REL   Position relative to current file offset
  EVFS_SEEK_REV   Position from the end of the file

EVFS_SEEK_REL uses negative values to seek backward and positive
to go forward. The other origin types use positive offset values.

Args:
  fh:   The file to seek on
  offset: New position relative to the origin
  origin: Start position for the seek

Returns:
  EVFS_OK on success
*/
int evfs_file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_seek(fh, offset, origin);
}

/*
Get the current position within a file

Args:
  fh:   The file to report on

Returns:
  Current offset into the file from the start
*/
evfs_off_t evfs_file_tell(EvfsFile *fh) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_tell(fh);
}

/*
Identify end of file

Args:
  fh:   The file to report on

Returns:
  true if file is at the end
*/
bool evfs_file_eof(EvfsFile *fh) {
  if(PTR_CHECK(fh)) return EVFS_ERR_BAD_ARG;
  return fh->methods->m_eof(fh);
}


// ******************** Directory access methods ********************

/*
Close a directory

Args:
  dh:   The directory to close

Returns:
  EVFS_OK on success
*/
int evfs_dir_close(EvfsDir *dh) {
  if(PTR_CHECK(dh)) return EVFS_ERR_BAD_ARG;
  int rval = dh->methods->m_close(dh);

  evfs_free(dh);

  return rval;
}

/*
Read the next directory entry

Different VFS backends may only support a partial set of EvfsInfo fields.
Use the EVFS_CMD_GET_DIR_FIELDS command with evfs_vfs_ctrl() to query
which fields are valid on a particular VFS. Unsupported fields will be 0.

Args:
  dh:   The directory to read from
  info: Information reported on the file

Returns:
  EVFS_OK on success. EVFS_DONE when iteration is complete.
*/
int evfs_dir_read(EvfsDir *dh, EvfsInfo *info) {
  if(PTR_CHECK(dh) || PTR_CHECK(info)) return EVFS_ERR_BAD_ARG;
  return dh->methods->m_read(dh, info);
}


/*
Rewind a directory iterator to the beginning

Args:
  dh:   The directory to rewind

Returns:
  EVFS_OK on success
*/
int evfs_dir_rewind(EvfsDir *dh) {
  if(PTR_CHECK(dh)) return EVFS_ERR_BAD_ARG;
  return dh->methods->m_rewind(dh);
}


/*
Find a file matching a glob pattern

This scans a directory for a file until a match to pattern is found.
Repeated calls will find the next file that matches the pattern.

Args:
  dh:      The directory to search
  pattern: Glob pattern to be matched
  info:    Information reported on the file

Returns:
  EVFS_OK on success. EVFS_DONE when iteration is complete.
*/
int evfs_dir_find(EvfsDir *dh, const char *pattern, EvfsInfo *info) {
  if(PTR_CHECK(dh) || PTR_CHECK(pattern) || PTR_CHECK(info)) return EVFS_ERR_BAD_ARG;
  int rval = evfs_dir_read(dh, info);
  while(rval == EVFS_OK) {
    if(glob_match(pattern, info->name, EVFS_PATH_SEPS))
      return EVFS_OK;
    rval = evfs_dir_read(dh, info);
  }

  info->type = 0;
  info->name = NULL;
  return rval;
}


// ******************** String output ********************


/*
Print formatted string to allocated buffer

Args:
  fmt:  Format string
  args: Variable argument list

Returns:
  A new string buffer on success. NULL on failure
*/
char *evfs__vmprintf(const char *fmt, va_list args) {
  va_list args_check;

  // Duplicate args list so we can compute the length of the formatted string
  va_copy(args_check, args);
  int size = vsnprintf(NULL, 0, fmt, args_check) + 1;
  va_end(args_check);

  char *buf = NULL;

  if(size > 0)
    buf = evfs_malloc(size);

  if(buf)
    vsprintf(buf, fmt, args);

  return buf;
}


/*
Print a formatted string to a file

Args:
  fh:   Open file to print into
  fmt:  Format string
  args: Variable argument list

Returns:
  Number of bytes written on success or negative error code on failure
*/
int evfs_file_printf(EvfsFile *fh, const char *fmt, ...) {
  va_list args;
  char *buf;

  // Print into malloc'ed buffer
  va_start(args, fmt);
  buf = evfs__vmprintf(fmt, args);
  va_end(args);

  int status = EVFS_ERR_ALLOC;
  if(buf) {
    status = evfs_file_write(fh, buf, strlen(buf));
    evfs_free(buf);
  }

  return status;
}


/*
Write a string to a file

Args:
  fh:   Open file to print into
  str:  String to write

Returns:
  Number of bytes written on success or negative error code on failure
*/
int evfs_file_puts(EvfsFile *fh, const char *str) {
  return evfs_file_write(fh, str, strlen(str));
}


