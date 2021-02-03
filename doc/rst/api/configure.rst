================
Configuring EVFS
================

There are two sets of configuration options for the EVFS library. One is handled by the CMake build system using command line configuration settings. The other uses definitions from 'evfs_config.h' for settings that don't affect the build script.


CMake configuration
-------------------


Thread support
~~~~~~~~~~~~~~

EVFS uses shared data structures that need to be protected with locks in multi-threaded envronments. You will need to select the threading API that EVFS will work with.

Threading support is provided for the C11 and pthreads APIs. If you are targeting another platform like an RTOS, it is necessary to write wrapper functions that expose the required locking functionality to EVFS. See 'evfs_c11_thread.c' for an example of the required wrappers. You will also have to add a section to 'evfs_custom_threading.h' that provides a typedef for :c:type:`EvfsLock` and defines any :c:macro:`LOCK_INITIALIZER` macro.


.. c:macro:: USE_C11_THREADS

  Use the C11 threading API.

.. c:macro:: USE_PTHREADS

  Use the pthreads API.


These are Boolean CMake options that are configured from the command line:

.. code-block:: sh

  > cmake -DUSE_C11_THREADS=on .

  > cmake -DUSE_PTHREADS=on .


C11 threads will take priority if both are enabled.


evfs_config.h
-------------

The configuration header has defaults that are set up to work in a typical PC environment. For embedded targets you will need modify features like the thread support settings.


Debug settings
~~~~~~~~~~~~~~

.. c:macro:: EVFS_DEBUG

  Define this to enable debugging of EVFS. Set it to 1 to enable and 0 to disable debugging. If it is undefined, EVFS debugging will be active unless :c:macro:`NDEBUG` is defined.

.. c:macro:: EVFS_ASSERT_LEVEL

  This sets the debug levels for the library's internal :c:macro:`ASSERT()` macro. It should be set to one of the following values:

  * 0 - Ignore assertion
  * 1 - Silent assertion check with no `stderr` output
  * 2 - Check assertion with `stderr` output
  * 3 - Check assertion with `stderr` output and call :c:func:`abort` when EVFS_DEBUG == 1, otherwise silent check


Path handling
~~~~~~~~~~~~~

.. c:macro:: EVFS_PATH_SEPS

  This is a string of all characters recognized as a path separator. Default is "\\\\/".

.. c:macro:: EVFS_DIR_SEP

  This is the character used as separator in normalized paths. Default is '/'.

.. c:macro:: EVFS_MAX_PATH

  Set the maximum allowed length for path strings. This affects the sizing of internal buffers. Default is 256. On memory constrained targets it may be necessary to reduce this.

.. c:macro:: ALLOW_LONG_PATHS

  Allow paths that exceed :c:macro:`EVFS_MAX_PATH` as output from :c:func:`evfs_path_join_ex` and :c:func:`evfs_path_absolute_ex`.



Library behavior
~~~~~~~~~~~~~~~~

.. c:macro:: EVFS_FILE_OFFSET_BITS

  Set the number of bits in :c:type:`evfs_off_t` used to handle file sizes and offsets. It defaults to 32-bits. Set this to 64 to enable 64-bit support. This will be necessary if you need to work with files larger than 2GiB.


.. c:macro:: EVFS_USE_ATEXIT

  Install an :c:func:`atexit` handler to shutdown the EVFS library when the process exits. Disable this if you are on an embedded platform where :c:func:`atexit` will not be called. If this is disabled you should always terminate by calling :c:func:`evfs_unregister_all` so that VFSs have the opportunity to flush data and release resources. 


.. c:macro:: EVFS_USE_ANSI_COLOR

  Use ANSI color for diagnostic debug output. This is useful for dealing with the output from the trace shim. Disable this if your debug console can't support color.


VFS options
~~~~~~~~~~~

These are options spcific to various filesystems and shims

.. c:macro:: EVFS_USE_STDIO_POSIX

  Enable POSIX API calls for the Stdio filesystem driver. The Stdio driver will have reduced functionality without this defined.

.. c:macro:: EVFS_USE_LITTLEFS_SHARED_BUFFER

  Save memory by using a common shared buffer in the littlefs driver.

.. c:macro::  EVFS_USE_ROTATE_SHARED_BUFFER

  Save memory by using a common shared buffer in the rotate shim driver.

