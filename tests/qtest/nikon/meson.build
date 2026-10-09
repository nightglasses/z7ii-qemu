if config_all_devices.has_key('CONFIG_NIKON_Z7II')
  nikon_emulator = emulators['qemu-system-arm']
  nikon_env = environment()
  nikon_env.set('QTEST_QEMU_BINARY', './qemu-system-arm')
  nikon_sources = {
    'nikon': files('devices.c', 'completion.c', 'bridge.c', 'transfer.c'),
    'nikon_timer': files('timer.c'),
    'nikon_dma': files('dma.c'),
    'nikon_memory': files('memory.c'),
  }
  foreach name, sources : nikon_sources
    fixture = executable(name, sources,
      dependencies: [qemuutil, qos],
      include_directories: include_directories('..'))
    test(name, fixture,
      depends: [roms, nikon_emulator, emulator_modules], env: nikon_env,
      args: ['--tap', '-k'], protocol: 'tap', timeout: 60,
      suite: ['nikon', 'qtest', 'qtest-arm'])
    if name == 'nikon'
      benchmark(name, fixture,
        depends: [roms, nikon_emulator, emulator_modules], env: nikon_env,
        args: ['--tap', '-m', 'perf', '-p', '/arm/nikon/bench/dma'],
        protocol: 'tap', timeout: 60, suite: ['nikon'])
    endif
  endforeach
endif
