fixture = executable('nikon_battery_unit',
  files('battery.c', '../../../tools/nikon/battery.c'), genh,
  dependencies: [qemuutil])
test('nikon_battery_unit', fixture, env: test_env,
  args: ['--tap', '-k', nikon_battery_tool.full_path()],
  depends: nikon_battery_tool, protocol: 'tap', timeout: 30,
  suite: ['unit', 'nikon'])

fixture = executable('nikon_batterypeer_unit',
  files('batterypeer.c', '../../../tools/nikon/battery.c',
        '../../../tools/nikon/batterypeer.c'), genh,
  dependencies: [qemuutil])
test('nikon_batterypeer_unit', fixture, env: test_env,
  args: ['--tap', '-k', nikon_battery_tool.full_path()],
  depends: nikon_battery_tool, protocol: 'tap', timeout: 30,
  suite: ['unit', 'nikon'])

fixture = executable('nikon_module_unit',
  files('module.c', '../../../tools/nikon/module.c'), genh,
  dependencies: [qemuutil])
test('nikon_module_unit', fixture, env: test_env,
  args: ['--tap', '-k', nikon_module_tool.full_path()],
  depends: nikon_module_tool, protocol: 'tap', timeout: 30,
  suite: ['unit', 'nikon'])

fixture = executable('nikon_gatepacket_unit',
  files('gatepacket.c', '../../../hw/nikon/devices/gatepacket.c'), genh,
  dependencies: [qemuutil])
test('nikon_gatepacket_unit', fixture, env: test_env, args: ['--tap', '-k'],
  protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])

fixture = executable('nikon_pairedpixels_unit',
  files('pairedpixels.c', '../../../hw/nikon/devices/pairedpixels.c'), genh,
  dependencies: [qemuutil])
test('nikon_pairedpixels_unit', fixture, env: test_env, args: ['--tap', '-k'],
  protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])

fixture = executable('nikon_gatepixels_unit',
  files('gatepixels.c', '../../../hw/nikon/devices/gatepixels.c'), genh,
  dependencies: [qemuutil])
test('nikon_gatepixels_unit', fixture, env: test_env, args: ['--tap', '-k'],
  protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])

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
  foreach name : ['memory', 'copy', 'transfer', 'bridge', 'dispatcher',
                  'command', 'input', 'readout', 'panel',
                  'lookup', 'exreset', 'exlevels', 'save', 'processor', 'spi',
                  'acceleration', 'lens', 'gatework']
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

if have_system and (config_target_mak.has_key('arm-softmmu') and
                   config_all_devices.has_key('CONFIG_NIKON_Z7II'))
  fixture = executable('nikon_exboot_unit',
    files('exboot.c', '../../../hw/nikon/components/exboot.c'), genh,
    dependencies: [qemuutil])
  test('nikon_exboot_unit', fixture, env: test_env, args: ['--tap', '-k'],
    protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])

  fixture = executable('nikon_exconfig_unit',
    files('exconfig.c', '../../../hw/nikon/components/exconfig.c'), genh,
    dependencies: [qemuutil])
  test('nikon_exconfig_unit', fixture, env: test_env, args: ['--tap', '-k'],
    protocol: 'tap', timeout: 30, suite: ['unit', 'nikon'])
endif
