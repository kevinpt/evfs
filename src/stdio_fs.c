/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Stdio VFS
  A VFS wrapper for the Stdio API.

  This is a VFS that works with C stdio routines to access an underlying
  filesystem. Some operations require POSIX support or are more efficient with
  a POSIX implementation. Define EVFS_USE_STDIO_POSIX in evfs_config.h to enable
  POSIX features. Directory traversal is only available when EVFS_USE_STDIO_POSIX is
  enabled.

------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/stdio_fs.h"

///////////////////////////////////////////////////////////////////////////////////


#ifdef EVFS_USE_STDIO_POSIX
# include <sys/stat.h>
# include <unistd.h>
# include <dirent.h>
# include <errno.h>
#endif

typedef struct StdioData_s {
  // VFS config options
  unsigned cfg_readonly    :1; // EVFS_CMD_SET_READONLY
  unsigned cfg_no_dir_dots :1; // EVFS_CMD_SET_NO_DIR_DOTS
} StdioData;

typedef struct StdioFile_s {
  EvfsFile base;
  StdioData *fs_data;
  FILE *fp;
} StdioFile;


#ifdef EVFS_USE_STDIO_POSIX
typedef struct StdioDir_s {
  EvfsDir base;
  StdioData *fs_data;
  DIR *dp;
} StdioDir;
#endif


// Common error code conversions
#ifdef EVFS_USE_STDIO_POSIX
static int translate_error(int err) {
  switch(err) {
    case 0:            return EVFS_OK; break;
    case EIO:          return EVFS_ERR_IO; break;
    case ENOENT:       return EVFS_ERR_NO_FILE; break;
    case EEXIST:       return EVFS_ERR_EXISTS; break;
    case ENOTDIR:      return EVFS_ERR_NO_PATH; break;
    case EISDIR:       return EVFS_ERR_IS_DIR; break;
    case ENOTEMPTY:    return EVFS_ERR_NOT_EMPTY; break;
    case ERANGE:       return EVFS_ERR_OVERFLOW; break;
    case EINVAL:       return EVFS_ERR_BAD_ARG; break;
    case ENOSPC:       return EVFS_ERR_FS_FULL; break;
    case ENOMEM:       return EVFS_ERR_ALLOC; break;
    case ENAMETOOLONG: return EVFS_ERR_TOO_LONG; break;
    case EACCES:       return EVFS_ERR_AUTH; break;
    default:           return EVFS_ERR; break;
  }
}
#endif


#define simple_error(e)  ((e) == 0 ? EVFS_OK : EVFS_ERR)



// ******************** File access methods ********************

static int stdio__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  //StdioFile *fil = (StdioFile *)fh;
  
  return EVFS_OK;
}

static int stdio__file_close(EvfsFile *fh) {
  StdioFile *fil = (StdioFile *)fh;
  fclose(fil->fp);
  fil->fp = NULL;
  return EVFS_OK;
}

static ptrdiff_t stdio__file_read(EvfsFile *fh, void *buf, size_t size) {
  StdioFile *fil = (StdioFile *)fh;
  return (ptrdiff_t)fread(buf, 1, size, fil->fp);
}

static ptrdiff_t stdio__file_write(EvfsFile *fh, const void *buf, size_t size) {
  StdioFile *fil = (StdioFile *)fh;
  StdioData *fs_data = (StdioData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;


  return (ptrdiff_t)fwrite(buf, 1, size, fil->fp);
}

static int stdio__file_truncate(EvfsFile *fh, evfs_off_t size) {
#ifdef EVFS_USE_STDIO_POSIX  
  StdioFile *fil = (StdioFile *)fh;
  StdioData *fs_data = (StdioData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  return simple_error(ftruncate(fileno(fil->fp), size));
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}

static int stdio__file_sync(EvfsFile *fh) {
  StdioFile *fil = (StdioFile *)fh;
  return simple_error(fflush(fil->fp));
}

static evfs_off_t stdio__file_size(EvfsFile *fh) {
  StdioFile *fil = (StdioFile *)fh;

#ifdef EVFS_USE_STDIO_POSIX  
  struct stat s;

  if(fstat(fileno(fil->fp), &s) != 0)
    return 0;

  return s.st_size;
#else

  // This is bad practice but our only option with only the plain C library
  evfs_off_t pos = ftell(fil->fp);
  fseek(fil->fp, 0, SEEK_END);
  evfs_off_t fsize = ftell(fil->fp);
  fseek(fil->fp, pos, SEEK_SET);

  return fsize;
#endif
}

static int stdio__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  StdioFile *fil = (StdioFile *)fh;
  int sorg = SEEK_SET;

  switch(origin) {
    case EVFS_SEEK_TO:   sorg = SEEK_SET; break;
    case EVFS_SEEK_REL:  sorg = SEEK_CUR; break;
    case EVFS_SEEK_REV:  sorg = SEEK_END; break;
    default:
      break;
  }

  return simple_error(fseek(fil->fp, offset, sorg));
}

static evfs_off_t stdio__file_tell(EvfsFile *fh) {
  StdioFile *fil = (StdioFile *)fh;
  return ftell(fil->fp);
}

static bool stdio__file_eof(EvfsFile *fh) {
  StdioFile *fil = (StdioFile *)fh;
  return feof(fil->fp) != 0;
}



static EvfsFileMethods s_stdio_methods = {
  .m_ctrl     = stdio__file_ctrl,
  .m_close    = stdio__file_close,
  .m_read     = stdio__file_read,
  .m_write    = stdio__file_write,
  .m_truncate = stdio__file_truncate,
  .m_sync     = stdio__file_sync,
  .m_size     = stdio__file_size,
  .m_seek     = stdio__file_seek,
  .m_tell     = stdio__file_tell,
  .m_eof      = stdio__file_eof
};



// ******************** Directory access methods ********************

#ifdef EVFS_USE_STDIO_POSIX
static int stdio__dir_close(EvfsDir *dh) {
  StdioDir *dir = (StdioDir *)dh;
  closedir(dir->dp);
  dir->dp = NULL;
  return EVFS_OK;
}


static int stdio__dir_read(EvfsDir *dh, EvfsInfo *info) {
  StdioDir *dir = (StdioDir *)dh;
  StdioData *fs_data = (StdioData *)dir->fs_data;

  struct dirent *posix_entry;

  posix_entry = readdir(dir->dp);

  // Skip over dir dots if configured
  if(posix_entry && fs_data->cfg_no_dir_dots) {
    while(posix_entry && (!strcmp(posix_entry->d_name, ".") || !strcmp(posix_entry->d_name, ".."))) {
      posix_entry = readdir(dir->dp);
    }
  }

  memset(info, 0, sizeof(*info));

  if(posix_entry) {
    info->name = posix_entry->d_name;
    if(posix_entry->d_type == DT_DIR)
       info->type |= EVFS_FILE_DIR;
  } else {
    info->name = NULL;
  }

  return posix_entry ? EVFS_OK : EVFS_DONE;
}


static int stdio__dir_rewind(EvfsDir *dh) {
  StdioDir *dir = (StdioDir *)dh;

  rewinddir(dir->dp);
  return EVFS_OK;
}


static EvfsDirMethods s_stdio_dir_methods = {
  .m_close    = stdio__dir_close,
  .m_read     = stdio__dir_read,
  .m_rewind   = stdio__dir_rewind
};
#endif




// ******************** FS access methods ********************


/*
  fopen() mode string mapping (adapted from FatFS)

  "r"   EVFS_READ
  "r+"  EVFS_READ | EVFS_WRITE
  "w"   EVFS_WRITE
  "w"   EVFS_OVERWRITE
  "w+"  EVFS_OVERWRITE | EVFS_WRITE | EVFS_READ
  "a"   EVFS_APPEND
  "a+"  EVFS_APPEND | EVFS_WRITE | EVFS_READ
  "wx"  EVFS_NO_EXIST
  "w+x" EVFS_NO_EXIST | EVFS_WRITE | EVFS_READ
*/


static int stdio__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  StdioFile *fil = (StdioFile *)fh;
  StdioData *fs_data = (StdioData *)vfs->fs_data;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_stdio_methods;

  if((flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND)) && fs_data->cfg_readonly)
    return EVFS_ERR_DISABLED;

  // Build mode string from flags
  char mode[5] = {0};
  int mode_pos = 0;

  if(flags & EVFS_APPEND)
    mode[mode_pos++] = 'a';
  else if((flags & EVFS_OVERWRITE) || (flags & EVFS_NO_EXIST) || ((flags & EVFS_WRITE) && !(flags & EVFS_READ)))
    mode[mode_pos++] = 'w';
  else // Default to reading
    mode[mode_pos++] = 'r';

  if((flags & EVFS_WRITE) && (flags & EVFS_READ))
    mode[mode_pos++] = '+';

  mode[mode_pos++] = 'b'; // Always use binary mode

#if(__STDC_VERSION__ >= 201112L)
  if(mode[0] == 'w' && (flags & EVFS_NO_EXIST))
    mode[mode_pos++] = 'x';
#else
#  warning "C11 is needed to support EVFS_NO_EXIST for evfs_stdio driver"
#endif

  // "r" and "r+" do not create new files so we have to manually handle EVFS_OPEN_OR_NEW in this case.
  if(mode[0] == 'r' && (flags & EVFS_OPEN_OR_NEW)) {
#ifdef EVFS_USE_STDIO_POSIX
    struct stat s;
    if(stat(path, &s) != 0) {
#endif
      // Open the file in append mode to force creation if it doesn't exist
      fil->fp = fopen(path, "a");
      if(fil->fp)
        fclose(fil->fp);
#ifdef EVFS_USE_STDIO_POSIX
    }
#endif
  }

  fil->fs_data = fs_data;
  fil->fp = fopen(path, mode);

  if(fil->fp)
    return EVFS_OK;

#ifdef EVFS_USE_STDIO_POSIX
  return translate_error(errno);
#else
  return EVFS_ERR;
#endif
}


static int stdio__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
#ifdef EVFS_USE_STDIO_POSIX
  struct stat s;

  int err = stat(path, &s);

  memset(info, 0, sizeof(*info));

  if(err == 0) {
    info->size = s.st_size;
    info->mtime = s.st_mtime;

    if(S_ISDIR(s.st_mode))
      info->type |= EVFS_FILE_DIR;
    if(S_ISLNK(s.st_mode))
      info->type |= EVFS_FILE_SYM_LINK;

    return EVFS_OK;
  }

  return translate_error(errno);
#else
  return EVFS_ERR;
#endif
}

static int stdio__delete(Evfs *vfs, const char *path) {
  StdioData *fs_data = (StdioData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  return simple_error(remove(path));
}


static int stdio__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  StdioData *fs_data = (StdioData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  return simple_error(rename(old_path, new_path));
}


#ifdef EVFS_USE_STDIO_POSIX
static int stdio__make_dir(Evfs *vfs, const char *path) {
  StdioData *fs_data = (StdioData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  int err = simple_error(mkdir(path, S_IRWXU));
  if(err == EVFS_OK)
    return err;

  return translate_error(errno);
}

static int stdio__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  StdioDir *dir = (StdioDir *)dh;

  memset(dir, 0, sizeof(*dir));
  dh->methods = &s_stdio_dir_methods;
  dir->fs_data = (StdioData *)vfs->fs_data;

  dir->dp = opendir(path);

  return dir->dp ? EVFS_OK : EVFS_ERR;
}


static int stdio__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  char *status = getcwd((char *)cur_dir->start, range_size(cur_dir));

  if(status) return EVFS_OK;

  return translate_error(errno);  
}

static int stdio__set_cur_dir(Evfs *vfs, const char *path) {
  return simple_error(chdir(path));
}

#endif


static int stdio__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  StdioData *fs_data = (StdioData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER: return EVFS_OK; break;

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
        *v = EVFS_INFO_SIZE | EVFS_INFO_MTIME | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    case EVFS_CMD_GET_DIR_FIELDS:
      {
        unsigned *v = (unsigned *)arg;
        *v = EVFS_INFO_NAME | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    default: return EVFS_ERR_NO_SUPPORT; break;
  }
}


static StdioData s_stdio_data = {0};

static Evfs s_stdio_vfs = {
  .vfs_name = "stdio",
  .vfs_file_size = sizeof(StdioFile),
#ifdef EVFS_USE_STDIO_POSIX
  .vfs_dir_size = sizeof(StdioDir),
#else
  .vfs_dir_size = sizeof(EvfsDir), // Directory traversal not supported
#endif

  .fs_data = &s_stdio_data,

  // Required mothods
  .m_open = stdio__open,
  .m_stat = stdio__stat,

  // Optional methods
  .m_delete = stdio__delete,
  .m_rename = stdio__rename,
#ifdef EVFS_USE_STDIO_POSIX
  .m_make_dir = stdio__make_dir,
  .m_open_dir = stdio__open_dir,
  .m_get_cur_dir = stdio__get_cur_dir,
  .m_set_cur_dir = stdio__set_cur_dir,
#endif  
  .m_vfs_ctrl = stdio__vfs_ctrl,

};


/*
Register a stdio instance.

This VFS is always named "stdio". There should only be one instance per application.

Args:
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_stdio(bool default_vfs) {
  return evfs_register(&s_stdio_vfs, default_vfs);
}


