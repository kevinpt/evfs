#ifndef EVFS_CUSTOM_THREADING_H
#define EVFS_CUSTOM_THREADING_H

// Enable declarations in evfs_internal.h
#define EVFS_SOURCE_USES_LOCK

// ******************** C11 threads ********************
#if defined EVFS_USE_C11_THREADS
#  include <threads.h>

typedef mtx_t EvfsLock;

// This is just a placeholder C11 threads require dynamic init
#  define LOCK_INITIALIZER   {0}


// ******************** pthreads ********************
#elif defined EVFS_USE_PTHREADS
#  include <pthread.h>

typedef pthread_mutex_t EvfsLock;

#  define LOCK_INITIALIZER   PTHREAD_MUTEX_INITIALIZER
#  define HAVE_STATIC_LOCK_INIT


// ******************** Default ********************
#else
#  error "EVFS needs a threading library configured"
#endif


#endif // EVFS_CUSTOM_THREADING_H
