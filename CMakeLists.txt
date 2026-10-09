cmake_minimum_required(VERSION 3.24)
project(Nightglass LANGUAGES NONE)

set(NATIVE_COMPONENT "qemu" CACHE STRING "Native execution provider: qemu or nds")
if(DEFINED NATIVE_CONFIGURED_COMPONENT AND
    NOT NATIVE_CONFIGURED_COMPONENT STREQUAL NATIVE_COMPONENT)
  message(FATAL_ERROR "Each native provider requires its own CMake build directory")
endif()
set(NATIVE_CONFIGURED_COMPONENT "${NATIVE_COMPONENT}" CACHE INTERNAL "Native source graph owner")
set(NATIVE_VIEW "${CMAKE_BINARY_DIR}/source")
find_package(Git REQUIRED)

function(native_provider directory)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${directory}" rev-parse --show-toplevel
    OUTPUT_VARIABLE root OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE status)
  if(NOT status STREQUAL "0" OR NOT root STREQUAL directory)
    message(FATAL_ERROR "Initialize the official dependency: ${directory}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${directory}" diff
    --quiet --ignore-submodules=untracked -- RESULT_VARIABLE status)
  if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Official dependency has tracked changes: ${directory}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${directory}" diff
    --cached --quiet --ignore-submodules=untracked HEAD -- RESULT_VARIABLE status)
  if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Official dependency has staged changes: ${directory}")
  endif()
endfunction()

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

# Write source text verbatim and preserve timestamps when its content is equal.
function(native_write destination content)
  get_filename_component(parent "${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
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

function(native_file path content)
  native_write("${NATIVE_VIEW}/${path}" "${content}")
endfunction()

if(NATIVE_COMPONENT STREQUAL "nds")
  include(cmake/ndsbuild.cmake)
  return()
elseif(NOT NATIVE_COMPONENT STREQUAL "qemu")
  message(FATAL_ERROR "Unknown native execution provider: ${NATIVE_COMPONENT}")
endif()

set(QEMU_UPSTREAM "${CMAKE_SOURCE_DIR}/qemu")
set(QEMU_VIEW "${NATIVE_VIEW}")
if(NOT EXISTS "${QEMU_UPSTREAM}/configure")
  message(FATAL_ERROR "Initialize dependencies: git submodule update --init --recursive")
endif()
native_provider("${QEMU_UPSTREAM}")
foreach(directory hw/nikon include/hw/nikon tests/qtest/nikon tests/unit/nikon)
  if(EXISTS "${QEMU_UPSTREAM}/${directory}")
    message(FATAL_ERROR "Project-owned ${directory} belongs outside the official qemu/ dependency")
  endif()
endforeach()

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

# Meson owns downloaded subprojects; refresh their declared native overlays too.
include(cmake/wrap.cmake)

foreach(directory hw/nikon include/hw/nikon tests/qtest/nikon tests/unit/nikon)
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/${directory}/*")
  foreach(path IN LISTS sources)
    get_filename_component(parent "${QEMU_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    native_link("${CMAKE_SOURCE_DIR}/${path}" "${QEMU_VIEW}/${path}")
  endforeach()
endforeach()

file(READ "${QEMU_UPSTREAM}/hw/Kconfig" content)
native_file("hw/Kconfig" "${content}\nsource nikon/Kconfig\n")
file(READ "${QEMU_UPSTREAM}/hw/meson.build" content)
native_file("hw/meson.build" "${content}\nsubdir('nikon')\n")
file(READ "${QEMU_UPSTREAM}/tests/qtest/meson.build" content)
native_file("tests/qtest/meson.build" "${content}\nsubdir('nikon')\n")
file(READ "${QEMU_UPSTREAM}/tests/unit/meson.build" content)
native_file("tests/unit/meson.build" "${content}\nsubdir('nikon')\n")
include(cmake/rx.cmake)
