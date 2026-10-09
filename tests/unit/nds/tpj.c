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
  unsigned emissions;
  uint64_t token;
  unsigned phase;
  uint32_t reply;
  int accept;
  unsigned notify_level;
  unsigned notifications;
  unsigned notify_history[64];
};

static int
capture_reply (void *opaque, unsigned type, unsigned kind, uint64_t token,
               unsigned bits, uint32_t phase, uint32_t word, uint64_t time)
{
  struct fixture *test = opaque;
  (void) time;
  if (type == NIKON_WIRE_SIGNAL)
    {
      assert (kind == NIKON_PEER_GPIO && !token && !bits &&
              phase == NIKON_TOUCH_NOTIFY && word <= 1);
      if (test->accept)
        {
          assert (test->notifications < 64);
          test->notify_history[test->notifications++] = word;
          test->notify_level = word;
        }
      return test->accept;
    }
  assert (type == NIKON_WIRE_REPLY && kind == NIKON_PEER_I2C && bits == 8);
  ++test->emissions;
  test->token = token;
  test->phase = phase;
  test->reply = word;
  return test->accept;
}

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
  test->accept = 1;
  test->device = tpj_open (test->sd, capture_reply, test);
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
  unsigned i, transferred;
  uint32_t result = 0;
  transferred = sim_core_read_buffer (test->sd, NULL, read_map, bytes, address, size);
  if (transferred != size)
    {
      fprintf (stderr, "TPJ fixture read address=%08x width=%u transferred=%u\n",
               address, size, transferred);
      tpj_boundary (test->device);
    }
  assert (transferred == size);
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
  char saved_path[] = "/tmp/tpjsavedXXXXXX";
  unsigned char *flash = malloc (0x10000);
  unsigned char sample[24];
  unsigned i;
  uint32_t initial_sum, saved_sum;
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
  initial_sum = read_register (test, 0x41018, 4);
  assert (initial_sum == 0xff0000U - (0xff - 0x7d));
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
  assert (read_register (test, 0x41018, 4) == initial_sum - (0x7d - 0x0d));
  write_register (test, 0x53000, 0, 4);
  for (i = 0; i < 6; ++i)
    write_register (test, 0x53004 + i * 2, i + 1, 2);
  write_register (test, 0x53002, 1, 1);
  assert (!read_register (test, 0x53001, 1));
  finish_event (test);
  assert (read_register (test, 0x53001, 1) == 1);
  for (i = 0; i < 6; ++i)
    assert (read_register (test, 0x53010 + i * 2, 2) == (i + 1) * 10);
  input_file (saved_path, sample, sizeof sample);
  saved_sum = read_register (test, 0x41018, 4);
  assert (tpj_save (test->device, saved_path));
  close_fixture (test);
  test = open_fixture ();
  assert (tpj_flash (test->device, saved_path));
  assert (!unlink (saved_path));
  assert (read_register (test, 0x41018, 4) == saved_sum);
  flash_command (test, 0xab, 0);
  finish_event (test);
  flash_command (test, 0x3b, 0xfff8);
  finish_event (test);
  assert (read_register (test, 0x41012, 1) == 0x0d);
  flash_command (test, 6, 0);
  finish_event (test);
  flash_command (test, 0x20, 0xfff8);
  finish_event (test);
  assert (read_register (test, 0x41018, 4) == 0xff0000U);
  close_fixture (test);
}

static void
original_event_flags (void)
{
  const char snapshot[] = "6001c 1 1\n60028 1 1\n6002c 1 0\n60003 1 5\n";
  char path[] = "/tmp/tpjflagsXXXXXX";
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  input_file (path, snapshot, sizeof snapshot - 1);
  assert (tpj_snapshot (test->device, path));
  assert (!unlink (path));
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == (1U << 2));
  assert (read_register (test, 0x60028, 1) == 1);
  assert (read_register (test, 0x60003, 1) == 0x15);
  /* Original handler clears the hardware flag; host never clears guest RAM. */
  write_register (test, 0x6001c, 0, 1);
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND));
  assert (read_register (test, 0x60003, 1) == 5);
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

static void
i2c_phase (struct fixture *test, uint64_t token, unsigned phase, uint32_t word,
           uint32_t reply)
{
  unsigned before = test->emissions;
  assert (tpj_i2c (test->device, token, phase, word, 0));
  tpj_flush (test->device);
  assert (test->emissions == before + 1 && test->token == token &&
          test->phase == phase && test->reply == reply);
}

static void
guest_i2c_reply (void)
{
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  uint32_t code = N32_TYPE2 (SWI, 0, 1, 0);
  unsigned char instruction[] = { code >> 24, code >> 16, code >> 8, code };
  unsigned before;
  i2c_phase (test, 1, NIKON_I2C_START, 0x54, NIKON_I2C_NAK);
  assert (!test->notifications); /* Reset storage is not a guest wire producer. */
  i2c_phase (test, 2, NIKON_I2C_START, 0x55, 0);
  i2c_phase (test, 3, NIKON_I2C_WRITE, 0x10, 0);
  assert (read_register (test, 0x60028, 1) == 0x10);
  assert (!read_register (test, 0x60003, 1)); /* START/WRITE alone is no wake event. */
  i2c_phase (test, 4, NIKON_I2C_START, 0xd5, 0);
  assert (read_register (test, 0x6002c, 1) == 1);
  assert (read_register (test, 0x6001c, 1) == 1);
  assert (read_register (test, 0x60003, 1) == 0x10);
  before = test->emissions;
  assert (tpj_i2c (test->device, 5, NIKON_I2C_READ, 0, 0));
  tpj_flush (test->device);
  assert (test->emissions == before); /* A command match produces no data. */
  /* Actual decoder store supplies bytes. This fixture is not the Nikon ISR. */
  assert (sim_write (test->sd, 0x100, instruction, sizeof instruction)
          == sizeof instruction);
  CPU_PC_SET (cpu, 0x100);
  CCPU_GPR[0].u = 0x12345678;
  CCPU_GPR[1].u = 0x60040;
  assert (nds32_system_run (test->sd, 1) == SIM_RC_OK);
  test->accept = 0;
  tpj_flush (test->device);
  assert (test->reply == 0x78 && test->token == 5);
  assert (!tpj_i2c (test->device, 6, NIKON_I2C_READ, 0, 0));
  test->accept = 1;
  tpj_flush (test->device);
  assert (test->reply == 0x78 && test->token == 5);
  i2c_phase (test, 6, NIKON_I2C_READ, 0, 0x56);
  i2c_phase (test, 7, NIKON_I2C_NACK, 0, 0);
  i2c_phase (test, 8, NIKON_I2C_STOP, 0, 0);
  i2c_phase (test, 9, NIKON_I2C_START, 0x55, 0);
  i2c_phase (test, 10, NIKON_I2C_WRITE, 1, 0);
  i2c_phase (test, 11, NIKON_I2C_WRITE, 0xa5, 0);
  i2c_phase (test, 12, NIKON_I2C_STOP, 0, 0);
  assert (read_register (test, 0x6002c, 1) == 0);
  assert (read_register (test, 0x60024, 1) == 1); /* One genuine data byte. */
  assert (read_register (test, 0x60080, 1) == 0xa5);
  assert (read_register (test, 0x60081, 1) == 0);
  assert (read_register (test, 0x60020, 1) == 1);
  i2c_phase (test, 13, NIKON_I2C_START, 0x55, 0);
  i2c_phase (test, 14, NIKON_I2C_WRITE, 0x11, 0);
  i2c_phase (test, 15, NIKON_I2C_START, 0xd5, 0);
  before = test->emissions;
  assert (tpj_i2c (test->device, 16, NIKON_I2C_READ, 0, 0));
  tpj_flush (test->device);
  assert (test->emissions == before); /* Old command bytes are not a reply. */
  tpj_cancel (test->device, 16);
  assert (tpj_i2c (test->device, 17, NIKON_I2C_READ, 0, 0));
  tpj_flush (test->device);
  assert (test->emissions == before);
  tpj_cancel (test->device, 17);
  i2c_phase (test, 18, NIKON_I2C_NACK, 0, 0);
  i2c_phase (test, 19, NIKON_I2C_STOP, 0, 0);
  write_register (test, 0x71010, 0, 1);
  tpj_flush (test->device);
  assert (test->notifications == 1 && test->notify_level == 0);
  write_register (test, 0x71010, 4, 4);
  tpj_flush (test->device);
  assert (test->notifications == 2 && test->notify_level == 1);
  assert (tpj_signal (test->device, NIKON_PEER_GPIO, NIKON_TOUCH_CONTROL, 1, 0));
  assert (CPU_PC_GET (cpu) == 0x104); /* External control never jumps the CPU. */
  close_fixture (test);
}

static void
explicit_update_snapshot (void)
{
  struct fixture *test = open_fixture ();
  unsigned char *bytes = malloc (0xe000);
  char path[] = "/tmp/tpjupdateXXXXXX";
  assert (bytes);
  memset (bytes, 0xff, 0xe000);
  bytes[1] = 0x12;
  input_file (path, bytes, 0xe000);
  free (bytes);
  assert (!tpj_flash (test->device, path)); /* No implicit partial fallback. */
  assert (tpj_update (test->device, path));
  assert (!unlink (path));
  assert (read_register (test, 0x41018, 4) == 0xff0000U - (0xff - 0x12));
  flash_command (test, 0xab, 0);
  finish_event (test);
  flash_command (test, 0x3b, 0xfff8);
  finish_event (test);
  assert (read_register (test, 0x41012, 1) == 0xff);
  close_fixture (test);
}

static void
source_one_shot (void)
{
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  write_register (test, 0x5000c, 2, 4);
  write_register (test, 0x50018, 1, 1);
  assert (!read_register (test, 0x50014, 1));
  finish_event (test);
  assert (read_register (test, 0x50014, 1) == 1);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == (1U << 3));
  write_register (test, 0x50014, 0, 1);
  write_register (test, 0x50018, 0, 1);
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND));
  write_register (test, 0x5000c, 8, 4);
  write_register (test, 0x50018, 1, 1);
  assert (nds32_system_run (test->sd, 2) == SIM_RC_OK);
  assert (!read_register (test, 0x50014, 1));
  write_register (test, 0x50018, 0, 1);
  assert (nds32_system_run (test->sd, 12) == SIM_RC_OK);
  assert (!read_register (test, 0x50014, 1));
  write_register (test, 0x50018, 1, 1);
  assert (nds32_system_run (test->sd, 2) == SIM_RC_OK);
  write_register (test, 0x50018, 0, 1);
  write_register (test, 0x5000c, 2, 4);
  write_register (test, 0x50018, 1, 1);
  finish_event (test);
  assert (read_register (test, 0x50014, 1) == 1);
  close_fixture (test);
}

static void
source_configuration (void)
{
  struct fixture *test = open_fixture ();
  char snapshot[] = "/tmp/tpjcontrollerXXXXXX";
  const char supplied_completion[] = "80042 1 81\n";
  unsigned i;
  input_file (snapshot, supplied_completion, sizeof supplied_completion - 1);
  assert (!tpj_snapshot (test->device, snapshot));
  assert (!unlink (snapshot));
  assert (!read_register (test, 0x80042, 1));
  write_register (test, 0x80043, 2, 1);
  assert (!read_register (test, 0x80042, 1));
  finish_event (test);
  assert (read_register (test, 0x80042, 1) == 0x80);
  write_register (test, 0x80042, 0, 1); /* Original masked ack. */
  write_register (test, 0x80043, 3, 1);
  finish_event (test);
  assert (read_register (test, 0x80042, 1) == 1);
  write_register (test, 0x80042, 0, 1);
  write_register (test, 0x80043, 0, 1);
  write_register (test, 0x80043, 2, 1);
  write_register (test, 0x80043, 0, 1); /* Cancel before expiry. */
  finish_event (test);
  assert (!read_register (test, 0x80042, 1));
  for (i = 0; i < 11; ++i)
    {
      write_register (test, 0x80068, i, 1);
      write_register (test, 0x8006c, 0x80 | i, 1);
    }
  for (i = 0; i < 11; ++i)
    {
      write_register (test, 0x80068, i, 1);
      assert (read_register (test, 0x8006c, 1) == (0x80 | i));
    }
  write_register (test, 0x80216, 5, 1);
  assert (read_register (test, 0x80216, 1) == 5);
  finish_event (test);
  assert (read_register (test, 0x80216, 1) == 4);
  for (i = 0; i < 9; ++i)
    write_register (test, 0x80118 + i, i, 1); /* Whole Ob0c4/a8b0/a8e0 loop. */
  /* Whole 384-halfword configuration is guest-written retained storage. */
  for (i = 0; i < 0x180; ++i)
    {
      write_register (test, 0x80204, i, 2);
      write_register (test, 0x80208, 0x5500 | i, 2);
    }
  write_register (test, 0x80214, 1, 1); /* Original streaming mode. */
  write_register (test, 0x80204, 0x3d, 2);
  for (i = 0; i < 16; ++i)
    assert (read_register (test, 0x80208, 2) == (0x5500 | (0x3d + i)));
  write_register (test, 0x80204, 0x3d, 2);
  for (i = 0; i < 16; ++i)
    write_register (test, 0x80208, 0x3300 | i, 2);
  write_register (test, 0x80214, 0, 1);
  for (i = 0; i < 16; ++i)
    {
      write_register (test, 0x80204, 0x3d + i, 2);
      assert (read_register (test, 0x80208, 2) == (0x3300 | i));
    }
  assert (!read_register (test, 0x80050, 1)); /* No invented scan completion. */
  /* O9be/O9ae inspect byte flags; O9d8 clears them with a word store.
     A word READ would widen the real guest contract. */
  assert (!read_register (test, 0x72024, 1));
  assert (!read_register (test, 0x72025, 1));
  close_fixture (test);
}

static void
ordered_notifications (void)
{
  struct fixture *test = open_fixture ();
  write_register (test, 0x71010, 4, 1);
  write_register (test, 0x71010, 0, 1);
  write_register (test, 0x71010, 4, 1);
  write_register (test, 0x71010, 4, 1); /* Same level produces no extra edge. */
  test->accept = 0;
  tpj_flush (test->device);
  assert (!test->notifications);
  test->accept = 1;
  tpj_flush (test->device);
  assert (test->notifications == 3);
  assert (test->notify_history[0] == 1 && test->notify_history[1] == 0 &&
          test->notify_history[2] == 1);
  tpj_flush (test->device);
  assert (test->notifications == 3);
  close_fixture (test);
}

static void
first_source_boundary (void)
{
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  enum sim_stop reason;
  int signal;
  uint32_t code = N32_TYPE2 (LBI, 0, 1, 0);
  unsigned char instruction[] = { code >> 24, code >> 16, code >> 8, code };
  assert (sim_write (test->sd, 0x100, instruction, sizeof instruction)
          == sizeof instruction);
  CCPU_GPR[1].u = 0x80045; /* Source region, intentionally unproved field. */
  assert (nds32_system_run (test->sd, 4096) == SIM_RC_OK);
  sim_stop_reason (test->sd, &reason, &signal);
  assert (tpj_boundary (test->device));
  assert (reason == sim_stopped && signal == GDB_SIGNAL_SEGV);
  /* A single genuine bus exception is retained; no successful budget stop. */
  assert (nds32_system_read_sr (cpu, SRIDX_IPC) == 0x100);
  close_fixture (test);
}

static void
sensing_configuration (void)
{
  struct fixture *test = open_fixture ();
  static const unsigned counts[] = { 57, 49, 50, 51, 52, 53, 54, 55, 56 };
  unsigned page, entry;
  for (entry = 0; entry < 4; ++entry)
    write_register (test, 0x801a0 + entry, entry * 0x11, 1);
  /* Oa4b6 uses actual copied channel counts with inclusive inner bounds;
     its outer loop selects nine distinct 64-entry pages. No sensor data. */
  for (page = 0; page < 9; ++page)
    for (entry = 0; entry <= counts[page]; ++entry)
      {
        write_register (test, 0x801ea, page * 64 + entry, 2);
        write_register (test, 0x801e0, page * 0x100 + entry, 2);
      }
  assert (!tpj_boundary (test->device));
  assert (!read_register (test, 0x80050, 1));
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x801ea, 0x240, 2);
  write_register (test, 0x801e0, 0x1234, 2);
  assert (tpj_boundary (test->device)); /* Native finite extent, no wrapping. */
  close_fixture (test);
}

static void
normal_samples (void)
{
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  unsigned char records[2 * 884] = { 0 };
  char path[] = "/tmp/tpjsamplesXXXXXX";
  unsigned record, word, byte;
  write_register (test, 0x80003, 0x40, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* No input, no production. */
  for (record = 0; record < 2; ++record)
    {
      records[record * 884] = record ? 8 : 4;
      for (word = 0; word < 220; ++word)
        {
          uint32_t measurement = 0x12000000 + record * 0x10000 + word;
          for (byte = 0; byte < 4; ++byte)
            records[record * 884 + 4 + word * 4 + byte] = measurement >> (8 * byte);
        }
    }
  input_file (path, records, sizeof records);
  write_register (test, 0x72040, 0x28000, 4);
  write_register (test, 0x72044, 0x29000, 4);
  write_register (test, 0x72080, 0x289cc, 4);
  write_register (test, 0x72084, 0x299cc, 4);
  write_register (test, 0x80218, 1, 1); /* Distinct calibration request. */
  assert (tpj_samples (test->device, path));
  assert (!unlink (path));
  assert (nds32_system_run (test->sd, 8) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x80218, 0, 1);
  write_register (test, 0x72039, 1, 1); /* Distinct freeze-result consumer. */
  assert (nds32_system_run (test->sd, 8) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x7203a, 1, 1); /* Resume the normal source mode. */
  finish_event (test);
  assert (read_register (test, 0x80050, 1) == 8);
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND)); /* Source mask inactive. */
  for (word = 0; word < 209; ++word)
    assert (read_register (test, 0x28000 + word * 4, 4) == 0x12000000 + word);
  for (word = 0; word < 11; ++word)
    assert (read_register (test, 0x289cc + word * 4, 4) == 0x12000000 + 209 + word);
  write_register (test, 0x8004c, 8, 1);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == (1U << 12));
  write_register (test, 0x802b6, 6, 1); /* Both buffers held. */
  write_register (test, 0x80050, 0, 1); /* Actual original masked ack. */
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND));
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x802b6, 2, 1); /* Hold slot0, release slot1. */
  write_register (test, 0x80003, 0, 1); /* Cancel before its actual deadline. */
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x80003, 0x40, 1);
  assert (nds32_system_run (test->sd, 12) == SIM_RC_OK);
  assert (read_register (test, 0x80050, 1) == 8);
  assert (!(read_register (test, 0x72037, 1) & 1));
  assert (read_register (test, 0x29000 + 0x340, 4) == 0x120100d0);
  assert (read_register (test, 0x299cc + 0x28, 4) == 0x120100db);
  assert (read_register (test, 0x28000, 4) == 0x12000000); /* Held data retained. */
  write_register (test, 0x80050, 0, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1)); /* Finite stream exhausted. */
  assert (!read_register (test, 0x72024, 1)); /* No fabricated freeze result. */
  close_fixture (test);
}

static void
sample_input_guards (void)
{
  struct fixture *test = open_fixture ();
  unsigned char record[884] = { 0 };
  unsigned char byte;
  char path[] = "/tmp/tpjsampleguardXXXXXX";
  input_file (path, record, sizeof record);
  assert (!tpj_samples (test->device, path)); /* Zero delay is rejected. */
  assert (!unlink (path));
  assert (!sim_core_read_buffer (test->sd, NULL, read_map, &byte, 0x28000, 1));
  assert (tpj_boundary (test->device)); /* Absent data isn't a zero sample. */
  close_fixture (test);
}

int
main (void)
{
  external_producers ();
  original_event_flags ();
  missing_producer ();
  guest_i2c_reply ();
  explicit_update_snapshot ();
  source_one_shot ();
  source_configuration ();
  ordered_notifications ();
  first_source_boundary ();
  sensing_configuration ();
  normal_samples ();
  sample_input_guards ();
  return 0;
}
