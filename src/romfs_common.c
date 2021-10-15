
#include <stdio.h>
#include <string.h>
#include <stdalign.h>

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/util/unaligned_access.h"

#ifdef EVFS_USE_ROMFS_FAST_INDEX
#  include "evfs/util/dhash.h"
#endif

#include "evfs/romfs_common.h"

//#include "../test/hex_dump.h"

// ******************** Romfs core ********************

static inline int file_header_len(RomfsFileHead *hdr) {
  // Header = 16 bytes + file_name
  // Round up to 16-byte boundary
  return (16 + strnlen(hdr->file_name, EVFS_ROMFS_MAX_NAME_LEN-1)+1 + 15) & ~0xF;
}


// Read Romfs file header from image
bool romfs_read_file_header(Romfs *fs, long hdr_pos, RomfsFileHead *hdr) {
  memset(hdr, 0, sizeof(*hdr));

  ptrdiff_t buf_len = romfs_read(fs, hdr_pos, hdr, sizeof(*hdr));
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
static bool romfs__find_path_elem(Romfs *fs, evfs_off_t dir_pos, StringRange *element, RomfsFileHead *hdr) {
  evfs_off_t cur_hdr = dir_pos;

  while(cur_hdr != 0) {
    if(!romfs_read_file_header(fs, cur_hdr, hdr))
      break;

    if(range_eq(element, hdr->file_name)) {
      hdr->offset = cur_hdr | FILE_MODE(hdr); // Replace with offset of the element
      return true;
    }

    cur_hdr = FILE_OFFSET(hdr);
  }

  return false;
}

#ifdef EVFS_USE_ROMFS_FAST_INDEX
// Fast path lookups using a hash table
static int romfs__fast_lookup_abs_path(Romfs *fs, const char *path, RomfsFileHead *hdr) {
  int status;

  // Lookup path in fast index
  dhKey key;
  key.data = &path[1]; // Skip leading '/'
  key.length = strlen(key.data);

  alignas(uintptr_t) evfs_off_t entry;
  if(dh_lookup(&fs->fast_index.hash_table, key, &entry)) {
    romfs_read_file_header(fs, entry, hdr);
    //DPRINT("## FAST LOOKUP: @ %08X %08X %s -> '%s'", entry, FILE_OFFSET(hdr), path, hdr->file_name);
    hdr->offset = entry | FILE_MODE(hdr); // Replace with offset of the element
    status = EVFS_OK;
  } else {
    status = EVFS_ERR_NO_PATH;
  }

  return status;
}
#endif



// Simplfied version of evfs_vfs_scan_path() with no root element handling
static int romfs__scan_path(const char *path, StringRange *element) {
  bool new_tok;

  if(path) {
    // Get first path element
    new_tok = range_token(path, EVFS_PATH_SEPS, element);

  } else {
    new_tok = range_token(NULL, EVFS_PATH_SEPS, element);
  }

  return new_tok ? EVFS_OK : EVFS_ERR;
}



// Lookup paths by walking the directory tree
static int romfs__lookup_abs_path(Romfs *fs, const char *path, RomfsFileHead *hdr) {
  evfs_off_t dir_pos = fs->root_dir;

  StringRange element;

  int status = romfs__scan_path(path, &element); // Get first element in path
  bool end_scan;

  if(status != EVFS_OK) { // Assume this is the root path
    return romfs_read_file_header(fs, dir_pos, hdr) ? EVFS_OK : EVFS_ERR_NO_PATH;
  }

  while(status == EVFS_OK) {
    int file_type = -1;
    end_scan = false;

    if(romfs__find_path_elem(fs, dir_pos, &element, hdr)) {
      file_type = FILE_TYPE(hdr);
      switch(file_type) {

      case FILE_TYPE_HARD_LINK:
        dir_pos = hdr->spec_info;  // Follow link dest
        romfs_read_file_header(fs, dir_pos, hdr); // Reparse header (should never fail)
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

    status = romfs__scan_path(NULL, &element); // Get next element in path

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


static int romfs__validate(Romfs *fs) {
  evfs_off_t chunk_pos = 0;

  // Superblock covers first 512 bytes.
  // We will read it in chunks to reduce stack usage.
#define SUPERBLOCK_LEN  512
#define CHUNK_LEN       64
  uint32_t buf[CHUNK_LEN/4];

  ptrdiff_t buf_len = romfs_read(fs, chunk_pos, buf, 4*COUNT_OF(buf));
  chunk_pos += CHUNK_LEN;
  if(ASSERT(buf_len >= ROMFS_MIN_HEADER_SIZE, "Romfs too small"))
    return EVFS_ERR_INVALID;

  // Force magic number string to little-endian
  if(get_unaligned_le(&buf[0]) == 0x6D6F722D && get_unaligned_le(&buf[1]) == 0x2D736631) {
    uint32_t fs_bytes = get_unaligned_be(&buf[2]);

    if(ASSERT((evfs_off_t)fs_bytes <= fs->total_size, "Invalid Romfs size"))
      return EVFS_ERR_INVALID;

    // Iterate over chunks to verify checksum
    int32_t checksum = 0;

    for(int c = 0; c < SUPERBLOCK_LEN / CHUNK_LEN; c++) {
      buf_len /= 4;
      for(int i = 0; i < buf_len; i++) {
        checksum += get_unaligned_be(&buf[i]);
      }

      buf_len = romfs_read(fs, chunk_pos, buf, 4*COUNT_OF(buf));
      chunk_pos += CHUNK_LEN;
      if(buf_len <= 0)
        break;
    }

    if(checksum == 0) { // Valid superblock
      // Read first chunk again
      chunk_pos = 0;
      buf_len = romfs_read(fs, chunk_pos, buf, 4*COUNT_OF(buf));

      char *vol_name = (char *)&buf[4];
      //DPRINT("VOLUME: '%s'", vol_name);

      // Root dir starts at first file header
      fs->root_dir = (16 + strnlen(vol_name, EVFS_ROMFS_MAX_NAME_LEN-1)+1 + 15) & ~0xF;
      return EVFS_OK;
    }
  }

  return EVFS_ERR_INVALID;
}


// ******************** Fast hashed index ********************

#ifdef EVFS_USE_ROMFS_FAST_INDEX

// Dhash callbacks
static void destroy_hashed_file(dhKey key, void *value, void *ctx) {
}


static int romfs__fast_index_init(RomfsIndex *ht, int total_files, size_t total_path_len) {
  dhConfig s_hash_init = {
    .init_buckets = total_files,
    .value_size   = sizeof(evfs_off_t),

    .destroy_item = destroy_hashed_file,
    .gen_hash     = dh_gen_hash_string,
    .is_equal     = dh_equal_hash_keys_string
  };

  ht->keys = evfs_malloc(total_path_len);
  if(MEM_CHECK(ht->keys)) return EVFS_ERR_ALLOC;


  int err = dh_init(&ht->hash_table, &s_hash_init, NULL) ? EVFS_OK : EVFS_ERR;
  return err;
}


static void romfs__fast_index_free(RomfsIndex *ht) {
  dh_free(&ht->hash_table);
  evfs_free(ht->keys);
  ht->keys = NULL;
}



static int romfs__get_next_file(Romfs *fs, RomfsFileHead *cur_file, evfs_off_t *cur_file_offset) {
  *cur_file_offset = FILE_OFFSET(cur_file);
  if(*cur_file_offset == 0)
    return EVFS_DONE;

  return romfs_read_file_header(fs, *cur_file_offset, cur_file) ? EVFS_OK : EVFS_ERR;
}


static int romfs__get_dir(Romfs *fs, const char *path, RomfsFileHead *hdr,
                                evfs_off_t *cur_file_offset, evfs_off_t *dir_pos) {
  int status = fs->lookup_abs_path(fs, path, hdr);

  if(dir_pos)
    *dir_pos = FILE_OFFSET(hdr); // Offset to the dir entry


  if(status == EVFS_OK && FILE_TYPE(hdr) != FILE_TYPE_DIRECTORY) {
    status = EVFS_ERR_NO_PATH;
  }

  if(status == EVFS_OK) {
    // Get first file in directory
    romfs_read_file_header(fs, hdr->spec_info, hdr) ? EVFS_OK : EVFS_ERR;

    // Skip hard link entries
    romfs__get_next_file(fs, hdr, cur_file_offset);

    status = romfs__get_next_file(fs, hdr, cur_file_offset);
  }

  return status;
}




// Recursively scan the filesystem and collect stats on all paths
static size_t scan_dir_tree(Romfs *fs, const char *path, size_t prefix_len, int *total_files) {
  RomfsFileHead cur_file;
  evfs_off_t    cur_file_offset;
  int status;

  size_t total_path_len = 0;

  status = romfs__get_dir(fs, path, &cur_file, &cur_file_offset, NULL);
  //DPRINT("OPEN DIR: %s %d", path, status);

  do {
    if(status == EVFS_OK) {
      //DPRINT("  '%s'  %08X  %08X", cur_file.file_name, cur_file_offset, FILE_OFFSET(&cur_file));
      (*total_files)++;

      if(FILE_TYPE(&cur_file) == FILE_TYPE_DIRECTORY) {
        // Build path to sub directory
        size_t new_prefix_len = prefix_len + 1 + strlen(cur_file.file_name);
        total_path_len += new_prefix_len; // Entry for the directory

        char *sub_path = malloc(new_prefix_len+1);
        if(MEM_CHECK(sub_path)) return 0;

        AppendRange sub_path_r;
        range_init(&sub_path_r, sub_path, new_prefix_len+1);
        range_cat_str(&sub_path_r, path);
        range_cat_char(&sub_path_r, '/');
        range_cat_str(&sub_path_r, cur_file.file_name);

        //DPRINT("SUB PATH: '%s'  %08X", sub_path, cur_file_offset);
        total_path_len += scan_dir_tree(fs, sub_path, new_prefix_len, total_files);
        free(sub_path);

      } else { // Normal file
        total_path_len += prefix_len + 1 + strlen(cur_file.file_name);
      }

      status = romfs__get_next_file(fs, &cur_file, &cur_file_offset);
    }
  } while(status == EVFS_OK);

  return total_path_len;
}


// Recursively scan the filesystem to add hash table indices
static int index_dir_tree(Romfs *fs, const char *path, size_t prefix_len, RomfsIndex *ht, AppendRange *keys_r) {
  RomfsFileHead cur_file;
  evfs_off_t    cur_file_offset = 0;
  int status;
  dhKey key;
  alignas(uintptr_t) evfs_off_t entry;

  status = romfs__get_dir(fs, path, &cur_file, &cur_file_offset, &entry);
  //DPRINT("INDEX, OPEN DIR: %s %d", path, status);

  // Prepare key
  key.data = keys_r->start;
  key.length = 0;

  if(prefix_len > 0)
    key.length += range_cat_str(keys_r, &path[1]); // Skip leading '/'
  else
    key.length += range_cat_str(keys_r, "");
  range_cat_char(keys_r,'\0');

  if(!dh_insert(&ht->hash_table, key, &entry))
    return EVFS_ERR;

  //DPRINT("## INDEX DIR: '%s' @ %08X", key.data, entry);

  do {
    if(status == EVFS_OK) {
      //DPRINT("  '%s'", cur_file.file_name);

      if(FILE_TYPE(&cur_file) == FILE_TYPE_DIRECTORY) {
        // Build path to sub directory
        size_t new_prefix_len = prefix_len + 1 + strlen(cur_file.file_name);

        char *sub_path = malloc(new_prefix_len+1);
        if(MEM_CHECK(sub_path)) return EVFS_ERR_ALLOC;

        AppendRange sub_path_r;
        range_init(&sub_path_r, sub_path, new_prefix_len+1);
        range_cat_str(&sub_path_r, path);
        range_cat_char(&sub_path_r, '/');
        range_cat_str(&sub_path_r, cur_file.file_name);

        //DPRINT("SUB PATH: '%s'", sub_path);

        index_dir_tree(fs, sub_path, new_prefix_len, ht, keys_r);
        free(sub_path);

      } else { // Normal file
        // Prepare key
        key.data = keys_r->start;
        key.length = 0;

        if(prefix_len > 0) {
          key.length += range_cat_str(keys_r, &path[1]); // Skip leading '/'
          key.length += range_cat_char(keys_r,'/');
        }
        key.length += range_cat_str(keys_r, cur_file.file_name);
        range_cat_char(keys_r,'\0');

        entry = cur_file_offset;
        if(!dh_insert(&ht->hash_table, key, &entry))
          return EVFS_ERR;

        //DPRINT("## INDEX FIL: %s @ %08X", key.data, entry);
      }

      status = romfs__get_next_file(fs, &cur_file, &cur_file_offset);
    }
  } while(status == EVFS_OK);

  return EVFS_OK;
}


static int romfs__build_index(Romfs *fs, RomfsIndex *ht) {
  // Scan all files to get total storage for path keys
  int status;

  size_t total_path_len = 0;
  int total_files = 0;

  total_path_len = scan_dir_tree(fs, "", 0, &total_files);
  // Add space for root entry
  total_files++;
  total_path_len += 1;

  // Prepare the hash index
  status = romfs__fast_index_init(ht, total_files, total_path_len);
  if(status == EVFS_OK) {
    AppendRange keys_r;
    range_init(&keys_r, ht->keys, total_path_len);

    status = index_dir_tree(fs, "", 0, ht, &keys_r);
    //dump_array((uint8_t *)ht->keys, total_path_len);

    fs->lookup_abs_path = romfs__fast_lookup_abs_path;
  }

  return status;
}

#endif // EVFS_USE_ROMFS_FAST_INDEX



int romfs_init(Romfs *fs, RomfsConfig *cfg) {
  fs->ctx         = cfg->ctx;
  fs->total_size  = cfg->total_size;
  fs->read_data   = cfg->read_data;
  fs->unmount     = cfg->unmount;

  fs->lookup_abs_path = romfs__lookup_abs_path;

  int status = romfs__validate(fs);

#ifdef EVFS_USE_ROMFS_FAST_INDEX
  if(status == EVFS_OK) {
    romfs__build_index(fs, &fs->fast_index);
  }
#endif

  return status;
}


void romfs_unmount(Romfs *fs) {
  fs->unmount(fs);

#ifdef EVFS_USE_ROMFS_FAST_INDEX
  romfs__fast_index_free(&fs->fast_index);
#endif

}

