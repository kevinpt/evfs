/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Thread wrappers for C11 threads
------------------------------------------------------------------------------
*/

#include "evfs.h"
#include "evfs_internal.h"

#if defined EVFS_USE_THREADING && defined USE_C11_THREADS

static once_flag s_evfs_init_flag = ONCE_FLAG_INIT;
void evfs__init_once(void) {
  call_once(&s_evfs_init_flag, evfs__lib_init);
}

// ******************** Locking API ********************

int evfs__lock_init(EvfsLock *lock) {
  int err = mtx_init(lock, mtx_plain);
  return err == thrd_success ? EVFS_OK : EVFS_ERR;
}

int evfs__lock_destroy(EvfsLock *lock) {
  mtx_destroy(lock);
  return EVFS_OK;
}


int evfs__lock(EvfsLock *lock) {
  int err = mtx_lock(lock);
  return err == thrd_success ? EVFS_OK : EVFS_ERR;
}

int evfs__unlock(EvfsLock *lock) {
  int err = mtx_unlock(lock);
  return err == thrd_success ? EVFS_OK : EVFS_ERR;
}

#endif
