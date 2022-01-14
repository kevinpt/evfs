/* SPDX-License-Identifier: MIT
Copyright 2020, 2022 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  File rotation shim VFS

This shim implements virtual self-rotating log files useful for logging data.
Older file contents are gradually purged once the log file reaches its
maximum size. 

************************************************************************
WARNING: Do not use this for important data. There are latent race
conditions that can cause data loss.
************************************************************************

The virtual files are represented as a container directory in the underlying
filesystem. This directory contains a configuration file recording the
geometry settings the container was created with and multiple chunk files
that contain segments of the file's data. Chunks have a fixed size and there
is a maximum number of chunks set on creation. You can have no more than
99999 chunks. The minimum chunk size is limited to 32 bytes to protect against
excessive filesystem activity.

When the container is accessed through this shim it will appear as a single
continuous file of data. You can perform all normal file operations on an
open file handle. Understand that as rotation happens the offsets of the
file contents will change. You should not access a container simultaneously
through multiple file handles as they will not be synchronized. Use append
mode writes to add data to the end of the file.

The chunking algorithm is designed to work on systems that don't record
timestamps. When a container is opened the chunks are scanned to find the
start and end of the sequence using the gen flag encoded into the file
names. If you perform random access writes in the middle of the file there is
a risk of a chunk disappearing or becoming zero length if a system fault
happens.

The rotation process only involves deleting the oldest chunk at the start of
the file. This minimizes the amount of filesystem activity on flash based
filesystems.

Rotation will leave portions of data spanning the chunk boundary at the new
start of the file. For text files the first line will be missing an initial
portion. You can trim off this first fragmentary line by scanning for the
newline. With binary data you have to be prepared to lose a portion of a record
unless you always write a fixed record size that is an integral factor of the
chunk size. Another approach is to accumulate data for a chunk until it's
nearly full with padding added to ensure no data spans a chunk boundary.
Otherwise you will need to have some form or synchronizing information stored
periodically so you can skip past the truncated data remaining at the start of
the file.

The initial container configuration settings are passed to
evfs_register_rotate() when the shim is installed. If you need to change the
settings you can send a new RotateConfig struct to the shim using the 
EVFS_CMD_SET_ROTATE_CFG as the operation evfs_vfs_ctrl().

------------------------------------------------------------------------------
*/


#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/shim/shim_rotate.h"

// Containers with missing chunks can't be worked with.
// We repair them by removing enough chunks to make a valid sequence.

// Access objects allocated in a single block of memory
#define NEXT_OBJ(o) (&(o)[1])


#define MULTIPART_GEOMETRY_FILE  "geom.dat"

#define EVFS_MULTI_MAGIC         0x53465645
#define EVFS_MULTI_ROTATE_TYPE   0x01

#define CUR_MULTI_ROTATE_VERSION 1

// If you need more chunks this is probably not a good fit for your storage needs
#define MULTIPART_MAX_CHUNK       99999

// Protect against chunks too small and thrashing the filesystem
#define MULTIPART_MIN_CHUNK_SIZE  32

// MSVC
#ifdef _MSC_VER
#  define PACKED_BEGIN   __pragma(pack(push, 1))
#  define PACKED_END     __pragma(pack(pop))

// IAR / Keil
#elif defined __IAR_SYSTEMS_ICC__ || defined __UVISION_VERSION
#  define PACKED_BEGIN   __packed
#  define PACKED_END

// GCC/Clang (C11 fallback)
#else
#  define PACKED_BEGIN   _Pragma("pack(push, 1)")
#  define PACKED_END     _Pragma("pack(pop)")
#endif


// ******************** Structs for container files ********************

PACKED_BEGIN
struct MultipartHeader {
  uint32_t  magic;
  uint8_t   type;
  uint8_t   version;
  uint16_t  reserved;
};
PACKED_END

typedef struct MultipartHeader MultipartHeader;

// Config data written to geom.dat
PACKED_BEGIN
struct RotateGeometry {
  uint32_t      chunk_size;
  uint32_t      max_chunks;
};
PACKED_END

typedef struct RotateGeometry RotateGeometry;



// ******************** Shim structs ********************

// Shared buffer needs lock if threading is enabled
#if defined EVFS_USE_ROTATE_SHARED_BUFFER && defined EVFS_USE_THREADING
#  define USE_ROT_LOCK
#endif


#ifdef USE_ROT_LOCK
#  define LOCK(sb)    evfs__lock(&(sb)->buf_lock)
#  define UNLOCK(sb)  evfs__unlock(&(sb)->buf_lock)
#else
#  define LOCK(sb)
#  define UNLOCK(sb)
#endif


typedef struct SharedBuffer {
  char        tmp_path[EVFS_MAX_PATH]; // Shared buffer for building temp paths
#ifdef USE_ROT_LOCK
  EvfsLock     buf_lock; // Serialize access to shared tmp_path buffer
#endif
} SharedBuffer;

typedef struct {
  int chunk;
  int gen; // Generation flag: 0 or 1
} ChunkId;

typedef struct MultipartState {
  const char *container_path;
  int         flags;
  evfs_off_t  chunk_size;
  evfs_off_t  total_size;
  evfs_off_t  file_pos;     // Logical position for reads and writes in non-append mode
  ChunkId     active_chunk;
  EvfsFile   *active_chunk_fh;

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  SharedBuffer *buf;
#endif
} MultipartState;


typedef struct ChunkPos {
  evfs_off_t offset;
  ChunkId    chunk_num;
} ChunkPos;



typedef struct RotateState {
  MultipartState  base;
  RotateConfig    cfg;  // Configuration read from the geometry file

  ChunkId start_chunk;  // First chunk in the logical file. It always follows a gap in the chunk sequence
  ChunkId end_chunk;    // Last chunk in logical file
} RotateState;


typedef struct RotateData {
  Evfs       *base_vfs;
  const char *vfs_name;
  Evfs       *shim_vfs;

  RotateConfig cfg;  // Configuration for new containers
#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  SharedBuffer buf;
#endif
} RotateData;

typedef struct RotateFile {
  EvfsFile    base;
  RotateData *shim_data;
  EvfsFile   *base_file;

  RotateState *rot_state;
} RotateFile;

typedef struct RotateDir {
  EvfsDir     base;
  RotateData *shim_data;
  EvfsDir    *base_dir;
} RotateDir;



// ******************** Internal rotation API ********************


static evfs_off_t cur_write_pos(MultipartState *ms) {
  evfs_off_t wpos = (ms->flags & EVFS_APPEND) ? ms->total_size : ms->file_pos;
  //DPRINT("ROT write pos %d  append:%c  (%p)", wpos, (ms->flags & EVFS_APPEND) ? 'T' :'f', ms);

  return wpos;
}


static void incr_write_pos(MultipartState *ms, evfs_off_t offset) {
  if(!(ms->flags & EVFS_APPEND))
    ms->file_pos += offset;
}



// Method
static int init_rotate_container(Evfs *base_vfs, const char *path, RotateConfig *cfg) {
  // Create the multipart container directory
  int status = base_vfs->m_make_dir(base_vfs, path);
  if(status != EVFS_OK) return status;

  // Add dat file
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
  evfs_vfs_path_join_str(base_vfs, path, MULTIPART_GEOMETRY_FILE, &joined_r);

  EvfsFile *dat_fh;
  status = evfs_vfs_open(base_vfs, joined, &dat_fh, EVFS_WRITE | EVFS_OVERWRITE);
  if(status != EVFS_OK) return status;

  // Write header
  MultipartHeader hdr = {
    .magic   = EVFS_MULTI_MAGIC,
    .type    = EVFS_MULTI_ROTATE_TYPE,
    .version = CUR_MULTI_ROTATE_VERSION
  };

  status = evfs_file_write(dat_fh, &hdr, sizeof(hdr));

  if(status > 0) {
    // Write geometry
    RotateGeometry geom = {
      .chunk_size = cfg->chunk_size,
      .max_chunks = cfg->max_chunks
    };

    status = evfs_file_write(dat_fh, &geom, sizeof(geom));

  }

  evfs_file_close(dat_fh);

  return status == sizeof(RotateGeometry) ? EVFS_OK : EVFS_ERR;
}


static int clear_container(Evfs *base_vfs, const char *path) {
  // Remove all files in the path directory
  EvfsDir *dh;
  EvfsInfo info;

  int status = evfs_vfs_open_dir(base_vfs, path, &dh);
  if(status != EVFS_OK) return status;

  while(evfs_dir_read(dh, &info) != EVFS_DONE) {
    // Check for dir dots
    if(info.name[0] == '.') continue;

    char joined[EVFS_MAX_PATH];
    StringRange joined_r = RANGE_FROM_ARRAY(joined);
    evfs_vfs_path_join_str(base_vfs, path, info.name, &joined_r);
    base_vfs->m_delete(base_vfs, joined);
  }

  evfs_dir_close(dh);

  return status;
}


static bool is_rotate_file(Evfs *base_vfs, const char *path) {
  // Container directory has to exist
  EvfsInfo info;
  int status = base_vfs->m_stat(base_vfs, path, &info);

  if(status != EVFS_OK || !(info.type & EVFS_FILE_DIR))
    return false;

  // Multipart info file needs to exist
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
  evfs_vfs_path_join_str(base_vfs, path, MULTIPART_GEOMETRY_FILE, &joined_r);

  status = base_vfs->m_stat(base_vfs, joined, &info);
  return status == EVFS_OK;
}


static int build_chunk_path(Evfs *base_vfs, MultipartState *ms, ChunkId chunk_num,
                            StringRange *chunk_path) {
  char chunk_file[19];
  snprintf(chunk_file, COUNT_OF(chunk_file), "c%05d_%c.cnk", chunk_num.chunk,
          chunk_num.gen ? '1' : '0');
  return evfs_vfs_path_join_str(base_vfs, ms->container_path, chunk_file, chunk_path);
}


static bool chunk_exists(Evfs *base_vfs, MultipartState *ms, ChunkId chunk_num, evfs_off_t *chunk_size) {

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  LOCK(ms->buf);
  char *joined = ms->buf->tmp_path;
  StringRange joined_r;
  range_init(&joined_r, joined, COUNT_OF(ms->buf->tmp_path));
#else
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
#endif
  build_chunk_path(base_vfs, ms, chunk_num, &joined_r);

  EvfsInfo info;
  int status = base_vfs->m_stat(base_vfs, joined, &info);
  UNLOCK(ms->buf);

  if(chunk_size)
    *chunk_size = (status == EVFS_OK) ? info.size : 0;

  return status == EVFS_OK;
}


// Method
static ChunkPos get_chunk_pos(RotateState *rs, evfs_off_t logical_off) {
  uint32_t raw_chunk = logical_off / rs->cfg.chunk_size;
  evfs_off_t offset = logical_off % rs->cfg.chunk_size;

  // Raw chunk number always starts from 0
  // Valid range is 0 to max_chunks-1
  if(raw_chunk >= rs->cfg.max_chunks) { // Too large for logical file
    raw_chunk = rs->cfg.max_chunks; // Set to beyond last chunk
    offset = 0;
  }

  ChunkPos pos;
  // Offset by start of logical file and wrap to upper bound
  pos.chunk_num.chunk = (raw_chunk + rs->start_chunk.chunk) % rs->cfg.max_chunks;
  pos.chunk_num.gen = (pos.chunk_num.chunk < rs->start_chunk.chunk) ?
                              1 - rs->start_chunk.gen : rs->start_chunk.gen;
  pos.offset = offset;
  return pos;
}



static int evict_chunk(Evfs *base_vfs, MultipartState *ms, ChunkId chunk_num, evfs_off_t *chunk_size) {
  //DPRINT("EVICT: %d_%d", chunk_num.chunk, chunk_num.gen);

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  LOCK(ms->buf);
  char *joined = ms->buf->tmp_path;
  StringRange joined_r;
  range_init(&joined_r, joined, COUNT_OF(ms->buf->tmp_path));
#else
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
#endif
  build_chunk_path(base_vfs, ms, chunk_num, &joined_r);

  EvfsInfo info;
  int status = base_vfs->m_stat(base_vfs, joined, &info);
  if(status == EVFS_OK)
    status = base_vfs->m_delete(base_vfs, joined);

  UNLOCK(ms->buf);

  if(status == EVFS_OK)
    ms->total_size -= info.size;

  if(chunk_size)
    *chunk_size = (status == EVFS_OK) ? info.size : 0;

  return status;
}


static inline void incr_chunk(RotateState *rs, ChunkId *id) {
  id->chunk++;
  if(id->chunk >= (int)rs->cfg.max_chunks) {
    id->chunk = 0;
    id->gen = 1 - id->gen;
  }
}


static inline ChunkId next_chunk(RotateState *rs) {
  // Next chunk always follows the end chunk
  ChunkId next = rs->end_chunk;
  incr_chunk(rs, &next);
  return next;
}


static inline void deactivate_chunk(MultipartState *ms) {
  // Shutdown current active chunk
  if(ms->active_chunk_fh) {
    evfs_file_close(ms->active_chunk_fh);
    ms->active_chunk_fh = NULL;
    ms->active_chunk.chunk = -1;
  }
}


static int activate_chunk(Evfs *base_vfs, MultipartState *ms, ChunkId chunk_num) {
  //DPRINT("ROT Activate chunk: %d_%d", chunk_num.chunk, chunk_num.gen);

  if(ms->active_chunk.chunk == chunk_num.chunk) // Already opened chunk
    return EVFS_OK;

  // Shutdown current active chunk
  deactivate_chunk(ms);

  // Open new chunk
  EvfsFile *fh;

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  LOCK(ms->buf);
  char *joined = ms->buf->tmp_path;
  StringRange joined_r;
  range_init(&joined_r, joined, COUNT_OF(ms->buf->tmp_path));
#else
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
#endif
  build_chunk_path(base_vfs, ms, chunk_num, &joined_r);

  int status = evfs_vfs_open(base_vfs, joined, &fh, EVFS_RDWR);

  UNLOCK(ms->buf);

  if(status == EVFS_OK) {
    ms->active_chunk_fh = fh;
    ms->active_chunk = chunk_num;
  }

  return status;
}


// Method
static int append_new_chunk(Evfs *base_vfs, RotateState *rs, EvfsFile **fh) {
  bool is_empty = rs->start_chunk.chunk == rs->end_chunk.chunk &&
                  !chunk_exists(base_vfs, &rs->base, rs->end_chunk, NULL);
  ChunkId next = rs->end_chunk;

  //DPRINT("ROT append  empty:%c", is_empty ? 'T':'f');

  if(!is_empty) {
    next = next_chunk(rs);

    if(next.chunk >= MULTIPART_MAX_CHUNK)
      return EVFS_ERR_TOO_LONG;

    // Remove the old start chunk on wrap around
    if(next.chunk == rs->start_chunk.chunk) {
      evict_chunk(base_vfs, &rs->base, rs->start_chunk, NULL);

      incr_chunk(rs, &rs->start_chunk);

      // Adjust read/write pos to reflect trimmed file
      if(rs->base.file_pos > (evfs_off_t)rs->cfg.chunk_size)
        rs->base.file_pos -= rs->cfg.chunk_size;
      else
        rs->base.file_pos = 0;
    }

    rs->end_chunk = next;
  }

  //DPRINT("ROT: Append new chunk: %d_%d", rs->end_chunk.chunk, rs->end_chunk.gen);

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  LOCK(rs->base.buf);
  char *joined = rs->base.buf->tmp_path;
  StringRange joined_r;
  range_init(&joined_r, joined, COUNT_OF(rs->base.buf->tmp_path));
#else
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
#endif

  // Add new chunk file
  build_chunk_path(base_vfs, &rs->base, next, &joined_r);

  // Open file handle is returned through fh
  int status = evfs_vfs_open(base_vfs, joined, fh, EVFS_RDWR | EVFS_OVERWRITE);

  UNLOCK(rs->base.buf);

  return status;
}


static ChunkId parse_chunk_name(const char *name) {
  ChunkId id = {-1,0};

  if(strlen(name) == 12) {
    id.chunk = strtol(&name[1], NULL, 10);
    id.gen = name[7] - '0';

    DPRINT("ROT name: %s --> %d_%d", name, id.chunk, id.gen);
  }

  return id;
}


static int discover_chunk_sequence(Evfs *base_vfs, RotateState *rs) {
  // Scan directory for chunks
  evfs_off_t chunk_size;
  int status = EVFS_OK;

  // Loop over all chunks in the container sequence.
  // This lets us identify where the gen bit flips

  struct span {
    int start;
    int end;
  };

  struct span gen_span[2] = {
    {MULTIPART_MAX_CHUNK, -1},
    {MULTIPART_MAX_CHUNK, -1}
  };

  rs->base.total_size = 0;

  // Scan chunks
  EvfsDir *dh;
  if(evfs_vfs_open_dir(base_vfs, rs->base.container_path, &dh) == EVFS_OK) {
    EvfsInfo info;
    while(evfs_dir_find(dh, "c*.cnk", &info) == EVFS_OK) {
      ChunkId id = parse_chunk_name(info.name);
      bool exists = chunk_exists(base_vfs, &rs->base, id, &chunk_size);

      // It's possible a 0-length chunk was leftover from a previous
      // power failure. We'll clean it up now. This should just leave
      // us with a gap between generations unless more significant
      // errors have happened.
      if(exists && chunk_size == 0) {
        //DPRINT("ROT evict zero length chunk");
        evict_chunk(base_vfs, &rs->base, id, NULL);
        exists = false;
      }

      if(exists) {
        gen_span[id.gen].start  = MIN(gen_span[id.gen].start, id.chunk);
        gen_span[id.gen].end    = MAX(gen_span[id.gen].end, id.chunk);

        rs->base.total_size += chunk_size;
        //DPRINT("ROT found chunk %d_%d  size:%d (%p)", id.chunk, id.gen, rs->base.total_size,
        //        &rs->base);
      }

    }
    evfs_dir_close(dh);
  }

  if(gen_span[0].end >= 0 || gen_span[1].end >= 0) { // Found chunk files
    if(gen_span[0].end >= 0 && gen_span[1].end >= 0) { // Both generations present
      if(gen_span[0].start < gen_span[1].start) { // Gen 0 is newer
        rs->start_chunk = (ChunkId){gen_span[1].start, 1};
        rs->end_chunk   = (ChunkId){gen_span[0].end, 0};
      } else {  // Gen 1 is newer
        rs->start_chunk = (ChunkId){gen_span[0].start, 0};
        rs->end_chunk   = (ChunkId){gen_span[1].end, 1};
      }
    } else { // One generation
      if(gen_span[0].end >= 0) { // Gen 0
        rs->start_chunk = (ChunkId){gen_span[0].start, 0};
        rs->end_chunk   = (ChunkId){gen_span[0].end, 0};
      } else {  // Gen 1
        rs->start_chunk = (ChunkId){gen_span[1].start, 1};
        rs->end_chunk   = (ChunkId){gen_span[1].end, 1};
      }
    }

    //DPRINT("ROT init: start: %d_%d  end: %d_%d  size: %d", rs->start_chunk.chunk, rs->start_chunk.gen,
    //      rs->end_chunk.chunk, rs->end_chunk.gen, rs->base.total_size);

  } else {  // No chunks
    //DPRINT("ROT init: no chunks");
    rs->start_chunk = (ChunkId){0, 0};
    rs->end_chunk   = (ChunkId){0, 0};
  }

  return status;
}


// Method
static int open_rotate_container(Evfs *vfs, const char *path, RotateFile *fh, int flags) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  if(!is_rotate_file(base_vfs, path)) return EVFS_ERR_NO_FILE;

  // Allocate state to track file rotation
  size_t state_size = sizeof(RotateState) + strlen(path)+1;
  RotateState *rs = evfs_malloc(state_size);
  if(MEM_CHECK(rs)) return EVFS_ERR_ALLOC;

  memset(rs, 0, state_size);

  // Configure RotateState

  // Read dat settings

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  LOCK(&shim_data->buf);
  char *joined = shim_data->buf.tmp_path;
  StringRange joined_r;
  range_init(&joined_r, joined, COUNT_OF(shim_data->buf.tmp_path));
#else
  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
#endif
  evfs_vfs_path_join_str(base_vfs, path, MULTIPART_GEOMETRY_FILE, &joined_r);

  EvfsFile *dat_fh;
  int status = evfs_vfs_open(base_vfs, joined, &dat_fh, EVFS_READ);
  UNLOCK(&shim_data->buf);

  if(status != EVFS_OK)
    goto cleanup1;

  // Read header
  MultipartHeader hdr;

  status = evfs_file_read(dat_fh, &hdr, sizeof(hdr));
  if(status != sizeof(MultipartHeader)) {
    status = EVFS_ERR_INVALID;
    goto cleanup2;
  }

  // Confirm multipart type
  if(hdr.magic != EVFS_MULTI_MAGIC || hdr.type != EVFS_MULTI_ROTATE_TYPE) {
    status = EVFS_ERR_INVALID;
    goto cleanup2;
  }

  // Read geometry
  RotateGeometry geom;
  status = evfs_file_read(dat_fh, &geom, sizeof(geom));
  if(status != sizeof(RotateGeometry)) {
    status = EVFS_ERR_INVALID;
    goto cleanup2;
  }

  // Copy geometry fields
  rs->cfg.chunk_size = geom.chunk_size;
  rs->cfg.max_chunks = geom.max_chunks;


  // Successful read

  //DPRINT("ROT Chunk size: %d  Max chunks: %d\n", rs->cfg.chunk_size, rs->cfg.max_chunks);

  // Save path to container
  rs->base.container_path = (char *)NEXT_OBJ(rs);
  strcpy((char *)rs->base.container_path, path);

  rs->base.flags = flags;
  rs->base.chunk_size = geom.chunk_size;
  rs->base.file_pos = 0;
  rs->base.active_chunk.chunk = -1;
  rs->base.active_chunk.gen = 0;
  rs->base.active_chunk_fh = NULL;

#ifdef EVFS_USE_ROTATE_SHARED_BUFFER
  rs->base.buf = &shim_data->buf;
#endif

  status = discover_chunk_sequence(base_vfs, rs);

  fh->rot_state = rs;

cleanup2:
  evfs_file_close(dat_fh);

cleanup1:
  if(status != EVFS_OK)
    evfs_free(rs);

  return status;
}


static int set_rotate_config(Evfs *vfs, RotateConfig *cfg) {
  // Validate the config settings
  if(cfg->max_chunks < 2
    || cfg->max_chunks > MULTIPART_MAX_CHUNK
    || cfg->chunk_size < MULTIPART_MIN_CHUNK_SIZE)
    return EVFS_ERR_INVALID;

  RotateData *shim_data = (RotateData *)vfs->fs_data; 
  shim_data->cfg = *cfg;

  return EVFS_OK;
}


// Trim chunks from start of file
static int trim_start_chunks(RotateFile *fil, evfs_off_t trim_bytes) {
  RotateState *rs = fil->rot_state;
  RotateData *shim_data = fil->shim_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = EVFS_OK;
  // We need to round down to the nearest chunk size
  int trim_chunks = trim_bytes / rs->cfg.chunk_size;

  if(trim_chunks == 0) // No change
    return 0;

  ChunkId trim_start, trim_end;

  // Determine range of chunks to delete
  trim_start = rs->start_chunk;

  if(trim_chunks * (evfs_off_t)rs->cfg.chunk_size >= rs->base.total_size) { // Remove all chunks
    trim_end = rs->end_chunk;

  } else { // Partial trim
    trim_end = trim_start;
    trim_end.chunk += trim_chunks - 1;
    if(trim_end.chunk >= (int)rs->cfg.max_chunks) {
      trim_end.chunk -= rs->cfg.max_chunks;
      trim_end.gen = 1 - trim_end.gen;
    }
  }

  evfs_off_t chunk_size;
  evfs_off_t trimmed_size = 0;
  ChunkId cur_chunk = trim_start;

  // Delete chunks
  while(1) {
    status = evict_chunk(base_vfs, &rs->base, cur_chunk, &chunk_size);
    if(status != EVFS_OK)
      break;

    trimmed_size += chunk_size;

    if(cur_chunk.chunk == trim_end.chunk)
      break;

    incr_chunk(rs, &cur_chunk);
  }

  // Fix up state
  if(cur_chunk.chunk == rs->end_chunk.chunk) { // All chunks removed
    rs->base.total_size = 0; // Just to be sure this is correct
    rs->start_chunk = (ChunkId){0,0};
    rs->end_chunk = (ChunkId){0,0};
    rs->base.file_pos = 0;

  } else { // Partial trim
    incr_chunk(rs, &cur_chunk);
    rs->start_chunk = cur_chunk;

    if(rs->base.file_pos > trimmed_size)
      rs->base.file_pos -= trimmed_size;
    else
      rs->base.file_pos = 0;
  }


  if(status == EVFS_OK)
    status = trimmed_size;

  return status;
}


// ******************** File access methods ********************

static int rotate__file_ctrl(EvfsFile *fh, int cmd, void *arg) {
  RotateFile *fil = (RotateFile *)fh;
  //RotateData *shim_data = fil->shim_data;

  int status = EVFS_ERR_NO_SUPPORT;

  if(!fil->rot_state) { // This is a normal file
    status = fil->base_file->methods->m_ctrl(fil->base_file, cmd, arg);

  } else { // This is a rotate container
    switch(cmd) {
    case EVFS_CMD_SET_ROTATE_TRIM: // Trim chunks from start of file
      {
        evfs_off_t *v = (evfs_off_t *)arg;
        status = trim_start_chunks(fil, *v);
      }
      break;

    default:
      status = EVFS_ERR_NO_SUPPORT;
      break;
    }
  }

  return status;
}


static int rotate__file_close(EvfsFile *fh) {
  RotateFile *fil = (RotateFile *)fh;

  int status;

  if(!fil->rot_state) { // This is a normal file
    status = fil->base_file->methods->m_close(fil->base_file);

  } else { // This is a rotate container
    RotateState *rs = fil->rot_state;

    // Shutdown current active chunk
    if(rs->base.active_chunk_fh) {
      evfs_file_close(rs->base.active_chunk_fh);
      rs->base.active_chunk_fh = NULL;
      rs->base.active_chunk.chunk = -1;
    }

    evfs_free(rs);

    fil->rot_state = NULL;
    status = EVFS_OK;
  }


  if(status == EVFS_OK) {
    fil->base.methods = NULL; // Disable this instance
  }

  return status;
}


static ptrdiff_t rotate__file_read(EvfsFile *fh, void *buf, size_t size) {
  RotateFile *fil = (RotateFile *)fh;
  uint8_t *cbuf = (uint8_t *)buf;

  ptrdiff_t read = 0;
  if(!fil->rot_state) { // This is a normal file
    read = fil->base_file->methods->m_read(fil->base_file, cbuf, size);

  } else {
    RotateState *rs = fil->rot_state;
    RotateData *shim_data = fil->shim_data;
    Evfs *base_vfs = shim_data->base_vfs;

    int status = EVFS_OK;

    // We may have more than one chunk's worth of data to read
    while(size > 0) {
      ChunkPos rpos = get_chunk_pos(rs, rs->base.file_pos);

      //DPRINT("rptr: %ld, rpos: %d:%d,  active: %d", rs->base.file_pos, rpos.chunk_num, rpos.offset, rs->base.active_chunk);

      // If the chunk doesn't exist we are out of chunks to read
      evfs_off_t chunk_size;
      if(!chunk_exists(base_vfs, &rs->base, rpos.chunk_num, &chunk_size))
        break;

      if(rpos.chunk_num.chunk != rs->base.active_chunk.chunk) {
        status = activate_chunk(base_vfs, &rs->base, rpos.chunk_num);
        if(status != EVFS_OK) return status;
      }

      if(rpos.offset >= chunk_size) // Nothing left to read
        break;

      // Seek into this chunk
      evfs_file_seek(rs->base.active_chunk_fh, rpos.offset, EVFS_SEEK_TO);
      evfs_off_t remain_space = chunk_size - rpos.offset;
      size_t read_size = MIN((evfs_off_t)size, remain_space);

      ptrdiff_t read_chunk = evfs_file_read(rs->base.active_chunk_fh, cbuf, read_size);
      if(read_chunk > 0) {
        cbuf += read_chunk;
        size -= read_chunk;
        rs->base.file_pos += read_chunk;
        read += read_chunk;
      } else { // Chunk is prematurely empty
        return EVFS_ERR_IO;
      }

    }

    if(status != EVFS_OK)
      read = status;

  }

  return read;
}


static ptrdiff_t rotate__file_write(EvfsFile *fh, const void *buf, size_t size) {
  RotateFile *fil = (RotateFile *)fh;
  uint8_t *cbuf = (uint8_t *)buf;

  ptrdiff_t wrote = 0;
  if(!fil->rot_state) { // This is a normal file
    wrote = fil->base_file->methods->m_write(fil->base_file, cbuf, size);

  } else { // Write to rotate container
    RotateState *rs = fil->rot_state;
    RotateData *shim_data = fil->shim_data;
    Evfs *base_vfs = shim_data->base_vfs;

    EvfsFile *new_chunk;
    int status = EVFS_OK;

    //DPRINT("ROT: write  %d", size);

    // We may have more than one chunk's worth of data to write
    while(size > 0) {
      // Our write position depends on whether we're in append mode
      ChunkPos wpos = get_chunk_pos(rs, cur_write_pos(&rs->base));

      // If the chunk doesn't exist we need to create it
      if(!chunk_exists(base_vfs, &rs->base, wpos.chunk_num, NULL)) {
        status = append_new_chunk(base_vfs, rs, &new_chunk);
        if(status != EVFS_OK) return status;

        // Set new active chunk
        deactivate_chunk(&rs->base);
        rs->base.active_chunk = wpos.chunk_num;
        rs->base.active_chunk_fh = new_chunk;
      }

      if(wpos.chunk_num.chunk != rs->base.active_chunk.chunk) {
        status = activate_chunk(base_vfs, &rs->base, wpos.chunk_num);
        if(status != EVFS_OK) return status;
      }

      // Seek into this chunk
      evfs_file_seek(rs->base.active_chunk_fh, wpos.offset, EVFS_SEEK_TO);

      evfs_off_t free_space = rs->cfg.chunk_size - wpos.offset;
      if(ASSERT(free_space != 0, "No free space to write in chunk")) // This shouldn't happen
        return EVFS_ERR_CORRUPTION;

      size_t write_size = MIN((evfs_off_t)size, free_space);

      ptrdiff_t wrote_chunk = evfs_file_write(rs->base.active_chunk_fh, cbuf, write_size);
      if(wrote_chunk > 0) {
        cbuf += wrote_chunk;
        size -= wrote_chunk;
        incr_write_pos(&rs->base, wrote_chunk);
        wrote += wrote_chunk;
        rs->base.total_size += wrote_chunk;
      }

      // Check if we have more chunks to write
      if(size > 0) {
        status = append_new_chunk(base_vfs, rs, &new_chunk);
        if(status != EVFS_OK) return status;
        deactivate_chunk(&rs->base);
        rs->base.active_chunk = wpos.chunk_num;
        rs->base.active_chunk_fh = new_chunk;
      }
    }

    if(status != EVFS_OK)
      wrote = status;
  }

  return wrote;
}


static int rotate__file_truncate(EvfsFile *fh, evfs_off_t size) {
  RotateFile *fil = (RotateFile *)fh;

  int status = EVFS_OK;

  if(!fil->rot_state) { // This is a normal file
    status = fil->base_file->methods->m_truncate(fil->base_file, size);

  } else { // Rotate container
    RotateState *rs = fil->rot_state;
    RotateData *shim_data = fil->shim_data;
    Evfs *base_vfs = shim_data->base_vfs;

    if(size >= rs->base.total_size) // Do nothing if larger
      return EVFS_OK;

    // Delete all chunks after truncation point

    // The last chunk may be partially filled so it needs special handling
    evfs_off_t delete_bytes = rs->base.total_size - size;

    status = activate_chunk(base_vfs, &rs->base, rs->end_chunk);
    if(status != EVFS_OK) return EVFS_ERR_CORRUPTION;

    evfs_off_t chunk_size = evfs_file_size(rs->base.active_chunk_fh);
    int delete_chunks;

    if(delete_bytes >= chunk_size)
      delete_chunks = 1 + (delete_bytes - chunk_size) / rs->cfg.chunk_size;
    else
      delete_chunks = 0;

    // Remove whole chunks
    while(delete_chunks) {
      status = evict_chunk(base_vfs, &rs->base, rs->end_chunk, &chunk_size);
      if(status != EVFS_OK)
        break;

      rs->end_chunk.chunk--;
      if(rs->end_chunk.chunk < 0) {
        rs->end_chunk.chunk = rs->cfg.max_chunks - 1;
        rs->end_chunk.gen = 1 - rs->end_chunk.gen;
      }

      delete_bytes -= chunk_size;
      delete_chunks--;
    }

    // Remove any leftover remainder from new end chunk
    if(delete_bytes > 0 && status == EVFS_OK) {
      // Truncate new end chunk
      status = activate_chunk(base_vfs, &rs->base, rs->end_chunk);
      if(status != EVFS_OK) return EVFS_ERR_CORRUPTION;

      chunk_size = evfs_file_size(rs->base.active_chunk_fh);

      // We should already have handled full chunks above and only
      // have a portion less than chunk_size left to truncate.
      if(ASSERT(delete_bytes < chunk_size, "Truncation error; Excess remainder"))
        return EVFS_ERR_CORRUPTION;

      evfs_file_truncate(rs->base.active_chunk_fh, chunk_size - delete_bytes);
      rs->base.total_size -= delete_bytes;
    }

    if(rs->base.total_size == 0) { // Restart chunk sequence
      rs->start_chunk = (ChunkId){0,0};
      rs->end_chunk = (ChunkId){0,0};
    }

    // Adjust position
    if(rs->base.file_pos > size)
      rs->base.file_pos = size;

    if(ASSERT(size == rs->base.total_size, "failed truncation: size=%ld  total=%ld", size, rs->base.total_size))
      status = EVFS_ERR_CORRUPTION;

  }

  return status;
}


static int rotate__file_sync(EvfsFile *fh) {
  RotateFile *fil = (RotateFile *)fh;

  int status = EVFS_OK;

  if(!fil->rot_state) { // This is a normal file
    status = fil->base_file->methods->m_sync(fil->base_file);
  } else { // Rotate container
    RotateState *rs = fil->rot_state;

    if(rs->base.active_chunk_fh)
      status = evfs_file_sync(rs->base.active_chunk_fh);

  }

  return status;
}


static evfs_off_t rotate__file_size(EvfsFile *fh) {
  RotateFile *fil = (RotateFile *)fh;
  evfs_off_t size;

  if(!fil->rot_state) { // This is a normal file
    size = fil->base_file->methods->m_size(fil->base_file);
  } else { // Rotate container
    RotateState *rs = fil->rot_state;

    size = rs->base.total_size;
  }

  return size;
}


static int rotate__file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin) {
  RotateFile *fil = (RotateFile *)fh;
  
  int status = EVFS_OK;

  if(!fil->rot_state) { // This is a normal file
    status = fil->base_file->methods->m_seek(fil->base_file, offset, origin);

  } else { // Rotate container
    RotateState *rs = fil->rot_state;
    evfs_off_t new_off = evfs__absolute_offset(fh, offset, origin);

    // Only allow seeks up to the end of the file
    if(new_off <= rs->base.total_size)
      rs->base.file_pos = new_off;
    else
      status = EVFS_ERR_OVERFLOW;

  }

  return status;
}


static evfs_off_t rotate__file_tell(EvfsFile *fh) {
  RotateFile *fil = (RotateFile *)fh;

  evfs_off_t pos;

  if(!fil->rot_state) { // This is a normal file
    pos = fil->base_file->methods->m_tell(fil->base_file);

  } else { // Rotate container
    RotateState *rs = fil->rot_state;
    pos = rs->base.file_pos;
  }

  return pos;
}


static bool rotate__file_eof(EvfsFile *fh) {
  RotateFile *fil = (RotateFile *)fh;

  bool eof;

  if(!fil->rot_state) { // This is a normal file
    eof = fil->base_file->methods->m_eof(fil->base_file);

  } else { // Rotate container
    RotateState *rs = fil->rot_state;

    eof = rs->base.file_pos >= rs->base.total_size;
  }

  return eof;
}


static const EvfsFileMethods s_rotate_methods = {
  .m_ctrl     = rotate__file_ctrl,
  .m_close    = rotate__file_close,
  .m_read     = rotate__file_read,
  .m_write    = rotate__file_write,
  .m_truncate = rotate__file_truncate,
  .m_sync     = rotate__file_sync,
  .m_size     = rotate__file_size,
  .m_seek     = rotate__file_seek,
  .m_tell     = rotate__file_tell,
  .m_eof      = rotate__file_eof
};


// ******************** Directory access methods ********************

static int rotate__dir_close(EvfsDir *dh) {
  RotateDir *dir = (RotateDir *)dh;

  int status = dir->base_dir->methods->m_close(dir->base_dir);

  if(status == EVFS_OK) {
    dir->base.methods = NULL; // Disable this instance
  }

  return status;
}

static int rotate__dir_read(EvfsDir *dh, EvfsInfo *info) {
  RotateDir *dir = (RotateDir *)dh;

  int status = dir->base_dir->methods->m_read(dir->base_dir, info);

  return status;
}

static int rotate__dir_rewind(EvfsDir *dh) {
  RotateDir *dir = (RotateDir *)dh;

  int status = dir->base_dir->methods->m_rewind(dir->base_dir);

  return status;
}

static const EvfsDirMethods s_rotate_dir_methods = {
  .m_close    = rotate__dir_close,
  .m_read     = rotate__dir_read,
  .m_rewind   = rotate__dir_rewind
};


// ******************** FS access methods ********************


static int rotate__open(Evfs *vfs, const char *path, EvfsFile *fh, int flags) {
  RotateFile *fil = (RotateFile *)fh;
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  fil->shim_data = shim_data;

  fil->base_file = (EvfsFile *)NEXT_OBJ(fil);  // We have two objects allocated together [RotateFile][<base VFS file size>]

  int status;

  // Check if the path exists
  EvfsInfo info;
  bool exists = base_vfs->m_stat(base_vfs, path, &info) == EVFS_OK;

  // Check if this is an existing regular file
  if(exists && !is_rotate_file(base_vfs, path)) {
    status = base_vfs->m_open(base_vfs, path, fil->base_file, flags);
  } else {
    // Open or create a container
    status = EVFS_OK;
    if(!exists) // Create new container
      status = init_rotate_container(base_vfs, path, &shim_data->cfg);

    if(status == EVFS_OK)
      status = open_rotate_container(vfs, path, fil, flags);

  }

  if(status == EVFS_OK) {
    // Add methods to make this functional
    fh->methods = &s_rotate_methods;
  } else { // Open failed
    fh->methods = NULL;
  }

  return status;
}



static int rotate__stat(Evfs *vfs, const char *path, EvfsInfo *info) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = base_vfs->m_stat(base_vfs, path, info);

  return status;
}



static int rotate__delete(Evfs *vfs, const char *path) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status;

  if(!is_rotate_file(base_vfs, path)) { // This is a normal file
    status = base_vfs->m_delete(base_vfs, path);

  } else { // This is a rotate container
    clear_container(base_vfs, path);
    status = base_vfs->m_delete(base_vfs, path);
  }

  return status;
}


static int rotate__rename(Evfs *vfs, const char *old_path, const char *new_path) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = base_vfs->m_rename(base_vfs, old_path, new_path);

  return status;
}


static int rotate__make_dir(Evfs *vfs, const char *path) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = base_vfs->m_make_dir(base_vfs, path);

  return status;
}


static int rotate__open_dir(Evfs *vfs, const char *path, EvfsDir *dh) {
  RotateDir *dir = (RotateDir *)dh;
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  dir->shim_data = shim_data;
  dir->base_dir = (EvfsDir *)NEXT_OBJ(dir);   // We have two objects allocated together [RotateDir][<base VFS dir size>]


  // Prevent opening container directories
  if(is_rotate_file(base_vfs, path)) {
    dh->methods = NULL;
    return EVFS_ERR_NO_PATH;
  }

  int status = base_vfs->m_open_dir(base_vfs, path, dir->base_dir);

  if(status == EVFS_OK) {
    // Construct a shimmed dir object
    if(dir->base_dir->methods) {
      dh->methods = &s_rotate_dir_methods;
    } else {
      dh->methods = NULL;
      status = EVFS_ERR_INIT;
    }
    
  } else { // Open failed
    dh->methods = NULL;
  }

  return status;
}




static int rotate__get_cur_dir(Evfs *vfs, StringRange *cur_dir) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = base_vfs->m_get_cur_dir(base_vfs, cur_dir);

  return status;
}


static int rotate__set_cur_dir(Evfs *vfs, const char *path) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  int status = base_vfs->m_set_cur_dir(base_vfs, path);

  return status;
}



static int rotate__vfs_ctrl(Evfs *vfs, int cmd, void *arg) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  // We need special handling for EVFS_CMD_UNREGISTER.
  // It can't pass through since we need to deallocate VFSs in the proper sequence
  // to avoid corrupting the registered VFS linked list.
  if(cmd == EVFS_CMD_UNREGISTER) {
#ifdef USE_ROT_LOCK
      evfs__lock_destroy(&shim_data->buf.buf_lock);
#endif
      evfs_free(vfs); // Free this trace VFS
      return EVFS_OK;
  }

  int status = EVFS_ERR_NO_SUPPORT;

  switch(cmd) {
  case EVFS_CMD_SET_ROTATE_CFG:
    {
      RotateConfig *v = (RotateConfig *)arg;
      status = set_rotate_config(vfs, v);
    }
    break;

  default: // Everything else passes to the underlying VFS
    status = base_vfs->m_vfs_ctrl(base_vfs, cmd, arg);
    break;
  }

  return status;
}


static bool rotate__path_root_component(Evfs *vfs, const char *path, StringRange *root) {
  RotateData *shim_data = (RotateData *)vfs->fs_data;
  Evfs *base_vfs = shim_data->base_vfs;

  bool is_absolute = base_vfs->m_path_root_component(base_vfs, path, root);

  return is_absolute;
}


/*
Register a rotate filesystem shim

Args:
  vfs_name:      Name of new shim
  old_vfs_name:  Existing VFS to wrap with shim
  cfg:           Configuration settings for new containers
  default_vfs:   Make this the default VFS when true

Returns:
  EVFS_OK on success
*/
int evfs_register_rotate(const char *vfs_name, const char *old_vfs_name, RotateConfig *cfg, bool default_vfs) {
  if(PTR_CHECK(vfs_name) || PTR_CHECK(old_vfs_name)) return EVFS_ERR_BAD_ARG;

  Evfs *base_vfs, *shim_vfs;
  RotateData *shim_data;

  base_vfs = evfs_find_vfs(old_vfs_name);
  if(PTR_CHECK(base_vfs)) return EVFS_ERR_NO_VFS;


  // Construct a new VFS
  // We have three objects allocated together [Evfs][RotateData][char[]]
  size_t shim_size = sizeof(*shim_vfs) + sizeof(*shim_data) + strlen(vfs_name)+1;
  shim_vfs = evfs_malloc(shim_size);
  if(MEM_CHECK(shim_vfs)) return EVFS_ERR_ALLOC;

  memset(shim_vfs, 0, shim_size);

  shim_data = (RotateData *)NEXT_OBJ(shim_vfs);

  shim_vfs->vfs_name = (char *)NEXT_OBJ(shim_data);
  strcpy((char *)shim_vfs->vfs_name, vfs_name);

  shim_data->base_vfs = base_vfs;
  shim_data->vfs_name = shim_vfs->vfs_name;
  shim_data->shim_vfs = shim_vfs;

  shim_vfs->vfs_file_size = sizeof(RotateFile) + base_vfs->vfs_file_size;
  shim_vfs->vfs_dir_size = sizeof(RotateDir) + base_vfs->vfs_dir_size;
  shim_vfs->fs_data = shim_data;

  shim_vfs->m_open = rotate__open;
  shim_vfs->m_stat = rotate__stat;
  shim_vfs->m_delete = rotate__delete;
  shim_vfs->m_rename = rotate__rename;
  shim_vfs->m_make_dir = rotate__make_dir;
  shim_vfs->m_open_dir = rotate__open_dir;
  shim_vfs->m_get_cur_dir = rotate__get_cur_dir;
  shim_vfs->m_set_cur_dir = rotate__set_cur_dir;
  shim_vfs->m_vfs_ctrl = rotate__vfs_ctrl;

  shim_vfs->m_path_root_component = rotate__path_root_component;


  // Validate the config settings
  // We have to do this after the VFS is filled in
  int status = set_rotate_config(shim_vfs, cfg);
  if(status != EVFS_OK) {
    evfs_free(shim_vfs);
    return status;
  }

#ifdef USE_ROT_LOCK
  if(evfs__lock_init(&shim_data->buf.buf_lock) != EVFS_OK) {
    evfs_free(shim_vfs);
    THROW(EVFS_ERR_INIT);
  }
#endif


  return evfs_register(shim_vfs, default_vfs);
}

