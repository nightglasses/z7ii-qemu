/*
 * Wire-only first battery challenge reconstruction. Public arithmetic
 * parameters and provenance: docs/runtime/authentication.md and seed.md.
 * No firmware image, guest interface, private scalar or answer file is used.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "crypto/clmul.h"
#include "battery.h"

/* Canonical GF(2^131), polynomial x^131 + x^8 + x^3 + x^2 + 1. */
typedef struct Field {
    uint64_t lo, mid, hi;
} Field;

typedef struct Wide {
    uint64_t lo, hi;
} Wide;

typedef struct Point {
    Field x, z;
} Point;

typedef Field (*Multiply)(Field, Field);
typedef Field (*Square)(Field);
typedef Wide (*Carryless)(uint64_t, uint64_t);

static const Field base_g = {
    UINT64_C(0x4ec4b6f5d285601f), UINT64_C(0x0720a61b50252fd3), 6,
};
static const Field base_h = {
    UINT64_C(0x3f3a3bd9c53f4ecf), UINT64_C(0x0fdc0f0f00862f76), 0,
};
static const Field curve_c = {
    UINT64_C(0xe2ed5a9b1b80055e), UINT64_C(0x3d078a701dcff787), 0,
};

static bool field_nonzero(Field a)
{
    return (a.lo | a.mid | a.hi) != 0;
}

static bool field_equal(Field a, Field b)
{
    return a.lo == b.lo && a.mid == b.mid && a.hi == b.hi;
}

static Field field_xor(Field a, Field b)
{
    return (Field) { a.lo ^ b.lo, a.mid ^ b.mid, a.hi ^ b.hi };
}

static Wide carryless_portable(uint64_t a, uint64_t b)
{
    Int128 value = clmul_64_gen(a, b);

    return (Wide) { int128_getlo(value), (uint64_t)int128_gethi(value) };
}

static Wide carryless(uint64_t a, uint64_t b)
{
    Int128 value = clmul_64(a, b);

    return (Wide) { int128_getlo(value), (uint64_t)int128_gethi(value) };
}

static Field reduce(uint64_t p0, uint64_t p1, uint64_t p2,
                    uint64_t p3, uint64_t p4)
{
    uint64_t q0 = (p2 >> 3) | (p3 << 61);
    uint64_t q1 = (p3 >> 3) | (p4 << 61);
    uint64_t q2 = p4 >> 3;
    uint64_t r0 = p0 ^ q0, r1 = p1 ^ q1, r2 = (p2 & 7) ^ q2;
    static const unsigned shifts[] = { 2, 3, 8 };

    for (unsigned i = 0; i < 3; i++) {
        unsigned shift = shifts[i];

        r0 ^= q0 << shift;
        r1 ^= (q1 << shift) | (q0 >> (64 - shift));
        r2 ^= (q2 << shift) | (q1 >> (64 - shift));
    }
    /* A second fold removes the at-most-seven overflow bits. */
    uint64_t high = r2 >> 3;

    r0 ^= high ^ (high << 2) ^ (high << 3) ^ (high << 8);
    return (Field) { r0, r1, r2 & 7 };
}

static Field product(Field a, Field b, Carryless clmul)
{
    Wide low = clmul(a.lo, b.lo);
    Wide high = clmul(a.mid, b.mid);
    Wide cross = clmul(a.lo ^ a.mid, b.lo ^ b.mid);
    Wide u = clmul(a.hi, b.lo), v = clmul(b.hi, a.lo);
    uint64_t p0 = low.lo;
    uint64_t p1 = low.hi ^ cross.lo ^ low.lo ^ high.lo;
    uint64_t p2 = high.lo ^ cross.hi ^ low.hi ^ high.hi ^ u.lo ^ v.lo;
    uint64_t p3 = high.hi ^ u.hi ^ v.hi;

    u = clmul(a.hi, b.mid);
    v = clmul(b.hi, a.mid);
    p3 ^= u.lo ^ v.lo;
    return reduce(p0, p1, p2, p3,
                  u.hi ^ v.hi ^ clmul(a.hi, b.hi).lo);
}

static Field square_product(Field a, Carryless clmul)
{
    Wide low = clmul(a.lo, a.lo), high = clmul(a.mid, a.mid);

    return reduce(low.lo, low.hi, high.lo, high.hi,
                  clmul(a.hi, a.hi).lo);
}

static Field field_multiply(Field a, Field b)
{
    return product(a, b, carryless);
}

static Field field_square(Field a)
{
    return square_product(a, carryless);
}

static Field portable_multiply(Field a, Field b)
{
    return product(a, b, carryless_portable);
}

static Field portable_square(Field a)
{
    return square_product(a, carryless_portable);
}

/*
 * Verification uses shift-and-reduce, independently of product accumulation,
 * Karatsuba, accelerated instructions and the search reduction above.
 */
static Field reference_multiply(Field a, Field b)
{
    Field result = { 0, 0, 0 };

    for (unsigned bit = 0; bit < 131; bit++) {
        if (b.lo & 1) {
            result = field_xor(result, a);
        }
        b.lo = (b.lo >> 1) | (b.mid << 63);
        b.mid = (b.mid >> 1) | (b.hi << 63);
        b.hi >>= 1;
        bool overflow = (a.hi & 4) != 0;

        a.hi = ((a.hi << 1) | (a.mid >> 63)) & 7;
        a.mid = (a.mid << 1) | (a.lo >> 63);
        a.lo <<= 1;
        if (overflow) {
            a.lo ^= UINT64_C(0x10d);
        }
    }
    return result;
}

static Field reference_square(Field a)
{
    return reference_multiply(a, a);
}

static Point point_double(Point p, Multiply multiply, Square square)
{
    Field x = square(p.x), z = square(p.z);

    return (Point) {
        square(field_xor(x, multiply(curve_c, z))), multiply(x, z),
    };
}

static Point point_add(Point p, Point q, Field base,
                       Multiply multiply, Square square)
{
    Field t = multiply(p.x, q.z), v = multiply(q.x, p.z);
    Field w = square(field_xor(t, v));

    return (Point) { field_xor(multiply(t, v), multiply(base, w)), w };
}

static Point ladder(Field base, const uint16_t scalar[8],
                    Multiply multiply, Square square)
{
    Point p = { { 1, 0, 0 }, { 0, 0, 0 } };
    Point q = { base, { 1, 0, 0 } };

    for (int bit = 127; bit >= 0; bit--) {
        Point sum = point_add(p, q, base, multiply, square);

        if ((scalar[bit / 16] >> (bit % 16)) & 1) {
            q = point_double(q, multiply, square);
            p = sum;
        } else {
            p = point_double(p, multiply, square);
            q = sum;
        }
    }
    return p;
}

static bool normalize(Point p, Multiply multiply, Square square, Field *out)
{
    Field inverse = { 1, 0, 0 };

    if (!field_nonzero(p.z)) {
        return false;
    }
    /* z^(2^131 - 2). */
    for (int bit = 130; bit >= 0; bit--) {
        inverse = square(inverse);
        if (bit) {
            inverse = multiply(inverse, p.z);
        }
    }
    *out = multiply(p.x, inverse);
    return true;
}

static void field_encode(Field value, uint8_t out[17])
{
    for (unsigned i = 0; i < 8; i++) {
        out[i] = value.lo >> (8 * i);
        out[8 + i] = value.mid >> (8 * i);
    }
    out[16] = value.hi;
}

static Field field_decode(const uint8_t in[17])
{
    Field value = { 0, 0, in[16] };

    for (unsigned i = 0; i < 8; i++) {
        value.lo |= (uint64_t)in[i] << (8 * i);
        value.mid |= (uint64_t)in[8 + i] << (8 * i);
    }
    return value;
}

static void first_scalar(uint32_t seed, uint16_t scalar[8], bool full_ring)
{
    uint32_t ring[55], x = seed;
    unsigned p = 31, q = 0;

    for (int i = 54; i >= 0; i--) {
        ring[i] = x + (x >> 16);
        x = UINT32_C(69069) * x + UINT32_C(0x66d619e1);
    }
    /* The first eight reads do not overlap previously updated ring cells. */
    for (unsigned i = 0; i < 8; i++) {
        uint32_t value = ring[p] + ring[q];

        scalar[i] = value;
        if (full_ring) {
            ring[q] = value;
        }
        p = (p + 1) % 55;
        q = (q + 1) % 55;
    }
}

static bool canonical(const uint8_t *challenge)
{
    return challenge && challenge[16] <= 7;
}

static bool is_cancelled(NikonBatteryCancelled cancelled, void *opaque)
{
    return cancelled && cancelled(opaque);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool nikon_battery_parse_challenge(const char *hex, uint8_t challenge[17])
{
    uint8_t parsed[17];

    if (!hex || !challenge || strlen(hex) != 34) {
        return false;
    }
    for (unsigned i = 0; i < 17; i++) {
        int high = hex_digit(hex[2 * i]), low = hex_digit(hex[2 * i + 1]);

        if (high < 0 || low < 0) {
            return false;
        }
        parsed[i] = (high << 4) | low;
    }
    if (!canonical(parsed)) {
        return false;
    }
    memcpy(challenge, parsed, sizeof(parsed));
    return true;
}

const char *nikon_battery_backend(void)
{
    return HAVE_CLMUL_ACCEL ? "qemu-accelerated" : "qemu-portable";
}

bool nikon_battery_challenge(uint32_t seed, uint8_t challenge[17])
{
    uint16_t scalar[8];
    Field value;

    if (!challenge) {
        return false;
    }
    first_scalar(seed, scalar, false);
    if (!normalize(ladder(base_g, scalar, field_multiply, field_square),
                   field_multiply, field_square, &value)) {
        return false;
    }
    field_encode(value, challenge);
    return true;
}

static bool verify(const uint8_t challenge[17], uint32_t seed,
                   uint16_t scalar[8])
{
    uint8_t recomputed[17];
    Field value;

    first_scalar(seed, scalar, true);
    if (!normalize(ladder(base_g, scalar, reference_multiply, reference_square),
                   reference_multiply, reference_square, &value)) {
        return false;
    }
    field_encode(value, recomputed);
    return memcmp(challenge, recomputed, sizeof(recomputed)) == 0;
}

NikonBatterySearchStatus nikon_battery_search(
    const uint8_t challenge[17], uint64_t start, uint64_t count,
    NikonBatteryCancelled cancelled, void *opaque,
    NikonBatterySearchResult *result)
{
    if (!result) {
        return NIKON_BATTERY_INVALID;
    }
    result->tried = 0;
    if (!canonical(challenge) || start >= NIKON_BATTERY_SEED_LIMIT ||
        !count || count > NIKON_BATTERY_SEED_LIMIT - start) {
        return NIKON_BATTERY_INVALID;
    }
    Field target = field_decode(challenge);

    for (uint64_t seed = start; seed < start + count; seed++) {
        uint16_t scalar[8];

        if (is_cancelled(cancelled, opaque)) {
            return NIKON_BATTERY_CANCELLED;
        }
        first_scalar(seed, scalar, false);
        Point p = ladder(base_g, scalar, field_multiply, field_square);

        result->tried++;
        if (field_nonzero(p.z) &&
            field_equal(p.x, field_multiply(target, p.z))) {
            bool valid = verify(challenge, seed, scalar);

            if (is_cancelled(cancelled, opaque)) {
                return NIKON_BATTERY_CANCELLED;
            }
            if (!valid) {
                return NIKON_BATTERY_VERIFY_FAILED;
            }
            result->representative = seed;
            return NIKON_BATTERY_FOUND;
        }
    }
    return is_cancelled(cancelled, opaque) ? NIKON_BATTERY_CANCELLED :
                                           NIKON_BATTERY_NOT_FOUND;
}

NikonBatterySearchStatus nikon_battery_first_auth(
    const uint8_t challenge[17], uint32_t representative,
    NikonBatteryCancelled cancelled, void *opaque, uint8_t reply[29])
{
    uint16_t scalar[8];
    uint8_t candidate[29] = { 2, 6, 0 }, shared_bytes[17];
    Field shared;

    if (!canonical(challenge) || !reply ||
        representative >= NIKON_BATTERY_SEED_LIMIT) {
        return NIKON_BATTERY_INVALID;
    }
    if (is_cancelled(cancelled, opaque)) {
        return NIKON_BATTERY_CANCELLED;
    }
    bool valid = verify(challenge, representative, scalar);

    if (is_cancelled(cancelled, opaque)) {
        return NIKON_BATTERY_CANCELLED;
    }
    if (!valid) {
        return NIKON_BATTERY_VERIFY_FAILED;
    }
    if (!normalize(ladder(base_h, scalar, reference_multiply, reference_square),
                   reference_multiply, reference_square, &shared)) {
        return NIKON_BATTERY_VERIFY_FAILED;
    }
    /*
     * R=1; tag=first8(S). Only first-auth has the source-backed zero tail.
     * Bytes 0/2 are explicit framing choices, not extra validator predicates.
     */
    field_encode(shared, shared_bytes);
    memcpy(candidate + 3, shared_bytes, 8);
    candidate[11] = 1;
    for (unsigned i = 0; i < 28; i++) {
        candidate[28] ^= candidate[i];
    }
    if (is_cancelled(cancelled, opaque)) {
        return NIKON_BATTERY_CANCELLED;
    }
    memcpy(reply, candidate, sizeof(candidate));
    return NIKON_BATTERY_FOUND;
}

bool nikon_battery_selftest(void)
{
    static const struct {
        uint32_t seed;
        const char *challenge;
    } vectors[] = {
        { 0, "c1bac8b20c6ffe82fecd05132fbd7c1003" },
        { 1, "674f96a8fc71ea14dbe0a9c01ab6da2302" },
        { 2, "9e13f3d8a9e86567a1ec12c6e1f1964105" },
        { 3, "079860c54741744e8109f40615ae05a407" },
        { UINT32_C(0x12345678), "838c9af191891f66daddbf41963b8df406" },
        { UINT32_C(0xdeadbeef), "6fc23b006f63b4d3509c0edfc845868302" },
        { UINT32_C(0x7fffffff), "aae8d794fc45fa2ed43709c89c72cecc06" },
        { UINT32_MAX, "aae8d794fc45fa2ed43709c89c72cecc06" },
    };

    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint8_t expected[17], actual[17], portable[17];
        uint16_t scalar[8];
        Field value;

        if (!nikon_battery_parse_challenge(vectors[i].challenge, expected) ||
            !nikon_battery_challenge(vectors[i].seed, actual) ||
            memcmp(expected, actual, sizeof(expected)) ||
            !verify(expected, vectors[i].seed, scalar)) {
            return false;
        }
        first_scalar(vectors[i].seed, scalar, false);
        if (!normalize(ladder(base_g, scalar,
                              portable_multiply, portable_square),
                       portable_multiply, portable_square, &value)) {
            return false;
        }
        field_encode(value, portable);
        if (memcmp(expected, portable, sizeof(expected))) {
            return false;
        }
    }
    return true;
}
