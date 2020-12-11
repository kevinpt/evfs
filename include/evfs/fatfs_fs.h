/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  FatFs VFS
  A VFS wrapper for the FatFs API.
------------------------------------------------------------------------------
*/

#ifndef FATFS_FS_H
#define FATFS_FS_H


int evfs_register_fatfs(const char *vfs_name, uint8_t pdrv, bool default_vfs);

#endif // FATFS_FS_H
