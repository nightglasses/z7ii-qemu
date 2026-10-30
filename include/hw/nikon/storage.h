/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_STORAGE_H
#define HW_NIKON_STORAGE_H

#include "hw/pci/pci.h"
#include "system/block-backend.h"
#include "hw/core/sysbus.h"

#define TYPE_NIKON_STORAGE_HOST "nikon.storage"
OBJECT_DECLARE_SIMPLE_TYPE(NikonStorageHost, NIKON_STORAGE_HOST)

/* Source-local ports, not ECAM. SysBus IRQs are f2, f3, f4, f6 in this order.
 * The required memory link selects this CPU domain's DMA MemoryRegion before
 * realization. The optional drive property supplies real media; absent media
 * stays offline. GPIO "present" reflects insertion before PHY training and
 * survives link cancellation/reset. Hot removal is not implemented. A private
 * CPU domain must not link the MAS system memory.
 */
enum {
    NIKON_STORAGE_DBI,
    NIKON_STORAGE_CONFIG,
    NIKON_STORAGE_MEMORY,
    NIKON_STORAGE_PHY,
    NIKON_STORAGE_PLATFORM,
};

/* The caller owns the source-bound PCI host, DMA address space and IRQ route.
 * The returned device is owned by its bus. No register or IRQ address is chosen
 * here. Media contents and capacity come from the supplied BlockBackend.
 */
PCIDevice *nikon_storage_attach(PCIBus *bus, int devfn, BlockBackend *media,
                               const char *serial, Error **errp);

#endif
