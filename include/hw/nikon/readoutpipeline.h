/* Functional C1.70 readout-control to CMOS composition.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_READOUT_PIPELINE_H
#define HW_NIKON_READOUT_PIPELINE_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_READOUT_PIPELINE "nikon-readout-pipeline"

/* Completed immutable input publication pulses input-ready; successful MAIN
 * publication also pulses main-input-ready. Neither is an acquisition request
 * or a downstream processing completion. One fresh automatic-rise admits at
 * most one eligible MAIN view and the already-owned auxiliary generation. */

struct NikonCaptureInput;

/* Borrowed completed MAIN bytes; auxiliary completions never replace them. */
bool nikon_readout_pipeline_main_input(DeviceState *dev,
                                      struct NikonCaptureInput *input);

#endif
