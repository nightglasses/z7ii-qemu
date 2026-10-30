/*
 * One bounded continuous graph. Coordinate rounding, chroma sampling and
 * luma-parity measurements are functional hypotheses, not physical DSP
 * recovery.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/gatework.h"
#include "hw/nikon/gatepixels.h"
#include "hw/nikon/compute_pipeline.h"
#include "system/dma.h"

typedef struct GateWorkSpan {
    uintptr_t start;
    size_t bytes;
} GateWorkSpan;

typedef struct GateWorkOutput {
    uint32_t address[2], pitch[2];
    unsigned width, height, uv_height;
    int32_t x, y, sx, sy;
    uint8_t *pixels;
} GateWorkOutput;

static uint32_t word(const NikonGatePacket *p, unsigned offset)
{
    return p->value[offset / 4];
}

static const NikonGateWorkCommand *command(const NikonGateWork *w,
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

static void expect(NikonGatePacket *p, unsigned offset, uint32_t value)
{
    p->present[offset / 4] = true;
    p->value[offset / 4] = value;
}

static void block(NikonGatePacket *p, unsigned offset, unsigned words)
{
    for (unsigned i = 0; i < words; i++) {
        expect(p, offset + 4 * i, 0);
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

static bool neutral_carrier(const NikonGatePacket *p)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 3);
    expect(&e, 0, 8);
    expect(&e, 4, 0x30);
    expect(&e, 8, 0x202);
    block(&e, 0x200, 80);
    expect(&e, 0x400, 0x31);
    expect(&e, 0x404, 8);
    expect(&e, 0x408, 0xfe);
    return equal(p, &e);
}

static bool monitor_profile(const NikonGatePacket *p)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 2);
    block(&e, 0x10, 6);
    block(&e, 0x40, 1);
    block(&e, 0x60, 8);
    expect(&e, 0x10, 0x20200);
    expect(&e, 0x14, 0x20201);
    expect(&e, 0x18, 0x20202);
    expect(&e, 0x1c, 0x203);
    return equal(p, &e);
}

static bool input_shape(const NikonGateWork *w)
{
    const uint32_t *s = w->slots[8];

    return (w->parameter_valid & (1U << 8)) && s[0] == 0x9000201 &&
           s[1] == 0x15500 && !s[2] && !s[3] &&
           s[4] >= w->width && s[4] <= 0x1ffff && !(s[4] & 63) &&
           s[5] == s[4] && !s[6] && !s[7] &&
           s[8] == (w->width | (w->height << 16)) && !s[9] &&
           s[10] == 0x8000 && !s[11] && !s[12];
}

static void window(NikonGatePacket *e, bool terminal)
{
    block(e, 0, 9);
    block(e, 0x40, 7);
    block(e, 0x80, 16);
    expect(e, 0x18, terminal ? 0xff010000 : 0xff000000);
    expect(e, 0x80, 6);
    expect(e, 0x8c, 0xff);
    expect(e, 0x94, 0xff);
    expect(e, 0xa8, 0xff);
    expect(e, 0xb0, 0xff);
}

static bool intermediate(const NikonGatePacket *p, unsigned *width,
                         unsigned *height)
{
    NikonGatePacket e = { 0 };
    uint32_t x = word(p, 0x40), y = word(p, 0x44);

    if ((x | y) & 0xffff) {
        return false;
    }
    *width = (x >> 16) + 1;
    *height = (y >> 16) + 1;
    if ((*width | *height) & 1 || *width > 4096 || *height > 4096) {
        return false;
    }
    window(&e, false);
    expect(&e, 0, 0x90000);
    expect(&e, 4, 0xff0000);
    expect(&e, 8, 0x10500);
    expect(&e, 0x40, x);
    expect(&e, 0x44, y);
    expect(&e, 0x48, 0x400040);
    expect(&e, 0x4c, 0x80008);
    return equal(p, &e);
}

static bool grid(const NikonGatePacket *p, unsigned width, unsigned height,
                 uint32_t *address, size_t *bytes, NikonGatePixels *pixels)
{
    NikonGatePacket e = { 0 };
    unsigned columns = word(p, 0x50) & 0xffff;
    unsigned rows = word(p, 0x50) >> 16;

    if (columns != DIV_ROUND_UP(width, 64) || !rows || rows > 512 ||
        rows * 8 != height || !word(p, 0x54)) {
        return false;
    }
    block(&e, 0, 33);
    expect(&e, 0, 0x6a);
    for (unsigned i = 4; i <= 0xc; i += 4) {
        expect(&e, i, 0xff);
    }
    expect(&e, 0x10, 0x30);
    expect(&e, 0x14, 0x10000000);
    expect(&e, 0x18, 0x4080);
    expect(&e, 0x1c, 0x120);
    expect(&e, 0x28, 1);
    for (unsigned i = 0x30; i <= 0x3c; i += 4) {
        expect(&e, i, 0x100000);
    }
    for (unsigned i = 0x40; i <= 0x4c; i += 4) {
        expect(&e, i, 0xff);
    }
    expect(&e, 0x50, word(p, 0x50));
    expect(&e, 0x54, word(p, 0x54));
    expect(&e, 0x64, 4);
    expect(&e, 0x68, 0x40302);
    expect(&e, 0x78, 0x31);
    expect(&e, 0x7c, 0x3000);
    expect(&e, 0x80, 0x10);
    if (!equal(p, &e)) {
        return false;
    }
    *address = word(p, 0x54);
    *bytes = (size_t)columns * rows * 40;
    pixels->columns = columns;
    pixels->rows = rows;
    pixels->dst_width = width;
    pixels->dst_height = height;
    return true;
}

/*
 * A translated direct-RAM interval identifies backing, including bus aliases.
 * All intervals are checked before DMA or numerical output becomes visible.
 */
static bool ram(const NikonGateWork *w, uint64_t address, size_t bytes,
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
        GateWorkSpan span = { pointer + offset, length };

        g_array_append_val(spans, span);
        address += length;
        bytes -= length;
    }
    return true;
}

static gint span_order(gconstpointer a, gconstpointer b)
{
    const GateWorkSpan *x = a, *y = b;

    return (x->start > y->start) - (x->start < y->start);
}

static bool overlaps(const GateWorkSpan *a, const GateWorkSpan *b)
{
    return a->start < b->start + b->bytes &&
           b->start < a->start + a->bytes;
}

static bool aliases(GArray *writes, GArray *reads)
{
    g_array_sort(writes, span_order);
    for (unsigned i = 0; i < writes->len; i++) {
        GateWorkSpan *a = &g_array_index(writes, GateWorkSpan, i);

        if (i && overlaps(a, &g_array_index(writes, GateWorkSpan, i - 1))) {
            return true;
        }
        for (unsigned j = 0; j < reads->len; j++) {
            if (overlaps(a, &g_array_index(reads, GateWorkSpan, j))) {
                return true;
            }
        }
    }
    return false;
}

static int32_t signed_bits(uint32_t value, unsigned bits)
{
    uint32_t sign = 1U << (bits - 1);

    return (int32_t)((value & ((sign << 1) - 1)) ^ sign) - sign;
}

static bool affine_packet(const NikonGatePacket *p, unsigned branch,
                          uint32_t *address)
{
    NikonGatePacket e = { 0 };

    block(&e, 0, 27);
    block(&e, 0x78, 3);
    expect(&e, 0, branch == 1 ? 0xa : 0xff);
    expect(&e, 4, branch == 0 ? 0x14 : branch == 2 ? 0x16 : 0xff);
    expect(&e, 8, 0xff);
    expect(&e, 0xc, 0xff);
    expect(&e, 0x10, branch == 2 ? 0x10 : 0x13);
    expect(&e, 0x14, 0x10000000);
    expect(&e, 0x18, 0x5050);
    expect(&e, 0x1c, 0x300);
    expect(&e, 0x20, branch == 0 ? 0x10 : 0);
    expect(&e, 0x40, 0xff);
    expect(&e, 0x44, 0xff);
    expect(&e, 0x48, 0xff);
    expect(&e, 0x50, 0x10001);
    expect(&e, 0x54, word(p, 0x54));
    expect(&e, 0x64, 0xd);
    expect(&e, 0x78, 1);
    *address = word(p, 0x54);
    return *address && equal(p, &e);
}

static bool output(const NikonGateWork *w, const NikonGateWorkCommand *c,
                   unsigned branch, GateWorkOutput *o, unsigned *window_w,
                   unsigned *window_h)
{
    static const unsigned route[] = { 0x40, 7, 0x46 };
    NikonGatePacket e = { 0 };
    const NikonGatePacket *p = c->packet;
    unsigned slot = word(p, 8) & 15;
    const uint32_t *s = w->slots[slot];
    uint32_t xy = word(p, 0x40), yy = word(p, 0x44);
    uint64_t base, y, uv;

    if ((xy | yy) & 0xffff || !(w->parameter_valid & (1U << slot)) ||
        c->auxiliary[0] != 0x200087e0 || c->auxiliary[1] != 0x88000) {
        return false;
    }
    window(&e, true);
    expect(&e, 0, route[branch] << 16);
    expect(&e, 8, 0x10000 | slot);
    expect(&e, 0x40, xy);
    expect(&e, 0x44, yy);
    expect(&e, 0x48, branch == 2 ? 0x400040 : 0x1000100);
    expect(&e, 0x4c, 0x20002);
    if (!equal(p, &e)) {
        return false;
    }
    *window_w = (xy >> 16) + 1;
    *window_h = (yy >> 16) + 1;
    o->width = s[8] & 0xffff;
    o->height = s[8] >> 16;
    if (s[0] != 0x9000201 || s[1] != 0x15500 || s[2] || s[3] ||
        s[6] || s[7] || s[9] || s[11] || s[12] ||
        !o->width || !o->height || ((o->width | o->height) & 1) ||
        o->width > 4096 || o->height > 4096 ||
        *window_w != o->width || *window_h < o->height ||
        *window_h > 4096 || s[4] < o->width || s[5] < o->width ||
        s[4] > 0x1ffff || s[5] > 0x1ffff || (s[4] | s[5]) & 63) {
        return false;
    }
    o->pitch[0] = s[4];
    o->pitch[1] = s[5];
    /* Format selection is restricted further by the branch's control tuple. */
    o->uv_height = o->height / 2;
    base = w->config[(0x2200 + w->phase * 64 + slot * 4) / 4];
    y = base;
    uv = base + s[10];
    if (!base || y > UINT32_MAX || uv > UINT32_MAX) {
        return false;
    }
    o->address[0] = y;
    o->address[1] = uv;
    return true;
}

static bool affine(const uint8_t data[32], unsigned width, unsigned height,
                    unsigned branch, GateWorkOutput *o)
{
    uint32_t x = ldl_le_p(data), y = ldl_le_p(data + 4);
    uint32_t sx = ldl_le_p(data + 8), sy = ldl_le_p(data + 20);

    if (ldl_le_p(data + 12) || ldl_le_p(data + 16) ||
        ldl_le_p(data + 28) || (sx | sy) & 0xff000000 ||
        ldl_le_p(data + 24) != (width | (height << 16)) ||
        (x >> 31) != (branch == 0) || (y >> 31) != (branch == 0)) {
        return false;
    }
    o->x = signed_bits(x, 31);
    o->y = signed_bits(y, 31);
    o->sx = signed_bits(sx, 24);
    o->sy = signed_bits(sy, 24);
    return o->sx > 0 && o->sy > 0;
}

static unsigned sample_index(int64_t coordinate, unsigned extent,
                              unsigned fractional)
{
    int64_t half = INT64_C(1) << (fractional - 1);

    /* Nearest, half-up, replicate edges. This is an explicit finite rule. */
    coordinate = CLAMP(coordinate, 0, ((int64_t)extent - 1) << fractional);
    return (coordinate + half) >> fractional;
}

static void render(const uint8_t *input, unsigned width, unsigned height,
                    GateWorkOutput *o)
{
    size_t ybytes = (size_t)o->width * o->height;
    uint16_t xindex[4096]; /* Output width is validated before worker admission. */

    /* Each affine coordinate is separable; every row uses the same X mapping. */
    for (unsigned x = 0; x < o->width; x++) {
        xindex[x] = sample_index(o->x + (int64_t)x * o->sx, width, 16);
    }
    for (unsigned y = 0; y < o->height; y++) {
        unsigned iy = sample_index(o->y + (int64_t)y * o->sy, height, 16);
        const uint8_t *row = input + (size_t)iy * width;

        for (unsigned x = 0; x < o->width; x++) {
            o->pixels[(size_t)y * o->width + x] = row[xindex[x]];
        }
    }
    for (unsigned x = 0; x < o->width; x += 2) {
        xindex[x / 2] = sample_index(o->x + (int64_t)x * o->sx, width / 2, 17);
    }
    for (unsigned y = 0; y < o->uv_height; y++) {
        unsigned iy = sample_index(o->y + (int64_t)(y * 2) * o->sy,
                                    height / 2, 17);
        const uint8_t *row = input + (size_t)width * height + (size_t)iy * width;

        for (unsigned x = 0; x < o->width; x += 2) {
            memcpy(o->pixels + ybytes + (size_t)y * o->width + x,
                   row + 2 * xindex[x / 2], 2);
        }
    }
}

static const unsigned work_ports[] = {
    0xe60, 0x900, 0xde0, 0x8a0, 0x8c0, 0x8e0,
    0x9e0, 0x940, 0xba0, 0x920,
};

static bool numeric_port(unsigned offset)
{
    return offset == 0x900 || offset == 0x8a0 || offset == 0x8c0 ||
           offset == 0x8e0;
}

bool nikon_gate_work_same_layout(unsigned offset, const NikonGatePacket *a,
                                 const NikonGatePacket *b)
{
    for (unsigned i = 0; i < NIKON_GATE_REGISTERS; i++) {
        if (a->present[i] != b->present[i] ||
            (a->present[i] && a->value[i] != b->value[i] &&
             !(numeric_port(offset) && i == 0x54 / 4))) {
            return false;
        }
    }
    return true;
}

struct NikonGateWorkPlan {
    NikonGatePixels pixels;
    GateWorkOutput out[3];
    uint8_t transforms[3][32];
    uint32_t grid_address;
    size_t grid_bytes;
    uint8_t *input, *records, *warped;
    uint32_t config[0x4080 / 4], slots[16][13];
    NikonGatePacket packets[ARRAY_SIZE(work_ports)];
    uint32_t auxiliary[ARRAY_SIZE(work_ports)][2];
    NikonGateMonitor monitor;
    unsigned phase;
    bool computed;
};

/* Main-thread only: validate the entire batch against current RAM backing. */
static NikonGateWorkResult layout(const NikonGateWork *w,
                                  NikonGateWorkPlan *plan,
                                  const NikonGateWorkPlan *owned,
                                  const NikonGateWork *live_sources)
{
    const NikonGateWorkCommand *c[ARRAY_SIZE(work_ports)];
    GateWorkOutput *out = plan->out;
    NikonGatePixels *pixels = &plan->pixels;
    g_autoptr(GArray) reads = g_array_new(false, false, sizeof(GateWorkSpan));
    g_autoptr(GArray) writes = g_array_new(false, false, sizeof(GateWorkSpan));
    uint8_t (*transforms)[32] = plan->transforms;
    unsigned width, height;
    size_t grid_bytes;
    uint32_t grid_address;
    NikonGateWorkResult result = NIKON_GATE_WORK_CONFIG;

    if (!w || !w->as || !w->config || !w->slots || !w->commands ||
        !w->input || !w->width || !w->height || w->phase > 3 ||
        ((w->width | w->height) & 1) ||
        w->width > 4096 || w->height > 4096 ||
        w->input_size != (size_t)w->width * w->height * 3 / 2 ||
        w->config[0x2100 / 4] != 0x121 ||
        (w->config[0x2104 / 4] & ~0x30U) != 0xc2 ||
        w->config[0x2108 / 4] ||
        w->config[0x2110 / 4] != 0x200087e0 ||
        w->config[0x2114 / 4] != 0x88000 ||
        w->config[0x2118 / 4] != 0x139 ||
        w->config[0x2600 / 4] != 0x305 ||
        w->config[0x2604 / 4] != w->height ||
        w->config[0x2608 / 4] != 1 || w->config[0x260c / 4] ||
        w->config[0x2610 / 4] || w->config[0x2618 / 4] != 3) {
        return result;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(c); i++) {
        c[i] = command(w, work_ports[i]);
        if (!c[i]) {
            return NIKON_GATE_WORK_TOPOLOGY;
        }
    }
    if (!input_shape(w) || !neutral_carrier(c[0]->packet) ||
        !monitor_profile(c[9]->packet) ||
        !intermediate(c[2]->packet, &width, &height) ||
        w->config[0x4050 / 4] != 2 ||
        (w->config[0x4054 / 4] & 0xffff) != width ||
        !(w->config[0x4054 / 4] >> 16) ||
        (w->config[0x4054 / 4] >> 16) > height) {
        return NIKON_GATE_WORK_FORMAT;
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        /* The typed producer belongs to the kind43 Y/UV ports, not RAM. */
        if (w->config[(0x2200 + phase * 64 + 8 * 4) / 4] != 0x40000000) {
            return NIKON_GATE_WORK_FORMAT;
        }
    }
    if (!grid(c[1]->packet, width, height, &grid_address, &grid_bytes,
              pixels)) {
        return NIKON_GATE_WORK_GRID;
    }
    if (!ram(w, grid_address, grid_bytes, false, reads)) {
        return NIKON_GATE_WORK_MEMORY;
    }
    if (live_sources) {
        const NikonGateWorkCommand *live = command(live_sources, 0x900);
        uint32_t address = word(live->packet, 0x54);

        /* Complete numerical uploads own their future source RAM too. Do not
         * decode new numbers, but do not overwrite their committed storage.
         * The caller already checked that the fixed record shape is unchanged.
         */
        if (address != grid_address &&
            !ram(w, address, grid_bytes, false, reads)) {
            return NIKON_GATE_WORK_MEMORY;
        }
    }
    for (unsigned i = 0; i < 3; i++) {
        uint32_t address;
        unsigned window_w, window_h;

        if (!affine_packet(c[3 + i]->packet, i, &address) ||
            !output(w, c[6 + i], i, &out[i], &window_w, &window_h)) {
            return NIKON_GATE_WORK_FORMAT;
        }
        if (!ram(w, address, 32, false, reads)) {
            return NIKON_GATE_WORK_MEMORY;
        }
        if (live_sources) {
            const NikonGateWorkCommand *live = command(live_sources,
                                                       work_ports[3 + i]);
            uint32_t future = word(live->packet, 0x54);

            if (future != address && !ram(w, future, 32, false, reads)) {
                return NIKON_GATE_WORK_MEMORY;
            }
        }
        if (owned) {
            memcpy(transforms[i], owned->transforms[i], 32);
        } else if (dma_memory_read(w->as, address, transforms[i], 32,
                                   MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return NIKON_GATE_WORK_MEMORY;
        }
        if (!affine(transforms[i], window_w, window_h, i, &out[i])) {
            return NIKON_GATE_WORK_AFFINE;
        }
        for (unsigned plane = 0; plane < 2; plane++) {
            unsigned rows = plane ? out[i].uv_height : out[i].height;

            for (unsigned y = 0; y < rows; y++) {
                if (!ram(w, out[i].address[plane] +
                         (uint64_t)y * out[i].pitch[plane], out[i].width,
                         true, writes)) {
                    return NIKON_GATE_WORK_MEMORY;
                }
            }
        }
    }
    GateWorkSpan input_span = { (uintptr_t)w->input, w->input_size };

    if (w->input_size > UINTPTR_MAX - input_span.start) {
        return NIKON_GATE_WORK_ALIAS;
    }
    g_array_append_val(reads, input_span);
    if (aliases(writes, reads)) {
        return NIKON_GATE_WORK_ALIAS;
    }
    plan->grid_address = grid_address;
    plan->grid_bytes = grid_bytes;
    return NIKON_GATE_WORK_OK;
}

void nikon_gate_work_free(NikonGateWorkPlan *plan)
{
    if (!plan) {
        return;
    }
    g_free(plan->input);
    g_free(plan->records);
    g_free(plan->warped);
    for (unsigned i = 0; i < 3; i++) {
        g_free(plan->out[i].pixels);
    }
    g_free(plan);
}

NikonGateWorkResult nikon_gate_work_prepare(const NikonGateWork *w,
                                           NikonGateWorkPlan **prepared)
{
    NikonGateWorkPlan *plan = g_try_new0(NikonGateWorkPlan, 1);
    NikonGateWorkResult result;

    *prepared = NULL;
    if (!plan) {
        return NIKON_GATE_WORK_PIXELS;
    }
    result = layout(w, plan, NULL, NULL);
    if (result != NIKON_GATE_WORK_OK) {
        goto fail;
    }
    memcpy(plan->config, w->config, sizeof(plan->config));
    memcpy(plan->slots, w->slots, sizeof(plan->slots));
    plan->phase = w->phase;
    for (unsigned i = 0; i < ARRAY_SIZE(work_ports); i++) {
        const NikonGateWorkCommand *c = command(w, work_ports[i]);

        plan->packets[i] = *c->packet;
        memcpy(plan->auxiliary[i], c->auxiliary, sizeof(c->auxiliary));
    }
    plan->input = g_try_malloc(w->input_size);
    plan->records = g_try_malloc(plan->grid_bytes);
    plan->warped = g_try_malloc((size_t)plan->pixels.dst_width *
                                plan->pixels.dst_height * 3 / 2);
    for (unsigned i = 0; i < 3; i++) {
        GateWorkOutput *o = &plan->out[i];
        o->pixels = g_try_malloc((size_t)o->width *
                                 (o->height + o->uv_height));
    }
    if (!plan->input || !plan->records || !plan->warped ||
        !plan->out[0].pixels || !plan->out[1].pixels || !plan->out[2].pixels) {
        result = NIKON_GATE_WORK_PIXELS;
        goto fail;
    }
    memcpy(plan->input, w->input, w->input_size);
    if (dma_memory_read(w->as, plan->grid_address, plan->records,
                        plan->grid_bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        result = NIKON_GATE_WORK_MEMORY;
        goto fail;
    }
    plan->pixels.input = plan->input;
    plan->pixels.input_size = w->input_size;
    plan->pixels.src_width = w->width;
    plan->pixels.src_height = w->height;
    plan->pixels.records = plan->records;
    plan->pixels.records_size = plan->grid_bytes;
    *prepared = plan;
    return NIKON_GATE_WORK_OK;
fail:
    nikon_gate_work_free(plan);
    return result;
}

static void measure(const GateWorkOutput *input, NikonGateMonitor *monitor)
{
    const int64_t maximum_sum = (INT64_C(1) << 48) - 1;

    memset(monitor, 0, sizeof(*monitor));
    for (unsigned i = 0; i < 4; i++) {
        monitor->channel[i].minimum = INT16_MAX;
        monitor->channel[i].maximum = INT16_MIN;
    }
    /*
     * RAW1038f20a sums spatial parities. This profile projects NV12 Y into
     * positive14-bit samples; UV, DMA padding and cropped rows are not samples.
     * Extrema, first-tie ordering, endpoints and bins are virtual laws.
     */
    for (unsigned y = 0; y < input->height; y++) {
        for (unsigned x = 0; x < input->width; x++) {
            NikonGateMeasurement *m = &monitor->channel[2 * (y & 1) + (x & 1)];
            unsigned bin = input->pixels[(size_t)y * input->width + x];
            int16_t value = bin << 6;

            if (value < m->minimum) {
                m->minimum = value;
                m->minimum_x = x;
                m->minimum_y = y;
            }
            if (value > m->maximum) {
                m->maximum = value;
                m->maximum_x = x;
                m->maximum_y = y;
            }
            m->histogram[bin]++;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        NikonGateMeasurement *m = &monitor->channel[i];

        /* Exact weighted distribution sum; avoid three stores per pixel. */
        for (unsigned bin = 0; bin < 256; bin++) {
            m->sum += ((int64_t)bin << 6) * m->histogram[bin];
        }
        m->end_x = input->width - 2 + (i & 1);
        m->end_y = input->height - 2 + (i >> 1);
        /*
         * Virtual per-bank overflow byte, not recovered hardware ready/status.
         * Validated dimensions make every admitted luma sum fit signed49.
         */
        m->overflow = m->sum > maximum_sum;
        m->sum = MIN(m->sum, maximum_sum);
    }
}

/* Pure numerical worker: no guest memory or device state is reachable here. */
NikonGateWorkResult nikon_gate_work_compute(NikonGateWorkPlan *plan)
{
    unsigned width = plan->pixels.dst_width, height = plan->pixels.dst_height;

    if (!nikon_compute_pipeline_warp_nv12(NULL, &plan->pixels, plan->warped,
                                          (size_t)width * height * 3 / 2)) {
        return NIKON_GATE_WORK_PIXELS;
    }
    for (unsigned i = 0; i < 3; i++) {
        render(plan->warped, width, height, &plan->out[i]);
    }
    measure(&plan->out[1], &plan->monitor);
    plan->computed = true;
    return NIKON_GATE_WORK_OK;
}

const NikonGateMonitor *nikon_gate_work_monitor(const NikonGateWorkPlan *plan)
{
    return plan && plan->computed ? &plan->monitor : NULL;
}

NikonGateWorkResult nikon_gate_work_publish(NikonGateWorkPlan *plan,
                                           const NikonGateWork *current)
{
    NikonGateWorkPlan now = { 0 };
    NikonGateWorkCommand commands[ARRAY_SIZE(work_ports)];
    uint32_t config[ARRAY_SIZE(plan->config)];
    NikonGateWork view;
    NikonGateWorkResult result;

    if (!current || !current->config || !current->commands) {
        return NIKON_GATE_WORK_CONFIG;
    }
    view = *current;
    memcpy(config, current->config, sizeof(config));
    /* Numerical uploads are future profiles. The current destinations and
     * transport controls remain live; an owned job never re-reads new math.
     * Atomic profile latching is a finite model, not recovered silicon timing.
     */
    config[0x4054 / 4] = (config[0x4054 / 4] & 0xffff) |
                         (plan->config[0x4054 / 4] & 0xffff0000);
    for (unsigned i = 0; i < ARRAY_SIZE(work_ports); i++) {
        const NikonGateWorkCommand *c = command(current, work_ports[i]);

        if (!c) {
            return NIKON_GATE_WORK_TOPOLOGY;
        }
        commands[i] = *c;
        if (!nikon_gate_work_same_layout(work_ports[i], c->packet,
                                         &plan->packets[i])) {
            return NIKON_GATE_WORK_CONFIG;
        }
        if (numeric_port(work_ports[i])) {
            commands[i].packet = &plan->packets[i];
            memcpy(commands[i].auxiliary, plan->auxiliary[i],
                   sizeof(commands[i].auxiliary));
        }
    }
    view.config = config;
    view.commands = commands;
    view.command_count = ARRAY_SIZE(commands);
    result = layout(&view, &now, plan, current);

    if (result != NIKON_GATE_WORK_OK) {
        return result;
    }
    if (!plan->computed || current->phase != plan->phase ||
        current->width != plan->pixels.src_width ||
        current->height != plan->pixels.src_height ||
        current->config[0x2104 / 4] != plan->config[0x2104 / 4] ||
        memcmp(current->slots[8], plan->slots[8], sizeof(plan->slots[8])) ||
        now.grid_address != plan->grid_address ||
        now.grid_bytes != plan->grid_bytes ||
        now.pixels.dst_width != plan->pixels.dst_width ||
        now.pixels.dst_height != plan->pixels.dst_height) {
        return NIKON_GATE_WORK_CONFIG;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(work_ports); i++) {
        const NikonGateWorkCommand *c = &commands[i];

        if (!equal(c->packet, &plan->packets[i]) ||
            memcmp(c->auxiliary, plan->auxiliary[i], sizeof(c->auxiliary))) {
            return NIKON_GATE_WORK_CONFIG;
        }
    }
    for (unsigned i = 0; i < 3; i++) {
        GateWorkOutput *a = &plan->out[i], *b = &now.out[i];
        unsigned slot = word(&plan->packets[6 + i], 8) & 15;

        if (memcmp(a->address, b->address, sizeof(a->address)) ||
            memcmp(a->pitch, b->pitch, sizeof(a->pitch)) ||
            memcmp(current->slots[slot], plan->slots[slot],
                   sizeof(plan->slots[slot])) ||
            a->width != b->width || a->height != b->height ||
            a->uv_height != b->uv_height) {
            return NIKON_GATE_WORK_CONFIG;
        }
    }
    /* All rows and aliases were revalidated together under BQL, before DMA. */
    for (unsigned i = 0; i < 3; i++) {
        GateWorkOutput *o = &plan->out[i];
        size_t source = 0;

        for (unsigned plane = 0; plane < 2; plane++) {
            unsigned rows = plane ? o->uv_height : o->height;

            for (unsigned y = 0; y < rows; y++) {
                MemTxResult tx = dma_memory_write(current->as,
                    o->address[plane] + (uint64_t)y * o->pitch[plane],
                    o->pixels + source, o->width, MEMTXATTRS_UNSPECIFIED);
                g_assert(tx == MEMTX_OK);
                source += o->width;
            }
        }
    }
    return NIKON_GATE_WORK_OK;
}

NikonGateWorkResult nikon_gate_work_execute(const NikonGateWork *w)
{
    NikonGateWorkPlan *plan;
    NikonGateWorkResult result = nikon_gate_work_prepare(w, &plan);

    if (result == NIKON_GATE_WORK_OK) {
        result = nikon_gate_work_compute(plan);
        if (result == NIKON_GATE_WORK_OK) {
            result = nikon_gate_work_publish(plan, w);
        }
    }
    nikon_gate_work_free(plan);
    return result;
}

const char *nikon_gate_work_result_name(NikonGateWorkResult result)
{
    static const char * const names[] = {
        "ok", "configuration", "topology", "format", "grid", "affine",
        "memory", "alias", "pixels",
    };

    return (unsigned)result < ARRAY_SIZE(names) ? names[result] : "invalid";
}
