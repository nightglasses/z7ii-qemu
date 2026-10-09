/* Host transport envelope. Nikon firmware bytes are carried unchanged.
 * This envelope is not a Nikon firmware packet or guest acknowledgement.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_WIRE_H
#define HW_NIKON_WIRE_H

#define NIKON_WIRE_SIZE 40
#define NIKON_WIRE_MAGIC 0x3150474eU /* Little-endian "NGP1". */

/* All multi-byte fields are little-endian. Reserved fields must be zero. */
#define NIKON_WIRE_TYPE 4       /* u8 */
#define NIKON_WIRE_BITS 5       /* u8, payload width; I2C uses 8 */
#define NIKON_WIRE_KIND 6       /* u8, NikonPeerKind */
#define NIKON_WIRE_RESERVED 7   /* u8 */
#define NIKON_WIRE_TOKEN 8      /* u64, nonzero for clock/cancel */
#define NIKON_WIRE_TIME 16      /* u64, sender virtual nanoseconds */
#define NIKON_WIRE_LINE 24      /* u32, wire identifier */
#define NIKON_WIRE_WORD 28      /* u32, clock word or physical wire level */
#define NIKON_WIRE_PADDING 32   /* u64, reserved */

typedef enum NikonPeerKind {
    NIKON_PEER_CLOCK = 1,
    NIKON_PEER_SELECT = 2,
    NIKON_PEER_GPIO = 3,
    NIKON_PEER_I2C = 4,
} NikonPeerKind;

typedef enum NikonI2CPhase {
    NIKON_I2C_START = 1,
    NIKON_I2C_WRITE = 2,
    NIKON_I2C_READ = 3,
    NIKON_I2C_STOP = 4,
    NIKON_I2C_NACK = 5,
} NikonI2CPhase;

/* START carries address[6:0] and receive[7]; WRITE carries a byte.
 * READ/STOP/NACK carry zero. Final READ is followed by explicit NACK.
 * Replies preserve the phase.
 * A READ reply is an actual byte or NAK; other replies are zero or NAK.
 */
#define NIKON_I2C_NAK 0x100U

/* Functional touch wires join source producer/consumer roles, not pin IDs. */
typedef enum NikonTouchWire {
    NIKON_TOUCH_NOTIFY = 1,
    NIKON_TOUCH_CONTROL = 2,
} NikonTouchWire;

enum NikonWireType {
    NIKON_WIRE_REQUEST = 1,
    NIKON_WIRE_REPLY = 2,
    NIKON_WIRE_CANCEL = 3,
    NIKON_WIRE_SIGNAL = 4,
};

#endif
