cmake_minimum_required(VERSION 3.24)
project(Nightglass LANGUAGES NONE)

set(QEMU_UPSTREAM "${CMAKE_SOURCE_DIR}/qemu")
set(QEMU_VIEW "${CMAKE_BINARY_DIR}/source")
if(NOT EXISTS "${QEMU_UPSTREAM}/configure")
  message(FATAL_ERROR "Initialize dependencies: git submodule update --init --recursive")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${QEMU_UPSTREAM}" rev-parse --show-toplevel
  OUTPUT_VARIABLE upstream_root OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE git_status)
if(NOT git_status STREQUAL "0" OR NOT upstream_root STREQUAL QEMU_UPSTREAM)
  message(FATAL_ERROR "qemu/ must be the initialized official Git submodule")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${QEMU_UPSTREAM}" diff
  --quiet --ignore-submodules=untracked -- RESULT_VARIABLE git_status)
if(NOT git_status STREQUAL "0")
  message(FATAL_ERROR "Official qemu/ has tracked changes; maintain Nikon source in the parent repository")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${QEMU_UPSTREAM}" diff
  --cached --quiet --ignore-submodules=untracked HEAD -- RESULT_VARIABLE git_status)
if(NOT git_status STREQUAL "0")
  message(FATAL_ERROR "Official qemu/ has staged changes; maintain Nikon source in the parent repository")
endif()
foreach(directory hw/nikon include/hw/nikon tests/qtest/nikon tests/unit/nikon)
  if(EXISTS "${QEMU_UPSTREAM}/${directory}")
    message(FATAL_ERROR "Project-owned ${directory} belongs outside the official qemu/ dependency")
  endif()
endforeach()

function(native_link source destination)
  if(IS_SYMLINK "${destination}")
    file(READ_SYMLINK "${destination}" current)
    if(current STREQUAL source)
      return()
    endif()
    file(REMOVE "${destination}")
  elseif(EXISTS "${destination}")
    message(FATAL_ERROR "Native source view contains an unmanaged file: ${destination}")
  endif()
  file(CREATE_LINK "${source}" "${destination}" SYMBOLIC)
endfunction()

# QEMU provides no external machine source option. Compose its native source
# graph without changing the upstream checkout or copying its implementation.
# Directories remain real so Meson wraps never write into the submodule.
file(GLOB_RECURSE upstream CONFIGURE_DEPENDS LIST_DIRECTORIES true
  RELATIVE "${QEMU_UPSTREAM}" "${QEMU_UPSTREAM}/*" "${QEMU_UPSTREAM}/.*")
foreach(path IN LISTS upstream)
  if(path MATCHES "^\\.git(/|$)" OR
     path STREQUAL "hw/Kconfig" OR path STREQUAL "hw/meson.build" OR
     path STREQUAL "tests/qtest/meson.build" OR
     path STREQUAL "tests/unit/meson.build" OR
     path STREQUAL "target/rx/cpu.c" OR path STREQUAL "target/rx/cpu.h" OR
     path STREQUAL "target/rx/helper.c" OR path STREQUAL "target/rx/translate.c")
    continue()
  endif()
  if(IS_DIRECTORY "${QEMU_UPSTREAM}/${path}")
    file(MAKE_DIRECTORY "${QEMU_VIEW}/${path}")
  else()
    get_filename_component(parent "${QEMU_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    native_link("${QEMU_UPSTREAM}/${path}" "${QEMU_VIEW}/${path}")
  endif()
endforeach()

foreach(directory hw/nikon include/hw/nikon tests/qtest/nikon tests/unit/nikon)
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/${directory}/*")
  foreach(path IN LISTS sources)
    get_filename_component(parent "${QEMU_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    native_link("${CMAKE_SOURCE_DIR}/${path}" "${QEMU_VIEW}/${path}")
  endforeach()
endforeach()

# Write text verbatim: Meson's @0@ placeholders are not CMake substitutions.
function(native_file path content)
  set(destination "${QEMU_VIEW}/${path}")
  if(IS_SYMLINK "${destination}")
    file(REMOVE "${destination}")
  endif()
  if(EXISTS "${destination}")
    file(READ "${destination}" previous)
    if(previous STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${destination}" "${content}")
endfunction()

file(READ "${QEMU_UPSTREAM}/hw/Kconfig" content)
native_file("hw/Kconfig" "${content}\nsource nikon/Kconfig\n")
file(READ "${QEMU_UPSTREAM}/hw/meson.build" content)
native_file("hw/meson.build" "${content}\nsubdir('nikon')\n")
file(READ "${QEMU_UPSTREAM}/tests/qtest/meson.build" content)
native_file("tests/qtest/meson.build" "${content}\nsubdir('nikon')\n")
file(READ "${QEMU_UPSTREAM}/tests/unit/meson.build" content)
native_file("tests/unit/meson.build" "${content}\nsubdir('nikon')\n")
include(cmake/rx.cmake)
