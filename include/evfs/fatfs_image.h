/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Routines to manage FatFs images hosted on another filesystem

  This implements an EVFS based backend for FatFs
------------------------------------------------------------------------------
*/
#ifndef FATFS_IMAGE_H
#define FATFS_IMAGE_H

typedef struct FatfsImage_s {
  EvfsFile *fh; // Opened file handle for lfs image
  FATFS fs;
} FatfsImage;


int fatfs_make_image(const char *img_path, uint8_t pdrv, evfs_off_t img_size);
int fatfs_mount_image(const char *img_path, uint8_t pdrv);
void fatfs_unmount_image(uint8_t pdrv);

#endif // FATFS_IMAGE_H
