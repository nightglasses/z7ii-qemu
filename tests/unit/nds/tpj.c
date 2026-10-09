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
  unsigned transferred;
  transferred = sim_core_write_buffer (test->sd, NULL, write_map, bytes, address, size);
  if (transferred != size)
    {
      fprintf (stderr, "TPJ fixture write address=%08x width=%u transferred=%u\n",
               address, size, transferred);
      tpj_boundary (test->device);
    }
  assert (transferred == size);
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
  unsigned i;
  write_register (test, 0x53000, 0, 4);
  for (i = 0; i < 6; ++i)
    write_register (test, 0x53004 + i * 2, i + 1, 2);
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
  test = open_fixture ();
  {
    unsigned char transform[24] = { 0 };
    char path[] = "/tmp/tpjtransformrequestXXXXXX";
    input_file (path, transform, sizeof transform);
    assert (tpj_transform (test->device, path));
    assert (!unlink (path));
  }
  write_register (test, 0x53000, 0, 4);
  write_register (test, 0x53002, 1, 1);
  assert (tpj_boundary (test->device)); /* Missing input isn't a zero tuple. */
  assert (!read_register (test, 0x53001, 1));
  close_fixture (test);
  test = open_fixture ();
  /* Even the one-byte program needs the actual O0bf8 count-minus-one store. */
  flash_command (test, 2, 0);
  assert (tpj_boundary (test->device));
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
  write_register (test, 0x80190, 0x12345678, 4);
  assert (read_register (test, 0x80190, 4) == 0x12345678);
  write_register (test, 0x80064, 0x5a, 1);
  assert (read_register (test, 0x80064, 2) == 0x5a);
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
  unsigned char records[2 * 896 + 12] = { 0 };
  char path[] = "/tmp/tpjsamplesXXXXXX";
  unsigned record, word, byte;
  write_register (test, 0x80003, 0x40, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* No input, no production. */
  for (record = 0; record < 2; ++record)
    {
      unsigned offset = record ? 908 : 0;
      records[offset] = 1;
      records[offset + 4] = record ? 8 : 4;
      for (word = 0; word < 220; ++word)
        {
          uint32_t measurement = 0x12000000 + record * 0x10000 + word;
          for (byte = 0; byte < 4; ++byte)
            records[offset + 16 + word * 4 + byte] = measurement >> (8 * byte);
        }
    }
  records[896] = 4; /* Independent QUIESCE between the two raw records. */
  records[900] = 12;
  records[904] = 2; /* Explicit SCAN completion, never derived from NORMAL. */
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
  for (word = 0; word < 4; ++word)
    write_register (test, 0x80124 + word * 4, 0x100 + word, 4);
  write_register (test, 0x80116, 0x1100, 2);
  write_register (test, 0x8019c, 2, 1);
  write_register (test, 0x8022b, 0, 1);
  write_register (test, 0x7203a, 0, 1); /* Outer7d30 source freeze mode. */
  write_register (test, 0x802b6, 6, 1); /* Both buffers held. */
  write_register (test, 0x80050, 0, 1); /* Actual original masked ack. */
  assert (!nds32_system_read_sr (cpu, SRIDX_INT_PEND));
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (read_register (test, 0x80050, 1) == 0x20); /* Supplied independent quiescence. */
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x7203a, 1, 1); /* Restore normal controller mode. */
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
  unsigned char record[896] = { 1 };
  unsigned char byte;
  unsigned i;
  char path[] = "/tmp/tpjsampleguardXXXXXX";
  input_file (path, record, sizeof record);
  assert (!tpj_samples (test->device, path)); /* Zero delay is rejected. */
  assert (!unlink (path));
  for (i = 0; i < 4; ++i)
    {
      char invalid[] = "/tmp/tpjprocessingguardXXXXXX";
      record[4] = 4;
      record[8] = i == 0 ? 3 : i == 2 ? 1 : 0;
      record[12] = i == 1 ? 1 : 0;
      input_file (invalid, record, sizeof record - (i == 3 ? 8 : 0));
      assert (!tpj_samples (test->device, invalid));
      assert (!unlink (invalid));
    }
  for (i = 0; i < 4; ++i)
    {
      unsigned char quiesce[] = { 4, 0, 0, 0, 12, 0, 0, 0, 2, 0, 0, 0 };
      char invalid[] = "/tmp/tpjquiesceguardXXXXXX";
      if (i == 0)
        quiesce[4] = 0;
      else if (i == 1)
        quiesce[8] = 0;
      else if (i == 2)
        quiesce[8] = 3;
      else
        quiesce[9] = 1;
      input_file (invalid, quiesce, sizeof quiesce);
      assert (!tpj_samples (test->device, invalid));
      assert (!unlink (invalid));
    }
  assert (!sim_core_read_buffer (test->sd, NULL, read_map, &byte, 0x28000, 1));
  assert (tpj_boundary (test->device)); /* Absent data isn't a zero sample. */
  close_fixture (test);
}

static void
processing_setup (struct fixture *test)
{
  unsigned i;
  write_register (test, 0x72040, 0x28000, 4);
  write_register (test, 0x72044, 0x29000, 4);
  write_register (test, 0x72080, 0x289cc, 4);
  write_register (test, 0x72084, 0x299cc, 4);
  for (i = 0; i < 4; ++i)
    write_register (test, 0x80124 + i * 4, 0x100 + i, 4);
  write_register (test, 0x80116, 0x1100, 2);
  write_register (test, 0x7203a, 0, 1);
  write_register (test, 0x8022b, 1, 1);
  write_register (test, 0x80003, 0x40, 1);
}

static void
normal_record (unsigned char *record, unsigned kind, unsigned delay,
               uint32_t measurement)
{
  unsigned i;
  memset (record, 0, 896);
  record[0] = 1;
  record[4] = 4;
  record[8] = kind;
  for (i = 0; i < 4; ++i)
    record[12 + i] = delay >> (8 * i);
  for (i = 0; i < 880; ++i)
    record[16 + i] = measurement >> ((i % 4) * 8);
}

static void
processing_input (struct fixture *test, const unsigned char *records, unsigned size)
{
  char path[] = "/tmp/tpjprocessingXXXXXX";
  input_file (path, records, size);
  assert (tpj_samples (test->device, path));
  assert (!unlink (path));
}

static void
normal_processing (void)
{
  struct fixture *test = open_fixture ();
  unsigned char records[2 * 896];
  /* NONE stays NONE even with the actual processing request and slot mask. */
  processing_setup (test);
  normal_record (records, 0, 0, 0x1111);
  processing_input (test, records, 896);
  finish_event (test);
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x802b6, 2, 1);
  assert (nds32_system_run (test->sd, 24) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1));
  assert (!read_register (test, 0x80050, 1));
  close_fixture (test);

  test = open_fixture ();
  processing_setup (test);
  normal_record (records, 1, 12, 0x2222);
  processing_input (test, records, 896);
  finish_event (test);
  assert (read_register (test, 0x80050, 1) == 8);
  assert (!read_register (test, 0x72024, 1)); /* First phase is only scan progress. */
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x7203a, 1, 1);
  write_register (test, 0x802b6, 2, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1)); /* Inactive processing profile. */
  write_register (test, 0x7203a, 0, 1);
  write_register (test, 0x8022b, 0, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x8022b, 1, 1);
  finish_event (test);
  write_register (test, 0x802b6, 0, 1); /* Cancel before the independent deadline. */
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x802b6, 2, 1);
  write_register (test, 0x80116, 0x2200, 2); /* Another configuration is not this frame. */
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x80116, 0x1100, 2);
  finish_event (test);
  write_register (test, 0x80003, 0, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x80003, 0x40, 1);
  assert (nds32_system_run (test->sd, 16) == SIM_RC_OK);
  assert (read_register (test, 0x72024, 1) == 1);
  assert (nds32_system_read_sr (STATE_CPU (test->sd, 0), SRIDX_INT_PEND) == (1U << 7));
  assert (!read_register (test, 0x80050, 1)); /* BUFFER never becomes SCAN. */
  write_register (test, 0x72024, 0, 4);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1)); /* One completion per generation. */
  close_fixture (test);

  test = open_fixture ();
  processing_setup (test);
  normal_record (records, 1, 12, 0x3333);
  normal_record (records + 896, 0, 0, 0x4444);
  records[896 + 4] = 12;
  processing_input (test, records, sizeof records);
  finish_event (test);
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x802b6, 2, 1);
  finish_event (test);
  write_register (test, 0x802b6, 4, 1); /* Old event canceled; slot0 can be replaced. */
  assert (nds32_system_run (test->sd, 16) == SIM_RC_OK);
  assert (read_register (test, 0x28000, 4) == 0x4444);
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x802b6, 2, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x72024, 1)); /* New NONE frame cannot inherit old ACK. */
  close_fixture (test);

  test = open_fixture ();
  processing_setup (test);
  normal_record (records, 2, 12, 0x5555);
  processing_input (test, records, 896);
  finish_event (test);
  write_register (test, 0x80050, 0, 1);
  write_register (test, 0x8004c, 0x20, 1);
  write_register (test, 0x802b6, 6, 1); /* Explicit SCAN work retains its own deadline. */
  finish_event (test);
  assert (!read_register (test, 0x80050, 1));
  assert (nds32_system_run (test->sd, 16) == SIM_RC_OK);
  assert (read_register (test, 0x80050, 1) == 0x20);
  assert (!read_register (test, 0x72024, 1));
  write_register (test, 0x80050, 0, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1)); /* No implicit drain for the same work. */
  close_fixture (test);

  test = open_fixture ();
  write_register (test, 0x72040, 0x28000, 4);
  write_register (test, 0x72044, 0x29000, 4);
  write_register (test, 0x72080, 0x289cc, 4);
  write_register (test, 0x72084, 0x299cc, 4);
  write_register (test, 0x80003, 0x40, 1);
  normal_record (records, 1, 12, 0x6666);
  processing_input (test, records, 896);
  assert (nds32_system_run (test->sd, 8) == SIM_RC_OK);
  assert (tpj_boundary (test->device)); /* Missing config isn't a zero descriptor. */
  assert (!read_register (test, 0x72024, 1));
  close_fixture (test);
}

static void
engine_quiescence (void)
{
  struct fixture *test = open_fixture ();
  sim_cpu *cpu = STATE_CPU (test->sd, 0);
  unsigned char record[12] = { 4, 0, 0, 0, 12, 0, 0, 0, 2 };
  unsigned kind;
  write_register (test, 0x802b6, 6, 1); /* Cold inactive engine is not ready. */
  finish_event (test);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x80003, 0x40, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* No actual ownership transition. */
  processing_setup (test);
  write_register (test, 0x7203a, 0, 1);
  write_register (test, 0x8022b, 0, 1);
  write_register (test, 0x8019c, 2, 1);
  write_register (test, 0x8004c, 0x20, 1);
  write_register (test, 0x802b6, 0, 1);
  write_register (test, 0x802b6, 6, 1);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1)); /* No supplied quiescence, no completion. */
  processing_input (test, record, sizeof record);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* Explicit deadline isn't one tick. */
  write_register (test, 0x802b6, 0, 1);
  write_register (test, 0x802b6, 6, 1);
  write_register (test, 0x802b6, 0, 1); /* a818 restore before native expiry. */
  finish_event (test);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x802b6, 6, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x80116, 0x2200, 2); /* Replacement invalidates old request. */
  write_register (test, 0x80116, 0x1100, 2);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x802b6, 0, 1);
  write_register (test, 0x802b6, 6, 1);
  finish_event (test);
  write_register (test, 0x72014, 0, 4); /* Stop cancels, restart needs fresh transition. */
  write_register (test, 0x72014, 1, 4);
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1));
  write_register (test, 0x802b6, 0, 1);
  write_register (test, 0x802b6, 6, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1));
  assert (nds32_system_run (test->sd, 16) == SIM_RC_OK);
  assert (read_register (test, 0x80050, 1) == 0x20);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == (1U << 12));
  write_register (test, 0x80050, 0, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* No repeat from held level. */
  assert (!read_register (test, 0x72024, 1));
  close_fixture (test);

  /* Both status domains are explicit, and incomplete requests never match. */
  for (kind = 1; kind <= 2; ++kind)
    {
      test = open_fixture ();
      record[8] = kind;
      processing_input (test, record, sizeof record);
      write_register (test, 0x80003, 0x40, 1);
      write_register (test, 0x802b6, 6, 1);
      assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
      assert (!read_register (test, 0x72024, 1) && !read_register (test, 0x80050, 1));
      processing_setup (test);
      write_register (test, 0x7203a, 1, 1); /* Inner7778 exit is not freeze mode. */
      write_register (test, 0x8022b, 0, 1);
      write_register (test, 0x8019c, 2, 1);
      write_register (test, 0x802b6, 0, 1);
      write_register (test, 0x802b6, 6, 1);
      assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
      assert (!read_register (test, 0x72024, 1) && !read_register (test, 0x80050, 1));
      write_register (test, 0x7203a, 0, 1); /* Outer7d30 clear requires a new edge. */
      assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
      assert (!read_register (test, 0x72024, 1) && !read_register (test, 0x80050, 1));
      write_register (test, 0x802b6, 0, 1);
      write_register (test, 0x802b6, 6, 1);
      finish_event (test);
      write_register (test, 0x8019c, 3, 1);
      write_register (test, 0x8019c, 2, 1);
      assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
      assert (!read_register (test, 0x72024, 1) && !read_register (test, 0x80050, 1));
      write_register (test, 0x802b6, 0, 1);
      write_register (test, 0x802b6, 6, 1);
      assert (nds32_system_run (test->sd, 16) == SIM_RC_OK);
      assert (read_register (test, 0x72024, 1) == (kind == 1));
      assert (read_register (test, 0x80050, 1) == (kind == 2 ? 0x20 : 0));
      write_register (test, 0x72024, 0, 4);
      write_register (test, 0x80050, 0, 1);
      write_register (test, 0x802b6, 0, 1);
      write_register (test, 0x802b6, 6, 1);
      assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
      assert (!read_register (test, 0x72024, 1) && !read_register (test, 0x80050, 1));
      close_fixture (test);
    }
}

static void
indexed_capture (void)
{
  struct fixture *test = open_fixture ();
  unsigned char record[824] = { 2, 0, 0, 0, 4 };
  const char snapshot[] = "80220 2 a123\n";
  char control_path[] = "/tmp/tpjcapturecontrolXXXXXX";
  char path[] = "/tmp/tpjcaptureXXXXXX";
  unsigned i;
  for (i = 0; i < 10; ++i)
    write_register (test, 0x800c0 + i * 4, 0, 4);
  write_register (test, 0x72000, 18, 1);
  write_register (test, 0x8019c, 2, 1);
  write_register (test, 0x80116, 0x1234, 2);
  /* Explicit initial control, then the actual Ob23c/248 halfword RMW. */
  input_file (control_path, snapshot, sizeof snapshot - 1);
  assert (tpj_snapshot (test->device, control_path));
  assert (!unlink (control_path));
  write_register (test, 0x80220,
                  (read_register (test, 0x80220, 2) & 0xf000) | 0x7d8, 2);
  assert (read_register (test, 0x80220, 2) == 0xa7d8);
  write_register (test, 0x8004c, 4, 1);
  record[48] = 18;
  record[49] = 2;
  record[52] = 0x34;
  record[53] = 0x12;
  record[54] = 0xd8;
  record[55] = 0xa7;
  for (i = 0; i < 0x180; ++i)
    {
      record[56 + i * 2] = i;
      record[57 + i * 2] = (0x5000 + i) >> 8;
    }
  input_file (path, record, sizeof record);
  assert (tpj_samples (test->device, path));
  assert (!unlink (path));
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* Supplied data needs a request. */
  write_register (test, 0x80218, 1, 1);
  write_register (test, 0x80218, 0, 1);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* Canceled record wasn't consumed. */
  write_register (test, 0x80218, 1, 1);
  finish_event (test);
  assert (read_register (test, 0x80050, 1) == 4);
  assert (read_register (test, 0x802b6, 1) & 0x40);
  assert (!(read_register (test, 0x80218, 1) & 1));
  for (i = 0; i < 0x180; ++i)
    {
      write_register (test, 0x80204, i, 2);
      assert (read_register (test, 0x80208, 2) == 0x5000 + i);
    }
  /* Ob254/260 restores the source mode without losing the high bits. */
  write_register (test, 0x80220,
                  (read_register (test, 0x80220, 2) & 0xf000) | 0x800, 2);
  assert (read_register (test, 0x80220, 2) == 0xa800);
  close_fixture (test);
  test = open_fixture ();
  {
    char mismatch[] = "/tmp/tpjcaptureguardXXXXXX";
    input_file (mismatch, record, sizeof record);
    assert (tpj_samples (test->device, mismatch));
    assert (!unlink (mismatch));
  }
  for (i = 0; i < 10; ++i)
    write_register (test, 0x800c0 + i * 4, 0, 4);
  write_register (test, 0x72000, 19, 1); /* Complete request, distinct row count. */
  write_register (test, 0x8019c, 2, 1);
  write_register (test, 0x80116, 0x1234, 2);
  write_register (test, 0x80220, 0xa7d8, 2);
  write_register (test, 0x80218, 1, 1); /* Genuine request, unmatched tuple. */
  assert (nds32_system_run (test->sd, 8) == SIM_RC_OK);
  assert (tpj_boundary (test->device));
  assert (!read_register (test, 0x80050, 1));
  assert (!(read_register (test, 0x802b6, 1) & 0x40));
  close_fixture (test);
  test = open_fixture ();
  {
    unsigned char absent[824] = { 2, 0, 0, 0, 4 };
    char missing[] = "/tmp/tpjcapturerequestXXXXXX";
    input_file (missing, absent, sizeof absent);
    assert (tpj_samples (test->device, missing));
    assert (!unlink (missing));
  }
  write_register (test, 0x80218, 1, 1);
  assert (nds32_system_run (test->sd, 8) == SIM_RC_OK);
  assert (tpj_boundary (test->device)); /* Absent descriptors aren't zero tuples. */
  assert (!read_register (test, 0x80050, 1));
  assert (!(read_register (test, 0x802b6, 1) & 0x40));
  close_fixture (test);
}

static void
reference_completion (void)
{
  struct fixture *test = open_fixture ();
  unsigned char record[892] = { 3, 0, 0, 0, 4, 0, 0, 0, 16 };
  char path[] = "/tmp/tpjreferenceXXXXXX";
  unsigned i;
  for (i = 0; i < 220; ++i)
    {
      record[12 + i * 4] = i;
      record[13 + i * 4] = 0x30;
    }
  input_file (path, record, sizeof record);
  assert (tpj_samples (test->device, path));
  assert (!unlink (path));
  write_register (test, 0x802b6, 6, 1); /* Special request has its own source pairs. */
  write_register (test, 0x72040, 0x2a000, 4);
  write_register (test, 0x72044, 0x2a000, 4);
  write_register (test, 0x72080, 0x2a9cc, 4);
  write_register (test, 0x72084, 0x2a9cc, 4);
  write_register (test, 0x8004c, 0x20, 1);
  write_register (test, 0x72039, 1, 1);
  finish_event (test);
  assert (read_register (test, 0x72024, 1) == 1);
  assert (!read_register (test, 0x80050, 1));
  assert (read_register (test, 0x2a340, 4) == 0x30d0);
  assert (read_register (test, 0x2a9f4, 4) == 0x30db);
  assert (nds32_system_run (test->sd, 24) == SIM_RC_OK);
  assert (!read_register (test, 0x80050, 1)); /* First event still unacknowledged. */
  write_register (test, 0x72024, 0, 4);
  finish_event (test);
  assert (!read_register (test, 0x80050, 1)); /* Explicit second-stage delay. */
  assert (nds32_system_run (test->sd, 20) == SIM_RC_OK);
  assert (read_register (test, 0x80050, 1) == 0x20);
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
  normal_processing ();
  engine_quiescence ();
  indexed_capture ();
  reference_completion ();
  return 0;
}
