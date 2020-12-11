/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Routines to manage Littlefs images hosted on another filesystem

  This implements an EVFS based backend for Littlefs
------------------------------------------------------------------------------
*/

#include "lfs.h"
#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/littlefs_image.h"


/*
Make a Littlefs image file if it doesn't exist

Image geometry is taken from the configuration data.

Args:
  img_path:  Path to the image file
  cfg:       Configuration struct for the Littlefs this is mounted on

Returns:
  EVFS_OK on success
*/
int littlefs_make_image(const char *img_path, struct lfs_config *cfg) {
  if(PTR_CHECK(img_path) || PTR_CHECK(cfg)) return EVFS_ERR_BAD_ARG;

  LittlefsImage *img = (LittlefsImage *)cfg->context;

  if(PTR_CHECK(img)) return EVFS_ERR;

  int status = evfs_open(img_path, &img->fh, EVFS_WRITE | EVFS_NO_EXIST);

  if(status == EVFS_OK) {
    // Expand image file to match configuration data
    evfs_file_seek(img->fh, cfg->block_size*cfg->block_count - 1, EVFS_SEEK_TO);

    char buf[] = "\0";
    status = evfs_file_write(img->fh, buf, sizeof(char));
    if(status > 0) {
      // Reopen for formatting
      evfs_file_close(img->fh);
      status = evfs_open(img_path, &img->fh, EVFS_READ | EVFS_WRITE);

      lfs_t lfs;
      status = (lfs_format(&lfs, cfg) == LFS_ERR_OK) ? EVFS_OK : EVFS_ERR;
    }

    evfs_file_close(img->fh);

  }

  img->fh = NULL;
  return status;
}


/*
Mount a Littlefs image


Args:
  img_path:  Path to the image file
  cfg:       Configuration struct for the Littlefs this is mounted on
  lfs:       Littlefs filesystem object to mount onto

Returns:
  EVFS_OK on success
*/
int littlefs_mount_image(const char *img_path, struct lfs_config *cfg, lfs_t *lfs) {
  LittlefsImage *img = (LittlefsImage *)cfg->context;

  // Open the image
  int status = evfs_open(img_path, &img->fh, EVFS_READ | EVFS_WRITE);
  if(status != EVFS_OK)
    return status;

  // Confirm the image size matches the config
  evfs_off_t expect_size = cfg->block_size * cfg->block_count;
  if(evfs_file_size(img->fh) != expect_size) {
    evfs_file_close(img->fh);
    return EVFS_ERR;
  }

  status = lfs_mount(lfs, cfg); // Mount the filesystem
  status = (status == LFS_ERR_OK) ? EVFS_OK : EVFS_ERR;

  if(status != EVFS_OK)
    evfs_file_close(img->fh);

  return status;
}


/*
Unmount a Littlefs image

Args:
  lfs:       Mounted Littlefs filesystem object

*/
void littlefs_unmount_image(lfs_t *lfs) {
  LittlefsImage *img = (LittlefsImage *)lfs->cfg->context;

  lfs_unmount(lfs);
  evfs_file_close(img->fh);
}




//////////////////////////////////////////////////////////
// LFS device operations for image file in EVFS container

int littlefs_image_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {

  LittlefsImage *img = cfg->context;

  evfs_file_seek(img->fh, block * cfg->block_size + off, EVFS_SEEK_TO);
  ptrdiff_t read = evfs_file_read(img->fh, buffer, size);

  return read == size ? LFS_ERR_OK : LFS_ERR_IO;
}

int littlefs_image_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {

  LittlefsImage *img = cfg->context;

  evfs_file_seek(img->fh, block * cfg->block_size + off, EVFS_SEEK_TO);

  ptrdiff_t wrote = evfs_file_write(img->fh, buffer, size);
  return wrote == size ? LFS_ERR_OK : LFS_ERR_IO;
}


// Erase not needed without direct hardware I/O
int littlefs_image_erase(const struct lfs_config *cfg, lfs_block_t block) {
  return LFS_ERR_OK;
}

int littlefs_image_sync(const struct lfs_config *cfg) {
  LittlefsImage *img = cfg->context;
  return evfs_file_sync(img->fh) == EVFS_OK ? LFS_ERR_OK : LFS_ERR_IO;
}


/*

// Example littlefs configuration using the image I/O callbacks

LittlefsImage s_lfs_img = {0};

#define KB  *1024UL
#define LFS_VOL_SIZE   (512 KB)
#define LFS_BLOCK_SIZE  4096

struct lfs_config s_lfs_cfg = {
    // Must have a valid LittlefsImage object for context data
    .context = &s_lfs_img,

    // Block device operations
    .read  = littlefs_image_read,
    .prog  = littlefs_image_prog,
    .erase = littlefs_image_erase,
    .sync  = littlefs_image_sync,

    // Block device configuration
    .read_size      = 16,
    .prog_size      = 16,
    .block_size     = LFS_BLOCK_SIZE,
    .block_count    = LFS_VOL_SIZE / LFS_BLOCK_SIZE,
    .cache_size     = 1024,
    .lookahead_size = 16,
    .block_cycles   = -1, // Disable wear leveling  // 500,
};

*/
