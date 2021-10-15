# Common build script for generic PC

#################### Global build options ####################

set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(CMAKE_BUILD_TYPE MATCHES Debug)
   set(CMAKE_C_FLAGS_DEBUG "-O0 -g -gdwarf-3 -gstrict-dwarf")
   set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -gdwarf-3 -gstrict-dwarf")
endif()

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
   set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -DNDEBUG -g -gdwarf-3 -gstrict-dwarf")
   set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Os -DNDEBUG -g -gdwarf-3 -gstrict-dwarf")
endif()

# No change to default flags for Release and MinSizeRel


add_compile_options(
  -fno-exceptions
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  -funsigned-char
  -funsigned-bitfields
  -fno-common
  -fdata-sections
  -ffunction-sections
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -Wshadow
  -Werror
#  -Wfatal-errors
#  -Wpedantic
#  -pedantic-errors
#  -Wredundant-decls
#  -Wmissing-prototypes
  -Wdouble-promotion
  -Wundef
)


#################### Functions ####################

## Wrapper to add exec along with utility targets
function(add_pc_executable EXEC_NAME)
  cmake_parse_arguments(
    PARSE_ARGV 1 "ARG"
    "" "" "SOURCE"
  )


  if(NOT ARG_SOURCE)
    message(FATAL_ERROR "Target '${EXEC_NAME}' is missing source list")
  endif()


  # Add all project source
  add_executable(${EXEC_NAME} EXCLUDE_FROM_ALL
    ${ARG_SOURCE}
  )

  target_link_options(${EXEC_NAME}.elf
    PRIVATE
      "LINKER:--gc-sections"
      "LINKER:-Map,${EXEC_NAME}.map"
      "LINKER:--cref"
      "LINKER:--print-memory-usage"
  )


  #################### Generate reports ####################

  add_custom_command( OUTPUT ${EXEC_NAME}.lst
    COMMAND
      objdump -h -S ${EXEC_NAME} > ${EXEC_NAME}.lst
    DEPENDS ${EXEC_NAME}
  )


  #################### Utility ####################
  set_target_properties(${EXEC_NAME}
    PROPERTIES
      ADDITIONAL_CLEAN_FILES "${EXEC_NAME}.map"
  )

endfunction(add_pc_executable)

