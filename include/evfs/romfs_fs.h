/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Romfs VFS
  A VFS implementation of Linux Romfs.
------------------------------------------------------------------------------
*/

#ifndef ROMFS_FS_H
#define ROMFS_FS_H


int evfs_register_romfs(const char *vfs_name, EvfsFile *image, bool default_vfs);

#endif // ROMFS_FS_H
