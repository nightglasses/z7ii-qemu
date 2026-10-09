/* Source-bound configuration and result readout protocol.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_READOUT_H
#define HW_NIKON_READOUT_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_READOUT "nikon-readout"
#define NIKON_READOUT_WORDS 40

/* Called under BQL by a real producer. A zero generation means no request.
 * Completion rejects stale/cancelled generations and unsupported event bits.
 * Results are copied atomically and retained across the ISR's next request.
 * The model does not calculate measurements or synthesize image data.
 */
uint64_t nikon_results_generation(DeviceState *dev);
bool nikon_results_publish(DeviceState *dev, uint64_t generation,
                          uint32_t events,
                          const uint32_t first[NIKON_READOUT_WORDS],
                          const uint32_t second[NIKON_READOUT_WORDS]);

/* External reset/state producer supplies the observed bit0 of register +4.
 * Guest reset writes never manufacture its completion.
 */
void nikon_results_state(DeviceState *dev, bool state);

#endif
