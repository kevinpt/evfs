#ifndef SHIM_ROTATE_H
#define SHIM_ROTATE_H

typedef struct RotateConfig_s {
  // Geometry
  // Total capacity = max_chunks * chunk_size
  uint32_t      chunk_size;     // Must be at least MULTIPART_MIN_CHUNK_SIZE (32)
  uint32_t      max_chunks;     // Must be between 2 and MULTIPART_MAX_CHUNK (99999)
  bool          repair_corrupt; // Repair invalid containers by removing chunks
} RotateConfig;


int evfs_register_rotate(const char *vfs_name, const char *old_vfs_name, RotateConfig *cfg, bool default_vfs);

#endif // SHIM_ROTATE_H
