/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Configuration for thread API
------------------------------------------------------------------------------
*/

#ifndef EVFS_CUSTOM_THREADING_H
#define EVFS_CUSTOM_THREADING_H

// Enable declarations in evfs_internal.h
#define EVFS_SOURCE_USES_LOCK

// ******************** C11 threads ********************
#if defined USE_C11_THREADS
#  include <threads.h>

typedef mtx_t EvfsLock;

// This is just a placeholder C11 threads require dynamic init
#  define LOCK_INITIALIZER   {0}


// ******************** pthreads ********************
#elif defined USE_PTHREADS
#  include <pthread.h>

typedef pthread_mutex_t EvfsLock;

#  define LOCK_INITIALIZER   PTHREAD_MUTEX_INITIALIZER
#  define HAVE_STATIC_LOCK_INIT


// ******************** Default ********************
#else
#  error "EVFS needs a threading library configured"
#endif


#endif // EVFS_CUSTOM_THREADING_H
