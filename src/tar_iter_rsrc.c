
/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  TAR resource iterator
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "evfs.h"
#include "evfs/tar_iter_rsrc.h"



void tar_rsrc_iter_init(TarRsrcIterator *tar_it, uint8_t *resource, size_t resource_len) {
  memset(tar_it, 0, sizeof(TarRsrcIterator));
  tar_it->resource = resource;
  tar_it->read_pos = resource;
  tar_it->resource_len = resource_len;
}


static bool tar__valid_header(TarHeader *header) {
  // Only support ustar format
  if(strcmp((const char *)header->magic, "ustar  ") != 0) return false;
  
  // Compute the checksum
  uint8_t *raw_header = (uint8_t *)header;
  uint32_t checksum = 0;
  for(size_t i = 0; i < offsetof(TarHeader, checksum); i++) {
    checksum += raw_header[i];
  }
  // Replace checksum with spaces
  for(size_t i = offsetof(TarHeader, checksum); i < offsetof(TarHeader, type_flag); i++) {
    checksum += ' ';
  }
  for(size_t i = offsetof(TarHeader, type_flag); i < TAR_HEADER_SIZE; i++) {
    checksum += raw_header[i];
  }

  // Validate  
  // Only supporting NUL terminated octal with '0' padding
  uint32_t orig_checksum_value = strtol((const char *)&header->checksum, NULL, 8);

  return orig_checksum_value == checksum;
}


static bool tar__rsrc_get_header(TarRsrcIterator *tar_it) {
  tar_it->header_offset = tar_it->read_pos - tar_it->resource;

  tar_it->cur_header = (TarHeader *)tar_it->read_pos;
  tar_it->read_pos += TAR_HEADER_SIZE;
  if(tar__valid_header(tar_it->cur_header)) {
    // Seek to start of next 512 byte TAR block for file data
    tar_it->read_pos += TAR_BLOCK_SIZE - TAR_HEADER_SIZE;

    // Extract fields
    // Only supporting NUL terminated octal with '0' padding
    tar_it->file_size = strtol((const char *)tar_it->cur_header->size, NULL, 8);
    return true;
  }

  tar_it->cur_header = NULL;
  tar_it->header_offset = 0;
  tar_it->file_size = 0;

  return false;
}


bool tar_rsrc_iter_seek(TarRsrcIterator *tar_it, evfs_off_t offset) {
  tar_it->read_pos = tar_it->resource + offset;

  return tar__rsrc_get_header(tar_it);
}


bool tar_rsrc_iter_next(TarRsrcIterator *tar_it) {
  // Position at start of next header
  size_t file_blocks = (tar_it->file_size + TAR_BLOCK_SIZE-1) / TAR_BLOCK_SIZE;
  size_t next_header = tar_it->header_offset + (file_blocks+1) * TAR_BLOCK_SIZE;

  tar_it->read_pos = tar_it->resource + next_header;
  if((size_t)(tar_it->read_pos - tar_it->resource) >= tar_it->resource_len)
    return false;

  return tar__rsrc_get_header(tar_it);
}


size_t tar_rsrc_iter_file_offset(TarRsrcIterator *tar_it) {
  if(!tar_it) return 0;
  return tar_it->header_offset + TAR_BLOCK_SIZE;
}




