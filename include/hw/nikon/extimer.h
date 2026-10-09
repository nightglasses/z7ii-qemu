/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_EXTIMER_H
#define HW_NIKON_EXTIMER_H

#include "hw/core/sysbus.h"

#define TYPE_NIKON_EX_TIMER "nikon-ex-timer"
OBJECT_DECLARE_SIMPLE_TYPE(NikonEXTimerState, NIKON_EX_TIMER)

/*
 * Clock input: clk. Four IRQ outputs: update, compare, trigger/COM, break.
 * With advanced=false all enabled events use output zero.
 * Named capture[4]/trigger/break inputs and output[4]/dma[7] outputs are real
 * electrical/event endpoints; no source signal is fabricated at reset.
 */
#endif
