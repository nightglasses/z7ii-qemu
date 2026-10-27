# Darwin sigwait can consume a signal pending on another vCPU thread.
# Generate the finite qtest wake adaptation in the native source graph only.
# Keep the pinned official checkout, TCG, and the default POSIX kick unchanged.
function(qtest_replace variable before after)
  string(LENGTH "${${variable}}" size)
  string(LENGTH "${before}" removed)
  string(REPLACE "${before}" "" without "${${variable}}")
  string(LENGTH "${without}" remaining)
  math(EXPR count "${size} - ${remaining}")
  if(NOT count EQUAL removed)
    message(FATAL_ERROR "Fixed upstream qtest source does not match its unique wake seam")
  endif()
  string(REPLACE "${before}" "${after}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

file(READ "${QEMU_UPSTREAM}/include/hw/core/cpu.h" content)
qtest_replace(content [=[#ifdef _WIN32
    QemuSemaphore sem;
#endif]=] [=[#if defined(_WIN32) || defined(__APPLE__)
    QemuSemaphore sem;
#endif]=])
native_file("include/hw/core/cpu.h" "${content}")

file(READ "${QEMU_UPSTREAM}/accel/dummy-cpus.h" content)
qtest_replace(content [=[void dummy_start_vcpu_thread(CPUState *cpu);]=]
  [=[void dummy_start_vcpu_thread(CPUState *cpu);
#ifdef __APPLE__
void dummy_kick_vcpu_thread(CPUState *cpu);
#endif]=])
native_file("accel/dummy-cpus.h" "${content}")

file(READ "${QEMU_UPSTREAM}/accel/dummy-cpus.c" content)
qtest_replace(content [=[#include "qemu/rcu.h"]=]
  [=[#include "qemu/rcu.h"
#include "qemu/atomic.h"]=])
qtest_replace(content [=[#ifndef _WIN32
    sigset_t waitset;]=] [=[#if !defined(_WIN32) && !defined(__APPLE__)
    sigset_t waitset;]=])
qtest_replace(content [=[#ifndef _WIN32
        do {]=] [=[#if !defined(_WIN32) && !defined(__APPLE__)
        do {]=])
qtest_replace(content [=[void dummy_start_vcpu_thread(CPUState *cpu)]=]
  [=[#ifdef __APPLE__
void dummy_kick_vcpu_thread(CPUState *cpu)
{
    /* Each vCPU owns its token; another sigwait cannot consume this wake. */
    if (!qatomic_xchg(&cpu->thread_kicked, true)) {
        qemu_sem_post(&cpu->sem);
    }
}
#endif

void dummy_start_vcpu_thread(CPUState *cpu)]=])
native_file("accel/dummy-cpus.c" "${content}")

file(READ "${QEMU_UPSTREAM}/accel/qtest/qtest.c" content)
qtest_replace(content [=[static void qtest_accel_class_init(ObjectClass *oc, const void *data)]=]
  [=[#ifdef __APPLE__
static bool qtest_cpu_common_realize(CPUState *cpu, Error **errp)
{
    /* Realization precedes qemu_init_vcpu and its first possible kick. */
    qemu_sem_init(&cpu->sem, 0);
    return true;
}

static void qtest_cpu_common_unrealize(CPUState *cpu)
{
    /* Some targets already joined through cpu_remove_sync before the common
     * hook. Its sole unplug producer is that synchronous removal path.
     * Join other created DUMMY threads before releasing their wake storage.
     */
    if (cpu->created && !cpu->unplug) {
        cpu_remove_sync(cpu);
    }
    qemu_sem_destroy(&cpu->sem);
}
#endif

static void qtest_accel_class_init(ObjectClass *oc, const void *data)]=])
qtest_replace(content [=[    ac->allowed = &qtest_allowed;]=]
  [=[    ac->allowed = &qtest_allowed;
#ifdef __APPLE__
    ac->cpu_common_realize = qtest_cpu_common_realize;
    ac->cpu_common_unrealize = qtest_cpu_common_unrealize;
#endif]=])
qtest_replace(content [=[    ops->create_vcpu_thread = dummy_start_vcpu_thread;]=]
  [=[    ops->create_vcpu_thread = dummy_start_vcpu_thread;
#ifdef __APPLE__
    ops->kick_vcpu_thread = dummy_kick_vcpu_thread;
#endif]=])
native_file("accel/qtest/qtest.c" "${content}")
