/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Littlefs VFS
  A VFS wrapper for the Littlefs API.
------------------------------------------------------------------------------
*/

#ifndef LITTLEFS_FS_H
#define LITTLEFS_FS_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_littlefs(const char *vfs_name, lfs_t *lfs, bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // LITTLEFS_FS_H
