/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
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
