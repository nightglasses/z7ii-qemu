/*
 * Source-defined lookup configuration, two lanes and explicit external results.
 * No image operation, DMA, hardware identity or completion time is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/lookup.h"

#define TYPE_NIKON_LOOKUP "nikon-lookup"
OBJECT_DECLARE_TYPE(NikonLookupState, NikonLookupClass, NIKON_LOOKUP)

struct NikonLookupClass {
    SysBusDeviceClass parent_class;
    bool words;
};

struct NikonLookupState {
    SysBusDevice parent_obj;
    MemoryRegion main, auxiliary;
    qemu_irq irq;
    uint32_t config[0xf4 / 4];
    uint32_t auxiliary_config[0x2c / 4];
    uint8_t byte_config[11];
    uint8_t tables[NIKON_LOOKUP_TABLES][NIKON_LOOKUP_TABLE_SIZE];
    uint64_t written[NIKON_LOOKUP_TABLES];
    NikonLookupSample sample;
    bool active[2];
    bool external_active;
    bool connected;
    bool reported_error;
};

static bool nikon_lookup_words(NikonLookupState *s)
{
    return NIKON_LOOKUP_GET_CLASS(s)->words;
}

static void nikon_lookup_irq(NikonLookupState *s)
{
    /* Mask polarity is an explicit functional convention. The provider must
     * publish real summary causes; auxiliary flags do not manufacture them. */
    qemu_set_irq(s->irq, s->connected &&
                 !!(s->sample.pending & s->config[0x84 / 4]));
}

static uint32_t nikon_lookup_busy(NikonLookupState *s)
{
    uint32_t value = s->connected ? s->sample.busy : 0;

    /* Functional idle is known only when no operation is owned or observed.
     * Stop requests cannot retire work; a real idle observation or reset can. */
    if (s->active[0] || s->active[1] || s->external_active) {
        value |= 0x80000000;
    }
    return value;
}

static MemTxResult nikon_lookup_error(NikonLookupState *s, MemoryRegion *mr,
                                     hwaddr offset, unsigned size, bool write,
                                     uint64_t value)
{
    nikon_access_error(&s->reported_error, mr->addr + offset, size,
                      write, value, "lookup-access-or-provider-not-supported");
    return MEMTX_ERROR;
}

static bool nikon_lookup_config(NikonLookupState *s, hwaddr offset,
                               uint32_t *mask)
{
    bool words = nikon_lookup_words(s);

    switch (offset) {
    case 0: *mask = 0xc03e; break;
    case 4: *mask = 0xc00f; break;
    case 8: *mask = words ? 0x10110c01 : 0x10c01; break;
    case 0x18: case 0x1c: case 0x50: *mask = 0xffff; break;
    case 0x10: case 0x14: case 0x30: case 0x34: case 0x40:
    case 0x54: case 0x58: case 0x84: *mask = UINT32_MAX; break;
    case 0x90: case 0x94: *mask = 0x3000f7; break;
    case 0xe0: *mask = 0xff3f; break;
    case 0x20: case 0x24: case 0x28:
        if (!words) {
            return false;
        }
        *mask = 0xffff;
        break;
    case 0xb0:
        if (!words) {
            return false;
        }
        *mask = 0xffffff;
        break;
    case 0xc0: case 0xc8: case 0xd0:
        if (!words) {
            return false;
        }
        *mask = 0x03ff03ff;
        break;
    case 0xc4: case 0xcc: case 0xd4:
        if (!words) {
            return false;
        }
        *mask = UINT32_MAX; /* Separately validate the source sign extension. */
        break;
    case 0xf0:
        if (words) {
            return false;
        }
        *mask = UINT32_MAX;
        break;
    default:
        return false;
    }
    return true;
}

static MemTxResult nikon_lookup_main_read(void *opaque, hwaddr offset,
                                        uint64_t *value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonLookupState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0x70:
        *value = s->sample.count;
        break;
    case 0x74:
        *value = s->sample.result;
        break;
    case 0x80:
        *value = s->sample.pending;
        break;
    case 0x88:
        *value = nikon_lookup_busy(s);
        return MEMTX_OK;
    case 0x90: case 0x94:
        *value = s->config[offset / 4] |
                 (s->sample.lane[(offset - 0x90) / 4] << 24);
        break;
    default:
        if (!nikon_lookup_config(s, offset, &mask)) {
            goto error;
        }
        *value = s->config[offset / 4];
        return MEMTX_OK;
    }
    if (s->connected) {
        return MEMTX_OK;
    }
error:
    *value = 0;
    return nikon_lookup_error(s, &s->main, offset, size, false, 0);
}

static MemTxResult nikon_lookup_main_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned size,
                                         MemTxAttrs attrs)
{
    NikonLookupState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    if (offset == 0x80) {
        /* ACK cannot establish a previously unknown external status. */
        if (!s->connected) {
            goto error;
        }
        s->sample.pending &= ~(uint32_t)value;
    } else if (nikon_lookup_config(s, offset, &mask)) {
        if (value & ~mask) {
            goto error;
        }
        if (offset == 0 || offset == 4) {
            uint32_t command = value & 0xc000;

            if (command != 0 && command != 0x4000) {
                goto error;
            }
            if (command == 0x4000) {
                s->active[offset / 4] = true;
            }
        }
        if (offset == 0xc4 || offset == 0xcc || offset == 0xd4) {
            if ((int32_t)value != (int16_t)value) {
                goto error;
            }
        }
        s->config[offset / 4] = value;
    } else {
        goto error;
    }
    nikon_lookup_irq(s);
    return MEMTX_OK;
error:
    return nikon_lookup_error(s, &s->main, offset, size, true, value);
}

static bool nikon_lookup_aux_config(NikonLookupState *s, hwaddr offset,
                                   uint32_t *mask)
{
    if (!nikon_lookup_words(s)) {
        if (offset > 10 || offset == 2) {
            return false;
        }
        *mask = offset == 0 ? 0x1f : offset == 1 ? 0x83 : 0xff;
        return true;
    }
    switch (offset) {
    case 0: *mask = 0x73f; break;
    case 4: *mask = 0x80000003; break;
    case 0xc: case 0x10: *mask = 0x3f; break;
    case 0x14: case 0x28: *mask = UINT32_MAX; break;
    case 0x18: case 0x1c: *mask = 0xffff; break;
    case 0x24: *mask = 7; break;
    default: return false;
    }
    return true;
}

static bool nikon_lookup_table_offset(NikonLookupState *s, hwaddr offset,
                                     unsigned size, unsigned *table,
                                     unsigned *byte)
{
    if (offset < 0x100 || offset >= 0x200 ||
        size != (nikon_lookup_words(s) ? 4 : 1) || (offset & (size - 1))) {
        return false;
    }
    *table = (offset - 0x100) / NIKON_LOOKUP_TABLE_SIZE;
    *byte = (offset - 0x100) % NIKON_LOOKUP_TABLE_SIZE;
    return true;
}

static MemTxResult nikon_lookup_aux_read(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonLookupState *s = opaque;
    bool words = nikon_lookup_words(s);
    uint32_t mask;
    unsigned table, byte;

    *value = 0;
    if (nikon_lookup_table_offset(s, offset, size, &table, &byte)) {
        uint64_t required = ((1ULL << size) - 1) << byte;

        if ((s->written[table] & required) != required) {
            goto error;
        }
        *value = words ? ldl_be_p(&s->tables[table][byte]) :
                         s->tables[table][byte];
        return MEMTX_OK;
    }
    if (size != (words ? 4 : 1) || (offset & (size - 1))) {
        goto error;
    }
    if (nikon_lookup_aux_config(s, offset, &mask)) {
        *value = words ? s->auxiliary_config[offset / 4] : s->byte_config[offset];
        return MEMTX_OK;
    }
    if (!s->connected) {
        goto error;
    }
    if (words) {
        switch (offset) {
        case 0x20: *value = s->sample.auxiliary_result; break;
        case 0x2c: *value = s->sample.auxiliary_pending; break;
        case 0x30: *value = s->sample.selection; break;
        case 0x34: *value = s->sample.flags; break;
        case 0x38: *value = s->sample.available; break;
        case 0x3c: *value = s->sample.extent; break;
        default: goto error;
        }
    } else {
        switch (offset) {
        case 0xb: *value = (s->sample.auxiliary_result >> 16) & 0xff; break;
        case 0xc: *value = (s->sample.auxiliary_result >> 8) & 0xff; break;
        case 0xd: *value = s->sample.auxiliary_result & 0xff; break;
        case 0xe: *value = s->sample.auxiliary_pending; break;
        case 0xf: *value = s->sample.auxiliary_result >> 24; break;
        default: goto error;
        }
    }
    return MEMTX_OK;
error:
    *value = 0;
    return nikon_lookup_error(s, &s->auxiliary, offset, size, false, 0);
}

static MemTxResult nikon_lookup_aux_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonLookupState *s = opaque;
    bool words = nikon_lookup_words(s);
    uint32_t mask;
    unsigned table, byte;

    if (nikon_lookup_table_offset(s, offset, size, &table, &byte)) {
        if (words) {
            stl_be_p(&s->tables[table][byte], value);
        } else {
            s->tables[table][byte] = value;
        }
        s->written[table] |= ((1ULL << size) - 1) << byte;
        return MEMTX_OK;
    }
    if (size != (words ? 4 : 1) || (offset & (size - 1))) {
        goto error;
    }
    if (offset == (words ? 0x2c : 0xe)) {
        if (!s->connected) {
            goto error;
        }
        s->sample.auxiliary_pending &= value;
    } else if (nikon_lookup_aux_config(s, offset, &mask)) {
        if (value & ~mask) {
            goto error;
        }
        if (words) {
            s->auxiliary_config[offset / 4] = value;
        } else {
            s->byte_config[offset] = value;
        }
    } else {
        goto error;
    }
    return MEMTX_OK;
error:
    return nikon_lookup_error(s, &s->auxiliary, offset, size, true, value);
}

bool nikon_lookup_publish(DeviceState *dev, const NikonLookupSample *sample)
{
    NikonLookupState *s = NIKON_LOOKUP(dev);
    bool words = nikon_lookup_words(s);
    uint32_t pending, auxiliary_pending;

    if (!sample || sample->lane[0] > (words ? 3 : 0xff) ||
        sample->lane[1] > (words ? 3 : 0xff) ||
        (!words && (sample->auxiliary_pending > 0xff || sample->selection ||
                    sample->flags || sample->available || sample->extent))) {
        return false;
    }
    pending = s->sample.pending;
    auxiliary_pending = s->sample.auxiliary_pending;
    s->sample = *sample;
    s->sample.pending |= pending;
    s->sample.auxiliary_pending |= auxiliary_pending;
    s->external_active = !!(sample->busy & 0x80000000);
    if (!s->external_active) {
        memset(s->active, 0, sizeof(s->active));
    }
    s->connected = true;
    nikon_lookup_irq(s);
    return true;
}

void nikon_lookup_disconnect(DeviceState *dev)
{
    NikonLookupState *s = NIKON_LOOKUP(dev);

    /* Withdraw observations, not causes awaiting an independent guest ACK. */
    s->connected = false;
    nikon_lookup_irq(s);
}

bool nikon_lookup_table(DeviceState *dev, unsigned table,
                        uint8_t data[NIKON_LOOKUP_TABLE_SIZE])
{
    NikonLookupState *s = NIKON_LOOKUP(dev);

    if (!data || table >= NIKON_LOOKUP_TABLES || s->written[table] != UINT64_MAX) {
        return false;
    }
    memcpy(data, s->tables[table], NIKON_LOOKUP_TABLE_SIZE);
    return true;
}

static const MemoryRegionOps nikon_lookup_main_ops = {
    .read_with_attrs = nikon_lookup_main_read,
    .write_with_attrs = nikon_lookup_main_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static const MemoryRegionOps nikon_lookup_aux_ops = {
    .read_with_attrs = nikon_lookup_aux_read,
    .write_with_attrs = nikon_lookup_aux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_lookup_reset(DeviceState *dev)
{
    NikonLookupState *s = NIKON_LOOKUP(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->auxiliary_config, 0, sizeof(s->auxiliary_config));
    memset(s->byte_config, 0, sizeof(s->byte_config));
    memset(s->written, 0, sizeof(s->written));
    memset(s->active, 0, sizeof(s->active));
    memset(&s->sample, 0, sizeof(s->sample));
    s->external_active = false;
    s->reported_error = false;
    nikon_lookup_disconnect(dev);
}

static void nikon_lookup_init(Object *obj)
{
    NikonLookupState *s = NIKON_LOOKUP(obj);

    memory_region_init_io(&s->main, obj, &nikon_lookup_main_ops, s,
                          "nikon.lookup.main", 0xf4);
    memory_region_init_io(&s->auxiliary, obj, &nikon_lookup_aux_ops, s,
                          "nikon.lookup.auxiliary", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->main);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->auxiliary);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription nikon_lookup_vmstate = {
    .name = TYPE_NIKON_LOOKUP,
    .unmigratable = true,
};

static void nikon_lookup_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_lookup_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_lookup_reset);
}

static void nikon_lookup_words_class_init(ObjectClass *oc, const void *data)
{
    NIKON_LOOKUP_CLASS(oc)->words = true;
}

static const TypeInfo nikon_lookup_types[] = {
    {
        .name = TYPE_NIKON_LOOKUP,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NikonLookupState),
        .class_size = sizeof(NikonLookupClass),
        .instance_init = nikon_lookup_init,
        .class_init = nikon_lookup_class_init,
        .abstract = true,
    }, {
        .name = TYPE_NIKON_LOOKUP_BYTES,
        .parent = TYPE_NIKON_LOOKUP,
    }, {
        .name = TYPE_NIKON_LOOKUP_WORDS,
        .parent = TYPE_NIKON_LOOKUP,
        .class_init = nikon_lookup_words_class_init,
    },
};

DEFINE_TYPES(nikon_lookup_types)
