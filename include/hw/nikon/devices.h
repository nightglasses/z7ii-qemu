/* Nikon finite device boundary; machine mappings live in hw/nikon/machines/. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_DEVICES_H
#define HW_NIKON_DEVICES_H

#include "qemu/osdep.h" /* IWYU pragma: keep */
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "qemu/typedefs.h"
#include "hw/core/qdev.h"
#include "hw/i2c/i2c.h"

#define TYPE_NIKON_FILL "nikon-fill-functional"
#define TYPE_NIKON_USIO "nikon-usio-functional"
#define TYPE_NIKON_SPI "nikon-spi-functional"
#define TYPE_NIKON_SYSCONFIG "nikon-sysconfig-functional"
#define TYPE_NIKON_GPIO "nikon-gpio-functional"
#define TYPE_NIKON_CCOMP "nikon-ccomp-functional"
#define TYPE_NIKON_CAPTURE_ENDPOINTS "nikon-capture-endpoints-functional"
#define TYPE_NIKON_CAPTURE_BANKS "nikon-capture-banks-functional"
#define TYPE_NIKON_CAPTURE_GROUP "nikon-capture-group-functional"
#define TYPE_NIKON_CAPTURE_PARAMETERS "nikon-capture-parameters-functional"
#define TYPE_NIKON_EXIU "nikon-exiu-functional"
#define TYPE_NIKON_EX_RNG "nikon-ex-rng-functional"
#define TYPE_NIKON_HDMAC "nikon-hdmac-functional"
#define TYPE_NIKON_I2C "nikon-i2c-functional"
#define TYPE_NIKON_EYE_SENSOR "nikon-eye-sensor-functional"
#define TYPE_NIKON_ACCELERATION "nikon-acceleration-functional"
#define TYPE_NIKON_AUDIO "nikon-audio-functional"
#define TYPE_NIKON_TIMING "nikon-timing-functional"
#define TYPE_NIKON_COPY "nikon-copy-functional"
#define TYPE_NIKON_TRANSFER "nikon-transfer-functional"
#define TYPE_NIKON_PROCESSOR "nikon-processor-control-functional"
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
#define TYPE_NIKON_PANEL "nikon-panel-functional"
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
MemTxResult nikon_memc_nor_backend_access(DeviceState *dev, hwaddr address,
                                         unsigned size, bool write,
                                         uint64_t value);
void nikon_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason);

/*
 * A guest poll loop whose modelled completion can never happen used to be
 * silent: the firmware spins on a status bit forever.  Devices call this from
 * the status read; after `limit` consecutive polls that observed no state
 * change the wait is reported as an uncovered stall and the VM stops.
 */
bool nikon_wait_stall(bool *reported, hwaddr addr, unsigned size,
                      uint64_t value, unsigned limit, const char *reason);
void nikon_mxic_attach_ram(DeviceState *dev, MemoryRegion *ram, hwaddr address);
/* Return the controller's real upstream I2C bus after realization. */
I2CBus *nikon_i2c_bus(DeviceState *dev);
/*
 * Borrowed peer address space must outlive bridge accesses. NULL disconnects.
 * SysBus regions 1/2 expose bank0/1 local windows; they are not board mappings.
 * Named peer-start/peer-configuration outputs pulse at actual completion.
 * A connected peer's peer-control inputs 0/1 latch raw IRQ bits 0/5.
 */
void nikon_bridge_attach_peer(DeviceState *dev, unsigned bank,
                              AddressSpace *peer);
/* Borrowed symmetric pair; lifetime exceeds all notification accesses.
 * NULL disconnects without an edge. The pair reflects each endpoint's
 * write-one notification word and drives the peer's enabled IRQ15 cause.
 */
void nikon_bridge_connect_notifications(DeviceState *dev, DeviceState *peer);

/* Firmware-side descriptor context, kept separate from COPY MMIO state. */
void nikon_copy_set_descriptor_context(DeviceState *dev, const char *owner,
                                       uint32_t producer_pc,
                                       uint32_t producer_lr,
                                       uint64_t control_destination,
                                       uint32_t record);
void nikon_copy_clear_descriptor_context(DeviceState *dev);

#endif
