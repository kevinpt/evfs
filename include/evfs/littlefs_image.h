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

  Routines to manage Littlefs images hosted on another filesystem

  This implements an EVFS based backend for Littlefs
------------------------------------------------------------------------------
*/

#ifndef LITTLEFS_IMAGE_H
#define LITTLEFS_IMAGE_H

typedef struct LittlefsImage_s {
  EvfsFile *fh; // Opened file handle for lfs image
} LittlefsImage;


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

#endif // LITTLEFS_IMAGE_H
