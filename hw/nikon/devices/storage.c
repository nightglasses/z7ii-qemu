/*
 * Source-bound NVMe endpoint using QEMU's existing controller and block layer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nikon/storage.h"

PCIDevice *nikon_storage_attach(PCIBus *bus, int devfn, BlockBackend *media,
                               const char *serial, Error **errp)
{
    PCIDevice *controller;
    DeviceState *dev;

    if (!bus || !media || !serial || !serial[0]) {
        error_setg(errp, "NVMe storage requires a PCI bus, media and serial");
        return NULL;
    }

    controller = pci_new(devfn, "nvme");
    dev = DEVICE(controller);
    qdev_prop_set_string(dev, "serial", serial);
    if (!qdev_prop_set_drive_err(dev, "drive", media, errp)) {
        object_unref(OBJECT(controller));
        return NULL;
    }
    /* Keep the mature controller's namespace, queue, DMA, interrupt, reset,
     * cancellation and error machinery. The original driver uses queue IDs
     * 0, 1 and 2; upstream defaults support all of them without a new profile.
     */
    if (!pci_realize_and_unref(controller, bus, errp)) {
        return NULL;
    }
    return controller;
}
