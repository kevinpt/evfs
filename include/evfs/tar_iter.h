#ifndef TAR_ITER_H
#define TAR_ITER_H

#include "tar_common.h"



typedef struct TarFileIterator {
  EvfsFile *fd;

  TarHeader cur_header;

  evfs_off_t header_offset;  // Offset within tar file
  evfs_off_t file_size;      // Size of current archived file

} TarFileIterator;

#ifdef __cplusplus
extern "C" {
#endif

void tar_iter_init(TarFileIterator *tar_it, EvfsFile *fd);
void tar_iter_close(TarFileIterator *tar_it);
bool tar_iter_seek(TarFileIterator *tar_it, evfs_off_t offset);
#define tar_iter_begin(r)  tar_iter_seek(r, 0)
bool tar_iter_next(TarFileIterator *tar_it);
#define tar_iter_reset(r) tar_iter_seek((r), (r)->header_offset)

evfs_off_t tar_iter_file_offset(TarFileIterator *tar_it);

#ifdef __cplusplus
}
#endif

#endif // TAR_ITER_H

