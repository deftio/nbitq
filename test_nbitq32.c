/**
 * @file test_nbitq32.c
 * @brief nbitq32 against nbitq as the oracle.
 *
 * nbitq32.c is a second copy of the u32 path with 32-bit positions. A copy
 * is only acceptable if it cannot drift, so this suite holds it to nbitq.c:
 * every width 1..32, every start offset across several bytes, both orders,
 * the same bytes written into the same poison and the same values read
 * back. Then its own refusals, including the position that would wrap.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <stdio.h>
#include <string.h>

#include "nbitq.h"
#include "nbitq32.h"

static int checks = 0, failures = 0;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

/* The pair under test and the oracle, by order. */
static nbq32_result put32(int le, uint8_t *b, uint32_t cap, uint32_t pos, unsigned n, uint32_t v)
{
    return le ? nbq32_put_le(b, cap, pos, n, v) : nbq32_put(b, cap, pos, n, v);
}
static nbq32_result get32(int le, const uint8_t *b, uint32_t len, uint32_t pos, unsigned n,
                          uint32_t *out)
{
    return le ? nbq32_get_le(b, len, pos, n, out) : nbq32_get(b, len, pos, n, out);
}
static nbq_result put_ref(int le, uint8_t *b, uint64_t cap, uint64_t pos, unsigned n, uint32_t v)
{
    return le ? nbq_put_u32_le(b, cap, pos, n, v) : nbq_put_u32(b, cap, pos, n, v);
}
static nbq_result get_ref(int le, const uint8_t *b, uint64_t len, uint64_t pos, unsigned n,
                          uint32_t *out)
{
    return le ? nbq_get_u32_le(b, len, pos, n, out) : nbq_get_u32(b, len, pos, n, out);
}

/* ── the codes are the same numbers ─────────────────────────────────── */

static void test_result_codes_match(void)
{
    CHECK((int)NBQ32_OK == (int)NBQ_OK, "OK");
    CHECK((int)NBQ32_ERR_PARAM == (int)NBQ_ERR_PARAM, "PARAM");
    CHECK((int)NBQ32_ERR_RANGE == (int)NBQ_ERR_RANGE, "RANGE");
}

/* ── every width, every offset, both orders, against the oracle ─────── */

static void test_same_bytes_as_nbitq(void)
{
    static const uint32_t V[] = {0u, 1u, 2u, 0x55555555u, 0xAAAAAAAAu, 0x7FFFFFFFu,
                                 0x80000000u, 0xFFFFFFFFu, 0xDEADBEEFu, 0x01234567u};
    uint8_t a[16], b[16];
    const uint32_t cap = sizeof(a) * 8u;

    for (int le = 0; le < 2; le++) {
        for (unsigned n = 1; n <= 32; n++) {
            const uint32_t mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
            for (size_t vi = 0; vi < sizeof(V) / sizeof(V[0]); vi++) {
                const uint32_t want = V[vi] & mask;
                /* Every start offset over three bytes, so no alignment or
                   byte boundary is special, and the poison rotates so a
                   field that spans bytes meets both surroundings. */
                for (uint32_t off = 0; off <= 24; off++) {
                    const uint8_t poison = (uint8_t)(0xCCu ^ (off * 0x11u));
                    memset(a, poison, sizeof(a));
                    memset(b, poison, sizeof(b));
                    CHECK(put32(le, a, cap, off, n, want) == NBQ32_OK, "put32 n=%u off=%u", n, off);
                    CHECK(put_ref(le, b, cap, off, n, want) == NBQ_OK, "put_ref n=%u off=%u", n, off);
                    CHECK(memcmp(a, b, sizeof(a)) == 0, "bytes differ le=%d n=%u off=%u", le, n,
                          off);

                    uint32_t got = 0xFFFFFFFFu, ref = 0;
                    CHECK(get32(le, a, cap, off, n, &got) == NBQ32_OK, "get32");
                    CHECK(get_ref(le, a, cap, off, n, &ref) == NBQ_OK, "get_ref");
                    CHECK(got == want, "le=%d n=%u off=%u: got %lu want %lu", le, n, off,
                          (unsigned long)got, (unsigned long)want);
                    CHECK(got == ref, "disagree with oracle le=%d n=%u off=%u", le, n, off);
                }
            }
        }
    }
}

/* Reading back a field someone else wrote, at every bit position of a
   buffer: the two decoders see the same value in random-looking bytes. */
static void test_reads_agree_on_arbitrary_bytes(void)
{
    uint8_t buf[64];
    uint32_t x = 0x9E3779B9u;
    for (size_t i = 0; i < sizeof(buf); i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        buf[i] = (uint8_t)x;
    }
    const uint32_t len = sizeof(buf) * 8u;
    for (int le = 0; le < 2; le++) {
        for (unsigned n = 1; n <= 32; n += 1) {
            for (uint32_t pos = 0; pos + n <= len; pos++) {
                uint32_t got = 0, ref = 1;
                CHECK(get32(le, buf, len, pos, n, &got) == NBQ32_OK, "get32 pos=%lu",
                      (unsigned long)pos);
                CHECK(get_ref(le, buf, len, pos, n, &ref) == NBQ_OK, "get_ref");
                CHECK(got == ref, "le=%d n=%u pos=%lu: %lx vs %lx", le, n, (unsigned long)pos,
                      (unsigned long)got, (unsigned long)ref);
            }
        }
    }
}

/* ── refusals, and nothing touched when refusing ────────────────────── */

static void test_refusals(void)
{
    uint8_t buf[8];
    uint8_t before[8];
    uint32_t out = 0x12345678u;
    const uint32_t len = sizeof(buf) * 8u;

    memset(buf, 0xA5, sizeof(buf));
    memcpy(before, buf, sizeof(buf));

    for (int le = 0; le < 2; le++) {
        /* parameters */
        CHECK(get32(le, NULL, len, 0, 8, &out) == NBQ32_ERR_PARAM, "null buf");
        CHECK(get32(le, buf, len, 0, 8, NULL) == NBQ32_ERR_PARAM, "null out");
        CHECK(get32(le, buf, len, 0, 0, &out) == NBQ32_ERR_PARAM, "n = 0");
        CHECK(get32(le, buf, len, 0, 33, &out) == NBQ32_ERR_PARAM, "n = 33");
        CHECK(put32(le, NULL, len, 0, 8, 1) == NBQ32_ERR_PARAM, "put null buf");
        CHECK(put32(le, buf, len, 0, 0, 1) == NBQ32_ERR_PARAM, "put n = 0");
        CHECK(put32(le, buf, len, 0, 33, 1) == NBQ32_ERR_PARAM, "put n = 33");

        /* range: the last bit that fits, and one past it, for every width */
        for (unsigned n = 1; n <= 32; n++) {
            uint32_t v;
            CHECK(get32(le, buf, len, len - n, n, &v) == NBQ32_OK, "last fit n=%u", n);
            CHECK(get32(le, buf, len, len - n + 1u, n, &v) == NBQ32_ERR_RANGE, "one past n=%u", n);
            CHECK(get32(le, buf, n - 1u, 0, n, &v) == NBQ32_ERR_RANGE, "buffer too short n=%u", n);
            CHECK(put32(le, buf, len, len - n + 1u, n, 0) == NBQ32_ERR_RANGE, "put one past n=%u",
                  n);
        }

        /* A position that would wrap if the check computed pos + n. The
           buffer is never dereferenced, so the claimed length can be
           anything. */
        CHECK(get32(le, buf, 0xFFFFFFFFu, 0xFFFFFFF0u, 32, &out) == NBQ32_ERR_RANGE, "wrap get");
        CHECK(put32(le, buf, 0xFFFFFFFFu, 0xFFFFFFF0u, 32, 0) == NBQ32_ERR_RANGE, "wrap put");
        CHECK(get32(le, buf, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, &out) == NBQ32_ERR_RANGE,
              "last position, one bit past");
        CHECK(put32(le, buf, 8, 0xFFFFFFFFu, 1, 1) == NBQ32_ERR_RANGE, "huge pos, small buffer");
    }
    CHECK(out == 0x12345678u, "a refused read wrote to *out");
    CHECK(memcmp(buf, before, sizeof(buf)) == 0, "a refused write touched the buffer");

    /* The oracle refuses the same calls. */
    CHECK(nbq_get_u32(buf, len, len - 7u, 8, &out) == NBQ_ERR_RANGE &&
              nbq32_get(buf, len, len - 7u, 8, &out) == NBQ32_ERR_RANGE,
          "both refuse the same read");
}

/* ── a field at the top of a 32-bit position space ──────────────────── */

static void test_positions_near_the_limit(void)
{
    /* A real buffer cannot be 512 MB here, but the arithmetic on the position
       can be exercised: a 16-byte buffer whose first bit is declared to be
       at a huge position by offsetting the pointer back is not valid C, so
       instead claim a length just large enough and check the bounds math
       at the edge of what fits. */
    uint8_t buf[16];
    const uint32_t len = sizeof(buf) * 8u;
    memset(buf, 0, sizeof(buf));
    CHECK(nbq32_put(buf, len, len - 32u, 32, 0xC0FFEE11u) == NBQ32_OK, "last 32 bits");
    uint32_t v = 0;
    CHECK(nbq32_get(buf, len, len - 32u, 32, &v) == NBQ32_OK && v == 0xC0FFEE11u, "read back");
    CHECK(buf[12] == 0xC0 && buf[13] == 0xFF && buf[14] == 0xEE && buf[15] == 0x11,
          "aligned 32 bits are the bytes themselves, MSB first");
    CHECK(nbq32_put_le(buf, len, len - 32u, 32, 0xC0FFEE11u) == NBQ32_OK, "last 32 bits, le");
    CHECK(buf[12] == 0x11 && buf[13] == 0xEE && buf[14] == 0xFF && buf[15] == 0xC0,
          "aligned 32 bits are the bytes little-endian, LSB first");
    CHECK(nbq32_get_be(buf, len, 0, 1, &v) == NBQ32_OK && v == 0, "the _be name is the plain one");
    CHECK(nbq32_put_be(buf, len, 0, 1, 1) == NBQ32_OK && buf[0] == 0x80, "_be put");
}

int main(void)
{
    test_result_codes_match();
    test_same_bytes_as_nbitq();
    test_reads_agree_on_arbitrary_bytes();
    test_refusals();
    test_positions_near_the_limit();

    printf("nbitq32: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
