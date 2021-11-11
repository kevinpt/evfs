/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  TAR FS VFS
  A VFS wrapper for the TAR files API.
------------------------------------------------------------------------------
*/

#ifndef TAR_FS_H
#define TAR_FS_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_tar_fs(const char *vfs_name, EvfsFile *tar_file, bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // TAR_FS_H
