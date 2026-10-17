/**
 * @file fuzz_nbitq.c
 * @brief Fuzz harness for nbitq. Builds for libFuzzer or standalone.
 *
 * Asserts more than "does not crash". The properties checked are the ones a
 * caller actually relies on:
 *
 *   - anything that decodes re-encodes to identical bits (canonical form);
 *   - the u64, u32 and hi/lo paths agree on every read, at every alignment;
 *   - a failed read leaves the cursor untouched;
 *   - a copied bit run compares equal to its source.
 *
 * Copyright (c) 2026 M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nbitq.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3)
        return 0;

    const unsigned base = 2u + (data[0] % 63u);  /* 2..64 */
    const unsigned width = 1u + (data[1] % 64u); /* 1..64 */
    const uint8_t *buf = data + 2;
    const uint64_t bits = (uint64_t)(size - 2) * 8u;

    /* varints: every decode must re-encode to the same bits */
    {
        uint64_t pos = 0;
        for (int i = 0; i < 64; i++) {
            uint64_t v, next;
            if (nbq_get_varint(buf, bits, pos, base, &v, &next) != NBQ_OK)
                break;
            assert(next > pos);
            assert(nbq_varint_bits(v, base) == (unsigned)(next - pos));

            uint8_t re[80];
            memset(re, 0, sizeof(re));
            if (nbq_put_varint(re, sizeof(re) * 8, 0, base, v, NULL) == NBQ_OK) {
                int cmp = 1;
                assert(nbq_compare_bits(buf, bits, pos, re, sizeof(re) * 8, 0, next - pos,
                                        &cmp) == NBQ_OK);
                assert(cmp == 0); /* canonical: same value, same bits */
            }
            pos = next;
        }
    }

    /* signed varints round-trip through zigzag */
    {
        uint64_t pos = 0;
        for (int i = 0; i < 64; i++) {
            int64_t v;
            uint64_t next;
            if (nbq_get_varint_s(buf, bits, pos, base, &v, &next) != NBQ_OK)
                break;
            assert(nbq_varint_bits_s(v, base) == (unsigned)(next - pos));
            pos = next;
        }
    }

    /* the three fixed-width paths must agree, read for read */
    {
        for (uint64_t pos = 0; pos + width <= bits; pos += width) {
            uint64_t v64;
            nbq_u64_parts p;
            assert(nbq_get_u64(buf, bits, pos, width, &v64) == NBQ_OK);
            assert(nbq_get_parts(buf, bits, pos, width, &p) == NBQ_OK);
            assert(p.hi == (uint32_t)(v64 >> 32));
            assert(p.lo == (uint32_t)(v64 & 0xFFFFFFFFu));
            if (width <= 32) {
                uint32_t v32;
                assert(nbq_get_u32(buf, bits, pos, width, &v32) == NBQ_OK);
                assert((uint64_t)v32 == v64);
            }
            /* sign extension agrees with reinterpreting the unsigned read */
            int64_t s;
            assert(nbq_get_s64(buf, bits, pos, width, &s) == NBQ_OK);
            uint64_t expect = v64;
            if (width < 64 && ((v64 >> (width - 1)) & 1u))
                expect |= ~((((uint64_t)1) << width) - 1u);
            assert((uint64_t)s == expect);
        }
    }

    /* a failed read never moves the cursor */
    {
        nbq_reader r;
        if (nbq_reader_init(&r, buf, bits) == NBQ_OK) {
            assert(nbq_reader_seek(&r, bits) == NBQ_OK);
            const uint64_t at_end = r.pos;
            uint64_t v;
            assert(nbq_read_u64(&r, 1, &v) == NBQ_ERR_RANGE);
            assert(r.pos == at_end);
            assert(nbq_read_varint(&r, base, &v) != NBQ_OK);
            assert(r.pos == at_end);
        }
    }

    /* a copied run equals its source, at an awkward offset */
    if (bits > 16) {
        uint8_t dst[512];
        const uint64_t n = (bits - 8 > 256) ? 256 : bits - 8;
        memset(dst, 0, sizeof(dst));
        if (nbq_copy_bits(buf, bits, 3, dst, sizeof(dst) * 8, 5, n) == NBQ_OK) {
            int cmp = 1;
            assert(nbq_compare_bits(buf, bits, 3, dst, sizeof(dst) * 8, 5, n, &cmp) == NBQ_OK);
            assert(cmp == 0);
            uint64_t pa = 0, pb = 1;
            assert(nbq_popcount_bits(buf, bits, 3, n, &pa) == NBQ_OK);
            assert(nbq_popcount_bits(dst, sizeof(dst) * 8, 5, n, &pb) == NBQ_OK);
            assert(pa == pb);
        }
    }

    return 0;
}

#ifdef NBQ_FUZZ_STANDALONE
#include <stdio.h>

/* xorshift64*, so a run is identical on every platform. libFuzzer is better
   at finding things; this exists so the harness runs where libFuzzer is not
   installed, which includes Apple's clang. */
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return rng * 0x2545F4914F6CDD1Dull;
}

int main(int argc, char **argv)
{
    long iters = (argc > 1) ? strtol(argv[1], NULL, 10) : 100000;
    const char *env = getenv("NBQ_FUZZ_ITERS");
    if (env)
        iters = strtol(env, NULL, 10);

    uint8_t buf[320];
    for (long i = 0; i < iters; i++) {
        size_t n = (size_t)(rnd() % sizeof(buf));
        if (n < 3)
            n = 3;
        for (size_t j = 0; j < n; j += 8) {
            uint64_t r = rnd();
            for (size_t k = 0; k < 8 && j + k < n; k++)
                buf[j + k] = (uint8_t)(r >> (k * 8));
        }
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_nbitq: %ld iterations, no failures\n", iters);
    return 0;
}
#endif
