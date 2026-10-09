/* Finite external-device regions through the GNU simulator hardware bus.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "system.h"
#include "sim-core.h"
#include "sim-hw.h"
#include "hw-main.h"
#include "hw-tree.h"

struct nds32_boundary {
  struct nds32_boundary_ops ops;
  int bound;
  void *opaque;
  uint32_t base;
  uint32_t size;
};

static unsigned
boundary_read (struct hw *hw, void *buffer, int space, unsigned_word address,
               unsigned size)
{
  struct nds32_boundary *device = hw_data (hw);
  if (!device->bound || !device->ops.read || space != 0 ||
      address < device->base || size > device->size ||
      address - device->base > device->size - size)
    return 0;
  return device->ops.read (device->opaque, address, buffer, size);
}

static unsigned
boundary_write (struct hw *hw, const void *buffer, int space,
                unsigned_word address, unsigned size)
{
  struct nds32_boundary *device = hw_data (hw);
  if (!device->bound || !device->ops.write || space != 0 ||
      address < device->base || size > device->size ||
      address - device->base > device->size - size)
    return 0;
  return device->ops.write (device->opaque, address, buffer, size);
}

static void
boundary_finish (struct hw *hw)
{
  set_hw_data (hw, HW_ZALLOC (hw, struct nds32_boundary));
  set_hw_io_read_buffer (hw, boundary_read);
  set_hw_io_write_buffer (hw, boundary_write);
}

const struct hw_descriptor dv_boundary_descriptor[] = {
  { "boundary", boundary_finish },
  { NULL, NULL }
};

SIM_RC
nds32_system_attach (SIM_DESC sd, uint32_t address, uint32_t size,
                     const struct nds32_boundary_ops *ops, void *opaque)
{
  struct hw *hw;
  struct nds32_boundary *device;
  if (!STATE_CPU (sd, 0)->system_mode || !ops || size == 0 ||
      size - 1 > UINT32_MAX - address)
    return SIM_RC_FAIL;
  /* GNU's generic unit decoder uses strtoul(base0), not implicit hexadecimal.
     Without0x, addresses containinga..f become partial/colliding tree units;
     distinct finite regions would then reuse an already bound device. */
  hw = sim_hw_parse (sd, "/boundary@0x%x", address);
  hw_tree_finish (hw);
  device = hw_data (hw);
  if (device->bound)
    return SIM_RC_FAIL;
  device->ops = *ops;
  device->bound = 1;
  device->opaque = opaque;
  device->base = address;
  device->size = size;
  /* Device memory is never executable; unregistered holes stay unmapped. */
  sim_core_attach (sd, NULL, 0, access_read | access_write, 0, address, size,
                   0, hw, NULL);
  return SIM_RC_OK;
}
