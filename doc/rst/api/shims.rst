=====
Shims
=====

EVFS allows you to install shim VFS objects on top of existing filesystem interfaces or other shims. These intercept function calls passing through the shim and can change the apparent behavior of the underlying filesystem. Shims do not mask the underlying VFS object. They can still be accessed by their own name or can be set to be the new default VFS.



Trace
-----

The tracing shim adds debug output for EVFS operations. You register it with a callback function that will receive debug strings for console output. As EVFS is used, each function will generate diagnostic reports on what EVFS function is being called and its return value. This is useful for debugging new filesystem interfaces and shims. You can have multiple tracing shims interspersed within a stack of other shims and the bottom VFS. This allows you to see where errors are stemming from in the stack of VFSs.

The output of the trace shim is colorized by default. Remove the :c:macro:`EVFS_USE_ANSI_COLOR` define in 'evfs_config.h' to revert to plain text.

.. c:function:: int evfs_register_trace(const char *vfs_name, const char *old_vfs_name, int (*report)(const char *buf, void *ctx), void *ctx, bool default_vfs)

  Register a tracing filesystem shim.

  :param vfs_name:      Name of new shim
  :param old_vfs_name:  Existing VFS to wrap with shim
  :param report:        Callback function for trace output
  :param ctx:           User defined context for the report callback
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success


.. code-block:: c

  #include "evfs.h"
  #include "evfs/shim/shim_trace.h"

  // Callback for trace shim
  int report(const char *buf, void *ctx) {
    fputs(buf, (FILE *)ctx);
    return 0;
  }

  ...

  evfs_register_stdio(/*default_vfs*/ false);
  evfs_register_trace("t_stdio", "stdio", report, stderr, /*default_vfs*/ true);

  // The following operations will generate trace output

  EvfsFile *fh;
  evfs_open("/file.txt", &fh, EVFS_READ);

  evfs_file_close(fh);



Jail
----

The jail shim treats a designated directory as the root of a jailed VFS. You can work with absolute paths within the jail that map to this base directory on the real filesystem. The jail keeps its own concept of the current directory so that relative paths can be used within the jailed VFS.

.. c:function:: int evfs_register_jail(const char *vfs_name, const char *old_vfs_name, const char *jail_root, bool default_vfs)

  Register a jail filesystem shim.

  :param vfs_name:      Name of new shim
  :param old_vfs_name:  Existing VFS to wrap with shim
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success


.. code-block:: c

  #include "evfs.h"
  #include "evfs/shim/shim_jail.h"

  ...

  evfs_register_stdio(/*default_vfs*/ false);
  evfs_register_jail("my_jail", "stdio", "/home/user/testing", /*default_vfs*/ true);

  // my_jail VFS root maps to /home/user/testing

  EvfsFile *fh;
  evfs_open("/etc/passwd", &fh, EVFS_WRITE);

  evfs_make_dir("/lib/foo");
  evfs_make_dir("../foo");  // Normalizes to "/foo" within the jail




Rotate
------

The rotate shim implements virtual self-rotating files useful for logging data.
Older file contents are gradually purged once the log file reaches its
maximum size. 

.. warning::
  Do not use this for important data. There are latent race
  conditions that can cause data loss.


The virtual files are represented as a container directory in the underlying
filesystem. When the container is accessed through this shim it will appear as a single
continuous file of data. You can perform all normal file operations on an
open file handle. Understand that as rotation happens the offsets of the
file contents will change. You should not access a container simultaneously
through multiple file handles as they will not be synchronized. Use append
mode writes to add data to the end of the file.

The initial container configuration settings are passed to
:c:func:`evfs_register_rotate` when the shim is installed. If you need to change the
settings you can send a new :c:struct:`RotateConfig` struct to the shim using the 
:c:macro:`EVFS_CMD_SET_ROTATE_CFG` as the operation :c:func:`evfs_vfs_ctrl_ex`.

Rotation will leave portions of data spanning the chunk boundary at the new
start of the file. For text files, the first line will be missing an initial
portion. You can trim off this first fragmentary line by scanning for the
newline. With binary data you have to be prepared to lose a portion of a record
unless you always write a fixed record size that is an integral factor of the
chunk size. Otherwise you will need to have some form or synchronizing
information stored periodically so you can skip past the truncated data
remaining at the start of the file.


.. c:struct:: RotateConfig

  Configuration settings for the rotate shim

  * :c:texpr:`uint32_t` chunk_size     - Size of each chunk
  * :c:texpr:`uint32_t` max_chunks     - Maximum chunks in the file container
  * :c:texpr:`bool`     repair_corrupt - Repair corrupted containers


.. c:function:: int evfs_register_rotate(const char *vfs_name, const char *old_vfs_name, RotateConfig *cfg, bool default_vfs)

  Register a rotate filesystem shim.

  :param vfs_name:      Name of new shim
  :param old_vfs_name:  Existing VFS to wrap with shim
  :param cfg:           Configuration settings for new containers
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success



.. code-block:: c

  #include "evfs.h"
  #include "evfs/shim/shim_rotate.h"

  ...

  // Log file will have 100 chunks of 50KiB for a max capacity of 5MiB.
  RotateConfig cfg = {
    .chunk_size = 50 * 1024,
    .max_chunks = 100,
    .repair_corrupt = true
  };

  evfs_register_stdio(/*default_vfs*/ true);
  evfs_register_jail("rotate", "stdio", &cfg, /*default_vfs*/ false);

  // Open a container. This is a directory that appears to be a file
  EvfsFile *fh;
  evfs_open_ex("log.txt", &fh, EVFS_WRITE | EVFS_CREATE_OR_NEW| EVFS_APPEND, "rotate");

  char buf[100];

  ...

  evfs_file_write(fh, buf, strlen(buf)); // Write to rotating log

  evfs_file_close(fh);

Implementation
~~~~~~~~~~~~~~

The container directory contains a configuration file recording the
geometry settings the container was created with and multiple chunk files
that contain segments of the file's data. Chunks have a fixed size and there
is a maximum number of chunks set on creation. You can have no more than
99999 chunks. The minimum chunk size is limited to 32 bytes to protect against
excessive filesystem activity. Normally this should be a few kilobytes or larger
depending on your system's needs and capabilities.

The chunking algorithm is designed to work on systems that don't record
timestamps. When a container is opened the chunks are scanned to find the
start and end of the sequence. This requires that single gap is present
in the chunk number sequence. If there are multiple gaps in the sequence,
these two end points can't be unambiguously identified and the container is
unusable. There is an optional repair procedure that will drop enough chunks
to restore a valid sequence. This should be unlikely to happen if you only
write to the container in append mode. If you perform random access writes in
the middle of the file there is a risk of a chunk disappearing or becoming zero
length if a system fault happens.

The rotation process only involves deleting the oldest chunk at the start of
the file. This minimizes the amount of filesystem activity on flash based
filesystems.





