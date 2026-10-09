/* Asynchronous physical peer wires over a native QEMU CharBackend.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PEER_H
#define HW_NIKON_PEER_H

#include "hw/core/qdev.h"
#include "hw/nikon/wire.h"

#define TYPE_NIKON_PEER "nikon-peer"
OBJECT_DECLARE_SIMPLE_TYPE(NikonPeer, NIKON_PEER)

/* Shared SPI owner implements these functions; transport never mutates SPI. */
typedef struct NikonSPIAsyncOps {
    bool (*request)(void *opaque, uint64_t token, uint32_t word, unsigned bits);
    void (*cancel)(void *opaque, uint64_t token);
} NikonSPIAsyncOps;

void nikon_spi_attach_async(DeviceState *, const NikonSPIAsyncOps *, void *);
void nikon_spi_detach_async(DeviceState *, void *owner);
void nikon_spi_async_ready(DeviceState *);
void nikon_spi_async_complete(DeviceState *, uint64_t token, uint32_t received);

/* I2C owner advances START/TX/RX/STOP only on an actual matching response. */
typedef struct NikonI2CAsyncOps {
    bool (*request)(void *, uint64_t token, NikonI2CPhase, uint8_t value);
    void (*cancel)(void *, uint64_t token);
} NikonI2CAsyncOps;

/* Binding errors leave controller state unchanged and report through errp. */
bool nikon_i2c_attach_async(DeviceState *, const NikonI2CAsyncOps *, void *,
                            Error **errp);
void nikon_i2c_detach_async(DeviceState *, void *owner);
void nikon_i2c_async_ready(DeviceState *);
void nikon_i2c_async_complete(DeviceState *, uint64_t token, uint32_t result);
/* GPIO owner reports a level only when a guest GPIO output actually drives. */
bool nikon_gpio_output(DeviceState *, unsigned pin, bool *level);

typedef struct NikonPeerRequest {
    uint64_t token;
    uint64_t time_ns;
    NikonPeerKind kind;
    uint32_t word;
    uint32_t line;
    unsigned bits;
} NikonPeerRequest;

/* Endpoint callbacks run in the BQL/main-loop context.  CLOCK may remain
 * pending while the real guest ISR arms its TX/RX state.  SELECT/GPIO are
 * physical levels, never acknowledgements of firmware processing.
 */
typedef struct NikonPeerEndpointOps {
    void (*request)(void *, const NikonPeerRequest *);
    void (*cancel)(void *, uint64_t token);
    void (*wire)(void *, NikonPeerKind kind, uint32_t line, bool level,
                 uint64_t time_ns);
} NikonPeerEndpointOps;

typedef void (*NikonPeerComplete)(void *, uint64_t token, uint32_t received);
typedef void (*NikonPeerState)(void *, bool connected);

/* Ops are copied; opaque pointers must outlive this device.  Clock requests
 * use one in-flight transaction per peer.  False means no accepted request;
 * no RX byte, completion IRQ, or guest acknowledgement is manufactured.
 */
void nikon_peer_endpoint(NikonPeer *, const NikonPeerEndpointOps *, void *);
/* Cancel an incoming clock without reply; remote master stays uncompleted. */
bool nikon_peer_endpoint_cancel(NikonPeer *, uint64_t token);
bool nikon_peer_state(NikonPeer *, NikonPeerState, void *);
bool nikon_peer_clock(NikonPeer *, uint64_t token, uint32_t word, unsigned bits,
                      NikonPeerComplete, void *);
bool nikon_peer_i2c(NikonPeer *, uint64_t token, NikonI2CPhase, uint8_t value,
                    NikonPeerComplete, void *);
void nikon_peer_cancel(NikonPeer *, uint64_t token);
bool nikon_peer_wire(NikonPeer *, NikonPeerKind, uint32_t line, bool level);
/* Reply only after the endpoint actually clocks its guest peripheral. */
bool nikon_peer_reply(NikonPeer *, uint64_t token, uint32_t received);

#endif
