#ifndef TAR_COMMON_H
#define TAR_COMMON_H

#define TAR_BLOCK_SIZE 512
#define TAR_HEADER_SIZE 500


#define TAR_FILE_NAME_LEN    100
#define TAR_LINK_NAME_LEN    100
#define TAR_FILE_PREFIX_LEN  155

typedef struct TarHeader
{                                    // Field offset
  uint8_t file_name[TAR_FILE_NAME_LEN];     //   0
  uint8_t mode[8];                          // 100
  uint8_t uid[8];                           // 108
  uint8_t gid[8];                           // 116
  uint8_t size[12];                         // 124
  uint8_t mtime[12];                        // 136
  uint8_t checksum[8];                      // 148
  uint8_t type_flag;                        // 156
  uint8_t link_name[TAR_LINK_NAME_LEN];     // 157
  uint8_t magic[6];                         // 257
  uint8_t version[2];                       // 263
  uint8_t uname[32];                        // 265
  uint8_t gname[32];                        // 297
  uint8_t dev_major[8];                     // 329
  uint8_t dev_minor[8];                     // 337
  uint8_t file_prefix[TAR_FILE_PREFIX_LEN]; // 345
} __attribute__((packed)) TarHeader;


// Values for type_flag field:
#define TAR_TYPE_NORMAL_FILE '0'
#define TAR_TYPE_HARD_LINK   '1'
#define TAR_TYPE_SYM_LINK    '2'
#define TAR_TYPE_CHAR_DEV    '3'
#define TAR_TYPE_BLOCK_DEV   '4'
#define TAR_TYPE_DIRECTORY   '5'
#define TAR_TYPE_FIFO        '6'
#define TAR_TYPE_CONTIG_FILE '7'
#define TAR_TYPE_GLOBAL_EXT  'g'
#define TAR_TYPE_EXT         'x'



#endif // TAR_COMMON_H

