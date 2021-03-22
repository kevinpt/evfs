
/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  TAR file iterator
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
#include "evfs/tar_iter.h"



void tar_iter_init(TarFileIterator *tar_it, EvfsFile *fd) {
  memset(tar_it, 0, sizeof(TarFileIterator));
  tar_it->fd = fd;
}


void tar_iter_close(TarFileIterator *tar_it) {
  evfs_file_close(tar_it->fd);
  tar_it->fd = NULL;
}


static bool tar__valid_header(TarHeader *header) {
  // Only support ustar format
  if(strcmp((const char *)header->magic, "ustar  ") != 0) return false;
  
  // Compute the checksum
  uint8_t *raw_header = (uint8_t *)header;
  uint32_t checksum = 0;
  for(int i = 0; i < offsetof(TarHeader, checksum); i++) {
    checksum += raw_header[i];
  }
  // Replace checksum with spaces
  for(int i = offsetof(TarHeader, checksum); i < offsetof(TarHeader, type_flag); i++) {
    checksum += ' ';
  }
  for(int i = offsetof(TarHeader, type_flag); i < TAR_HEADER_SIZE; i++) {
    checksum += raw_header[i];
  }

  // Validate  
  // Only supporting NUL terminated octal with '0' padding
  uint32_t orig_checksum_value = strtol((const char *)&header->checksum, NULL, 8);

  return orig_checksum_value == checksum;
}


static bool tar__get_header(TarFileIterator *tar_it, TarHeader *header) {
  tar_it->header_offset = evfs_file_tell(tar_it->fd);

  if(evfs_file_read(tar_it->fd, header, TAR_HEADER_SIZE) != TAR_HEADER_SIZE)
    goto fail;

  if(tar__valid_header(header)) {
    // Seek to start of next 512 byte TAR block for file data
    if(evfs_file_seek(tar_it->fd, TAR_BLOCK_SIZE - TAR_HEADER_SIZE, EVFS_SEEK_REL) != EVFS_OK)
      goto fail;

    // Extract fields
    // Only supporting NUL terminated octal with '0' padding
    tar_it->file_size = strtol((const char *)header->size, NULL, 8);
    return true;
  }

fail:
  tar_it->header_offset = 0;
  tar_it->file_size = 0;

  return false;
}


bool tar_iter_seek(TarFileIterator *tar_it, evfs_off_t offset) {
  if(evfs_file_seek(tar_it->fd, offset, EVFS_SEEK_TO) != EVFS_OK)
    return false;

  return tar__get_header(tar_it, &tar_it->cur_header);
}


bool tar_iter_next(TarFileIterator *tar_it) {
  // Position at start of next header
  evfs_off_t file_blocks = (tar_it->file_size + TAR_BLOCK_SIZE-1) / TAR_BLOCK_SIZE;
  evfs_off_t next_header = tar_it->header_offset + (file_blocks+1) * TAR_BLOCK_SIZE;

  if(evfs_file_seek(tar_it->fd, next_header, EVFS_SEEK_TO) != EVFS_OK)
    return false;

  return tar__get_header(tar_it, &tar_it->cur_header);
}


evfs_off_t tar_iter_file_offset(TarFileIterator *tar_it) {
  if(!tar_it) return 0;
  return tar_it->header_offset + TAR_BLOCK_SIZE;
}




