/*
 * Virtual unit8 confidence-map law, not a physical ASIC implementation.
 * Full profile/RAM validation precedes every visible output DMA.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/gateplane.h"
#include "hw/nikon/gategrid.h"
#include "system/dma.h"

typedef struct PlaneSpan {
    uintptr_t start;
    size_t bytes;
} PlaneSpan;

static uint32_t word(const NikonGatePacket *p, unsigned offset)
{
    return p->value[offset / 4];
}

static void set(NikonGatePacket *p, unsigned offset, uint32_t value)
{
    p->present[offset / 4] = true;
    p->value[offset / 4] = value;
}

static void block(NikonGatePacket *p, unsigned offset, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        set(p, offset + i * 4, 0);
    }
}

static bool equal(const NikonGatePacket *a, const NikonGatePacket *b)
{
    for (unsigned i = 0; i < NIKON_GATE_REGISTERS; i++) {
        if (a->present[i] != b->present[i] ||
            (a->present[i] && a->value[i] != b->value[i])) {
            return false;
        }
    }
    return true;
}

static int32_t signed_bits(uint32_t value, unsigned bits)
{
    uint32_t sign = 1U << (bits - 1);

    return (int32_t)((value & ((sign << 1) - 1)) ^ sign) - sign;
}

static const NikonGateWorkCommand *command(const NikonGatePlaneWork *w,
                                           unsigned offset)
{
    const NikonGateWorkCommand *found = NULL;

    for (size_t i = 0; i < w->command_count; i++) {
        if (w->commands[i].offset == offset) {
            if (found || !w->commands[i].packet) {
                return NULL;
            }
            found = &w->commands[i];
        }
    }
    return found;
}

static bool carrier(const NikonGatePacket *p, unsigned width, unsigned height)
{
    NikonGatePacket e = { 0 };

    block(&e, 4, 1);
    block(&e, 0x10, 15);
    set(&e, 4, 0x5a);
    set(&e, 0x10, 0x10);
    set(&e, 0x14, 0x10000);
    set(&e, 0x18, 0x30004);
    set(&e, 0x20, (width - 1) << 16);
    set(&e, 0x24, (height + 3) << 16);
    set(&e, 0x28, width | (width << 16));
    set(&e, 0x2c, 0x20006);
    set(&e, 0x30, 2);
    set(&e, 0x38, 1);
    /* The four halo rows qualify the profile; they authorize no extra reads. */
    return equal(p, &e);
}

static bool gain(const NikonGatePacket *p, NikonGateGridGain out[3])
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 2);
    block(&e, 0x10, 13);
    set(&e, 0, 0xffff0022);
    set(&e, 4, 0x770);
    if (word(p, 0x1c) & 0xffff0000 || word(p, 0x2c) & ~0xfffU) {
        return false;
    }
    set(&e, 0x18, word(p, 0x18));
    set(&e, 0x1c, word(p, 0x1c));
    set(&e, 0x2c, word(p, 0x2c));
    for (unsigned c = 0; c < 3; c++) {
        uint32_t upper = word(p, 0x30 + 4 * c);

        if (upper > 0x3fff) {
            return false;
        }
        set(&e, 0x30 + 4 * c, upper);
        out[c] = (NikonGateGridGain) {
            .gain = c == 2 ? word(p, 0x1c) :
                    (word(p, 0x18) >> (16 * c)) & 0xffff,
            .shift = (word(p, 0x2c) >> (4 * c)) & 15,
            .upper = upper,
        };
    }
    return equal(p, &e);
}

static bool curves(const NikonGatePacket *p)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 6);
    set(&e, 0, 0x5a0020);
    set(&e, 4, 0xff00ff);
    set(&e, 8, 0xff00ff);
    set(&e, 0xc, 0xff00ff);
    set(&e, 0x10, 1);
    set(&e, 0x20, 0xe4b101c);
    for (unsigned i = 0; i < 6; i++) {
        unsigned base = 0x30 + 0x20 * i;

        block(&e, base, i == 5 ? 4 : 5);
        set(&e, base + 4, 0x3fff3fff);
        set(&e, base + 8, 0x40004000);
        if (i == 1 || i == 2) {
            set(&e, base + 12, (i == 1 ? 1U : 2U) << 29);
        }
        if (i < 5) {
            set(&e, base + 16, i < 2 ? 0xe4b101c : 0x1c);
        }
    }
    return equal(p, &e);
}

static bool matrix(const NikonGatePacket *p, bool second,
                   NikonGateGridMatrix *out)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 1);
    block(&e, 0x20, 14);
    block(&e, 0x60, 4);
    block(&e, 0x80, 6);
    set(&e, 0, second ? 0x200068 : 0x220021);
    for (unsigned r = 0; r < 4; r++) {
        NikonGateGridRow *row = &out->row[r];
        unsigned bits = r == 3 ? 9 : 15;
        uint32_t limit = (1U << bits) - 1;
        uint32_t clamp = word(p, 0x60 + 4 * r);

        for (unsigned c = 0; c < 4; c += 2) {
            unsigned offset = 0x20 + 8 * r + 2 * c;
            uint32_t packed = word(p, offset);

            if (packed & 0x80008000) {
                return false;
            }
            row->coefficient[c] = signed_bits(packed, 15);
            row->coefficient[c + 1] = signed_bits(packed >> 16, 15);
            set(&e, offset, packed);
        }
        if (clamp & ~(limit | (limit << 16))) {
            return false;
        }
        row->lower = signed_bits(clamp >> 16, bits);
        row->upper = r == 3 ? signed_bits(clamp, bits) : clamp & limit;
        if (row->lower > row->upper) {
            return false;
        }
        /* Q12 is the virtual camera law, not a decoded physical shift. */
        row->shift = 12;
        set(&e, 0x60 + 4 * r, clamp);
    }
    set(&e, 0x40, second ? 7 : 1);
    if (second) {
        set(&e, 0x4c, 0xb);
        set(&e, 0x80, 1);
        set(&e, 0x84, 0x11111111);
        set(&e, 0x88, 0x32103112);
        set(&e, 0x8c, 0x16b);
        set(&e, 0x90, 0x076e07ba);
        set(&e, 0x94, 0x011f0000);
    }
    return equal(p, &e);
}

static void window(NikonGatePacket *e, unsigned width, unsigned height)
{
    block(e, 0, 9);
    block(e, 0x40, 7);
    block(e, 0x80, 16);
    set(e, 0x18, 0xff000000);
    set(e, 0x40, (width - 1) << 16);
    set(e, 0x44, (height - 1) << 16);
    set(e, 0x48, width | (width << 16));
    set(e, 0x4c, 0x20002);
    set(e, 0x8c, 0xfff);
    set(e, 0x94, 0xfff);
}

static bool windows(const NikonGateWorkCommand *a,
                    const NikonGateWorkCommand *b, unsigned width,
                    unsigned height)
{
    NikonGatePacket first = { 0 }, last = { 0 };

    window(&first, width, height);
    set(&first, 0, 0x210000);
    set(&first, 4, 0x50000);
    set(&first, 8, 0x10400);
    set(&first, 0x1c, 0x101);
    set(&first, 0xa8, 0xff);
    set(&first, 0xb0, 0xff);
    window(&last, width, height);
    set(&last, 0, 0x40000);
    set(&last, 8, 0x10002);
    return !a->auxiliary[0] && !a->auxiliary[1] &&
           b->auxiliary[0] == 0x74011 && b->auxiliary[1] == 0x4100 &&
           equal(a->packet, &first) && equal(b->packet, &last);
}

static bool filter(const NikonGatePacket *p, unsigned width, unsigned height,
                   NikonGateGridFilter *out)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 21);
    block(&e, 0x64, 4);
    block(&e, 0x80, 9);
    block(&e, 0xc0, 9);
    block(&e, 0x100, 9);
    block(&e, 0x22c, 4);
    set(&e, 0, 0x6912);
    set(&e, 4, 4);
    set(&e, 8, 0x440012);
    set(&e, 0xc, 0x10000);
    set(&e, 0x10, (width - 1) << 16);
    set(&e, 0x14, (height - 1) << 16);
    set(&e, 0x18, width | (width << 16));
    set(&e, 0x1c, 0x20002);
    set(&e, 0x20, 0xfffff084);
    set(&e, 0x24, 0x07770774);
    for (unsigned i = 0x28; i <= 0x3c; i += 4) {
        set(&e, i, 0xfff);
    }
    out->shift = word(p, 0x68);
    if (out->shift > 31 || word(p, 0x6c) != out->shift ||
        word(p, 0x70) != out->shift || word(p, 0xa0) & 0xffff0000) {
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        set(&e, 0x68 + 4 * i, out->shift);
    }
    for (unsigned i = 0; i < 17; i++) {
        out->tap[i] = word(p, 0x80 + (i / 2) * 4) >> ((i % 2) * 16);
    }
    for (unsigned i = 0; i < 9; i++) {
        set(&e, 0x80 + 4 * i, word(p, 0x80 + 4 * i));
    }
    set(&e, 0x22c, 0xc);
    set(&e, 0x234, 1);
    out->lower = 0;
    out->upper = 4095;
    return equal(p, &e);
}

static bool ram(const NikonGatePlaneWork *w, uint64_t address, size_t bytes,
                bool write, GArray *spans)
{
    if (!address || !bytes || address + bytes > 0x100000000ULL) {
        return false;
    }
    while (bytes) {
        hwaddr offset, length = bytes;
        MemoryRegion *mr = address_space_translate(w->as, address, &offset,
            &length, write, MEMTXATTRS_UNSPECIFIED);
        uintptr_t pointer;

        if (!length || !memory_region_is_ram(mr) ||
            !memory_access_is_direct(mr, write, MEMTXATTRS_UNSPECIFIED) ||
            (write && mr->readonly)) {
            return false;
        }
        pointer = (uintptr_t)memory_region_get_ram_ptr(mr);
        if (offset > UINTPTR_MAX - pointer ||
            length > UINTPTR_MAX - pointer - offset) {
            return false;
        }
        PlaneSpan span = { pointer + offset, length };

        g_array_append_val(spans, span);
        address += length;
        bytes -= length;
    }
    return true;
}

static bool overlap(const PlaneSpan *a, const PlaneSpan *b)
{
    return a->start < b->start + b->bytes &&
           b->start < a->start + a->bytes;
}

static gint order(gconstpointer a, gconstpointer b)
{
    const PlaneSpan *x = a, *y = b;

    return (x->start > y->start) - (x->start < y->start);
}

static bool aliases(GArray *writes, GArray *reads)
{
    g_array_sort(writes, order);
    for (unsigned i = 0; i < writes->len; i++) {
        PlaneSpan *a = &g_array_index(writes, PlaneSpan, i);

        if (i && overlap(a, &g_array_index(writes, PlaneSpan, i - 1))) {
            return true;
        }
        for (unsigned j = 0; j < reads->len; j++) {
            if (overlap(a, &g_array_index(reads, PlaneSpan, j))) {
                return true;
            }
        }
    }
    return false;
}

static bool layout(const NikonGatePlaneWork *w, NikonGatePlaneOutput *out)
{
    const uint32_t *s = w->slots[1], *d = w->slots[2];
    unsigned map = (w->config[0x2004 / 4] & 1) ? 0x2300 : 0x2200;
    uint32_t base = w->config[(map + w->phase * 64 + 4) / 4];
    uint32_t dimensions = w->width | (w->height << 16);

    if ((w->parameter_valid & 6) != 6 || s[0] != 0x20121 ||
        d[0] != 0x10621 || s[1] != 0x15500 || d[1] != 0x15500 ||
        s[2] || s[3] || s[7] || d[2] || d[3] || d[6] || d[7] ||
        s[8] != dimensions || d[8] != dimensions ||
        d[9] || d[10] || d[11] || d[12] || s[12] ||
        !d[4] || d[4] > 0x1ffff || d[5] != d[4] ||
        d[4] < (w->width * 12 + 7) / 8) {
        return false;
    }
    for (unsigned c = 0; c < 3; c++) {
        uint64_t address = (uint64_t)base + s[9 + c];

        if (!base || address > UINT32_MAX ||
            address != w->input_address[c] || s[4 + c] != w->input_stride[c] ||
            s[4 + c] < w->width * 2 || s[4 + c] > 0x1ffff ||
            (s[4 + c] & 15)) {
            return false;
        }
    }
    *out = (NikonGatePlaneOutput) {
        .address = w->config[(map + w->phase * 64 + 8) / 4],
        .width = w->width, .height = w->height, .stride = d[4],
    };
    return out->address != 0;
}

NikonGateWorkResult nikon_gate_plane_execute(const NikonGatePlaneWork *w,
                                             NikonGatePlaneOutput *output)
{
    static const unsigned ports[] = {
        0x800, 0xd00, 0xa40, 0xa00, 0xa20, 0xdc0, 0x880, 0x9c0,
    };
    const NikonGateWorkCommand *c[ARRAY_SIZE(ports)];
    NikonGateGridGain gains[3];
    NikonGateGridMatrix first = { 0 }, second = { 0 };
    NikonGateGridFilter spatial = { 0 };
    NikonGatePlaneOutput out;
    g_autoptr(GArray) reads = g_array_new(false, false, sizeof(PlaneSpan));
    g_autoptr(GArray) writes = g_array_new(false, false, sizeof(PlaneSpan));
    g_autofree uint16_t *colors = NULL;
    g_autofree int32_t *input = NULL, *features = NULL, *projected = NULL;
    g_autofree int32_t *filtered = NULL, *blended = NULL;
    g_autofree uint8_t *packed = NULL;
    size_t samples, row_bytes;

    if (!w || !output || !w->as || !w->config || !w->slots || !w->commands ||
        !w->table || !w->rgb || !w->width || !w->height || w->phase > 3 ||
        w->width > 4096 || w->height > 4096 ||
        w->config[0x2000 / 4] != 0x131 ||
        (w->config[0x2004 / 4] & ~0x31U) != 0xc2 ||
        w->config[0x2008 / 4] || w->config[0x2010 / 4] != 0x74011 ||
        w->config[0x2014 / 4] != 0x4100 || w->config[0x2018 / 4] != 7 ||
        w->config[0x2640 / 4] != 0x202 ||
        w->config[0x2644 / 4] != w->height ||
        w->config[0x2648 / 4] != 3 || w->config[0x264c / 4] != 2 ||
        w->config[0x2650 / 4] || w->config[0x2658 / 4] != 3 ||
        w->config[0x4040 / 4] != 0x10 ||
        w->config[0x4044 / 4] != (w->width | (w->height << 16))) {
        return NIKON_GATE_WORK_CONFIG;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(c); i++) {
        c[i] = command(w, ports[i]);
        if (!c[i]) {
            return NIKON_GATE_WORK_TOPOLOGY;
        }
        if (i != 7 && (c[i]->auxiliary[0] || c[i]->auxiliary[1])) {
            return NIKON_GATE_WORK_GRID;
        }
    }
    if (!layout(w, &out)) {
        return NIKON_GATE_WORK_FORMAT;
    }
    if (!carrier(c[0]->packet, w->width, w->height) ||
        !gain(c[1]->packet, gains) || !curves(c[2]->packet) ||
        !matrix(c[3]->packet, false, &first) ||
        !matrix(c[4]->packet, true, &second) ||
        !windows(c[5], c[7], w->width, w->height) ||
        !filter(c[6]->packet, w->width, w->height, &spatial)) {
        return NIKON_GATE_WORK_GRID;
    }
    for (unsigned bank = 0; bank < 6; bank++) {
        for (unsigned i = 0; i < 1024; i++) {
            if (w->table[bank][i] > 0x3fff) {
                return NIKON_GATE_WORK_GRID;
            }
        }
    }
    samples = (size_t)w->width * w->height;
    row_bytes = (w->width * 12 + 7) / 8;
    for (unsigned channel = 0; channel < 3; channel++) {
        for (unsigned y = 0; y < w->height; y++) {
            if (!ram(w, w->input_address[channel] +
                     (uint64_t)y * w->input_stride[channel],
                     w->width * 2, false, reads)) {
                return NIKON_GATE_WORK_MEMORY;
            }
        }
    }
    for (unsigned y = 0; y < w->height; y++) {
        if (!ram(w, out.address + (uint64_t)y * out.stride,
                 row_bytes, true, writes)) {
            return NIKON_GATE_WORK_MEMORY;
        }
    }
    PlaneSpan private_input = { (uintptr_t)w->rgb,
                                samples * 3 * sizeof(*w->rgb) };

    if (private_input.bytes > UINTPTR_MAX - private_input.start) {
        return NIKON_GATE_WORK_ALIAS;
    }
    g_array_append_val(reads, private_input);
    if (aliases(writes, reads)) {
        return NIKON_GATE_WORK_ALIAS;
    }
    colors = g_new(uint16_t, samples * 3);
    input = g_new(int32_t, samples * 3);
    features = g_new(int32_t, samples * 4);
    projected = g_new(int32_t, samples);
    filtered = g_new(int32_t, samples);
    blended = g_new(int32_t, samples);
    packed = g_malloc0(row_bytes * w->height);
    if (!nikon_gate_grid_gain3(gains, w->rgb, samples, colors, samples * 3) ||
        !nikon_gate_grid_lut3(w->table, colors, samples, colors, samples * 3)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    for (size_t i = 0; i < samples * 3; i++) {
        input[i] = colors[i];
    }
    if (!nikon_gate_grid_matrix4(&first, input, samples, features,
                                 samples * 4)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    /* Canonical virtual RGB bus; no physical luma/chroma order is asserted. */
    memcpy(input, features, samples * 3 * sizeof(*input));
    if (!nikon_gate_grid_matrix4(&second, input, samples, features,
                                 samples * 4)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    for (size_t i = 0; i < samples; i++) {
        uint64_t sum = 0;

        for (unsigned r = 0; r < 4; r++) {
            int64_t value = features[r * samples + i];
            const NikonGateGridRow *row = &second.row[r];
            int64_t norm = MAX(llabs((int64_t)row->lower),
                               llabs((int64_t)row->upper));

            sum += norm ? MIN((llabs(value) * 4095 + norm / 2) / norm,
                              4095) : 0;
        }
        projected[i] = (sum + 2) / 4;
    }
    if (!nikon_gate_grid_filter(&spatial, projected, 1, w->width, w->height,
                                filtered, samples)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    for (size_t i = 0; i < samples; i++) {
        blended[i] = (projected[i] + filtered[i] + 1) / 2;
    }
    if (!nikon_gate_grid_filter(&spatial, blended, 1, w->width, w->height,
                                filtered, samples)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    for (unsigned y = 0; y < w->height; y++) {
        uint8_t *row = packed + y * row_bytes;

        for (unsigned x = 0; x < w->width; x++) {
            unsigned value = filtered[(size_t)y * w->width + x];
            unsigned byte = (x / 2) * 3;

            if (x & 1) {
                row[byte + 1] |= (value & 15) << 4;
                row[byte + 2] = value >> 4;
            } else {
                row[byte] = value;
                row[byte + 1] = value >> 8;
            }
        }
    }
    if (w->begin && !w->begin(w->opaque, &out)) {
        return NIKON_GATE_WORK_CONFIG;
    }
    /* Caller keeps direct-RAM topology stable throughout the call. */
    for (unsigned y = 0; y < w->height; y++) {
        MemTxResult tx = dma_memory_write(w->as,
            out.address + (uint64_t)y * out.stride, packed + y * row_bytes,
            row_bytes, MEMTXATTRS_UNSPECIFIED);

        g_assert(tx == MEMTX_OK);
    }
    *output = out;
    return NIKON_GATE_WORK_OK;
}
