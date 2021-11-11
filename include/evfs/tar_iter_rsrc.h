#ifndef TAR_ITER_RSRC_H
#define TAR_ITER_RSRC_H

#include "tar_common.h"



typedef struct TarRsrcIterator {
  uint8_t  *resource;
  uint8_t  *read_pos;
  size_t    resource_len;

  TarHeader *cur_header;

  size_t    header_offset;  // Offset within tar file
  size_t    file_size;      // Size of current archived file

} TarRsrcIterator;

#ifdef __cplusplus
extern "C" {
#endif

void tar_rsrc_iter_init(TarRsrcIterator *tar_it, uint8_t *resource, size_t resource_len);

bool tar_rsrc_iter_seek(TarRsrcIterator *tar_it, evfs_off_t offset);
#define tar_rsrc_iter_begin(r)  tar_rsrc_iter_seek(r, 0)
bool tar_rsrc_iter_next(TarRsrcIterator *tar_it);
#define tar_rsrc_iter_reset(r) tar_rsrc_iter_seek((r), (r)->header_offset)

size_t tar_rsrc_iter_file_offset(TarRsrcIterator *tar_it);

#ifdef __cplusplus
}
#endif

#endif // TAR_ITER_RSRC_H

