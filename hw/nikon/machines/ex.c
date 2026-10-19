/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon EX software functional profile.
 *
 * The complete component supplies its own vector/reset/scatter initialization.
 * Cortex-M4 and the STM32F4 peripheral protocol are software profile choices,
 * not an assertion of the unidentified Nikon board's part number.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/or-irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/machines-qom.h"
#include "hw/char/stm32f2xx_usart.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/exboot.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/exio.h"
#include "hw/nikon/expower.h"
#include "hw/nikon/extimer.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "system/qtest.h"
#include "target/arm/cpu.h"

#define EX_FLASH_BASE 0x08000000
#define EX_FLASH_SIZE 0x80000
#define EX_RAM_BASE 0x20000000
#define EX_RAM_SIZE 0x40000
#define EX_RCC_BASE 0x40023800
#define EX_HSI_HZ 16000000
#define EX_HSE_HZ 10000000
#define EX_OSC_DELAY_NS 100000

#define TYPE_EX_SPI_PORT "nikon-ex-spi-port"
typedef struct EXSPI {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    NikonEXState *processor;
    unsigned channel;
    uint32_t reg[9];
    uint32_t txdata;
    bool tx_valid;
    bool ovr_dr_seen;
    SSIBus *bus;
} EXSPI;

typedef struct EXI2C {
    uint32_t reg[9];
    bool sr1_seen;
    bool receive;
    uint8_t rx[2];
    unsigned rx_count;
    unsigned rx_remaining;
    I2CBus *bus;
    NikonEXState *processor;
    unsigned channel;
    QEMUTimer *rx_timer;
} EXI2C;

#define EX_I2C_RX_UNBOUNDED UINT_MAX

/* One SCL byte at 100 kHz; used until the guest programs CCR. */
#define EX_I2C_DEFAULT_BYTE_NS 90000

static bool ex_i2c_rx_open(const EXI2C *p)
{
    return p->rx_remaining == EX_I2C_RX_UNBOUNDED || p->rx_remaining;
}

typedef struct EXDMAStream {
    uint32_t reg[6];
    uint32_t initial_count;
    uint32_t initial_memory;
} EXDMAStream;

typedef struct EXDMA {
    uint32_t status[2];
    EXDMAStream stream[8];
} EXDMA;

typedef struct EXADC {
    uint32_t reg[20];
    uint16_t sample[19];
    uint32_t valid;
} EXADC;

typedef struct EXPinRoute {
    NikonEXState *soc;
    unsigned port;
    unsigned pin;
} EXPinRoute;

struct NikonEXState {
    SysBusDevice parent_obj;
    ARMv7MState armv7m;
    MemoryRegion memory;
    MemoryRegion system_view;
    MemoryRegion flash;
    MemoryRegion flash_alias;
    MemoryRegion ram;
    MemoryRegion peripheral;
    MemoryRegion gpio_window[9];
    AddressSpace dma_as;
    Clock *cpuclk;
    Clock *refclk;
    Clock *timerclk[14];
    DeviceState *gpio[9];
    DeviceState *uart[6];
    DeviceState *timer[14];
    DeviceState *io;
    OrIRQState timer_or[6];
    EXPinRoute route[9][16];
    qemu_irq pin_route_irq[9][16];
    qemu_irq pad_output[9 * 16];
    GBytes *firmware;
    uint32_t rcc[38];
    uint32_t pwr[2];
    uint32_t flashreg[6];
    uint32_t syscfg[9];
    uint32_t exti[6];
    uint16_t input[9];
    uint16_t external_mask[9];
    uint16_t external_level[9];
    uint16_t resetting_ports;
    uint16_t exti_level;
    EXSPI spi[4];
    EXI2C i2c[3];
    EXDMA dma[2];
    EXADC adc[3];
    uint32_t adc_common;
    uint32_t rtc[40];
    unsigned rtc_key;
    bool rtc_unlocked;
    bool rtc_initialized;
    QEMUTimer *rtc_sync;
    QEMUTimer *rtc_tick;
    QEMUTimer *rtc_wakeup;
    QEMUTimer *osc_timer;
    QEMUBH *reset_bh;
    QEMUBH *adc_bh;
    QEMUTimer *exception_poll;
    bool first_exception_valid;
    uint32_t first_exception_index;
    uint32_t first_exception_pc;
    uint32_t first_exception_cpsr;
    uint32_t first_exception_number;
    uint32_t first_exception_cfsr;
    uint32_t first_exception_dfsr;
    uint32_t first_exception_mmfar;
    uint32_t first_exception_bfar;
    uint32_t first_exception_irq_state;
    uint32_t first_exception_secure;
    uint32_t first_exception_fsr;
    uint32_t first_exception_fault_address;
    qemu_irq reset_irq;
    bool dma_running;
};

static const uint32_t ex_spi_base[] = { 0x40013000, 0x40003800, 0x40003c00,
                                      0x40013400 };
static const int ex_spi_irq[] = { 35, 36, 51, 84 };
static const uint32_t ex_i2c_base[] = { 0x40005400, 0x40005800, 0x40005c00 };
static const int ex_i2c_irq[] = { 31, 33, 72 };
static const int ex_dma_irq[2][8] = {
    { 11, 12, 13, 14, 15, 16, 17, 47 },
    { 56, 57, 58, 59, 60, 68, 69, 70 },
};
static const unsigned ex_dma_shift[] = { 0, 6, 16, 22 };

MemoryRegion *nikon_ex_memory(NikonEXState *s)
{
    return &s->memory;
}

I2CBus *nikon_ex_i2c_bus(NikonEXState *s, unsigned channel)
{
    assert(channel < ARRAY_SIZE(s->i2c));
    return s->i2c[channel].bus;
}

DeviceState *nikon_ex_io(NikonEXState *s)
{
    return s->io;
}

SSIBus *nikon_ex_spi_bus(NikonEXState *s, unsigned channel)
{
    assert(channel < ARRAY_SIZE(s->spi));
    return s->spi[channel].bus;
}

qemu_irq nikon_ex_gpio_input(NikonEXState *s, unsigned port, unsigned pin)
{
    assert(port < ARRAY_SIZE(s->gpio) && pin < 16);
    return qdev_get_gpio_in_named(DEVICE(s), "input", port * 16 + pin);
}

void nikon_ex_gpio_connect_output(NikonEXState *s, unsigned port, unsigned pin,
                                  qemu_irq sink)
{
    assert(port < 9 && pin < 16);
    qdev_connect_gpio_out_named(DEVICE(s), "pad", port * 16 + pin, sink);
}

bool nikon_ex_set_firmware(NikonEXState *s, GBytes *component, Error **errp)
{
    gsize size;
    const uint8_t *data = g_bytes_get_data(component, &size);
    uint32_t sp, pc;

    if (DEVICE(s)->realized || s->firmware) {
        error_setg(errp, "EX firmware must be supplied once before realization");
        return false;
    }
    if (size != EX_FLASH_SIZE) {
        error_setg(errp, "EX component must contain the complete 512 KiB image");
        return false;
    }
    sp = ldl_le_p(data);
    pc = ldl_le_p(data + 4);
    if (sp <= EX_RAM_BASE || sp > EX_RAM_BASE + EX_RAM_SIZE || (sp & 3) ||
        !(pc & 1) || (pc & ~1u) < EX_FLASH_BASE ||
        (pc & ~1u) >= EX_FLASH_BASE + EX_FLASH_SIZE) {
        error_setg(errp, "EX component has invalid SRAM/Thumb reset vectors");
        return false;
    }
    s->firmware = g_bytes_ref(component);
    return true;
}

static void ex_irq(NikonEXState *s, unsigned irq, bool level)
{
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->armv7m), irq), level);
}

static void ex_exti_update(NikonEXState *s)
{
    uint32_t pending = s->exti[0] & s->exti[5];
    for (unsigned i = 0; i < 5; i++) {
        ex_irq(s, 6 + i, pending & (1 << i));
    }
    ex_irq(s, 23, pending & 0x03e0);
    ex_irq(s, 40, pending & 0xfc00);
    ex_irq(s, 41, pending & (1 << 17));
    ex_irq(s, 3, pending & (1 << 22));
}

static unsigned ex_rtc_hz(NikonEXState *s)
{
    uint32_t bdcr = s->rcc[0x70 / 4];
    if (!(bdcr & (1 << 15)) || (bdcr & (1 << 16))) {
        return 0;
    }
    switch (extract32(bdcr, 8, 2)) {
    case 1: return bdcr & 2 ? 32768 : 0;
    case 2: return s->rcc[0x74 / 4] & 2 ? 32000 : 0;
    case 3: return s->rcc[0] & (1 << 17) ? EX_HSE_HZ / 8 : 0;
    default: return 0;
    }
}

static uint64_t ex_rtc_period(NikonEXState *s)
{
    unsigned hz = ex_rtc_hz(s);
    uint32_t pre = s->rtc[4];
    return hz ? (uint64_t)((pre & 0x7fff) + 1) *
        (extract32(pre, 16, 7) + 1) * NANOSECONDS_PER_SECOND / hz : 0;
}

static void ex_rtc_schedule(NikonEXState *s)
{
    unsigned hz = ex_rtc_hz(s);
    uint64_t period = ex_rtc_period(s);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (hz && !timer_pending(s->rtc_sync)) {
        timer_mod(s->rtc_sync, now + 2 * NANOSECONDS_PER_SECOND / hz);
    }
    if (period && !(s->rtc[3] & (1 << 7))) {
        if (!timer_pending(s->rtc_tick)) {
            timer_mod(s->rtc_tick, now + period);
        }
    } else {
        timer_del(s->rtc_tick);
    }
    if (hz && (s->rtc[2] & (1 << 10))) {
        unsigned sel = s->rtc[2] & 7;
        uint64_t count = (s->rtc[5] & 0xffff) + 1;
        uint64_t wake_period;
        if (sel < 4) {
            wake_period = count * (16 >> sel) * NANOSECONDS_PER_SECOND / hz;
        } else {
            count += sel >= 6 ? 65536 : 0;
            wake_period = count * period;
        }
        if (!timer_pending(s->rtc_wakeup)) {
            timer_mod(s->rtc_wakeup, now + wake_period);
        }
    } else {
        timer_del(s->rtc_wakeup);
    }
}

static void ex_rtc_sync(void *opaque)
{
    NikonEXState *s = opaque;
    if (!ex_rtc_hz(s)) {
        return;
    }
    s->rtc[3] = deposit32(s->rtc[3], 6, 1, !!(s->rtc[3] & (1 << 7)));
    if (!(s->rtc[3] & (1 << 7))) {
        s->rtc[3] |= 1 << 5; /* actual calendar-to-shadow synchronization */
    }
    s->rtc[3] = (s->rtc[3] & ~7u) |
        (!(s->rtc[2] & (1 << 8))) |
        (!(s->rtc[2] & (1 << 9)) << 1) |
        (!(s->rtc[2] & (1 << 10)) << 2);
}

static unsigned ex_bcd_decode(unsigned value)
{
    return (value >> 4) * 10 + (value & 15);
}

static unsigned ex_bcd_encode(unsigned value)
{
    return (value / 10 << 4) | (value % 10);
}

static bool ex_rtc_alarm(NikonEXState *s, unsigned alarm)
{
    uint32_t value = s->rtc[alarm ? 8 : 7], tr = s->rtc[0], dr = s->rtc[1];
    uint32_t masks[] = { 0x7f, 0x7f00, 0x7f0000 };
    for (unsigned i = 0; i < 3; i++) {
        if (!(value & (1u << (7 + i * 8))) &&
            (value & masks[i]) != (tr & masks[i])) {
            return false;
        }
    }
    unsigned day = value & (1 << 30) ? extract32(dr, 13, 3) : dr & 0x3f;
    return (value & (1u << 31)) || extract32(value, 24, 6) == day;
}

static void ex_rtc_tick(void *opaque)
{
    NikonEXState *s = opaque;
    if (!ex_rtc_hz(s) || (s->rtc[3] & (1 << 7))) {
        return;
    }
    uint32_t tr = s->rtc[0], dr = s->rtc[1];
    unsigned sec = ex_bcd_decode(tr & 0x7f) + 1;
    unsigned min = ex_bcd_decode((tr >> 8) & 0x7f);
    unsigned hour = ex_bcd_decode((tr >> 16) & 0x3f);
    bool pm = tr & (1 << 22), carry_day = false;
    if (sec >= 60) {
        sec = 0;
        if (++min >= 60) {
            min = 0;
            hour++;
            if (s->rtc[2] & (1 << 6)) {
                if (hour == 12) {
                    carry_day = pm;
                    pm = !pm;
                } else if (hour > 12) {
                    hour = 1;
                }
            } else if (hour >= 24) {
                hour = 0;
                carry_day = true;
            }
        }
    }
    if (carry_day) {
        static const unsigned days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        unsigned year = ex_bcd_decode(dr >> 16), month = ex_bcd_decode((dr >> 8) & 0x1f);
        unsigned day = ex_bcd_decode(dr & 0x3f) + 1;
        unsigned weekday = extract32(dr, 13, 3) % 7 + 1;
        if (month >= 1 && month <= 12 && day > days[month - 1] + (month == 2 && !(year % 4))) {
            day = 1;
            if (++month > 12) {
                month = 1;
                year = (year + 1) % 100;
            }
        }
        s->rtc[1] = ex_bcd_encode(day) | (ex_bcd_encode(month) << 8) |
            (weekday << 13) | (ex_bcd_encode(year) << 16);
    }
    s->rtc[0] = ex_bcd_encode(sec) | (ex_bcd_encode(min) << 8) |
        (ex_bcd_encode(hour) << 16) | (pm << 22);
    s->rtc[3] |= 1 << 5;
    for (unsigned i = 0; i < 2; i++) {
        if ((s->rtc[2] & (1 << (8 + i))) && ex_rtc_alarm(s, i)) {
            s->rtc[3] |= 1 << (8 + i);
            if (s->rtc[2] & (1 << (12 + i))) {
                s->exti[5] |= 1 << 17;
            }
        }
    }
    ex_exti_update(s);
    ex_rtc_schedule(s);
}

static void ex_rtc_wakeup(void *opaque)
{
    NikonEXState *s = opaque;
    if (!ex_rtc_hz(s) || !(s->rtc[2] & (1 << 10))) {
        return;
    }
    s->rtc[3] |= 1 << 10;
    if (s->rtc[2] & (1 << 14)) {
        s->exti[5] |= 1 << 22;
        ex_exti_update(s);
    }
    ex_rtc_schedule(s);
}

static bool ex_rtc_reg(unsigned reg)
{
    return reg <= 6 || (reg >= 7 && reg <= 14) ||
        (reg >= 16 && reg < 40);
}

static void ex_rtc_reset(NikonEXState *s)
{
    timer_del(s->rtc_sync);
    timer_del(s->rtc_tick);
    timer_del(s->rtc_wakeup);
    memset(s->rtc, 0, sizeof(s->rtc));
    s->rtc[1] = 0x2101;
    s->rtc[3] = 7;
    s->rtc[4] = 0x007f00ff;
    s->rtc[5] = 0xffff;
    s->rtc_unlocked = false;
    s->rtc_initialized = true;
    s->rtc_key = 0;
}

static void ex_pin_change(void *opaque, int n, int level)
{
    EXPinRoute *r = opaque;
    NikonEXState *s = r->soc;
    uint32_t bit = 1 << r->pin;
    unsigned selected = extract32(s->syscfg[2 + r->pin / 4],
                                  4 * (r->pin % 4), 4);
    bool old = s->exti_level & bit;

    if ((s->resetting_ports & (1 << r->port)) &&
        (s->external_mask[r->port] & bit)) {
        level = !!(s->external_level[r->port] & bit);
    }
    s->input[r->port] = deposit32(s->input[r->port], r->pin, 1, !!level);
    qemu_set_irq(s->pad_output[r->port * 16 + r->pin], level);
    if (selected != r->port) {
        return;
    }
    s->exti_level = deposit32(s->exti_level, r->pin, 1, !!level);
    if ((!old && level && (s->exti[2] & bit)) ||
        (old && !level && (s->exti[3] & bit))) {
        s->exti[5] |= bit;
    }
    ex_exti_update(s);
}

static void ex_replay_port(NikonEXState *s, unsigned port)
{
    for (unsigned pin = 0; pin < 16; pin++) {
        if (s->external_mask[port] & (1 << pin)) {
            qemu_set_irq(qdev_get_gpio_in(s->gpio[port], pin),
                         !!(s->external_level[port] & (1 << pin)));
        }
    }
}

static void ex_external_input(void *opaque, int line, int level)
{
    NikonEXState *s = opaque;
    unsigned port = line / 16, pin = line % 16;
    unsigned bit = 1 << pin;
    if (level < 0) {
        s->external_mask[port] &= ~bit;
        object_property_set_uint(OBJECT(s->gpio[port]), "disconnected-pins",
                                 bit, &error_abort);
    } else {
        s->external_mask[port] |= bit;
        s->external_level[port] = deposit32(s->external_level[port], pin,
                                          1, !!level);
        /* An external wire remains physically connected while firmware
         * temporarily selects the pin as an output.  The STM32 GPIO model
         * disconnects such pins to avoid a driven short; reconnect it here
         * so a later return to input mode observes the retained board level. */
        uint16_t disconnected = object_property_get_uint(
            OBJECT(s->gpio[port]), "disconnected-pins", &error_abort);
        object_property_set_uint(OBJECT(s->gpio[port]), "disconnected-pins",
                                 disconnected & ~bit, &error_abort);
        qemu_set_irq(qdev_get_gpio_in(s->gpio[port], pin), !!level);
    }
}

static void ex_reset_port(NikonEXState *s, unsigned port)
{
    s->resetting_ports |= 1 << port;
    device_cold_reset(s->gpio[port]);
    ex_replay_port(s, port);
    s->resetting_ports &= ~(1 << port);
}

static uint64_t ex_pll_hz(NikonEXState *s)
{
    uint32_t cfg = s->rcc[1];
    unsigned m = cfg & 0x3f;
    unsigned n = extract32(cfg, 6, 9);
    unsigned p = 2 * (extract32(cfg, 16, 2) + 1);
    if (m < 2 || n < 50 || n > 432) {
        return 0;
    }
    return (uint64_t)((cfg & (1 << 22)) ? EX_HSE_HZ : EX_HSI_HZ) * n / m / p;
}

static void ex_timer_clocks(NikonEXState *s)
{
    static const unsigned ppre[] = { 1, 1, 1, 1, 2, 4, 8, 16 };
    static const unsigned apb2_bits[] = { 0, 1, 16, 17, 18 };
    uint32_t cfg = s->rcc[2];
    uint64_t hclk = clock_get_hz(s->cpuclk);
    for (unsigned i = 0; i < ARRAY_SIZE(s->timerclk); i++) {
        bool apb2 = i >= 9;
        unsigned divider = ppre[extract32(cfg, apb2 ? 13 : 10, 3)];
        unsigned bit = apb2 ? apb2_bits[i - 9] : i;
        bool enabled = s->rcc[(apb2 ? 0x44 : 0x40) / 4] & (1 << bit);
        uint64_t hz = enabled ? hclk / divider * (divider > 1 ? 2 : 1) : 0;
        clock_update_hz(s->timerclk[i], hz);
    }
}

static void ex_clock_settle(void *opaque)
{
    NikonEXState *s = opaque;
    uint32_t *cr = &s->rcc[0];
    uint32_t cfg = s->rcc[2];
    unsigned source = cfg & 3;
    uint64_t hz = EX_HSI_HZ;
    static const unsigned hpre[] = { 1, 1, 1, 1, 1, 1, 1, 1,
                                     2, 4, 8, 16, 64, 128, 256, 512 };

    *cr = deposit32(*cr, 1, 1, !!(*cr & 1));
    *cr = deposit32(*cr, 17, 1, !!(*cr & (1 << 16)));
    bool pll_source_ready = s->rcc[1] & (1 << 22) ?
        (*cr & (1 << 17)) : (*cr & (1 << 1));
    *cr = deposit32(*cr, 25, 1,
                    (*cr & (1 << 24)) && pll_source_ready && ex_pll_hz(s));
    *cr = deposit32(*cr, 27, 1, (*cr & (1 << 26)) && pll_source_ready);
    *cr = deposit32(*cr, 29, 1, (*cr & (1 << 28)) && pll_source_ready);
    s->rcc[0x70 / 4] = deposit32(s->rcc[0x70 / 4], 1, 1,
                                 s->rcc[0x70 / 4] & 1);
    s->rcc[0x74 / 4] = deposit32(s->rcc[0x74 / 4], 1, 1,
                                 s->rcc[0x74 / 4] & 1);
    if ((source == 0 && (*cr & 2)) ||
        (source == 1 && (*cr & (1 << 17))) ||
        (source == 2 && (*cr & (1 << 25)))) {
        s->rcc[2] = deposit32(cfg, 2, 2, source);
    }
    source = extract32(s->rcc[2], 2, 2);
    if (source == 1) {
        hz = EX_HSE_HZ;
    } else if (source == 2) {
        hz = ex_pll_hz(s);
    }
    hz /= hpre[extract32(cfg, 4, 4)];
    if (hz) {
        clock_update_hz(s->cpuclk, hz);
        clock_update_hz(s->refclk, hz / 8);
    }
    ex_timer_clocks(s);
    ex_rtc_schedule(s);
}

static void ex_spi_reset(NikonEXState *s, unsigned i)
{
    memset(s->spi[i].reg, 0, sizeof(s->spi[i].reg));
    s->spi[i].reg[2] = 2;
    s->spi[i].reg[4] = 7;
    s->spi[i].reg[8] = 2;
    s->spi[i].tx_valid = false;
    s->spi[i].ovr_dr_seen = false;
    s->spi[i].txdata = 0;
    ex_irq(s, ex_spi_irq[i], false);
}

static void ex_i2c_reset(NikonEXState *s, unsigned i)
{
    i2c_end_transfer(s->i2c[i].bus);
    memset(s->i2c[i].reg, 0, sizeof(s->i2c[i].reg));
    timer_del(s->i2c[i].rx_timer);
    s->i2c[i].sr1_seen = s->i2c[i].receive = false;
    s->i2c[i].rx_count = 0;
    s->i2c[i].rx_remaining = 0;
    ex_irq(s, ex_i2c_irq[i], false);
    ex_irq(s, ex_i2c_irq[i] + 1, false);
}

static void ex_i2c_fill_limit(EXI2C *p, unsigned limit)
{
    if (p->receive && i2c_bus_busy(p->bus)) {
        limit = MIN(limit, p->rx_remaining);
        while (p->rx_count < limit && ex_i2c_rx_open(p)) {
            p->rx[p->rx_count++] = i2c_recv(p->bus);
            if (p->rx_remaining != EX_I2C_RX_UNBOUNDED) {
                p->rx_remaining--;
            }
        }
    }
    p->reg[5] = (p->reg[5] & ~0x44u) |
                (p->rx_count ? 0x40 : 0) | (p->rx_count == 2 ? 4 : 0);
}

static void ex_i2c_fill(EXI2C *p)
{
    unsigned limit = (p->reg[0] & ((1 << 10) | (1 << 11))) ? 2 : 1;

    ex_i2c_fill_limit(p, limit);
}

static void ex_rcc_reset_devices(NikonEXState *s, unsigned off, uint32_t bits)
{
    if (off == 0x10) {
        for (unsigned i = 0; i < 9; i++) {
            if (bits & (1 << i)) {
                ex_reset_port(s, i);
            }
        }
        for (unsigned c = 0; c < 2; c++) {
            if (bits & (1 << (21 + c))) {
                memset(&s->dma[c], 0, sizeof(s->dma[c]));
                for (unsigned n = 0; n < 8; n++) {
                    s->dma[c].stream[n].reg[5] = 0x21;
                    ex_irq(s, ex_dma_irq[c][n], false);
                }
            }
        }
    } else if (off == 0x20) {
        for (unsigned i = 0; i < 9; i++) {
            if (bits & (1 << i)) {
                device_cold_reset(s->timer[i]);
            }
        }
        for (unsigned i = 0; i < 3; i++) {
            if (bits & (1 << (21 + i))) {
                ex_i2c_reset(s, i);
            }
        }
        for (unsigned i = 1; i < 3; i++) {
            if (bits & (1 << (13 + i))) {
                ex_spi_reset(s, i);
            }
        }
        for (unsigned i = 1; i < 5; i++) {
            if (bits & (1 << (16 + i))) {
                device_cold_reset(s->uart[i]);
            }
        }
    } else if (off == 0x24) {
        static const unsigned timer_bits[] = { 0, 1, 16, 17, 18 };
        for (unsigned i = 0; i < 5; i++) {
            if (bits & (1 << timer_bits[i])) {
                device_cold_reset(s->timer[9 + i]);
            }
        }
        if (bits & (1 << 12)) {
            ex_spi_reset(s, 0);
        }
        if (bits & (1 << 13)) {
            ex_spi_reset(s, 3);
        }
        if (bits & (1 << 4)) {
            device_cold_reset(s->uart[0]);
        }
        if (bits & (1 << 5)) {
            device_cold_reset(s->uart[5]);
        }
    }
}

static bool ex_rcc_reg(unsigned off)
{
    switch (off) {
    case 0x00: case 0x04: case 0x08: case 0x0c:
    case 0x10: case 0x14: case 0x18: case 0x20: case 0x24:
    case 0x30: case 0x34: case 0x38: case 0x40: case 0x44:
    case 0x50: case 0x54: case 0x58: case 0x60: case 0x64:
    case 0x70: case 0x74: case 0x80: case 0x84: case 0x88:
    case 0x8c: case 0x90: case 0x94:
        return true;
    default:
        return false;
    }
}

static void ex_spi_update(NikonEXState *s, unsigned channel)
{
    EXSPI *p = &s->spi[channel];
    uint32_t status = p->reg[2], enables = p->reg[1];
    ex_irq(s, ex_spi_irq[channel],
           ((status & 1) && (enables & (1 << 6))) ||
           ((status & 2) && (enables & (1 << 7))) ||
           ((status & 0x70) && (enables & (1 << 5))));
}

static void ex_i2c_update(NikonEXState *s, unsigned channel)
{
    EXI2C *p = &s->i2c[channel];
    uint32_t status = p->reg[5], enables = p->reg[1];
    bool event = (status & 0x1f) ||
        ((enables & (1 << 10)) && (status & 0xc0));
    ex_irq(s, ex_i2c_irq[channel], event && (enables & (1 << 9)));
    ex_irq(s, ex_i2c_irq[channel] + 1,
           (status & 0xff00) && (enables & (1 << 8)));
}

/*
 * The acknowledge bit for a received byte is sampled at the end of that byte,
 * not when the address phase ends.  One SCL period is derived from the
 * programmed CCR and APB1 clock, so a reader that clears ACK inside the first
 * byte never receives a second one, while a reader that keeps ACK lets the
 * next byte reach the shift register and raises BTF.
 */
static uint64_t ex_i2c_byte_ns(NikonEXState *s, const EXI2C *p)
{
    static const unsigned apb1_div[] = { 1, 1, 1, 1, 2, 4, 8, 16 };
    uint32_t ccr = p->reg[7];
    uint64_t hclk = clock_get_hz(s->cpuclk);
    unsigned divider = apb1_div[extract32(s->rcc[2], 10, 3)];
    uint64_t pclk = divider ? hclk / divider : 0;
    uint64_t scl, ticks;

    if (!pclk || !(ccr & 0xfff)) {
        return EX_I2C_DEFAULT_BYTE_NS;
    }
    if (ccr & (1 << 15)) {
        scl = (uint64_t)(ccr & 0xfff) * ((ccr & (1 << 14)) ? 25 : 3);
    } else {
        scl = (uint64_t)(ccr & 0xfff);
    }
    ticks = scl * 9; /* eight data bits plus the acknowledge bit */
    return MAX(ticks * 1000000000ull / pclk, 1000ull);
}

static void ex_i2c_rx_shift(void *opaque)
{
    EXI2C *p = opaque;

    if (!p->receive || !i2c_bus_busy(p->bus) || !(p->reg[0] & (1 << 10))) {
        return;
    }
    if (p->rx_count < 2) {
        ex_i2c_fill_limit(p, 2);
    }
    ex_i2c_update(p->processor, p->channel);
}

static void ex_dma_update(NikonEXState *s, unsigned controller, unsigned stream)
{
    EXDMA *d = &s->dma[controller];
    unsigned shift = ex_dma_shift[stream % 4];
    uint32_t flags = (d->status[stream / 4] >> shift) & 0x3d;
    uint32_t cr = d->stream[stream].reg[0];
    uint32_t mask = ((cr << 1) & 0x3c) | (d->stream[stream].reg[5] & 0x80 ? 1 : 0);
    ex_irq(s, ex_dma_irq[controller][stream], flags & mask);
}

static void ex_dma_flag(NikonEXState *s, unsigned c, unsigned n, unsigned flag)
{
    s->dma[c].status[n / 4] |= flag << ex_dma_shift[n % 4];
    ex_dma_update(s, c, n);
}

/* Transfer one bus beat only after a real peripheral request or MEM2MEM EN. */
static bool ex_dma_beat(NikonEXState *s, unsigned c, unsigned n)
{
    EXDMAStream *p = &s->dma[c].stream[n];
    uint32_t cr = p->reg[0], direction = extract32(cr, 6, 2);
    unsigned psz = 1 << extract32(cr, 11, 2);
    unsigned msz = 1 << extract32(cr, 13, 2);
    uint32_t source, dest;
    uint8_t data[4] = { 0 };
    MemTxResult result;

    if (!(cr & 1) || !p->reg[1]) {
        return false;
    }
    if (psz > 4 || msz > 4 || direction == 3 || (cr & (1 << 18))) {
        ex_dma_flag(s, c, n, 8);
        p->reg[0] &= ~1u;
        return false;
    }
    source = direction == 1 ? p->reg[3] : p->reg[2];
    dest = direction == 1 ? p->reg[2] : p->reg[3];
    result = address_space_read(&s->dma_as, source, MEMTXATTRS_UNSPECIFIED,
                                data, direction == 1 ? msz : psz);
    if (result == MEMTX_OK) {
        result = address_space_write(&s->dma_as, dest, MEMTXATTRS_UNSPECIFIED,
                                     data, direction == 1 ? psz : msz);
    }
    if (result != MEMTX_OK) {
        ex_dma_flag(s, c, n, 8);
        p->reg[0] &= ~1u;
        return false;
    }
    if (cr & (1 << 9)) {
        p->reg[2] += psz;
    }
    if (cr & (1 << 10)) {
        p->reg[3] += msz;
    }
    p->reg[1]--;
    if (p->reg[1] == p->initial_count / 2) {
        ex_dma_flag(s, c, n, 0x10);
    }
    if (!p->reg[1]) {
        ex_dma_flag(s, c, n, 0x20);
        if (cr & (1 << 8)) {
            p->reg[1] = p->initial_count;
            p->reg[3] = p->initial_memory;
        } else {
            p->reg[0] &= ~1u;
        }
    }
    return true;
}

static void ex_dma_request(NikonEXState *s, uint32_t peripheral, bool transmit)
{
    if (s->dma_running) {
        return;
    }
    s->dma_running = true;
    for (unsigned c = 0; c < 2; c++) {
        for (unsigned n = 0; n < 8; n++) {
            EXDMAStream *p = &s->dma[c].stream[n];
            if (p->reg[2] == peripheral &&
                extract32(p->reg[0], 6, 2) == (transmit ? 1 : 0)) {
                ex_dma_beat(s, c, n);
            }
        }
    }
    s->dma_running = false;
}

static void ex_adc_update(NikonEXState *s)
{
    bool irq = false;
    for (unsigned i = 0; i < 3; i++) {
        EXADC *p = &s->adc[i];
        irq |= ((p->reg[0] & 2) && (p->reg[1] & (1 << 5))) ||
               ((p->reg[0] & 0x20) && (p->reg[1] & (1 << 26)));
    }
    ex_irq(s, 18, irq);
}

static void ex_adc_convert(NikonEXState *s, unsigned i)
{
    EXADC *p = &s->adc[i];
    unsigned channel = p->reg[13] & 0x1f;
    if ((p->reg[2] & ((1 << 30) | 1)) != ((1 << 30) | 1)) {
        return;
    }
    p->reg[0] |= 0x10; /* A real conversion request has started. */
    if (channel >= 19 || !(p->valid & (1 << channel))) {
        return;
    }
    unsigned resolution = 12 - 2 * extract32(p->reg[1], 24, 2);
    uint32_t sample = p->sample[channel] >> (12 - resolution);
    if (p->reg[2] & (1 << 11)) {
        sample <<= 16 - resolution;
    }
    if (p->reg[0] & 2) {
        p->reg[0] |= 0x20;
    }
    p->reg[19] = sample;
    p->reg[0] |= 2;
    p->reg[2] &= ~(1 << 30);
    if (p->reg[2] & (1 << 8)) {
        ex_dma_request(s, 0x4001204c + i * 0x100, false);
    }
    ex_adc_update(s);
}

static void ex_adc_dispatch(void *opaque)
{
    NikonEXState *s = opaque;
    for (unsigned i = 0; i < ARRAY_SIZE(s->adc); i++) {
        ex_adc_convert(s, i);
    }
}

void nikon_ex_adc_input(NikonEXState *s, unsigned adc, unsigned channel,
                        int sample)
{
    assert(adc < ARRAY_SIZE(s->adc) && channel < 19);
    assert(sample >= -1 && sample < 4096);
    if (sample < 0) {
        s->adc[adc].valid &= ~(1U << channel);
    } else {
        s->adc[adc].sample[channel] = sample;
        s->adc[adc].valid |= 1U << channel;
        if (qdev_is_realized(DEVICE(s))) {
            qemu_bh_schedule(s->adc_bh);
        }
    }
}

static void ex_adc_input(void *opaque, int index, int sample)
{
    nikon_ex_adc_input(opaque, index / 19, index % 19, sample);
}

uint32_t nikon_ex_spi_transfer(NikonEXState *s, unsigned channel, uint32_t value)
{
    assert(channel < ARRAY_SIZE(s->spi));
    EXSPI *p = &s->spi[channel];
    unsigned mask = p->reg[0] & (1 << 11) ? 0xffff : 0xff;
    uint32_t outgoing = p->tx_valid ? p->txdata & mask : mask;

    if (!(p->reg[0] & (1 << 6)) || (p->reg[0] & (1 << 2)) ||
        ((p->reg[0] & (1 << 9)) && (p->reg[0] & (1 << 8)))) {
        return mask;
    }
    if (p->reg[1] & 2) {
        ex_dma_request(s, ex_spi_base[channel] + 0x0c, true);
        outgoing = p->tx_valid ? p->txdata & mask : mask;
    }
    if (p->reg[2] & 1) {
        p->reg[2] |= 1 << 6; /* overrun: unread previous receive word */
    } else {
        p->reg[3] = value & mask;
        p->reg[2] |= 1;
    }
    p->tx_valid = false;
    p->reg[2] |= 2;
    if (p->reg[1] & 1) {
        ex_dma_request(s, ex_spi_base[channel] + 0x0c, false);
    }
    ex_spi_update(s, channel);
    return outgoing;
}

static MemTxResult ex_bad(NikonEXState *s, uint32_t address, unsigned size,
                          bool write)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "EX cpu=%d pc=0x%08x unknown %s%u @0x%08x\n",
                  CPU(s->armv7m.cpu)->cpu_index,
                  s->armv7m.cpu->env.regs[15], write ? "W" : "R",
                  size, address);
    return MEMTX_ERROR;
}

static MemTxResult ex_peripheral_access(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs, bool consume)
{
    NikonEXState *s = opaque;
    uint32_t addr = 0x40000000 + offset, aligned = addr & ~3u;
    uint32_t v;
    if ((addr & 3) + size > 4) {
        return ex_bad(s, addr, size, false);
    }
    if (aligned >= EX_RCC_BASE && aligned <= EX_RCC_BASE + 0x94 &&
        ex_rcc_reg(aligned - EX_RCC_BASE)) {
        v = s->rcc[(aligned - EX_RCC_BASE) / 4];
    } else if (aligned == 0x40007000 || aligned == 0x40007004) {
        v = s->pwr[(aligned - 0x40007000) / 4];
    } else if (aligned >= 0x40023c00 && aligned <= 0x40023c14) {
        v = s->flashreg[(aligned - 0x40023c00) / 4];
    } else if ((aligned >= 0x40013800 && aligned <= 0x40013814) ||
               aligned == 0x40013820) {
        v = s->syscfg[(aligned - 0x40013800) / 4];
    } else if (aligned >= 0x40013c00 && aligned <= 0x40013c14) {
        v = s->exti[(aligned - 0x40013c00) / 4];
    } else if (aligned >= 0x40002800 && aligned < 0x400028a0 &&
               ex_rtc_reg((aligned - 0x40002800) / 4)) {
        unsigned reg = (aligned - 0x40002800) / 4;
        v = s->rtc[reg];
        if (reg == 9 || reg == 11) {
            v = 0; /* documented write-only key/shift interfaces */
        } else if (reg == 10 && timer_pending(s->rtc_tick)) {
            uint64_t left = timer_expire_time_ns(s->rtc_tick) -
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            uint64_t period = ex_rtc_period(s);
            v = period ? MIN((s->rtc[4] & 0x7fff),
                            left * ((s->rtc[4] & 0x7fff) + 1) / period) : 0;
        }
    } else if (aligned == 0x40012304) {
        v = s->adc_common;
    } else {
        for (unsigned i = 0; i < 3; i++) {
            uint32_t base = 0x40012000 + i * 0x100;
            if (aligned >= base && aligned <= base + 0x4c) {
                unsigned reg = (aligned - base) / 4;
                v = s->adc[i].reg[reg];
                if (consume && reg == 19) {
                    s->adc[i].reg[0] &= ~2u;
                    ex_adc_update(s);
                }
                goto found;
            }
        }
        for (unsigned i = 0; i < 3; i++) {
            if (aligned >= ex_i2c_base[i] && aligned <= ex_i2c_base[i] + 0x20) {
                EXI2C *p = &s->i2c[i];
                unsigned reg = (aligned - ex_i2c_base[i]) / 4;
                if (consume && reg == 4 && p->receive && p->rx_count) {
                    p->reg[4] = p->rx[0];
                    bool pipeline = p->rx_count == 2;
                    if (pipeline) {
                        p->rx[0] = p->rx[1];
                    }
                    p->rx_count--;
                    /* With ACK cleared and both the data and shift
                     * registers occupied, STM32 clocks the final byte into
                     * the shift register before the last two DR reads. */
                    if (pipeline && ex_i2c_rx_open(p) &&
                        !(p->reg[0] & (1 << 10)) &&
                        i2c_bus_busy(p->bus)) {
                        p->rx[p->rx_count++] = i2c_recv(p->bus);
                        if (p->rx_remaining != EX_I2C_RX_UNBOUNDED) {
                            p->rx_remaining--;
                        }
                        i2c_nack(p->bus);
                    }
                    if (p->reg[0] & (1 << 10)) {
                        ex_i2c_fill(p);
                    }
                    ex_i2c_update(s, i);
                }
                if (consume && reg == 4 && p->receive &&
                    (p->reg[0] & (1 << 10)) && i2c_bus_busy(p->bus)) {
                    ex_i2c_fill(p);
                    ex_i2c_update(s, i);
                }
                if (consume && reg == 4 && p->receive && !p->rx_count) {
                    /* The last DR read retires RXNE/BTF even when ACK was
                     * already cleared and the STOP ended the bus. */
                    p->reg[5] &= ~0x44u;
                    ex_i2c_update(s, i);
                }
                v = p->reg[reg];
                if (consume && reg == 5) {
                    p->sr1_seen = true;
                } else if (consume && reg == 6 && p->sr1_seen) {
                    /* SR1 followed by SR2 clears ADDR and STOPF. */
                    p->reg[5] &= ~0x12u;
                    p->sr1_seen = false;
                    ex_i2c_update(s, i);
                }
                goto found;
            }
        }
        for (unsigned c = 0; c < 2; c++) {
            uint32_t base = 0x40026000 + c * 0x400;
            if (aligned < base || aligned >= base + 0xd0) {
                continue;
            }
            unsigned off = aligned - base;
            if (off < 8) {
                v = s->dma[c].status[off / 4];
            } else if (off < 16) {
                v = 0; /* documented write-only flag-clear register */
            } else {
                v = s->dma[c].stream[(off - 16) / 24].reg[((off - 16) % 24) / 4];
            }
            goto found;
        }
        return ex_bad(s, addr, size, false);
    }
found:
    *value = (v >> (8 * (addr & 3))) & MAKE_64BIT_MASK(0, size * 8);
    return MEMTX_OK;
}

static MemTxResult ex_peripheral_read(void *opaque, hwaddr offset,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    return ex_peripheral_access(opaque, offset, value, size, attrs, true);
}

static MemTxResult ex_peripheral_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonEXState *s = opaque;
    uint32_t addr = 0x40000000 + offset, aligned = addr & ~3u;
    uint32_t mask, v;
    uint64_t previous;
    if ((addr & 3) + size > 4 ||
        ex_peripheral_access(s, aligned - 0x40000000, &previous, 4,
                             attrs, false) != MEMTX_OK) {
        return ex_bad(s, addr, size, true);
    }
    mask = MAKE_64BIT_MASK(0, size * 8) << (8 * (addr & 3));
    v = (previous & ~mask) | ((value << (8 * (addr & 3))) & mask);
    if (aligned >= EX_RCC_BASE && aligned <= EX_RCC_BASE + 0x94) {
        unsigned off = aligned - EX_RCC_BASE;
        if (off == 0) {
            v = (v & ~0x2a020002u) | (s->rcc[0] & 0x2a020002u);
        } else if (off == 8) {
            v = (v & ~0xcu) | (s->rcc[2] & 0xc);
        } else if (off == 0x70 || off == 0x74) {
            v = (v & ~2u) | (s->rcc[off / 4] & 2);
        }
        s->rcc[off / 4] = v;
        if (off == 0x70 && (v & (1 << 16))) {
            ex_rtc_reset(s);
        }
        ex_rcc_reset_devices(s, off, v & ~previous);
        if (off == 0x40 || off == 0x44 || off == 8) {
            ex_timer_clocks(s);
        }
        timer_mod(s->osc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + EX_OSC_DELAY_NS);
    } else if (aligned == 0x40007000) {
        s->pwr[0] = v & 0x0003ffff;
        s->pwr[1] = deposit32(s->pwr[1], 14, 1, !!(v & 0xc000));
    } else if (aligned == 0x40007004) {
        s->pwr[1] = (s->pwr[1] & ~0x300u) | (v & 0x300);
    } else if (aligned >= 0x40023c00 && aligned <= 0x40023c14) {
        unsigned reg = (aligned - 0x40023c00) / 4;
        if (reg == 0) {
            s->flashreg[0] = v & 0x1f0f;
        } else if (reg == 3) {
            s->flashreg[3] &= ~(v & 0xf3);
        } else {
            /* Flash programming isn't a cache-control transaction. */
            return ex_bad(s, addr, size, true);
        }
    } else if ((aligned >= 0x40013800 && aligned <= 0x40013814) ||
               aligned == 0x40013820) {
        unsigned reg = (aligned - 0x40013800) / 4;
        if (reg == 0 && (v & 3)) {
            return ex_bad(s, addr, size, true);
        }
        s->syscfg[reg] = reg == 8 ? ((v & 1) | (!(v & 1) << 8)) : v;
        if (reg >= 2 && reg <= 5) {
            /* Selecting a pad samples its level; routing is not an edge. */
            for (unsigned i = 0; i < 4; i++) {
                unsigned pin = (reg - 2) * 4 + i;
                unsigned port = extract32(v, i * 4, 4);
                bool level = port < ARRAY_SIZE(s->input) &&
                             (s->input[port] & (1 << pin));

                s->exti_level = deposit32(s->exti_level, pin, 1, level);
            }
        }
    } else if (aligned >= 0x40013c00 && aligned <= 0x40013c14) {
        unsigned reg = (aligned - 0x40013c00) / 4;
        if (reg == 5) {
            s->exti[5] &= ~v;
        } else {
            s->exti[reg] = v & 0x7fffff;
            if (reg == 4) {
                s->exti[5] |= v;
            }
        }
        ex_exti_update(s);
    } else if (aligned >= 0x40002800 && aligned < 0x400028a0) {
        unsigned reg = (aligned - 0x40002800) / 4;
        if (!(s->pwr[0] & (1 << 8))) {
            return MEMTX_OK; /* backup-domain writes are protected by DBP */
        }
        if (reg == 9) {
            if ((v & 0xff) == 0xca) {
                s->rtc_key = 1;
            } else if ((v & 0xff) == 0x53 && s->rtc_key == 1) {
                s->rtc_unlocked = true;
                s->rtc_key = 0;
            } else {
                s->rtc_unlocked = false;
                s->rtc_key = 0;
            }
        } else if (reg >= 20 || (reg == 17) || s->rtc_unlocked) {
            if (reg == 3) {
                bool initializing = s->rtc[3] & (1 << 7);
                /* INIT is writable; RSF and event flags are write-zero-clear. */
                s->rtc[3] &= v | ~0xff20u;
                s->rtc[3] = deposit32(s->rtc[3], 7, 1, !!(v & (1 << 7)));
                if (!(v & (1 << 7))) {
                    s->rtc[3] &= ~(1 << 6);
                    if (initializing) {
                        s->rtc[3] |= 1 << 4;
                    }
                }
            } else if (reg == 0 || reg == 1 || reg == 4) {
                if (s->rtc[3] & (1 << 6)) {
                    static const uint32_t masks[] = { 0x007f7f7f, 0x00ffff3f };
                    s->rtc[reg] = reg == 4 ? v & 0x007f7fff : v & masks[reg];
                }
            } else if (reg == 2) {
                s->rtc[2] = v & 0x00ffffff;
                s->rtc[3] &= ~7u;
                timer_del(s->rtc_wakeup);
            } else if (reg == 5) {
                if (s->rtc[3] & 4) {
                    s->rtc[5] = v & 0xffff;
                }
            } else if (reg == 7 || reg == 8) {
                if (s->rtc[3] & (1 << (reg - 7))) {
                    s->rtc[reg] = v;
                }
            } else if (reg == 10 || (reg >= 12 && reg <= 14)) {
                return ex_bad(s, addr, size, true);
            } else if (reg == 11 || reg == 16) {
                /* Subsecond shift/calibration requires a timed profile. */
                if (v) {
                    return ex_bad(s, addr, size, true);
                }
            } else {
                s->rtc[reg] = v;
            }
        }
        ex_rtc_schedule(s);
    } else if (aligned == 0x40012304) {
        s->adc_common = v & 0x00c3ff1f;
    } else {
        for (unsigned i = 0; i < 3; i++) {
            uint32_t base = 0x40012000 + i * 0x100;
            if (aligned >= base && aligned <= base + 0x4c) {
                unsigned reg = (aligned - base) / 4;
                if (reg >= 15) {
                    return ex_bad(s, addr, size, true);
                }
                if (reg == 0) {
                    s->adc[i].reg[0] &= v & 0x3f;
                } else {
                    s->adc[i].reg[reg] = v;
                }
                if (reg == 2) {
                    qemu_bh_schedule(s->adc_bh);
                }
                ex_adc_update(s);
                return MEMTX_OK;
            }
        }
        for (unsigned i = 0; i < 3; i++) {
            if (aligned >= ex_i2c_base[i] && aligned <= ex_i2c_base[i] + 0x20) {
                EXI2C *p = &s->i2c[i];
                unsigned reg = (aligned - ex_i2c_base[i]) / 4;
                uint32_t cr1_prev = p->reg[0];
                if (reg == 5) {
                    p->reg[5] &= v | 0xff;
                } else if (reg == 6) {
                    return ex_bad(s, addr, size, true);
                } else {
                    p->reg[reg] = v;
                }
                if (reg == 0) {
                    if ((cr1_prev & (1 << 10)) && !(v & (1 << 10))) {
                        timer_del(p->rx_timer);
                    }
                    if ((v & (1 << 15)) || !(v & 1)) {
                        i2c_end_transfer(p->bus);
                        p->reg[5] = p->reg[6] = 0;
                        p->rx_count = 0;
                    } else if (v & (1 << 9)) {
                        timer_del(p->rx_timer);
                        i2c_end_transfer(p->bus);
                        p->reg[6] = 0;
                        /* STOPF stays observable after the transfer ends.
                         * Preserve RXNE/BTF and the received byte: the
                         * firmware writes STOP before its RXNE wait, then
                         * reads DR. */
                        p->reg[5] &= ~0x02u;
                        p->reg[5] |= 0x10;
                        p->reg[0] &= ~(1 << 9);
                    } else if (v & (1 << 8)) {
                        timer_del(p->rx_timer);
                        p->reg[5] = 1; /* START generated, address required */
                        p->reg[6] = 3;
                        p->reg[0] &= ~(1 << 8);
                    }
                } else if (reg == 4 && (p->reg[0] & 1)) {
                    if (p->reg[5] & 1) {
                        p->rx_count = 0;
                        p->receive = v & 1;
                        /* The guest controls the transfer boundary through
                         * ACK/STOP.  Keep the controller FIFO fed until it
                         * explicitly releases the bus; the slave owns its
                         * register-group wrap semantics. */
                        p->rx_remaining = EX_I2C_RX_UNBOUNDED;
                        p->reg[5] &= ~1u;
                        if (i2c_start_transfer(p->bus, (v >> 1) & 0x7f, p->receive)) {
                            p->reg[5] |= 1 << 10; /* real absent slave NACK */
                        } else {
                            p->reg[5] |= 2 | (p->receive ? 0x40 : 0x80);
                            p->reg[6] = p->receive ? 3 : 7;
                            if (p->receive) {
                                ex_i2c_fill_limit(p, 1);
                                if (p->reg[0] & (1 << 10)) {
                                    timer_mod(p->rx_timer,
                                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                              ex_i2c_byte_ns(s, p));
                                }
                            }
                        }
                    } else if (i2c_bus_busy(p->bus) && !p->receive) {
                        p->reg[5] |= i2c_send(p->bus, v) ? 1 << 10 : 0x84;
                    } else {
                        p->reg[5] |= 1 << 10;
                    }
                }
                ex_i2c_update(s, i);
                return MEMTX_OK;
            }
        }
        for (unsigned c = 0; c < 2; c++) {
            uint32_t base = 0x40026000 + c * 0x400;
            if (aligned < base || aligned >= base + 0xd0) {
                continue;
            }
            unsigned off = aligned - base;
            if (off < 8) {
                return ex_bad(s, addr, size, true);
            } else if (off < 16) {
                s->dma[c].status[(off - 8) / 4] &= ~v;
                for (unsigned n = 0; n < 8; n++) {
                    ex_dma_update(s, c, n);
                }
            } else {
                unsigned n = (off - 16) / 24, reg = ((off - 16) % 24) / 4;
                EXDMAStream *p = &s->dma[c].stream[n];
                uint32_t old = p->reg[reg];
                if ((p->reg[0] & 1) && reg != 0) {
                    return ex_bad(s, addr, size, true);
                }
                p->reg[reg] = reg == 1 ? v & 0xffff : v;
                if (reg == 0 && !(old & 1) && (v & 1)) {
                    p->initial_count = p->reg[1];
                    p->initial_memory = p->reg[3];
                    if (extract32(v, 6, 2) == 2) {
                        s->dma_running = true;
                        for (unsigned left = p->reg[1]; left && (p->reg[0] & 1); left--) {
                            if (!ex_dma_beat(s, c, n)) {
                                break;
                            }
                        }
                        s->dma_running = false;
                    }
                }
                ex_dma_update(s, c, n);
            }
            return MEMTX_OK;
        }
        return ex_bad(s, addr, size, true);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps ex_peripheral_ops = {
    .read_with_attrs = ex_peripheral_read,
    .write_with_attrs = ex_peripheral_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static MemTxResult ex_spi_port_read(void *opaque, hwaddr offset,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    EXSPI *p = opaque;
    unsigned reg = offset / 4;
    if ((offset & 3) + size > 4 || reg >= ARRAY_SIZE(p->reg)) {
        return ex_bad(p->processor, ex_spi_base[p->channel] + offset,
                      size, false);
    }
    *value = extract64(p->reg[reg], 8 * (offset & 3), size * 8);
    if (reg == 3) {
        p->ovr_dr_seen = !!(p->reg[2] & (1 << 6));
        p->reg[2] &= ~1u;
        ex_spi_update(p->processor, p->channel);
    } else if (reg == 2 && p->ovr_dr_seen) {
        p->reg[2] &= ~(1 << 6);
        p->ovr_dr_seen = false;
        ex_spi_update(p->processor, p->channel);
    }
    return MEMTX_OK;
}

static MemTxResult ex_spi_port_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    EXSPI *p = opaque;
    unsigned reg = offset / 4;
    uint32_t v;
    if ((offset & 3) + size > 4 || reg >= ARRAY_SIZE(p->reg)) {
        return ex_bad(p->processor, ex_spi_base[p->channel] + offset,
                      size, true);
    }
    v = deposit32(p->reg[reg], 8 * (offset & 3), size * 8, value);
    if (reg == 2) {
        p->reg[2] &= v | ~(1 << 4);
    } else if (reg != 3) {
        p->reg[reg] = v;
    }
    if (reg == 3) {
        p->txdata = v;
        p->tx_valid = true;
        p->reg[2] &= ~2u;
        if ((p->reg[0] & 0x44) == 0x44) {
            unsigned mask = p->reg[0] & (1 << 11) ? 0xffff : 0xff;
            if (p->reg[2] & 1) {
                p->reg[2] |= 1 << 6;
            } else {
                p->reg[3] = ssi_transfer(p->bus, v & mask) & mask;
                p->reg[2] |= 1;
            }
            p->tx_valid = false;
            p->reg[2] |= 2;
        }
    }
    ex_spi_update(p->processor, p->channel);
    return MEMTX_OK;
}

static const MemoryRegionOps ex_spi_port_ops = {
    .read_with_attrs = ex_spi_port_read,
    .write_with_attrs = ex_spi_port_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void ex_spi_port_realize(DeviceState *dev, Error **errp)
{
    EXSPI *p = (EXSPI *)dev;
    memory_region_init_io(&p->memory, OBJECT(dev), &ex_spi_port_ops, p,
                          "ex.spi", 0x24);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &p->memory);
}

static void ex_spi_port_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ex_spi_port_realize;
    dc->user_creatable = false;
}

static void ex_reset(DeviceState *dev)
{
    NikonEXState *s = NIKON_EX(dev);
    uint32_t backup_clock = s->rcc[0x70 / 4];
    uint32_t lsi_clock = s->rcc[0x74 / 4] & 3;
    qemu_bh_cancel(s->reset_bh);
    qemu_bh_cancel(s->adc_bh);
    timer_del(s->osc_timer);
    memset(s->rcc, 0, sizeof(s->rcc));
    s->rcc[0] = 0x83;
    s->rcc[1] = 0x24003010;
    s->rcc[0x74 / 4] = 0x0e000000;
    if (s->rtc_initialized) {
        s->rcc[0x70 / 4] = backup_clock;
        s->rcc[0x74 / 4] |= lsi_clock;
    }
    memset(s->pwr, 0, sizeof(s->pwr));
    s->pwr[0] = 0xc000;
    s->pwr[1] = 0x4000;
    memset(s->flashreg, 0, sizeof(s->flashreg));
    s->flashreg[4] = 0x80000000;
    memset(s->syscfg, 0, sizeof(s->syscfg));
    s->syscfg[8] = 0x100;
    memset(s->exti, 0, sizeof(s->exti));
    memset(s->input, 0, sizeof(s->input));
    s->exti_level = 0;
    memset(s->dma, 0, sizeof(s->dma));
    s->adc_common = 0;
    if (!s->rtc_initialized) {
        ex_rtc_reset(s);
    } else {
        s->rtc[3] &= ~0xe0u; /* system reset preserves the backup calendar */
        s->rtc_unlocked = false;
        s->rtc_key = 0;
    }
    for (unsigned i = 0; i < 3; i++) {
        /* A processor reset cannot disconnect an external analog source. */
        memset(s->adc[i].reg, 0, sizeof(s->adc[i].reg));
        s->adc[i].reg[9] = 0xfff;
    }
    ex_adc_update(s);
    for (unsigned c = 0; c < 2; c++) {
        for (unsigned n = 0; n < 8; n++) {
            s->dma[c].stream[n].reg[5] = 0x21;
            ex_dma_update(s, c, n);
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->spi); i++) {
        ex_spi_reset(s, i);
    }
    for (unsigned i = 0; i < 3; i++) {
        ex_i2c_reset(s, i);
    }
    ex_exti_update(s);
    clock_update_hz(s->cpuclk, EX_HSI_HZ);
    clock_update_hz(s->refclk, EX_HSI_HZ / 8);
    ex_timer_clocks(s);
}

static void ex_reset_on_cpu(CPUState *cpu, run_on_cpu_data data)
{
    NikonEXState *s = data.host_ptr;
    /* AIRCR resets the MCU, not an independently powered I2C peripheral. */
    ex_reset(DEVICE(s));
    /* A guest-requested reset re-runs the boot loader like a real MCU. */
    nikon_ex_boot_image(memory_region_get_ram_ptr(&s->ram), EX_RAM_BASE,
                        EX_RAM_SIZE, memory_region_get_ram_ptr(&s->flash),
                        EX_FLASH_BASE, EX_FLASH_SIZE, NIKON_EX_SCATTER_TABLE);
    /* ARMv7M is a QOM container, not a parent bus for these devices. */
    device_cold_reset(DEVICE(&s->armv7m.nvic));
    device_cold_reset(DEVICE(&s->armv7m.systick[M_REG_NS]));
    for (unsigned i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        ex_reset_port(s, i);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->uart); i++) {
        device_cold_reset(s->uart[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->timer); i++) {
        device_cold_reset(s->timer[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->timer_or); i++) {
        device_cold_reset(DEVICE(&s->timer_or[i]));
    }
    cpu_reset(cpu);
}

static void ex_reset_requested(void *opaque)
{
    NikonEXState *s = opaque;

    /* Reset the running M-profile core on its own vCPU thread under MTTCG. */
    run_on_cpu(CPU(s->armv7m.cpu), ex_reset_on_cpu, RUN_ON_CPU_HOST_PTR(s));
}

static void ex_clear_first_exception(NikonEXState *s);

static void ex_reset_hold(Object *obj, ResetType type)
{
    NikonEXState *s = NIKON_EX(obj);

    s->resetting_ports = (1 << ARRAY_SIZE(s->gpio)) - 1;
    timer_del(s->exception_poll);
    ex_clear_first_exception(s);
    ex_reset(DEVICE(obj));
    /* A boot loader re-applies the scatter table on every reset. */
    nikon_ex_boot_image(memory_region_get_ram_ptr(&s->ram), EX_RAM_BASE,
                        EX_RAM_SIZE, memory_region_get_ram_ptr(&s->flash),
                        EX_FLASH_BASE, EX_FLASH_SIZE,
                        NIKON_EX_SCATTER_TABLE);
}

static void ex_reset_exit(Object *obj, ResetType type)
{
    NikonEXState *s = NIKON_EX(obj);
    for (unsigned i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        ex_replay_port(s, i);
    }
    s->resetting_ports = 0;
    /* Bus-less CPUs do not participate in the SysBus device reset walk. */
    cpu_reset(CPU(s->armv7m.cpu));
    timer_mod(s->exception_poll,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

static void ex_sysresetreq(void *opaque, int n, int level)
{
    NikonEXState *s = opaque;
    if (level) {
        qemu_bh_schedule(s->reset_bh);
    }
}

/* Capture the first M-profile exception without altering exception routing. */
static void ex_exception_sample_on_cpu(CPUState *cpu, run_on_cpu_data data)
{
    NikonEXState *s = data.host_ptr;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    if (s->first_exception_valid ||
        (cpu->exception_index < 0 && env->v7m.exception == 0)) {
        return;
    }
    s->first_exception_valid = true;
    s->first_exception_index = cpu->exception_index;
    s->first_exception_pc = env->regs[15];
    s->first_exception_cpsr = env->uncached_cpsr;
    s->first_exception_number = env->v7m.exception;
    s->first_exception_cfsr = env->v7m.cfsr[M_REG_NS];
    s->first_exception_dfsr = env->v7m.dfsr;
    s->first_exception_mmfar = env->v7m.mmfar[M_REG_NS];
    s->first_exception_bfar = env->v7m.bfar;
    s->first_exception_irq_state = env->irq_line_state;
    s->first_exception_secure = env->v7m.secure;
    s->first_exception_fsr = env->exception.fsr;
    s->first_exception_fault_address = env->exception.vaddress;
}

static void ex_exception_poll(void *opaque)
{
    NikonEXState *s = opaque;

    if (s->armv7m.cpu && !s->first_exception_valid) {
        run_on_cpu(CPU(s->armv7m.cpu), ex_exception_sample_on_cpu,
                   RUN_ON_CPU_HOST_PTR(s));
    }
    if (!s->first_exception_valid) {
        timer_mod(s->exception_poll,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    }
}

static bool ex_get_first_exception(Object *obj, Error **errp)
{
    return NIKON_EX(obj)->first_exception_valid;
}

static void ex_clear_first_exception(NikonEXState *s)
{
    s->first_exception_valid = false;
    s->first_exception_index = 0;
    s->first_exception_pc = 0;
    s->first_exception_cpsr = 0;
    s->first_exception_number = 0;
    s->first_exception_cfsr = 0;
    s->first_exception_dfsr = 0;
    s->first_exception_mmfar = 0;
    s->first_exception_bfar = 0;
    s->first_exception_irq_state = 0;
    s->first_exception_secure = 0;
    s->first_exception_fsr = 0;
    s->first_exception_fault_address = 0;
}

static void ex_map_device(NikonEXState *s, DeviceState *dev, uint32_t base,
                          int irq)
{
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion_overlap(&s->memory, base,
                                        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 10);
    if (irq >= 0) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                            qdev_get_gpio_in(DEVICE(&s->armv7m), irq));
    }
}

static void ex_realize(DeviceState *dev, Error **errp)
{
    NikonEXState *s = NIKON_EX(dev);
    DeviceState *armv7m = DEVICE(&s->armv7m);
    static const uint32_t uart_base[] = { 0x40011000, 0x40004400, 0x40004800,
                                         0x40004c00, 0x40005000, 0x40011400 };
    static const int uart_irq[] = { 37, 38, 39, 52, 53, 71 };
    static const uint32_t timer_base[] = { 0x40000000, 0x40000400, 0x40000800,
        0x40000c00, 0x40001000, 0x40001400, 0x40001800, 0x40001c00,
        0x40002000, 0x40010000, 0x40010400, 0x40014000, 0x40014400, 0x40014800 };
    static const int timer_irq[] = { 28, 29, 30, 50, 54, 55, -1, -1,
                                    -1, -1, -1, -1, -1, -1 };
    static const unsigned timer_channels[] = { 4, 4, 4, 4, 0, 0, 2, 1,
                                               1, 4, 4, 2, 1, 1 };
    static const unsigned timer_number[] = { 2, 3, 4, 5, 6, 7, 12, 13,
                                             14, 1, 8, 9, 10, 11 };
    static const unsigned shared_irq[] = { 24, 25, 26, 43, 44, 45 };
    static const unsigned shared_small[] = { 11, 12, 13, 6, 7, 8 };
    static const unsigned advanced_output[] = { 3, 0, 2 };

    memory_region_init(&s->memory, OBJECT(s), "ex.address-space", UINT64_C(0x100000000));
    address_space_init(&s->dma_as, &s->memory, "ex.dma");
    memory_region_init_rom(&s->flash, OBJECT(s), "ex.flash", EX_FLASH_SIZE, &error_fatal);
    memory_region_add_subregion(&s->memory, EX_FLASH_BASE, &s->flash);
    memory_region_init_alias(&s->flash_alias, OBJECT(s), "ex.reset-alias",
                             &s->flash, 0, EX_FLASH_SIZE);
    memory_region_add_subregion(&s->memory, 0, &s->flash_alias);
    memory_region_init_ram(&s->ram, OBJECT(s), "ex.sram", EX_RAM_SIZE, &error_fatal);
    memory_region_add_subregion(&s->memory, EX_RAM_BASE, &s->ram);
    if (s->firmware) {
        memcpy(memory_region_get_ram_ptr(&s->flash),
                g_bytes_get_data(s->firmware, NULL), EX_FLASH_SIZE);
        /* Cold boot: the loader owns the data segment, before the CPU runs. */
        nikon_ex_boot_image(memory_region_get_ram_ptr(&s->ram), EX_RAM_BASE,
                            EX_RAM_SIZE, memory_region_get_ram_ptr(&s->flash),
                            EX_FLASH_BASE, EX_FLASH_SIZE,
                            NIKON_EX_SCATTER_TABLE);
    }
    memory_region_init_io(&s->peripheral, OBJECT(s), &ex_peripheral_ops, s,
                          "ex.peripherals", 0x30000);
    memory_region_add_subregion(&s->memory, 0x40000000, &s->peripheral);
    for (unsigned i = 0; i < ARRAY_SIZE(s->spi); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        memory_region_add_subregion_overlap(&s->memory, ex_spi_base[i],
                                            &s->spi[i].memory, 10);
    }

    {
        /*
         * The media record path builds a descriptor from the constant
         * 0x50060800 and drives control/status/data words there.  Model that
         * finite block; see hw/nikon/devices/exrng.c.
         */
        DeviceState *rng = qdev_new(TYPE_NIKON_EX_RNG);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(rng), &error_fatal);
        memory_region_add_subregion(&s->memory, 0x50060800,
                                    sysbus_mmio_get_region(SYS_BUS_DEVICE(rng), 0));
    }
    qdev_prop_set_uint32(armv7m, "num-irq", 97);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->cpuclk);
    qdev_connect_clock_in(armv7m, "refclk", s->refclk);
    object_property_set_link(OBJECT(armv7m), "memory", OBJECT(&s->memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(armv7m), errp)) {
        return;
    }
    s->reset_irq = qemu_allocate_irq(ex_sysresetreq, s, 0);
    qdev_connect_gpio_out_named(armv7m, "SYSRESETREQ", 0, s->reset_irq);
    for (unsigned i = 0; i < ARRAY_SIZE(uart_base); i++) {
        DeviceState *uart = qdev_new(TYPE_STM32F2XX_USART);
        s->uart[i] = uart;
        qdev_prop_set_chr(uart, "chardev", serial_hd(i));
        ex_map_device(s, uart, uart_base[i], uart_irq[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(timer_base); i++) {
        DeviceState *timer = qdev_new(TYPE_NIKON_EX_TIMER);
        g_autofree char *name = g_strdup_printf("timer%u", i);
        s->timer[i] = timer;
        object_property_add_child(OBJECT(s), name, OBJECT(timer));
        qdev_prop_set_uint32(timer, "width", i == 0 || i == 3 ? 32 : 16);
        qdev_prop_set_uint32(timer, "channels", timer_channels[i]);
        qdev_prop_set_uint32(timer, "number", timer_number[i]);
        qdev_prop_set_bit(timer, "advanced", i == 9 || i == 10);
        qdev_prop_set_uint64(timer, "base", timer_base[i]);
        object_property_set_link(OBJECT(timer), "cpu", OBJECT(s->armv7m.cpu),
                                 &error_abort);
        qdev_connect_clock_in(timer, "clk", s->timerclk[i]);
        ex_map_device(s, timer, timer_base[i], timer_irq[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->timer_or); i++) {
        qdev_prop_set_uint32(DEVICE(&s->timer_or[i]), "num-lines", 2);
        qdev_realize(DEVICE(&s->timer_or[i]), NULL, &error_fatal);
        qdev_connect_gpio_out(DEVICE(&s->timer_or[i]), 0,
                              qdev_get_gpio_in(armv7m, shared_irq[i]));
        unsigned advanced = i < 3 ? 9 : 10;
        sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[advanced]),
                            advanced_output[i % 3],
                            qdev_get_gpio_in(DEVICE(&s->timer_or[i]), 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[shared_small[i]]), 0,
                            qdev_get_gpio_in(DEVICE(&s->timer_or[i]), 1));
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[9]), 1,
                        qdev_get_gpio_in(armv7m, 27));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[10]), 1,
                        qdev_get_gpio_in(armv7m, 46));
    for (unsigned i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        DeviceState *gpio = qdev_new(TYPE_STM32L4X5_GPIO);
        g_autofree char *name = g_strdup_printf("EX GPIO%c", 'A' + i);
        s->gpio[i] = gpio;
        qdev_prop_set_string(gpio, "name", name);
        qdev_prop_set_uint32(gpio, "mode-reset", i == 0 ? 0xa8000000 :
                                                       i == 1 ? 0x280 : 0);
        qdev_prop_set_uint32(gpio, "ospeed-reset", i == 1 ? 0xc0 : 0);
        qdev_prop_set_uint32(gpio, "pupd-reset", i == 0 ? 0x64000000 :
                                                       i == 1 ? 0x100 : 0);
        qdev_connect_clock_in(gpio, "clk", s->cpuclk);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio), &error_fatal);
        /* Only the common F4 register range is exposed, no L4 analog registers. */
        memory_region_init_alias(&s->gpio_window[i], OBJECT(s), name,
                                 sysbus_mmio_get_region(SYS_BUS_DEVICE(gpio), 0), 0, 0x28);
        memory_region_add_subregion_overlap(&s->memory, 0x40020000 + i * 0x400,
                                            &s->gpio_window[i], 10);
        for (unsigned pin = 0; pin < 16; pin++) {
            s->route[i][pin] = (EXPinRoute) { s, i, pin };
            s->pin_route_irq[i][pin] = qemu_allocate_irq(ex_pin_change,
                                                        &s->route[i][pin], 0);
            qdev_connect_gpio_out(gpio, pin, s->pin_route_irq[i][pin]);
        }
    }
    s->io = DEVICE(i2c_slave_new(TYPE_NIKON_EX_IO, 0x22));
    object_property_add_child(OBJECT(s), "io", OBJECT(s->io));
    /* Selected mode 8: handle 0x1a is EXTI8; late GPIO setup selects C8. */
    qdev_connect_gpio_out_named(s->io, "interrupt", 0,
                               nikon_ex_gpio_input(s, 2, 8));
    i2c_slave_realize_and_unref(I2C_SLAVE(s->io), s->i2c[0].bus, &error_fatal);
    const struct {
        const char *type;
        const char *name;
        uint8_t address;
    } power[] = {
        { TYPE_NIKON_EX_POWER, "power", 0x4f },
        { TYPE_NIKON_EX_POWER_AUX, "poweraux", 0x10 },
    };
    for (unsigned i = 0; i < ARRAY_SIZE(power); i++) {
        I2CSlave *peer = i2c_slave_new(power[i].type, power[i].address);

        object_property_add_child(OBJECT(s), power[i].name, OBJECT(peer));
        i2c_slave_realize_and_unref(peer, s->i2c[0].bus, &error_fatal);
    }
    {
        I2CSlave *monitor = i2c_slave_new(TYPE_NIKON_EX_MONITOR, 0x6b);
        I2CSlave *status = i2c_slave_new(TYPE_NIKON_EX_STATUS, 0x0d);

        object_property_add_child(OBJECT(s), "monitor", OBJECT(monitor));
        object_property_add_child(OBJECT(s), "status", OBJECT(status));
        i2c_slave_realize_and_unref(monitor, s->i2c[0].bus, &error_fatal);
        i2c_slave_realize_and_unref(status, s->i2c[0].bus, &error_fatal);
    }
}

static void ex_init(Object *obj)
{
    NikonEXState *s = NIKON_EX(obj);
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);
    for (unsigned i = 0; i < ARRAY_SIZE(s->timer_or); i++) {
        object_initialize_child(obj, "timer-or[*]", &s->timer_or[i], TYPE_OR_IRQ);
    }
    s->cpuclk = clock_new(obj, "cpuclk");
    s->refclk = clock_new(obj, "refclk");
    for (unsigned i = 0; i < ARRAY_SIZE(s->timerclk); i++) {
        g_autofree char *name = g_strdup_printf("timerclk%u", i);
        s->timerclk[i] = clock_new(obj, name);
    }
    clock_set_hz(s->cpuclk, EX_HSI_HZ);
    clock_set_hz(s->refclk, EX_HSI_HZ / 8);
    s->osc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ex_clock_settle, s);
    s->exception_poll = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     ex_exception_poll, s);
    s->rtc_sync = timer_new_ns(QEMU_CLOCK_VIRTUAL, ex_rtc_sync, s);
    s->rtc_tick = timer_new_ns(QEMU_CLOCK_VIRTUAL, ex_rtc_tick, s);
    s->rtc_wakeup = timer_new_ns(QEMU_CLOCK_VIRTUAL, ex_rtc_wakeup, s);
    s->reset_bh = qemu_bh_new(ex_reset_requested, s);
    s->adc_bh = qemu_bh_new(ex_adc_dispatch, s);
    qdev_init_gpio_out_named(DEVICE(s), s->pad_output, "pad", ARRAY_SIZE(s->pad_output));
    qdev_init_gpio_in_named(DEVICE(s), ex_external_input, "input", 9 * 16);
    qdev_init_gpio_in_named(DEVICE(s), ex_adc_input, "adc", 3 * 19);
    object_property_add_bool(obj, "first-exception", ex_get_first_exception,
                             NULL);
    object_property_add_uint32_ptr(obj, "first-exception-index",
                                   &s->first_exception_index,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-pc",
                                   &s->first_exception_pc,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-cpsr",
                                   &s->first_exception_cpsr,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-number",
                                   &s->first_exception_number,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-cfsr",
                                   &s->first_exception_cfsr,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-dfsr",
                                   &s->first_exception_dfsr,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-mmfar",
                                   &s->first_exception_mmfar,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-bfar",
                                   &s->first_exception_bfar,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-irq-state",
                                   &s->first_exception_irq_state,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-secure",
                                   &s->first_exception_secure,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-fsr",
                                   &s->first_exception_fsr,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-exception-fault-address",
                                   &s->first_exception_fault_address,
                                   OBJ_PROP_FLAG_READ);
    for (unsigned i = 0; i < 3; i++) {
        g_autofree char *name = g_strdup_printf("i2c%u", i + 1);
        s->i2c[i].bus = i2c_init_bus(DEVICE(s), name);
        s->i2c[i].processor = s;
        s->i2c[i].channel = i;
        s->i2c[i].rx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          ex_i2c_rx_shift, &s->i2c[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->spi); i++) {
        g_autofree char *port_name = g_strdup_printf("port%u", i + 1);
        g_autofree char *spi_name = g_strdup_printf("spi%u", i + 1);
        object_initialize_child(OBJECT(s), port_name, &s->spi[i], TYPE_EX_SPI_PORT);
        s->spi[i].processor = s;
        s->spi[i].channel = i;
        s->spi[i].bus = ssi_create_bus(DEVICE(&s->spi[i]), spi_name);
    }
}

static void ex_finalize(Object *obj)
{
    NikonEXState *s = NIKON_EX(obj);
    timer_free(s->osc_timer);
    for (unsigned i = 0; i < 3; i++) {
        timer_free(s->i2c[i].rx_timer);
    }
    timer_free(s->exception_poll);
    timer_free(s->rtc_sync);
    timer_free(s->rtc_tick);
    timer_free(s->rtc_wakeup);
    qemu_bh_delete(s->reset_bh);
    qemu_bh_delete(s->adc_bh);
    qemu_free_irq(s->reset_irq);
    g_clear_pointer(&s->firmware, g_bytes_unref);
    if (DEVICE(s)->realized) {
        address_space_destroy(&s->dma_as);
        for (unsigned i = 0; i < 9; i++) {
            for (unsigned pin = 0; pin < 16; pin++) {
                qemu_free_irq(s->pin_route_irq[i][pin]);
            }
        }
    }
}

static void ex_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    dc->realize = ex_realize;
    rc->phases.hold = ex_reset_hold;
    rc->phases.exit = ex_reset_exit;
    dc->desc = "Nikon EX Cortex-M4 software functional profile";
    dc->user_creatable = false;
}

static void ex_machine_init(MachineState *machine)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_EX);
    NikonEXState *s = NIKON_EX(dev);
    object_property_add_child(OBJECT(machine), "ex", OBJECT(dev));
    if (machine->firmware) {
        NikonBundle *bundle = nikon_bundle_load(machine->firmware, &error_fatal);
        GBytes *component = nikon_bundle_component(bundle, NIKON_COMPONENT_EX);
        nikon_ex_set_firmware(s, component, &error_fatal);
        g_bytes_unref(component);
        nikon_bundle_free(bundle);
    } else if (!qtest_enabled()) {
        error_report("nikonex requires -bios with a decoded complete component bundle");
        exit(EXIT_FAILURE);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    /* ARMv7M already owns the private board region in its container. */
    memory_region_init_alias(&s->system_view, OBJECT(s), "ex.system-view",
                             &s->armv7m.container, 0, UINT64_C(0x100000000));
    memory_region_add_subregion(get_system_memory(), 0, &s->system_view);
}

static void ex_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "Nikon EX complete component functional profile";
    mc->init = ex_machine_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m4");
    mc->max_cpus = 1;
    mc->default_ram_size = 0;
    mc->no_floppy = true;
    mc->no_cdrom = true;
    mc->no_parallel = true;
}

#define TYPE_EX_SPI_SLAVE "nikon-ex-spi-slave"
typedef struct EXSPISlave {
    SSIPeripheral parent_obj;
    NikonEXState *processor;
    uint32_t channel;
    uint32_t master_bits;
    bool master_lsb_first;
} EXSPISlave;

static uint32_t ex_slave_transfer(SSIPeripheral *dev, uint32_t value)
{
    EXSPISlave *s = (EXSPISlave *)dev;
    EXSPI *spi = &s->processor->spi[s->channel];
    unsigned bits = spi->reg[0] & (1 << 11) ? 16 : 8;
    bool slave_lsb_first = spi->reg[0] & (1 << 7);
    uint32_t reply = 0;

    /* SSI carries numeric master words, not their width or serial bit order.
     * Every subword is a complete real EX clock, including its DMA requests.
     */
    for (unsigned n = 0; n < s->master_bits / bits; n++) {
        unsigned shift = s->master_lsb_first ? n * bits :
                         s->master_bits - (n + 1) * bits;
        uint32_t input = extract32(value, shift, bits);
        bool reverse = s->master_lsb_first != slave_lsb_first;
        if (reverse) {
            input = bits == 8 ? revbit8(input) : revbit16(input);
        }
        uint32_t output = nikon_ex_spi_transfer(s->processor, s->channel, input);
        if (reverse) {
            output = bits == 8 ? revbit8(output) : revbit16(output);
        }
        reply |= output << shift;
    }
    return reply;
}

static void ex_slave_realize(SSIPeripheral *dev, Error **errp)
{
    EXSPISlave *s = (EXSPISlave *)dev;
    if (!s->processor || !DEVICE(s->processor)->realized || s->channel >= 4) {
        error_setg(errp, "EX SPI endpoint needs a realized processor and channel 0..3");
        return;
    }
    if (s->master_bits != 16) {
        error_setg(errp, "EX MCU peer profile requires 16-bit master words");
    }
}

static const Property ex_slave_properties[] = {
    DEFINE_PROP_LINK("processor", EXSPISlave, processor, TYPE_NIKON_EX,
                     NikonEXState *),
    DEFINE_PROP_UINT32("channel", EXSPISlave, channel, 0),
    DEFINE_PROP_UINT32("master-word-bits", EXSPISlave, master_bits, 16),
    DEFINE_PROP_BOOL("master-lsb-first", EXSPISlave, master_lsb_first, true),
};

static void ex_slave_class_init(ObjectClass *klass, const void *data)
{
    SSIPeripheralClass *sc = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    sc->realize = ex_slave_realize;
    sc->transfer = ex_slave_transfer;
    /* The original EX configures SSM=1, SSI=0; no guessed physical NSS pad. */
    sc->cs_polarity = SSI_CS_NONE;
    device_class_set_props(dc, ex_slave_properties);
}

DeviceState *nikon_ex_spi_slave(NikonEXState *s, unsigned channel, SSIBus *bus,
                                uint8_t chip_select, unsigned master_bits,
                                bool master_lsb_first, Error **errp)
{
    DeviceState *dev = qdev_new(TYPE_EX_SPI_SLAVE);
    object_property_set_link(OBJECT(dev), "processor", OBJECT(s), &error_abort);
    qdev_prop_set_uint32(dev, "channel", channel);
    qdev_prop_set_uint8(dev, "cs", chip_select);
    qdev_prop_set_uint32(dev, "master-word-bits", master_bits);
    qdev_prop_set_bit(dev, "master-lsb-first", master_lsb_first);
    if (!ssi_realize_and_unref(dev, bus, errp)) {
        return NULL;
    }
    return dev; /* owned by the master SSI bus */
}

static const TypeInfo ex_types[] = {
    { .name = TYPE_EX_SPI_PORT, .parent = TYPE_SYS_BUS_DEVICE,
      .instance_size = sizeof(EXSPI), .class_init = ex_spi_port_class_init },
    { .name = TYPE_EX_SPI_SLAVE, .parent = TYPE_SSI_PERIPHERAL,
      .instance_size = sizeof(EXSPISlave), .class_init = ex_slave_class_init },
    { .name = TYPE_NIKON_EX, .parent = TYPE_SYS_BUS_DEVICE,
      .instance_size = sizeof(NikonEXState), .instance_init = ex_init,
      .instance_finalize = ex_finalize, .class_init = ex_class_init },
    { .name = MACHINE_TYPE_NAME("nikonex"), .parent = TYPE_MACHINE,
      .class_init = ex_machine_class_init,
      .interfaces = arm_machine_interfaces },
};

DEFINE_TYPES(ex_types)
