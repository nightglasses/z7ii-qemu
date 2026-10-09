arm_common_ss.add(when: 'CONFIG_NIKON_ARM', if_true: files(
  'components/bundle.c',
  'devices/access.c',
  'devices/timestamp.c',
  'devices/usio.c',
  'peers/transport.c',
))
arm_common_ss.add(when: 'CONFIG_NIKON_MEMORY', if_true: files(
  'devices/bridge.c',
  'devices/clock.c',
  'devices/completion.c',
  'devices/config.c',
  'devices/fill.c',
  'devices/gpio.c',
  'devices/memc.c',
  'devices/mxic.c',
  'devices/sdi.c',
  'devices/sysconfig.c',
))
arm_common_ss.add(when: 'CONFIG_NIKON_SLA', if_true: files('machines/sla.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_EX', if_true: files(
  'machines/ex.c', 'devices/exio.c', 'devices/extimer.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_SENSOR', if_true: files('devices/sensor.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_STORAGE', if_true: files('devices/storage.c'))
arm_common_ss.add(when: 'CONFIG_NIKON_Z7II', if_true: files(
  'machines/z7ii.c',
  'devices/audio.c',
  'devices/dispatcher.c',
  'devices/copy.c',
  'devices/sampler.c',
  'devices/banks.c',
  'devices/gate.c',
  'devices/exiu.c',
  'devices/hdmac.c',
  'devices/i2c.c',
  'devices/ipud.c',
  'devices/pairs.c',
  'devices/router.c',
  'devices/parameters.c',
  'devices/processing.c',
  'devices/status.c',
  'devices/shared.c',
  'devices/signal.c',
  'devices/spi.c',
  'peers/spi.c',
  'devices/staging.c',
  'devices/table.c',
  'devices/timing.c',
  'devices/transfer.c',
))

rx_ss.add(when: 'CONFIG_NIKON_VR', if_true: files(
  'machines/vr.c',
  'components/bundle.c',
  'devices/access.c',
  'devices/renesas.c',
  'devices/vr.c',
  'peers/transport.c',
  'peers/vr.c',
))
