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

/* ── every refusal, at every entry point ─────────────────────────────── */

/* A null pointer, a width or base the function cannot serve, and a field that
   ends past the buffer: each is refused with the documented code by every
   function that takes it. Written out in full rather than looped, so a
   function that forgets a check or returns the wrong code is named by the
   failing line. */
static void test_every_entry_point_refuses_bad_input(void)
{
    uint8_t buf[8] = {0};
    const uint64_t cap = sizeof(buf) * 8;
    uint64_t u64 = 0, next = 0;
    int64_t s64 = 0;
    uint32_t u32 = 0;
    int32_t s32 = 0;
    int cmp = 0;
    nbq_u64_parts p = {0, 0};
    nbq_reader r;
    nbq_writer w;

    /* null output */
    CHECK(nbq_get_s64(buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "get_s64 null out");
    CHECK(nbq_get_u32(buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "get_u32 null out");
    CHECK(nbq_get_s32(buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "get_s32 null out");
    CHECK(nbq_get_parts(buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "get_parts null out");
    CHECK(nbq_get_varint(buf, cap, 0, 8, NULL, &next) == NBQ_ERR_PARAM, "get_varint null out");
    CHECK(nbq_get_varint_s(buf, cap, 0, 8, NULL, &next) == NBQ_ERR_PARAM, "get_varint_s null out");
    CHECK(nbq_compare_bits(buf, cap, 0, buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "compare null out");
    CHECK(nbq_popcount_bits(buf, cap, 0, 8, NULL) == NBQ_ERR_PARAM, "popcount null out");

    /* null buffer */
    CHECK(nbq_get_u32(NULL, cap, 0, 8, &u32) == NBQ_ERR_PARAM, "get_u32 null buf");
    CHECK(nbq_get_parts(NULL, cap, 0, 8, &p) == NBQ_ERR_PARAM, "get_parts null buf");
    CHECK(nbq_put_u64(NULL, cap, 0, 8, 1) == NBQ_ERR_PARAM, "put_u64 null buf");
    CHECK(nbq_put_u32(NULL, cap, 0, 8, 1) == NBQ_ERR_PARAM, "put_u32 null buf");
    CHECK(nbq_put_parts(NULL, cap, 0, 8, p) == NBQ_ERR_PARAM, "put_parts null buf");
    CHECK(nbq_get_varint(NULL, cap, 0, 8, &u64, &next) == NBQ_ERR_PARAM, "get_varint null buf");
    CHECK(nbq_put_varint(NULL, cap, 0, 8, 1, &next) == NBQ_ERR_PARAM, "put_varint null buf");
    CHECK(nbq_copy_bits(NULL, cap, 0, buf, cap, 0, 8) == NBQ_ERR_PARAM, "copy null src");
    CHECK(nbq_copy_bits(buf, cap, 0, NULL, cap, 0, 8) == NBQ_ERR_PARAM, "copy null dst");
    CHECK(nbq_fill_bits(NULL, cap, 0, 8, 1) == NBQ_ERR_PARAM, "fill null buf");
    CHECK(nbq_compare_bits(NULL, cap, 0, buf, cap, 0, 8, &cmp) == NBQ_ERR_PARAM, "compare null a");
    CHECK(nbq_popcount_bits(NULL, cap, 0, 8, &u64) == NBQ_ERR_PARAM, "popcount null buf");
    CHECK(nbq_reader_init(NULL, buf, cap) == NBQ_ERR_PARAM, "reader_init null reader");
    CHECK(nbq_reader_init(&r, NULL, cap) == NBQ_ERR_PARAM, "reader_init null buf");
    CHECK(nbq_writer_init(NULL, buf, cap) == NBQ_ERR_PARAM, "writer_init null writer");
    CHECK(nbq_writer_init(&w, NULL, cap) == NBQ_ERR_PARAM, "writer_init null buf");

    /* width 0, and one past the widest */
    CHECK(nbq_put_u64(buf, cap, 0, 65, 1) == NBQ_ERR_PARAM, "put_u64 n=65");
    CHECK(nbq_put_s64(buf, cap, 0, 0, -1) == NBQ_ERR_PARAM, "put_s64 n=0");
    CHECK(nbq_put_s64(buf, cap, 0, 65, -1) == NBQ_ERR_PARAM, "put_s64 n=65");
    CHECK(nbq_put_u32(buf, cap, 0, 33, 1) == NBQ_ERR_PARAM, "put_u32 n=33");
    CHECK(nbq_put_s32(buf, cap, 0, 0, -1) == NBQ_ERR_PARAM, "put_s32 n=0");
    CHECK(nbq_put_s32(buf, cap, 0, 33, -1) == NBQ_ERR_PARAM, "put_s32 n=33");
    CHECK(nbq_get_varint(buf, cap, 0, 1, &u64, &next) == NBQ_ERR_PARAM, "get_varint base 1");
    CHECK(nbq_get_varint(buf, cap, 0, 65, &u64, &next) == NBQ_ERR_PARAM, "get_varint base 65");

    /* a field that ends one bit past the buffer; the signed forms pass the
       code through from the unsigned ones they call */
    CHECK(nbq_get_s64(buf, cap, cap - 7, 8, &s64) == NBQ_ERR_RANGE, "get_s64 range");
    CHECK(nbq_get_s32(buf, cap, cap - 7, 8, &s32) == NBQ_ERR_RANGE, "get_s32 range");
    CHECK(nbq_put_u32(buf, cap, cap - 7, 8, 1) == NBQ_ERR_RANGE, "put_u32 range");
    CHECK(nbq_put_parts(buf, cap, cap - 7, 8, p) == NBQ_ERR_RANGE, "put_parts range");
    CHECK(nbq_put_varint(buf, cap, cap - 7, 8, 1, &next) == NBQ_ERR_RANGE, "put_varint range");
    CHECK(nbq_copy_bits(buf, cap, cap - 7, buf, cap, 0, 8) == NBQ_ERR_RANGE, "copy src range");
    CHECK(nbq_compare_bits(buf, cap, cap - 7, buf, cap, 0, 8, &cmp) == NBQ_ERR_RANGE,
          "compare range");

    /* n == 0 is a no-op, not an error, and touches nothing */
    memset(buf, 0xA5, sizeof(buf));
    CHECK(nbq_fill_bits(buf, cap, 3, 0, 1) == NBQ_OK, "fill n=0");
    CHECK(buf[0] == 0xA5, "fill n=0 wrote nothing");
    CHECK(nbq_copy_bits(buf, cap, 0, buf, cap, 3, 0) == NBQ_OK, "copy n=0");
    CHECK(buf[0] == 0xA5, "copy n=0 wrote nothing");
}

/* ── the cursor forms the exhaustive tests above do not reach ─────────── */

static void test_cursor_every_type(void)
{
    uint8_t buf[32] = {0};
    uint8_t src[4] = {0xA5, 0x5A, 0xFF, 0x00};
    uint8_t dst[4] = {0};
    nbq_writer w;
    nbq_reader r;
    nbq_u64_parts p = {0x12345u, 0xDEADBEEFu}, q = {0, 0};
    int64_t s = 0;
    uint64_t u = 0;
    int cmp = 1;

    CHECK(nbq_writer_init(&w, buf, sizeof(buf) * 8) == NBQ_OK, "writer_init");
    CHECK(nbq_write_s64(&w, 40, -123456789012LL) == NBQ_OK, "write_s64");
    CHECK(nbq_write_parts(&w, 52, p) == NBQ_OK, "write_parts");
    CHECK(nbq_write_bits(&w, src, 32, 4, 20) == NBQ_OK, "write_bits");
    CHECK(w.pos == 40 + 52 + 20, "pos after three writes: %llu", (unsigned long long)w.pos);

    CHECK(nbq_reader_init(&r, buf, w.pos) == NBQ_OK, "reader_init");
    CHECK(nbq_read_s64(&r, 40, &s) == NBQ_OK && s == -123456789012LL, "read_s64");
    CHECK(nbq_read_parts(&r, 52, &q) == NBQ_OK && q.hi == 0x12345u && q.lo == 0xDEADBEEFu,
          "read_parts: %llx %llx", (unsigned long long)q.hi, (unsigned long long)q.lo);
    CHECK(nbq_read_bits(&r, dst, 32, 4, 20) == NBQ_OK, "read_bits");
    CHECK(nbq_compare_bits(src, 32, 4, dst, 32, 4, 20, &cmp) == NBQ_OK && cmp == 0,
          "read_bits copied the run");
    CHECK(r.pos == w.pos && nbq_reader_remaining(&r) == 0, "reader at the end");

    /* a failed read or write of a run leaves the cursor where it was */
    CHECK(nbq_reader_seek(&r, w.pos - 10) == NBQ_OK, "seek back 10");
    CHECK(nbq_read_bits(&r, dst, 32, 0, 20) == NBQ_ERR_RANGE && r.pos == w.pos - 10,
          "read_bits past the end");
    CHECK(nbq_writer_seek(&w, sizeof(buf) * 8 - 10) == NBQ_OK, "seek near the end");
    CHECK(nbq_write_bits(&w, src, 32, 0, 20) == NBQ_ERR_RANGE && w.pos == sizeof(buf) * 8 - 10,
          "write_bits past the end");
    CHECK(nbq_write_varint(&w, 16, 1) == NBQ_ERR_RANGE && w.pos == sizeof(buf) * 8 - 10,
          "write_varint past the end (16 bits into 10)");
    CHECK(nbq_write_varint(&w, 1, 1) == NBQ_ERR_PARAM, "write_varint base 1");

    /* null cursors */
    CHECK(nbq_read_varint(NULL, 8, &u) == NBQ_ERR_PARAM, "read_varint null");
    CHECK(nbq_read_varint_s(NULL, 8, &s) == NBQ_ERR_PARAM, "read_varint_s null");
    CHECK(nbq_read_bits(NULL, dst, 32, 0, 8) == NBQ_ERR_PARAM, "read_bits null");
    CHECK(nbq_write_varint(NULL, 8, 1) == NBQ_ERR_PARAM, "write_varint null");
    CHECK(nbq_write_bits(NULL, src, 32, 0, 8) == NBQ_ERR_PARAM, "write_bits null");
}

static void test_remaining_seek_and_align_edges(void)
{
    uint8_t buf[4] = {0};
    nbq_reader r;
    nbq_writer w;

    CHECK(nbq_reader_init(&r, buf, 30) == NBQ_OK, "reader over 30 bits");
    CHECK(nbq_reader_remaining(&r) == 30, "remaining at start");
    CHECK(nbq_reader_seek(&r, 12) == NBQ_OK && nbq_reader_remaining(&r) == 18,
          "remaining after seek");
    CHECK(nbq_reader_seek(&r, 31) == NBQ_ERR_RANGE && r.pos == 12, "seek past the end refused");
    CHECK(nbq_reader_seek(&r, 30) == NBQ_OK && nbq_reader_remaining(&r) == 0,
          "seek to the end allowed");
    CHECK(nbq_reader_seek(NULL, 0) == NBQ_ERR_PARAM, "reader_seek null");
    CHECK(nbq_reader_remaining(NULL) == 0, "remaining of null is 0");

    CHECK(nbq_reader_seek(&r, 16) == NBQ_OK && nbq_reader_align(&r, 8) == NBQ_OK && r.pos == 16,
          "align when already aligned is a no-op");
    CHECK(nbq_reader_seek(&r, 26) == NBQ_OK && nbq_reader_align(&r, 8) == NBQ_ERR_RANGE &&
              r.pos == 26,
          "align whose padding would cross the end");
    CHECK(nbq_reader_align(NULL, 8) == NBQ_ERR_PARAM, "reader_align null");
    CHECK(nbq_reader_align(&r, 0) == NBQ_ERR_PARAM, "reader_align n=0");

    CHECK(nbq_writer_init(&w, buf, 30) == NBQ_OK, "writer over 30 bits");
    CHECK(nbq_writer_align(NULL, 8) == NBQ_ERR_PARAM, "writer_align null");
    CHECK(nbq_writer_align(&w, 0) == NBQ_ERR_PARAM, "writer_align n=0");
    CHECK(nbq_writer_seek(&w, 26) == NBQ_OK && nbq_writer_align(&w, 8) == NBQ_ERR_RANGE &&
              w.pos == 26,
          "writer align past a fixed capacity");
    CHECK(nbq_writer_seek(NULL, 0) == NBQ_ERR_PARAM, "writer_seek null");
    CHECK(nbq_writer_seek(&w, 31) == NBQ_ERR_RANGE && w.pos == 26,
          "fixed writer: seek past capacity");
}

#ifndef NBQ_NO_MALLOC
/* The allocating writer's limits: what it refuses, and that a refusal leaves
   the writer usable. The impossible sizes are real requests the allocator
   turns down. Under ASan that needs allocator_may_return_null=1, which the
   Makefile and CMakeLists.txt set for these tests. */
static void test_allocating_writer_limits(void)
{
    nbq_writer w;
    uint8_t src[1] = {0xFF};
    uint32_t v = 0;

    CHECK(nbq_writer_init_alloc(NULL, 64) == NBQ_ERR_PARAM, "init_alloc null");

    CHECK(nbq_writer_init_alloc(&w, 0) == NBQ_OK, "0 bits means a default size");
    CHECK(w.cap == 512 && w.owns_buf, "default is 512 bits");
    nbq_writer_free(&w);
    CHECK(w.buf == NULL && w.cap == 0 && !w.owns_buf, "free clears the writer");
    nbq_writer_free(&w); /* a second free is a no-op */
    nbq_writer_free(NULL);

    CHECK(nbq_writer_init_alloc(&w, UINT64_MAX) == NBQ_ERR_ALLOC, "an impossible size is refused");

    CHECK(nbq_writer_init_alloc(&w, 64) == NBQ_OK, "init_alloc 64");
    CHECK(nbq_write_u32(&w, 32, 0xC0FFEE) == NBQ_OK, "write before growing");
    /* a run so long that doubling the capacity would overflow */
    CHECK(nbq_write_bits(&w, src, 8, 0, ((uint64_t)1 << 63) + 1u) == NBQ_ERR_ALLOC,
          "growth that would overflow");
    /* a run the allocator cannot satisfy */
    CHECK(nbq_write_bits(&w, src, 8, 0, (uint64_t)1 << 61) == NBQ_ERR_ALLOC, "growth refused");
    CHECK(w.pos == 32 && w.cap == 64, "a refused growth changes nothing");
    CHECK(nbq_get_u32(w.buf, w.cap, 0, 32, &v) == NBQ_OK && v == 0xC0FFEE, "the buffer is intact");

    /* seeking past the capacity of an owning writer grows it */
    CHECK(nbq_writer_seek(&w, 1000) == NBQ_OK && w.pos == 1000 && w.cap >= 1000, "seek grows");
    CHECK(nbq_write_u32(&w, 32, 7) == NBQ_OK, "write after the seek");
    CHECK(nbq_writer_seek(&w, (uint64_t)1 << 61) == NBQ_ERR_ALLOC && w.pos == 1032,
          "a seek that cannot be satisfied leaves the position");
    nbq_writer_free(&w);
}
#endif


/* ════════════════════════════════════════════════════════════════════════
 * Bit order
 * ════════════════════════════════════════════════════════════════════════
 *
 * The oracle: a buffer expanded to one character per bit, in stream order.
 * MSB first, each byte contributes its bits from the top down; LSB first,
 * from the bottom up. A member is then a substring, and its value is read
 * from that substring in the order's direction: MSB first the first
 * character is the top bit, LSB first it is bit 0. There are no shifts over
 * a buffer and no masks, so it is right by inspection, and the library is
 * compared against it in both orders at every width and offset. The orders
 * are then tied to each other by a mirror identity, and the LSB-first path
 * to the outside world by DEFLATE blocks that zlib produced.
 */

#define ORACLE_BYTES 32
typedef char oracle[ORACLE_BYTES * 8 + 1];

static void oracle_expand(const uint8_t *buf, nbq_bit_order order, oracle s)
{
    for (size_t i = 0; i < ORACLE_BYTES; i++)
        for (unsigned b = 0; b < 8; b++) {
            const unsigned sh = (order == NBQ_LSB_FIRST) ? b : 7u - b;
            s[i * 8 + b] = ((buf[i] >> sh) & 1u) ? '1' : '0';
        }
    s[ORACLE_BYTES * 8] = '\0';
}

static void oracle_pack(const oracle s, nbq_bit_order order, uint8_t *buf)
{
    for (size_t i = 0; i < ORACLE_BYTES; i++) {
        unsigned v = 0;
        for (unsigned b = 0; b < 8; b++) {
            const unsigned sh = (order == NBQ_LSB_FIRST) ? b : 7u - b;
            if (s[i * 8 + b] == '1')
                v |= 1u << sh;
        }
        buf[i] = (uint8_t)v;
    }
}

static uint64_t oracle_value(const oracle s, uint64_t pos, unsigned n, nbq_bit_order order)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint64_t bit = (s[pos + i] == '1') ? 1u : 0u;
        if (order == NBQ_LSB_FIRST)
            v |= bit << i; /* the first character is bit 0 */
        else
            v = (v << 1) | bit; /* the first character is the top bit */
    }
    return v;
}

static void oracle_write(oracle s, uint64_t pos, unsigned n, uint64_t v, nbq_bit_order order)
{
    for (unsigned i = 0; i < n; i++) {
        const unsigned src = (order == NBQ_LSB_FIRST) ? i : n - 1u - i;
        s[pos + i] = ((v >> src) & 1u) ? '1' : '0';
    }
}

/* The public names for an order, so one loop can run both. The MSB-first
   side goes through the _be spellings, which proves those aliases exist. */
#define BY_ORDER(o, fn, ...) ((o) == NBQ_LSB_FIRST ? fn##_le(__VA_ARGS__) : fn##_be(__VA_ARGS__))

static const nbq_bit_order ORDERS[] = {NBQ_MSB_FIRST, NBQ_LSB_FIRST};
static const char *const ORDER_NAME[] = {"msb", "lsb"};

static uint64_t bitrev_n(uint64_t v, unsigned n)
{
    uint64_t r = 0;
    for (unsigned i = 0; i < n; i++)
        r |= ((v >> i) & 1u) << (n - 1u - i);
    return r;
}

static uint8_t bitrev8(uint8_t b)
{
    return (uint8_t)bitrev_n(b, 8);
}

static uint64_t sign_extend(uint64_t raw, unsigned n)
{
    if (n < 64u && ((raw >> (n - 1u)) & 1u))
        raw |= ~((((uint64_t)1) << n) - 1u);
    return raw;
}

static void test_both_orders_against_the_oracle(void)
{
    static const uint64_t V[] = {0ull, 1ull, 2ull, 0x5555555555555555ull,
                                 0xAAAAAAAAAAAAAAAAull, 0x7FFFFFFFFFFFFFFFull,
                                 0x8000000000000000ull, UINT64_MAX, 0xDEADBEEFCAFEBABEull};
    uint8_t buf[ORACLE_BYTES], poison[ORACLE_BYTES];
    const uint64_t cap = ORACLE_BYTES * 8;
    oracle s, p;

    /* a poison that is not symmetric under bit reversal, so a field written
       in the wrong order cannot look right by accident */
    for (size_t i = 0; i < sizeof(poison); i++)
        poison[i] = (uint8_t)(0x1Bu + 37u * i);

    for (size_t oi = 0; oi < 2; oi++) {
        const nbq_bit_order order = ORDERS[oi];
        const char *name = ORDER_NAME[oi];
        for (unsigned n = 1; n <= 64; n++) {
            const uint64_t mask = (n == 64) ? UINT64_MAX : ((1ull << n) - 1ull);
            for (size_t vi = 0; vi < sizeof(V) / sizeof(V[0]); vi++) {
                const uint64_t want = V[vi] & mask;
                for (unsigned off = 0; off < 8; off++) {
                    uint64_t got = 0;
                    int64_t sgot = 0;
                    nbq_u64_parts parts = {0, 0};

                    /* put: the library writes, the oracle reads; and nothing
                       outside the field moved */
                    memcpy(buf, poison, sizeof(buf));
                    CHECK(BY_ORDER(order, nbq_put_u64, buf, cap, off, n, want) == NBQ_OK,
                          "%s put_u64 n=%u off=%u", name, n, off);
                    oracle_expand(buf, order, s);
                    CHECK(oracle_value(s, off, n, order) == want,
                          "%s put_u64 n=%u off=%u: oracle reads %llx want %llx", name, n, off,
                          (unsigned long long)oracle_value(s, off, n, order),
                          (unsigned long long)want);
                    oracle_expand(poison, order, p);
                    oracle_write(p, off, n, want, order);
                    CHECK(memcmp(s, p, sizeof(s)) == 0, "%s put_u64 n=%u off=%u touched other bits",
                          name, n, off);

                    /* get: the oracle writes, the library reads, by every path */
                    oracle_expand(poison, order, s);
                    oracle_write(s, off, n, want, order);
                    oracle_pack(s, order, buf);
                    CHECK(BY_ORDER(order, nbq_get_u64, buf, cap, off, n, &got) == NBQ_OK &&
                              got == want,
                          "%s get_u64 n=%u off=%u: %llx want %llx", name, n, off,
                          (unsigned long long)got, (unsigned long long)want);
                    CHECK(BY_ORDER(order, nbq_get_parts, buf, cap, off, n, &parts) == NBQ_OK &&
                              parts.hi == (uint32_t)(want >> 32) && parts.lo == (uint32_t)want,
                          "%s get_parts n=%u off=%u", name, n, off);
                    CHECK(BY_ORDER(order, nbq_get_s64, buf, cap, off, n, &sgot) == NBQ_OK &&
                              (uint64_t)sgot == sign_extend(want, n),
                          "%s get_s64 n=%u off=%u", name, n, off);

                    /* the other writers produce the same bits as put_u64 */
                    memcpy(buf, poison, sizeof(buf));
                    CHECK(BY_ORDER(order, nbq_put_parts, buf, cap, off, n, parts) == NBQ_OK,
                          "%s put_parts", name);
                    oracle_expand(buf, order, p);
                    CHECK(memcmp(s, p, sizeof(s)) == 0, "%s put_parts n=%u off=%u differs", name, n,
                          off);
                    memcpy(buf, poison, sizeof(buf));
                    CHECK(BY_ORDER(order, nbq_put_s64, buf, cap, off, n, (int64_t)want) == NBQ_OK,
                          "%s put_s64", name);
                    oracle_expand(buf, order, p);
                    CHECK(memcmp(s, p, sizeof(s)) == 0, "%s put_s64 n=%u off=%u differs", name, n,
                          off);

                    if (n <= 32) {
                        uint32_t g32 = 0;
                        int32_t s32 = 0;
                        oracle_pack(s, order, buf);
                        CHECK(BY_ORDER(order, nbq_get_u32, buf, cap, off, n, &g32) == NBQ_OK &&
                                  g32 == (uint32_t)want,
                              "%s get_u32 n=%u off=%u", name, n, off);
                        CHECK(BY_ORDER(order, nbq_get_s32, buf, cap, off, n, &s32) == NBQ_OK &&
                                  (uint32_t)s32 == (uint32_t)sign_extend(want, n),
                              "%s get_s32 n=%u off=%u", name, n, off);
                        memcpy(buf, poison, sizeof(buf));
                        CHECK(BY_ORDER(order, nbq_put_u32, buf, cap, off, n, (uint32_t)want) ==
                                  NBQ_OK,
                              "%s put_u32", name);
                        oracle_expand(buf, order, p);
                        CHECK(memcmp(s, p, sizeof(s)) == 0, "%s put_u32 n=%u off=%u differs", name,
                              n, off);
                        memcpy(buf, poison, sizeof(buf));
                        CHECK(BY_ORDER(order, nbq_put_s32, buf, cap, off, n, (int32_t)want) ==
                                  NBQ_OK,
                              "%s put_s32", name);
                        oracle_expand(buf, order, p);
                        CHECK(memcmp(s, p, sizeof(s)) == 0, "%s put_s32 n=%u off=%u differs", name,
                              n, off);
                    }
                }
            }
        }
    }
}

/* The two orders are mirror images: reading byte-reversed data LSB first
   gives the bit-reversal of the MSB-first read, at every width and offset,
   and writing likewise. This ties the orders to each other independently of
   the oracle. */
static void test_orders_are_mirror_images(void)
{
    uint8_t a[ORACLE_BYTES], ra[ORACLE_BYTES], b[ORACLE_BYTES], rb[ORACLE_BYTES];
    const uint64_t cap = ORACLE_BYTES * 8;
    uint64_t seed = 0x9E3779B97F4A7C15ull;

    for (size_t i = 0; i < sizeof(a); i++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        a[i] = (uint8_t)(seed >> 56);
        ra[i] = bitrev8(a[i]);
    }

    for (unsigned n = 1; n <= 64; n++) {
        for (unsigned off = 0; off < 16; off++) {
            uint64_t le = 0, be = 0;
            CHECK(nbq_get_u64_le(a, cap, off, n, &le) == NBQ_OK, "le read");
            CHECK(nbq_get_u64_be(ra, cap, off, n, &be) == NBQ_OK, "be read of reversed");
            CHECK(le == bitrev_n(be, n), "mirror read n=%u off=%u: le %llx be %llx", n, off,
                  (unsigned long long)le, (unsigned long long)be);

            memcpy(b, a, sizeof(b));
            memcpy(rb, ra, sizeof(rb));
            CHECK(nbq_put_u64_le(b, cap, off, n, le ^ 0x5A5A5A5A5A5A5A5Aull) == NBQ_OK, "le put");
            CHECK(nbq_put_u64_be(rb, cap, off, n, bitrev_n(le ^ 0x5A5A5A5A5A5A5A5Aull, n)) ==
                      NBQ_OK,
                  "be put");
            int same = 1;
            for (size_t i = 0; i < sizeof(b); i++)
                same = same && bitrev8(b[i]) == rb[i];
            CHECK(same, "mirror write n=%u off=%u", n, off);
        }
    }
}

/* Raw DEFLATE as zlib emits it: an LSB-first stream, with a quirk that is
   the best possible test of bit order. Huffman codes are packed starting
   from their most significant bit, so inside an LSB-first stream each code
   appears bit-reversed. A reader with the wrong order gets every field
   wrong; one with the right order gets the literal 'h' as 0x19, which is
   its 8-bit fixed code 0x98 reversed.

   Bytes from Python: zlib.compressobj(level, zlib.DEFLATED, -15) over
   b"hello", level 0 for the stored block and 9 for the fixed one. */
static void test_deflate_blocks_from_zlib(void)
{
    static const uint8_t stored[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};
    static const uint8_t fixed[] = {0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00};
    static const uint32_t codes[] = {0x19, 0xA9, 0x39, 0x39, 0xF9}; /* h e l l o, reversed */
    uint8_t out[16];
    nbq_reader r;
    nbq_writer w;
    uint32_t v = 99;

    /* stored block: BFINAL=1, BTYPE=00, pad to a byte, LEN, NLEN, bytes */
    CHECK(nbq_reader_init_le(&r, stored, sizeof(stored) * 8) == NBQ_OK, "init_le");
    CHECK(r.order == NBQ_LSB_FIRST, "reader order");
    CHECK(nbq_read_u32(&r, 1, &v) == NBQ_OK && v == 1, "BFINAL");
    CHECK(nbq_read_u32(&r, 2, &v) == NBQ_OK && v == 0, "BTYPE stored");
    CHECK(nbq_reader_align(&r, 8) == NBQ_OK && r.pos == 8, "pad to byte");
    CHECK(nbq_read_u32(&r, 16, &v) == NBQ_OK && v == 5, "LEN = 5 (little-endian bytes)");
    CHECK(nbq_read_u32(&r, 16, &v) == NBQ_OK && v == 0xFFFA, "NLEN = ~5");
    for (int i = 0; i < 5; i++)
        CHECK(nbq_read_u32(&r, 8, &v) == NBQ_OK && v == (uint32_t)"hello"[i], "byte %d", i);
    CHECK(nbq_reader_remaining(&r) == 0, "consumed exactly");

    /* the indexed _le forms read the same fields */
    CHECK(nbq_get_u32_le(stored, 80, 8, 16, &v) == NBQ_OK && v == 5, "indexed LEN");
    CHECK(nbq_get_u32_le(stored, 80, 24, 16, &v) == NBQ_OK && v == 0xFFFA, "indexed NLEN");

    /* the same bytes MSB first are different numbers, as they must be */
    CHECK(nbq_get_u32(stored, 80, 0, 1, &v) == NBQ_OK && v == 0, "MSB-first sees BFINAL as 0");

    /* and the LSB-first writer reproduces zlib's bytes exactly */
    memset(out, 0, sizeof(out));
    CHECK(nbq_writer_init_le(&w, out, sizeof(out) * 8) == NBQ_OK, "writer_init_le");
    CHECK(w.order == NBQ_LSB_FIRST, "writer order");
    CHECK(nbq_write_u32(&w, 1, 1) == NBQ_OK && nbq_write_u32(&w, 2, 0) == NBQ_OK, "header");
    CHECK(nbq_writer_align(&w, 8) == NBQ_OK && w.pos == 8, "pad");
    CHECK(nbq_write_u32(&w, 16, 5) == NBQ_OK && nbq_write_u32(&w, 16, 0xFFFA) == NBQ_OK, "lengths");
    for (int i = 0; i < 5; i++)
        CHECK(nbq_write_u32(&w, 8, (uint32_t)"hello"[i]) == NBQ_OK, "byte %d", i);
    CHECK(w.pos == 80 && memcmp(out, stored, sizeof(stored)) == 0,
          "stored block reproduced byte for byte");

    /* fixed-Huffman block: BFINAL=1, BTYPE=01, then bit-reversed codes */
    CHECK(nbq_reader_init_le(&r, fixed, sizeof(fixed) * 8) == NBQ_OK, "init_le fixed");
    CHECK(nbq_read_u32(&r, 1, &v) == NBQ_OK && v == 1, "BFINAL");
    CHECK(nbq_read_u32(&r, 2, &v) == NBQ_OK && v == 1, "BTYPE fixed");
    for (int i = 0; i < 5; i++)
        CHECK(nbq_read_u32(&r, 8, &v) == NBQ_OK && v == codes[i], "code %d: %x want %x", i, v,
              codes[i]);
    CHECK(nbq_read_u32(&r, 7, &v) == NBQ_OK && v == 0, "end-of-block code 0000000");
    CHECK(nbq_reader_remaining(&r) == 6, "six bits of padding left");
    CHECK(nbq_reader_align(&r, 8) == NBQ_OK && nbq_reader_remaining(&r) == 0, "padding is zero");

    memset(out, 0, sizeof(out));
    CHECK(nbq_writer_init_le(&w, out, sizeof(out) * 8) == NBQ_OK, "writer_init_le");
    CHECK(nbq_write_u32(&w, 1, 1) == NBQ_OK && nbq_write_u32(&w, 2, 1) == NBQ_OK, "header");
    for (int i = 0; i < 5; i++)
        CHECK(nbq_write_u32(&w, 8, codes[i]) == NBQ_OK, "code %d", i);
    CHECK(nbq_write_u32(&w, 7, 0) == NBQ_OK && nbq_writer_align(&w, 8) == NBQ_OK, "EOB and pad");
    CHECK(w.pos == 56 && memcmp(out, fixed, sizeof(fixed)) == 0,
          "fixed block reproduced byte for byte");
}

static void test_le_varints(void)
{
    static const uint64_t V[] = {0ull, 1ull, 7ull, 8ull, 127ull, 128ull, 300ull, 16383ull,
                                 16384ull, 0xDEADBEEFull, 0x7FFFFFFFFFFFFFFFull, UINT64_MAX};
    uint8_t buf[ORACLE_BYTES];
    const uint64_t cap = ORACLE_BYTES * 8;
    oracle s;

    for (unsigned base = 2; base <= 64; base++) {
        const unsigned payload = base - 1;
        for (size_t vi = 0; vi < sizeof(V) / sizeof(V[0]); vi++) {
            const uint64_t want = V[vi];
            const unsigned bits = nbq_varint_bits(want, base);
            if (bits > 200)
                continue;
            for (unsigned off = 0; off < 8; off++) {
                uint64_t got = 0, next = 0, acc = 0;
                unsigned shift = 0, g = 0;
                int done = 0;

                memset(buf, 0, sizeof(buf));
                CHECK(nbq_put_varint_le(buf, cap, off, base, want, &next) == NBQ_OK &&
                          next == off + bits,
                      "put_varint_le base=%u v=%llu off=%u", base, (unsigned long long)want, off);
                CHECK(nbq_get_varint_le(buf, cap, off, base, &got, &next) == NBQ_OK &&
                          got == want && next == off + bits,
                      "get_varint_le base=%u v=%llu off=%u: got %llu", base,
                      (unsigned long long)want, off, (unsigned long long)got);

                /* the oracle decodes the groups by the stated rule */
                oracle_expand(buf, NBQ_LSB_FIRST, s);
                for (g = 0; !done && g < 64; g++) {
                    const uint64_t group = oracle_value(s, off + g * base, base, NBQ_LSB_FIRST);
                    const uint64_t chunk = (payload >= 64) ? group : (group & ((1ull << payload) - 1));
                    if (shift < 64)
                        acc |= chunk << shift;
                    shift += payload;
                    done = ((group >> payload) & 1u) == 0;
                }
                CHECK(acc == want && g * base == bits, "oracle decode base=%u v=%llu off=%u", base,
                      (unsigned long long)want, off);

                /* through an LSB-first cursor, the same */
                {
                    nbq_reader r;
                    got = 0;
                    CHECK(nbq_reader_init_le(&r, buf, cap) == NBQ_OK &&
                              nbq_reader_seek(&r, off) == NBQ_OK &&
                              nbq_read_varint(&r, base, &got) == NBQ_OK && got == want &&
                              r.pos == off + bits,
                          "read_varint LSB first base=%u", base);
                }
            }
        }

        /* signed: zigzag round trip */
        {
            static const int64_t SV[] = {0, -1, 1, -2, 2, -64, 63, -65536, 65535, INT64_MIN,
                                         INT64_MAX};
            for (size_t vi = 0; vi < sizeof(SV) / sizeof(SV[0]); vi++) {
                int64_t sgot = 0;
                uint64_t next = 0;
                memset(buf, 0, sizeof(buf));
                CHECK(nbq_put_varint_s_le(buf, cap, 3, base, SV[vi], &next) == NBQ_OK &&
                          nbq_get_varint_s_le(buf, cap, 3, base, &sgot, &next) == NBQ_OK &&
                          sgot == SV[vi],
                      "varint_s_le base=%u v=%lld", base, (long long)SV[vi]);
            }
        }
    }

    /* malformed and overflowing input is refused in this order too */
    {
        uint64_t got = 0, next = 0;
        memset(buf, 0, sizeof(buf));
        CHECK(nbq_put_u64_le(buf, cap, 0, 8, 0x80) == NBQ_OK &&
                  nbq_put_u64_le(buf, cap, 8, 8, 0x00) == NBQ_OK,
              "write an overlong zero by hand");
        CHECK(nbq_get_varint_le(buf, cap, 0, 8, &got, &next) == NBQ_ERR_MALFORMED,
              "overlong is refused");
        for (unsigned i = 0; i < 10; i++)
            CHECK(nbq_put_u64_le(buf, cap, i * 8, 8, 0xFF) == NBQ_OK, "ten continued groups");
        CHECK(nbq_get_varint_le(buf, cap, 0, 8, &got, &next) == NBQ_ERR_OVERFLOW,
              "a value past 64 bits is refused");
        CHECK(nbq_get_varint_le(buf, cap, cap - 4, 8, &got, &next) == NBQ_ERR_RANGE,
              "a group past the end is refused");
    }

    /* the two orders agree only by coincidence of alignment: base 8 at a
       byte boundary gives the same bytes, one bit over it does not */
    {
        uint8_t m[ORACLE_BYTES], l[ORACLE_BYTES];
        uint64_t next;
        memset(m, 0, sizeof(m));
        memset(l, 0, sizeof(l));
        CHECK(nbq_put_varint(m, cap, 0, 8, 300, &next) == NBQ_OK &&
                  nbq_put_varint_le(l, cap, 0, 8, 300, &next) == NBQ_OK,
              "both orders at a byte boundary");
        CHECK(memcmp(m, l, 2) == 0 && m[0] == 0xAC && m[1] == 0x02,
              "aligned base-8 groups are whole bytes in both orders");
        memset(m, 0, sizeof(m));
        memset(l, 0, sizeof(l));
        CHECK(nbq_put_varint(m, cap, 1, 8, 300, &next) == NBQ_OK &&
                  nbq_put_varint_le(l, cap, 1, 8, 300, &next) == NBQ_OK,
              "both orders one bit in");
        CHECK(memcmp(m, l, 3) != 0, "unaligned, the orders differ");
    }
}

static void test_le_runs(void)
{
    uint8_t a[ORACLE_BYTES], b[ORACLE_BYTES], c[ORACLE_BYTES];
    const uint64_t cap = ORACLE_BYTES * 8;
    oracle sa, sb, sc, want;
    uint64_t seed = 0x2545F4914F6CDD1Dull, pop = 0, count = 0;
    int cmp = 1;

    for (size_t i = 0; i < sizeof(a); i++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        a[i] = (uint8_t)(seed >> 56);
        b[i] = (uint8_t)(seed >> 40);
    }
    oracle_expand(a, NBQ_LSB_FIRST, sa);
    oracle_expand(b, NBQ_LSB_FIRST, sb);

    /* copy between buffers: the substring moves, nothing else does */
    memcpy(c, b, sizeof(c));
    CHECK(nbq_copy_bits_le(a, cap, 3, c, cap, 11, 157) == NBQ_OK, "copy_bits_le");
    oracle_expand(c, NBQ_LSB_FIRST, sc);
    memcpy(want, sb, sizeof(want));
    memcpy(want + 11, sa + 3, 157);
    CHECK(memcmp(sc, want, sizeof(sc)) == 0, "copy_bits_le moved exactly the substring");

    /* same buffer, overlapping, both directions, against memmove on the string */
    memcpy(c, a, sizeof(c));
    CHECK(nbq_copy_bits_le(c, cap, 5, c, cap, 9, 100) == NBQ_OK, "overlap forward");
    oracle_expand(c, NBQ_LSB_FIRST, sc);
    memcpy(want, sa, sizeof(want));
    memmove(want + 9, want + 5, 100);
    CHECK(memcmp(sc, want, sizeof(sc)) == 0, "overlap forward matches memmove");
    memcpy(c, a, sizeof(c));
    CHECK(nbq_copy_bits_le(c, cap, 9, c, cap, 5, 100) == NBQ_OK, "overlap backward");
    oracle_expand(c, NBQ_LSB_FIRST, sc);
    memcpy(want, sa, sizeof(want));
    memmove(want + 5, want + 9, 100);
    CHECK(memcmp(sc, want, sizeof(sc)) == 0, "overlap backward matches memmove");

    /* fill */
    memcpy(c, a, sizeof(c));
    CHECK(nbq_fill_bits_le(c, cap, 13, 37, 1) == NBQ_OK, "fill_bits_le");
    oracle_expand(c, NBQ_LSB_FIRST, sc);
    memcpy(want, sa, sizeof(want));
    memset(want + 13, '1', 37);
    CHECK(memcmp(sc, want, sizeof(sc)) == 0, "fill_bits_le set exactly the substring");

    /* compare and popcount agree with the strings */
    CHECK(nbq_compare_bits_le(a, cap, 3, a, cap, 3, 100, &cmp) == NBQ_OK && cmp == 0,
          "compare_bits_le equal");
    {
        /* a and the filled copy differ somewhere in [13, 50); the first
           differing character in stream order decides, as strcmp would */
        int want_sign = 0;
        for (unsigned i = 13; i < 50 && want_sign == 0; i++)
            if (sa[i] != sc[i])
                want_sign = (sa[i] > sc[i]) ? 1 : -1;
        CHECK(nbq_compare_bits_le(a, cap, 13, c, cap, 13, 37, &cmp) == NBQ_OK &&
                  ((cmp > 0) - (cmp < 0)) == want_sign,
              "compare_bits_le decided by the first differing stream bit");
    }
    CHECK(nbq_popcount_bits_le(a, cap, 7, 200, &pop) == NBQ_OK, "popcount_bits_le");
    for (unsigned i = 0; i < 200; i++)
        count += (sa[7 + i] == '1');
    CHECK(pop == count, "popcount_bits_le counts the ones: %llu want %llu",
          (unsigned long long)pop, (unsigned long long)count);

    /* the _be spellings of the runs exist */
    CHECK(nbq_copy_bits_be(a, cap, 0, c, cap, 0, 8) == NBQ_OK && nbq_fill_bits_be(c, cap, 0, 8, 0) == NBQ_OK &&
              nbq_compare_bits_be(a, cap, 8, a, cap, 8, 8, &cmp) == NBQ_OK && cmp == 0 &&
              nbq_popcount_bits_be(a, cap, 0, 8, &pop) == NBQ_OK,
          "_be aliases for runs");
}

static void test_le_cursor_and_floats(void)
{
    uint8_t buf[64], via_c[64], src[2] = {0xA5, 0x3C}, dst[2] = {0, 0};
    const uint64_t cap = sizeof(buf) * 8;
    nbq_writer w;
    nbq_reader r;
    nbq_u64_parts p = {0x1u, 0x80000000u}, q = {0, 0};
    uint64_t u64 = 0;
    int64_t s64 = 0;
    uint32_t u32 = 0;
    int32_t s32 = 0;
    float f = 0.0f;
    double d = 0.0;
    int cmp = 1;

    /* one of everything through an LSB-first writer */
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_writer_init_le(&w, buf, cap) == NBQ_OK && w.order == NBQ_LSB_FIRST, "init_le");
    CHECK(nbq_write_u64(&w, 40, 0x123456789Aull) == NBQ_OK, "u64");
    CHECK(nbq_write_s64(&w, 20, -7) == NBQ_OK, "s64");
    CHECK(nbq_write_u32(&w, 12, 3000) == NBQ_OK, "u32");
    CHECK(nbq_write_s32(&w, 5, -3) == NBQ_OK, "s32");
    CHECK(nbq_write_parts(&w, 33, p) == NBQ_OK, "parts");
    CHECK(nbq_write_varint(&w, 5, 100) == NBQ_OK, "varint");
    CHECK(nbq_write_varint_s(&w, 5, -1) == NBQ_OK, "varint_s");
    CHECK(nbq_writer_align(&w, 8) == NBQ_OK && w.pos % 8 == 0, "align");
    CHECK(nbq_write_f32(&w, 1.5f) == NBQ_OK && nbq_write_f64(&w, -2.25) == NBQ_OK, "floats");
    CHECK(nbq_write_bits(&w, src, 16, 3, 10) == NBQ_OK, "bits");
    const uint64_t end = w.pos;

    /* the indexed _le forms see the same fields at the same positions */
    CHECK(nbq_get_u64_le(buf, cap, 0, 40, &u64) == NBQ_OK && u64 == 0x123456789Aull, "get_u64_le");
    CHECK(nbq_get_s64_le(buf, cap, 40, 20, &s64) == NBQ_OK && s64 == -7, "get_s64_le");
    CHECK(nbq_get_u32_le(buf, cap, 60, 12, &u32) == NBQ_OK && u32 == 3000, "get_u32_le");
    CHECK(nbq_get_s32_le(buf, cap, 72, 5, &s32) == NBQ_OK && s32 == -3, "get_s32_le");
    CHECK(nbq_get_parts_le(buf, cap, 77, 33, &q) == NBQ_OK && q.hi == 1u && q.lo == 0x80000000u,
          "get_parts_le");

    /* an LSB-first reader walks it back */
    CHECK(nbq_reader_init_le(&r, buf, end) == NBQ_OK && r.order == NBQ_LSB_FIRST, "reader_init_le");
    CHECK(nbq_read_u64(&r, 40, &u64) == NBQ_OK && u64 == 0x123456789Aull, "read u64");
    CHECK(nbq_read_s64(&r, 20, &s64) == NBQ_OK && s64 == -7, "read s64");
    CHECK(nbq_read_u32(&r, 12, &u32) == NBQ_OK && u32 == 3000, "read u32");
    CHECK(nbq_read_s32(&r, 5, &s32) == NBQ_OK && s32 == -3, "read s32");
    CHECK(nbq_read_parts(&r, 33, &q) == NBQ_OK && q.hi == 1u && q.lo == 0x80000000u, "read parts");
    CHECK(nbq_read_varint(&r, 5, &u64) == NBQ_OK && u64 == 100, "read varint");
    CHECK(nbq_read_varint_s(&r, 5, &s64) == NBQ_OK && s64 == -1, "read varint_s");
    CHECK(nbq_reader_align(&r, 8) == NBQ_OK && r.pos % 8 == 0, "reader align");
    CHECK(nbq_read_f32(&r, &f) == NBQ_OK && f == 1.5f, "read f32");
    CHECK(nbq_read_f64(&r, &d) == NBQ_OK && d == -2.25, "read f64");
    CHECK(nbq_read_bits(&r, dst, 16, 3, 10) == NBQ_OK, "read bits");
    CHECK(nbq_compare_bits_le(src, 16, 3, dst, 16, 3, 10, &cmp) == NBQ_OK && cmp == 0,
          "read_bits copied the run in stream order");
    CHECK(r.pos == end && nbq_reader_remaining(&r) == 0, "consumed exactly");

    /* an MSB-first reader on the same bytes gets a different first field */
    CHECK(nbq_reader_init(&r, buf, end) == NBQ_OK && nbq_read_u64(&r, 40, &u64) == NBQ_OK &&
              u64 != 0x123456789Aull,
          "order matters");

    /* padding written by align is zero in stream order and checked that way */
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_writer_init_le(&w, buf, cap) == NBQ_OK && nbq_write_u32(&w, 3, 7) == NBQ_OK &&
              nbq_writer_align(&w, 8) == NBQ_OK && w.pos == 8 && buf[0] == 0x07,
          "LSB-first pad lands in the high bits of the byte");
    CHECK(nbq_reader_init_le(&r, buf, 16) == NBQ_OK && nbq_reader_seek(&r, 3) == NBQ_OK &&
              nbq_reader_align(&r, 8) == NBQ_OK && r.pos == 8,
          "zero padding accepted");
    CHECK(nbq_put_u32_le(buf, cap, 5, 1, 1) == NBQ_OK && nbq_reader_seek(&r, 3) == NBQ_OK &&
              nbq_reader_align(&r, 8) == NBQ_ERR_MALFORMED && r.pos == 3,
          "a set pad bit is refused, cursor unmoved");

    /* floats: little-endian bytes at an aligned position, big-endian for _be */
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_put_f32_le(buf, cap, 0, 3.14159f) == NBQ_OK && buf[0] == 0xD0 && buf[1] == 0x0F &&
              buf[2] == 0x49 && buf[3] == 0x40,
          "put_f32_le is little-endian: %02X %02X %02X %02X", buf[0], buf[1], buf[2], buf[3]);
    CHECK(nbq_get_f32_le(buf, cap, 0, &f) == NBQ_OK && nbq_f32_bits(f) == 0x40490FD0u,
          "get_f32_le");
    CHECK(nbq_put_f32_be(buf, cap, 32, 3.14159f) == NBQ_OK && buf[4] == 0x40 && buf[5] == 0x49 &&
              buf[6] == 0x0F && buf[7] == 0xD0 && nbq_get_f32_be(buf, cap, 32, &f) == NBQ_OK &&
              nbq_f32_bits(f) == 0x40490FD0u,
          "_be float aliases are big-endian");
    CHECK(nbq_put_f64_le(buf, cap, 64, -2.5) == NBQ_OK && buf[14] == 0x04 && buf[15] == 0xC0 &&
              nbq_get_f64_le(buf, cap, 64, &d) == NBQ_OK && d == -2.5,
          "f64 _le");
    CHECK(nbq_put_f64_be(buf, cap, 128, -2.5) == NBQ_OK && buf[16] == 0xC0 && buf[17] == 0x04 &&
              nbq_get_f64_be(buf, cap, 128, &d) == NBQ_OK && d == -2.5,
          "f64 _be");
    CHECK(nbq_put_f32_le(buf, cap, 3, -0.0f) == NBQ_OK && nbq_get_f32_le(buf, cap, 3, &f) == NBQ_OK &&
              nbq_f32_bits(f) == 0x80000000u,
          "unaligned negative zero survives");

    /* the C writer in the other order produces different bytes for the same
       calls, so a program that compares streams gets to see both */
    memset(via_c, 0, sizeof(via_c));
    CHECK(nbq_writer_init(&w, via_c, cap) == NBQ_OK && nbq_write_u32(&w, 12, 3000) == NBQ_OK,
          "msb writer");
    memset(buf, 0, sizeof(buf));
    CHECK(nbq_writer_init_le(&w, buf, cap) == NBQ_OK && nbq_write_u32(&w, 12, 3000) == NBQ_OK,
          "lsb writer");
    CHECK(via_c[0] == 0xBB && via_c[1] == 0x80 && buf[0] == 0xB8 && buf[1] == 0x0B,
          "3000 in 12 bits: BB 80 MSB first, B8 0B LSB first");

#ifndef NBQ_NO_MALLOC
    /* the growing writer keeps its order across growth */
    CHECK(nbq_writer_init_alloc_le(&w, 16) == NBQ_OK && w.order == NBQ_LSB_FIRST, "init_alloc_le");
    for (uint32_t i = 0; i < 100; i++)
        CHECK(nbq_write_u32(&w, 12, i) == NBQ_OK, "grow");
    CHECK(w.order == NBQ_LSB_FIRST && nbq_get_u32_le(w.buf, w.cap, 12 * 57, 12, &u32) == NBQ_OK &&
              u32 == 57,
          "order survives growth");
    nbq_writer_free(&w);
#endif
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
    test_every_entry_point_refuses_bad_input();
    test_cursor_every_type();
    test_remaining_seek_and_align_edges();
#ifndef NBQ_NO_MALLOC
    test_allocating_writer_limits();
#endif
    test_both_orders_against_the_oracle();
    test_orders_are_mirror_images();
    test_deflate_blocks_from_zlib();
    test_le_varints();
    test_le_runs();
    test_le_cursor_and_floats();

    printf("nbitq: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
