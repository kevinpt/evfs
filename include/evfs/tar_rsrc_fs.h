/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  TAR resource FS VFS
  A VFS wrapper for the TAR resource API.
------------------------------------------------------------------------------
*/

#ifndef TAR_RSRC_FS_H
#define TAR_RSRC_FS_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_tar_rsrc_fs(const char *vfs_name, uint8_t *resource, size_t resource_len,
                              bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // TAR_RSRC_FS_H
