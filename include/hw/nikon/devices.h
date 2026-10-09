/* Nikon finite device boundary; machine mappings live in hw/nikon/machines/. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_DEVICES_H
#define HW_NIKON_DEVICES_H

#include "system/memory.h"
#include "hw/core/qdev.h"

#define TYPE_NIKON_FILL "nikon-fill-functional"
#define TYPE_NIKON_USIO "nikon-usio-functional"
#define TYPE_NIKON_SPI "nikon-spi-functional"
#define TYPE_NIKON_SYSCONFIG "nikon-sysconfig-functional"
#define TYPE_NIKON_GPIO "nikon-gpio-functional"
#define TYPE_NIKON_EXIU "nikon-exiu-functional"
#define TYPE_NIKON_HDMAC "nikon-hdmac-functional"
#define TYPE_NIKON_I2C "nikon-i2c-functional"
#define TYPE_NIKON_AUDIO "nikon-audio-functional"
#define TYPE_NIKON_TIMING "nikon-timing-functional"
#define TYPE_NIKON_COPY "nikon-copy-functional"
#define TYPE_NIKON_BRIDGE "nikon-bridge-functional"
#define TYPE_NIKON_SIGNAL "nikon-signal-functional"
#define TYPE_NIKON_IPUD "nikon-ipud-functional"
#define TYPE_NIKON_STAGED_CONFIGURATION "nikon-staged-configuration-functional"
#define TYPE_NIKON_EVENT_GATE "nikon-event-gate-functional"
#define TYPE_NIKON_PAIRED_EVENT_BANKS "nikon-paired-event-banks-functional"
#define TYPE_NIKON_DATA_SAMPLER "nikon-data-sampler-functional"
#define TYPE_NIKON_TABLE_SAMPLER "nikon-table-sampler-functional"
#define TYPE_NIKON_SHARED_PARAMETER_EVENTS "nikon-shared-parameter-events-functional"
#define TYPE_NIKON_PARAMETER_EVENTS "nikon-parameter-events-functional"
#define TYPE_NIKON_DUAL_EVENT_BANKS "nikon-dual-event-banks-functional"
#define TYPE_NIKON_BYTE_EVENT_ROUTER "nikon-byte-event-router-functional"
#define TYPE_NIKON_REQUEST_STATUS "nikon-request-status-functional"
#define TYPE_NIKON_SDI "nikon-sdi-functional"
#define TYPE_NIKON_MXIC "nikon-mxic-regions-functional"
#define TYPE_NIKON_MXIC_CONFIG "nikon-mxic-config-functional"

#define TYPE_NIKON_TIMESTAMP "nikon-timestamp-functional"
#define TYPE_NIKON_MEMC "nikon-memc-diagnostic"
#define TYPE_NIKON_CLOCK_GATES "nikon-clock-gates-diagnostic"
#define TYPE_NIKON_COMPLETION "nikon-completion-diagnostic"
#define TYPE_NIKON_PERIPHERAL_EVENT_ROUTER "nikon-peripheral-event-router"

MemTxResult nikon_memc_nor_access(DeviceState *dev, hwaddr address,
                                 unsigned size, bool write, uint64_t value);
void nikon_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason);
void nikon_mxic_attach_ram(DeviceState *dev, MemoryRegion *ram, hwaddr address);

#endif
