/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Tracing shim VFS
  This adds debugging traces for calls to the underlying VFS.
------------------------------------------------------------------------------
*/


#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/shim/shim_trace.h"


// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])

#define FILE_NAME_SIZE  32

typedef struct TraceData_s {
  Evfs *base_vfs;
  int (*report)(const char *buf, void *ctx);
  void *ctx;
  const char *vfs_name;
  Evfs *shim_vfs;
} TraceData;

typedef struct TraceFile_s {
  EvfsFile base;
  TraceData *shim_data;
  EvfsFile *base_file;
  char filename[FILE_NAME_SIZE+1];
} TraceFile;

typedef struct TraceDir_s {
  EvfsDir base;
  TraceData *shim_data;
  EvfsDir *base_dir;
  char filename[FILE_NAME_SIZE+1];
} TraceDir;



#ifdef EVFS_USE_ANSI_COLOR
// Yellow trace output
#  define TRACE_PREFIX A_YLW "[[ "
// No color
#  define TRACE_SUFFIX " ]]" A_NONE "\n"

#else
#  define TRACE_PREFIX "[[ "
#  define TRACE_SUFFIX " ]]\n"
#endif


// Common prefix for trace format strings:
#define TP(s)  (TRACE_PREFIX s A_NONE)

// Common sufffix for trace format strings:
#define TS(s)  (A_YLW s TRACE_SUFFIX)

// Common wrapper for trace format strings:
#define TF(s)  (TRACE_PREFIX s TRACE_SUFFIX)


// Set ANSI color to highlight file/dir names
#ifdef EVFS_USE_ANSI_COLOR
#  define  HL_FNS  A_BYLW
#  define  HL_FNE  A_YLW
#else
#  define  HL_FNS
#  define  HL_FNE
#endif

#define HL_NAME  HL_FNS "%s" HL_FNE


static void trace_printf(TraceData *shim_data, const char *fmt, ...) {
  va_list args;
  char *buf;

  va_start(args, fmt);
  buf = evfs__vmprintf(fmt, args);
  va_end(args);
  
  if(buf) {
    shim_data->report(buf, shim_data->ctx); 
    evfs_free(buf);
  }
}

// Print common result string with optional colorization of errors
static void trace_print_result(TraceData *shim_data, const char *err_name, int err) {

#ifdef EVFS_USE_ANSI_COLOR
  if(err >= 0) // No color change
#endif
    trace_printf(shim_data, TS(" -> %s"), err_name);
#ifdef EVFS_USE_ANSI_COLOR
  else // Bold red error name
    trace_printf(shim_data, TS(" -> " A_BRED "%s" A_YLW ), err_name);
#endif
}



// ******************** File access methods ********************

static int trace__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  if(evfs_cmd_name(cmd)[0] != '<')
    trace_printf(shim_data, TP("%s.m_ctrl(" HL_NAME ", cmd=%s)"), shim_data->vfs_name, fil->filename, evfs_cmd_name(cmd));
  else // Unknown command; Report its value
    trace_printf(shim_data, TP("%s.m_ctrl(" HL_NAME ", cmd=%d)"), shim_data->vfs_name, fil->filename, cmd);

  int status = fil->base_file->methods->m_ctrl(fil->base_file, cmd, arg);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__file_close(EvfsFile *fh) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_close(" HL_NAME ")"), shim_data->vfs_name, fil->filename);
  int status = fil->base_file->methods->m_close(fil->base_file);
  trace_print_result(shim_data, evfs_err_name(status), status);

  if(status == EVFS_OK) {
    fil->base.methods = NULL; // Disable this instance
  }

  return status;
}


static ptrdiff_t trace__file_read(EvfsFile *fh, void *buf, size_t size) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_read(" HL_NAME ", size=%ld)"), shim_data->vfs_name, fil->filename, size);
  ptrdiff_t read = fil->base_file->methods->m_read(fil->base_file, buf, size);
  if(read >= 0)
    trace_printf(shim_data, TS(" -> %ld"), read);
  else
    trace_print_result(shim_data, evfs_err_name(read), read);

  return read;
}


static ptrdiff_t trace__file_write(EvfsFile *fh, const void *buf, size_t size) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_write(" HL_NAME ", size=%ld)"), shim_data->vfs_name, fil->filename, size);
  ptrdiff_t wrote = fil->base_file->methods->m_write(fil->base_file, buf, size);
  if(wrote >= 0)
    trace_printf(shim_data, TS(" -> %ld"), wrote);
  else
    trace_print_result(shim_data, evfs_err_name(wrote), wrote);

  return wrote;
}


static int trace__file_truncate(EvfsFile *fh, evfs_off_t size) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_truncate(" HL_NAME ", size=%ld)"), shim_data->vfs_name, fil->filename, size);
  int status = fil->base_file->methods->m_truncate(fil->base_file, size);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__file_sync(EvfsFile *fh) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_sync(" HL_NAME ")"), shim_data->vfs_name, fil->filename);
  int status = fil->base_file->methods->m_sync(fil->base_file);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}



static evfs_off_t trace__file_size(EvfsFile *fh) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_size(" HL_NAME ")"), shim_data->vfs_name, fil->filename);
  evfs_off_t size = fil->base_file->methods->m_size(fil->base_file);
  trace_printf(shim_data, TS(" -> %d"), size);

  return size;
}


static int trace__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;
  
  const char *org_name;
  switch(origin) {
  case EVFS_SEEK_TO:  org_name = "EVFS_SEEK_TO"; break;
  case EVFS_SEEK_REL: org_name = "EVFS_SEEK_REL"; break;
  case EVFS_SEEK_REV: org_name = "EVFS_SEEK_REV"; break;
  default:            org_name = "<unknown>"; break;
  }

  trace_printf(shim_data, TP("%s.m_seek(" HL_NAME ", offset=%ld, origin=%s)"),
               shim_data->vfs_name, fil->filename, offset, org_name);
  int status = fil->base_file->methods->m_seek(fil->base_file, offset, origin);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static evfs_off_t trace__file_tell(EvfsFile *fh) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_tell(" HL_NAME ")"), shim_data->vfs_name, fil->filename);
  evfs_off_t pos = fil->base_file->methods->m_tell(fil->base_file);
  trace_printf(shim_data, TS(" -> %d"), pos);

  return pos;
}


static bool trace__file_eof(EvfsFile *fh) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = fil->shim_data;

  trace_printf(shim_data, TP("%s.m_eof(" HL_NAME ")"), shim_data->vfs_name, fil->filename);
  bool eof = fil->base_file->methods->m_eof(fil->base_file);
  trace_printf(shim_data, TS(" -> %c"), eof ? 'T' : 'f');

  return eof;
}


static const EvfsFileMethods s_trace_methods = {
  .m_ctrl     = trace__file_ctrl,
  .m_close    = trace__file_close,
  .m_read     = trace__file_read,
  .m_write    = trace__file_write,
  .m_truncate = trace__file_truncate,
  .m_sync     = trace__file_sync,
  .m_size     = trace__file_size,
  .m_seek     = trace__file_seek,
  .m_tell     = trace__file_tell,
  .m_eof      = trace__file_eof
};




// ******************** Directory access methods ********************

static int trace__dir_close(EvfsDir *dh) {
  TraceDir *dir = (TraceDir *)dh;
  TraceData *shim_data = dir->shim_data;

  trace_printf(shim_data, TP("%s.m_dir_close(" HL_NAME ")"), shim_data->vfs_name, dir->filename);
  int status = dir->base_dir->methods->m_close(dir->base_dir);
  trace_print_result(shim_data, evfs_err_name(status), status);

  if(status == EVFS_OK) {
    dir->base.methods = NULL; // Disable this instance
  }

  return status;
}

static int trace__dir_read(EvfsDir *dh, EvfsInfo *info) {
  TraceDir *dir = (TraceDir *)dh;
  TraceData *shim_data = dir->shim_data;

  trace_printf(shim_data, TP("%s.m_dir_read(" HL_NAME ")"), shim_data->vfs_name, dir->filename);
  int status = dir->base_dir->methods->m_read(dir->base_dir, info);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}

static int trace__dir_rewind(EvfsDir *dh) {
  TraceDir *dir = (TraceDir *)dh;
  TraceData *shim_data = dir->shim_data;

  trace_printf(shim_data, TP("%s.m_dir_rewind(" HL_NAME ")"), shim_data->vfs_name, dir->filename);
  int status = dir->base_dir->methods->m_rewind(dir->base_dir);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static const EvfsDirMethods s_trace_dir_methods = {
  .m_close    = trace__dir_close,
  .m_read     = trace__dir_read,
  .m_rewind   = trace__dir_rewind
};



// ******************** FS access methods ********************


static int trace__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  TraceFile *fil = (TraceFile *)fh;
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  fil->shim_data = shim_data;
  if(path) {
    StringRange tail;
    evfs_path_basename(path, &tail);
    strncpy(fil->filename, tail.start, COUNT_OF(fil->filename));
    fil->filename[COUNT_OF(fil->filename)-1] = '\0';
  } else {
    strncpy(fil->filename, "<unknown>", COUNT_OF(fil->filename));
  }

  fil->base_file = (EvfsFile *)NEXT_OBJ(fil);  // We have two objects allocated together [TraceFile][<base VFS file size>]

  trace_printf(shim_data, TP("%s.m_open(" HL_NAME ", flags=0x%02X)"), shim_data->vfs_name, fil->filename, flags);
  int status = base_vfs->m_open(base_vfs, path, fil->base_file, flags);
  trace_print_result(shim_data, evfs_err_name(status), status);
  
  if(status == EVFS_OK) {
    // Add methods to make this functional
    if(fil->base_file->methods) {
      fh->methods = &s_trace_methods;
    } else {
      fh->methods = NULL;
      status = EVFS_ERR_INIT;
    }

  } else { // Open failed
    fh->methods = NULL;
  }

  return status;
}



static int trace__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_stat(" HL_NAME ")"), shim_data->vfs_name, path);
  int status = base_vfs->m_stat(base_vfs, path, info);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}



static int trace__delete(Evfs *vfs, const char *path) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_delete(" HL_NAME ")"), shim_data->vfs_name, path);
  int status = base_vfs->m_delete(base_vfs, path);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_rename(" HL_NAME ", " HL_NAME ")"), shim_data->vfs_name, old_path, new_path);
  int status = base_vfs->m_rename(base_vfs, old_path, new_path);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__make_dir(Evfs *vfs, const char *path) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_make_dir(" HL_NAME ")"), shim_data->vfs_name, path);
  int status = base_vfs->m_make_dir(base_vfs, path);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  TraceDir *dir = (TraceDir *)dh;
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  dir->shim_data = shim_data;
  //dir->filename = path ? path : "<unknown>";
  if(path) {
    strncpy(dir->filename, path, COUNT_OF(dir->filename));
    dir->filename[COUNT_OF(dir->filename)-1] = '\0';
  } else {
    strncpy(dir->filename, "<unknown>", COUNT_OF(dir->filename));
  }

  dir->base_dir = (EvfsDir *)NEXT_OBJ(dir);   // We have two objects allocated together [TraceDir][<base VFS dir size>]

  trace_printf(shim_data, TP("%s.m_open_dir(" HL_NAME ")"), shim_data->vfs_name, dir->filename);
  int status = base_vfs->m_open_dir(base_vfs, path, dir->base_dir);
  trace_print_result(shim_data, evfs_err_name(status), status);

  if(status == EVFS_OK) {
    // Add methods to make this functional
    if(dir->base_dir->methods) {
      dh->methods = &s_trace_dir_methods;
    } else {
      dh->methods = NULL;
      status = EVFS_ERR_INIT;
    }

  } else { // Open failed
    dh->methods = NULL;
  }

  return status;
}


static int trace__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_get_cur_dir()"), shim_data->vfs_name);
  int status = base_vfs->m_get_cur_dir(base_vfs, cur_dir);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__set_cur_dir(Evfs *vfs, const char *path) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_set_cur_dir(" HL_NAME ")"), shim_data->vfs_name, path);
  int status = base_vfs->m_set_cur_dir(base_vfs, path);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static int trace__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  // We need special handling for EVFS_CMD_UNREGISTER.
  // It can't pass through since we need to deallocate VFSs in the proper sequence
  // to avoid corrupting the registered VFS linked list.
  if(cmd == EVFS_CMD_UNREGISTER) {
      evfs_free(vfs); // Free this trace VFS
      return EVFS_OK;
  }

  if(evfs_cmd_name(cmd)[0] != '<')
    trace_printf(shim_data, TP("%s.m_vfs_ctrl(" HL_NAME ")"), shim_data->vfs_name, evfs_cmd_name(cmd));
  else // Unknown command; Report its value
    trace_printf(shim_data, TP("%s.m_vfs_ctrl(%d)"), shim_data->vfs_name, cmd);

  int status = base_vfs->m_vfs_ctrl(base_vfs, cmd, arg);
  trace_print_result(shim_data, evfs_err_name(status), status);

  return status;
}


static bool trace__path_root_component(Evfs *vfs, const char *path, StringRange *root) {
  TraceData *shim_data = (TraceData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  trace_printf(shim_data, TP("%s.m_path_root_component(" HL_NAME ")"),
                             shim_data->vfs_name, path);
  bool is_absolute = base_vfs->m_path_root_component(base_vfs, path, root);
  trace_printf(shim_data, TS(" -> '%.*s' %s"), RANGE_FMT(root), is_absolute ? "absolute" : "relative");

  return is_absolute;
}


/*
Register a tracing filesystem shim

Args:
  vfs_name:      Name of new shim
  old_vfs_name:  Existing VFS to wrap with shim
  report:        Callback function for trace output
  ctx:           User defined context for the report callback
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_trace(const char *vfs_name, const char *old_vfs_name, 
    int (*report)(const char *buf, void *ctx), void *ctx, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(old_vfs_name) || PTR_CHECK(report)) return EVFS_ERR_BAD_ARG;

  Evfs *base_vfs, *shim_vfs;
  TraceData *shim_data;

  base_vfs = evfs_find_vfs(old_vfs_name);
  if(PTR_CHECK(base_vfs)) return EVFS_ERR_NO_VFS;

  // Construct a new VFS
  // We have three objects allocated together [Evfs][TraceData][char[]]
  size_t shim_size = sizeof(*shim_vfs) + sizeof(*shim_data) + strlen(vfs_name)+1;
  shim_vfs = evfs_malloc(shim_size);
  if(MEM_CHECK(shim_vfs)) return EVFS_ERR_ALLOC;

  memset(shim_vfs, 0, shim_size);

  shim_data = (TraceData *)NEXT_OBJ(shim_vfs);

  shim_vfs->vfs_name = (char *)NEXT_OBJ(shim_data);
  strcpy((char *)shim_vfs->vfs_name, vfs_name);

  shim_data->base_vfs = base_vfs;
  shim_data->report = report;
  shim_data->ctx = ctx;
  shim_data->vfs_name = shim_vfs->vfs_name;
  shim_data->shim_vfs = shim_vfs;

  shim_vfs->vfs_file_size = sizeof(TraceFile) + base_vfs->vfs_file_size;
  shim_vfs->vfs_dir_size = sizeof(TraceDir) + base_vfs->vfs_dir_size;
  shim_vfs->fs_data = shim_data;

  shim_vfs->m_open = trace__open;
  shim_vfs->m_stat = trace__stat;
  shim_vfs->m_delete = trace__delete;
  shim_vfs->m_rename = trace__rename;
  shim_vfs->m_make_dir = trace__make_dir;
  shim_vfs->m_open_dir = trace__open_dir;
  shim_vfs->m_get_cur_dir = trace__get_cur_dir;
  shim_vfs->m_set_cur_dir = trace__set_cur_dir;
  shim_vfs->m_vfs_ctrl = trace__vfs_ctrl;

  shim_vfs->m_path_root_component = trace__path_root_component;

  return evfs_register(shim_vfs, default_vfs);
}

