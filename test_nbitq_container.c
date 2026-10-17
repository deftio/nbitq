/**
 * @file test_nbitq_container.c
 * @brief Tests for the optional .nbq container.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbitq_container.h"

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

/* The published vector for CRC-64/XZ. Checking against an outside source
   rather than against ourselves is the only way to know the polynomial,
   reflection, init and final XOR are all right. */
static void test_crc64_published_vector(void)
{
    const char *s = "123456789";
    CHECK(nbq_crc64((const uint8_t *)s, 9) == 0x995DC9BBDF1939FAull,
          "CRC-64/XZ(\"123456789\") should be 995DC9BBDF1939FA");
    CHECK(nbq_crc64((const uint8_t *)"", 0) == 0ull, "CRC-64/XZ of empty is 0");

    /* Incremental must equal one-shot, split anywhere. */
    for (size_t split = 0; split <= 9; split++) {
        uint64_t c = NBQ_CRC64_INIT;
        c = nbq_crc64_update(c, (const uint8_t *)s, split);
        c = nbq_crc64_update(c, (const uint8_t *)s + split, 9 - split);
        CHECK(nbq_crc64_final(c) == 0x995DC9BBDF1939FAull, "incremental split at %zu", split);
    }
}

/* Every payload bit length, so the final partial byte and its padding are
   exercised at all eight positions. */
static void test_round_trip_every_bit_length(void)
{
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(0xA5 ^ (i * 17));

    for (uint64_t nbits = 0; nbits <= 200; nbits++) {
        uint64_t need = 0;
        CHECK(nbq_container_size(4, nbits, &need) == NBQ_OK, "size nbits=%llu",
              (unsigned long long)nbits);
        uint8_t *out = (uint8_t *)malloc((size_t)need);
        CHECK(out != NULL, "alloc");
        if (!out)
            return;

        size_t written = 0;
        CHECK(nbq_container_write(out, (size_t)need, "trie", 4, payload, nbits, &written) == NBQ_OK,
              "write nbits=%llu", (unsigned long long)nbits);
        CHECK(written == need, "size query disagrees with write at nbits=%llu",
              (unsigned long long)nbits);

        nbq_container c;
        memset(&c, 0, sizeof(c));
        CHECK(nbq_container_read(out, written, &c) == NBQ_OK, "read nbits=%llu",
              (unsigned long long)nbits);
        CHECK(c.payload_bits == nbits, "bit count: got %llu want %llu",
              (unsigned long long)c.payload_bits, (unsigned long long)nbits);
        CHECK(c.type_len == 4 && memcmp(c.type, "trie", 4) == 0, "type string");
        CHECK(c.version == NBQ_CONTAINER_VERSION, "version");

        /* The payload bits must come back identical. */
        if (nbits > 0) {
            int cmp = 1;
            CHECK(nbq_compare_bits(payload, sizeof(payload) * 8, 0, c.payload,
                                   (uint64_t)written * 8, 0, nbits, &cmp) == NBQ_OK,
                  "compare");
            CHECK(cmp == 0, "payload differs at nbits=%llu", (unsigned long long)nbits);
        }
        free(out);
    }
}

/* Two writers of the same object must produce the same bytes, which means the
   padding in the final partial byte has to be deterministic. */
static void test_write_is_canonical(void)
{
    uint8_t payload[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t a[64], b[64];
    /* 13 bits of a 0xFF payload: the final byte has 5 bits of padding. */
    memset(a, 0x11, sizeof(a));
    memset(b, 0xEE, sizeof(b)); /* different pre-existing garbage */
    size_t la = 0, lb = 0;
    CHECK(nbq_container_write(a, sizeof(a), "x", 1, payload, 13, &la) == NBQ_OK, "write a");
    CHECK(nbq_container_write(b, sizeof(b), "x", 1, payload, 13, &lb) == NBQ_OK, "write b");
    CHECK(la == lb, "lengths differ");
    CHECK(memcmp(a, b, la) == 0, "same object produced different bytes");
}

static void test_empty_type_and_empty_payload(void)
{
    uint8_t out[64];
    size_t len = 0;
    CHECK(nbq_container_write(out, sizeof(out), NULL, 0, NULL, 0, &len) == NBQ_OK,
          "empty type and payload");
    nbq_container c;
    CHECK(nbq_container_read(out, len, &c) == NBQ_OK, "read empty");
    CHECK(c.type_len == 0, "type_len");
    CHECK(c.payload_bits == 0, "payload_bits");
}

static void test_rejections(void)
{
    uint8_t good[64];
    size_t len = 0;
    const uint8_t payload[4] = {1, 2, 3, 4};
    CHECK(nbq_container_write(good, sizeof(good), "raw", 3, payload, 32, &len) == NBQ_OK, "setup");

    nbq_container c;
    uint8_t bad[64];

    /* bad magic */
    memcpy(bad, good, len);
    bad[0] = 'X';
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "bad magic");

    /* unknown version */
    memcpy(bad, good, len);
    bad[3] = 99;
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "unknown version");

    /* corrupted payload must fail the CRC */
    memcpy(bad, good, len);
    bad[len - 9] ^= 0xFF;  /* last payload byte, just before the CRC */
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "payload corruption");

    /* corrupted CRC itself */
    memcpy(bad, good, len);
    bad[len - 1] ^= 0x01;
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "crc corruption");

    /* truncation at every length is refused, never a short object */
    for (size_t cut = 0; cut < len; cut++)
        CHECK(nbq_container_read(good, cut, &c) != NBQ_OK, "truncated to %zu was accepted", cut);

    /* trailing bytes */
    memcpy(bad, good, len);
    bad[len] = 0x00;
    CHECK(nbq_container_read(bad, len + 1, &c) == NBQ_ERR_MALFORMED, "trailing byte");

    /* a payload bit count larger than the bytes present */
    memcpy(bad, good, len);
    /* magic(4) + varint type_len(1) + "raw"(3) = byte 8 holds the bit count */
    bad[8] = 0x7F; /* 127 bits, but only 4 bytes of payload follow */
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "declared bits exceed the buffer");

    /* parameter checks */
    CHECK(nbq_container_read(NULL, len, &c) == NBQ_ERR_PARAM, "null buf");
    CHECK(nbq_container_read(good, len, NULL) == NBQ_ERR_PARAM, "null out");
    CHECK(nbq_container_write(good, 4, "raw", 3, payload, 32, &len) == NBQ_ERR_RANGE,
          "output buffer too small");
    uint64_t need;
    CHECK(nbq_container_size(NBQ_TYPE_MAX + 1u, 0, &need) == NBQ_ERR_PARAM, "type too long");
}

/* The unchecked reader skips the checksum and nothing else: every length is
   still validated, or it would be a way to walk off the buffer. */
static void test_unchecked_still_validates_lengths(void)
{
    uint8_t good[64], bad[64];
    size_t len = 0;
    const uint8_t payload[4] = {9, 9, 9, 9};
    CHECK(nbq_container_write(good, sizeof(good), "raw", 3, payload, 32, &len) == NBQ_OK, "setup");

    nbq_container c;
    memcpy(bad, good, len);
    bad[len - 1] ^= 0xFF; /* break the CRC only */
    CHECK(nbq_container_read(bad, len, &c) == NBQ_ERR_MALFORMED, "checked rejects bad CRC");
    CHECK(nbq_container_read_unchecked(bad, len, &c) == NBQ_OK, "unchecked accepts bad CRC");

    memcpy(bad, good, len);
    bad[8] = 0x7F; /* lie about the bit count */
    CHECK(nbq_container_read_unchecked(bad, len, &c) == NBQ_ERR_MALFORMED,
          "unchecked must still refuse an impossible length");
    CHECK(nbq_container_read_unchecked(good, 3, &c) != NBQ_OK, "unchecked refuses a short buffer");
}

/* Frames long enough to pass the length check, each with one fault in the
   header: refused as malformed, never read past, by both parse paths. */
static void test_malformed_headers_are_refused(void)
{
    static const struct {
        const char *what;
        uint8_t hdr[8];
        size_t n;
    } T[] = {
        {"overlong type_len varint", {0x80, 0x00}, 2},
        {"type_len 257, past NBQ_TYPE_MAX", {0x81, 0x02}, 2},
        {"type_len 100, past the end of the frame", {0x64}, 1},
        {"overlong payload_bits varint", {0x00, 0x80, 0x00}, 3},
        {"a payload of 2^40 bits", {0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x20}, 7},
    };
    uint8_t f[14];
    nbq_container c;

    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        memset(f, 0, sizeof(f));
        f[0] = 'N';
        f[1] = 'B';
        f[2] = 'Q';
        f[3] = 1;
        memcpy(f + 4, T[i].hdr, T[i].n);
        CHECK(nbq_container_read(f, sizeof(f), &c) == NBQ_ERR_MALFORMED, "%s", T[i].what);
        CHECK(nbq_container_read_unchecked(f, sizeof(f), &c) == NBQ_ERR_MALFORMED,
              "%s (unchecked)", T[i].what);
    }
}

static void test_write_refusals(void)
{
    uint8_t out[64], payload[2] = {0xAE, 0x10};
    char big[NBQ_TYPE_MAX + 1];
    size_t n = 0;

    memset(big, 'x', sizeof(big));
    CHECK(nbq_container_write(NULL, sizeof(out), "t", 1, payload, 12, &n) == NBQ_ERR_PARAM,
          "null out");
    CHECK(nbq_container_write(out, sizeof(out), NULL, 1, payload, 12, &n) == NBQ_ERR_PARAM,
          "null type with a length");
    CHECK(nbq_container_write(out, sizeof(out), "t", 1, NULL, 12, &n) == NBQ_ERR_PARAM,
          "null payload with bits");
    CHECK(nbq_container_write(out, sizeof(out), big, sizeof(big), payload, 12, &n) ==
              NBQ_ERR_PARAM,
          "type longer than NBQ_TYPE_MAX");
    CHECK(nbq_crc64_update(NBQ_CRC64_INIT, NULL, 5) == NBQ_CRC64_INIT,
          "crc64_update tolerates a null pointer");
}


int main(void)
{
    test_crc64_published_vector();
    test_round_trip_every_bit_length();
    test_write_is_canonical();
    test_empty_type_and_empty_payload();
    test_rejections();
    test_unchecked_still_validates_lengths();
    test_malformed_headers_are_refused();
    test_write_refusals();
    printf("nbitq_container: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
