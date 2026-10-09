# RXv2 EXTB support required by the original Nikon VR component.
# The four generated translation units compile in QEMU's native target graph.
# Keep all CPU changes here; never mutate the fixed official submodule.
function(rx_replace variable before after)
  string(LENGTH "${${variable}}" size)
  string(LENGTH "${before}" removed)
  string(REPLACE "${before}" "" without "${${variable}}")
  string(LENGTH "${without}" remaining)
  math(EXPR count "${size} - ${remaining}")
  if(NOT count EQUAL removed)
    message(FATAL_ERROR "Fixed upstream RX source does not match the unique EXTB seam")
  endif()
  string(REPLACE "${before}" "${after}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

file(READ "${QEMU_UPSTREAM}/target/rx/cpu.c" content)
rx_replace(content [=[#include "hw/core/loader.h"]=]
  [=[#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"]=])
rx_replace(content [=[    rx_cpu_unpack_psw(env, 0, 1);]=]
  [=[    rx_cpu_unpack_psw(env, 0, 1);
    env->extb = 0xffffff80;]=])
rx_replace(content [=[static void rx_cpu_class_init(ObjectClass *klass, const void *data)]=]
  [=[static const Property rx_cpu_properties[] = {
    DEFINE_PROP_BOOL("has-extb", RXCPU, env.has_extb, false),
};

static void rx_cpu_class_init(ObjectClass *klass, const void *data)]=])
rx_replace(content [=[    device_class_set_parent_realize(dc, rx_cpu_realize,]=]
  [=[    device_class_set_props(dc, rx_cpu_properties);

    device_class_set_parent_realize(dc, rx_cpu_realize,]=])
native_file("target/rx/cpu.c" "${content}")

file(READ "${QEMU_UPSTREAM}/target/rx/cpu.h" content)
rx_replace(content [=[    uint32_t intb;              /* interrupt vector */]=]
  [=[    uint32_t intb;              /* interrupt vector */
    uint32_t extb;              /* relocatable exception vector base */]=])
rx_replace(content [=[    /* Internal use */]=]
  [=[    /* Internal use */
    bool has_extb;              /* explicit RXv2 control-register capability */]=])
native_file("target/rx/cpu.h" "${content}")

file(READ "${QEMU_UPSTREAM}/target/rx/helper.c" content)
rx_replace(content [=[cpu_ldl_le_data(env, 0xffffff80 + vec * 4)]=]
  [=[cpu_ldl_le_data(env, env->extb + vec * 4)]=])
native_file("target/rx/helper.c" "${content}")

file(READ "${QEMU_UPSTREAM}/target/rx/translate.c" content)
rx_replace(content [=[        "bpsw", "bpc", "isp", "fintv", "intb", "", "", ""]=]
  [=[        "bpsw", "bpc", "isp", "fintv", "intb", "extb", "", ""]=])
rx_replace(content [=[static TCGv_i32 cpu_fintv, cpu_intb, cpu_pc;]=]
  [=[static TCGv_i32 cpu_fintv, cpu_intb, cpu_extb, cpu_pc;]=])
rx_replace(content [=[        tcg_gen_mov_i32(ret, cpu_intb);
        break;
    default:]=]
  [=[        tcg_gen_mov_i32(ret, cpu_intb);
        break;
    case 13:    /* RXv2 EXTB, available only in an explicit CPU profile. */
        if (ctx->env->has_extb) {
            tcg_gen_mov_i32(ret, cpu_extb);
            break;
        }
        /* Preserve the original RXv1 unsupported-register behavior. */
        /* fall through */
    default:]=])
rx_replace(content [=[        tcg_gen_mov_i32(cpu_intb, val);
        break;
    default:]=]
  [=[        tcg_gen_mov_i32(cpu_intb, val);
        break;
    case 13:    /* RXv2 EXTB */
        if (ctx->env->has_extb) {
            tcg_gen_mov_i32(cpu_extb, val);
            break;
        }
        /* fall through */
    default:]=])
rx_replace(content [=[    ALLOC_REGISTER(intb, "INTB");]=]
  [=[    ALLOC_REGISTER(intb, "INTB");
    ALLOC_REGISTER(extb, "EXTB");]=])
native_file("target/rx/translate.c" "${content}")
