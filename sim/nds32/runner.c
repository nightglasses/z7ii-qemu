/* Native TPJ process: mature GNU execution and physical peer envelopes.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "bfd.h"
#include "gdb/callback.h"
#include "system.h"
#include "sim-core.h"
#include "tpj.h"

#define ROM_SIZE 0xe000U
#define RAM_BASE 0x20000U
#define RAM_SIZE 0x4000U
#define OUTPUT_LIMIT (64U * NIKON_WIRE_SIZE)

struct transport {
  int fd;
  struct tpj *device;
  unsigned char input[NIKON_WIRE_SIZE];
  unsigned input_length;
  unsigned char output[OUTPUT_LIMIT];
  unsigned output_start;
  unsigned output_length;
  uint64_t pending;
  unsigned pending_bits;
  unsigned pending_kind;
  uint32_t pending_line;
};

struct image_layout {
  unsigned char coverage[ROM_SIZE];
  unsigned bytes;
  int valid;
};

static volatile sig_atomic_t interrupted;

static void
interrupt (int signal_number)
{
  interrupted = 1;
}

static void
image_section (bfd *image, asection *section, void *opaque)
{
  struct image_layout *layout = opaque;
  flagword flags = bfd_get_section_flags (image, section);
  bfd_vma address = bfd_section_vma (image, section);
  bfd_size_type size = bfd_section_size (image, section);
  unsigned i;
  if (!(flags & SEC_ALLOC) || !size)
    return;
  if (!(flags & SEC_LOAD) || !(flags & SEC_HAS_CONTENTS) ||
      address != bfd_section_lma (image, section) || address >= ROM_SIZE ||
      size > ROM_SIZE - address)
    {
      layout->valid = 0;
      return;
    }
  for (i = 0; i < size; ++i)
    {
      if (layout->coverage[address + i])
        layout->valid = 0;
      else
        {
          layout->coverage[address + i] = 1;
          ++layout->bytes;
        }
    }
}

static bfd *
open_image (const char *path)
{
  struct image_layout layout = { { 0 }, 0, 1 };
  bfd *image = bfd_openr (path, NULL);
  if (!image)
    return NULL;
  if (!bfd_check_format (image, bfd_object) ||
      bfd_get_flavour (image) != bfd_target_elf_flavour ||
      bfd_get_arch (image) != bfd_arch_nds32 ||
      !bfd_little_endian (image) ||
      bfd_get_start_address (image) != 0)
    {
      bfd_close (image);
      return NULL;
    }
  bfd_map_over_sections (image, image_section, &layout);
  if (!layout.valid || layout.bytes != ROM_SIZE)
    {
      fprintf (stderr, "TPJ ELF must cover every byte of [0,e000) once; "
                       "RAM COPY/ZERO belongs to the guest.\n");
      bfd_close (image);
      return NULL;
    }
  return image;
}

static int
connect_peer (const char *path)
{
  struct sockaddr_un address;
  int fd, flags, result, error;
  socklen_t error_size = sizeof error;
  struct pollfd wait;
  if (strlen (path) >= sizeof address.sun_path)
    {
      errno = ENAMETOOLONG;
      return -1;
    }
  memset (&address, 0, sizeof address);
  address.sun_family = AF_UNIX;
  memcpy (address.sun_path, path, strlen (path) + 1);
  fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  flags = fcntl (fd, F_GETFL, 0);
  if (flags < 0 || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
      fcntl (fd, F_SETFD, FD_CLOEXEC) < 0)
    goto failure;
  result = connect (fd, (struct sockaddr *) &address,
                    offsetof (struct sockaddr_un, sun_path) + strlen (path) + 1);
  if (result < 0 && errno != EINPROGRESS)
    goto failure;
  if (result < 0)
    {
      wait.fd = fd;
      wait.events = POLLOUT;
      do
        result = poll (&wait, 1, 1000);
      while (result < 0 && errno == EINTR && !interrupted);
      if (result != 1 || interrupted)
        {
          errno = result == 0 ? ETIMEDOUT : EINTR;
          goto failure;
        }
      if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0)
        goto failure;
      if (error)
        {
          errno = error;
          goto failure;
        }
    }
  return fd;
failure:
  error = errno;
  close (fd);
  errno = error;
  return -1;
}

static int
emit (void *opaque, unsigned type, unsigned kind, uint64_t token,
      unsigned bits, uint32_t line, uint32_t word, uint64_t time)
{
  struct transport *transport = opaque;
  unsigned char *frame;
  if (type == NIKON_WIRE_REPLY &&
      (!token || token != transport->pending || bits != transport->pending_bits ||
       kind != transport->pending_kind || line != transport->pending_line))
    return 0;
  if (transport->output_start &&
      transport->output_start + transport->output_length + NIKON_WIRE_SIZE >
      OUTPUT_LIMIT)
    {
      memmove (transport->output, transport->output + transport->output_start,
               transport->output_length);
      transport->output_start = 0;
    }
  if (transport->output_length + NIKON_WIRE_SIZE > OUTPUT_LIMIT)
    return 0;
  frame = transport->output + transport->output_start + transport->output_length;
  memset (frame, 0, NIKON_WIRE_SIZE);
  bfd_putl32 (NIKON_WIRE_MAGIC, frame);
  frame[NIKON_WIRE_TYPE] = type;
  frame[NIKON_WIRE_KIND] = kind;
  frame[NIKON_WIRE_BITS] = bits;
  bfd_putl64 (token, frame + NIKON_WIRE_TOKEN);
  bfd_putl64 (time, frame + NIKON_WIRE_TIME);
  bfd_putl32 (line, frame + NIKON_WIRE_LINE);
  bfd_putl32 (word, frame + NIKON_WIRE_WORD);
  transport->output_length += NIKON_WIRE_SIZE;
  if (type == NIKON_WIRE_REPLY)
    transport->pending = 0;
  return 1;
}

static int
receive_frame (struct transport *transport)
{
  const unsigned char *frame = transport->input;
  unsigned type = frame[NIKON_WIRE_TYPE], kind = frame[NIKON_WIRE_KIND];
  unsigned bits = frame[NIKON_WIRE_BITS];
  uint64_t token = bfd_getl64 (frame + NIKON_WIRE_TOKEN);
  uint64_t time = bfd_getl64 (frame + NIKON_WIRE_TIME);
  uint32_t line = bfd_getl32 (frame + NIKON_WIRE_LINE);
  uint32_t word = bfd_getl32 (frame + NIKON_WIRE_WORD);
  if (bfd_getl32 (frame) != NIKON_WIRE_MAGIC ||
      frame[NIKON_WIRE_RESERVED] || bfd_getl64 (frame + NIKON_WIRE_PADDING))
    return 0;
  if (type == NIKON_WIRE_SIGNAL)
    return !token && !bits && word <= 1 &&
           (kind == NIKON_PEER_SELECT || kind == NIKON_PEER_GPIO) &&
           tpj_signal (transport->device, kind, line, word, time);
  if (kind == NIKON_PEER_I2C)
    {
      if (!token || bits != 8 || line < NIKON_I2C_START ||
          line > NIKON_I2C_NACK ||
          (type == NIKON_WIRE_CANCEL ? word != 0 :
           (line <= NIKON_I2C_WRITE ? word > 0xff : word != 0)))
        return 0;
    }
  else if (kind != NIKON_PEER_CLOCK || !token || !bits || bits > 32 || line)
    return 0;
  if (type == NIKON_WIRE_CANCEL)
    {
      if (word)
        return 0;
      if (transport->pending == token)
        {
          if (kind != transport->pending_kind || bits != transport->pending_bits ||
              line != transport->pending_line)
            return 0;
          transport->pending = 0;
          tpj_cancel (transport->device, token);
        }
      return 1;
    }
  if (type != NIKON_WIRE_REQUEST || transport->pending)
    return 0;
  transport->pending = token;
  transport->pending_bits = bits;
  transport->pending_kind = kind;
  transport->pending_line = line;
  if (kind == NIKON_PEER_I2C)
    return tpj_i2c (transport->device, token, line, word, time);
  return tpj_clock (transport->device, token, word, bits, time);
}

static int
transport_poll (struct transport *transport)
{
  struct pollfd wait = { transport->fd, POLLIN, 0 };
  int result;
  if (transport->fd < 0)
    return 1;
  if (transport->output_length)
    wait.events |= POLLOUT;
  do
    result = poll (&wait, 1, 0);
  while (result < 0 && errno == EINTR && !interrupted);
  if (result < 0)
    return interrupted;
  /* Consume readable bytes before HUP so a final complete frame is handled. */
  if (wait.revents & POLLIN)
    {
      ssize_t count = read (transport->fd,
                            transport->input + transport->input_length,
                            NIKON_WIRE_SIZE - transport->input_length);
      if (count == 0)
        return 0;
      if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        return 0;
      if (count > 0)
        {
          transport->input_length += count;
          if (transport->input_length == NIKON_WIRE_SIZE)
            {
              transport->input_length = 0;
              if (!receive_frame (transport))
                return 0;
            }
        }
    }
  if (wait.revents & POLLOUT)
    {
      ssize_t count = write (transport->fd,
                             transport->output + transport->output_start,
                             transport->output_length);
      if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        return 0;
      if (count > 0)
        {
          transport->output_start += count;
          transport->output_length -= count;
          if (!transport->output_length)
            transport->output_start = 0;
        }
    }
  return !(wait.revents & (POLLERR | POLLHUP | POLLNVAL));
}

static void
usage (const char *name)
{
  fprintf (stderr, "Usage: %s --image whole.elf [--socket path] "
                   "[--ticks 4096] [--limit ticks] [--consent 0|1] "
                   "[--flash fullnor | --update payload] [--save fullnor] "
                   "[--snapshot fields] [--transform sample] [--samples scans]\n"
                   "Ticks are GNU instruction/event slices, not a CPU frequency.\n",
                   name);
}

int
main (int argc, char **argv)
{
  static const struct option options[] = {
    { "image", required_argument, NULL, 'i' },
    { "socket", required_argument, NULL, 's' },
    { "ticks", required_argument, NULL, 't' },
    { "consent", required_argument, NULL, 'c' },
    { "limit", required_argument, NULL, 'l' },
    { "flash", required_argument, NULL, 'f' },
    { "update", required_argument, NULL, 'u' },
    { "save", required_argument, NULL, 'o' },
    { "snapshot", required_argument, NULL, 'p' },
    { "transform", required_argument, NULL, 'x' },
    { "samples", required_argument, NULL, 'm' },
    { NULL, 0, NULL, 0 }
  };
  char *sim_argv[] = { "tpj", "--nds32-system", "--endian=little", NULL };
  const char *image_path = NULL, *socket_path = NULL;
  const char *flash_path = NULL, *snapshot_path = NULL, *transform_path = NULL;
  const char *update_path = NULL, *save_path = NULL;
  const char *samples_path = NULL;
  uint64_t ticks = 4096;
  uint64_t limit = 0, executed = 0;
  int option, consent = 0, status = 1;
  host_callback callback = default_callback;
  SIM_DESC sd = NULL;
  bfd *image = NULL;
  unsigned char *rom = NULL, *ram = NULL;
  struct transport transport = { .fd = -1 };
  while ((option = getopt_long (argc, argv, "", options, NULL)) != -1)
    {
      char *end;
      switch (option)
        {
        case 'i': image_path = optarg; break;
        case 's': socket_path = optarg; break;
        case 'f': flash_path = optarg; break;
        case 'u': update_path = optarg; break;
        case 'o': save_path = optarg; break;
        case 'p': snapshot_path = optarg; break;
        case 'x': transform_path = optarg; break;
        case 'm': samples_path = optarg; break;
        case 'l':
          errno = 0;
          limit = strtoull (optarg, &end, 0);
          if (errno || end == optarg || *end || !limit || optarg[0] == '-')
            goto bad_arguments;
          break;
        case 't':
          errno = 0;
          ticks = strtoull (optarg, &end, 0);
          if (errno || end == optarg || *end || ticks == 0 || ticks > 0x7fffffffU)
            goto bad_arguments;
          break;
        case 'c':
          if (strcmp (optarg, "0") && strcmp (optarg, "1"))
            goto bad_arguments;
          consent = optarg[0] == '1';
          break;
        default: goto bad_arguments;
        }
    }
  if (!image_path || optind != argc || (flash_path && update_path) ||
      (save_path && !flash_path && !update_path))
    goto bad_arguments;
  signal (SIGINT, interrupt);
  signal (SIGTERM, interrupt);
  signal (SIGPIPE, SIG_IGN);
  bfd_init ();
  image = open_image (image_path);
  if (!image)
    {
      fprintf (stderr, "Cannot load complete TPJ ELF: %s\n", image_path);
      goto cleanup;
    }
  callback.init (&callback);
  sd = sim_open (SIM_OPEN_DEBUG, &callback, image, sim_argv);
  if (!sd)
    goto callback_cleanup;
  rom = calloc (1, ROM_SIZE);
  ram = calloc (1, RAM_SIZE);
  if (!rom || !ram)
    goto cleanup_sim;
  sim_core_attach (sd, NULL, 0, access_read_write_exec, 0, 0,
                   ROM_SIZE, 0, NULL, rom);
  sim_core_attach (sd, NULL, 0, access_read_write_exec, 0, RAM_BASE,
                   RAM_SIZE, 0, NULL, ram);
  if (sim_load (sd, image_path, image, 0) != SIM_RC_OK ||
      sim_create_inferior (sd, image, NULL, NULL) != SIM_RC_OK)
    goto cleanup_sim;
  sim_core_detach (sd, NULL, 0, 0, 0);
  sim_core_attach (sd, NULL, 0, access_read | access_exec, 0, 0,
                   ROM_SIZE, 0, NULL, rom);
  transport.device = tpj_open (sd, emit, &transport);
  if (!transport.device || nds32_system_consent (sd, consent) != SIM_RC_OK)
    goto cleanup_sim;
  if (tpj_boundary (transport.device))
    goto cleanup_sim;
  if ((flash_path && !tpj_flash (transport.device, flash_path)) ||
      (update_path && !tpj_update (transport.device, update_path)) ||
      (snapshot_path && !tpj_snapshot (transport.device, snapshot_path)) ||
      (transform_path && !tpj_transform (transport.device, transform_path)) ||
      (samples_path && !tpj_samples (transport.device, samples_path)))
    {
      fprintf (stderr, "Invalid or inaccessible TPJ external input.\n");
      goto cleanup_sim;
    }
  if (socket_path)
    {
      transport.fd = connect_peer (socket_path);
      if (transport.fd < 0)
        {
          fprintf (stderr, "Cannot connect peer %s: %s\n", socket_path, strerror (errno));
          goto cleanup_sim;
        }
    }
  while (!interrupted)
    {
      enum sim_stop reason;
      int sigrc;
      uint64_t quantum = limit && limit - executed < ticks ? limit - executed : ticks;
      if (!transport_poll (&transport))
        {
          fprintf (stderr, "TPJ peer disconnected or invalid frame; no completion.\n");
          goto cleanup_sim;
        }
      tpj_flush (transport.device);
      if (tpj_boundary (transport.device) ||
          nds32_system_run (sd, quantum) != SIM_RC_OK)
        goto cleanup_sim;
      executed += quantum;
      if (tpj_boundary (transport.device))
        goto cleanup_sim;
      sim_stop_reason (sd, &reason, &sigrc);
      if (reason != sim_stopped || sigrc)
        {
          fprintf (stderr, "TPJ execution stopped reason=%d signal=%d pc=%08x\n",
                           reason, sigrc, CPU_PC_GET (STATE_CPU (sd, 0)));
          goto cleanup_sim;
        }
      if (limit && executed == limit)
        {
          fprintf (stderr, "TPJ finite budget=%llu pc=%08x sleep=%u\n",
                           (unsigned long long) executed,
                           CPU_PC_GET (STATE_CPU (sd, 0)),
                           STATE_CPU (sd, 0)->system_sleep);
          status = 0;
          goto cleanup_sim;
        }
    }
  status = 0;
cleanup_sim:
  if (transport.device)
    {
      if (transport.pending)
        tpj_cancel (transport.device, transport.pending);
      if (save_path && !tpj_save (transport.device, save_path))
        {
          fprintf (stderr, "Cannot save complete idle TPJ NOR: %s\n", save_path);
          status = 1;
        }
      tpj_quiesce (transport.device);
    }
  if (transport.fd >= 0)
    close (transport.fd);
  sim_close (sd, 0);
  if (transport.device)
    tpj_close (transport.device);
  free (rom);
  free (ram);
callback_cleanup:
  callback.shutdown (&callback);
cleanup:
  if (image)
    bfd_close (image);
  return status;
bad_arguments:
  usage (argv[0]);
  return 2;
}
