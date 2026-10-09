if have_system and (config_all_devices.has_key('CONFIG_NIKON_Z7II') or
                    config_all_devices.has_key('CONFIG_NIKON_VR'))
  fixture = executable('nikon_bundle',
    files('bundle.c', '../../../hw/nikon/components/bundle.c'), genh,
    dependencies: [qemuutil])
  test('nikon_bundle', fixture, env: test_env, args: ['--tap', '-k'],
    protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])
endif

if have_system and (config_target_mak.has_key('arm-softmmu') and
                   config_all_devices.has_key('CONFIG_NIKON_Z7II'))
  foreach name : ['memory', 'copy', 'command', 'input', 'readout', 'lookup']
  fixture = executable('nikon_' + name + '_unit', files(name + '.c'), genh,
    nikon_system['sources'],
    objects: nikon_system['objects'],
    dependencies: nikon_system['dependencies'],
    c_args: nikon_system['c_args'],
    include_directories: nikon_system['include_directories'],
    link_args: nikon_system['link_args'],
    link_depends: nikon_system['link_depends'])
  test('nikon_' + name + '_unit', fixture, env: test_env, args: ['--tap', '-k'],
    protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])
  endforeach
endif
