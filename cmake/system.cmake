# Reuse the emulator's complete typed ARM graph for an in-process system unit.
file(READ "${QEMU_UPSTREAM}/meson.build" content)
set(seam "  foreach exe: execs\n")
string(REPLACE "${seam}" "" without "${content}")
string(LENGTH "${content}" original)
string(LENGTH "${without}" remaining)
string(LENGTH "${seam}" expected)
math(EXPR removed "${original} - ${remaining}")
if(NOT removed EQUAL expected)
  message(FATAL_ERROR "Fixed QEMU target loop does not match its unique system unit seam")
endif()
set(registration [==[
  if target == 'arm-softmmu' and config_all_devices.has_key('CONFIG_NIKON_Z7II')
    nikon_system = {
      'sources': main_rs,
      'objects': lib.extract_all_objects(recursive: true),
      'dependencies': arch_deps + [sdl, target_stubs],
      'c_args': c_args,
      'include_directories': target_inc,
      'link_args': link_args,
      'link_depends': [block_syms, qemu_syms],
    }
  endif
]==])
string(REPLACE "${seam}" "${registration}\n${seam}" content "${content}")
native_file("meson.build" "${content}")
