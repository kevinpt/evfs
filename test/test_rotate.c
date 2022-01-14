#include <stdio.h>
#include <string.h>

#include "evfs.h"

#include "evfs/stdio_fs.h"
#include "evfs/shim/shim_trace.h"
#include "evfs/shim/shim_rotate.h"

#include "evfs/util/getopt_r.h"


// Callback for trace shim
static int treport(const char *buf, void *ctx) {
  fputs(buf, (FILE *)ctx);
  return 0;
}


int main(int argc, char *argv[]) {
  printf("Rotate test\n");

  evfs_init();

  // Process command line
  struct s_options {
    char operation;
    bool show_trace;
  } options;


  options.operation = 'w';
  options.show_trace = false;

  GetoptState state = {.report_errors = true};

  int c;
  while((c = getopt_r(argv, "o:th", &state)) != -1) {
    switch(c) {
    case 'o':
      if(state.optarg) {
        if(!strcmp(state.optarg, "trim")) {
          options.operation = 'x';
        } else {
          options.operation = state.optarg[0];
        }
      }
      break;
    case 't':
      options.show_trace = true;
      break;
    default:
    case 'h':
    case ':':
    case '?':
      {
        // Get the base name for the executable
        StringRange base;
        evfs_path_basename(argv[0], &base);

        printf("Usage: %.*s [-o write|read|trunc|r] [-t] [-h]\n", RANGE_FMT(&base));
        puts("  -o <op>\ttest operation on container");
        puts("  -t     \tshow EVFS tracing");
        puts("  -h     \tdisplay this help and exit");
      }
      return 0;
      break;
    }
  }







  RotateConfig cfg = {
    .chunk_size = 40,
    .max_chunks = 4
  };


  int status;

  evfs_register_stdio(/*default*/ false);
  if(options.show_trace) {
    evfs_register_trace("t_stdio", "stdio", treport, stderr, /*default*/ false);
    status = evfs_register_rotate("rot", "t_stdio", &cfg, /*default*/ true);
  } else {
    status = evfs_register_rotate("rot", "stdio", &cfg, /*default*/ true);
  }

  if(status != EVFS_OK) {
    printf("Failed to register shim: %s\n", evfs_err_name(status));
    return 1;
  }

  cfg.chunk_size = 50;

  status = evfs_vfs_ctrl(EVFS_CMD_SET_ROTATE_CFG, (void *)&cfg);
  printf("Reconfigured geometry: %s\n", evfs_err_name(status));

  EvfsFile *fh;
  status = evfs_open("test.log", &fh, EVFS_RDWR | EVFS_APPEND);

  if(status == EVFS_OK) {
    printf("Opened file\n");

    char buf[64*(15+1)];

    // Write
    if(options.operation == 'w') {
      printf("**** WRITING\n");
      for(int i = 0; i < 3; i++) {
        sprintf(buf, "|%04d,abcdefghijklmnopqrstuvwxy\n", i);
        printf("Write\n");
        evfs_file_write(fh, buf, strlen(buf));
      }
    }


    // Read
    printf("File size: %ld\n", (int64_t)evfs_file_size(fh));

    if(options.operation == 'r') {
      printf("**** READING\n");
      ptrdiff_t read = evfs_file_read(fh, buf, COUNT_OF(buf)-1);
      printf("Read: %ld   buf size: %ld\n", read,  COUNT_OF(buf));
      if(read > 0) {
        printf(">>>>\n");
        buf[read] = '\0';
        fputs(buf, stdout);
        printf("<<<<\n");
      } else {
        printf("Read error: %ld\n", read);
      }
    }

    // Truncate
    if(options.operation == 't') {
      printf("**** TRUNCATING\n");
      size_t file_size = evfs_file_size(fh);

      size_t trunc_bytes = 40; //(40*2+10);
      if(trunc_bytes > file_size)
        trunc_bytes = file_size;

      evfs_file_truncate(fh, file_size - trunc_bytes);
      file_size = evfs_file_size(fh);
      printf("New file size: %ld\n", file_size);
    }


   // Trim
    if(options.operation == 'x') {
      printf("**** TRIMMING\n");
      size_t trim_bytes = 50*3 + 5;
      status = evfs_file_ctrl(fh, EVFS_CMD_SET_ROTATE_TRIM, &trim_bytes);
      printf("Trimmed: %ld -> %d\n", trim_bytes, status);
    }


/*    evfs_file_rewind(fh);
    read = evfs_file_read(fh, buf, COUNT_OF(buf)-1);
    printf("Read: %ld   buf size: %ld\n", read,  COUNT_OF(buf));
    if(read > 0) {
      printf(">>>>\n");
      buf[read] = '\0';
      fputs(buf, stdout);
      printf("<<<<\n");
    } else {
      printf("Read error: %ld\n", read);
    }*/


    evfs_file_rewind(fh);
    //evfs_file_seek(fh, -10, EVFS_SEEK_REL);
    evfs_delete_ex("copy.txt", "stdio");
    evfs_copy_to_file_ex("copy.txt", fh, NULL, 0, "stdio");

    evfs_off_t log_size = evfs_file_size(fh);

    evfs_file_close(fh);

    evfs_open("copy.txt", &fh, EVFS_READ);
    evfs_off_t copy_size = evfs_file_size(fh);
    printf("File sizes: log: %d  copy: %d\n", log_size, copy_size);

    evfs_file_close(fh);

    //evfs_delete("test.log");
  }

  return 0;
}
