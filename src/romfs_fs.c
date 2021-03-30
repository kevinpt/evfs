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

#include "evfs.h"
#include "evfs_internal.h"

#include "evfs/util/unaligned_access.h"

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


#define ROMFS_MAX_NAME_LEN 32

#define ROMFS_MAX_HEADER_SIZE ((16 + ROMFS_MAX_NAME_LEN + 15) & ~0xF)
#define ROMFS_MIN_HEADER_SIZE (16 + 2)

typedef struct RomfsFileHead {
  uint32_t offset; // next file header in binary format
  uint32_t spec_info;
  uint32_t size;
  uint32_t header_len; // This is the checksum in the romfs binary format
  char file_name[ROMFS_MAX_NAME_LEN];
} RomfsFileHead;

#define FILE_TYPE_MASK  0x07
#define FILE_EX_MASK    0x08

#define FILE_OFFSET(hdr) ((hdr)->offset & ~0xF)
#define FILE_TYPE(hdr) ((hdr)->offset & FILE_TYPE_MASK)

#define FILE_TYPE_HARD_LINK     0
#define FILE_TYPE_DIRECTORY     1
#define FILE_TYPE_REGULAR_FILE  2
#define FILE_TYPE_SYM_LINK      3
#define FILE_TYPE_BLOCK_DEV     4
#define FILE_TYPE_CHAR_DEV      5
#define FILE_TYPE_SOCKET        6
#define FILE_TYPE_FIFO          7




typedef struct RomfsData {
  Evfs       *vfs;
  EvfsFile   *image;
  evfs_off_t  root_dir;

  char      cur_dir[EVFS_MAX_PATH];
#ifdef EVFS_USE_ROMFS_SHARED_BUFFER
  char      abs_path[EVFS_MAX_PATH]; // Shared buffer for building absolute paths
#endif
#ifdef USE_ROMFS_LOCK
  EvfsLock  romfs_lock; // Serialize access to shared abs_path buffer
#endif

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
  RomfsFileHead cur_file; // Iterator position
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
 int status = make_absolute_path(vfs, path, &abs_path, false); \
 if(status != EVFS_OK) \
    return status; \
} while(0)


#ifdef EVFS_USE_ROMFS_SHARED_BUFFER
#  define FREE_ABS(abs_path)  UNLOCK()
#else
#  define FREE_ABS(abs_path)  evfs_free(abs_path)
#endif


// ******************** Romfs core ********************

static inline int file_header_len(RomfsFileHead *hdr) {
  // Header = 16 bytes + file_name
  // Round up to 16-byte boundary
  return (16 + strnlen(hdr->file_name, ROMFS_MAX_NAME_LEN-1)+1 + 15) & ~0xF;
}


// Read Romfs file header from image
static bool romfs__read_file_header(RomfsData *fs, long hdr_pos, RomfsFileHead *hdr) {
  evfs_file_seek(fs->image, hdr_pos, EVFS_SEEK_TO);

  memset(hdr, 0, sizeof(*hdr));

  ptrdiff_t buf_len = evfs_file_read(fs->image, hdr, sizeof(*hdr));
  if(buf_len < ROMFS_MIN_HEADER_SIZE)
    return false;

  int header_len = file_header_len(hdr);

  // Verify checksum
  int32_t checksum = 0;
  for(int i = 0; i < header_len / 4; i++) {
    checksum += get_unaligned_be(&((uint32_t *)hdr)[i]);
  }

  if(checksum != 0)
    return false;

  hdr->offset       = get_unaligned_be(&hdr->offset);
  hdr->spec_info    = get_unaligned_be(&hdr->spec_info);
  hdr->size         = get_unaligned_be(&hdr->size);
  hdr->header_len   = header_len; // Overwrite checksum

  return true;
}

// Scan a directory for file from a path
static bool romfs__find_path_elem(RomfsData *fs, evfs_off_t dir_pos, StringRange *element, RomfsFileHead *hdr) {
  evfs_off_t cur_hdr = dir_pos;

  while(cur_hdr != 0) {
    if(!romfs__read_file_header(fs, cur_hdr, hdr))
      break;

    if(range_eq(element, hdr->file_name)) {
      hdr->offset = cur_hdr | (hdr->offset & 0xF); // Replace with offset of the element
      return true;
    }

    cur_hdr = FILE_OFFSET(hdr);
  }

  return false;
}


static int romfs__lookup_abs_path(RomfsData *fs, const char *path, RomfsFileHead *hdr) {
  evfs_off_t dir_pos = fs->root_dir;

  StringRange element;

  int status = evfs_vfs_scan_path(fs->vfs, path, &element); // Get first element in path
  bool end_scan;
  while(status == EVFS_OK) {
    int file_type = -1;
    end_scan = false;

    if(romfs__find_path_elem(fs, dir_pos, &element, hdr)) {
      file_type = FILE_TYPE(hdr);
      switch(file_type) {

      case FILE_TYPE_HARD_LINK:
        dir_pos = hdr->spec_info;  // Follow link dest
        romfs__read_file_header(fs, dir_pos, hdr); // Reparse header (should never fail)
        dir_pos = hdr->spec_info; // First file in dir
        //DPRINT("FOLLOW LINK: %08X", dir_pos);
        break;

      case FILE_TYPE_DIRECTORY:
        dir_pos = hdr->spec_info; // First file in dir
        //DPRINT("FOLLOW DIR: %.*s  %08X --> %08X", RANGE_FMT(&element), FILE_OFFSET(hdr), dir_pos);
        break;

      default: // Should be last element of path
        end_scan = true;
        break;
      }

    } else { // No match for element
      return EVFS_ERR_NO_PATH;
    }

    status = evfs_vfs_scan_path(fs->vfs, NULL, &element); // Get next element in path

    if(end_scan) { // Element is not a directory or link
      // If we terminated path on last element then status should be EVFS_ERR
      return (status == EVFS_ERR) ? EVFS_OK : EVFS_ERR_NO_PATH;

    } else { // A directory or link
      if(status == EVFS_ERR) // If path scan terminated we are done
        return EVFS_OK;
    }
  }

  return status;
}


static int romfs__image_validate(RomfsData *fs) {
  evfs_file_rewind(fs->image);

  // Read first 512 bytes or whatever is in image so we can
  // get superblock and validate checksum.

  uint32_t buf[512/4] = {0};
  ptrdiff_t buf_len = evfs_file_read(fs->image, buf, 4*COUNT_OF(buf));
  if(ASSERT(buf_len >= ROMFS_MIN_HEADER_SIZE, "Romfs too small"))
    return EVFS_ERR_INVALID;

  // Force magic number string to little-endian
  buf[0] = get_unaligned_le(&buf[0]);
  buf[1] = get_unaligned_le(&buf[1]);

  if(buf[0] == 0x6D6F722D && buf[1] == 0x2D736631) {
    uint32_t fs_bytes = get_unaligned_be(&buf[2]);

    if(ASSERT(fs_bytes <= evfs_file_size(fs->image), "Invalid Romfs size"))
      return EVFS_ERR_INVALID;

    int32_t checksum = 0;
    buf_len /= 4;
    for(int i = 0; i < buf_len; i++) {
      checksum += get_unaligned_be(&buf[i]);
    }

    if(checksum == 0) { // Valid superblock
      char *vol_name = (char *)&buf[4];
      //DPRINT("VOLUME: '%s'", vol_name);

      // Root dir starts at first file header
      fs->root_dir = (16 + strnlen(vol_name, ROMFS_MAX_NAME_LEN-1)+1 + 15) & ~0xF;
      return EVFS_OK;
    }
  }

  return EVFS_ERR_INVALID;
}


// ******************** File access methods ********************

static int romfs__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  return EVFS_ERR_NO_SUPPORT;
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

  if(size > remaining)
    size = remaining;

  LOCK();
  evfs_file_seek(fs_data->image, fil->hdr.offset + fil->hdr.header_len + fil->read_pos, EVFS_SEEK_TO);
  fil->read_pos += size;
  ptrdiff_t rval = evfs_file_read(fs_data->image, buf, size);
  UNLOCK();

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

  if(offset > fil->hdr.size)
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
  return fil->read_pos >= fil->hdr.size;
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
    romfs__read_file_header(fs_data, dir->dir_pos, &dir->cur_file);
    next_entry = dir->cur_file.spec_info;
    dir->is_reset = false;

    if(fs_data->cfg_no_dir_dots) { // Skip over hard links
      romfs__read_file_header(fs_data, next_entry, &dir->cur_file); // Skip "."
      romfs__read_file_header(fs_data, FILE_OFFSET(&dir->cur_file), &dir->cur_file); // Skip ".."
      next_entry = FILE_OFFSET(&dir->cur_file);
    }

  } else { // Already initialized
    next_entry = FILE_OFFSET(&dir->cur_file);
  }

  if(next_entry > 0)
    status = romfs__read_file_header(fs_data, next_entry, &dir->cur_file) ? EVFS_OK : EVFS_DONE;
  else
    status = EVFS_DONE;


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

static int romfs__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;
  RomfsFile *fil = (RomfsFile *)fh;

  memset(fil, 0, sizeof(*fil));
  fh->methods = &s_romfs_methods;

  if(flags & (EVFS_WRITE | EVFS_OPEN_OR_NEW | EVFS_OVERWRITE | EVFS_APPEND))
    return EVFS_ERR_NO_SUPPORT;


  fil->fs_data = fs_data;

  int status;

  if(evfs_vfs_path_is_absolute(vfs, path)) {
    status = romfs__lookup_abs_path(fs_data, path, &fil->hdr);

  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = romfs__lookup_abs_path(fs_data, abs_path, &fil->hdr);
    FREE_ABS(abs_path);
  }

  // Must be a plain file
  int file_type = FILE_TYPE(&fil->hdr);
  if(file_type != FILE_TYPE_REGULAR_FILE) {
    status = EVFS_ERR_NO_FILE;
  }

  return status;
}


static int romfs__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;

  int status;
  RomfsFileHead hdr;

  if(evfs_vfs_path_is_absolute(vfs, path)) {
    status = romfs__lookup_abs_path(fs_data, path, &hdr);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = romfs__lookup_abs_path(fs_data, abs_path, &hdr);
    FREE_ABS(abs_path);
  }

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

  RomfsDir *dir = (RomfsDir *)dh;

  memset(dir, 0, sizeof(*dir));
  dh->methods = &s_romfs_dir_methods;
  dir->fs_data = fs_data;

  int status;
  RomfsFileHead hdr;

  if(evfs_vfs_path_is_absolute(vfs, path)) {  
    status = romfs__lookup_abs_path(fs_data, path, &hdr);
  } else { // Convert relative path
    MAKE_ABS(path, abs_path);
    status = romfs__lookup_abs_path(fs_data, abs_path, &hdr);
    FREE_ABS(abs_path);
  }

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



static int romfs__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  RomfsData *fs_data = (RomfsData *)vfs->fs_data;

  switch(cmd) {
    case EVFS_CMD_UNREGISTER:
      evfs_file_close(fs_data->image);
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

/*
Register a Romfs instance

Args:
  vfs_name:      Name of new VFS
  lfs:           Mounted littlefs object
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_romfs(const char *vfs_name, EvfsFile *image, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(image)) return EVFS_ERR_BAD_ARG;
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
  fs_data->image  = image;
  fs_data->vfs    = new_vfs;
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

  // Validate image superblock
  int status = romfs__image_validate(fs_data);
  if(status != EVFS_OK)
    return status;

  return evfs_register(new_vfs, default_vfs);
}


