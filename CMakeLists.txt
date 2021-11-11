cmake_minimum_required(VERSION 3.15)

# Common build script for PC
include(scripts/build_pc.cmake)


project(evfs VERSION 1.0.0 LANGUAGES C CXX ASM)



#################### Build config settings ####################

configure_file (
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs_build_config.h.in
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/evfs_build_config.h
)

# Select optional threading library for concurrent access to objects
option(USE_C11_THREADS  "Enable support for C11 threading API"    OFF)
option(USE_PTHREADS     "Enable support for POSIX threading API"  OFF)




#################### sources ####################
set(EVFS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(EVFS_LIB_SOURCE
  evfs.c
  $<$<BOOL:${USE_C11_THREADS}>:evfs_c11_thread.c>
  $<$<BOOL:${USE_PTHREADS}>:evfs_pthread.c>
  evfs_path.c
  shim_trace.c
  shim_jail.c
  shim_rotate.c
  util/glob.c
  util/range_strings.c
  util/dhash.c
  util/search.c
  stdio_fs.c
  tar_iter.c
  tar_fs.c
  tar_iter_rsrc.c
  tar_rsrc_fs.c
  romfs_common.c
  romfs_fs.c
)

list(TRANSFORM EVFS_LIB_SOURCE PREPEND "${EVFS_PREFIX}/")


set(LFS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/vendored/littlefs")

set(LITTLEFS_SOURCE
  ${EVFS_PREFIX}/littlefs_fs.c
  ${EVFS_PREFIX}/littlefs_image.c
  ${LFS_PREFIX}/lfs.c
  ${LFS_PREFIX}/lfs_util.c
)


set(FF_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/vendored/fatfs")

set(FATFS_SOURCE
  ${EVFS_PREFIX}/fatfs_fs.c
  ${EVFS_PREFIX}/fatfs_image.c
  ${FF_PREFIX}/source/ff.c
  ${FF_PREFIX}/source/ffunicode.c
)


#################### libevfs (static) ####################

add_library(evfs STATIC ${EVFS_LIB_SOURCE})

target_include_directories(evfs
PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(evfs
PUBLIC
  $<$<OR:$<BOOL:${USE_C11_THREADS}>,$<BOOL:${USE_PTHREADS}>>:pthread>
)


#################### libevfs (shared) ####################
add_library(evfs_so SHARED ${EVFS_LIB_SOURCE})

set_target_properties(evfs_so
  PROPERTIES
    OUTPUT_NAME evfs
    VERSION     ${PROJECT_VERSION}
)

target_include_directories(evfs_so
PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(evfs_so
PUBLIC
  $<$<OR:$<BOOL:${USE_C11_THREADS}>,$<BOOL:${USE_PTHREADS}>>:pthread>
)



#################### test_image ####################

add_pc_executable(test_image
  SOURCE
    test/test_image.c
    ${EVFS_PREFIX}/util/getopt_r.c
    ${EVFS_PREFIX}/stdio_fs.c
    ${FATFS_SOURCE}
    ${LITTLEFS_SOURCE}
)

target_include_directories(test_image
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
  ${CMAKE_CURRENT_SOURCE_DIR}/vendored/littlefs
  ${CMAKE_CURRENT_SOURCE_DIR}/vendored/fatfs/source
)

target_link_libraries(test_image
PRIVATE
  evfs
)
#target_compile_definitions(test_image PRIVATE  -DLFS_YES_TRACE)


#################### test_rotate ####################

add_pc_executable(test_rotate
  SOURCE
    test/test_rotate.c
    ${EVFS_PREFIX}/util/getopt_r.c
    ${EVFS_PREFIX}/stdio_fs.c
)

target_include_directories(test_rotate
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(test_rotate
PRIVATE
  evfs
)


#################### test_jail ####################

add_pc_executable(test_jail
  SOURCE
    test/test_jail.c
    ${EVFS_PREFIX}/util/getopt_r.c
    ${EVFS_PREFIX}/stdio_fs.c
)

target_include_directories(test_jail
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(test_jail
PRIVATE
  evfs
)

#################### test_tar ####################

add_pc_executable(test_tar
  SOURCE
    test/test_tar.c
    ${EVFS_PREFIX}/util/getopt_r.c
    ${EVFS_PREFIX}/stdio_fs.c
)

target_include_directories(test_tar
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(test_tar
PRIVATE
  evfs
)


#################### test_romfs ####################

add_pc_executable(test_romfs
  SOURCE
    test/test_romfs.c
    ${EVFS_PREFIX}/util/getopt_r.c
    ${EVFS_PREFIX}/stdio_fs.c
    test/hex_dump.c
)

target_include_directories(test_romfs
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(test_romfs
PRIVATE
  evfs
)


#################### tests ####################

add_custom_target(test
  ALL
    DEPENDS test_image test_rotate test_jail test_tar test_romfs
)


#################### Installation ####################

install(TARGETS evfs evfs_so
  DESTINATION "lib"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" # source directory
        DESTINATION "include" # target directory
        FILES_MATCHING # install only matched files
        PATTERN "*.h" # select header files
)
