/* Source-backed TPJ register domains and explicit external data producers.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <inttypes.h>
#include <limits.h>
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
#define FLASH_SIZE 0x10000U
#define UPDATE_SIZE 0xe000U
#define MATRIX_SIZE 0x344U
#define AUXILIARY_SIZE 0x2cU
#define SAMPLE_SIZE (MATRIX_SIZE + AUXILIARY_SIZE)

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
  { 0x721ce, 2, READ_FIELD | WRITE_FIELD }, /* O635c flag test/clear. */
  { 0x80040, 1, READ_FIELD | WRITE_FIELD }, /* Oa992/a99c configuration. */
  { 0x80041, 1, READ_FIELD | WRITE_FIELD }, /* Oa99e/a9a8 configuration. */
  { 0x80042, 1, READ_FIELD | WRITE_FIELD }, /* Oa966..a990 completion/ack. */
  { 0x80068, 1, READ_FIELD | WRITE_FIELD }, /* Oa9ba/a9c4 index 0..10. */
  { 0x8006c, 1, READ_FIELD | WRITE_FIELD }, /* Oa9ce/a9d8 indexed value. */
  { 0x801e0, 2, WRITE_FIELD }, /* Oa50a selected sensing configuration. */
  { 0x801ea, 2, WRITE_FIELD } /* Oa502 nine 64-entry index pages. */
};

struct bank {
  uint32_t base;
  unsigned size;
  unsigned char value[0x300];
  unsigned char present[0x300];
};

struct scan_buffer {
  uint32_t base;
  unsigned size;
  unsigned char bytes[MATRIX_SIZE];
  unsigned char present[MATRIX_SIZE];
};

struct tpj {
  SIM_DESC sd;
  tpj_emit emit;
  void *opaque;
  struct bank banks[11];
  struct scan_buffer buffers[8];
  FILE *samples;
  unsigned char sample[SAMPLE_SIZE];
  unsigned sample_delay;
  int sample_loaded;
  sim_event *sample_event;
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
  sim_event *timer_event;
  sim_event *boundary_event;
  sim_event *settle_event;
  sim_event *enable_event;
  sim_event *config_event;
  unsigned char channels[11];
  unsigned short configuration[0x180];
  unsigned char configuration_valid[0x180];
  unsigned config_cursor;
  unsigned short sensing[0x240];
  int blocked;
  uint32_t blocked_pc;
  uint32_t blocked_address;
  unsigned blocked_width;
  const char *blocked_reason;
  uint64_t clock_token;
  unsigned clock_bits;
  uint32_t clock_word;
  uint64_t clock_time;
  uint64_t i2c_token;
  uint64_t i2c_time;
  unsigned i2c_phase;
  uint32_t i2c_reply;
  int i2c_reply_ready;
  int i2c_selected;
  int i2c_receive;
  int command_present;
  int command_dispatched;
  unsigned payload_count;
  unsigned reply_cursor;
  uint32_t reply_valid;
  int notify_level;
  int notify_known;
  unsigned char notify_edges[64];
  unsigned notify_head;
  unsigned notify_count;
  int control_level;
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
  if (access == WRITE_FIELD && address >= 0x80118 &&
      address + size <= 0x80121 && size == 1)
    return 1; /* Ob0c4/a8b0/a8e0 copy exactly nine source configuration bytes. */
  if (access == WRITE_FIELD && address >= 0x801a0 &&
      address + size <= 0x801a4 && size == 1)
    return 1; /* Oa57a..a58e writes four packed source configuration bytes. */
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
boundary_stop (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  sim_cpu *cpu = STATE_CPU (sd, 0);
  device->boundary_event = NULL;
  /* The failed transfer still raises the original architectural bus fault.
     Stop at the next native event boundary, before re-entering its fault loop. */
  sim_engine_halt (sd, cpu, NULL, CPU_PC_GET (cpu), sim_stopped, SIM_SIGSEGV);
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
      device->boundary_event = sim_events_schedule (device->sd, 0,
                                                  boundary_stop, device);
    }
}

static void
event_inputs (struct tpj *device)
{
  unsigned i;
  int command = 0;
  for (i = 0; i < 6; ++i)
    command |= value (device, 0x6001c + i, 1) & 1;
  /* O1abc/1ba4/62fa check only bit4 before standby wake_grant. The
     functional assignment mirrors the same actual command IRQ cause, not
     bus START, acceptance or reset readiness; other supplied bits remain. */
  store (device, 0x60003,
         (value (device, 0x60003, 1) & ~0x10U) | (command ? 0x10 : 0), 1);
  /* Logical source-event profile: vector slots identify real guest consumers.
     This is not an assertion about board pin numbers or a SPI clock route. */
  nds32_system_irq (device->sd, 2, command);
  nds32_system_irq (device->sd, 3, value (device, 0x50014, 1) & 1);
  nds32_system_irq (device->sd, 5, value (device, 0x50054, 1) & 1);
  nds32_system_irq (device->sd, 8, value (device, 0x50094, 1) & 1);
  nds32_system_irq (device->sd, 7, !!(value (device, 0x72024, 4) & 0x101));
  nds32_system_irq (device->sd, 9, !!(value (device, 0x721c0, 2) & 0x100));
  nds32_system_irq (device->sd, 12,
                    !!(value (device, 0x80050, 1) &
                       value (device, 0x8004c, 1) & 0x2d));
}

static struct scan_buffer *
find_buffer (struct tpj *device, uint32_t address, unsigned size)
{
  unsigned i;
  for (i = 0; i < 8; ++i)
    {
      struct scan_buffer *buffer = &device->buffers[i];
      if (address >= buffer->base && size <= buffer->size &&
          address - buffer->base <= buffer->size - size)
        return buffer;
    }
  return NULL;
}

static unsigned
scan_read (void *opaque, uint32_t address, void *bytes, unsigned size)
{
  struct tpj *device = opaque;
  struct scan_buffer *buffer = find_buffer (device, address, size);
  unsigned i;
  if (!buffer)
    {
      block (device, address, size, "scan read exceeds the source matrix/auxiliary extent");
      return 0;
    }
  for (i = 0; i < size; ++i)
    if (!buffer->present[address - buffer->base + i])
      {
        block (device, address, size, "scan data has no external sample or guest producer");
        return 0;
      }
  memcpy (bytes, buffer->bytes + address - buffer->base, size);
  return size;
}

static unsigned
scan_write (void *opaque, uint32_t address, const void *bytes, unsigned size)
{
  struct tpj *device = opaque;
  struct scan_buffer *buffer = find_buffer (device, address, size);
  if (!buffer)
    {
      block (device, address, size, "scan write exceeds the source matrix/auxiliary extent");
      return 0;
    }
  memcpy (buffer->bytes + address - buffer->base, bytes, size);
  memset (buffer->present + address - buffer->base, 1, size);
  return size;
}

static int
next_sample (struct tpj *device)
{
  unsigned char ticks[4];
  size_t count = fread (ticks, 1, 4, device->samples);
  if (!count && feof (device->samples))
    {
      device->sample_loaded = 0;
      return 1;
    }
  if (count != 4 || fread (device->sample, 1, SAMPLE_SIZE, device->samples)
                    != SAMPLE_SIZE)
    return 0;
  device->sample_delay = (unsigned) ticks[0] | (unsigned) ticks[1] << 8 |
                        (unsigned) ticks[2] << 16 | (unsigned) ticks[3] << 24;
  device->sample_loaded = 1;
  return device->sample_delay && device->sample_delay <= INT_MAX;
}

static void sample_schedule (struct tpj *);

static int
sample_slot (struct tpj *device)
{
  unsigned ownership = value (device, 0x802b6, 1);
  /* Oa636 selects slot0 when72037 bit0 is set, otherwise slot1. Oa660
     holds the corresponding buffer through802b6 bit1/2. Prefer the other
     buffer; never overwrite a guest-held one. */
  unsigned slot = value (device, 0x72037, 1) & 1 ? 1 : 0;
  if (!(ownership & (2U << slot)))
    return slot;
  slot ^= 1;
  return ownership & (2U << slot) ? -1 : (int) slot;
}

static void
sample_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  struct scan_buffer *matrix, *auxiliary;
  int slot;
  uint32_t address;
  (void) sd;
  device->sample_event = NULL;
  slot = sample_slot (device);
  if (!device->sample_loaded || slot < 0)
    return;
  address = value (device, 0x72040 + slot * 4, 4);
  matrix = find_buffer (device, address, MATRIX_SIZE);
  auxiliary = find_buffer (device, value (device, 0x72080 + slot * 4, 4),
                           AUXILIARY_SIZE);
  if (!matrix || matrix->base != address || matrix->size != MATRIX_SIZE ||
      !auxiliary || auxiliary->base != address + 0x9cc ||
      auxiliary->size != AUXILIARY_SIZE)
    {
      block (device, 0x72040 + slot * 4, 4,
             "sample DMA pointers do not name a source matrix/auxiliary pair");
      return;
    }
  memcpy (matrix->bytes, device->sample, MATRIX_SIZE);
  memset (matrix->present, 1, MATRIX_SIZE);
  memcpy (auxiliary->bytes, device->sample + MATRIX_SIZE, AUXILIARY_SIZE);
  memset (auxiliary->present, 1, AUXILIARY_SIZE);
  store (device, 0x72037, (value (device, 0x72037, 1) & ~1U) |
                        (slot == 0), 1);
  /* Only a completed, supplied normal scan raises this source progress bit.
     Oa0e acknowledges it and performs all GP counters and software flags. */
  store (device, 0x80050, value (device, 0x80050, 1) | 8, 1);
  event_inputs (device);
  if (!next_sample (device))
    block (device, 0x80050, 1, "external sample stream changed or became unreadable");
}

static void
sample_schedule (struct tpj *device)
{
  int slot = sample_slot (device);
  int pointers = 0;
  if (slot >= 0)
    {
      struct bank *bank = find_bank (device, 0x72040, 0x48);
      unsigned i;
      pointers = 1;
      for (i = 0; i < 4; ++i)
        pointers &= bank->present[0x40 + slot * 4 + i] &&
                    bank->present[0x80 + slot * 4 + i];
    }
  /* Functional continuous-enable assignment comes from Oab68. The special
     Oac1c one-shot and Oa6ea freeze requests have different output consumers;
     a normal matrix must never manufacture their bit2/bit5/72024 completion. */
  int active = !device->blocked && device->samples && device->sample_loaded &&
               (value (device, 0x80003, 1) & 0x40) &&
               !(value (device, 0x80050, 1) & 8) &&
               !(value (device, 0x80218, 1) & 1) &&
               !((value (device, 0x72039, 1) & 1) &&
                 !(value (device, 0x7203a, 1) & 1)) &&
               pointers && slot >= 0;
  if (!active && device->sample_event)
    {
      sim_events_deschedule (device->sd, device->sample_event);
      device->sample_event = NULL;
    }
  if (active && !device->sample_event)
    device->sample_event = sim_events_schedule (device->sd, device->sample_delay,
                                               sample_finish, device);
}

static void
flash_checksum (struct tpj *device)
{
  uint32_t sum = 0;
  size_t i;
  /* Oca4 validates two data/footer sums, the erased tail and eight final bytes.
     41018 is the functional producer of their actual whole-flash byte sum. */
  for (i = 0; i < device->flash_size; ++i)
    sum += device->flash[i];
  store (device, 0x41018, sum, 4);
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
      flash_checksum (device);
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
      flash_checksum (device);
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
  if (address == 0x80208 && size == 2)
    {
      unsigned cursor = device->config_cursor;
      if (cursor >= 0x180 || !device->configuration_valid[cursor])
        {
          block (device, address, size, "indexed configuration has no guest producer");
          return 0;
        }
      store (device, address, device->configuration[cursor], size);
      /* Oacde reads sixteen values without reselecting; Oad4c writes sixteen.
         The source selects streaming mode through 80214 bit0. */
      if (value (device, 0x80214, 1) & 1)
        ++device->config_cursor;
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

static void
timer_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  (void) sd;
  device->timer_event = NULL;
  if (value (device, 0x50018, 1) & 1)
    {
      store (device, 0x50014, value (device, 0x50014, 1) | 1, 1);
      event_inputs (device);
    }
}

static void
timer_request (struct tpj *device, unsigned previous)
{
  unsigned active = value (device, 0x50018, 1) & 1;
  uint32_t count;
  if (!active && device->timer_event)
    {
      sim_events_deschedule (device->sd, device->timer_event);
      device->timer_event = NULL;
    }
  if (active && !(previous & 1))
    {
      count = value (device, 0x5000c, 4);
      /* Functional source one-shot: programmed counts are GNU virtual ticks.
         No physical oscillator frequency is inferred; zero counts take one
         tick. O7a0 stop/reprogram/start and O830 flag/start ack remain guest. */
      device->timer_event = sim_events_schedule (device->sd, count ? count : 1,
                                               timer_finish, device);
    }
}

static void
settle_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  (void) sd;
  device->settle_event = NULL;
  store (device, 0x80042, value (device, 0x80042, 1) | 0x80, 1);
}

static void
enable_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  (void) sd;
  device->enable_event = NULL;
  store (device, 0x80042, value (device, 0x80042, 1) | 1, 1);
}

static void
config_finish (SIM_DESC sd, void *opaque)
{
  struct tpj *device = opaque;
  (void) sd;
  device->config_event = NULL;
  store (device, 0x80216, value (device, 0x80216, 1) & ~1U, 1);
}

static void
request_edge (struct tpj *device, unsigned previous, unsigned current,
              unsigned bit, sim_event **event, sim_event_handler *handler)
{
  if (!(current & bit) && *event)
    {
      sim_events_deschedule (device->sd, *event);
      *event = NULL;
    }
  if ((current & bit) && !(previous & bit))
    *event = sim_events_schedule (device->sd, 1, handler, device);
}

static void
notify_write (struct tpj *device)
{
  unsigned level = !!(value (device, 0x71010, 1) & 4);
  if (device->notify_known && level == device->notify_level)
    return;
  device->notify_known = 1;
  device->notify_level = level;
  if (device->notify_count == sizeof device->notify_edges)
    {
      block (device, 0x71010, 1, "guest notification queue requires transport progress");
      return;
    }
  device->notify_edges[(device->notify_head + device->notify_count++) %
                       sizeof device->notify_edges] = level;
}

static unsigned
register_write (void *opaque, uint32_t address, const void *buffer, unsigned size)
{
  struct tpj *device = opaque;
  struct bank *bank = find_bank (device, address, size);
  uint32_t previous, timer_previous, request_previous, config_previous, data = 0;
  unsigned i;
  if (!bank || !known_field (address, size, WRITE_FIELD))
    {
      block (device, address, size, "write outside a proved field/width");
      return 0;
    }
  previous = value (device, 0x53002, 1);
  timer_previous = value (device, 0x50018, 1);
  request_previous = value (device, 0x80043, 1);
  config_previous = value (device, 0x80216, 1);
  for (i = 0; i < size; ++i)
    data |= (uint32_t) ((const unsigned char *) buffer)[i] << (i * 8);
  store (device, address, data, size);
  if (address <= 0x71010 && address + size > 0x71010)
    notify_write (device);
  if (address == 0x80043)
    {
      request_edge (device, request_previous, data, 2, &device->settle_event,
                    settle_finish);
      request_edge (device, request_previous, data, 1, &device->enable_event,
                    enable_finish);
    }
  if (address == 0x80216)
    request_edge (device, config_previous, data, 1, &device->config_event,
                  config_finish);
  if (address == 0x80068)
    {
      unsigned index = data & 0x7f;
      if (index >= sizeof device->channels)
        block (device, address, size, "channel configuration index exceeds source extent");
      else
        store (device, 0x8006c, device->channels[index], 1);
    }
  if (address == 0x8006c)
    {
      unsigned index = value (device, 0x80068, 1) & 0x7f;
      if (index >= sizeof device->channels)
        block (device, address, size, "channel configuration index exceeds source extent");
      else
        device->channels[index] = data;
    }
  if (address == 0x80204 && size == 2)
    device->config_cursor = data;
  if (address == 0x80208 && size == 2)
    {
      if (device->config_cursor >= 0x180)
        block (device, address, size, "configuration index exceeds 384 source entries");
      else
        {
          device->configuration[device->config_cursor] = data;
          device->configuration_valid[device->config_cursor] = 1;
          if (value (device, 0x80214, 1) & 1)
            ++device->config_cursor;
        }
    }
  if (address == 0x801e0 && size == 2)
    {
      unsigned index = value (device, 0x801ea, 2);
      if (index >= 0x240)
        block (device, address, size, "sensing configuration exceeds nine source index pages");
      else
        device->sensing[index] = data;
    }
  if (address <= 0x50018 && address + size > 0x50018)
    timer_request (device, timer_previous);
  if (address >= 0x60040 && address + size <= 0x60060)
    for (i = 0; i < size; ++i)
      device->reply_valid |= 1U << (address - 0x60040 + i);
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
  sample_schedule (device);
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
  static const struct nds32_boundary_ops scan_ops = { scan_read, scan_write };
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
  store (device, 0x71010, 4, 4);
  device->notify_level = 1;
  /* Retained receive scratch reset is an explicit functional storage profile.
     WRITE updates only received bytes; untouched bytes retain their old value,
     not a fabricated received length or an automatic packet/event. */
  for (i = 0; i < 0x20; ++i)
    store (device, 0x60080 + i, 0, 1);
  /* Oa450/a41c/a68e program these exact pairs. Original3674/3580
     consume19x11 words plus11 auxiliary words; no intervening holes exist. */
  for (i = 0; i < 8; ++i)
    {
      struct scan_buffer *buffer = &device->buffers[i];
      buffer->base = 0x28000 + (i / 2) * 0x1000 + (i % 2 ? 0x9cc : 0);
      buffer->size = i % 2 ? AUXILIARY_SIZE : MATRIX_SIZE;
      if (nds32_system_attach (sd, buffer->base, buffer->size, &scan_ops, device)
          != SIM_RC_OK)
        goto failure;
    }
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
  if (device->timer_event)
    sim_events_deschedule (device->sd, device->timer_event);
  if (device->boundary_event)
    sim_events_deschedule (device->sd, device->boundary_event);
  if (device->settle_event)
    sim_events_deschedule (device->sd, device->settle_event);
  if (device->enable_event)
    sim_events_deschedule (device->sd, device->enable_event);
  if (device->config_event)
    sim_events_deschedule (device->sd, device->config_event);
  if (device->sample_event)
    sim_events_deschedule (device->sd, device->sample_event);
  device->flash_event = device->transform_event = NULL;
  device->timer_event = NULL;
  device->boundary_event = device->settle_event = device->enable_event = NULL;
  device->config_event = NULL;
  device->sample_event = NULL;
  device->clock_token = 0;
  device->i2c_token = 0;
}

void
tpj_close (struct tpj *device)
{
  free (device->flash);
  if (device->samples)
    fclose (device->samples);
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
tpj_samples (struct tpj *device, const char *path)
{
  FILE *input;
  unsigned char ticks[4];
  long length;
  unsigned delay;
  if (device->samples || !(input = fopen (path, "rb")))
    return 0;
  if (fseek (input, 0, SEEK_END) || (length = ftell (input)) <= 0 ||
      length % (4 + SAMPLE_SIZE) || fseek (input, 0, SEEK_SET))
    goto failure;
  /* Validate the entire finite stream before mutating the endpoint. Only one
     frame remains resident. Each record is LE32 virtual-tick delay followed
     by209 matrix words and11 auxiliary words in source row-major order. */
  while (length)
    {
      if (fread (ticks, 1, 4, input) != 4)
        goto failure;
      delay = (unsigned) ticks[0] | (unsigned) ticks[1] << 8 |
              (unsigned) ticks[2] << 16 | (unsigned) ticks[3] << 24;
      if (!delay || delay > INT_MAX || fseek (input, SAMPLE_SIZE, SEEK_CUR))
        goto failure;
      length -= 4 + SAMPLE_SIZE;
    }
  if (fseek (input, 0, SEEK_SET))
    goto failure;
  device->samples = input;
  if (!next_sample (device))
    {
      device->samples = NULL;
      device->sample_loaded = 0;
      goto failure;
    }
  sample_schedule (device);
  return 1;
failure:
  fclose (input);
  return 0;
}

int
tpj_flash (struct tpj *device, const char *path)
{
  size_t size = 0;
  unsigned char *bytes;
  if (device->flash)
    return 0;
  bytes = read_input (path, &size, FLASH_SIZE);
  if (!bytes || size != FLASH_SIZE)
    {
      free (bytes);
      return 0;
    }
  device->flash = bytes;
  device->flash_size = size;
  flash_checksum (device);
  return 1;
}

int
tpj_update (struct tpj *device, const char *path)
{
  size_t size = 0;
  unsigned char *bytes, *flash;
  if (device->flash)
    return 0;
  bytes = read_input (path, &size, UPDATE_SIZE);
  if (!bytes || size != UPDATE_SIZE)
    {
      free (bytes);
      return 0;
    }
  flash = malloc (FLASH_SIZE);
  if (!flash)
    {
      free (bytes);
      return 0;
    }
  /* Explicit virtual erased snapshot, never a silent partial-file fallback.
     e000..fff8 is reflected in Oca4's 1fd808 constant; the final eight erased
     bytes are an external functional input assumption, not camera evidence. */
  memset (flash, 0xff, FLASH_SIZE);
  memcpy (flash, bytes, UPDATE_SIZE);
  free (bytes);
  device->flash = flash;
  device->flash_size = FLASH_SIZE;
  flash_checksum (device);
  return 1;
}

int
tpj_save (struct tpj *device, const char *path)
{
  FILE *output;
  int complete;
  if (!device->flash || device->flash_event ||
      value (device, 0x41011, 1))
    return 0;
  output = fopen (path, "wb");
  if (!output)
    return 0;
  complete = fwrite (device->flash, 1, device->flash_size, output)
             == device->flash_size;
  return fclose (output) == 0 && complete;
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
          (address < 0x4101c && address + size > 0x41018) ||
          (address < 0x41013 && address + size > 0x41011) ||
          (address <= 0x53001 && address + size > 0x53001) ||
          (address <= 0x80042 && address + size > 0x80042) ||
          (address < 0x8020a && address + size > 0x80208) ||
          (address <= 0x80216 && address + size > 0x80216) ||
          (address <= 0x80050 && address + size > 0x80050) ||
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
  sample_schedule (device);
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
  if (device->i2c_token == token)
    device->i2c_token = 0;
}

static void
command_request (struct tpj *device, int receive)
{
  if (device->command_dispatched)
    return;
  device->command_dispatched = 1;
  store (device, 0x6002c, receive, 1);
  store (device, 0x6001c, 1, 1);
  if (!receive && device->payload_count)
    {
      store (device, 0x60024, device->payload_count, 1);
      store (device, 0x60020, 1, 1);
    }
  event_inputs (device);
}

int
tpj_i2c (struct tpj *device, uint64_t token, unsigned phase,
         uint32_t word, uint64_t time)
{
  if (!token || device->i2c_token || phase < NIKON_I2C_START ||
      phase > NIKON_I2C_NACK ||
      (phase <= NIKON_I2C_WRITE ? word > 0xff : word != 0))
    return 0;
  device->i2c_token = token;
  device->i2c_time = time;
  device->i2c_phase = phase;
  device->i2c_reply = 0;
  device->i2c_reply_ready = 1;
  switch (phase)
    {
    case NIKON_I2C_START:
      device->i2c_selected = (word & 0x7f) == 0x55;
      device->i2c_receive = !!(word & 0x80);
      if (!device->i2c_selected)
        device->i2c_reply = NIKON_I2C_NAK;
      else if (device->i2c_receive)
        {
          if (!device->command_present)
            device->i2c_reply = NIKON_I2C_NAK;
          else
            command_request (device, 1);
        }
      else
        {
          device->command_present = device->command_dispatched = 0;
          device->payload_count = device->reply_cursor = 0;
          device->reply_valid = 0;
        }
      break;
    case NIKON_I2C_WRITE:
      if (!device->i2c_selected || device->i2c_receive)
        device->i2c_reply = NIKON_I2C_NAK;
      else if (!device->command_present)
        {
          device->command_present = 1;
          store (device, 0x60028, word, 1);
        }
      else if (device->payload_count == 0x20)
        device->i2c_reply = NIKON_I2C_NAK;
      else
        store (device, 0x60080 + device->payload_count++, word, 1);
      break;
    case NIKON_I2C_READ:
      if (!device->i2c_selected || !device->i2c_receive ||
          !device->command_present || device->reply_cursor == 0x20)
        device->i2c_reply = NIKON_I2C_NAK;
      else
        device->i2c_reply_ready = 0;
      break;
    case NIKON_I2C_NACK:
      device->i2c_receive = 0;
      break;
    case NIKON_I2C_STOP:
      if (device->i2c_selected && device->command_present &&
          !device->i2c_receive && device->payload_count)
        command_request (device, 0);
      device->i2c_selected = device->i2c_receive = 0;
      break;
    }
  return 1;
}

int
tpj_signal (struct tpj *device, unsigned kind, uint32_t line, int level,
            uint64_t time)
{
  (void) time;
  if (kind == NIKON_PEER_GPIO && line == NIKON_TOUCH_CONTROL)
    {
      /* MAS worker drives this external control high; no TPJ consumer proves
         a reset/power effect, so it never changes PC or fabricates consent. */
      device->control_level = !!level;
      return 1;
    }
  /* Do not number 800xx registers as physical GPIO pads without a binding. */
  block (device, line, 1, "physical GPIO/select input has no proved board binding");
  return 1;
}

void
tpj_flush (struct tpj *device)
{
  while (device->emit && device->notify_count)
    {
      if (!device->emit (device->opaque, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO,
                         0, 0, NIKON_TOUCH_NOTIFY,
                         device->notify_edges[device->notify_head], 0))
        return;
      device->notify_head = (device->notify_head + 1) % sizeof device->notify_edges;
      --device->notify_count;
    }
  if (!device->i2c_token || !device->emit)
    return;
  if (!device->i2c_reply_ready &&
      (device->reply_valid & (1U << device->reply_cursor)))
    {
      device->i2c_reply = value (device, 0x60040 + device->reply_cursor, 1);
      device->i2c_reply_ready = 1;
    }
  if (device->i2c_reply_ready &&
      device->emit (device->opaque, NIKON_WIRE_REPLY, NIKON_PEER_I2C,
                    device->i2c_token, 8, device->i2c_phase,
                    device->i2c_reply, device->i2c_time))
    {
      if (device->i2c_phase == NIKON_I2C_READ &&
          device->i2c_reply != NIKON_I2C_NAK)
        ++device->reply_cursor;
      device->i2c_token = 0;
    }
}
