/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  FatFs VFS
  A VFS wrapper for the FatFs API.
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/fatfs_fs.h"
#include "ff.h"

///////////////////////////////////////////////////////////////////////////////////


typedef struct FatfsData_s {
  //FATFS *fs;
  uint8_t pdrv;
  FILINFO info;

  // VFS config options
  unsigned cfg_readonly:1; // EVFS_CMD_SET_READONLY

} FatfsData;

typedef struct FatfsFile_s {
  EvfsFile base;
  FatfsData *fs_data;
  uint8_t pdrv;
  FIL fil;
} FatfsFile;


typedef struct FatfsDir_s {
  EvfsDir base;
  uint8_t pdrv;
  DIR dir;
  FILINFO info; // Need to hold entry names
} FatfsDir;



// Common error code conversions
static int translate_error(int err) {
  switch(err) {
    case FR_OK:                   return EVFS_OK; break;
    case FR_DISK_ERR:             return EVFS_ERR_IO; break;
    case FR_INT_ERR:              return EVFS_ERR; break;
    case FR_NOT_READY:            return EVFS_ERR; break;
    case FR_NO_FILE:              return EVFS_ERR_NO_FILE; break;
    case FR_NO_PATH:              return EVFS_ERR_NO_PATH; break;
    case FR_INVALID_NAME:         return EVFS_ERR_BAD_NAME; break; // Doesn't conform to DOS naming conventions (when LFN disabled)
    case FR_DENIED:               return EVFS_ERR_AUTH; break;
    case FR_EXIST:                return EVFS_ERR_EXISTS; break;
    case FR_INVALID_OBJECT:       return EVFS_ERR; break;
    case FR_WRITE_PROTECTED:      return EVFS_ERR; break;
    case FR_INVALID_DRIVE:        return EVFS_ERR; break;
    case FR_NOT_ENABLED:          return EVFS_ERR; break;
    case FR_NO_FILESYSTEM:        return EVFS_ERR; break;
    case FR_MKFS_ABORTED:         return EVFS_ERR; break;
    case FR_TIMEOUT:              return EVFS_ERR; break;
    case FR_LOCKED:               return EVFS_ERR; break;
    case FR_NOT_ENOUGH_CORE:      return EVFS_ERR_ALLOC; break;
    case FR_TOO_MANY_OPEN_FILES:  return EVFS_ERR; break;
    case FR_INVALID_PARAMETER:    return EVFS_ERR_BAD_ARG; break;
    default:                      return EVFS_ERR; break;
  }
}

#define simple_error(e)  ((e) == FR_OK ? EVFS_OK : EVFS_ERR)


static bool fatfs__path_root_component(Evfs *vfs, const char *path, StringRange *root) {
  // Handle DOS style drive paths
  const char *pos = path;
  
  if(isalnum(*pos) && *(pos+1) == ':')
    pos += 2;

  int leading_seps = strspn(pos, EVFS_PATH_SEPS);
  size_t root_len = (pos - path) + leading_seps;
  range_init(root, (char *)path, root_len);

  return leading_seps > 0; // Report absolute path
}




// ******************** File access methods ********************

static int fatfs__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  //FatfsFile *fil = (FatfsFile *)fh;

  return EVFS_OK;
}

static int fatfs__file_close(EvfsFile *fh) {
  FatfsFile *fil = (FatfsFile *)fh;
  f_close(&fil->fil);
  return EVFS_OK;
}

static ptrdiff_t fatfs__file_read(EvfsFile *fh, void *buf, size_t size) {
  FatfsFile *fil = (FatfsFile *)fh;

  UINT read = 0;
  FRESULT status = f_read(&fil->fil, buf, size, &read);
  ptrdiff_t rval = translate_error(status);

  if(rval == EVFS_OK && read > 0)
    rval = read;

  return rval;
}

static ptrdiff_t fatfs__file_write(EvfsFile *fh, const void *buf, size_t size) {
#if FF_FS_READONLY == 0
  FatfsFile *fil = (FatfsFile *)fh;
  FatfsData *fs_data = (FatfsData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  UINT wrote = 0;
  FRESULT status = f_write(&fil->fil, buf, size, &wrote);
  ptrdiff_t rval = translate_error(status);

  if(rval == EVFS_OK && wrote > 0)
    rval = wrote;

  return rval;
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}

static int fatfs__file_truncate(EvfsFile *fh, evfs_off_t size) {
#if FF_FS_READONLY == 0
  FatfsFile *fil = (FatfsFile *)fh;
  FatfsData *fs_data = (FatfsData *)fil->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  evfs_off_t cur_pos = f_tell(&fil->fil);

  // f_lseek() can grow files in write mode which we don't want to do here.
  evfs_off_t file_size = f_size(&fil->fil);
  if(size >= file_size)
    size = file_size-1;

  if(cur_pos > size)
    cur_pos = size;

  FRESULT status = f_lseek(&fil->fil, size);
  if(status == FR_OK)
    status = f_truncate(&fil->fil);

  // Restore original position
  f_lseek(&fil->fil, cur_pos);

  return translate_error(status);
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}

static int fatfs__file_sync(EvfsFile *fh) {
#if FF_FS_READONLY == 0
  FatfsFile *fil = (FatfsFile *)fh;
  return simple_error(f_sync(&fil->fil));
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}

static evfs_off_t fatfs__file_size(EvfsFile *fh) {
  FatfsFile *fil = (FatfsFile *)fh;

  FSIZE_t size = f_size(&fil->fil);
  return size;
}

static int fatfs__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  FatfsFile *fil = (FatfsFile *)fh;

  offset = evfs__absolute_offset(fh, offset, origin);

/*  // Compute absolute offset from start of file
  switch(origin) {
    case EVFS_SEEK_TO: // No change
      break;

    case EVFS_SEEK_REL:
      offset += f_tell(&fil->fil);
      break;

    case EVFS_SEEK_REV:
      offset = f_size(&fil->fil) - offset;
      break;

    default:
      break;
  }*/

  if(ASSERT(offset >= 0, "Invalid offset")) return EVFS_ERR;

  return translate_error(f_lseek(&fil->fil, offset));
}

static evfs_off_t fatfs__file_tell(EvfsFile *fh) {
  FatfsFile *fil = (FatfsFile *)fh;
  return f_tell(&fil->fil);
}

static bool fatfs__file_eof(EvfsFile *fh) {
  FatfsFile *fil = (FatfsFile *)fh;
  return f_eof(&fil->fil);
}



static EvfsFileMethods s_fatfs_methods = {
  .m_ctrl     = fatfs__file_ctrl,
  .m_close    = fatfs__file_close,
  .m_read     = fatfs__file_read,
  .m_write    = fatfs__file_write,
  .m_truncate = fatfs__file_truncate,
  .m_sync     = fatfs__file_sync,
  .m_size     = fatfs__file_size,
  .m_seek     = fatfs__file_seek,
  .m_tell     = fatfs__file_tell,
  .m_eof      = fatfs__file_eof
};



// ******************** Directory access methods ********************


static int fatfs__dir_close(EvfsDir *dh) {
  FatfsDir *dir = (FatfsDir *)dh;
  return simple_error(f_closedir(&dir->dir));
}

/*
DWORD get_fattime(void) {
  time_t now_secs;

  time(&now_secs);
  struct tm now;
  localtime_r(&now_secs, &now);

  DWORD ftime = ((now.tm_year-80) << 25) | ((now.tm_mon+1) << 21) |
    (now.tm_mday << 16) | (now.tm_hour << 11) |
    (now.tm_min << 5) | (now.tm_sec >> 1);

  return ftime;
}

*/


static int fatfs__dir_read(EvfsDir *dh, EvfsInfo *info) {
  FatfsDir *dir = (FatfsDir *)dh;

  FRESULT status = f_readdir(&dir->dir, &dir->info);

  memset(info, 0, sizeof(*info));

  if(status == FR_OK && dir->info.fname[0] != '\0') {
    info->name = dir->info.fname;
    info->size = dir->info.fsize;

    if(dir->info.fattrib & AM_DIR)
      info->type |= EVFS_FILE_DIR;

    // Convert FAT timestamp to time_t
    struct tm mtime = {0};

    mtime.tm_mday = dir->info.fdate & 0x1F;
    dir->info.fdate >>= 5;

    mtime.tm_mon = (dir->info.fdate & 0x0F) - 1;
    dir->info.fdate >>= 4;
    
    mtime.tm_year = (dir->info.fdate & 0x7F) + 80;

    mtime.tm_sec = (dir->info.ftime & 0x1F) << 1;
    dir->info.ftime >>= 5;

    mtime.tm_min = dir->info.ftime & 0x3F;
    dir->info.ftime >>= 6;

    mtime.tm_hour = dir->info.ftime & 0x1F;

    info->mtime = mktime(&mtime);

  } else { // End of entries
    info->name = NULL;
  }

  return info->name ? EVFS_OK : EVFS_DONE;
}


static int fatfs__dir_rewind(EvfsDir *dh) {
  FatfsDir *dir = (FatfsDir *)dh;
  return translate_error(f_rewinddir(&dir->dir));
}


static EvfsDirMethods s_fatfs_dir_methods = {
  .m_close    = fatfs__dir_close,
  .m_read     = fatfs__dir_read,
  .m_rewind   = fatfs__dir_rewind
};




// ******************** FS access methods ********************

static int fatfs__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;
  FatfsFile *fil = (FatfsFile *)fh;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_fatfs_methods;

  if((flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND)) && fs_data->cfg_readonly)
    return EVFS_ERR_DISABLED;

  BYTE fs_flags = 0;

  if(flags & EVFS_READ)        fs_flags |= FA_READ;
  if(flags & EVFS_WRITE)       fs_flags |= FA_WRITE;
  if(flags & EVFS_OPEN_OR_NEW) fs_flags |= FA_OPEN_ALWAYS;
  if(flags & EVFS_NO_EXIST)    fs_flags |= FA_CREATE_NEW;
  if(flags & EVFS_OVERWRITE)   fs_flags |= FA_CREATE_ALWAYS;
  if(flags & EVFS_APPEND)      fs_flags |= FA_OPEN_APPEND;

  fil->fs_data = fs_data;
  fil->pdrv = fs_data->pdrv;
  
  FRESULT status;
  status = f_open(&fil->fil, path, fs_flags);

  return translate_error(status);
}


static int fatfs__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  FRESULT status;

  status = f_stat(path, &fs_data->info);

  memset(info, 0, sizeof(*info));

  if(status == FR_OK) {
    info->name = fs_data->info.fname;
    info->size = fs_data->info.fsize;

    if(fs_data->info.fattrib & AM_DIR)
      info->type |= EVFS_FILE_DIR;
  } else {
    info->name = NULL;
  }

  return translate_error(status);
}

static int fatfs__delete(Evfs *vfs, const char *path) {
#if FF_FS_READONLY == 0
  FRESULT status;
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  status = f_unlink(path);
  return translate_error(status);
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}


static int fatfs__rename(Evfs *vfs, const char *old_path, const char *new_path) {
#if FF_FS_READONLY == 0
  FRESULT status;
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  status = f_rename(old_path, new_path);
  return translate_error(status);
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}



static int fatfs__make_dir(Evfs *vfs, const char *path) {
#if FF_FS_READONLY == 0
  FRESULT status;
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  if(fs_data->cfg_readonly) return EVFS_ERR_DISABLED;

  status = f_mkdir(path);
  return translate_error(status);
#else
  THROW(EVFS_ERR_NO_SUPPORT);
#endif
}

static int fatfs__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  FatfsDir *dir = (FatfsDir *)dh;

  memset(dir, 0, sizeof(*dir));
  dh->methods = &s_fatfs_dir_methods;
  //dir->fs = fs_data->fs;
  dir->pdrv = fs_data->pdrv;

  FRESULT status;

  status = f_opendir(&dir->dir, path);
  return translate_error(status);

}



static int fatfs__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  FRESULT status = f_getcwd((char *)cur_dir->start, range_size(cur_dir));
  return translate_error(status);
}

static int fatfs__set_cur_dir(Evfs *vfs, const char *path) {
  FRESULT status = f_chdir(path);
  return translate_error(status);
}


static int fatfs__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  FatfsData *fs_data = (FatfsData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER:
      evfs_free(vfs);
      return EVFS_OK; break;

    case EVFS_CMD_SET_READONLY:
      {
        unsigned *v = (unsigned *)arg;
        fs_data->cfg_readonly = !!*v;
      }
      return EVFS_OK; break;

    case EVFS_CMD_GET_STAT_FIELDS:
      {
        unsigned *v = (unsigned *)arg;
        *v = EVFS_INFO_NAME | EVFS_INFO_SIZE | EVFS_INFO_MTIME | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    case EVFS_CMD_GET_DIR_FIELDS:
      {
        unsigned *v = (unsigned *)arg;
        *v = EVFS_INFO_NAME | EVFS_INFO_SIZE | EVFS_INFO_MTIME | EVFS_INFO_TYPE;
      }
      return EVFS_OK; break;

    default: return EVFS_ERR_NO_SUPPORT; break;
  }
}


// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])


/*
Register a FatFs instance

Args:
  vfs_name:      Name of new VFS
  pdrv:          FatFs volume number
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_fatfs(const char *vfs_name, uint8_t pdrv, bool default_vfs) {
  if(PTR_CHECK(vfs_name)) return EVFS_ERR_BAD_ARG;
  Evfs *new_vfs;
  FatfsData *fs_data;

  // Construct a new VFS
  // We have three objects allocated together [Evfs][FatfsData][char[]]
  size_t alloc_size = sizeof(*new_vfs) + sizeof(*fs_data) + strlen(vfs_name)+1;
  new_vfs = evfs_malloc(alloc_size);
  if(MEM_CHECK(new_vfs)) return EVFS_ERR_ALLOC;

  memset(new_vfs, 0, alloc_size);

  // Prepare new objects
  fs_data = (FatfsData *)NEXT_OBJ(new_vfs);

  new_vfs->vfs_name = (char *)NEXT_OBJ(fs_data);
  strcpy((char *)new_vfs->vfs_name, vfs_name);

  // Init FS data
  fs_data->pdrv = pdrv;

  // Init VFS
  new_vfs->vfs_file_size = sizeof(FatfsFile);
  new_vfs->vfs_dir_size = sizeof(FatfsDir);
  new_vfs->fs_data = fs_data;

  // Required methods
  new_vfs->m_open = fatfs__open;
  new_vfs->m_stat = fatfs__stat;

  // Optional methods
  new_vfs->m_delete = fatfs__delete;
  new_vfs->m_rename = fatfs__rename;
  new_vfs->m_make_dir = fatfs__make_dir;
  new_vfs->m_open_dir = fatfs__open_dir;
  new_vfs->m_get_cur_dir = fatfs__get_cur_dir;
  new_vfs->m_set_cur_dir = fatfs__set_cur_dir;
  new_vfs->m_vfs_ctrl = fatfs__vfs_ctrl;
  
  new_vfs->m_path_root_component = fatfs__path_root_component;

  return evfs_register(new_vfs, default_vfs);
}

