
#include "evfs.h"
#include "evfs_internal.h"

#if defined EVFS_USE_THREADING && defined EVFS_USE_PTHREADS


static pthread_once_t s_evfs_init_flag = PTHREAD_ONCE_INIT;
void evfs__init_once(void) {
  pthread_once(&s_evfs_init_flag, evfs__lib_init);
}

// ******************** Locking API ********************

int evfs__lock_init(EvfsLock *lock) {
  int err = pthread_mutex_init(lock, NULL);
  return err == 0 ? EVFS_OK : EVFS_ERR;
}

int evfs__lock_destroy(EvfsLock *lock) {
  int err = pthread_mutex_destroy(lock);
  return err == 0 ? EVFS_OK : EVFS_ERR;
}


int evfs__lock(EvfsLock *lock) {
  int err = pthread_mutex_lock(lock);
  return err == 0 ? EVFS_OK : EVFS_ERR;
}

int evfs__unlock(EvfsLock *lock) {
  int err = pthread_mutex_unlock(lock);
  return err == 0 ? EVFS_OK : EVFS_ERR;
}

#endif
