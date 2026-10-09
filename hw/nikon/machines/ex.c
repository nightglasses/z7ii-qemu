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
#include "hw/char/stm32f2xx_usart.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/ex.h"
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

typedef struct EXSPI {
    uint32_t reg[9];
    uint32_t txdata;
    bool tx_valid;
    SSIBus *bus;
} EXSPI;

typedef struct EXI2C {
    uint32_t reg[9];
    bool sr1_seen;
    bool receive;
    I2CBus *bus;
} EXI2C;

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
    MemoryRegion flash;
    MemoryRegion flash_alias;
    MemoryRegion ram;
    MemoryRegion peripheral;
    MemoryRegion gpio_window[9];
    AddressSpace dma_as;
    Clock *cpuclk;
    Clock *refclk;
    DeviceState *gpio[9];
    DeviceState *uart[6];
    DeviceState *timer[14];
    OrIRQState timer_or[2];
    EXPinRoute route[9][16];
    qemu_irq pin_route_irq[9][16];
    GBytes *firmware;
    uint32_t rcc[38];
    uint32_t pwr[2];
    uint32_t flashreg[6];
    uint32_t syscfg[9];
    uint32_t exti[6];
    uint16_t input[9];
    uint16_t exti_level;
    EXSPI spi[3];
    EXI2C i2c[3];
    EXDMA dma[2];
    EXADC adc[3];
    uint32_t adc_common;
    QEMUTimer *osc_timer;
    QEMUBH *reset_bh;
    qemu_irq reset_irq;
    bool dma_running;
};

static const uint32_t ex_spi_base[] = { 0x40013000, 0x40003800, 0x40003c00 };
static const int ex_spi_irq[] = { 35, 36, 51 };
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

SSIBus *nikon_ex_spi_bus(NikonEXState *s, unsigned channel)
{
    assert(channel < ARRAY_SIZE(s->spi));
    return s->spi[channel].bus;
}

qemu_irq nikon_ex_gpio_input(NikonEXState *s, unsigned port, unsigned pin)
{
    assert(port < ARRAY_SIZE(s->gpio) && pin < 16);
    return qdev_get_gpio_in(s->gpio[port], pin);
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
}

static void ex_pin_change(void *opaque, int n, int level)
{
    EXPinRoute *r = opaque;
    NikonEXState *s = r->soc;
    uint32_t bit = 1 << r->pin;
    unsigned selected = extract32(s->syscfg[2 + r->pin / 4],
                                  4 * (r->pin % 4), 4);
    bool old = s->exti_level & bit;
    s->input[r->port] = deposit32(s->input[r->port], r->pin, 1, !!level);
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
        clock_set_hz(s->cpuclk, hz);
        clock_set_hz(s->refclk, hz / 8);
    }
}

static void ex_spi_reset(NikonEXState *s, unsigned i)
{
    memset(s->spi[i].reg, 0, sizeof(s->spi[i].reg));
    s->spi[i].reg[2] = 2;
    s->spi[i].reg[4] = 7;
    s->spi[i].reg[8] = 2;
    s->spi[i].tx_valid = false;
    s->spi[i].txdata = 0;
    ex_irq(s, ex_spi_irq[i], false);
}

static void ex_i2c_reset(NikonEXState *s, unsigned i)
{
    i2c_end_transfer(s->i2c[i].bus);
    memset(s->i2c[i].reg, 0, sizeof(s->i2c[i].reg));
    s->i2c[i].sr1_seen = s->i2c[i].receive = false;
    ex_irq(s, ex_i2c_irq[i], false);
    ex_irq(s, ex_i2c_irq[i] + 1, false);
}

static void ex_rcc_reset_devices(NikonEXState *s, unsigned off, uint32_t bits)
{
    if (off == 0x10) {
        for (unsigned i = 0; i < 9; i++) {
            if (bits & (1 << i)) {
                device_cold_reset(s->gpio[i]);
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

void nikon_ex_adc_input(NikonEXState *s, unsigned adc, unsigned channel,
                        uint16_t sample)
{
    assert(adc < 3 && channel < 19 && sample < 4096);
    s->adc[adc].sample[channel] = sample;
    s->adc[adc].valid |= 1 << channel;
    ex_adc_convert(s, adc);
}

uint32_t nikon_ex_spi_transfer(NikonEXState *s, unsigned channel, uint32_t value)
{
    assert(channel < ARRAY_SIZE(s->spi));
    EXSPI *p = &s->spi[channel];
    unsigned mask = p->reg[0] & (1 << 11) ? 0xffff : 0xff;
    uint32_t outgoing = p->tx_valid ? p->txdata & mask : mask;

    if (!(p->reg[0] & (1 << 6)) || (p->reg[0] & (1 << 2))) {
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
            if (aligned >= ex_spi_base[i] && aligned <= ex_spi_base[i] + 0x20) {
                unsigned reg = (aligned - ex_spi_base[i]) / 4;
                v = s->spi[i].reg[reg];
                if (reg == 3 && consume) {
                    s->spi[i].reg[2] &= ~1u;
                    ex_spi_update(s, i);
                }
                goto found;
            }
            if (aligned >= ex_i2c_base[i] && aligned <= ex_i2c_base[i] + 0x20) {
                EXI2C *p = &s->i2c[i];
                unsigned reg = (aligned - ex_i2c_base[i]) / 4;
                if (consume && reg == 4 && p->receive && i2c_bus_busy(p->bus)) {
                    p->reg[4] = i2c_recv(p->bus);
                    if (!(p->reg[0] & (1 << 10))) {
                        i2c_nack(p->bus);
                    }
                }
                v = p->reg[reg];
                if (consume && reg == 5) {
                    p->sr1_seen = true;
                } else if (consume && reg == 6 && p->sr1_seen) {
                    p->reg[5] &= ~2u;
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
        ex_rcc_reset_devices(s, off, v & ~previous);
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
                    ex_adc_convert(s, i);
                }
                ex_adc_update(s);
                return MEMTX_OK;
            }
        }
        for (unsigned i = 0; i < 3; i++) {
            if (aligned >= ex_spi_base[i] && aligned <= ex_spi_base[i] + 0x20) {
                EXSPI *p = &s->spi[i];
                unsigned reg = (aligned - ex_spi_base[i]) / 4;
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
                        unsigned word_mask = p->reg[0] & (1 << 11) ? 0xffff : 0xff;
                        if (p->reg[2] & 1) {
                            p->reg[2] |= 1 << 6;
                        } else {
                            p->reg[3] = ssi_transfer(p->bus, v & word_mask) & word_mask;
                            p->reg[2] |= 1;
                        }
                        p->tx_valid = false;
                        p->reg[2] |= 2;
                    }
                }
                ex_spi_update(s, i);
                return MEMTX_OK;
            }
            if (aligned >= ex_i2c_base[i] && aligned <= ex_i2c_base[i] + 0x20) {
                EXI2C *p = &s->i2c[i];
                unsigned reg = (aligned - ex_i2c_base[i]) / 4;
                if (reg == 5) {
                    p->reg[5] &= v | 0xff;
                } else if (reg == 6) {
                    return ex_bad(s, addr, size, true);
                } else {
                    p->reg[reg] = v;
                }
                if (reg == 0) {
                    if ((v & (1 << 15)) || !(v & 1)) {
                        i2c_end_transfer(p->bus);
                        p->reg[5] = p->reg[6] = 0;
                    } else if (v & (1 << 9)) {
                        i2c_end_transfer(p->bus);
                        p->reg[6] = 0;
                        p->reg[5] &= ~0xc7u;
                        p->reg[0] &= ~(1 << 9);
                    } else if (v & (1 << 8)) {
                        p->reg[5] = 1; /* START generated, address required */
                        p->reg[6] = 3;
                        p->reg[0] &= ~(1 << 8);
                    }
                } else if (reg == 4 && (p->reg[0] & 1)) {
                    if (p->reg[5] & 1) {
                        p->receive = v & 1;
                        p->reg[5] &= ~1u;
                        if (i2c_start_transfer(p->bus, (v >> 1) & 0x7f, p->receive)) {
                            p->reg[5] |= 1 << 10; /* real absent slave NACK */
                        } else {
                            p->reg[5] |= 2 | (p->receive ? 0x40 : 0x80);
                            p->reg[6] = p->receive ? 3 : 7;
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

static void ex_reset(DeviceState *dev)
{
    NikonEXState *s = NIKON_EX(dev);
    qemu_bh_cancel(s->reset_bh);
    timer_del(s->osc_timer);
    memset(s->rcc, 0, sizeof(s->rcc));
    s->rcc[0] = 0x83;
    s->rcc[1] = 0x24003010;
    s->rcc[0x74 / 4] = 0x0e000000;
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
    memset(s->adc, 0, sizeof(s->adc));
    s->adc_common = 0;
    for (unsigned i = 0; i < 3; i++) {
        s->adc[i].reg[9] = 0xfff;
    }
    ex_adc_update(s);
    for (unsigned c = 0; c < 2; c++) {
        for (unsigned n = 0; n < 8; n++) {
            s->dma[c].stream[n].reg[5] = 0x21;
            ex_dma_update(s, c, n);
        }
    }
    for (unsigned i = 0; i < 3; i++) {
        ex_spi_reset(s, i);
        ex_i2c_reset(s, i);
    }
    ex_exti_update(s);
    clock_set_hz(s->cpuclk, EX_HSI_HZ);
    clock_set_hz(s->refclk, EX_HSI_HZ / 8);
}

static void ex_reset_requested(void *opaque)
{
    device_cold_reset(DEVICE(opaque));
}

static void ex_sysresetreq(void *opaque, int n, int level)
{
    NikonEXState *s = opaque;
    if (level) {
        qemu_bh_schedule(s->reset_bh);
    }
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
    static const int timer_irq[] = { 28, 29, 30, 50, 54, 55, 43, -1,
                                     45, -1, -1, 24, -1, 26 };

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
    }
    memory_region_init_io(&s->peripheral, OBJECT(s), &ex_peripheral_ops, s,
                          "ex.peripherals", 0x30000);
    memory_region_add_subregion(&s->memory, 0x40000000, &s->peripheral);

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
        DeviceState *timer = qdev_new(TYPE_STM32F2XX_TIMER);
        s->timer[i] = timer;
        qdev_prop_set_uint64(timer, "clock-frequency", EX_HSI_HZ);
        ex_map_device(s, timer, timer_base[i], timer_irq[i]);
    }
    for (unsigned i = 0; i < 2; i++) {
        qdev_prop_set_uint32(DEVICE(&s->timer_or[i]), "num-lines", 2);
        qdev_realize(DEVICE(&s->timer_or[i]), NULL, &error_fatal);
        qdev_connect_gpio_out(DEVICE(&s->timer_or[i]), 0,
                              qdev_get_gpio_in(armv7m, i ? 44 : 25));
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[9]), 0,
                        qdev_get_gpio_in(DEVICE(&s->timer_or[0]), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[12]), 0,
                        qdev_get_gpio_in(DEVICE(&s->timer_or[0]), 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[10]), 0,
                        qdev_get_gpio_in(DEVICE(&s->timer_or[1]), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->timer[7]), 0,
                        qdev_get_gpio_in(DEVICE(&s->timer_or[1]), 1));
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
}

static void ex_init(Object *obj)
{
    NikonEXState *s = NIKON_EX(obj);
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);
    for (unsigned i = 0; i < 2; i++) {
        object_initialize_child(obj, "timer-or[*]", &s->timer_or[i], TYPE_OR_IRQ);
    }
    s->cpuclk = clock_new(obj, "cpuclk");
    s->refclk = clock_new(obj, "refclk");
    clock_set_hz(s->cpuclk, EX_HSI_HZ);
    clock_set_hz(s->refclk, EX_HSI_HZ / 8);
    s->osc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ex_clock_settle, s);
    s->reset_bh = qemu_bh_new(ex_reset_requested, s);
    for (unsigned i = 0; i < 3; i++) {
        g_autofree char *name = g_strdup_printf("i2c%u", i + 1);
        g_autofree char *spi_name = g_strdup_printf("spi%u", i + 1);
        s->i2c[i].bus = i2c_init_bus(DEVICE(s), name);
        s->spi[i].bus = ssi_create_bus(DEVICE(s), spi_name);
    }
}

static void ex_finalize(Object *obj)
{
    NikonEXState *s = NIKON_EX(obj);
    timer_free(s->osc_timer);
    qemu_bh_delete(s->reset_bh);
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
    dc->realize = ex_realize;
    device_class_set_legacy_reset(dc, ex_reset);
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
    memory_region_add_subregion(get_system_memory(), 0, &s->memory);
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

static const TypeInfo ex_types[] = {
    { .name = TYPE_NIKON_EX, .parent = TYPE_SYS_BUS_DEVICE,
      .instance_size = sizeof(NikonEXState), .instance_init = ex_init,
      .instance_finalize = ex_finalize, .class_init = ex_class_init },
    { .name = MACHINE_TYPE_NAME("nikonex"), .parent = TYPE_MACHINE,
      .class_init = ex_machine_class_init },
};

DEFINE_TYPES(ex_types)
