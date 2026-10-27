/* Functional C1.70 readout-control to CMOS composition.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_READOUT_PIPELINE_H
#define HW_NIKON_READOUT_PIPELINE_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_READOUT_PIPELINE "nikon-readout-pipeline"

struct NikonCaptureInput;

/* Borrowed completed MAIN bytes; auxiliary completions never replace them. */
bool nikon_readout_pipeline_main_input(DeviceState *dev,
                                      struct NikonCaptureInput *input);

#endif
