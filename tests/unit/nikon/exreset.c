/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/main-loop.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/boot.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/ex.h"
#include "system/memory.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "target/arm/cpu.h"

int (*qemu_main)(void);

typedef struct ResetFixture {
    NikonEXState *ex;
    ARMCPU *cpu;
    AddressSpace *as;
} ResetFixture;

static ResetFixture processor(const char *name)
{
    g_autofree uint8_t *bytes = g_malloc0(0x80000);
    g_autoptr(GBytes) firmware = NULL;
    DeviceState *dev = qdev_new(TYPE_NIKON_EX);
    ARMv7MState *armv7m;
    ResetFixture f;

    /* Synthetic vector-only fixture; no original firmware is executed. */
    stl_le_p(bytes, 0x20000c08);
    stl_le_p(bytes + 4, 0x080002b5);
    firmware = g_bytes_new(bytes, 0x80000);
    object_property_add_child(object_get_objects_root(), name, OBJECT(dev));
    f.ex = NIKON_EX(dev);
    g_assert_true(nikon_ex_set_firmware(f.ex, firmware, &error_abort));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    armv7m = ARMV7M(object_resolve_path_component(OBJECT(dev), "armv7m"));
    f.cpu = armv7m->cpu;
    f.as = cpu_get_address_space(CPU(f.cpu), ARMASIdx_NS);
    return f;
}

static ResetFixture independent_processor(void)
{
    Object *owner = object_new(TYPE_CONTAINER);
    DeviceState *dev = qdev_new(TYPE_ARMV7M);
    ARMv7MState *armv7m = ARMV7M(dev);
    MemoryRegion *memory = g_new0(MemoryRegion, 1);
    MemoryRegion *flash = g_new0(MemoryRegion, 1);
    MemoryRegion *alias = g_new0(MemoryRegion, 1);
    MemoryRegion *ram = g_new0(MemoryRegion, 1);
    Clock *clock;
    uint8_t *vectors;
    ResetFixture f = { 0 };

    /* A second mature CPU domain, without a second board's singleton RAM IDs.
     * All storage and clocks below belong only to this synthetic fixture.
     */
    object_property_add_child(object_get_objects_root(), "reset.other", owner);
    object_property_add_child(owner, "processor", OBJECT(dev));
    memory_region_init(memory, OBJECT(dev), "reset.other.memory", UINT64_MAX);
    g_assert_true(memory_region_init_rom(flash, OBJECT(dev), "reset.other.flash",
                                        0x80000, &error_abort));
    vectors = memory_region_get_ram_ptr(flash);
    stl_le_p(vectors, 0x20000c08);
    stl_le_p(vectors + 4, 0x080002b5);
    memory_region_add_subregion(memory, 0x08000000, flash);
    memory_region_init_alias(alias, OBJECT(dev), "reset.other.alias", flash, 0,
                              0x80000);
    memory_region_add_subregion(memory, 0, alias);
    g_assert_true(memory_region_init_ram(ram, OBJECT(dev), "reset.other.sram",
                                        0x40000, &error_abort));
    memory_region_add_subregion(memory, 0x20000000, ram);
    clock = clock_new(owner, "clock");
    clock_set_hz(clock, 1000000); /* Fixture frequency, not camera evidence. */
    qdev_connect_clock_in(dev, "cpuclk", clock);
    qdev_connect_clock_in(dev, "refclk", clock);
    qdev_prop_set_string(dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_uint32(dev, "num-irq", 32);
    object_property_set_link(OBJECT(dev), "memory", OBJECT(memory),
                              &error_abort);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    f.cpu = armv7m->cpu;
    f.as = cpu_get_address_space(CPU(f.cpu), ARMASIdx_NS);
    /* The official M-profile board API registers its separate CPU reset. */
    armv7m_load_kernel(f.cpu, NULL, 0, 0x80000);
    return f;
}

static void write32(ResetFixture *f, uint32_t address, uint32_t value)
{
    uint32_t le = cpu_to_le32(value);

    g_assert_cmpint(address_space_write(f->as, address,
                        MEMTXATTRS_UNSPECIFIED, &le, sizeof(le)), ==, MEMTX_OK);
}

static uint32_t read32(ResetFixture *f, uint32_t address)
{
    uint32_t le;

    g_assert_cmpint(address_space_read(f->as, address,
                        MEMTXATTRS_UNSPECIFIED, &le, sizeof(le)), ==, MEMTX_OK);
    return le32_to_cpu(le);
}

static void dirty_cpu(CPUState *cs, run_on_cpu_data data)
{
    CPUARMState *env = &ARM_CPU(cs)->env;

    env->regs[15] = 0x0804909e;
    env->regs[13] = 0x20000b50;
    env->v7m.primask[M_REG_NS] = 1;
    env->v7m.basepri[M_REG_NS] = 0x80;
    xpsr_write(env, 0x0100000f, UINT32_MAX);
}

static void dirty(ResetFixture *f)
{
    run_on_cpu(CPU(f->cpu), dirty_cpu, RUN_ON_CPU_NULL);
    write32(f, 0xe000e100, 1 << 5); /* Enable external IRQ5. */
    write32(f, 0xe000e200, 1 << 5); /* Pend it without executing the core. */
    write32(f, 0xe000ed08, 0x20000000);
    write32(f, 0xe000e014, 12345);
    write32(f, 0xe000e010, 7);
}

static void is_dirty(ResetFixture *f)
{
    g_assert_cmphex(f->cpu->env.regs[15], ==, 0x0804909e);
    g_assert_cmphex(f->cpu->env.regs[13], ==, 0x20000b50);
    g_assert_cmphex(xpsr_read(&f->cpu->env) & 0x1ff, ==, 15);
    g_assert_cmphex(read32(f, 0xe000e200) & (1 << 5), ==, 1 << 5);
    g_assert_cmphex(read32(f, 0xe000ed08), ==, 0x20000000);
    g_assert_cmphex(read32(f, 0xe000e014), ==, 12345);
    g_assert_cmphex(read32(f, 0xe000e010) & 7, ==, 7);
}

static void is_reset(ResetFixture *f)
{
    g_assert_cmphex(f->cpu->env.regs[15], ==, 0x080002b4);
    g_assert_cmphex(f->cpu->env.regs[13], ==, 0x20000c08);
    g_assert_cmphex(xpsr_read(&f->cpu->env) & 0x010001ff, ==, 0x01000000);
    g_assert_cmphex(f->cpu->env.v7m.primask[M_REG_NS], ==, 0);
    g_assert_cmphex(f->cpu->env.v7m.basepri[M_REG_NS], ==, 0);
    g_assert_cmphex(read32(f, 0xe000e100), ==, 0);
    g_assert_cmphex(read32(f, 0xe000e200), ==, 0);
    g_assert_cmphex(read32(f, 0xe000ed08), ==, 0);
    g_assert_cmphex(read32(f, 0xe000e014), ==, 0);
    g_assert_cmphex(read32(f, 0xe000e010) & 7, ==, 0);
}

static void system_reset(void)
{
    ResetFixture f = processor("reset.ex");
    ResetFixture other = independent_processor();

    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    is_reset(&f);
    is_reset(&other);
    write32(&f, 0x20001000, 0x12345678);
    qemu_set_irq(nikon_ex_gpio_input(f.ex, 0, 10), 1);
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        dirty(&f);
        dirty(&other);
        write32(&f, 0x40023808, 0x1000);
        /* A malformed key must not schedule or retire a reset. */
        write32(&f, 0xe000ed0c, 0x12340304);
        main_loop_wait(true);
        is_dirty(&f);
        is_dirty(&other);
        /* Real NVIC SYSRESETREQ output -> EX BH -> owning CPU reset. */
        write32(&f, 0xe000ed0c, 0x05fa0304);
        main_loop_wait(true);
        is_reset(&f);
        g_assert_cmphex(read32(&f, 0x40023808), ==, 0);
        is_dirty(&other);
        g_assert_cmphex(read32(&f, 0x20001000), ==, 0x12345678);
        g_assert_cmphex(read32(&f, 0x40020010) & (1 << 10), ==, 1 << 10);
    }
    /* The containing board's normal reset must also reach bus-less cores. */
    dirty(&f);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    is_reset(&f);
    is_reset(&other);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    vm_start();
    g_test_add_func("/nikon/ex/local-system-reset", system_reset);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
