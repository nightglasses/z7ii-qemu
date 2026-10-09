cmake_minimum_required(VERSION 3.24)
project(Nightglass LANGUAGES NONE)

set(QEMU_UPSTREAM "${CMAKE_SOURCE_DIR}/vendor/qemu")
set(QEMU_VIEW "${CMAKE_BINARY_DIR}/source")
if(NOT EXISTS "${QEMU_UPSTREAM}/configure")
  message(FATAL_ERROR "Initialize dependencies: git submodule update --init --recursive")
endif()

# QEMU provides no external machine source option. Compose its native source
# graph without changing the upstream checkout or copying its implementation.
# Directories remain real so Meson wraps never write into the submodule.
file(GLOB_RECURSE upstream CONFIGURE_DEPENDS LIST_DIRECTORIES true
  RELATIVE "${QEMU_UPSTREAM}" "${QEMU_UPSTREAM}/*" "${QEMU_UPSTREAM}/.*")
foreach(path IN LISTS upstream)
  if(path MATCHES "^\\.git(/|$)" OR
     path STREQUAL "hw/Kconfig" OR path STREQUAL "hw/meson.build" OR
     path STREQUAL "tests/qtest/meson.build")
    continue()
  endif()
  if(IS_DIRECTORY "${QEMU_UPSTREAM}/${path}")
    file(MAKE_DIRECTORY "${QEMU_VIEW}/${path}")
  else()
    get_filename_component(parent "${QEMU_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    if(NOT IS_SYMLINK "${QEMU_VIEW}/${path}")
      file(CREATE_LINK "${QEMU_UPSTREAM}/${path}" "${QEMU_VIEW}/${path}" SYMBOLIC)
    endif()
  endif()
endforeach()

foreach(directory hw/nikon include/hw/nikon tests/qtest/nikon)
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/${directory}/*")
  foreach(path IN LISTS sources)
    get_filename_component(parent "${QEMU_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    if(NOT IS_SYMLINK "${QEMU_VIEW}/${path}")
      file(CREATE_LINK "${CMAKE_SOURCE_DIR}/${path}" "${QEMU_VIEW}/${path}" SYMBOLIC)
    endif()
  endforeach()
endforeach()

file(READ "${QEMU_UPSTREAM}/hw/Kconfig" content)
file(CONFIGURE OUTPUT "${QEMU_VIEW}/hw/Kconfig"
  CONTENT "${content}\nsource nikon/Kconfig\n" @ONLY)
file(READ "${QEMU_UPSTREAM}/hw/meson.build" content)
file(CONFIGURE OUTPUT "${QEMU_VIEW}/hw/meson.build"
  CONTENT "${content}\nsubdir('nikon')\n" @ONLY)
file(READ "${QEMU_UPSTREAM}/tests/qtest/meson.build" content)
file(CONFIGURE OUTPUT "${QEMU_VIEW}/tests/qtest/meson.build"
  CONTENT "${content}\nsubdir('nikon')\n" @ONLY)
