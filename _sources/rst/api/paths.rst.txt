=============
Path handling
=============

EVFS needs to be able to work with file and directory paths. The library provides functions to help you manipulate paths in an OS agnostic way. These are pure string manipulations and the referenced files and directories do not have to exist on any VFS. The operations allow you to extract portions of a path, join paths together, normalize them, and convert relative paths into absolute.

The representation of absolute paths can vary between systems. In Unix derived systems a bare '/' represents the root of the filesystem tree. On DOS/Windows systems this can be more elaborate with optional drive letters and other variants. To make path processing more universal, these functions work in conjunction with a method from each VFS that identifies the root component of a path and whether it is absolute. This portion of a path is left largely untouched by the EVFS library. Paths are passed through and validated by the underlying filesystem. Because of this behavior you will need at least one filesystem registered with EVFS before you can operate on paths. If you register multiple filesystems with different conventions for the root component you must ensure that the right VFS is referenced with its matching paths.

String ranges
-------------

The EVFS path API functions frequently take a :c:type:`StringRange` or :c:type:`AppendRange` struct as input and output parameters. These are part of a small string utility in 'src/util/range_strings.c'. These structs represent a substring pointing into another string. :c:type:`StringRange` objects represent a substring that will not change bounds after a function call. :c:type:`AppendRange` objects cover the empty space at the end of a string to append into. They have their start position advanced after an append operation. They have the same layout and only differ by the presence of const pointers in :c:type:`StringRange`. These types can be casted back and forth as necessary.

The general procedure for using these types is to prepare a storage area for a string, either a local array or one produced by dynamic allocation. You then initialize the :c:type:`StringRange` to cover the char array. This can be accomplished with a static initializer for true arrays or by calling :c:func:`range_init` with the start and size of the array when you have a pointer to allocated storage.

.. code-block:: c

  char buf[100];
  StringRange buf_r = RANGE_FROM_ARRAY(buf); // Initializer can be used with array variables

  char *buf2 = evfs_malloc(100);
  StringRange buf2_r;
  range_init(&buf_2_r, buf2, 100); // Initialize pointers

  char *str; // Unknown length
  StringRange str_r;
  range_init(&str_r, str, strlen(str)+1); // When using strlen, add 1 to include the NUL


A substring pointed to by :c:type:`StringRange` will not necessarily be NUL terminated. To print the substring using :c:func:`printf` formatted output, you use the "%.*s" specifier and the :c:macro:`RANGE_FMT` macro:

.. code-block:: c

  StringRange *str;

  printf("Substring in range is: '%.*s'\n", RANGE_FMT(str));




Path API
--------

All of the functions that generate a new string from joining, normalization, or absolute conversion have been designed to safely overwrite their input string if it is also the output. This allows you to reuse an existing range and minimize the need for temporary buffers. These functions also avoid use of :c:func:`evfs_malloc` whenever possible.

Most of these functions have an "_ex" suffix indicating that their last parameter is an optional VFS name. All of these forms have a macro defined without the suffix that uses the default VFS.


.. c:function:: bool evfs_path_root_component_ex(const char *path, StringRange *root, const char *vfs_name)

  Get the root portion of a path.

  This is a virtual method that calls into a VFS specific implementation to
  handle different filesystem path formats. On POSIX style systems the
  root component of absolute paths is a leading sequence of one or more '/'
  chars and nothing for relative paths. On DOS/Windows filesystems the root
  component may also have a colon separated volume letter or number on
  absolute or relative paths.

  :param path:     Path to extract root from
  :param root:     Substring of path corresponding to root or empty if none found
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: true when the path is absolute



.. c:function:: bool evfs_path_is_absolute_ex(const char *path, const char *vfs_name)

  Detect if a path is absolute on the supplied VFS.

  :param path:       Path to test
  :param vfs_name:   VFS to work on. Use default VFS if NULL

  :return: true if path is absolute



.. c:function:: int evfs_path_basename(const char *path, StringRange *tail)

  Get the file name portion of a path.

  This copies the behavior of Python os.path.basename().

  :param path:   Path to extract basename from
  :param tail:   Substring of path corresponding to the basename

  :return: EVFS_OK on success



.. c:function:: int evfs_path_extname(const char *path, StringRange *ext)

  Get the extension of a file.

  This copies the behavior of Python os.path.splitext().

  :param path:   Path to extract extension from
  :param ext:    Substring of path corresponding to the extension

  :return: EVFS_OK on success



.. c:function:: int evfs_path_dirname_ex(const char *path, StringRange *head, const char *vfs_name)

  Get the directory portion of a path.

  This copies the behavior of Python os.path.dirname().

  :param path:     Path to extract dirname from
  :param head:     Substring of path corresponding to the dirname
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success




.. c:function:: int evfs_path_join_ex(StringRange *head, StringRange *tail, StringRange *joined, const char *vfs_name)

  Join two paths.

  :param head:     Substring of left portion to join
  :param tail:     Substring of right portion to join
  :param joined:   Substring of output string. This can be the same as the head substring.
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_path_join_str_ex(const char *head, const char *tail, StringRange *joined, const char *vfs_name)

  Join two paths using char strings.

  :param head:     Substring of left portion to join
  :param tail:     Substring of right portion to join
  :param joined:   Substring of output string. This can be the same as the head path.
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_path_normalize_ex(const char *path, StringRange *normalized, const char *vfs_name)

  Normalize a path.

  This applies the following transformations:

  * Any root component is reduced to its minimal form.
  * Consecutive separators are merged into one
  * All separators after root component are converted to EVFS_DIR_SEP
  * "./" segments are removed
  * "../" segments are removed along with the preceeding segment
  * Trailing slashes are removed

  :param path:       Path to be normalized
  :param normalized: Normalized output path. This can be the same string as path
  :param vfs_name:   VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success




.. c:function:: int evfs_path_absolute_ex(const char *path, StringRange *absolute, const char *vfs_name)

  Convert a path to absolute form.

  If the path is already absolute it is normalized. Otherwise it is joined
  to the current working directory and normalized.

  :param path:     Path to become absolute
  :param absolute: Absolute output path. This can be the same string as path
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success





.. c:function:: bool evfs_vfs_path_is_absolute(Evfs *vfs, const char *path)

  Detect if a path is absolute on the supplied VFS.

  This variant is for fs wrappers to use without a name lookup.

  :param vfs:      The VFS for the path
  :param path:     Path to test

  :return: true if path is absolute





