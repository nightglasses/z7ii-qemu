nikon_targets = {}
nikon_arm_fixtures = {}
if config_all_devices.has_key('CONFIG_NIKON_Z7II')
  nikon_arm_fixtures += {
    'nikon': files('devices.c', 'completion.c', 'bridge.c', 'transfer.c',
                   'sampling.c', 'image.c', 'sdi.c', 'spi.c', 'processing.c',
                   'config.c', 'i2c.c', 'pressure.c'),
    'nikon_timer': files('timer.c'),
    'nikon_dma': files('dma.c'),
    'nikon_memory': files('memory.c'),
    'nikon_usio': files('usio.c'),
  }
endif
if config_all_devices.has_key('CONFIG_NIKON_MEMORY')
  nikon_arm_fixtures += {
    'nikon_fill': files('fill.c'),
    'nikon_hdmac': files('hdmac.c'),
  }
endif
if config_all_devices.has_key('CONFIG_NIKON_SLA')
  nikon_arm_fixtures += {
    'nikon_sla': files('sla.c', 'events.c', 'notifier.c', 'counter.c', 'signal.c',
                       'staging.c'),
  }
endif
foreach profile : ['EX', 'SENSOR']
  if config_all_devices.has_key('CONFIG_NIKON_' + profile)
    name = profile.to_lower()
    nikon_arm_fixtures += {'nikon_' + name: files(name + '.c')}
  endif
endforeach
if config_all_devices.has_key('CONFIG_NIKON_STORAGE') and config_all_devices.has_key('CONFIG_ARM_VIRT')
  nikon_arm_fixtures += {'nikon_storage': files('storage.c')}
endif
if nikon_arm_fixtures.keys().length() > 0
  nikon_targets += {'arm': nikon_arm_fixtures}
endif
if config_all_devices.has_key('CONFIG_NIKON_VR')
  nikon_targets += {'rx': {
    'nikon_vr': files('vr.c'),
    'nikon_peripherals': files('peripherals.c'),
  }}
endif
foreach arch, fixtures : nikon_targets
  nikon_emulator = emulators['qemu-system-' + arch]
  nikon_env = environment()
  nikon_env.set('QTEST_QEMU_BINARY', './qemu-system-' + arch)
  foreach name, sources : fixtures
    fixture = executable(name, sources,
      dependencies: [qemuutil, qos],
      include_directories: include_directories('..'))
    test(name, fixture,
      depends: [roms, nikon_emulator, emulator_modules], env: nikon_env,
      args: ['--tap', '-k'], protocol: 'tap', timeout: 60,
      suite: ['nikon', 'qtest', 'qtest-' + arch])
    if name == 'nikon'
      benchmark(name, fixture,
        depends: [roms, nikon_emulator, emulator_modules], env: nikon_env,
        args: ['--tap', '-m', 'perf', '-p', '/arm/nikon/bench/dma'],
        protocol: 'tap', timeout: 60, suite: ['nikon'])
    endif
  endforeach
endforeach
