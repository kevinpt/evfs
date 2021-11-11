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

#ifndef LITTLEFS_IMAGE_H
#define LITTLEFS_IMAGE_H

typedef struct LittlefsImage_s {
  EvfsFile *fh; // Opened file handle for lfs image
} LittlefsImage;

#ifdef __cplusplus
extern "C" {
#endif

int littlefs_make_image(const char *img_path, struct lfs_config *cfg);
int littlefs_mount_image(const char *img_path, struct lfs_config *cfg, lfs_t *lfs);
void littlefs_unmount_image(lfs_t *lfs);

// Callbacks for lfs configuration
int littlefs_image_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);
int littlefs_image_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);
int littlefs_image_erase(const struct lfs_config *cfg, lfs_block_t block);
int littlefs_image_sync(const struct lfs_config *cfg);

#ifdef __cplusplus
}
#endif

#endif // LITTLEFS_IMAGE_H
