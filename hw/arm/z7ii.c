/*
 * Z7II MAS diagnostic machine. This is not a Nikon board hardware model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "qemu/timer.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/or-irq.h"
#include "hw/intc/arm_gic.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "target/arm/cpu.h"
#include "z7ii.h"

#define TYPE_Z7II_MACHINE MACHINE_TYPE_NAME("z7ii-mas-diagnostic")
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMachineState, Z7II_MACHINE)

#define MAS_BASE 0x10020000ULL
#define MAS_MAX_SIZE 0x2477b78ULL
#define STACK_SLOT_SIZE 0x4000
#define MEMC_BASE 0x04264000ULL
#define CLOCK_BASE 0x04281000ULL
#define COMPLETION_BASE 0x04287000ULL
#define CBAR_BASE 0xf8100000ULL
#define TIMER_HZ 48000000ULL
#define Z7II_MAX_CPUS 2

#define TYPE_Z7II_MEMC "z7ii-memc-diagnostic"
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMemcState, Z7II_MEMC)

#define TYPE_Z7II_CLOCK_GATES "z7ii-clock-gates-diagnostic"
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIClockGatesState, Z7II_CLOCK_GATES)

typedef struct Z7IIClockChannel {
    Z7IIClockGatesState *owner;
    unsigned index;
    Clock *output;
    QEMUTimer *lock_timer;
    bool locked;
} Z7IIClockChannel;

struct Z7IIClockGatesState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t stopped[6];
    uint32_t known[6];
    uint32_t pll[3];
    uint32_t selector[10];
    uint32_t bit_control[2];
    uint32_t word_control;
    Z7IIClockChannel channel[9];
    bool reported_error;
};

/* Original nine-record group table; sentinel 0x20 means no such field. */
static const struct {
    unsigned reg;
    uint32_t stop_mask;
    uint32_t output_mask;
} z7ii_pll_group[] = {
    { 0, 4, 8 }, { 0, 0, 2 }, { 0, 0x10, 0x20 },
    { 0, 0x40, 0x80 }, { 0, 0x100, 0x200 }, { 0, 0x400, 0x800 },
    { 1, 1, 2 }, { 1, 0x70, 0 }, { 2, 1, 2 },
};

#define TYPE_Z7II_COMPLETION "z7ii-completion-diagnostic"
OBJECT_DECLARE_SIMPLE_TYPE(Z7IICompletionState, Z7II_COMPLETION)

struct Z7IICompletionState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    QEMUBH *bh;
    uint32_t request;
    uint32_t applied_domains;
    bool irq_enabled;
    bool done;
    bool pending;
    bool reported_error;
};

struct Z7IIMemcState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t mode;
    uint32_t timing;
    bool mode_initialized;
    bool timing_initialized;
    bool reported_error;
};

typedef struct Z7IINorSlot {
    MemoryRegion region;
    Z7IIMachineState *machine;
    hwaddr base;
} Z7IINorSlot;

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu[Z7II_MAX_CPUS];
    unsigned num_cpus;
    MemoryRegion image;
    Z7IINorSlot nv_slot[5];
    MemoryRegion unknown;
    MemoryRegion copy_ram[2];
    MemoryRegion table_ram;
    MemoryRegion bss_ram;
    MemoryRegion bss_storage;
    MemoryRegion kernel_arena;
    MemoryRegion relocated_code;
    MemoryRegion module_buffer;
    MemoryRegion private_class;
    MemoryRegion stack_ram[6 * Z7II_MAX_CPUS];
    MemoryRegion temporary_stack;
    MemoryRegion early_release;
    Z7IIMemcState memc;
    Z7IIClockGatesState clock_gates;
    Z7IICompletionState completion;
    bool reported_unknown;
};

void z7ii_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason)
{
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;

    /* Debugger/monitor probes must not become guest execution evidence. */
    if (!cpu || *reported) {
        return;
    }
    *reported = true;
    message = g_strdup_printf("z7ii-mas-diagnostic: uncovered access "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " direction=%s width=%u "
                              "value=0x%" PRIx64 " transaction=error reason=%s",
                              cpu->cpu_index,
                              (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu),
                              addr, write ? "write" : "read", size, value,
                              reason);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}

static MemTxResult z7ii_unknown_read(void *opaque, hwaddr addr,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    z7ii_access_error(&s->reported_unknown, addr, size, false, 0,
                      "uncovered-address");
    *value = 0; /* Output is invalid: the transaction explicitly fails. */
    return MEMTX_ERROR;
}

static MemTxResult z7ii_unknown_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    z7ii_access_error(&s->reported_unknown, addr, size, true, value,
                      "uncovered-address");
    return MEMTX_ERROR;
}

static const MemoryRegionOps z7ii_unknown_ops = {
    .read_with_attrs = z7ii_unknown_read,
    .write_with_attrs = z7ii_unknown_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static MemTxResult z7ii_memc_error(Z7IIMemcState *s, hwaddr addr,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    z7ii_access_error(&s->reported_error, addr, size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_memc_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIMemcState *s = opaque;

    *value = 0; /* Invalid unless the transaction below succeeds. */
    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, false, 0,
                               "memc-unsupported-access");
    }
    if ((offset == 0 && !s->mode_initialized) ||
        (offset == 0x20 && !s->timing_initialized)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, false, 0,
                               "memc-uninitialized-read");
    }
    *value = offset == 0 ? s->mode : s->timing;
    return MEMTX_OK;
}

static MemTxResult z7ii_memc_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    Z7IIMemcState *s = opaque;

    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, true, value,
                               "memc-unsupported-access");
    }
    /* Supported functional profiles, not the complete IP's legal values. */
    if (offset == 0) {
        if (value != 0x21 && value != 0x29) {
            return z7ii_memc_error(s, MEMC_BASE, size, true, value,
                                   "memc-mode-profile-not-supported");
        }
        s->mode = value; /* WDT=1 (x16), PAGE=1, WEOFF=bit3. */
        s->mode_initialized = true;
    } else {
        if (value != 0x153b0902) {
            return z7ii_memc_error(s, MEMC_BASE + offset, size, true, value,
                                   "memc-timing-profile-not-supported");
        }
        s->timing = value;
        s->timing_initialized = true;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "z7ii-memc: write offset=0x%" HWADDR_PRIx
                  " value=0x%" PRIx64 " width=4 accepted-profile\n",
                  offset, value);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_memc_ops = {
    .read_with_attrs = z7ii_memc_read,
    .write_with_attrs = z7ii_memc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* Dispatch bad widths/offsets too, so rejection records the guest PC. */
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_memc_reset(DeviceState *dev)
{
    Z7IIMemcState *s = Z7II_MEMC(dev);

    s->mode_initialized = false;
    s->timing_initialized = false;
    s->reported_error = false;
    /* No hardware reset value is supplied for either register. */
}

static void z7ii_memc_init(Object *obj)
{
    Z7IIMemcState *s = Z7II_MEMC(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_memc_ops, s,
                          "z7ii.memc", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_memc_vmstate = {
    .name = "z7ii-memc",
    .unmigratable = true,
};

static void z7ii_memc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_memc_vmstate;
    device_class_set_legacy_reset(dc, z7ii_memc_reset);
    dc->user_creatable = false;
}

static MemTxResult z7ii_clock_error(Z7IIClockGatesState *s, hwaddr offset,
                                   unsigned size, bool write, uint64_t value,
                                   const char *reason)
{
    z7ii_access_error(&s->reported_error, CLOCK_BASE + offset, size,
                      write, value, reason);
    return MEMTX_ERROR;
}

/* Six stop banks share SET(+0x100) and CLEAR(+0x200) mask aliases. */
static int z7ii_clock_bank(hwaddr offset, hwaddr first)
{
    if (offset < first || offset >= first + 6 * 4 || (offset & 3)) {
        return -1;
    }
    return (offset - first) / 4;
}

static void z7ii_pll_output(Z7IIClockChannel *channel)
{
    unsigned i = channel->index;
    uint32_t word = channel->owner->pll[z7ii_pll_group[i].reg];
    bool enabled = channel->locked &&
                   !(word & z7ii_pll_group[i].stop_mask) &&
                   !(word & z7ii_pll_group[i].output_mask);

    /* Independent nominal virtual rates, never inferred from multiplier 0x163. */
    clock_update_hz(channel->output, enabled ? TIMER_HZ : 0);
}

static void z7ii_pll_locked(void *opaque)
{
    Z7IIClockChannel *channel = opaque;

    channel->locked = true;
    z7ii_pll_output(channel);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-syscontrol: PLL group=%u modeled-lock nominal-Hz=%u\n",
                  channel->index, (unsigned)TIMER_HZ);
}

static void z7ii_pll_update(Z7IIClockGatesState *s, unsigned reg,
                           uint32_t before)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(z7ii_pll_group); i++) {
        Z7IIClockChannel *channel = &s->channel[i];
        uint32_t mask = z7ii_pll_group[i].stop_mask;

        if (z7ii_pll_group[i].reg != reg) {
            continue;
        }
        if (s->pll[reg] & mask) {
            timer_del(channel->lock_timer);
            channel->locked = false;
        } else if ((before & mask) ||
                   (!mask && (before & z7ii_pll_group[i].output_mask) &&
                    !(s->pll[reg] & z7ii_pll_group[i].output_mask))) {
            /* A 10us virtual lock interval is an explicit functional assumption.
             * No register is auto-cleared; output still follows guest gating. */
            channel->locked = false;
            timer_mod(channel->lock_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
        }
        z7ii_pll_output(channel);
    }
}

static MemTxResult z7ii_clock_read(void *opaque, hwaddr offset,
                                  uint64_t *value, unsigned size,
                                  MemTxAttrs attrs)
{
    Z7IIClockGatesState *s = opaque;
    int bank = z7ii_clock_bank(offset, 0x38);

    *value = 0; /* Invalid unless the complete word is known. */
    if (size == 4 && !(offset & 3)) {
        if (offset <= 0x24) {
            *value = s->selector[offset / 4];
            return MEMTX_OK;
        }
        if (offset >= 0x28 && offset <= 0x30) {
            *value = s->pll[(offset - 0x28) / 4];
            return MEMTX_OK;
        }
        if (offset == 0x34) {
            *value = s->word_control;
            return MEMTX_OK;
        }
        if (offset == 0x58 || offset == 0x5c) {
            *value = s->bit_control[(offset - 0x58) / 4];
            return MEMTX_OK;
        }
    }
    if (size != 4 || bank < 0) {
        return z7ii_clock_error(s, offset, size, false, 0,
                                "clock-access-not-supported");
    }
    if (s->known[bank] != UINT32_MAX) {
        return z7ii_clock_error(s, offset, size, false, 0,
                                "clock-stop-bits-uninitialized");
    }
    *value = s->stopped[bank];
    return MEMTX_OK;
}

static MemTxResult z7ii_clock_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size,
                                   MemTxAttrs attrs)
{
    Z7IIClockGatesState *s = opaque;
    int bank = z7ii_clock_bank(offset, 0x138);
    bool set = bank >= 0;
    uint32_t mask = value;

    if (size == 4 && !(offset & 3)) {
        static const uint32_t writable[] = {
            0x7ffffffe, 0xffff0073, 0x3f07ff07,
        };
        if (offset <= 0x24) {
            /* The original indexed API replaces the entire configuration word.
             * Rates remain explicit nominal inputs; selectors are not dividers. */
            s->selector[offset / 4] = value;
            return MEMTX_OK;
        }
        if (offset >= 0x28 && offset <= 0x30) {
            unsigned reg = (offset - 0x28) / 4;
            uint32_t before = s->pll[reg];
            if (value & ~writable[reg]) {
                return z7ii_clock_error(s, offset, size, true, value,
                                        "clock-control-fields-not-supported");
            }
            s->pll[reg] = value;
            z7ii_pll_update(s, reg, before);
            return MEMTX_OK;
        }
        if (offset == 0x34) {
            /*
             * Original O3908cc..912 performs full-word mask OR/BIC requests.
             * Zero reset and full32 configuration are virtual assumptions;
             * physical fields remain unknown, without invented rate/events.
             */
            s->word_control = value;
            return MEMTX_OK;
        }
        if (offset == 0x58 || offset == 0x5c) {
            /* Original O390914..960 is a mutex-protected word RMW request.
             * Physical roles remain unbound; no rate/ready effect is invented. */
            s->bit_control[(offset - 0x58) / 4] = value;
            return MEMTX_OK;
        }
    }

    if (!set) {
        bank = z7ii_clock_bank(offset, 0x238);
    }
    if (size != 4 || bank < 0) {
        return z7ii_clock_error(s, offset, size, true, value,
                                "clock-access-not-supported");
    }
    if (set) {
        s->stopped[bank] |= mask;
    } else {
        s->stopped[bank] &= ~mask;
    }
    s->known[bank] |= mask;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-clock-gates: bank=%d operation=%s mask=0x%08x "
                  "known=0x%08x\n", bank, set ? "set" : "clear", mask,
                  s->known[bank]);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_clock_ops = {
    .read_with_attrs = z7ii_clock_read,
    .write_with_attrs = z7ii_clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_clock_reset(DeviceState *dev)
{
    Z7IIClockGatesState *s = Z7II_CLOCK_GATES(dev);
    unsigned i;

    /* Canonical virtual-board assumptions, not measured hardware reset words. */
    memset(s->stopped, 0, sizeof(s->stopped));
    memset(s->known, 0xff, sizeof(s->known));
    memset(s->selector, 0, sizeof(s->selector));
    memset(s->bit_control, 0, sizeof(s->bit_control));
    s->word_control = 0;
    s->pll[0] = 0xffe;
    s->pll[1] = 0x73;
    s->pll[2] = 3;
    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        timer_del(s->channel[i].lock_timer);
        s->channel[i].locked = false;
        clock_update_hz(s->channel[i].output, 0);
    }
    s->reported_error = false;
}

static void z7ii_clock_init(Object *obj)
{
    Z7IIClockGatesState *s = Z7II_CLOCK_GATES(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        g_autofree char *name = g_strdup_printf("pll%u", i);
        s->channel[i].owner = s;
        s->channel[i].index = i;
        s->channel[i].output = qdev_init_clock_out(DEVICE(obj), name);
        s->channel[i].lock_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                               z7ii_pll_locked, &s->channel[i]);
    }
    memory_region_init_io(&s->regs, obj, &z7ii_clock_ops, s,
                          "z7ii.clock-gates", 0x250);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void z7ii_clock_finalize(Object *obj)
{
    Z7IIClockGatesState *s = Z7II_CLOCK_GATES(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        timer_free(s->channel[i].lock_timer);
    }
}

static const VMStateDescription z7ii_clock_vmstate = {
    .name = "z7ii-clock",
    .unmigratable = true,
};

static void z7ii_clock_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_clock_vmstate;
    device_class_set_legacy_reset(dc, z7ii_clock_reset);
    dc->user_creatable = false;
}

static MemTxResult z7ii_completion_error(Z7IICompletionState *s, hwaddr offset,
                                        unsigned size, bool write,
                                        uint64_t value, const char *reason)
{
    z7ii_access_error(&s->reported_error, COMPLETION_BASE + offset, size,
                      write, value, reason);
    return MEMTX_ERROR;
}

static void z7ii_completion_irq(Z7IICompletionState *s)
{
    qemu_set_irq(s->irq, s->irq_enabled && s->done);
}

static void z7ii_completion_apply(void *opaque)
{
    Z7IICompletionState *s = opaque;
    uint32_t bit = 1U << ((s->request >> 12) & 7);

    if (!s->pending) {
        return;
    }
    /* Functional request effects; physical domains and latency are unknown. */
    if (s->request & 0x100) {
        s->applied_domains |= bit;
    } else {
        s->applied_domains &= ~bit;
    }
    s->pending = false;
    s->done = true;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-completion: deferred request=0x%08x applied=0x%08x "
                  "done=1\n", s->request, s->applied_domains);
    z7ii_completion_irq(s);
}

static MemTxResult z7ii_completion_read(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs)
{
    Z7IICompletionState *s = opaque;

    *value = 0;
    if (size != 4 ||
        (offset != 0 && offset != 4 && offset != 8 && offset != 12)) {
        return z7ii_completion_error(s, offset, size, false, 0,
                                     "completion-access-not-supported");
    }
    if (offset == 12) {
        *value = s->applied_domains;
        return MEMTX_OK;
    }
    *value = offset == 0 ? s->request :
             offset == 4 ? s->done : s->irq_enabled;
    return MEMTX_OK;
}

static MemTxResult z7ii_completion_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    Z7IICompletionState *s = opaque;
    unsigned domain = (value >> 12) & 7;

    if (size != 4 ||
        (offset != 0 && offset != 4 && offset != 8 && offset != 16)) {
        return z7ii_completion_error(s, offset, size, true, value,
                                     "completion-access-not-supported");
    }
    if (s->pending) {
        return z7ii_completion_error(
            s, offset, size, true, value,
            "completion-concurrent-request-not-supported");
    }
    switch (offset) {
    case 0:
        /* Observed domain and request-mode profiles, not full IP legality. */
        if ((value & ~0x7110U) ||
            (domain != 0 && domain != 1 && domain != 3)) {
            return z7ii_completion_error(
                s, offset, size, true, value,
                "completion-request-profile-not-supported");
        }
        s->request = value;
        break;
    case 4:
        if (value != 1) {
            return z7ii_completion_error(s, offset, size, true, value,
                                         "completion-ack-not-supported");
        }
        /* W1C diagnostic binding; no host RTOS flag mutation. */
        s->done = false;
        z7ii_completion_irq(s);
        break;
    case 8:
        if (value > 1) {
            return z7ii_completion_error(
                s, offset, size, true, value,
                "completion-irq-profile-not-supported");
        }
        s->irq_enabled = value;
        z7ii_completion_irq(s);
        break;
    case 16:
        if (value != 0xa1) {
            return z7ii_completion_error(s, offset, size, true, value,
                                         "completion-command-not-supported");
        }
        s->pending = true;
        s->done = false;
        z7ii_completion_irq(s);
        qemu_bh_schedule(s->bh);
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_completion_ops = {
    .read_with_attrs = z7ii_completion_read,
    .write_with_attrs = z7ii_completion_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_completion_reset(DeviceState *dev)
{
    Z7IICompletionState *s = Z7II_COMPLETION(dev);

    qemu_bh_cancel(s->bh);
    s->pending = false;
    /* Coherent virtual domain-off/reset profile, not a measured Nikon reset. */
    s->request = 0;
    s->irq_enabled = false;
    s->done = false;
    s->applied_domains = 0;
    s->reported_error = false;
    z7ii_completion_irq(s);
}

static void z7ii_completion_init(Object *obj)
{
    Z7IICompletionState *s = Z7II_COMPLETION(obj);

    s->bh = qemu_bh_new(z7ii_completion_apply, s);
    memory_region_init_io(&s->regs, obj, &z7ii_completion_ops, s,
                          "z7ii.completion", 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void z7ii_completion_finalize(Object *obj)
{
    Z7IICompletionState *s = Z7II_COMPLETION(obj);

    qemu_bh_delete(s->bh);
}

static const VMStateDescription z7ii_completion_vmstate = {
    .name = "z7ii-completion",
    .unmigratable = true,
};

static void z7ii_completion_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_completion_vmstate;
    device_class_set_legacy_reset(dc, z7ii_completion_reset);
    dc->user_creatable = false;
}

static MemTxResult z7ii_nor_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    Z7IIMemcState *memc = &s->memc;
    const char *reason;

    if (!memc->mode_initialized || !memc->timing_initialized) {
        reason = "nor-memc-not-initialized";
    } else if (memc->mode & 8) {
        reason = "nor-write-protected"; /* WEOFF closes external WEX. */
    } else if (size != 2 || (offset & 1)) {
        reason = "nor-x16-access-not-supported";
    } else {
        /* x16/PAGE selects this backend; NOR command writes remain absent. */
        reason = "nor-backend-not-supported";
    }
    return z7ii_memc_error(memc, MAS_BASE + offset, size, true, value, reason);
}

static MemTxResult z7ii_nor_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;

    /* Normal ROMD reads bypass this callback and read the original bytes. */
    *value = 0;
    return z7ii_memc_error(&s->memc, MAS_BASE + offset, size, false, 0,
                           "nor-indirect-read-not-supported");
}

static const MemoryRegionOps z7ii_nor_ops = {
    .read_with_attrs = z7ii_nor_read,
    .write_with_attrs = z7ii_nor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static MemTxResult z7ii_nv_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    /* Same MEMC protection and unsupported-command contract, correct PA. */
    return z7ii_nor_write(slot->machine, slot->base - MAS_BASE + offset,
                         value, size, attrs);
}

static MemTxResult z7ii_nv_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    return z7ii_nor_read(slot->machine, slot->base - MAS_BASE + offset,
                        value, size, attrs);
}

static const MemoryRegionOps z7ii_nv_ops = {
    .read_with_attrs = z7ii_nv_read,
    .write_with_attrs = z7ii_nv_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_reset(void *opaque)
{
    Z7IIMachineState *s = opaque;
    unsigned i;

    s->reported_unknown = false;
    /* Even entry means ARM state. No firmware instruction is changed. */
    for (i = 0; i < s->num_cpus; i++) {
        cpu_reset(CPU(&s->cpu[i]));
        cpu_set_pc(CPU(&s->cpu[i]), MAS_BASE);
    }
}

static void z7ii_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, uint64_t size)
{
    /* Migratable board RAM uses NULL; its owner must not be a MachineState. */
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void z7ii_erased_slot(Z7IIMachineState *s, unsigned index,
                             const char *name, hwaddr base, uint64_t size)
{
    Z7IINorSlot *slot = &s->nv_slot[index];

    slot->machine = s;
    slot->base = base;
    memory_region_init_rom_device(&slot->region, NULL, &z7ii_nv_ops, slot,
                                  name, size, &error_fatal);
    memset(memory_region_get_ram_ptr(&slot->region), 0xff, size);
    memory_region_add_subregion(get_system_memory(), base, &slot->region);
}

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    DeviceState *gic;
    DeviceState *mxic;
    DeviceState *gpio;
    DeviceState *exiu;
    SysBusDevice *gic_bus;
    int64_t image_size;
    unsigned i;
    static const hwaddr stack_top[] = {
        0xb2bb0000, 0xb2ba8000, 0xb2ba0000,
        0xb2b98000, 0xb2b90000, 0xb2b88000,
    };

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7"))) {
        error_report("z7ii-mas-diagnostic currently requires Cortex-A7");
        exit(EXIT_FAILURE);
    }
    if (!machine->firmware) {
        error_report("z7ii-mas-diagnostic requires -bios <raw MAS payload>");
        exit(EXIT_FAILURE);
    }
    image_size = get_image_size(machine->firmware, &error_fatal);
    if (image_size <= 0 || (uint64_t)image_size > MAS_MAX_SIZE) {
        error_report("MAS diagnostic image must contain 1..%llu bytes",
                     (unsigned long long)MAS_MAX_SIZE);
        exit(EXIT_FAILURE);
    }

    /* Reject all uncovered addresses, including missing RAM and devices. */
    memory_region_init_io(&s->unknown, OBJECT(s), &z7ii_unknown_ops, s,
                          "z7ii.uncovered", 1ULL << 32);
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        &s->unknown, -1);

    /* Candidate VA placement is a diagnostic physical placement assumption. */
    /* ROMD preserves direct reads but sends all guest writes through MEMC. */
    memory_region_init_rom_device(&s->image, NULL, &z7ii_nor_ops, s,
                                  "z7ii.mas", image_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), MAS_BASE, &s->image);
    if (load_image_targphys(machine->firmware, MAS_BASE, image_size,
                            &error_fatal) != image_size) {
        error_report("failed to load the complete MAS diagnostic image");
        exit(EXIT_FAILURE);
    }
    /*
     * Union of the original external descriptor views3,5..32 and six
     * alternatives, fixed source f75cf38b. Only adjacent/overlapping covered
     * ranges coalesce. Descriptor0/1/2 and zero-span4 are excluded; actual
     * MAS ROM and every uncovered hole retain their prior ownership.
     * Each physical byte has one shared ROMD cell, including contained and
     * aliased views. FF is a named external input, not recovered valid NVM.
     */
    {
        static const struct {
            hwaddr start;
            uint64_t size;
        } external[] = {
            { 0x12960000, 0x8e0000 },
            { 0x132c0000, 0x0c0000 },
            { 0x133a0000, 0x160000 },
            { 0x13540000, 0x900000 },
            { 0x13e80000, 0x180000 },
        };

        for (i = 0; i < ARRAY_SIZE(external); i++) {
            g_autofree char *name = g_strdup_printf("z7ii.external-nor%u", i);

            z7ii_erased_slot(s, i, name, external[i].start, external[i].size);
        }
    }

    /* Static MAS copy contracts: destinations only; guest performs the copy. */
    z7ii_ram(&s->copy_ram[0], "z7ii.copy0", 0xb1800000, 0xaedb8);
    z7ii_ram(&s->copy_ram[1], "z7ii.copy1", 0xb2bb0000, 0x105c);
    /* Explicit byte-block fill profile: count 0x4ac * width 0x4000. */
    /* Preserve RAM/guest page-offset congruence without mapping padding. */
    memory_region_init_ram(&s->bss_storage, NULL, "z7ii.fill-bss-storage",
                           0x12b0dc0, &error_fatal);
    memory_region_init_alias(&s->bss_ram, NULL, "z7ii.fill-bss",
                             &s->bss_storage, 0xdc0, 0x12b0000);
    memory_region_add_subregion(get_system_memory(), 0xb18aedc0, &s->bss_ram);
    /* Original descriptor lowmem bounds, not a physical DDR capacity. */
    z7ii_ram(&s->kernel_arena, "z7ii.kernel-lowmem", 0xb2c00000, 0x3300000);
    /*
     * Original address-class initializer declares [baa00000,bb300000).
     * This finite virtual capacity includes the earlier kernel destination.
     * The guest owns code copies, aliases and PTE updates; no source is seeded.
     */
    z7ii_ram(&s->relocated_code, "z7ii.relocated-code", 0xbaa00000, 0x900000);
    /*
     * Four original bump requests observed from reset: 0x3a0000, 0x31f28,
     * 6 and 0xa410 bytes, each aligned to 64 bytes by the guest allocator.
     * Their contiguous union is finite virtual backing, not heap capacity.
     * The guest owns all copies, allocation pointers and object metadata.
     */
    z7ii_ram(&s->module_buffer, "z7ii.module-buffer", 0xb7d00000, 0x3dc3c0);
    /*
     * Original address-class pair: [14000000,15e00000) <-> [b5f00000,b7d00000).
     * This declared 30MiB virtual storage includes the original message ring.
     * Cold zero is a functional assumption, not physical heap capacity.
     * CPU logical views use the guest MMU, without a new physical bus alias.
     * Guest objects, ring indices and signatures are untouched.
     */
    z7ii_ram(&s->private_class, "z7ii.private-class", 0xb5f00000, 0x1e00000);
    z7ii_ram(&s->table_ram, "z7ii.table", 0xb2b74000, 0x4000);
    /* O394 + O4c6c + O5bd0/O5f40 require 8 + 40 + 40 bytes. */
    z7ii_ram(&s->temporary_stack, "z7ii.temporary-stack", 0x04008ea8, 0x58);
    /* CPU0 O0xd848 produces the magic; secondary WFE/read/clear is guest code. */
    z7ii_ram(&s->early_release, "z7ii.early-release", 0x04008ffc, 4);
    /* Each guest SP is top - (MPIDR selector << 14). The 16KiB backing
     * per mode/core is a declared virtual capacity, not proven by that stride. */
    for (i = 0; i < machine->smp.cpus * ARRAY_SIZE(stack_top); i++) {
        unsigned core = i / ARRAY_SIZE(stack_top);
        unsigned mode = i % ARRAY_SIZE(stack_top);
        g_autofree char *name = g_strdup_printf("z7ii.cpu%u-stack%u", core, mode);
        z7ii_ram(&s->stack_ram[i], name,
                 stack_top[mode] - (core + 1) * STACK_SLOT_SIZE, STACK_SLOT_SIZE);
    }

    {
        DeviceState *config = qdev_new(TYPE_Z7II_MXIC_CONFIG);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(config), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 0, 0x20010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 1, 0x24074000);
    }
    mxic = sysbus_create_simple(TYPE_Z7II_MXIC, 0x20016000, NULL);
    z7ii_mxic_attach_ram(mxic, &s->copy_ram[0], 0xb1800000);
    z7ii_mxic_attach_ram(mxic, &s->copy_ram[1], 0xb2bb0000);
    z7ii_mxic_attach_ram(mxic, &s->bss_ram, 0xb18aedc0);
    z7ii_mxic_attach_ram(mxic, &s->kernel_arena, 0xb2c00000);
    z7ii_mxic_attach_ram(mxic, &s->relocated_code, 0xbaa00000);
    z7ii_mxic_attach_ram(mxic, &s->module_buffer, 0xb7d00000);
    z7ii_mxic_attach_ram(mxic, &s->private_class, 0xb5f00000);
    z7ii_mxic_attach_ram(mxic, &s->table_ram, 0xb2b74000);
    for (i = 0; i < machine->smp.cpus * ARRAY_SIZE(stack_top); i++) {
        z7ii_mxic_attach_ram(mxic, &s->stack_ram[i],
                            stack_top[i % ARRAY_SIZE(stack_top)] -
                            (i / ARRAY_SIZE(stack_top) + 1) * STACK_SLOT_SIZE);
    }

    sysbus_create_simple(TYPE_Z7II_FILL, 0x20070000, NULL);

    s->num_cpus = machine->smp.cpus;
    for (i = 0; i < s->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%u", i);
        object_initialize_child(OBJECT(s), name, &s->cpu[i], machine->cpu_type);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "reset-cbar", CBAR_BASE);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "cntfrq", TIMER_HZ);
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    object_initialize_child(OBJECT(s), "memc", &s->memc, TYPE_Z7II_MEMC);
    sysbus_realize(SYS_BUS_DEVICE(&s->memc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->memc), 0, MEMC_BASE);

    object_initialize_child(OBJECT(s), "clock-gates", &s->clock_gates,
                            TYPE_Z7II_CLOCK_GATES);
    sysbus_realize(SYS_BUS_DEVICE(&s->clock_gates), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clock_gates), 0, CLOCK_BASE);

    sysbus_create_simple("z7ii-timestamp-functional", 0x04266000, NULL);
    sysbus_create_simple(TYPE_Z7II_SYSCONFIG, 0x04280000, NULL);
    gpio = sysbus_create_simple(TYPE_Z7II_GPIO, 0x04282000, NULL);
    {
        DeviceState *usio = qdev_new(TYPE_Z7II_USIO);

        if (serial_hd(0)) {
            qdev_prop_set_chr(usio, "chardev", serial_hd(0));
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(usio), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(usio), 0, 0x2c090000);
    }
    for (i = 0; i < 2; i++) {
        DeviceState *sdi = qdev_new(TYPE_Z7II_SDI);

        qdev_prop_set_uint32(sdi, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(sdi), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(sdi), 0, 0x20020000 + i * 0x20000);
    }

    /* Whole upstream GIC, independently configured from the CPU's EL2. */
    gic = qdev_new(TYPE_ARM_GIC);
    gic_bus = SYS_BUS_DEVICE(gic);
    qdev_prop_set_uint32(gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(gic, "num-irq", 256); /* SGI/PPI + 224 SPIs. */
    /* Original ISACTIVER initialization relies on the pre-v2 read-only view. */
    qdev_prop_set_uint32(gic, "revision", 1);
    qdev_prop_set_bit(gic, "has-security-extensions", true);
    qdev_prop_set_bit(gic, "has-virtualization-extensions", false);
    sysbus_realize_and_unref(gic_bus, &error_fatal);
    sysbus_mmio_map(gic_bus, 0, CBAR_BASE + 0x1000);
    sysbus_mmio_map(gic_bus, 1, CBAR_BASE + 0x2000);
    sysbus_create_simple(TYPE_Z7II_BRIDGE, 0x20130000,
                         qdev_get_gpio_in(gic, 127 - 32));
    sysbus_create_simple(TYPE_Z7II_IPUD, 0x24020000,
                         qdev_get_gpio_in(gic, 101 - 32));
    sysbus_create_simple(TYPE_Z7II_CONTROL20202, 0x20202000,
                         qdev_get_gpio_in(gic, 123 - 32));
    exiu = sysbus_create_simple(TYPE_Z7II_EXIU, 0x0426f000, NULL);
    /* Original 16 EXIU objects map GPIO5[7:15]/GPIO6[0:6] to IRQ65..80. */
    for (i = 0; i < 16; i++) {
        unsigned pad = i < 9 ? 5 * 16 + 7 + i : 6 * 16 + i - 9;

        qdev_connect_gpio_out_named(gpio, "pad", pad,
                                    qdev_get_gpio_in(exiu, i));
        sysbus_connect_irq(SYS_BUS_DEVICE(exiu), i,
                           qdev_get_gpio_in(gic, 65 + i - 32));
    }
    for (i = 0; i < 11; i++) {
        DeviceState *spi = qdev_new(TYPE_Z7II_SPI);

        qdev_prop_set_uint32(spi, "channel", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(spi), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(spi), 0, 0x2c0a0000 + i * 0x100);
        /*
         * Both original wrappers enter one ISR; common-pending routing is
         * a virtual-board assumption, not a measured TX/RX IRQ identity.
         */
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 0,
                           qdev_get_gpio_in(gic, 193 + i * 2 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 1,
                           qdev_get_gpio_in(gic, 192 + i * 2 - 32));
    }
    for (i = 0; i < 4; i++) {
        DeviceState *i2c = qdev_new(TYPE_Z7II_I2C);

        qdev_prop_set_uint32(i2c, "instance", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(i2c), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(i2c), 0, 0x2c018000 + i * 0x1000);
        sysbus_connect_irq(SYS_BUS_DEVICE(i2c), 0,
                           qdev_get_gpio_in(gic, 214 + i - 32));
    }
    for (i = 0; i < 2; i++) {
        DeviceState *hdmac = qdev_new(TYPE_Z7II_HDMAC);
        unsigned channel;

        qdev_prop_set_uint32(hdmac, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(hdmac), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(hdmac), 0, 0x2c020000 + i * 0x10000);
        for (channel = 0; channel < 8; channel++) {
            unsigned irq = (i ? 142 : 150) + channel;

            sysbus_connect_irq(SYS_BUS_DEVICE(hdmac), channel,
                               qdev_get_gpio_in(gic, irq - 32));
        }
        if (i == 1) {
            DeviceState *audio = qdev_new(TYPE_Z7II_AUDIO);

            sysbus_realize_and_unref(SYS_BUS_DEVICE(audio), &error_fatal);
            sysbus_mmio_map(SYS_BUS_DEVICE(audio), 0, 0x2c080000);
            sysbus_connect_irq(SYS_BUS_DEVICE(audio), 0,
                               qdev_get_gpio_in(gic, 167 - 32));
            sysbus_connect_irq(SYS_BUS_DEVICE(audio), 1,
                               qdev_get_gpio_in(gic, 168 - 32));
            /* Original logical DMA IDs132..139 select bank1 dynamically. */
            qdev_connect_gpio_out(audio, 0, qdev_get_gpio_in(hdmac, 3));
            qdev_connect_gpio_out(audio, 1, qdev_get_gpio_in(hdmac, 7));
            qdev_connect_gpio_out(audio, 2, qdev_get_gpio_in(hdmac, 5));
        }
    }
    {
        DeviceState *timing = qdev_new(TYPE_Z7II_TIMING);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(timing), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(timing), 0, 0x2c010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(timing), 1, 0x2c011000);
        for (i = 0; i < 12; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(timing), i,
                               qdev_get_gpio_in(gic, 172 + i - 32));
        }
    }
    {
        DeviceState *copy = qdev_new(TYPE_Z7II_COPY);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(copy), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(copy), 0, 0x04251000);
        for (i = 0; i < 4; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(copy), i,
                               qdev_get_gpio_in(gic, 45 + i - 32));
        }
    }
    {
        DeviceState *signal = qdev_new(TYPE_Z7II_SIGNAL);
        DeviceState *aggregate = qdev_new(TYPE_OR_IRQ);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(signal), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(signal), 0, 0x2b000000);
        sysbus_mmio_map(SYS_BUS_DEVICE(signal), 1, 0x28000000);
        qdev_prop_set_uint16(aggregate, "num-lines", 5);
        qdev_realize_and_unref(aggregate, NULL, &error_fatal);
        for (i = 0; i < 5; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(signal), i,
                               qdev_get_gpio_in(aggregate, i));
        }
        /* Original ISR registration supplies the single shared IRQ237. */
        qdev_connect_gpio_out(aggregate, 0,
                             qdev_get_gpio_in(gic, 237 - 32));
    }
    object_initialize_child(OBJECT(s), "completion", &s->completion,
                            TYPE_Z7II_COMPLETION);
    sysbus_realize(SYS_BUS_DEVICE(&s->completion), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->completion), 0, COMPLETION_BASE);
    /* The guest registers/enables IRQ250; this is its external SPI input. */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->completion), 0,
                       qdev_get_gpio_in(gic, 250 - 32));
    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpu = DEVICE(&s->cpu[i]);
        const int timer_irq[] = {
            [GTIMER_PHYS] = 30, [GTIMER_VIRT] = 27,
            [GTIMER_HYP] = 26, [GTIMER_SEC] = 29,
        };
        int irq;

        /* Same upstream A15MPCore timer/PPI array layout, no invented tick. */
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpu, irq,
                qdev_get_gpio_in(gic, 256 - 32 + i * 32 + timer_irq[irq]));
        }
        sysbus_connect_irq(gic_bus, i, qdev_get_gpio_in(cpu, ARM_CPU_IRQ));
        sysbus_connect_irq(gic_bus, i + s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_FIQ));
        sysbus_connect_irq(gic_bus, i + 2 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VIRQ));
        sysbus_connect_irq(gic_bus, i + 3 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VFIQ));
    }
    qemu_register_reset(z7ii_reset, s);
    error_report("z7ii-mas-diagnostic: assumptions cpu=%s cpus=%u "
                 "image-base=0x10020000 entry=0x10020000 ARM "
                 "image-bytes=%" PRId64 " per-core-mode-stack-slot=0x4000; "
                 "MEMC=0x04264000 profile-only CBAR=0xf8100000 GICv1=256 "
                 "cntfrq=48000000 secure-timer-PPI=29 early-release-RAM=4B; "
                 "six modes/core, virtual stack capacity; "
                 "no NOR writer/IPC devices/BootROM",
                 machine->cpu_type, s->num_cpus, image_size);
}

static void z7ii_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Z7II unchanged MAS diagnostic (hypothetical Cortex-A7/MEMC)";
    mc->init = z7ii_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_cpus = 2; /* Original TSysPrNum/TKerPrNum defaults are two. */
    mc->min_cpus = 1; /* Small synthetic contracts explicitly use -smp 1. */
    mc->max_cpus = Z7II_MAX_CPUS;
    mc->default_ram_size = 0;
    mc->ignore_memory_transaction_failures = false;
}

static const TypeInfo z7ii_machine_type = {
    .name = TYPE_Z7II_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Z7IIMachineState),
    .class_init = z7ii_class_init,
    .interfaces = arm_machine_interfaces,
};

static void z7ii_register_types(void)
{
    static const TypeInfo memc_type = {
        .name = TYPE_Z7II_MEMC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Z7IIMemcState),
        .instance_init = z7ii_memc_init,
        .class_init = z7ii_memc_class_init,
    };
    static const TypeInfo clock_type = {
        .name = TYPE_Z7II_CLOCK_GATES,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Z7IIClockGatesState),
        .instance_init = z7ii_clock_init,
        .instance_finalize = z7ii_clock_finalize,
        .class_init = z7ii_clock_class_init,
    };
    static const TypeInfo completion_type = {
        .name = TYPE_Z7II_COMPLETION,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Z7IICompletionState),
        .instance_init = z7ii_completion_init,
        .instance_finalize = z7ii_completion_finalize,
        .class_init = z7ii_completion_class_init,
    };
    type_register_static(&memc_type);
    type_register_static(&clock_type);
    type_register_static(&completion_type);
    type_register_static(&z7ii_machine_type);
}
type_init(z7ii_register_types)
