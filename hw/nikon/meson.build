arm_common_ss.add(when: 'CONFIG_NIKON_ARM', if_true: files(
  'components/bundle.c',
  'components/bootshim.c',
  'components/vrupload.c',
  'devices/access.c',
  'devices/timestamp.c',
  'devices/usio.c',
  'devices/vrinput.c',
  'devices/vrstore.c',
  'peers/transport.c',
  'peers/update.c',
))
arm_common_ss.add(when: 'CONFIG_NIKON_MEMORY', if_true: files(
  'devices/audio.c',
  'devices/bridge.c',
  'devices/clock.c',
  'devices/command.c',
  'devices/completion.c',
  'devices/exiu.c',
  'devices/config.c',
  'devices/copy.c',
  'devices/counter.c',
  'devices/events.c',
  'devices/fill.c',
  'devices/flags.c',
  'devices/gate.c',
  'devices/gpio.c',
  'devices/hdmac.c',
  'devices/i2c.c',
  'devices/lanes.c',
  'devices/lookup.c',
  'devices/memc.c',
  'devices/mxic.c',
  'devices/notifier.c',
  'devices/pairs.c',
  'devices/parameters.c',
  'devices/readout.c',
  'devices/router.c',
  'devices/sampler.c',
  'devices/sdi.c',
  'devices/signal.c',
  'devices/shared.c',
  'devices/staging.c',
  'devices/status.c',
  'devices/spi.c',
  'devices/sysconfig.c',
  'devices/table.c',
  'devices/timing.c',
))
arm_common_ss.add(when: 'CONFIG_NIKON_SLA', if_true: files('machines/sla.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_EX', if_true: files(
  'machines/ex.c', 'components/exboot.c', 'devices/exio.c',
  'devices/extimer.c', 'devices/expower.c', 'devices/exrng.c',
  'devices/input.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_SENSOR', if_true: files('devices/sensor.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_CMOS', if_true: files('devices/cmos.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_STORAGE', if_true: files('devices/storage.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_Z7II', if_true: files(
  'machines/z7ii.c',
  'components/exconfig.c',
  'devices/dispatcher.c',
  'devices/banks.c',
  'devices/panel.c',
  'peers/i2c.c',
  'devices/eyesensor.c',
  'devices/acceleration.c',
  'devices/ipud.c',
  'devices/processing.c',
  'devices/processor.c',
  'devices/frame.c',
  'devices/readoutpipeline.c',
  'peers/spi.c',
  'devices/transfer.c',
  'devices/path.c',
  'devices/save.c',
))
if config_all_devices.has_key('CONFIG_NIKON_Z7II')
  arm_common_ss.add(when: 'CONFIG_NIKON_Z7II',
                    if_true: dependency('cairo', version: '>=1.16'))
endif

rx_ss.add(when: 'CONFIG_NIKON_VR', if_true: files(
  'machines/vr.c',
  'components/bundle.c',
  'components/vrupload.c',
  'devices/access.c',
  'devices/renesas.c',
  'devices/vr.c',
  'devices/vrinput.c',
  'devices/vrstore.c',
  'peers/transport.c',
  'peers/update.c',
  'peers/vr.c',
))
