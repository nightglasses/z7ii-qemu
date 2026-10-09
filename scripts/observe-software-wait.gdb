# RTOS sources 54f792f16da74bbbbd435a34f2efcc0869550b11 and
# 43dc8ba53ab0d63596fef5379512d6c118a9600f; preserve raw Thumb LR.
# Source with native GDB -nx; verify stopped QEMU CPU thread IDs separately.
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
set non-stop off

define ng-observe-software-wait
  set $ng_cpu0_gthread = (unsigned int)$arg0
  set $ng_cpu1_gthread = (unsigned int)$arg1
  set scheduler-locking off
  set $ng_task = (unsigned int)0
  set $ng_cb = (unsigned int)0
  set $ng_id = (unsigned int)0x01000088
  set $ng_set_count = 0
  set $ng_pattern_count = 0

  thbreak *0x10104a14 if $r0 == $ng_id
  commands
    silent
    printf "ng_event_business_wait API entry; timeout is fifth stack argument\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    if ($lr & ~1u) == 0x103610f4
      printf "ng_wait_wrapper source-proven 16-byte frame; last word is caller LR\n"
      x/4wx $sp
    else
      x/wx $sp
    end
    continue &
  end

  hbreak *0x10104848 if $r0 == $ng_id && $ng_set_count < 8
  commands
    silent
    set $ng_set_count = $ng_set_count + 1
    printf "ng_event_set business API entry; attempt only\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    continue &
  end

  hbreak *0x101111c4 if $ng_cb != 0 && $r4 == $ng_cb && $ng_pattern_count < 8
  commands
    silent
    set $ng_pattern_count = $ng_pattern_count + 1
    printf "ng_event_pattern pre-STR; R0 is proposed new pattern\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_cb + 8)
    p/x *(unsigned int *)($ng_cb + 0x14)
    continue &
  end

  thbreak *0x1011121c if $ng_cb != 0 && $r4 == $ng_cb && $r0 == $ng_task
  commands
    silent
    printf "ng_event_selected_wake pre-BL; selection only\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_cb + 0x14)
    continue &
  end

  thbreak *0x10120858 if $ng_cb != 0 && $r4 == $ng_cb && $r0 == $ng_task && $lr == 0x10111220
  commands
    silent
    printf "ng_event_wait_release entry; guest return write has not executed\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    x/2wx 0xb2b415f4
    p/x *(unsigned int *)($ng_task + 0x40)
    continue &
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
    if $lr == 0x10120ad0 && *(unsigned int *)($ng_task + 0x30) == 0xb18aec48 && *(unsigned int *)($ng_task + 0x34) == $ng_id
      set $ng_cb = (unsigned int)$r4
      printf "ng_matched_event_wait; no mutex owner field\n"
      p/x *(unsigned int *)($ng_cb + 0)
      p/x *(unsigned int *)($ng_cb + 4)
      p/x *(unsigned int *)($ng_cb + 8)
      p/x *(unsigned int *)($ng_cb + 0x10)
      p/x *(unsigned int *)($ng_cb + 0x14)
      p/x *(unsigned int *)($ng_task + 0x44)
      p/x *(unsigned int *)($ng_task + 0x48)
      p/x *(unsigned int *)($ng_task + 0x4c)
    end
    continue &
  end
  disable $ng_make_wait_bp

  thbreak *0x103b2306 if $lr == 0x104000c1 && $r4 == 4 && $r6 == 0xb185e020 && ($_gthread == $ng_cpu0_gthread || $_gthread == $ng_cpu1_gthread)
  commands
    silent
    printf "ng_marker actual business entry\n"
    info threads -gid
    info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr
    set $ng_pid = (unsigned int)($_gthread == $ng_cpu1_gthread)
    x/2wx 0xb2b415f4
    set $ng_task = *(unsigned int *)(0xb2b415f4 + 4 * $ng_pid)
    p/x $ng_task
    enable $ng_make_wait_bp
    continue &
  end
  info breakpoints
end

document ng-observe-software-wait
Install seven hardware locations for the actual event-flag ID 01000088.
Pass verified global thread IDs for QEMU CPU#0/1; no initial continue is issued.
Use native background continue and interrupt; all-stop mode stops both CPUs.
Read/command errors abort the command list before resume. Setter/pattern logs cap at 8.
end
