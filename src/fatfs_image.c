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

#include <time.h>
#include <stdio.h>
#include <stdint.h>

#include "ff.h"
#include "diskio.h"

#include "evfs.h"
#include "evfs_internal.h"
#include "evfs/fatfs_image.h"

static FatfsImage s_fatfs_image_data[FF_VOLUMES] = {0};

static inline FatfsImage *get_image_data(BYTE pdrv) {
  return &s_fatfs_image_data[pdrv];
}


/*
Make a FatFs image file if it doesn't exist

If a new image is created it will be formatted.

Args:
  img_path:  Path to the image file
  pdrv:      Any unused FatFs volume number
  img_size:  Size of the image to generate

Returns:
  EVFS_OK on success
*/
int fatfs_make_image(const char *img_path, uint8_t pdrv, evfs_off_t img_size) {
  if(PTR_CHECK(img_path)) return EVFS_ERR_BAD_ARG;

  // We need to use a FatFs volume entry for formatting
  FatfsImage *img = get_image_data(pdrv);

  if(PTR_CHECK(img)) return EVFS_ERR;

  int status = evfs_open(img_path, &img->fh, EVFS_WRITE | EVFS_NO_EXIST);

  if(status == EVFS_OK) {
    // Expand image file to match configuration data
    evfs_file_seek(img->fh, img_size - 1, EVFS_SEEK_TO);

    char buf[] = "\0";
    status = evfs_file_write(img->fh, buf, sizeof(char));
    if(status > 0) {
#if FF_FS_READONLY == 0
      // Reopen for formatting
      evfs_file_close(img->fh);
      evfs_open(img_path, &img->fh, EVFS_READ | EVFS_WRITE);


      char drive_path[3];
      char buf[FF_MAX_SS*4];

      snprintf(drive_path, 2, "%d", pdrv);

      FRESULT err = f_mkfs(drive_path, NULL, buf, COUNT_OF(buf));
      status = (err == FR_OK) ? EVFS_OK : EVFS_ERR;
#else
      status = EVFS_ERR_NO_SUPPORT;
#endif
    }

    evfs_file_close(img->fh);
  }

  img->fh = NULL;
  return status;
}


/*
Mount a FatFs image


Args:
  img_path:  Path to the image file
  pdrv:      FatFs volume number where this image will be mounted

Returns:
  EVFS_OK on success
*/
int fatfs_mount_image(const char *img_path, uint8_t pdrv) {
  if(PTR_CHECK(img_path)) return EVFS_ERR_BAD_ARG;
  FatfsImage *img = get_image_data(pdrv);

  // Open the image
  int status = evfs_open(img_path, &img->fh, EVFS_READ | EVFS_WRITE);
  if(status != EVFS_OK)
    return status;


  char drive_path[3];
  snprintf(drive_path, 2, "%d", pdrv);

  FRESULT err = f_mount(&img->fs, drive_path, 1);
  status = (err == FR_OK) ? EVFS_OK : EVFS_ERR;

  if(status != EVFS_OK)
    evfs_file_close(img->fh);

  return status;
}


/*
Unmount a FatFs image


Args:
  pdrv:      FatFs volume number where this image is mounted

*/
void fatfs_unmount_image(uint8_t pdrv) {
  FatfsImage *img = get_image_data(pdrv);

  char drive_path[3];
  snprintf(drive_path, 2, "%d", pdrv);
  f_unmount(drive_path);

  evfs_file_close(img->fh);
}




//////////////////////////////////////////////////////////
// FatFs device operations for image file in EVFS container


DSTATUS disk_initialize(BYTE pdrv) {
  FatfsImage *img = get_image_data(pdrv);
  return img ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
  FatfsImage *img = get_image_data(pdrv);
  return img ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  FatfsImage *img = get_image_data(pdrv);

  evfs_file_seek(img->fh, sector * FF_MAX_SS, EVFS_SEEK_TO);
  ptrdiff_t read = evfs_file_read(img->fh, buff, count * FF_MAX_SS);

  return read == (count * FF_MAX_SS) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
  FatfsImage *img = get_image_data(pdrv);

  evfs_file_seek(img->fh, sector * FF_MAX_SS, EVFS_SEEK_TO);
  ptrdiff_t wrote = evfs_file_write(img->fh, buff, count * FF_MAX_SS);

  return wrote == (count * FF_MAX_SS) ? RES_OK : RES_ERROR;
}


DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  FatfsImage *img = get_image_data(pdrv);
  if(!img)
    return RES_PARERR;

  switch(cmd) {
  case CTRL_SYNC:         // Complete pending write process (needed at FF_FS_READONLY == 0)
    return evfs_file_sync(img->fh) == EVFS_OK ? RES_OK : RES_ERROR;
    break;

  case GET_SECTOR_COUNT:  // Get media size (needed at FF_USE_MKFS == 1)
    {
      LBA_t *sectors = (LBA_t *)buff;
      *sectors = evfs_file_size(img->fh) / FF_MAX_SS;
      return RES_OK;
    }
    break;

  case GET_SECTOR_SIZE:   // Get sector size (needed at FF_MAX_SS != FF_MIN_SS)
    {
      WORD *ss = (WORD *)buff;
      *ss = FF_MAX_SS;
      return RES_OK;
    }
    break;

  case GET_BLOCK_SIZE:    // Get erase block size (needed at FF_USE_MKFS == 1)
    {
      WORD *ss = (WORD *)buff;
      *ss = FF_MAX_SS;
      return RES_OK;
    }
    break;

  default:
    return RES_PARERR;
    break;
  }
}


void *ff_memalloc(UINT msize) {
  return evfs_malloc(msize);
}

void ff_memfree(void* mblock) {
  evfs_free(mblock);
}


DWORD get_fattime(void) {
  time_t now_secs;
  struct tm now;

  time(&now_secs);
  localtime_r(&now_secs, &now);

  DWORD fat_time = ((now.tm_year-80) << 25) | ((now.tm_mon+1) << 21) |
    (now.tm_mday << 16) | (now.tm_hour << 11) |
    (now.tm_min << 5) | (now.tm_sec >> 1);

  return fat_time;
}

