/*
 * Z7II functional SDI/PHY/DDR bring-up protocol.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IISDIState, Z7II_SDI)

typedef struct Z7IISDIChannel {
    Z7IISDIState *owner;
    QEMUTimer *timer;
    unsigned index;
    uint32_t request;
    bool reset;
    bool pending;
    bool done;
    bool initialized;
} Z7IISDIChannel;

/* Official Ghidra getPcode/SymbolicPropogator whole-wrapper observations,
 * validated against original bytes in research commit 6a6b721.
 * Configuration profiles include retry values.
 * Values are applied configuration, not firmware instruction/data bytes.
 * Unlisted offsets and values remain unsupported. */
static const struct {
    uint32_t offset;
    uint32_t values[2][3];
    unsigned count[2];
} config_profile[] = {
    { 0x100, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x10c, { { 0x303 },
                  { 0x303 } },
      { 1, 1 } },
    { 0x1004, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x1008, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x100c, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x1010, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x1014, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x1020, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x1024, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x1028, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x102c, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x1030, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x1038, { { 0x1010001 },
                  { 0x1010001 } },
      { 1, 1 } },
    { 0x2408, { { 0x1f8d },
                  { 0x1f8d } },
      { 1, 1 } },
    { 0x2414, { { 0x553b },
                  { 0x553b } },
      { 1, 1 } },
    { 0x3404, { { 0x43 },
                  { 0x43 } },
      { 1, 1 } },
    { 0x3408, { { 0x1a },
                  { 0x1a } },
      { 1, 1 } },
    { 0x340c, { { 0x2 },
                  { 0x2 } },
      { 1, 1 } },
    { 0x3500, { { 0x2030101 },
                  { 0x2030101 } },
      { 1, 1 } },
    { 0x3504, { { 0x2030101 },
                  { 0x2030101 } },
      { 1, 1 } },
    { 0x3508, { { 0x30000000 },
                  { 0x30000000 } },
      { 1, 1 } },
    { 0x350c, { { 0x30000000 },
                  { 0x30000000 } },
      { 1, 1 } },
    { 0x3510, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x3514, { { 0x30000000 },
                  { 0x30000000 } },
      { 1, 1 } },
    { 0x3600, { { 0xf9f },
                  { 0xf9f } },
      { 1, 1 } },
    { 0x3604, { { 0x13881 },
                  { 0x13881 } },
      { 1, 1 } },
    { 0x360c, { { 0x18f },
                  { 0x18f } },
      { 1, 1 } },
    { 0x3610, { { 0x40404 },
                  { 0x40404 } },
      { 1, 1 } },
    { 0x3614, { { 0x13031007 },
                  { 0x13031007 } },
      { 1, 1 } },
    { 0x3618, { { 0x807 },
                  { 0x807 } },
      { 1, 1 } },
    { 0x361c, { { 0x20b0b07 },
                  { 0x20b0b07 } },
      { 1, 1 } },
    { 0x3620, { { 0x30a0409 },
                  { 0x30a0409 } },
      { 1, 1 } },
    { 0x3624, { { 0x53 },
                  { 0x53 } },
      { 1, 1 } },
    { 0x3628, { { 0x8f0023 },
                  { 0x8f0023 } },
      { 1, 1 } },
    { 0x362c, { { 0x3000202 },
                  { 0x3000202 } },
      { 1, 1 } },
    { 0x3630, { { 0x50059 },
                  { 0x50059 } },
      { 1, 1 } },
    { 0x3634, { { 0xa0a01 },
                  { 0xa0a01 } },
      { 1, 1 } },
    { 0x3700, { { 0xd },
                  { 0xd } },
      { 1, 1 } },
    { 0x3708, { { 0x90003 },
                  { 0x90003 } },
      { 1, 1 } },
    { 0x370c, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x3800, { { 0x618 },
                  { 0x618 } },
      { 1, 1 } },
    { 0x3804, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x3808, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x3900, { { 0x9896800 },
                  { 0x9896800 } },
      { 1, 1 } },
    { 0x3904, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x3a00, { { 0x30d4000 },
                  { 0x30d4000 } },
      { 1, 1 } },
    { 0x3a04, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x3b00, { { 0x7 },
                  { 0x7 } },
      { 1, 1 } },
    { 0x3b04, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x4400, { { 0x3 },
                  { 0x3 } },
      { 1, 1 } },
    { 0x4404, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x4408, { { 0x44444 },
                  { 0x44444 } },
      { 1, 1 } },
    { 0x8040, { { 0x40004 },
                  { 0x40004 } },
      { 1, 1 } },
    { 0x9040, { { 0x40004 },
                  { 0x40004 } },
      { 1, 1 } },
    { 0xa040, { { 0x40004 },
                  { 0x40004 } },
      { 1, 1 } },
    { 0xb040, { { 0x40004 },
                  { 0x40004 } },
      { 1, 1 } },
    { 0xc040, { { 0x40004 },
                  { 0x40004 } },
      { 1, 1 } },
    { 0xe400, { { 0x2 },
                  { 0x2 } },
      { 1, 1 } },
    { 0xe600, { { 0x2 },
                  { 0x2 } },
      { 1, 1 } },
    { 0xe604, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0xe608, { { 0x40 },
                  { 0x40 } },
      { 1, 1 } },
    { 0xe700, { { 0x2 },
                  { 0x2 } },
      { 1, 1 } },
    { 0xe704, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0xe708, { { 0x40 },
                  { 0x40 } },
      { 1, 1 } },
    { 0xe70c, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0xe710, { { 0x10000000 },
                  { 0x10000000 } },
      { 1, 1 } },
    { 0x10010, { { 0x5 },
                  { 0x5 } },
      { 1, 1 } },
    { 0x10014, { { 0x8 },
                  { 0x8 } },
      { 1, 1 } },
    { 0x10018, { { 0x9 },
                  { 0x9 } },
      { 1, 1 } },
    { 0x1001c, { { 0x7 },
                  { 0x7 } },
      { 1, 1 } },
    { 0x10020, { { 0x3 },
                  { 0x3 } },
      { 1, 1 } },
    { 0x10024, { { 0x19061200 },
                  { 0x19061200 } },
      { 1, 1 } },
    { 0x10030, { { 0x66606 },
                  { 0x66606 } },
      { 1, 1 } },
    { 0x10034, { { 0x3713 },
                  { 0x3713 } },
      { 1, 1 } },
    { 0x10040, { { 0x30 },
                  { 0x30 } },
      { 1, 1 } },
    { 0x10044, { { 0x4664 },
                  { 0x4664 } },
      { 1, 1 } },
    { 0x10048, { { 0x330026 },
                  { 0x330026 } },
      { 1, 1 } },
    { 0x10050, { { 0x26 },
                  { 0x26 } },
      { 1, 1 } },
    { 0x10054, { { 0x9d084 },
                  { 0xaa092 } },
      { 1, 1 } },
    { 0x10058, { { 0xa0086 },
                  { 0xae094 } },
      { 1, 1 } },
    { 0x1005c, { { 0x9e086 },
                  { 0xac092 } },
      { 1, 1 } },
    { 0x10060, { { 0x9f085 },
                  { 0xb0096 } },
      { 1, 1 } },
    { 0x10074, { { 0x16030 },
                  { 0x1a030 } },
      { 1, 1 } },
    { 0x10078, { { 0x14030 },
                  { 0x17030 } },
      { 1, 1 } },
    { 0x1007c, { { 0x14030 },
                  { 0x15030 } },
      { 1, 1 } },
    { 0x10080, { { 0x16030 },
                  { 0x18030 } },
      { 1, 1 } },
    { 0x10094, { { 0x800 },
                  { 0x800 } },
      { 1, 1 } },
    { 0x10098, { { 0x800 },
                  { 0x800 } },
      { 1, 1 } },
    { 0x1009c, { { 0x800 },
                  { 0x800 } },
      { 1, 1 } },
    { 0x100a0, { { 0x800 },
                  { 0x800 } },
      { 1, 1 } },
    { 0x100b8, { { 0x3 },
                  { 0x3 } },
      { 1, 1 } },
    { 0x100e4, { { 0x89019, 0xd1019, 0xd1061 },
                  { 0x96026, 0xde026, 0xde06e } },
      { 3, 3 } },
    { 0x100e8, { { 0x89019, 0xd1019, 0xd1061 },
                  { 0x98028, 0xe0028, 0xe0070 } },
      { 3, 3 } },
    { 0x100ec, { { 0x8c01c, 0xd401c, 0xd4064 },
                  { 0x97027, 0xdf027, 0xdf06f } },
      { 3, 3 } },
    { 0x100f0, { { 0x88018, 0xd0018, 0xd0060 },
                  { 0x9a02a, 0xe202a, 0xe2072 } },
      { 3, 3 } },
    { 0x10128, { { 0x10016 },
                  { 0x1001a } },
      { 1, 1 } },
    { 0x1012c, { { 0x10014 },
                  { 0x10017 } },
      { 1, 1 } },
    { 0x10130, { { 0x10014 },
                  { 0x10015 } },
      { 1, 1 } },
    { 0x10134, { { 0x10016 },
                  { 0x10018 } },
      { 1, 1 } },
    { 0x10148, { { 0x10020 },
                  { 0x10020 } },
      { 1, 1 } },
    { 0x1014c, { { 0x10020 },
                  { 0x10020 } },
      { 1, 1 } },
    { 0x10150, { { 0x10020 },
                  { 0x10020 } },
      { 1, 1 } },
    { 0x10154, { { 0x10020 },
                  { 0x10020 } },
      { 1, 1 } },
    { 0x10820, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
    { 0x10824, { { 0x1 },
                  { 0x1 } },
      { 1, 1 } },
    { 0x10828, { { 0x43 },
                  { 0x43 } },
      { 1, 1 } },
    { 0x1082c, { { 0x1a },
                  { 0x1a } },
      { 1, 1 } },
    { 0x10840, { { 0x4b },
                  { 0x4b } },
      { 1, 1 } },
    { 0x10850, { { 0x10050 },
                  { 0x10050 } },
      { 1, 1 } },
    { 0x10910, { { 0x0 },
                  { 0x0 } },
      { 1, 1 } },
};

/* PHY, DDR init, five-mode training, arbitration, fifteen ports, final interface.
 * The port topology is the original software interface, not a silicon name. */
static const struct {
    uint32_t reset;
    uint32_t request;
    uint32_t status;
} channel_map[] = {
    { 0x2000, 0x2100, 0x2300 },
    { 0x3000, 0x3100, 0x3300 },
    { UINT32_MAX, 0x3118, 0x3318 },
    { 0x4000, 0x4100, 0x4300 },
    { 0x8000, 0x8010, 0x8030 }, { 0x8800, 0x8810, 0x8830 },
    { 0x8c00, 0x8c10, 0x8c30 }, { 0x9000, 0x9010, 0x9030 },
    { 0x9800, 0x9810, 0x9830 }, { 0x9c00, 0x9c10, 0x9c30 },
    { 0xa000, 0xa010, 0xa030 }, { 0xa800, 0xa810, 0xa830 },
    { 0xac00, 0xac10, 0xac30 }, { 0xb000, 0xb010, 0xb030 },
    { 0xb800, 0xb810, 0xb830 }, { 0xbc00, 0xbc10, 0xbc30 },
    { 0xc000, 0xc010, 0xc030 }, { 0xc800, 0xc810, 0xc830 },
    { 0xcc00, 0xcc10, 0xcc30 }, { 0xe000, 0xe100, 0xe300 },
};

struct Z7IISDIState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    unsigned bank;
    uint32_t config[ARRAY_SIZE(config_profile)];
    bool configured[ARRAY_SIZE(config_profile)];
    Z7IISDIChannel channel[ARRAY_SIZE(channel_map)];
    uint32_t training_modes;
    unsigned next_training_mode;
    bool training_result_known;
    bool top_reset;
    bool reported_error;
};

static MemTxResult z7ii_sdi_error(Z7IISDIState *s, hwaddr offset,
                                  unsigned size, bool write,
                                  uint64_t value, const char *reason)
{
    z7ii_access_error(&s->reported_error,
                       0x20020000ULL + s->bank * 0x20000ULL + offset,
                       size, write, value, reason);
    return MEMTX_ERROR;
}

static int z7ii_sdi_config_index(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(config_profile); i++) {
        if (config_profile[i].offset == offset) {
            return i;
        }
    }
    return -1;
}

static void z7ii_sdi_clear_channel(Z7IISDIChannel *channel)
{
    timer_del(channel->timer);
    channel->request = 0;
    channel->pending = false;
    channel->done = false;
    channel->initialized = false;
    channel->reset = true;
}

static void z7ii_sdi_complete(void *opaque)
{
    Z7IISDIChannel *channel = opaque;
    Z7IISDIState *s = channel->owner;

    channel->pending = false;
    channel->done = true;
    channel->initialized = true;
    if (channel->index == 2) {
        unsigned mode = channel->request >> 16;
        s->training_modes |= 1U << mode;
        s->next_training_mode = (mode + 1) % 5;
        s->training_result_known = true;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-sdi: bank=%u channel=%u request=0x%x "
                  "modeled-complete done=1 training=0x%x\n",
                  s->bank, channel->index, channel->request, s->training_modes);
}

static bool z7ii_sdi_configuration_complete(Z7IISDIState *s, unsigned channel)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(config_profile); i++) {
        uint32_t offset = config_profile[i].offset;
        bool required = false;

        if (channel == 0) {
            required = offset == 0x100 || offset == 0x10c ||
                       (offset >= 0x1004 && offset <= 0x1014) ||
                       offset == 0x2408 || offset == 0x2414 ||
                       offset >= 0x10000;
        } else if (channel == 1) {
            required = offset >= 0x3400 && offset < 0x4000;
        } else if (channel == 3) {
            required = offset >= 0x4400 && offset < 0x4500;
        } else if (channel >= 4) {
            uint32_t base = channel_map[channel].reset;
            uint32_t end = channel + 1 < ARRAY_SIZE(channel_map) ?
                           channel_map[channel + 1].reset : 0x10000;
            required = offset > base && offset < end;
        }
        if (required && !s->configured[i]) {
            return false;
        }
    }
    return true;
}

static MemTxResult z7ii_sdi_request(Z7IISDIState *s, unsigned index,
                                   hwaddr offset, uint64_t value)
{
    Z7IISDIChannel *channel = &s->channel[index];
    unsigned mode = value >> 16;
    uint32_t expected = index == 1 ? 0x10001 : 1;

    if (index == 2) {
        expected = 1 | (mode << 16);
    }
    if (value != expected || (index == 2 && mode > 4)) {
        return z7ii_sdi_error(s, offset, 4, true, value,
                              "sdi-request-profile-not-supported");
    }
    if (s->top_reset || (index != 2 && channel->reset) ||
        channel->pending || channel->done ||
        !z7ii_sdi_configuration_complete(s, index) ||
        (index == 1 && !s->channel[0].initialized) ||
        (index == 2 && (!s->channel[1].initialized ||
                       mode != s->next_training_mode)) ||
        (index == 3 && s->training_modes != 0x1f) ||
        (index >= 4 && !s->channel[3].initialized)) {
        return z7ii_sdi_error(s, offset, 4, true, value,
                              "sdi-request-phase-not-supported");
    }
    channel->request = value;
    channel->pending = true;
    /* Nominal virtual PHY/init/training/port latency, not physical calibration. */
    timer_mod(channel->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
    return MEMTX_OK;
}

static MemTxResult z7ii_sdi_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IISDIState *s = opaque;
    int config = z7ii_sdi_config_index(offset);
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_sdi_error(s, offset, size, false, 0,
                              "sdi-access-not-supported");
    }
    if (offset == 0) {
        *value = s->top_reset;
        return MEMTX_OK;
    }
    if (config >= 0) {
        *value = s->config[config];
        return MEMTX_OK;
    }
    if (offset == 0x1016c) {
        if (!s->training_result_known) {
            return z7ii_sdi_error(s, offset, size, false, 0,
                                  "sdi-training-result-uninitialized");
        }
        /* Successful virtual training backend; no physical health conclusion. */
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        if (offset == channel_map[i].reset) {
            *value = s->channel[i].reset;
            return MEMTX_OK;
        }
        if (offset == channel_map[i].request) {
            *value = s->channel[i].request;
            return MEMTX_OK;
        }
        if (offset == channel_map[i].status) {
            *value = s->channel[i].done;
            return MEMTX_OK;
        }
    }
    return z7ii_sdi_error(s, offset, size, false, 0,
                          "sdi-access-not-supported");
}

static void z7ii_sdi_reset(DeviceState *dev)
{
    Z7IISDIState *s = Z7II_SDI(dev);
    unsigned i;

    memset(s->config, 0, sizeof(s->config));
    memset(s->configured, 0, sizeof(s->configured));
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        z7ii_sdi_clear_channel(&s->channel[i]);
    }
    s->training_modes = 0;
    s->next_training_mode = 0;
    s->training_result_known = false;
    s->top_reset = true;
    s->reported_error = false;
}

static MemTxResult z7ii_sdi_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IISDIState *s = opaque;
    int config = z7ii_sdi_config_index(offset);
    unsigned i;

    if (size != 4 || (offset & 3)) {
        return z7ii_sdi_error(s, offset, size, true, value,
                              "sdi-access-not-supported");
    }
    if (offset == 0) {
        if (value > 1) {
            return z7ii_sdi_error(s, offset, size, true, value,
                                  "sdi-reset-profile-not-supported");
        }
        if (value) {
            z7ii_sdi_reset(DEVICE(s));
        } else {
            s->top_reset = false;
        }
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        Z7IISDIChannel *channel = &s->channel[i];

        if (offset == channel_map[i].reset) {
            if (value > 1) {
                return z7ii_sdi_error(s, offset, size, true, value,
                                      "sdi-reset-profile-not-supported");
            }
            if (value) {
                z7ii_sdi_clear_channel(channel);
                if (i <= 1 || i == 3) {
                    unsigned dependent;

                    for (dependent = i + 1;
                         dependent < ARRAY_SIZE(channel_map); dependent++) {
                        z7ii_sdi_clear_channel(&s->channel[dependent]);
                    }
                    if (i <= 1) {
                        s->training_modes = 0;
                        s->next_training_mode = 0;
                        s->training_result_known = false;
                    }
                }
            } else {
                channel->reset = false;
            }
            return MEMTX_OK;
        }
        if (offset == channel_map[i].request) {
            return z7ii_sdi_request(s, i, offset, value);
        }
        if (offset == channel_map[i].status) {
            if (value != 1 || channel->pending) {
                return z7ii_sdi_error(s, offset, size, true, value,
                                      "sdi-ack-profile-not-supported");
            }
            channel->done = false;
            return MEMTX_OK;
        }
    }
    if (config >= 0) {
        for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
            if (s->channel[i].pending) {
                return z7ii_sdi_error(s, offset, size, true, value,
                                      "sdi-config-while-pending");
            }
        }
        for (i = 0; i < config_profile[config].count[s->bank]; i++) {
            if (value == config_profile[config].values[s->bank][i]) {
                s->config[config] = value;
                s->configured[config] = true;
                return MEMTX_OK;
            }
        }
        return z7ii_sdi_error(s, offset, size, true, value,
                              "sdi-config-profile-not-supported");
    }
    return z7ii_sdi_error(s, offset, size, true, value,
                          "sdi-access-not-supported");
}

static const MemoryRegionOps z7ii_sdi_ops = {
    .read_with_attrs = z7ii_sdi_read,
    .write_with_attrs = z7ii_sdi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_sdi_init(Object *obj)
{
    Z7IISDIState *s = Z7II_SDI(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        s->channel[i].owner = s;
        s->channel[i].index = i;
        s->channel[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         z7ii_sdi_complete, &s->channel[i]);
    }
    memory_region_init_io(&s->regs, obj, &z7ii_sdi_ops, s,
                          "z7ii.sdi", 0x20000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void z7ii_sdi_finalize(Object *obj)
{
    Z7IISDIState *s = Z7II_SDI(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        timer_free(s->channel[i].timer);
    }
}

static const Property z7ii_sdi_properties[] = {
    DEFINE_PROP_UINT32("bank", Z7IISDIState, bank, 0),
};

static void z7ii_sdi_realize(DeviceState *dev, Error **errp)
{
    Z7IISDIState *s = Z7II_SDI(dev);

    if (s->bank > 1) {
        error_setg(errp, "Z7II SDI supports only banks 0 and 1");
    }
}

static const VMStateDescription z7ii_sdi_vmstate = {
    .name = "z7ii-sdi",
    .unmigratable = true,
};

static void z7ii_sdi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = z7ii_sdi_realize;
    device_class_set_props(dc, z7ii_sdi_properties);
    dc->vmsd = &z7ii_sdi_vmstate;
    device_class_set_legacy_reset(dc, z7ii_sdi_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_sdi_type = {
    .name = TYPE_Z7II_SDI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IISDIState),
    .instance_init = z7ii_sdi_init,
    .instance_finalize = z7ii_sdi_finalize,
    .class_init = z7ii_sdi_class_init,
};

static void z7ii_sdi_register_types(void)
{
    type_register_static(&z7ii_sdi_type);
}
type_init(z7ii_sdi_register_types)
