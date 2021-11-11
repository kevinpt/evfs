/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/


/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Tracing shim VFS
  This adds debugging traces for calls to the underlying VFS.
------------------------------------------------------------------------------
*/

#ifndef SHIM_TRACE_H
#define SHIM_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

int evfs_register_trace(const char *vfs_name, const char *old_vfs_name,
    int (*report)(const char *buf, void *ctx), void *ctx, bool default_vfs);

#ifdef __cplusplus
}
#endif

#endif // SHIM_TRACE_H
