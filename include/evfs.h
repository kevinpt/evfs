/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  This implements a common OOP wrapper for basic filesystem operations. It is
  used in conjunction with various backends to access files in a uniform way.
------------------------------------------------------------------------------
*/

#ifndef EVFS_H
#define EVFS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "evfs_config.h"
#include "evfs/util/range_strings.h"


typedef struct Evfs_s Evfs;
typedef struct EvfsFile_s EvfsFile;
typedef struct EvfsDir_s EvfsDir;
typedef struct EvfsInfo_s EvfsInfo;


// Type for working with file sizes and offsets
#if EVFS_FILE_OFFSET_BITS == 64
  typedef int64_t evfs_off_t;
#else
  // Max supported file size will be 2GiB
  typedef int32_t evfs_off_t;
#endif


// Base class for VFS wrappers
typedef struct Evfs_s {
  struct Evfs_s *next;
  const char *vfs_name;
  size_t vfs_file_size;
  size_t vfs_dir_size;
  void *fs_data;

  // Required methods
  int (*m_open)(Evfs *vfs, const char *path, EvfsFile *fh, int flags);
  int (*m_stat)(Evfs *vfs, const char *path, EvfsInfo *info);


  // Optional methods
  int (*m_delete)(Evfs *vfs, const char *path);
  int (*m_rename)(Evfs *vfs, const char *old_path, const char *new_path);
  int (*m_make_dir)(Evfs *vfs, const char *path);
  int (*m_open_dir)(Evfs *vfs, const char *path, EvfsDir *dh);

  int (*m_get_cur_dir)(Evfs *vfs, StringRange *cur_dir);
  int (*m_set_cur_dir)(Evfs *vfs, const char *path);

  int (*m_vfs_ctrl)(Evfs *vfs, int cmd, void *arg);

  bool (*m_path_root_component)(Evfs *vfs, const char *path, StringRange *root);

} Evfs;


typedef enum {
  EVFS_SEEK_TO  = 1,
  EVFS_SEEK_REL,
  EVFS_SEEK_REV
} EvfsSeekDir;


// Virtual methods for EvfsFile
typedef struct EvfsFileMethods_s {
  int       (*m_ctrl)(EvfsFile *fh, int cmd, void *arg);
  int       (*m_close)(EvfsFile *fh);
  ptrdiff_t (*m_read)(EvfsFile *fh, void *buf, size_t size);
  ptrdiff_t (*m_write)(EvfsFile *fh, const void *buf, size_t size);
  int       (*m_truncate)(EvfsFile *fh, evfs_off_t size);
  int       (*m_sync)(EvfsFile *fh);
  evfs_off_t (*m_size)(EvfsFile *fh);
  int       (*m_seek)(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin);
  evfs_off_t (*m_tell)(EvfsFile *fh);
  bool      (*m_eof)(EvfsFile *fh);
} EvfsFileMethods;


// Base class for file objects
struct EvfsFile_s {
  const EvfsFileMethods *methods;
};



// File information reported by evfs_stat() and evfs_dir_read()
typedef struct EvfsInfo_s {
  char       *name;
  time_t      mtime;
  evfs_off_t  size;
  uint8_t     type;
} EvfsInfo;

// EvfsInfo type flags
#define EVFS_FILE_DIR          0x01


// Virtual methods for directory objects
typedef struct EvfsDirMethods_s {
  int    (*m_close)(EvfsDir *dh);
  int    (*m_read)(EvfsDir *dh, EvfsInfo *info);
  int    (*m_rewind)(EvfsDir *dh);
} EvfsDirMethods;


// Base class for directory objects
struct EvfsDir_s {
  const EvfsDirMethods *methods;
};




// ******************** Return codes ********************
// The following use X-macro expansion to enable string names
// in evfs_err_name() and evfs_cmd_name().

#define EVFS_ERR_LIST(M) \
  M(EVFS_DONE, 1) \
  M(EVFS_OK, 0) \
  M(EVFS_ERR, -1) \
  M(EVFS_ERR_NO_SUPPORT, -2) \
  M(EVFS_ERR_NO_VFS, -3) \
  M(EVFS_ERR_IO, -4) \
  M(EVFS_ERR_CORRUPTION, -5) \
  M(EVFS_ERR_NO_FILE, -6) \
  M(EVFS_ERR_EXISTS, -7) \
  M(EVFS_ERR_NO_PATH, -8) \
  M(EVFS_ERR_IS_DIR, -9) \
  M(EVFS_ERR_NOT_EMPTY, -10) \
  M(EVFS_ERR_OVERFLOW, -11) \
  M(EVFS_ERR_BAD_ARG, -12) \
  M(EVFS_ERR_FS_FULL, -13) \
  M(EVFS_ERR_ALLOC, -14) \
  M(EVFS_ERR_TOO_LONG, -15) \
  M(EVFS_ERR_AUTH, -16) \
  M(EVFS_ERR_BAD_NAME, -17) \
  M(EVFS_ERR_INIT, -18) \
  M(EVFS_ERR_DISABLED, -19) \
  M(EVFS_ERR_INVALID, -20) \
  M(EVFS_ERR_REPAIRED, -21)


#define EVFS_ENUM_ITEM(E, V) E = V,

enum EvfsErrors {
  EVFS_ERR_LIST(EVFS_ENUM_ITEM)
};


// ******************** ctrl commands ********************

#define CMD_RD  0x01
#define CMD_WR  0x02
#define CMD_RW  (CMD_RD | CMD_WR)

#define CMD_DEF(n, m, t)  ( ((n) << 2) | (m) )

// evfs_vfs_ctrl() and evfs_file_ctrl() cmd arguments
// Shim specific VFS commands start at 200
// Shim specific file commands start at 300
#define EVFS_CMD_LIST(M) \
  M(EVFS_CMD_UNREGISTER,      CMD_DEF(10, CMD_WR, void)) \
  M(EVFS_CMD_SET_READONLY,    CMD_DEF(11, CMD_WR, unsigned)) \
  M(EVFS_CMD_SET_NO_DIR_DOTS, CMD_DEF(12, CMD_WR, unsigned)) \
  M(EVFS_CMD_GET_STAT_FIELDS, CMD_DEF(13, CMD_RD, unsigned)) \
  M(EVFS_CMD_GET_DIR_FIELDS,  CMD_DEF(14, CMD_RD, unsigned)) \
  M(EVFS_CMD_SET_ROTATE_CFG,  CMD_DEF(201, CMD_WR, RotateConfig)) \
  M(EVFS_CMD_SET_ROTATE_TRIM, CMD_DEF(301, CMD_WR, evfs_off_t))

// Offset for external user defined commands
#define EVFS_CMD_USER_DEFINED  1000

enum EvfsCommands {
  EVFS_CMD_LIST(EVFS_ENUM_ITEM)
};


// Masks for EVFS_CMD_GET_STAT_FIELDS and EVFS_CMD_GET_DIR_FIELDS:
#define EVFS_INFO_NAME   0x01
#define EVFS_INFO_SIZE   0x02
#define EVFS_INFO_MTIME  0x04
#define EVFS_INFO_TYPE   0x08


// Modes for evfs_open()
#define EVFS_READ          0x01
#define EVFS_WRITE         0x02
#define EVFS_RDWR          (EVFS_READ | EVFS_WRITE)
#define EVFS_OPEN_OR_NEW   0x04
#define EVFS_NO_EXIST      0x08
#define EVFS_OVERWRITE     0x10
#define EVFS_APPEND        0x20



#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif


#define evfs_malloc(b)  malloc(b)
#define evfs_free(p)    free(p)


const char *evfs_err_name(int err);
const char *evfs_cmd_name(int cmd);


void evfs_init(void);

// ******************** VFS registration ********************
Evfs *evfs_find_vfs(const char *vfs_name);
const char *evfs_vfs_name(Evfs *vfs);
const char *evfs_default_vfs_name(void);

int evfs_register(Evfs *vfs, bool make_default);
int evfs_unregister(Evfs *vfs);
void evfs_unregister_all(void);


// ******************** FS access methods ********************
int evfs_open_ex(const char *path, EvfsFile **fh, int flags, const char *vfs_name);

static inline int evfs_open(const char *path, EvfsFile **fh, int flags) {
  return evfs_open_ex(path, fh, flags, NULL);
}
//#define evfs_open(path, fh, flags) evfs_open_ex(path, fh, flags, NULL)

int evfs_stat_ex(const char *path, EvfsInfo *info, const char *vfs_name);

static inline int evfs_stat(const char *path, EvfsInfo *info) {
  return evfs_stat_ex(path, info, NULL);
}

bool evfs_existing_file_ex(const char *path, const char *vfs_name);

static inline bool evfs_existing_file(const char *path) {
  return evfs_existing_file_ex(path, NULL);
}


bool evfs_existing_dir_ex(const char *path, const char *vfs_name);

static inline bool evfs_existing_dir(const char *path) {
  return evfs_existing_dir_ex(path, NULL);
}


int evfs_delete_ex(const char *path, const char *vfs_name);

static inline int evfs_delete(const char *path) {
  return evfs_delete_ex(path, NULL);
}


int evfs_make_dir_ex(const char *path, const char *vfs_name);

static inline int evfs_make_dir(const char *path) {
  return evfs_make_dir_ex(path, NULL);
}


int evfs_make_path_ex(const char *path, const char *vfs_name);

static inline int evfs_make_path(const char *path) {
  return evfs_make_path_ex(path, NULL);
}


int evfs_make_path_range_ex(StringRange *path, const char *vfs_name);

static inline int evfs_make_path_range(StringRange *path) {
  return evfs_make_path_range_ex(path, NULL);
}


int evfs_open_dir_ex(const char *path, EvfsDir **dh, const char *vfs_name);

static inline int evfs_open_dir(const char *path, EvfsDir **dh) {
  return evfs_open_dir_ex(path, dh, NULL);
}


int evfs_get_cur_dir_ex(StringRange *cur_dir, const char *vfs_name);

static inline int evfs_get_cur_dir(StringRange *cur_dir) {
  return evfs_get_cur_dir_ex(cur_dir, NULL);
}


int evfs_set_cur_dir_ex(const char *path, const char *vfs_name);

static inline int evfs_set_cur_dir(const char *path) {
  return evfs_set_cur_dir_ex(path, NULL);
}


int evfs_vfs_ctrl_ex(int op, void *arg, const char *vfs_name);

static inline int evfs_vfs_ctrl(int op, void *arg) {
  return evfs_vfs_ctrl_ex(op, arg, NULL);
}


int evfs_copy_to_file_ex(const char *dest_path, EvfsFile *fh, char *buf, size_t buf_size, const char *vfs_name);

static inline int evfs_copy_to_file(const char *dest_path, EvfsFile *fh, char *buf, size_t buf_size) {
  return evfs_copy_to_file_ex(dest_path, fh, NULL, 0, NULL);
}


int evfs_vfs_open(Evfs *vfs, const char *path, EvfsFile **fh, int flags);
int evfs_vfs_open_dir(Evfs *vfs, const char *path, EvfsDir **dh);


// ******************** Path operations ********************
bool evfs_path_root_component_ex(const char *path, StringRange *root, const char *vfs_name);

static inline bool evfs_path_root_component(const char *path, StringRange *root) {
  return evfs_path_root_component_ex(path, root, NULL);
}


int evfs_path_basename(const char *path, StringRange *tail);

int evfs_path_dirname_ex(const char *path, StringRange *head, const char *vfs_name);

static inline int evfs_path_dirname(const char *path, StringRange *head) {
  return evfs_path_dirname_ex(path, head, NULL);
}


int evfs_path_join_ex(StringRange *head, StringRange *tail, StringRange *joined, const char *vfs_name);

static inline int evfs_path_join(StringRange *head, StringRange *tail, StringRange *joined) {
  return evfs_path_join_ex(head, tail, joined, NULL);
}


int evfs_path_join_str_ex(const char *head, const char *tail, StringRange *joined, const char *vfs_name);

static inline int evfs_path_join_str(const char *head, const char *tail, StringRange *joined) {
  return evfs_path_join_str_ex(head, tail, joined, NULL);
}


int evfs_path_extname(const char *path, StringRange *ext);

int evfs_path_normalize_ex(const char *path, StringRange *normalized, const char *vfs_name);

static inline int evfs_path_normalize(const char *path, StringRange *normalized) {
  return evfs_path_normalize_ex(path, normalized, NULL);
}


int evfs_path_absolute_ex(const char *path, StringRange *absolute, const char *vfs_name);

static inline int evfs_path_absolute(const char *path, StringRange *absolute) {
  return evfs_path_absolute_ex(path, absolute, NULL);
}


bool evfs_path_is_absolute_ex(const char *path, const char *vfs_name);
static inline bool evfs_path_is_absolute(const char *path) {
  return evfs_path_is_absolute_ex(path, NULL);
}


// Non-virtual path operations with direct VFS access
int evfs_vfs_path_basename(Evfs *vfs, const char *path, StringRange *tail);
int evfs_vfs_path_dirname(Evfs *vfs, const char *path, StringRange *head);
int evfs_vfs_path_join(Evfs *vfs, StringRange *head, StringRange *tail, StringRange *joined);
int evfs_vfs_path_join_str(Evfs *vfs, const char *head, const char *tail, StringRange *joined);
int evfs_vfs_path_extname(Evfs *vfs, const char *path, StringRange *ext);
int evfs_vfs_path_normalize(Evfs *vfs, const char *path, StringRange *normalized);
int evfs_vfs_path_absolute(Evfs *vfs, const char *path, StringRange *absolute);
bool evfs_vfs_path_is_absolute(Evfs *vfs, const char *path);


// ******************** File access methods ********************
int evfs_file_ctrl(EvfsFile *fh, int cmd, void *arg);
int evfs_file_close(EvfsFile *fh);
size_t evfs_file_read(EvfsFile *fh, void *buf, size_t size);
size_t evfs_file_write(EvfsFile *fh, const void *buf, size_t size);
int evfs_file_truncate(EvfsFile *fh, evfs_off_t size);
int evfs_file_sync(EvfsFile *fh);
evfs_off_t evfs_file_size(EvfsFile *fh);
int evfs_file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin);

static inline int evfs_file_rewind(EvfsFile *fh) {
  return evfs_file_seek(fh, 0, EVFS_SEEK_TO);
}

evfs_off_t evfs_file_tell(EvfsFile *fh);
bool evfs_file_eof(EvfsFile *fh);


// ******************** Directory access methods ********************
int evfs_dir_close(EvfsDir *dh);
int evfs_dir_read(EvfsDir *dh, EvfsInfo *info);
int evfs_dir_rewind(EvfsDir *dh);
int evfs_dir_find(EvfsDir *dh, const char *pattern, EvfsInfo *info);


// ******************** String output ********************
int evfs_file_printf(EvfsFile *fh, const char *fmt, ...);
int evfs_file_puts(EvfsFile *fh, const char *str);

#endif // EVFS_H

