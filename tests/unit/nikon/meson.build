if have_system and (config_all_devices.has_key('CONFIG_NIKON_Z7II') or
                    config_all_devices.has_key('CONFIG_NIKON_VR'))
  fixture = executable('nikon_bundle',
    files('bundle.c', '../../../hw/nikon/components/bundle.c'), genh,
    dependencies: [qemuutil])
  test('nikon_bundle', fixture, env: test_env, args: ['--tap', '-k'],
    protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])
endif
