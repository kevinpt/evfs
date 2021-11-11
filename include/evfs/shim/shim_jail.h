/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Jail shim VFS

  This creates a virtual root in a subdirectory for an undelying VFS similar
  to how chroot() works. This only affects access via the EVFS API that passes
  through the shim. If it isn't default VFS or the underlying FS is accessed
  by name then the path restiction can be bypassed.

  This can be used as a simplified way to perform operations using absolute
  paths that map into a subdirectory.
------------------------------------------------------------------------------
*/

#ifndef SHIM_JAIL_H
#define SHIM_JAIL_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_jail(const char *vfs_name, const char *old_vfs_name, const char *jail_root, bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // SHIM_JAIL_H
