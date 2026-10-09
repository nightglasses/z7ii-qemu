/* Source-backed TPJ register domains and explicit external data producers.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "sim-core.h"
#include "sim-events.h"
#include "tpj.h"

#define READ_FIELD 1U
#define WRITE_FIELD 2U
#define FLASH_BASE 0x41000U
#define TRANSFORM_BASE 0x53000U

struct field {
  uint32_t address;
  unsigned size;
  unsigned access;
};
static const struct field fields[] = {
#define TPJ_FIELD(address, size, access) { address, size, access },
#include "fields.h"
#undef TPJ_FIELD
};
/* Full helper/handler traces resolve these bases across calls and loops; they
   supplement, rather than widen, the locally qualified expression inventory. */
static const struct field traced[] = {
  { 0x41011, 1, READ_FIELD }, /* O0af8 busy poll. */
  { 0x41012, 1, READ_FIELD }, /* O0c3c latched data, read twice. */
  { 0x50014, 1, READ_FIELD | WRITE_FIELD }, /* O0830. */
  { 0x50054, 1, READ_FIELD | WRITE_FIELD }, /* O0804. */
  { 0x50094, 1, READ_FIELD | WRITE_FIELD }, /* O081a. */
  { 0x53001, 1, READ_FIELD }, /* Oa742 six-halfword operation. */
  { 0x6001c, 1, READ_FIELD | WRITE_FIELD }, /* O29d4/29dc. */
  { 0x60020, 1, READ_FIELD | WRITE_FIELD }, /* O2a50/2a58. */
  { 0x60021, 1, READ_FIELD | WRITE_FIELD }, /* O2a60/2a6a. */
  { 0x60028, 1, READ_FIELD }, /* O29e0 command key. */
  { 0x600a0, 1, READ_FIELD | WRITE_FIELD }, /* O2676/267c. */
  { 0x721ce, 2, READ_FIELD | WRITE_FIELD } /* O635c flag test/clear. */
};

struct bank {
  uint32_t base;
  unsigned size;
  unsigned char value[0x300];
  unsigned char present[0x300];
};

struct tpj {
  SIM_DESC sd;
  tpj_emit emit;
  void *opaque;
  struct bank banks[11];
  unsigned char scan_a[0x344];
  unsigned char scan_b[0x344];
  unsigned char *flash;
  size_t flash_size;
  sim_event *flash_event;
  unsigned flash_unlock;
  unsigned flash_command;
  uint32_t flash_address;
  unsigned flash_count;
  unsigned flash_received;
  unsigned char flash_payload[256];
  int flash_write_enabled;
  int flash_awake;
  unsigned char transform_input[12];
  unsigned char transform_output[12];
  int transform_available;
  sim_event *transform_event;
  int blocked;
  uint32_t blocked_pc;
  uint32_t blocked_address;
  unsigned blocked_width;
  const char *blocked_reason;
  uint64_t clock_token;
  unsigned clock_bits;
  uint32_t clock_word;
  uint64_t clock_time;
  int selected;
};

static struct bank *
find_bank (struct tpj *device, uint32_t address, unsigned size)
{
  unsigned i;
  for (i = 0; i < sizeof device->banks / sizeof device->banks[0]; ++i)
    {
      struct bank *bank = &device->banks[i];
      if (address >= bank->base && size <= bank->size &&
          address - bank->base <= bank->size - size)
        return bank;
    }
  return NULL;
}

static int
known_field (uint32_t address, unsigned size, unsigned access)
{
  unsigned i;
  if ((size != 1 && size != 2 && size != 4) || address % size)
    return 0;
  for (i = 0; i < sizeof fields / sizeof fields[0]; ++i)
    if (fields[i].address == address && fields[i].size == size &&
        (fields[i].access & access))
      return 1;
  for (i = 0; i < sizeof traced / sizeof traced[0]; ++i)
    if (traced[i].address == address && traced[i].size == size &&
        (traced[i].access & access))
      return 1;
  /* Curated original indexed/helper loops, not inferred flat banks. */
  if (access == WRITE_FIELD && address >= 0x60040 &&
      address + size <= 0x60060 && (size == 1 || size == 2))
    return 1;
  if (access == READ_FIELD && address >= 0x60080 &&
      address + size <= 0x600a0 && size == 1)
    return 1;
  if (access == WRITE_FIELD && address >= 0x800c0 &&
      address + size <= 0x800e8 && size == 4)
    return 1;
  if (access == WRITE_FIELD && address >= 0x800c0 &&
      address + size <= 0x800c9 && size == 1)
    return 1;
  if (access == WRITE_FIELD && address >= 0x53004 &&
      address + size <= 0x53010 && size == 2)
    return 1;
  if (access == READ_FIELD && address >= 0x53010 &&
      address + size <= 0x5301c && size == 2)
    return 1;
  return 0;
}

static uint32_t
value (struct tpj *device, uint32_t address, unsigned size)
{
  struct bank *bank = find_bank (device, address, size);
  uint32_t result = 0;
  unsigned i;
  for (i = 0; i < size; ++i)
    result |= (uint32_t) bank->value[address - bank->base + i] << (i * 8);
  return result;
}

static void
store (struct tpj *device, uint32_t address, uint32_t data, unsigned size)
{
  struct bank *bank = find_bank (device, address, size);
  unsigned i;
  for (i = 0; i < size; ++i)
    {
      bank->value[address - bank->base + i] = data >> (i * 8);
      bank->present[address - bank->base + i] = 1;
    }
}

static void
block (struct tpj *device, uint32_t address, unsigned size, const char *reason)
{
  if (!device->blocked)
    {
      device->blocked = 1;
      device->blocked_pc = CPU_PC_GET (STATE_CPU (device->sd, 0));
      device->blocked_address = address;
      device->blocked_width = size;
      device->blocked_reason = reason;
    }
}

static void
event_inputs (struct tpj *device)
{
  unsigned i;
  int command = 0;
  for (i = 0; i < 6; ++i)
    command |= value (device, 0x6001c + i, 1) & 1;
  /* Logical source-event profile: vector slots identify real guest consumers.
     This is not an assertion about board pin numbers or a SPI clock route. */
  nds32_system_irq (device->sd, 2, command);
  nds32_system_irq (device->sd, 3, value (device, 0x50014, 1) & 1);
  nds32_system_irq (device->sd, 5, value (device, 0x50054, 1) & 1);
  nds32_system_irq (device->sd, 8, value (device, 0x50094, 1) & 1);
  nds32_system_irq (device->sd, 7, !!(value (device, 0x72024, 4) & 0x101));
  nds32_system_irq (device->sd, 9, !!(value (device, 0x721c0, 2) & 0x100));
  nds32_system_irq (device->sd, 12, !!(value (device, 0x80050, 1) & 0x2d));
}

static void
flash_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  uint32_t address = device->flash_address;
  unsigned command = device->flash_command;
  unsigned i;
  (void) sd;
  device->flash_event = NULL;
  if (!device->flash)
    {
      block (device, 0x41011, 1, "external flash backing is absent");
      return;
    }
  switch (command)
    {
    case 0xab: device->flash_awake = 1; break;
    case 0xb9: device->flash_awake = 0; break;
    case 0x06:
      if (!device->flash_awake)
        goto asleep;
      device->flash_write_enabled = 1;
      break;
    case 0x3b:
      if (!device->flash_awake)
        goto asleep;
      if (address >= device->flash_size)
        goto missing;
      store (device, 0x41012, device->flash[address], 1);
      break;
    case 0x20:
      address &= ~0xfffU;
      if (!device->flash_awake)
        goto asleep;
      if (!device->flash_write_enabled || address > device->flash_size ||
          0x1000 > device->flash_size - address)
        goto missing;
      memset (device->flash + address, 0xff, 0x1000);
      device->flash_write_enabled = 0;
      break;
    case 0x02:
      if (!device->flash_awake)
        goto asleep;
      if (!device->flash_write_enabled || address > device->flash_size ||
          device->flash_count > device->flash_size - address)
        goto missing;
      for (i = 0; i < device->flash_count; ++i)
        device->flash[address + i] &= device->flash_payload[i];
      device->flash_write_enabled = 0;
      break;
    default:
      block (device, 0x41000, 1, "flash command has no source-backed operation");
      return;
    }
  store (device, 0x41011, 0, 1);
  return;
asleep:
  block (device, 0x41000, 1, "flash operation while external device is asleep");
  return;
missing:
  block (device, 0x41012, 1, "flash operation has no complete writable/readable extent");
}

static void
flash_request (struct tpj *device)
{
  if (device->flash_event || value (device, 0x41011, 1))
    {
      block (device, 0x41000, 1, "flash command overlaps an active operation");
      return;
    }
  device->flash_command = value (device, 0x41000, 1);
  device->flash_address = value (device, 0x41000, 4) >> 8;
  store (device, 0x41011, 1, 1);
  if (device->flash_command == 2)
    {
      /* O0b9c computes byte count-1, O0bf8 writes it. */
      device->flash_count = value (device, 0x41004, 1) + 1;
      device->flash_received = 0;
    }
  else
    device->flash_event = sim_events_schedule (device->sd, 1, flash_finish, device);
}

static void
transform_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  unsigned i;
  (void) sd;
  device->transform_event = NULL;
  for (i = 0; i < 12; ++i)
    store (device, 0x53010 + i, device->transform_output[i], 1);
  store (device, 0x53001, 1, 1);
}

static void
transform_request (struct tpj *device)
{
  struct bank *bank = find_bank (device, TRANSFORM_BASE, 0x1c);
  store (device, 0x53001, 0, 1);
  if (device->transform_event)
    {
      block (device, 0x53002, 1, "transformation overlaps an active request");
      return;
    }
  if (!device->transform_available ||
      memcmp (bank->value + 4, device->transform_input, 12))
    {
      block (device, 0x53004, 12, "no external result matches the six requested halfwords");
      return;
    }
  device->transform_event = sim_events_schedule (device->sd, 1,
                                               transform_finish, device);
}

static unsigned
register_read (void *opaque, uint32_t address, void *buffer, unsigned size)
{
  struct tpj *device = opaque;
  struct bank *bank = find_bank (device, address, size);
  unsigned i;
  if (!bank || !known_field (address, size, READ_FIELD))
    {
      block (device, address, size, "read outside a proved field/width");
      return 0;
    }
  for (i = 0; i < size; ++i)
    if (!bank->present[address - bank->base + i])
      {
        block (device, address, size, "external field data has no producer");
        return 0;
      }
  memcpy (buffer, bank->value + address - bank->base, size);
  return size;
}

static unsigned
register_write (void *opaque, uint32_t address, const void *buffer, unsigned size)
{
  struct tpj *device = opaque;
  struct bank *bank = find_bank (device, address, size);
  uint32_t previous, data = 0;
  unsigned i;
  if (!bank || !known_field (address, size, WRITE_FIELD))
    {
      block (device, address, size, "write outside a proved field/width");
      return 0;
    }
  previous = value (device, 0x53002, 1);
  for (i = 0; i < size; ++i)
    data |= (uint32_t) ((const unsigned char *) buffer)[i] << (i * 8);
  store (device, address, data, size);
  if (address == 0x41005)
    device->flash_unlock = data == 0x55 ? 1 : 0;
  if (address == 0x41006)
    device->flash_unlock = device->flash_unlock == 1 && data == 0xaa ? 2 : 0;
  if (address == 0x41007)
    {
      if (device->flash_unlock == 2 && data == 0x66)
        flash_request (device);
      device->flash_unlock = 0;
    }
  if (address == 0x41020)
    {
      if (device->flash_command != 2 || device->flash_event ||
          device->flash_received >= device->flash_count)
        block (device, address, size, "flash payload has no active program request");
      else
        {
          device->flash_payload[device->flash_received++] = data;
          if (device->flash_received == device->flash_count)
            device->flash_event = sim_events_schedule (device->sd, 1,
                                                      flash_finish, device);
        }
    }
  if (address <= 0x53002 && address + size > 0x53002 &&
      !(previous & 1) && (value (device, 0x53002, 1) & 1))
    transform_request (device);
  event_inputs (device);
  return size;
}

struct tpj *
tpj_open (SIM_DESC sd, tpj_emit emit, void *opaque)
{
  static const uint32_t bases[] = {
    0x40000, 0x41000, 0x42000, 0x50000, 0x53000, 0x60000,
    0x61000, 0x70000, 0x71000, 0x72000, 0x80000
  };
  static const unsigned sizes[] = {
    0x44, 0x24, 0x44, 0x98, 0x1c, 0xa1, 8, 0x1c, 0x14, 0x1d0, 0x300
  };
  static const struct nds32_boundary_ops ops = { register_read, register_write };
  struct tpj *device = calloc (1, sizeof *device);
  unsigned i, n;
  if (!device)
    return NULL;
  device->sd = sd;
  device->emit = emit;
  device->opaque = opaque;
  for (i = 0; i < sizeof bases / sizeof bases[0]; ++i)
    {
      device->banks[i].base = bases[i];
      device->banks[i].size = sizes[i];
      if (nds32_system_attach (sd, bases[i], sizes[i], &ops, device) != SIM_RC_OK)
        goto failure;
    }
  /* Explicit virtual cold configuration, not physical register reset proof.
     Only software read/write fields exist initially; read-only inputs require
     an actual external producer or a supplied snapshot. */
  for (i = 0; i < sizeof fields / sizeof fields[0]; ++i)
    if (fields[i].access == (READ_FIELD | WRITE_FIELD))
      for (n = 0; n < fields[i].size; ++n)
        store (device, fields[i].address + n, 0, 1);
  for (i = 0; i < sizeof traced / sizeof traced[0]; ++i)
    if (traced[i].access == (READ_FIELD | WRITE_FIELD))
      store (device, traced[i].address, 0, traced[i].size);
  /* Virtual inactive completion inputs, never inferred physical reset values. */
  store (device, 0x72024, 0, 4);
  store (device, 0x41011, 0, 1);
  store (device, 0x53001, 0, 1);
  sim_core_attach (sd, NULL, 0, access_read | access_write, 0, 0x2a000,
                   sizeof device->scan_a, 0, NULL, device->scan_a);
  sim_core_attach (sd, NULL, 0, access_read | access_write, 0, 0x2b000,
                   sizeof device->scan_b, 0, NULL, device->scan_b);
  event_inputs (device);
  return device;
failure:
  /* The caller closes the simulator immediately; retain bound callback data
     until that close, rather than freeing an opaque already registered. */
  block (device, 0, 0, "finite region registration failed");
  return device;
}

void
tpj_quiesce (struct tpj *device)
{
  if (device->flash_event)
    sim_events_deschedule (device->sd, device->flash_event);
  if (device->transform_event)
    sim_events_deschedule (device->sd, device->transform_event);
  device->flash_event = device->transform_event = NULL;
  device->clock_token = 0;
}

void
tpj_close (struct tpj *device)
{
  free (device->flash);
  free (device);
}

static unsigned char *
read_input (const char *path, size_t *size, size_t maximum)
{
  FILE *input = fopen (path, "rb");
  unsigned char *bytes = NULL;
  long length;
  if (!input)
    return NULL;
  if (fseek (input, 0, SEEK_END) || (length = ftell (input)) <= 0 ||
      (unsigned long) length > maximum || fseek (input, 0, SEEK_SET))
    goto finish;
  bytes = malloc (length);
  if (!bytes || fread (bytes, 1, length, input) != (size_t) length)
    {
      free (bytes);
      bytes = NULL;
    }
  else
    *size = length;
finish:
  fclose (input);
  return bytes;
}

int
tpj_flash (struct tpj *device, const char *path)
{
  if (device->flash)
    return 0;
  device->flash = read_input (path, &device->flash_size, 0x1000000);
  return !!device->flash;
}

int
tpj_transform (struct tpj *device, const char *path)
{
  size_t size;
  unsigned char *sample = read_input (path, &size, 24);
  if (!sample)
    return 0;
  if (size != 24)
    {
      free (sample);
      return 0;
    }
  memcpy (device->transform_input, sample, 12);
  memcpy (device->transform_output, sample + 12, 12);
  device->transform_available = 1;
  free (sample);
  return 1;
}

int
tpj_snapshot (struct tpj *device, const char *path)
{
  FILE *input = fopen (path, "r");
  uint32_t address, data;
  unsigned size;
  int count;
  if (!input)
    return 0;
  while ((count = fscanf (input, "%" SCNx32 " %u %" SCNx32,
                          &address, &size, &data)) == 3)
    {
      if (!find_bank (device, address, size) ||
          !known_field (address, size, READ_FIELD) ||
          (address < 0x41013 && address + size > 0x41011) ||
          (address <= 0x53001 && address + size > 0x53001) ||
          (size < 4 && data >> (size * 8)))
        {
          fclose (input);
          return 0;
        }
      store (device, address, data, size);
    }
  count = count == EOF && !ferror (input);
  fclose (input);
  event_inputs (device);
  return count;
}

int
tpj_boundary (struct tpj *device)
{
  if (!device->blocked)
    return 0;
  fprintf (stderr, "TPJ boundary pc=%08x address=%08x width=%u: %s\n",
                   device->blocked_pc, device->blocked_address,
                   device->blocked_width, device->blocked_reason);
  return 1;
}

int
tpj_clock (struct tpj *device, uint64_t token, uint32_t word,
           unsigned bits, uint64_t time)
{
  if (device->clock_token)
    return 0;
  device->clock_token = token;
  device->clock_word = word;
  device->clock_bits = bits;
  device->clock_time = time;
  /* Retain the physical request.  Normal wire framing is not established by
     60028 command dispatch; no guessed first-byte decoder or reply exists. */
  return 1;
}

void
tpj_cancel (struct tpj *device, uint64_t token)
{
  if (device->clock_token == token)
    device->clock_token = 0;
}

int
tpj_signal (struct tpj *device, unsigned kind, uint32_t line, int level,
            uint64_t time)
{
  (void) time;
  if (kind == NIKON_PEER_SELECT && line == 0)
    {
      device->selected = !!level;
      return 1;
    }
  /* Do not number 800xx registers as physical GPIO pads without a binding. */
  block (device, line, 1, "physical GPIO/select input has no proved board binding");
  return 1;
}

void
tpj_flush (struct tpj *device)
{
  /* Guest response bytes remain in 60040..60060.  Transport can consume them
     only after the master/header/clock join establishes the hardware framing. */
}
