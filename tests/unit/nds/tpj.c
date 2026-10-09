/* Source protocol fixtures with synthetic external bytes, no Nikon payload.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gdb/callback.h"
#include "system.h"
#include "sim-core.h"
#include "opcode/nds32.h"
#include "tpj.h"

struct fixture {
  host_callback callback;
  SIM_DESC sd;
  struct tpj *device;
};

static struct fixture *
open_fixture (void)
{
  char *argv[] = { "tpjcheck", "--nds32-system", "--endian=little",
                   "--memory-region=0,0x10000", NULL };
  struct fixture *test = calloc (1, sizeof *test);
  uint32_t loop = N32_JI (J, 0);
  unsigned char code[] = { loop >> 24, loop >> 16, loop >> 8, loop };
  assert (test);
  test->callback = default_callback;
  test->callback.init (&test->callback);
  test->sd = sim_open (SIM_OPEN_DEBUG, &test->callback, NULL, argv);
  assert (test->sd);
  assert (sim_write (test->sd, 0x100, code, sizeof code) == sizeof code);
  CPU_PC_SET (STATE_CPU (test->sd, 0), 0x100);
  test->device = tpj_open (test->sd, NULL, NULL);
  assert (test->device && !tpj_boundary (test->device));
  return test;
}

static void
close_fixture (struct fixture *test)
{
  tpj_quiesce (test->device);
  sim_close (test->sd, 0);
  tpj_close (test->device);
  test->callback.shutdown (&test->callback);
  free (test);
}

static void
write_register (struct fixture *test, uint32_t address, uint32_t value,
                unsigned size)
{
  unsigned char bytes[] = { value, value >> 8, value >> 16, value >> 24 };
  assert (sim_core_write_buffer (test->sd, NULL, write_map, bytes, address, size)
          == size);
}

static uint32_t
read_register (struct fixture *test, uint32_t address, unsigned size)
{
  unsigned char bytes[4] = { 0 };
  unsigned i;
  uint32_t result = 0;
  assert (sim_core_read_buffer (test->sd, NULL, read_map, bytes, address, size)
          == size);
  for (i = 0; i < size; ++i)
    result |= (uint32_t) bytes[i] << (8 * i);
  return result;
}

static void
finish_event (struct fixture *test)
{
  assert (nds32_system_run (test->sd, 4) == SIM_RC_OK);
  assert (!tpj_boundary (test->device));
}

static void
flash_command (struct fixture *test, unsigned command, uint32_t address)
{
  write_register (test, 0x41000, address << 8 | command, 4);
  write_register (test, 0x41005, 0x55, 1);
  write_register (test, 0x41006, 0xaa, 1);
  write_register (test, 0x41007, 0x66, 1);
  assert (read_register (test, 0x41011, 1) == 1);
}

static void
input_file (char *path, const void *bytes, size_t size)
{
  int fd = mkstemp (path);
  FILE *file;
  assert (fd >= 0);
  file = fdopen (fd, "wb");
  assert (file);
  assert (fwrite (bytes, 1, size, file) == size);
  assert (!fclose (file));
}

static void
external_producers (void)
{
  struct fixture *test = open_fixture ();
  char flash_path[] = "/tmp/tpjflashXXXXXX";
  char transform_path[] = "/tmp/tpjtransformXXXXXX";
  unsigned char *flash = malloc (0x10000);
  unsigned char sample[24];
  unsigned i;
  assert (flash);
  memset (flash, 0xff, 0x10000);
  flash[0xfff8] = 0x7d;
  for (i = 0; i < 6; ++i)
    {
      sample[i * 2] = i + 1;
      sample[i * 2 + 1] = 0;
      sample[12 + i * 2] = (i + 1) * 10;
      sample[13 + i * 2] = 0;
    }
  input_file (flash_path, flash, 0x10000);
  input_file (transform_path, sample, sizeof sample);
  free (flash);
  assert (tpj_flash (test->device, flash_path));
  assert (tpj_transform (test->device, transform_path));
  assert (!unlink (flash_path) && !unlink (transform_path));
  flash_command (test, 0xab, 0);
  finish_event (test);
  flash_command (test, 0x3b, 0xfff8);
  finish_event (test);
  assert (!read_register (test, 0x41011, 1));
  /* The helper reads the same latched byte for destination and sum. */
  assert (read_register (test, 0x41012, 1) == 0x7d);
  assert (read_register (test, 0x41012, 1) == 0x7d);
  flash_command (test, 6, 0);
  finish_event (test);
  write_register (test, 0x41004, 0, 1); /* Original count-minus-one. */
  flash_command (test, 2, 0xfff8);
  assert (nds32_system_run (test->sd, 4) == SIM_RC_OK);
  assert (read_register (test, 0x41011, 1) == 1); /* No payload, no ready. */
  write_register (test, 0x41020, 0x0f, 1);
  finish_event (test);
  flash_command (test, 0x3b, 0xfff8);
  finish_event (test);
  assert (read_register (test, 0x41012, 1) == (0x7d & 0x0f));
  write_register (test, 0x53000, 0, 4);
  for (i = 0; i < 6; ++i)
    write_register (test, 0x53004 + i * 2, i + 1, 2);
  write_register (test, 0x53002, 1, 1);
  assert (!read_register (test, 0x53001, 1));
  finish_event (test);
  assert (read_register (test, 0x53001, 1) == 1);
  for (i = 0; i < 6; ++i)
    assert (read_register (test, 0x53010 + i * 2, 2) == (i + 1) * 10);
  close_fixture (test);
}

static void
original_event_flags (void)
{
  const char snapshot[] = "6001c 1 1\n60028 1 1\n6002c 1 0\n";
  char path[] = "/tmp/tpjflagsXXXXXX";
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  input_file (path, snapshot, sizeof snapshot - 1);
  assert (tpj_snapshot (test->device, path));
  assert (!unlink (path));
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == (1U << 2));
  assert (read_register (test, 0x60028, 1) == 1);
  /* Original handler clears the hardware flag; host never clears guest RAM. */
  write_register (test, 0x6001c, 0, 1);
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND));
  write_register (test, 0x6005e, 0x1234, 2); /* Fifteenth indexed reply. */
  assert (!tpj_boundary (test->device));
  close_fixture (test);
}

static void
missing_producer (void)
{
  struct fixture *test = open_fixture ();
  write_register (test, 0x53000, 0, 4);
  write_register (test, 0x53002, 1, 1);
  assert (!read_register (test, 0x53001, 1));
  assert (tpj_boundary (test->device));
  close_fixture (test);
  test = open_fixture ();
  flash_command (test, 0xab, 0);
  assert (nds32_system_run (test->sd, 4) == SIM_RC_OK);
  assert (read_register (test, 0x41011, 1) == 1);
  assert (tpj_boundary (test->device));
  close_fixture (test);
  test = open_fixture ();
  {
    unsigned char byte;
    assert (!sim_core_read_buffer (test->sd, NULL, read_map, &byte, 0x4100f, 1));
    assert (tpj_boundary (test->device));
  }
  close_fixture (test);
}

int
main (void)
{
  external_producers ();
  original_event_flags ();
  missing_producer ();
  return 0;
}
