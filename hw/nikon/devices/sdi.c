/*
 * Nikon functional SDI/PHY/DDR bring-up protocol.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonSDIState, NIKON_SDI)

enum {
    SDI_PHY, SDI_DDR, SDI_TRAIN, SDI_ARB, SDI_PORT,
    SDI_CONFIG_WORDS = 110,
    SDI_LATENCY_NS = 10000,
};

typedef struct NikonSDIChannel {
    NikonSDIState *owner;
    QEMUTimer *timer;
    unsigned index;
    uint32_t request;
    bool reset;
    bool pending;
    bool done;
    bool initialized;
} NikonSDIChannel;

/* Register families, not firmware values. The contiguous word counts describe
 * the complete applied configuration in the MAS closure and SLA's two init
 * families (1002361c/10024cb8, source revision 1076d278). Packed fields whose
 * physical layout is not established retain their complete programmed word.
 * The four-lane delay masks and rank/clock masks are functional constraints;
 * they do not identify the silicon or model electrical calibration.
 */
static const struct {
    uint32_t base;
    unsigned count;
    unsigned stride;
    uint32_t mask;
    unsigned phase;
} config_group[] = {
    { 0x100, 1, 4, 3, SDI_PHY },                    /* TOP configuration */
    { 0x10c, 1, 4, 0x303, SDI_PHY },                /* rank enables */
    { 0x1004, 5, 4, 1, SDI_PHY },                   /* clock enables */
    { 0x1020, 5, 4, 0x01010001, SDI_PORT },          /* port clocks */
    { 0x1038, 1, 4, 0x01010001, SDI_PORT },
    { 0x2408, 1, 4, 0xffff, SDI_PHY },              /* PHY AC */
    { 0x2414, 1, 4, 0xffff, SDI_PHY },
    { 0x3404, 3, 4, 0xffff, SDI_DDR },              /* DDR mode registers */
    { 0x3500, 2, 4, UINT32_MAX, SDI_DDR },           /* DDR geometry */
    { 0x3508, 4, 4, UINT32_MAX, SDI_DDR },           /* address boundaries */
    { 0x3600, 2, 4, UINT32_MAX, SDI_DDR },           /* packed AC timings */
    { 0x360c, 11, 4, UINT32_MAX, SDI_DDR },
    { 0x3700, 1, 4, UINT32_MAX, SDI_DDR },           /* PHY function */
    { 0x3708, 2, 4, UINT32_MAX, SDI_DDR },
    { 0x3800, 3, 4, UINT32_MAX, SDI_DDR },           /* refresh */
    { 0x3900, 2, 4, UINT32_MAX, SDI_DDR },           /* ZQ interval */
    { 0x3a00, 2, 4, UINT32_MAX, SDI_DDR },           /* command interval */
    { 0x3b00, 2, 4, UINT32_MAX, SDI_DDR },
    { 0x4400, 3, 4, 0x000fffff, SDI_ARB },          /* arbitration */
    { 0x8040, 5, 0x1000, 0x000f000f, SDI_PORT },    /* port arbitration */
    { 0xe400, 1, 4, 3, SDI_PORT },                  /* final interface */
    { 0xe600, 3, 4, 0xffff, SDI_PORT },
    { 0xe700, 5, 4, UINT32_MAX, SDI_PORT },
    { 0x10010, 6, 4, UINT32_MAX, SDI_PHY },          /* DDRL wait / PHY mode */
    { 0x10030, 2, 4, UINT32_MAX, SDI_PHY },          /* impedance / ZQ */
    { 0x10040, 3, 4, UINT32_MAX, SDI_PHY },
    { 0x10050, 1, 4, 0x0303003f, SDI_PHY },          /* training selection */
    { 0x10054, 4, 4, 0x001ff1ff, SDI_PHY },          /* lane write delays */
    { 0x10074, 4, 4, 0x001ff1ff, SDI_PHY },          /* lane gate delays */
    { 0x10094, 4, 4, 0x00000fff, SDI_PHY },          /* lane synchronizers */
    { 0x100b8, 1, 4, 3, SDI_PHY },                  /* rank selection */
    { 0x100e4, 4, 4, 0x001ff1ff, SDI_PHY },          /* WL search windows */
    { 0x10128, 4, 4, 0x00010fff, SDI_PHY },          /* read eye controls */
    { 0x10148, 4, 4, 0x00010fff, SDI_PHY },          /* write eye controls */
    { 0x10820, 4, 4, UINT32_MAX, SDI_PHY },          /* training pattern */
    { 0x10840, 1, 4, UINT32_MAX, SDI_PHY },
    { 0x10850, 1, 4, UINT32_MAX, SDI_PHY },
    { 0x10910, 1, 4, UINT32_MAX, SDI_PHY },
};

/* PHY, DDR init, five-mode training, arbitration, fifteen ports, interface. */
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

struct NikonSDIState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    unsigned bank;
    uint32_t config[SDI_CONFIG_WORDS];
    bool configured[SDI_CONFIG_WORDS];
    NikonSDIChannel channel[ARRAY_SIZE(channel_map)];
    uint32_t training_modes;
    uint32_t training_error;
    unsigned next_training_mode;
    bool training_result_known;
    bool top_reset;
    bool reported_error;
};

static MemTxResult nikon_sdi_error(NikonSDIState *s, hwaddr offset,
                                  unsigned size, bool write,
                                  uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset,
                       size, write, value, reason);
    return MEMTX_ERROR;
}

static int nikon_sdi_config_index(hwaddr offset, unsigned *group)
{
    unsigned i, first = 0;

    for (i = 0; i < ARRAY_SIZE(config_group); i++) {
        uint32_t base = config_group[i].base;
        unsigned stride = config_group[i].stride;

        if (offset >= base && (offset - base) % stride == 0 &&
            (offset - base) / stride < config_group[i].count) {
            if (group) {
                *group = i;
            }
            return first + (offset - base) / stride;
        }
        first += config_group[i].count;
    }
    return -1;
}

static uint32_t nikon_sdi_config(NikonSDIState *s, uint32_t offset)
{
    int index = nikon_sdi_config_index(offset, NULL);

    assert(index >= 0 && index < SDI_CONFIG_WORDS);
    return s->config[index];
}

static void nikon_sdi_clear_channel(NikonSDIChannel *channel)
{
    timer_del(channel->timer);
    channel->request = 0;
    channel->pending = false;
    channel->done = false;
    channel->initialized = false;
    channel->reset = true;
}

/* The functional backend uses the applied manual lane delay as its stable
 * calibration target. A WL sweep succeeds only if its programmed interval
 * contains that target. No physical eye, RAM traffic, or vendor result byte is
 * invented. The guest can receive an actual error and retry with a new window.
 */
static uint32_t nikon_sdi_training_error(NikonSDIState *s, unsigned mode)
{
    unsigned lane;

    for (lane = 0; lane < 4; lane++) {
        if (mode == 0) {
            uint32_t window = nikon_sdi_config(s, 0x100e4 + lane * 4);
            uint32_t target = nikon_sdi_config(s, 0x10054 + lane * 4) & 0x1ff;
            uint32_t low = window & 0x1ff;
            uint32_t high = (window >> 12) & 0x1ff;

            if (low > high || target < low || target > high) {
                return 1U << 2; /* TRAINERR.WL_ERR */
            }
        } else if (mode >= 3) {
            uint32_t control = nikon_sdi_config(s,
                (mode == 3 ? 0x10128 : 0x10148) + lane * 4);

            if (!(control & 0x10000) || !(control & 0xfff)) {
                return 1U << (mode == 3 ? 5 : 6);
            }
        }
    }
    return 0;
}

static void nikon_sdi_complete(void *opaque)
{
    NikonSDIChannel *channel = opaque;
    NikonSDIState *s = channel->owner;

    channel->pending = false;
    channel->done = true;
    channel->initialized = true;
    if (channel->index == SDI_TRAIN) {
        unsigned mode = channel->request >> 16;

        s->training_error = nikon_sdi_training_error(s, mode);
        if (!s->training_error) {
            s->training_modes |= 1U << mode;
        }
        s->next_training_mode = (mode + 1) % 5;
        s->training_result_known = true;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-sdi: bank=%u channel=%u request=0x%x "
                  "modeled-complete done=1 training=0x%x error=0x%x\n",
                  s->bank, channel->index, channel->request,
                  s->training_modes, s->training_error);
}

static bool nikon_sdi_configuration_complete(NikonSDIState *s, unsigned channel)
{
    unsigned group, first = 0;

    for (group = 0; group < ARRAY_SIZE(config_group); group++) {
        for (unsigned word = 0; word < config_group[group].count; word++) {
            uint32_t offset = config_group[group].base +
                              word * config_group[group].stride;
            bool required = config_group[group].phase == channel;

            if (channel >= SDI_PORT) {
                uint32_t base = channel_map[channel].reset;
                uint32_t end = channel + 1 < ARRAY_SIZE(channel_map) ?
                               channel_map[channel + 1].reset : 0x10000;

                required = offset > base && offset < end;
                if (channel < ARRAY_SIZE(channel_map) - 1 &&
                    (channel - SDI_PORT) % 3 == 0) {
                    unsigned port = (channel - SDI_PORT) / 3;

                    required |= offset == (port == 4 ? 0x1030 :
                                           0x1020 + port * 4);
                } else if (channel == ARRAY_SIZE(channel_map) - 1) {
                    required |= offset == 0x1038;
                }
            }
            if (required && !s->configured[first + word]) {
                return false;
            }
        }
        first += config_group[group].count;
    }
    if (channel == SDI_PHY) {
        uint32_t ranks = nikon_sdi_config(s, 0x100b8);

        return ranks != 0 && nikon_sdi_config(s, 0x10c) ==
               (ranks | ranks << 8);
    }
    if (channel == SDI_DDR) {
        uint32_t end = nikon_sdi_config(s, 0x3508);

        return end && !(end & 0x0fffffff) &&
               end == nikon_sdi_config(s, 0x350c) &&
               end == nikon_sdi_config(s, 0x3514) &&
               !nikon_sdi_config(s, 0x3510) &&
               nikon_sdi_config(s, 0x3500) &&
               nikon_sdi_config(s, 0x3504) &&
               nikon_sdi_config(s, 0x3900) &&
               nikon_sdi_config(s, 0x3a00);
    }
    return true;
}

static MemTxResult nikon_sdi_request(NikonSDIState *s, unsigned index,
                                   hwaddr offset, uint64_t value)
{
    NikonSDIChannel *channel = &s->channel[index];
    unsigned mode = value >> 16;
    uint32_t expected = index == SDI_DDR ? 0x10001 : 1;

    if (index == SDI_TRAIN) {
        expected = 1 | mode << 16;
    }
    if (value != expected || (index == SDI_TRAIN && mode > 4)) {
        return nikon_sdi_error(s, offset, 4, true, value,
                              "sdi-request-not-supported");
    }
    if (s->top_reset || (index != SDI_TRAIN && channel->reset) ||
        channel->pending || channel->done ||
        !nikon_sdi_configuration_complete(s, index) ||
        (index == SDI_DDR && !s->channel[SDI_PHY].initialized) ||
        (index == SDI_TRAIN && (!s->channel[SDI_DDR].initialized ||
                               mode != s->next_training_mode)) ||
        (index == SDI_ARB && s->training_modes != 0x1f) ||
        (index >= SDI_PORT && !s->channel[SDI_ARB].initialized)) {
        return nikon_sdi_error(s, offset, 4, true, value,
                              "sdi-request-phase-not-supported");
    }
    if (index == SDI_TRAIN) {
        s->training_result_known = false;
        if (mode == 0) {
            s->training_modes = 0;
        }
    }
    channel->request = value;
    channel->pending = true;
    timer_mod(channel->timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDI_LATENCY_NS);
    return MEMTX_OK;
}

static MemTxResult nikon_sdi_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonSDIState *s = opaque;
    int config = nikon_sdi_config_index(offset, NULL);
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_sdi_error(s, offset, size, false, 0,
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
            return nikon_sdi_error(s, offset, size, false, 0,
                                  "sdi-training-result-uninitialized");
        }
        *value = s->training_error;
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
    return nikon_sdi_error(s, offset, size, false, 0,
                          "sdi-access-not-supported");
}

static void nikon_sdi_reset(DeviceState *dev)
{
    NikonSDIState *s = NIKON_SDI(dev);
    unsigned i;

    memset(s->config, 0, sizeof(s->config));
    memset(s->configured, 0, sizeof(s->configured));
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        nikon_sdi_clear_channel(&s->channel[i]);
    }
    s->training_modes = 0;
    s->training_error = 0;
    s->next_training_mode = 0;
    s->training_result_known = false;
    s->top_reset = true;
    s->reported_error = false;
}

static MemTxResult nikon_sdi_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonSDIState *s = opaque;
    unsigned group = 0, i;
    int config = nikon_sdi_config_index(offset, &group);

    if (size != 4 || (offset & 3)) {
        return nikon_sdi_error(s, offset, size, true, value,
                              "sdi-access-not-supported");
    }
    if (offset == 0) {
        if (value > 1) {
            return nikon_sdi_error(s, offset, size, true, value,
                                  "sdi-reset-not-supported");
        }
        if (value) {
            nikon_sdi_reset(DEVICE(s));
        } else {
            s->top_reset = false;
        }
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        NikonSDIChannel *channel = &s->channel[i];

        if (offset == channel_map[i].reset) {
            if (value > 1) {
                return nikon_sdi_error(s, offset, size, true, value,
                                      "sdi-reset-not-supported");
            }
            if (value) {
                nikon_sdi_clear_channel(channel);
                if (i <= SDI_DDR || i == SDI_ARB) {
                    for (unsigned dependent = i + 1;
                         dependent < ARRAY_SIZE(channel_map); dependent++) {
                        nikon_sdi_clear_channel(&s->channel[dependent]);
                    }
                    if (i <= SDI_DDR) {
                        s->training_modes = 0;
                        s->training_error = 0;
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
            return nikon_sdi_request(s, i, offset, value);
        }
        if (offset == channel_map[i].status) {
            if (value != 1 || channel->pending) {
                return nikon_sdi_error(s, offset, size, true, value,
                                      "sdi-ack-not-supported");
            }
            /* W1C of an already clear flag is legal, including the original
             * final interface's asymmetric CC30 acknowledgement.
             */
            channel->done = false;
            return MEMTX_OK;
        }
    }
    if (config >= 0) {
        for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
            if (s->channel[i].pending) {
                return nikon_sdi_error(s, offset, size, true, value,
                                      "sdi-config-while-pending");
            }
        }
        if (value & ~config_group[group].mask) {
            return nikon_sdi_error(s, offset, size, true, value,
                                  "sdi-config-reserved-bits");
        }
        if (offset >= 0x10050 && s->config[config] != value) {
            /* A result belongs to the applied training configuration. A new
             * sweep starts at mode zero after the guest programs its retry.
             */
            s->training_modes = 0;
            s->training_error = 0;
            s->next_training_mode = 0;
            s->training_result_known = false;
        }
        s->config[config] = value;
        s->configured[config] = true;
        return MEMTX_OK;
    }
    return nikon_sdi_error(s, offset, size, true, value,
                          "sdi-access-not-supported");
}

static const MemoryRegionOps nikon_sdi_ops = {
    .read_with_attrs = nikon_sdi_read,
    .write_with_attrs = nikon_sdi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_sdi_init(Object *obj)
{
    NikonSDIState *s = NIKON_SDI(obj);
    unsigned i, words = 0;

    for (i = 0; i < ARRAY_SIZE(config_group); i++) {
        words += config_group[i].count;
    }
    assert(words == SDI_CONFIG_WORDS);
    for (i = 0; i < ARRAY_SIZE(channel_map); i++) {
        s->channel[i].owner = s;
        s->channel[i].index = i;
        s->channel[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         nikon_sdi_complete, &s->channel[i]);
    }
    memory_region_init_io(&s->regs, obj, &nikon_sdi_ops, s,
                          "nikon.sdi", 0x20000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void nikon_sdi_finalize(Object *obj)
{
    NikonSDIState *s = NIKON_SDI(obj);

    for (unsigned i = 0; i < ARRAY_SIZE(channel_map); i++) {
        timer_free(s->channel[i].timer);
    }
}

static const Property nikon_sdi_properties[] = {
    DEFINE_PROP_UINT32("bank", NikonSDIState, bank, 0),
};

static void nikon_sdi_realize(DeviceState *dev, Error **errp)
{
    NikonSDIState *s = NIKON_SDI(dev);

    if (s->bank > 1) {
        error_setg(errp, "Nikon SDI supports only banks 0 and 1");
    }
}

static const VMStateDescription nikon_sdi_vmstate = {
    .name = "nikon-sdi",
    .unmigratable = true,
};

static void nikon_sdi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_sdi_realize;
    device_class_set_props(dc, nikon_sdi_properties);
    dc->vmsd = &nikon_sdi_vmstate;
    device_class_set_legacy_reset(dc, nikon_sdi_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_sdi_type = {
    .name = TYPE_NIKON_SDI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSDIState),
    .instance_init = nikon_sdi_init,
    .instance_finalize = nikon_sdi_finalize,
    .class_init = nikon_sdi_class_init,
};

static void nikon_sdi_register_types(void)
{
    type_register_static(&nikon_sdi_type);
}
type_init(nikon_sdi_register_types)
