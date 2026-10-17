/**
 * @file test_nbitq.c
 * @brief nbitq tests. Standalone -- no framework, no dependencies.
 *
 * The library is a few hundred lines whose entire job is to put a bit
 * somewhere and get it back, so the tests are exhaustive rather than
 * representative: every width, every varint base, every alignment. A bit
 * library that is right for 90% of widths is not useful.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbitq.h"

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

/* ── fixed-width round trip, every width, every alignment ───────────── */

static void test_u64_every_width_and_offset(void)
{
    static const uint64_t V[] = {0ull, 1ull, 2ull, 0x5555555555555555ull,
                                 0xAAAAAAAAAAAAAAAAull, 0x7FFFFFFFFFFFFFFFull,
                                 0x8000000000000000ull, UINT64_MAX, 0xDEADBEEFCAFEBABEull};
    uint8_t buf[32];

    for (unsigned n = 1; n <= 64; n++) {
        const uint64_t mask = (n == 64) ? UINT64_MAX : ((1ull << n) - 1ull);
        for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
            const uint64_t want = V[i] & mask;
            /* Every start offset within a byte, so no alignment is special. */
            for (unsigned off = 0; off < 8; off++) {
                memset(buf, 0xCC, sizeof(buf)); /* poison: prove we wrote it */
                CHECK(nbq_put_u64(buf, sizeof(buf) * 8, off, n, want) == NBQ_OK,
                      "put n=%u off=%u", n, off);
                uint64_t got = 0;
                CHECK(nbq_get_u64(buf, sizeof(buf) * 8, off, n, &got) == NBQ_OK, "get");
                CHECK(got == want, "n=%u off=%u: got %llu want %llu", n, off,
                      (unsigned long long)got, (unsigned long long)want);
            }
        }
    }
}

/* The 32-bit and hi/lo paths exist so a target without 64-bit arithmetic can
   be used. They are only worth anything if they produce identical bits. */
static void test_paths_agree(void)
{
    uint8_t a[32], b[32], c[32];
    for (unsigned n = 1; n <= 64; n++) {
        for (unsigned off = 0; off < 8; off++) {
            const uint64_t v = 0x0123456789ABCDEFull &
                               ((n == 64) ? UINT64_MAX : ((1ull << n) - 1ull));
            memset(a, 0, sizeof(a));
            memset(b, 0, sizeof(b));
            memset(c, 0, sizeof(c));

            CHECK(nbq_put_u64(a, sizeof(a) * 8, off, n, v) == NBQ_OK, "u64 put");
            nbq_u64_parts p = {(uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFFu)};
            CHECK(nbq_put_parts(b, sizeof(b) * 8, off, n, p) == NBQ_OK, "parts put");
            CHECK(memcmp(a, b, sizeof(a)) == 0, "parts differ from u64 at n=%u off=%u", n, off);

            if (n <= 32) {
                CHECK(nbq_put_u32(c, sizeof(c) * 8, off, n, (uint32_t)v) == NBQ_OK, "u32 put");
                CHECK(memcmp(a, c, sizeof(a)) == 0, "u32 differs from u64 at n=%u off=%u", n, off);
                uint32_t g32 = 0;
                CHECK(nbq_get_u32(a, sizeof(a) * 8, off, n, &g32) == NBQ_OK, "u32 get");
                CHECK((uint64_t)g32 == v, "u32 value n=%u", n);
            }
            nbq_u64_parts gp = {0, 0};
            CHECK(nbq_get_parts(a, sizeof(a) * 8, off, n, &gp) == NBQ_OK, "parts get");
            CHECK(gp.hi == p.hi && gp.lo == p.lo, "parts value n=%u", n);
        }
    }
}

/* ── sign extension ──────────────────────────────────────────────────── */

static void test_signed_round_trip(void)
{
    uint8_t buf[32];
    for (unsigned n = 2; n <= 64; n++) {
        const int64_t lo = (n == 64) ? INT64_MIN : -(1ll << (n - 1));
        const int64_t hi = (n == 64) ? INT64_MAX : ((1ll << (n - 1)) - 1);
        const int64_t vals[] = {lo, lo + 1, -1, 0, 1, hi};
        for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
            memset(buf, 0, sizeof(buf));
            CHECK(nbq_put_s64(buf, sizeof(buf) * 8, 3, n, vals[i]) == NBQ_OK, "put s64 n=%u", n);
            int64_t got = 0;
            CHECK(nbq_get_s64(buf, sizeof(buf) * 8, 3, n, &got) == NBQ_OK, "get s64");
            CHECK(got == vals[i], "s64 n=%u: got %lld want %lld", n, (long long)got,
                  (long long)vals[i]);
            if (n <= 32) {
                int32_t g32 = 0;
                CHECK(nbq_get_s32(buf, sizeof(buf) * 8, 3, n, &g32) == NBQ_OK, "get s32");
                CHECK((int64_t)g32 == vals[i], "s32 n=%u", n);
            }
        }
    }
}

/* All-ones in n bits is -1, not 2^n - 1. The predecessor library got the
   positional variant of this wrong. */
static void test_all_ones_is_negative_one(void)
{
    uint8_t buf[16];
    for (unsigned n = 1; n <= 64; n++) {
        memset(buf, 0, sizeof(buf));
        const uint64_t ones = (n == 64) ? UINT64_MAX : ((1ull << n) - 1ull);
        CHECK(nbq_put_u64(buf, sizeof(buf) * 8, 5, n, ones) == NBQ_OK, "put ones");
        int64_t got = 0;
        CHECK(nbq_get_s64(buf, sizeof(buf) * 8, 5, n, &got) == NBQ_OK, "get signed");
        CHECK(got == -1, "n=%u: all ones gave %lld", n, (long long)got);
    }
}

/* ── varints, every base ─────────────────────────────────────────────── */

static void test_varint_every_base(void)
{
    static const uint64_t V[] = {0, 1, 2, 3, 127, 128, 255, 256, 16383, 16384,
                                 65535, 0xFFFFFFFFull, 0x100000000ull, UINT64_MAX};
    uint8_t buf[64];

    for (unsigned base = NBQ_VARINT_MIN_BASE; base <= NBQ_VARINT_MAX_BASE; base++) {
        for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
            for (unsigned off = 0; off < 8; off++) {
                memset(buf, 0, sizeof(buf));
                uint64_t next_w = 0;
                CHECK(nbq_put_varint(buf, sizeof(buf) * 8, off, base, V[i], &next_w) == NBQ_OK,
                      "put varint base=%u", base);
                /* The size query must agree with what was written. */
                CHECK(next_w - off == nbq_varint_bits(V[i], base),
                      "size query disagrees at base=%u", base);

                uint64_t got = 0, next_r = 0;
                CHECK(nbq_get_varint(buf, sizeof(buf) * 8, off, base, &got, &next_r) == NBQ_OK,
                      "get varint base=%u", base);
                CHECK(got == V[i], "base=%u off=%u: got %llu want %llu", base, off,
                      (unsigned long long)got, (unsigned long long)V[i]);
                CHECK(next_r == next_w, "next_pos disagrees at base=%u", base);
            }
        }
    }
}

static void test_varint_signed_every_base(void)
{
    static const int64_t SV[] = {0, -1, 1, -2, 2, 127, -128, INT32_MAX, INT32_MIN,
                                 INT64_MAX, INT64_MIN};
    uint8_t buf[64];
    for (unsigned base = NBQ_VARINT_MIN_BASE; base <= NBQ_VARINT_MAX_BASE; base++) {
        for (size_t i = 0; i < sizeof(SV) / sizeof(SV[0]); i++) {
            memset(buf, 0, sizeof(buf));
            uint64_t next_w = 0;
            CHECK(nbq_put_varint_s(buf, sizeof(buf) * 8, 1, base, SV[i], &next_w) == NBQ_OK,
                  "put svarint base=%u", base);
            CHECK(next_w - 1 == nbq_varint_bits_s(SV[i], base),
                  "signed size query disagrees at base=%u", base);
            int64_t got = 0;
            CHECK(nbq_get_varint_s(buf, sizeof(buf) * 8, 1, base, &got, NULL) == NBQ_OK, "get");
            CHECK(got == SV[i], "base=%u: got %lld want %lld", base, (long long)got,
                  (long long)SV[i]);
        }
        /* zigzag symmetry: -1 costs what 0 costs, 1 costs what -2 costs */
        CHECK(nbq_varint_bits_s(-1, base) == nbq_varint_bits_s(0, base), "zigzag -1/0");
        CHECK(nbq_varint_bits_s(1, base) == nbq_varint_bits_s(-2, base), "zigzag 1/-2");
    }
}

static void test_varint_base8_is_leb128(void)
{
    /* 300 is 0xAC 0x02 in LEB128. If this changes, so does every file that
       ever used base 8. */
    uint8_t buf[8] = {0};
    CHECK(nbq_put_varint(buf, sizeof(buf) * 8, 0, 8, 300, NULL) == NBQ_OK, "put 300");
    CHECK(buf[0] == 0xAC && buf[1] == 0x02, "LEB128 of 300 is AC 02, got %02X %02X", buf[0],
          buf[1]);
}

static void test_varint_refusals(void)
{
    uint8_t buf[64];
    uint64_t v;

    memset(buf, 0, sizeof(buf));
    CHECK(nbq_put_varint(buf, sizeof(buf) * 8, 0, 1, 1, NULL) == NBQ_ERR_PARAM, "base 1");
    CHECK(nbq_put_varint(buf, sizeof(buf) * 8, 0, 65, 1, NULL) == NBQ_ERR_PARAM, "base 65");
    CHECK(nbq_varint_bits(1, 1) == 0, "size query base 1");
    CHECK(nbq_varint_bits(1, 65) == 0, "size query base 65");

    /* Overlong: 0x80 0x00 is zero in two base-8 groups. Accepting it lets two
       encoders produce two files for one value. */
    buf[0] = 0x80;
    buf[1] = 0x00;
    CHECK(nbq_get_varint(buf, 16, 0, 8, &v, NULL) == NBQ_ERR_MALFORMED, "overlong base 8");
    buf[0] = 0x80; /* 0b1000 then 0b0000 at base 4 */
    CHECK(nbq_get_varint(buf, 8, 0, 4, &v, NULL) == NBQ_ERR_MALFORMED, "overlong base 4");

    /* Wider than 64 bits: ten base-8 groups all carrying payload. */
    memset(buf, 0xFF, 9);
    buf[9] = 0x7F;
    CHECK(nbq_get_varint(buf, 80, 0, 8, &v, NULL) == NBQ_ERR_OVERFLOW, "70 payload bits");

    /* Continuation that never clears. */
    memset(buf, 0xFF, sizeof(buf));
    CHECK(nbq_get_varint(buf, sizeof(buf) * 8, 0, 8, &v, NULL) == NBQ_ERR_OVERFLOW,
          "unterminated");

    /* Truncation at every bit of a real varint is an error, never a value. */
    memset(buf, 0, sizeof(buf));
    uint64_t end = 0;
    nbq_put_varint(buf, sizeof(buf) * 8, 0, 8, 0xDEADBEEFull, &end);
    for (uint64_t cut = 0; cut < end; cut++)
        CHECK(nbq_get_varint(buf, cut, 0, 8, &v, NULL) != NBQ_OK, "truncated at %llu decoded",
              (unsigned long long)cut);
}

/* ── bounds: nothing may touch a bit past the limit ──────────────────── */

static void test_bounds_are_enforced(void)
{
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint64_t bits = 32;
    uint64_t v64;
    uint32_t v32;
    nbq_u64_parts p;

    /* Exactly reaching the end is fine; one bit past is not. */
    CHECK(nbq_get_u64(buf, bits, 24, 8, &v64) == NBQ_OK, "last byte");
    CHECK(nbq_get_u64(buf, bits, 25, 8, &v64) == NBQ_ERR_RANGE, "one past");
    CHECK(nbq_get_u64(buf, bits, 32, 1, &v64) == NBQ_ERR_RANGE, "at the end");
    CHECK(nbq_get_u32(buf, bits, 25, 8, &v32) == NBQ_ERR_RANGE, "u32 one past");
    CHECK(nbq_get_parts(buf, bits, 25, 8, &p) == NBQ_ERR_RANGE, "parts one past");

    /* A position far past the end, and one that would overflow pos+n. */
    CHECK(nbq_get_u64(buf, bits, 1000, 8, &v64) == NBQ_ERR_RANGE, "far past");
    CHECK(nbq_get_u64(buf, bits, UINT64_MAX - 2, 8, &v64) == NBQ_ERR_RANGE, "pos+n wraps");

    /* Widths out of range are refused, not clamped. */
    CHECK(nbq_get_u64(buf, bits, 0, 0, &v64) == NBQ_ERR_PARAM, "n=0");
    CHECK(nbq_get_u64(buf, bits, 0, 65, &v64) == NBQ_ERR_PARAM, "n=65");
    CHECK(nbq_get_u32(buf, bits, 0, 33, &v32) == NBQ_ERR_PARAM, "u32 n=33");
    CHECK(nbq_get_u64(NULL, bits, 0, 8, &v64) == NBQ_ERR_PARAM, "null buf");
    CHECK(nbq_get_u64(buf, bits, 0, 8, NULL) == NBQ_ERR_PARAM, "null out");

    /* A refused write must not have modified the buffer. */
    uint8_t w[2] = {0x00, 0x00};
    CHECK(nbq_put_u64(w, 16, 12, 8, 0xFFu) == NBQ_ERR_RANGE, "write past end refused");
    CHECK(w[0] == 0 && w[1] == 0, "refused write still modified the buffer");
}

/* ── bit arrays ──────────────────────────────────────────────────────── */

static void test_copy_bits_every_alignment(void)
{
    uint8_t src[16], dst[16], ref[16];
    for (size_t i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(0x5A ^ (i * 31));

    for (unsigned sp = 0; sp < 9; sp++) {
        for (unsigned dp = 0; dp < 9; dp++) {
            for (uint64_t n = 0; n <= 40; n += 7) {
                memset(dst, 0, sizeof(dst));
                CHECK(nbq_copy_bits(src, sizeof(src) * 8, sp, dst, sizeof(dst) * 8, dp, n) ==
                          NBQ_OK,
                      "copy sp=%u dp=%u n=%llu", sp, dp, (unsigned long long)n);
                /* Compare bit by bit against the source. */
                int cmp = 1;
                CHECK(nbq_compare_bits(src, sizeof(src) * 8, sp, dst, sizeof(dst) * 8, dp, n,
                                       &cmp) == NBQ_OK,
                      "compare");
                CHECK(cmp == 0, "copied bits differ: sp=%u dp=%u n=%llu", sp, dp,
                      (unsigned long long)n);
            }
        }
    }

    /* Overlapping within one buffer, shifting forward -- the case a naive
       forward loop corrupts. */
    memcpy(ref, src, sizeof(src));
    memcpy(dst, src, sizeof(src));
    CHECK(nbq_copy_bits(dst, sizeof(dst) * 8, 0, dst, sizeof(dst) * 8, 5, 60) == NBQ_OK,
          "overlapping copy");
    for (uint64_t i = 0; i < 60; i++) {
        uint64_t a = 0, b = 0;
        nbq_get_u64(ref, sizeof(ref) * 8, i, 1, &a);
        nbq_get_u64(dst, sizeof(dst) * 8, 5 + i, 1, &b);
        CHECK(a == b, "overlapping copy wrong at bit %llu", (unsigned long long)i);
    }
}

static void test_fill_compare_popcount(void)
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_fill_bits(buf, 64, 3, 20, 1) == NBQ_OK, "fill ones");
    uint64_t pc = 0;
    CHECK(nbq_popcount_bits(buf, 64, 0, 64, &pc) == NBQ_OK, "popcount");
    CHECK(pc == 20, "filled 20 bits, counted %llu", (unsigned long long)pc);
    CHECK(nbq_popcount_bits(buf, 64, 3, 20, &pc) == NBQ_OK, "popcount range");
    CHECK(pc == 20, "range popcount");
    CHECK(nbq_fill_bits(buf, 64, 3, 20, 0) == NBQ_OK, "fill zeros");
    CHECK(nbq_popcount_bits(buf, 64, 0, 64, &pc) == NBQ_OK, "popcount after clear");
    CHECK(pc == 0, "cleared");

    CHECK(nbq_fill_bits(buf, 64, 60, 8, 1) == NBQ_ERR_RANGE, "fill past end");
    CHECK(nbq_popcount_bits(buf, 64, 60, 8, &pc) == NBQ_ERR_RANGE, "popcount past end");

    /* compare orders most-significant bit first */
    uint8_t a[1] = {0x40}, b[1] = {0x80};
    int cmp = 0;
    CHECK(nbq_compare_bits(a, 8, 0, b, 8, 0, 8, &cmp) == NBQ_OK, "compare");
    CHECK(cmp < 0, "0x40 should sort below 0x80, got %d", cmp);
}

/* ── cursor: must agree with the indexed API, bit for bit ───────────── */

static void test_cursor_matches_indexed(void)
{
    uint8_t stack_buf[256];
    nbq_writer w;
    CHECK(nbq_writer_init(&w, stack_buf, sizeof(stack_buf) * 8) == NBQ_OK, "writer_init");
    memset(stack_buf, 0, sizeof(stack_buf));

    /* The motivating sequence from the 2000-era API: a 5-bit symbol, a 12-bit
       offset, a 7-bit value, then a varint, none byte-aligned. */
    CHECK(nbq_write_u32(&w, 5, 21) == NBQ_OK, "w 5");
    CHECK(nbq_write_u32(&w, 12, 3000) == NBQ_OK, "w 12");
    CHECK(nbq_write_s32(&w, 7, -19) == NBQ_OK, "w s7");
    CHECK(nbq_write_varint(&w, 5, 123456) == NBQ_OK, "w varint b5");
    CHECK(nbq_write_varint_s(&w, 8, -4242) == NBQ_OK, "w svarint b8");
    const uint64_t written = w.pos;

    nbq_reader r;
    CHECK(nbq_reader_init(&r, stack_buf, written) == NBQ_OK, "reader_init");
    uint32_t a, b;
    int32_t c;
    uint64_t d;
    int64_t e;
    CHECK(nbq_read_u32(&r, 5, &a) == NBQ_OK && a == 21, "r 5");
    CHECK(nbq_read_u32(&r, 12, &b) == NBQ_OK && b == 3000, "r 12");
    CHECK(nbq_read_s32(&r, 7, &c) == NBQ_OK && c == -19, "r s7");
    CHECK(nbq_read_varint(&r, 5, &d) == NBQ_OK && d == 123456, "r varint b5");
    CHECK(nbq_read_varint_s(&r, 8, &e) == NBQ_OK && e == -4242, "r svarint b8");
    CHECK(nbq_reader_remaining(&r) == 0, "consumed exactly what was written");

    /* The same values read with the indexed API at explicit positions. */
    uint64_t v = 0;
    CHECK(nbq_get_u64(stack_buf, written, 0, 5, &v) == NBQ_OK && v == 21, "indexed 5");
    CHECK(nbq_get_u64(stack_buf, written, 5, 12, &v) == NBQ_OK && v == 3000, "indexed 12");
}

/* A failed read must leave the cursor exactly where it was, or a caller can
   neither retry nor resynchronise. */
static void test_failed_reads_are_atomic(void)
{
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    nbq_reader r;
    nbq_reader_init(&r, buf, 32);
    nbq_reader_seek(&r, 28);

    uint64_t v;
    CHECK(nbq_read_u64(&r, 8, &v) == NBQ_ERR_RANGE, "read past end");
    CHECK(r.pos == 28, "cursor moved on a failed fixed read: %llu",
          (unsigned long long)r.pos);

    /* A varint spans groups; a truncated one must not consume the groups it
       managed to read. */
    uint8_t vb[8] = {0};
    uint64_t end = 0;
    nbq_put_varint(vb, 64, 0, 8, 1000000, &end); /* three groups */
    nbq_reader r2;
    nbq_reader_init(&r2, vb, end - 4); /* cut mid-varint */
    CHECK(nbq_read_varint(&r2, 8, &v) != NBQ_OK, "truncated varint");
    CHECK(r2.pos == 0, "cursor moved on a failed varint: %llu", (unsigned long long)r2.pos);
}

static void test_fixed_writer_never_grows(void)
{
    uint8_t small[2];
    memset(small, 0, sizeof(small));
    nbq_writer w;
    CHECK(nbq_writer_init(&w, small, 16) == NBQ_OK, "init");
    CHECK(nbq_write_u32(&w, 16, 0xBEEF) == NBQ_OK, "fill it");
    CHECK(nbq_write_u32(&w, 1, 1) == NBQ_ERR_RANGE, "one bit past capacity must fail");
    CHECK(w.pos == 16, "cursor moved past capacity");
    CHECK(small[0] == 0xBE && small[1] == 0xEF, "content");
    nbq_writer_free(&w); /* no-op on a non-owning writer */
}

static void test_allocating_writer_grows(void)
{
    nbq_writer w;
    CHECK(nbq_writer_init_alloc(&w, 8) == NBQ_OK, "init_alloc");
    for (unsigned i = 0; i < 5000; i++)
        CHECK(nbq_write_u32(&w, 13, i & 0x1FFF) == NBQ_OK, "write %u", i);
    CHECK(w.pos == 5000ull * 13ull, "position");

    nbq_reader r;
    nbq_reader_init(&r, w.buf, w.pos);
    for (unsigned i = 0; i < 5000; i++) {
        uint32_t got = 0;
        CHECK(nbq_read_u32(&r, 13, &got) == NBQ_OK, "read %u", i);
        CHECK(got == (i & 0x1FFF), "value %u", i);
    }
    nbq_writer_free(&w);
    CHECK(w.buf == NULL, "free cleared the buffer");
}

/* Backpatching: write a placeholder, keep going, come back and fill it in.
   This is what lets a one-pass encoder emit a length it does not yet know. */
static void test_writer_seek_backpatch(void)
{
    nbq_writer w;
    CHECK(nbq_writer_init_alloc(&w, 64) == NBQ_OK, "init");
    const uint64_t slot = w.pos;
    CHECK(nbq_write_u32(&w, 16, 0) == NBQ_OK, "placeholder");
    for (unsigned i = 0; i < 10; i++)
        CHECK(nbq_write_u32(&w, 7, i) == NBQ_OK, "body");
    const uint64_t body_bits = w.pos - slot - 16;

    const uint64_t resume = w.pos;
    CHECK(nbq_writer_seek(&w, slot) == NBQ_OK, "seek back");
    CHECK(nbq_write_u32(&w, 16, (uint32_t)body_bits) == NBQ_OK, "patch");
    CHECK(nbq_writer_seek(&w, resume) == NBQ_OK, "seek forward again");

    uint32_t patched = 0;
    CHECK(nbq_get_u32(w.buf, w.pos, slot, 16, &patched) == NBQ_OK, "read patched");
    CHECK(patched == body_bits, "patched %u want %llu", patched,
          (unsigned long long)body_bits);
    nbq_writer_free(&w);
}

static void test_alignment(void)
{
    nbq_writer w;
    CHECK(nbq_writer_init_alloc(&w, 64) == NBQ_OK, "init");
    CHECK(nbq_write_u32(&w, 3, 5) == NBQ_OK, "3 bits");
    CHECK(nbq_writer_align(&w, 8) == NBQ_OK, "align 8");
    CHECK(w.pos == 8, "aligned to 8, at %llu", (unsigned long long)w.pos);
    CHECK(nbq_writer_align(&w, 8) == NBQ_OK, "already aligned is a no-op");
    CHECK(w.pos == 8, "no-op moved the cursor");
    CHECK(nbq_write_u32(&w, 1, 1) == NBQ_OK, "1 bit");
    CHECK(nbq_writer_align(&w, 32) == NBQ_OK, "align 32");
    CHECK(w.pos == 32, "aligned to 32");

    nbq_reader r;
    nbq_reader_init(&r, w.buf, w.pos);
    uint32_t v;
    CHECK(nbq_read_u32(&r, 3, &v) == NBQ_OK, "read 3");
    CHECK(nbq_reader_align(&r, 8) == NBQ_OK, "reader align over zero padding");
    CHECK(r.pos == 8, "reader aligned");
    nbq_writer_free(&w);

    /* Padding that is not zero is not what a writer produced. */
    uint8_t bad[2] = {0xFF, 0xFF};
    nbq_reader r2;
    nbq_reader_init(&r2, bad, 16);
    nbq_reader_seek(&r2, 3);
    CHECK(nbq_reader_align(&r2, 8) == NBQ_ERR_MALFORMED, "nonzero padding must be refused");
}

static void test_strerror_covers_every_code(void)
{
    const nbq_result all[] = {NBQ_OK, NBQ_ERR_PARAM, NBQ_ERR_RANGE, NBQ_ERR_OVERFLOW,
                              NBQ_ERR_MALFORMED, NBQ_ERR_ALLOC};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        CHECK(strlen(nbq_strerror(all[i])) > 0, "strerror(%d)", (int)all[i]);
    CHECK(strlen(nbq_strerror((nbq_result)-999)) > 0, "strerror(unknown)");
}

static void test_version_is_self_reported(void)
{
    char expect[32];
    snprintf(expect, sizeof expect, "%d.%d.%d",
             NBQ_VERSION_MAJOR, NBQ_VERSION_MINOR, NBQ_VERSION_PATCH);
    CHECK(strcmp(NBQ_VERSION_STRING, expect) == 0, "NBQ_VERSION_STRING matches MAJOR.MINOR.PATCH");
    CHECK(strcmp(nbq_version_string(), NBQ_VERSION_STRING) == 0, "nbq_version_string()");
    CHECK(nbq_version() == (uint32_t)NBQ_VERSION, "nbq_version()");
    CHECK((nbq_version() >> 16) == (uint32_t)NBQ_VERSION_MAJOR, "nbq_version() major");
    CHECK(((nbq_version() >> 8) & 0xFFu) == (uint32_t)NBQ_VERSION_MINOR, "nbq_version() minor");
    CHECK((nbq_version() & 0xFFu) == (uint32_t)NBQ_VERSION_PATCH, "nbq_version() patch");
}

/* Floats are bit patterns: every class of value, including the ones that
   arithmetic would alter (NaN payloads, signalling NaNs, signed zero,
   denormals), must come back identical, at every offset within a byte, and
   the put must be byte-for-byte what nbq_put_u32/u64 of the pattern writes. */
static void test_float_is_a_bit_copy(void)
{
    static const uint32_t f32[] = {
        0x00000000u, 0x80000000u,              /* +0, -0 */
        0x3F800000u, 0xBF800000u, 0x40490FD0u, /* 1, -1, 3.14159 */
        0x00800000u, 0x00000001u, 0x807FFFFFu, /* FLT_MIN, smallest denormal, -largest denormal */
        0x7F7FFFFFu, 0x7F800000u, 0xFF800000u, /* FLT_MAX, +inf, -inf */
        0x7FC00000u, 0x7F800001u, 0xFFC0BEEFu  /* quiet NaN, signalling NaN, NaN with payload */
    };
    static const uint64_t f64[] = {
        0x0000000000000000ull, 0x8000000000000000ull,
        0x3FF0000000000000ull, 0xBFF0000000000000ull, 0x400921FB54442D18ull,
        0x0010000000000000ull, 0x0000000000000001ull, 0x800FFFFFFFFFFFFFull,
        0x7FEFFFFFFFFFFFFFull, 0x7FF0000000000000ull, 0xFFF0000000000000ull,
        0x7FF8000000000000ull, 0x7FF0000000000001ull, 0xFFF8DEADBEEF0001ull
    };
    uint8_t buf[32], ref[32];
    const uint64_t cap = sizeof(buf) * 8;

    for (size_t i = 0; i < sizeof(f32) / sizeof(f32[0]); i++) {
        for (uint64_t pos = 0; pos < 16; pos++) {
            float f = nbq_f32_from_bits(f32[i]), g = 0.0f;
            uint32_t u = 0;
            memset(buf, 0xA5, sizeof(buf));
            memset(ref, 0xA5, sizeof(ref));
            CHECK(nbq_f32_bits(f) == f32[i], "f32 pattern %08X survives the union", f32[i]);
            CHECK(nbq_put_f32(buf, cap, pos, f) == NBQ_OK, "put_f32 %08X at %llu", f32[i], (unsigned long long)pos);
            CHECK(nbq_put_u32(ref, cap, pos, 32, f32[i]) == NBQ_OK, "ref put");
            CHECK(memcmp(buf, ref, sizeof(buf)) == 0, "put_f32 == put_u32 of the pattern (%08X at %llu)", f32[i], (unsigned long long)pos);
            CHECK(nbq_get_f32(buf, cap, pos, &g) == NBQ_OK, "get_f32");
            CHECK(nbq_f32_bits(g) == f32[i], "get_f32 round trip %08X at %llu", f32[i], (unsigned long long)pos);
            CHECK(nbq_get_u32(buf, cap, pos, 32, &u) == NBQ_OK && u == f32[i], "get_u32 sees the pattern");
        }
    }
    for (size_t i = 0; i < sizeof(f64) / sizeof(f64[0]); i++) {
        for (uint64_t pos = 0; pos < 16; pos++) {
            double d = nbq_f64_from_bits(f64[i]), e = 0.0;
            memset(buf, 0x5A, sizeof(buf));
            memset(ref, 0x5A, sizeof(ref));
            CHECK(nbq_f64_bits(d) == f64[i], "f64 pattern %016llX survives the union", (unsigned long long)f64[i]);
            CHECK(nbq_put_f64(buf, cap, pos, d) == NBQ_OK, "put_f64");
            CHECK(nbq_put_u64(ref, cap, pos, 64, f64[i]) == NBQ_OK, "ref put");
            CHECK(memcmp(buf, ref, sizeof(buf)) == 0, "put_f64 == put_u64 of the pattern");
            CHECK(nbq_get_f64(buf, cap, pos, &e) == NBQ_OK, "get_f64");
            CHECK(nbq_f64_bits(e) == f64[i], "get_f64 round trip %016llX at %llu", (unsigned long long)f64[i], (unsigned long long)pos);
        }
    }

    /* Aligned, the bytes are big-endian IEEE-754, as a network peer expects. */
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_put_f32(buf, cap, 0, 3.14159f) == NBQ_OK, "put pi");
    CHECK(buf[0] == 0x40 && buf[1] == 0x49 && buf[2] == 0x0F && buf[3] == 0xD0, "3.14159f is 40 49 0F D0");
    CHECK(nbq_put_f64(buf, cap, 32, -2.5) == NBQ_OK, "put -2.5");
    CHECK(buf[4] == 0xC0 && buf[5] == 0x04 && buf[6] == 0 && buf[11] == 0, "-2.5 is C0 04 00 00 00 00 00 00");

    /* Cursor forms: advance by 32/64, and a failed read or write moves nothing. */
    {
        nbq_writer w;
        nbq_reader r;
        float g = 0.0f;
        double e = 0.0;
        memset(buf, 0, sizeof(buf));
        CHECK(nbq_writer_init(&w, buf, 100) == NBQ_OK, "writer 100 bits");
        CHECK(nbq_write_f32(&w, 1.0f) == NBQ_OK && w.pos == 32, "write_f32 advances 32");
        CHECK(nbq_write_f64(&w, -2.5) == NBQ_OK && w.pos == 96, "write_f64 advances 64");
        CHECK(nbq_write_f32(&w, 1.0f) == NBQ_ERR_RANGE && w.pos == 96, "write_f32 past cap refused, pos kept");
        CHECK(nbq_reader_init(&r, buf, 100) == NBQ_OK, "reader");
        CHECK(nbq_read_f32(&r, &g) == NBQ_OK && nbq_f32_bits(g) == 0x3F800000u && r.pos == 32, "read_f32");
        CHECK(nbq_read_f64(&r, &e) == NBQ_OK && nbq_f64_bits(e) == 0xC004000000000000ull && r.pos == 96, "read_f64");
        CHECK(nbq_read_f32(&r, &g) == NBQ_ERR_RANGE && r.pos == 96, "read_f32 past end refused, pos kept");
        CHECK(nbq_get_f32(NULL, cap, 0, &g) == NBQ_ERR_PARAM, "null buf");
        CHECK(nbq_get_f32(buf, 31, 0, &g) == NBQ_ERR_RANGE, "31 bits is not enough");
    }
}

int main(void)
{
    test_u64_every_width_and_offset();
    test_paths_agree();
    test_signed_round_trip();
    test_all_ones_is_negative_one();
    test_varint_every_base();
    test_varint_signed_every_base();
    test_varint_base8_is_leb128();
    test_varint_refusals();
    test_bounds_are_enforced();
    test_copy_bits_every_alignment();
    test_fill_compare_popcount();
    test_cursor_matches_indexed();
    test_failed_reads_are_atomic();
    test_fixed_writer_never_grows();
    test_allocating_writer_grows();
    test_writer_seek_backpatch();
    test_alignment();
    test_strerror_covers_every_code();
    test_version_is_self_reported();
    test_float_is_a_bit_copy();

    printf("nbitq: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
