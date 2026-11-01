/* Board-scoped Cortex-A7 CP15 state. No original firmware runs here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "target/arm/cpu.h"
#include "target/arm/cpregs.h"
#include "system/system.h"
#include "system/reset.h"

int (*qemu_main)(void);

static ARMCPU *board_cpu(unsigned index)
{
    g_autofree char *path = g_strdup_printf("/machine/cpu%u", index);
    ARMCPU *cpu = ARM_CPU(object_resolve_path(path, NULL));

    g_assert_nonnull(cpu);
    return cpu;
}

static const ARMCPRegInfo *reginfo(ARMCPU *cpu, bool ns)
{
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(cpu->cp_regs,
        ENCODE_CP_REG(15, false, ns, 1, 0, 0, 1));

    g_assert_nonnull(ri);
    return ri;
}

static void test_readback(void)
{
    ARMCPU *a = board_cpu(0), *b = board_cpu(1);
    const ARMCPRegInfo *ri = reginfo(a, false);

    cpu_reset(CPU(a));
    cpu_reset(CPU(b));
    g_assert_cmphex(read_raw_cp_reg(&a->env, ri), ==, 0x6000);
    g_assert_cmphex(read_raw_cp_reg(&a->env, ri) & 0x40, ==, 0);
    /* Original MAS's read/ORR/BIC/MCR sequence. */
    ri->writefn(&a->env, ri, (ri->readfn(&a->env, ri) | 0x40) & ~0x6000);
    g_assert_cmphex(ri->readfn(&a->env, ri), ==, 0x40);
    g_assert_cmphex(read_raw_cp_reg(&b->env, reginfo(b, false)), ==, 0x6000);
    ri->writefn(&a->env, ri, UINT32_MAX);
    g_assert_cmphex(ri->readfn(&a->env, ri), ==, 0x1000fc40);
    cpu_reset(CPU(a));
    g_assert_cmphex(ri->readfn(&a->env, ri), ==, 0x6000);
}

static void test_permissions(void)
{
    ARMCPU *cpu = board_cpu(0);
    CPUARMState *env = &cpu->env;
    const ARMCPRegInfo *ri = reginfo(cpu, true);

    cpu_reset(CPU(cpu));
    env->cp15.scr_el3 |= SCR_NS;
    env->cp15.nsacr = 0;
    g_assert_false(arm_is_secure(env));
    ri->writefn(env, ri, UINT32_MAX);
    g_assert_cmphex(ri->readfn(env, ri), ==, 0x6000);
    env->cp15.nsacr = 1U << 18;
    ri->writefn(env, ri, UINT32_MAX);
    g_assert_cmphex(ri->readfn(env, ri), ==, 0x6040);
    ri->writefn(env, ri, 0);
    g_assert_cmphex(ri->readfn(env, ri), ==, 0x6000);
    /* Preserve the upstream architectural TACR trap. */
    env->cp15.hcr_el2 = HCR_TACR;
    g_assert_cmpint(ri->accessfn(env, ri, true), ==, CP_ACCESS_TRAP_EL2);
    cpu_reset(CPU(cpu));
}

static void test_raw_migration(void)
{
    ARMCPU *cpu = board_cpu(0);
    const ARMCPRegInfo *ri = reginfo(cpu, false);
    uint64_t saved;
    bool found = false;

    cpu_reset(CPU(cpu));
    /* CPACR/CPTR raw views depend on SCR/NSACR: capture and restore must
     * use the same CPU security context, as a complete CPU migration does. */
    cpu->env.cp15.scr_el3 |= SCR_NS;
    cpu->env.cp15.nsacr = 0;
    ri->raw_writefn(&cpu->env, ri, 0x1000a440);
    saved = read_raw_cp_reg(&cpu->env, ri);
    g_assert_true(write_cpustate_to_list(cpu, false));
    for (unsigned i = 0; i < cpu->cpreg_array_len; i++) {
        const ARMCPRegInfo *entry = get_arm_cp_reginfo(cpu->cp_regs,
            kvm_to_cpreg_id(cpu->cpreg_indexes[i]));

        if (entry->readfn == ri->readfn) {
            g_assert_cmphex(cpu->cpreg_values[i], ==, saved);
            found = true;
        }
    }
    g_assert_true(found);
    cpu_reset(CPU(cpu));
    /* Raw migration bypasses non-secure guest write permission. */
    cpu->env.cp15.scr_el3 |= SCR_NS;
    cpu->env.cp15.nsacr = 0;
    g_assert_true(write_list_to_cpustate(cpu));
    g_assert_cmphex(read_raw_cp_reg(&cpu->env, ri), ==, saved);
    cpu_reset(CPU(cpu));
}

static void test_generic_unchanged(void)
{
    DeviceState *dev = qdev_new(ARM_CPU_TYPE_NAME("cortex-a7"));
    ARMCPU *cpu = ARM_CPU(dev);
    const ARMCPRegInfo *ri;

    object_property_add_child(object_get_objects_root(), "generic-a7",
                              OBJECT(dev));
    qdev_realize(dev, NULL, &error_abort);
    ri = reginfo(cpu, false);
    g_assert_true(ri->type & ARM_CP_CONST);
    g_assert_cmphex(read_raw_cp_reg(&cpu->env, ri), ==, 0);
    qdev_unrealize(dev);
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *image = g_build_filename(directory, "loop.bin", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev =
        g_strdup_printf("unix:%s,server=on,wait=off", socket);
    const char loop[] = { 0xfe, 0xff, 0xff, 0xea };
    int result;

    g_test_init(&argc, &argv, NULL);
    g_assert_true(g_file_set_contents(image, loop, sizeof(loop), NULL));
    {
        const char *args[] = {
            argv[0], "-machine", "z7ii-mas-diagnostic", "-bios", image,
            "-accel", "qtest", "-display", "none", "-nodefaults", "-qtest",
            chardev, "-smp", "2", "-S", NULL,
        };

        qemu_init(G_N_ELEMENTS(args) - 1, (char **)args);
    }
    g_test_add_func("/nikon/actlr/readback-reset-mask-cores", test_readback);
    g_test_add_func("/nikon/actlr/nonsecure-trap", test_permissions);
    g_test_add_func("/nikon/actlr/raw-migration", test_raw_migration);
    g_test_add_func("/nikon/actlr/generic-a7-unchanged", test_generic_unchanged);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(image);
    unlink(socket);
    rmdir(directory);
    return result;
}
