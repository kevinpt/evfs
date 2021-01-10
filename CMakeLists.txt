cmake_minimum_required(VERSION 3.13.4)
project(evfs VERSION 1.0.0 LANGUAGES C)

set (CMAKE_C_STANDARD 11)
set (CMAKE_C_STANDARD_REQUIRED ON)

# Debug options
set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=undefined")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=undefined")

# Enable warnings
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall)
endif()


#################### sources ####################
set(EVFS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(EVFS_LIB_SOURCE
  evfs.c
  evfs_c11_thread.c
  evfs_path.c
  shim_trace.c
  shim_jail.c
  shim_rotate.c
  util/glob.c
  util/range_strings.c
  stdio_fs.c
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
  pthread
)


#################### libevfs (shared) ####################
add_library(evfs_so SHARED ${EVFS_LIB_SOURCE})
set_target_properties(evfs_so PROPERTIES OUTPUT_NAME evfs)
target_include_directories(evfs_so
PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/evfs/util
)

target_link_libraries(evfs_so
PUBLIC
  pthread
)



#################### test_image ####################
add_executable(test_image EXCLUDE_FROM_ALL test/test_image.c)
target_sources(test_image
PRIVATE
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
add_executable(test_rotate EXCLUDE_FROM_ALL test/test_rotate.c)
target_sources(test_rotate
PRIVATE
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
add_executable(test_jail EXCLUDE_FROM_ALL test/test_jail.c)
target_sources(test_jail
PRIVATE
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



install(TARGETS evfs evfs_so
  DESTINATION "lib"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" # source directory
        DESTINATION "include" # target directory
        FILES_MATCHING # install only matched files
        PATTERN "*.h" # select header files
)
