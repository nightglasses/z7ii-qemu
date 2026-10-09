/* Finite two-lane write-only configuration interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_LANES_H
#define HW_NIKON_LANES_H

#define TYPE_NIKON_LANES "nikon-lanes-functional"

/* Two separate 20-byte SysBus MMIO regions, one per original lane. There
 * is no IRQ, ready flag or data producer associated with the recovered
 * configuration writer. Absolute placement belongs to the machine.
 */

#endif
