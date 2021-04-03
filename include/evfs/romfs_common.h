
#ifndef ROMFS_COMMON_H
#define ROMFS_COMMON_H



#define ROMFS_MAX_HEADER_SIZE ((16 + EVFS_ROMFS_MAX_NAME_LEN + 15) & ~0xF)
#define ROMFS_MIN_HEADER_SIZE (16 + 2)


#define FILE_MODE_MASK  0x0F
#define FILE_TYPE_MASK  0x07
#define FILE_EX_MASK    0x08

#define FILE_OFFSET(hdr) ((hdr)->offset & ~0xF)
#define FILE_TYPE(hdr) ((hdr)->offset & FILE_TYPE_MASK)
#define FILE_MODE(hdr) ((hdr)->offset & FILE_MODE_MASK)

#define FILE_TYPE_HARD_LINK     0
#define FILE_TYPE_DIRECTORY     1
#define FILE_TYPE_REGULAR_FILE  2
#define FILE_TYPE_SYM_LINK      3
#define FILE_TYPE_BLOCK_DEV     4
#define FILE_TYPE_CHAR_DEV      5
#define FILE_TYPE_SOCKET        6
#define FILE_TYPE_FIFO          7


// Header for file entries
typedef struct RomfsFileHead {
  uint32_t offset; // next file header in binary format
  uint32_t spec_info;
  uint32_t size;
  uint32_t header_len; // This is the checksum in the romfs binary format
  char file_name[EVFS_ROMFS_MAX_NAME_LEN];
} RomfsFileHead;



#ifdef EVFS_USE_ROMFS_FAST_INDEX
typedef struct RomfsIndex {
  dhash hash_table; // Manages index of hashed key/value pairs

  // Storage for file path keys
  char *keys;
} RomfsIndex;
#endif

struct Romfs;

typedef ptrdiff_t (*ReadMethod)(struct Romfs *fs, evfs_off_t offset, void *buf, size_t size);
typedef void (*UnmountMethod)(struct Romfs *fs);

// Dynamic callback for performing file lookups
// This lets us switch to a hash table lookup after an index is built.
typedef int (*LookupMethod)(struct Romfs *fs, const char *path, RomfsFileHead *hdr);


typedef struct RomfsConfig {
  void *ctx;
  evfs_off_t  total_size;

  ReadMethod    read_data;
  UnmountMethod unmount;
} RomfsConfig;


typedef struct Romfs {
  void       *ctx;
  evfs_off_t  total_size;
  evfs_off_t  root_dir;

#ifdef EVFS_USE_ROMFS_FAST_INDEX
  RomfsIndex  fast_index;
#endif

  ReadMethod      read_data;
  UnmountMethod   unmount;
  LookupMethod    lookup_abs_path;
} Romfs;



int romfs_init(Romfs *fs, RomfsConfig *cfg);


bool romfs_read_file_header(Romfs *fs, long hdr_pos, RomfsFileHead *hdr);


static inline ptrdiff_t romfs_read(Romfs *fs, evfs_off_t offset, void *buf, size_t size) {
  return fs->read_data(fs, offset, buf, size);
}

static inline int romfs_lookup_abs_path(Romfs *fs, const char *path, RomfsFileHead *hdr) {
  return fs->lookup_abs_path(fs, path, hdr);
}

void romfs_unmount(Romfs *fs);


#endif // ROMFS_COMMON_H

