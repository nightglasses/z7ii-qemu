# RTOS source 17f765d38903ec156b89ae9cf74abc9bc56c5a29.
# Source this file with native GDB -nx. Connect to stopped QEMU separately.
# The command definition does not install breakpoints or start execution.
set pagination off
set language c
set auto-load python-scripts off
set auto-load local-gdbinit off
set breakpoint condition-evaluation host
set may-call-functions off
set may-write-memory off
set may-write-registers off
set may-insert-breakpoints on
set may-insert-tracepoints off
set may-insert-fast-tracepoints off

define ng-observe-software-wait
  # Arguments are operator-verified GDB global thread IDs for CPU#0 and CPU#1.
  # info threads -gid and QEMU qThreadExtraInfo establish the binding at setup.
  set $ng_cpu0_gthread = (unsigned int)$arg0
  set $ng_cpu1_gthread = (unsigned int)$arg1
  set scheduler-locking off
  set $ng_task = (unsigned int)0
  set $ng_cb = (unsigned int)0
  set $ng_id = (unsigned int)0
  set $ng_pid = (unsigned int)0

  thbreak *0x10360ed8 if $r4 == 0xb18094e0
  set $ng_creator_bp = $bpnum
  commands
    silent
    printf "ng_creator post-store\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/wx 0xb18094e0
    continue
  end

  thbreak *0x10120908 if $ng_task != 0 && ($_gthread == $ng_cpu0_gthread || $_gthread == $ng_cpu1_gthread) && *(unsigned int *)(0xb2b415f4 + 4 * ($_gthread == $ng_cpu1_gthread)) == $ng_task
  set $ng_make_wait_bp = $bpnum
  commands
    silent
    printf "ng_make_wait generic entry\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned char *)($ng_task + 0x2b)
    p/x *(unsigned int *)($ng_task + 0x30)
    p/x *(unsigned int *)($ng_task + 0x34)
    p/x *(unsigned int *)($ng_task + 0x40)
    continue
  end
  disable $ng_make_wait_bp

  thbreak *0x1010fdd4 if $ng_task != 0 && $r1 == $ng_task && $r4 != 0 && ($r4 & 3) == 0
  set $ng_mutex_block_bp = $bpnum
  commands
    silent
    printf "ng_mutex_block pre-STR; TCB fields are old\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    set $ng_cb = (unsigned int)$r4
    p/x *(unsigned int *)($ng_cb + 0)
    p/x *(unsigned int *)($ng_cb + 4)
    set $ng_id = *(unsigned int *)($ng_cb + 8)
    p/x $ng_id
    p/x *(unsigned int *)($ng_cb + 0x10)
    p/x *(unsigned char *)($ng_cb + 0x14)
    p/x *(unsigned int *)($ng_cb + 0x18)
    p/x *(unsigned int *)($ng_cb + 0x1c)
    p/x *(unsigned char *)($ng_task + 0x2b)
    p/x *(unsigned int *)($ng_task + 0x30)
    p/x *(unsigned int *)($ng_task + 0x34)
    p/x *(unsigned int *)($ng_task + 0x40)
    p/x *(unsigned int *)($ng_task + 0x78)
    enable $ng_release_bp $ng_selected_wake_bp $ng_wait_release_bp
    continue
  end
  disable $ng_mutex_block_bp

  thbreak *0x1010ffd8 if $ng_cb != 0 && $r0 == $ng_id
  set $ng_release_bp = $bpnum
  commands
    silent
    printf "ng_release same registered ID; attempt only\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_cb + 0x18)
    continue
  end
  disable $ng_release_bp

  thbreak *0x10110138 if $ng_cb != 0 && $r4 == $ng_cb && $r0 == $ng_task
  set $ng_selected_wake_bp = $bpnum
  commands
    silent
    printf "ng_selected_wake pre-BL\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_cb + 0x18)
    continue
  end
  disable $ng_selected_wake_bp

  thbreak *0x10120858 if $ng_cb != 0 && $r0 == $ng_task && $r4 == $ng_cb && $lr == 0x1011013c
  set $ng_wait_release_bp = $bpnum
  commands
    silent
    printf "ng_wait_release mutex caller entry; return-slot write has not executed\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_task + 0x40)
    continue
  end
  disable $ng_wait_release_bp

  thbreak *0x103b2306 if $lr == 0x104000c1 && $r4 == 4 && $r6 == 0xb185e020 && ($_gthread == $ng_cpu0_gthread || $_gthread == $ng_cpu1_gthread)
  set $ng_marker_bp = $bpnum
  commands
    silent
    printf "ng_marker actual business entry\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    set $ng_pid = (unsigned int)($_gthread == $ng_cpu1_gthread)
    x/2wx 0xb2b415f4
    set $ng_task = *(unsigned int *)(0xb2b415f4 + 4 * $ng_pid)
    p/x $ng_task
    enable $ng_make_wait_bp $ng_mutex_block_bp
    continue
  end
  info breakpoints
end

document ng-observe-software-wait
Install seven source-scoped temporary hardware breakpoints in stopped QEMU.
Requires a fresh remote target, original byte guard and operator-reviewed policy.
Pass the verified GDB global thread IDs for QEMU CPU#0 and CPU#1, in that order.
Does not initially continue the guest. Any read/command error stops its list.
end
