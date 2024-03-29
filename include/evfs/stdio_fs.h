/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Stdio VFS
  A VFS wrapper for the Stdio API.
------------------------------------------------------------------------------
*/

#ifndef STDIO_FS_H
#define STDIO_FS_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_stdio(bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // STDIO_FS_H
