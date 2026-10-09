/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_STORAGE_H
#define HW_NIKON_STORAGE_H

#include "hw/pci/pci.h"
#include "system/block-backend.h"

/* The caller owns the source-bound PCI host, DMA address space and IRQ route.
 * The returned device is owned by its bus. No register or IRQ address is chosen
 * here. Media contents and capacity come from the supplied BlockBackend.
 */
PCIDevice *nikon_storage_attach(PCIBus *bus, int devfn, BlockBackend *media,
                               const char *serial, Error **errp);

#endif
