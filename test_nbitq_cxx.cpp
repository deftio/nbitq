/**
 * @file test_nbitq_cxx.cpp
 * @brief The library from C++. nbitq.h and nbitq_container.h promise
 *        extern "C"; nbitq.hpp promises that every method is one call into
 *        the C library. Both are only true if something compiles, links and
 *        runs them this way, so this does, and calls every method once.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <cstdio>
#include <cstring>
#include <utility>

#include "nbitq.hpp"
#include "nbitq_container.h"

static int checks = 0, failures = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                     \
        }                                                                                          \
    } while (0)

// The C API, called directly: the headers are extern "C" and link from C++.
static void test_c_headers_link()
{
    uint8_t bits[16] = {0};
    nbq_writer w;
    CHECK(nbq_writer_init(&w, bits, sizeof(bits) * 8) == NBQ_OK, "writer_init");
    CHECK(nbq_write_u32(&w, 12, 3000) == NBQ_OK, "write_u32");
    CHECK(nbq_write_varint(&w, 5, 123456) == NBQ_OK, "write_varint");

    uint32_t v = 0;
    CHECK(nbq_get_u32(bits, sizeof(bits) * 8, 0, 12, &v) == NBQ_OK && v == 3000, "get_u32");

    uint8_t box[64];
    std::size_t n = 0;
    CHECK(nbq_container_write(box, sizeof(box), "cxx", 3, bits, w.pos, &n) == NBQ_OK,
          "container_write");
    nbq_container c;
    std::memset(&c, 0, sizeof(c));
    CHECK(nbq_container_read(box, n, &c) == NBQ_OK, "container_read");
    CHECK(c.payload_bits == w.pos && c.type_len == 3 && std::memcmp(c.type, "cxx", 3) == 0,
          "container fields");
}

// The static inline functions in nbitq.h, compiled as C++. Every one of them,
// because this is the translation unit their coverage is measured from.
static void test_header_inlines()
{
    CHECK(nbq_f32_bits(3.14159f) == 0x40490FD0u, "f32_bits");
    CHECK(nbq_f32_bits(nbq_f32_from_bits(0x7FC00001u)) == 0x7FC00001u, "f32 NaN payload survives");
    CHECK(nbq_f64_bits(-2.5) == 0xC004000000000000ull, "f64_bits");
    CHECK(nbq_f64_bits(nbq_f64_from_bits(0x8000000000000000ull)) == 0x8000000000000000ull,
          "f64 negative zero survives");

    uint8_t buf[16] = {0};
    const uint64_t cap = sizeof(buf) * 8;
    float f = 0.0f;
    double d = 0.0;
    CHECK(nbq_put_f32(buf, cap, 3, 3.14159f) == NBQ_OK, "put_f32");
    CHECK(nbq_get_f32(buf, cap, 3, &f) == NBQ_OK && nbq_f32_bits(f) == 0x40490FD0u, "get_f32");
    CHECK(nbq_put_f64(buf, cap, 40, -2.5) == NBQ_OK, "put_f64");
    CHECK(nbq_get_f64(buf, cap, 40, &d) == NBQ_OK && d == -2.5, "get_f64");

    nbq_writer w;
    nbq_reader r;
    nbq_writer_init(&w, buf, cap);
    CHECK(nbq_write_f32(&w, 1.5f) == NBQ_OK && nbq_write_f64(&w, 0.25) == NBQ_OK, "write_f32/f64");
    nbq_reader_init(&r, buf, w.pos);
    CHECK(nbq_read_f32(&r, &f) == NBQ_OK && f == 1.5f, "read_f32");
    CHECK(nbq_read_f64(&r, &d) == NBQ_OK && d == 0.25, "read_f64");

    // the LSB-first inlines: little-endian bytes at an aligned position
    uint8_t le[16] = {0};
    CHECK(nbq_put_f32_le(le, 128, 0, 3.14159f) == NBQ_OK && le[0] == 0xD0 && le[3] == 0x40,
          "put_f32_le");
    CHECK(nbq_get_f32_le(le, 128, 0, &f) == NBQ_OK && nbq_f32_bits(f) == 0x40490FD0u, "get_f32_le");
    CHECK(nbq_put_f64_le(le, 128, 32, -2.5) == NBQ_OK && le[11] == 0xC0, "put_f64_le");
    CHECK(nbq_get_f64_le(le, 128, 32, &d) == NBQ_OK && d == -2.5, "get_f64_le");
}

// Writes one of everything with the C writer; the sequence the Reader and
// Writer tests both use. Returns the number of bits written.
static uint64_t write_everything_in_c(uint8_t *buf, std::size_t len)
{
    nbq_writer w;
    nbq_writer_init(&w, buf, len * 8);
    nbq_write_u64(&w, 40, 0x123456789Aull);
    nbq_write_s64(&w, 20, -7);
    nbq_write_u32(&w, 12, 3000);
    nbq_write_s32(&w, 5, -3);
    nbq_u64_parts p = {0x1u, 0x80000000u};
    nbq_write_parts(&w, 33, p);
    nbq_write_varint(&w, 8, 300);
    nbq_write_varint_s(&w, 5, -1);
    nbq_writer_align(&w, 8);
    nbq_write_f32(&w, 1.5f);
    nbq_write_f64(&w, -2.25);
    const uint8_t tail = 0xA5;
    nbq_write_bits(&w, &tail, 8, 0, 8);
    return w.pos;
}

static void test_reader()
{
    uint8_t buf[64] = {0};
    const uint64_t len = write_everything_in_c(buf, sizeof(buf));

    nbq::Reader r(buf, len);
    CHECK(r.buffer() == buf && r.length() == len && r.pos() == 0 && r.remaining() == len,
          "fresh reader");

    uint64_t u64 = 0;
    int64_t s64 = 0;
    uint32_t u32 = 0;
    int32_t s32 = 0;
    nbq_u64_parts q = {0, 0};
    float f = 0.0f;
    double d = 0.0;
    uint8_t tail = 0;
    CHECK(r.u64(40, &u64) == NBQ_OK && u64 == 0x123456789Aull, "u64");
    CHECK(r.s64(20, &s64) == NBQ_OK && s64 == -7, "s64");
    CHECK(r.u32(12, &u32) == NBQ_OK && u32 == 3000, "u32");
    CHECK(r.s32(5, &s32) == NBQ_OK && s32 == -3, "s32");
    CHECK(r.parts(33, &q) == NBQ_OK && q.hi == 0x1u && q.lo == 0x80000000u, "parts");
    CHECK(r.varint(8, &u64) == NBQ_OK && u64 == 300, "varint");
    CHECK(r.varint_s(5, &s64) == NBQ_OK && s64 == -1, "varint_s");
    CHECK(r.align(8) == NBQ_OK && r.pos() % 8 == 0, "align");
    CHECK(r.f32(&f) == NBQ_OK && f == 1.5f, "f32");
    CHECK(r.f64(&d) == NBQ_OK && d == -2.25, "f64");
    CHECK(r.bits(&tail, 8, 0, 8) == NBQ_OK && tail == 0xA5, "bits");
    CHECK(r.remaining() == 0 && r.pos() == len, "everything consumed");

    CHECK(r.seek(0) == NBQ_OK && r.pos() == 0, "seek");
    CHECK(r.seek(len + 1) == NBQ_ERR_RANGE && r.pos() == 0, "seek past the end");
    CHECK(r.u64(0, &u64) == NBQ_ERR_PARAM, "the C result codes come through unchanged");

    // the C struct is reachable for code that takes one
    CHECK(r.handle()->bits == len, "handle");
    const nbq::Reader &cr = r;
    CHECK(cr.handle()->pos == r.pos(), "const handle");

    // a copy is a second, independent cursor over the same buffer
    nbq::Reader copy = r;
    CHECK(copy.u32(8, &u32) == NBQ_OK && copy.pos() == 8 && r.pos() == 0, "copy");

    // empty, then pointed at a buffer; a failed init leaves it empty
    nbq::Reader e;
    CHECK(e.length() == 0 && e.buffer() == nullptr && e.u32(1, &u32) != NBQ_OK, "empty reader");
    CHECK(e.init(buf, len) == NBQ_OK && e.length() == len, "init");
    CHECK(e.init(nullptr, len) == NBQ_ERR_PARAM && e.length() == 0 && e.buffer() == nullptr,
          "failed init leaves an empty reader");
    CHECK(e.order() == NBQ_MSB_FIRST, "MSB first unless asked");

    // LSB first: the order is a property of the reader, set at init
    uint8_t lsb[8] = {0};
    nbq_writer cw;
    nbq_writer_init_le(&cw, lsb, 64);
    nbq_write_u32(&cw, 12, 3000);
    nbq_write_u32(&cw, 5, 19);
    nbq::Reader rl(lsb, cw.pos, NBQ_LSB_FIRST);
    CHECK(rl.order() == NBQ_LSB_FIRST, "order()");
    CHECK(rl.u32(12, &u32) == NBQ_OK && u32 == 3000 && rl.u32(5, &u32) == NBQ_OK && u32 == 19,
          "LSB-first reads");
    nbq::Reader rm(lsb, cw.pos);
    CHECK(rm.u32(12, &u32) == NBQ_OK && u32 != 3000, "the same bytes MSB first differ");
    CHECK(e.init(lsb, cw.pos, NBQ_LSB_FIRST) == NBQ_OK && e.order() == NBQ_LSB_FIRST, "init with order");
}

static void test_writer()
{
    uint8_t want[64] = {0};
    const uint64_t len = write_everything_in_c(want, sizeof(want));

    uint8_t buf[64] = {0};
    nbq::Writer w(buf, sizeof(buf) * 8);
    CHECK(w.buffer() == buf && w.capacity() == sizeof(buf) * 8 && w.pos() == 0, "fresh writer");

    nbq_u64_parts p = {0x1u, 0x80000000u};
    const uint8_t tail = 0xA5;
    CHECK(w.u64(40, 0x123456789Aull) == NBQ_OK, "u64");
    CHECK(w.s64(20, -7) == NBQ_OK, "s64");
    CHECK(w.u32(12, 3000) == NBQ_OK, "u32");
    CHECK(w.s32(5, -3) == NBQ_OK, "s32");
    CHECK(w.parts(33, p) == NBQ_OK, "parts");
    CHECK(w.varint(8, 300) == NBQ_OK, "varint");
    CHECK(w.varint_s(5, -1) == NBQ_OK, "varint_s");
    CHECK(w.align(8) == NBQ_OK, "align");
    CHECK(w.f32(1.5f) == NBQ_OK, "f32");
    CHECK(w.f64(-2.25) == NBQ_OK, "f64");
    CHECK(w.bits(&tail, 8, 0, 8) == NBQ_OK, "bits");

    // byte for byte what the C writer produced
    CHECK(w.pos() == len && std::memcmp(buf, want, sizeof(buf)) == 0, "same bits as C");

    // back-patch through seek
    const uint64_t end = w.pos();
    CHECK(w.seek(0) == NBQ_OK && w.u32(8, 0xFF) == NBQ_OK && w.seek(end) == NBQ_OK, "seek");
    CHECK(buf[0] == 0xFF && w.pos() == end, "back-patched");
    CHECK(w.seek(sizeof(buf) * 8 + 1) == NBQ_ERR_RANGE, "seek past the capacity");
    CHECK(w.u32(0, 1) == NBQ_ERR_PARAM, "the C result codes come through unchanged");

    CHECK(w.handle()->cap == sizeof(buf) * 8, "handle");
    const nbq::Writer &cw = w;
    CHECK(cw.handle()->pos == w.pos() && cw.buffer() == buf, "const handle and buffer");

    nbq::Writer e;
    CHECK(e.capacity() == 0 && e.buffer() == nullptr && e.u32(1, 1) == NBQ_ERR_RANGE,
          "empty writer refuses every write");
    CHECK(e.init(buf, 64) == NBQ_OK && e.capacity() == 64, "init");
    CHECK(e.init(nullptr, 64) == NBQ_ERR_PARAM && e.capacity() == 0, "failed init leaves it empty");
    CHECK(e.order() == NBQ_MSB_FIRST, "MSB first unless asked");

    // LSB first produces what the C LSB-first writer produces
    uint8_t lsb_cxx[8] = {0}, lsb_c[8] = {0};
    nbq::Writer wl(lsb_cxx, 64, NBQ_LSB_FIRST);
    CHECK(wl.order() == NBQ_LSB_FIRST && wl.u32(12, 3000) == NBQ_OK && wl.u32(5, 19) == NBQ_OK,
          "LSB-first writes");
    nbq_writer cwl;
    nbq_writer_init_le(&cwl, lsb_c, 64);
    nbq_write_u32(&cwl, 12, 3000);
    nbq_write_u32(&cwl, 5, 19);
    CHECK(std::memcmp(lsb_cxx, lsb_c, sizeof(lsb_c)) == 0 && lsb_c[0] == 0xB8, "same bytes as C");
    CHECK(e.init(buf, 64, NBQ_LSB_FIRST) == NBQ_OK && e.order() == NBQ_LSB_FIRST, "init with order");
}

#ifndef NBQ_NO_MALLOC
static void test_owning_writer()
{
    nbq::OwningWriter w(64);
    CHECK(w.capacity() >= 64 && w.buffer() != nullptr && w.pos() == 0, "allocated");
    for (uint32_t i = 0; i < 100; i++)
        CHECK(w.u32(32, i) == NBQ_OK, "write grows the buffer");
    CHECK(w.pos() == 3200 && w.capacity() >= 3200, "grown");

    nbq::Reader r(w.buffer(), w.pos());
    uint32_t v = 0;
    CHECK(r.seek(32 * 57) == NBQ_OK && r.u32(32, &v) == NBQ_OK && v == 57, "contents survived growth");

    // a move hands the buffer over and leaves the source empty
    uint8_t *p = w.buffer();
    nbq::OwningWriter m(std::move(w));
    CHECK(m.buffer() == p && m.pos() == 3200, "moved to");
    CHECK(w.buffer() == nullptr && w.capacity() == 0 && w.pos() == 0, "moved from");

    nbq::OwningWriter a;
    CHECK(a.capacity() == 0 && a.buffer() == nullptr, "default is empty");
    a = std::move(m);
    CHECK(a.buffer() == p && a.pos() == 3200 && m.buffer() == nullptr, "move-assigned");

    // self-assignment through a reference changes nothing
    nbq::OwningWriter &self = a;
    a = std::move(self);
    CHECK(a.buffer() == p && a.pos() == 3200, "self move-assignment is a no-op");

    // init again frees the old buffer and starts over; 0 means the default size
    CHECK(a.init(0) == NBQ_OK && a.capacity() == 512 && a.pos() == 0, "re-init");

    // an impossible size is refused and leaves nothing to free
    nbq::OwningWriter huge;
    CHECK(huge.init(UINT64_MAX) == NBQ_ERR_ALLOC && huge.buffer() == nullptr, "refused");

    // LSB first, kept across growth
    nbq::OwningWriter ol(16, NBQ_LSB_FIRST);
    CHECK(ol.order() == NBQ_LSB_FIRST, "owning order");
    for (uint32_t i = 0; i < 100; i++)
        CHECK(ol.u32(12, i) == NBQ_OK, "grow LSB first");
    CHECK(ol.order() == NBQ_LSB_FIRST && nbq_get_u32_le(ol.buffer(), ol.capacity(), 12 * 57, 12, &v) == NBQ_OK && v == 57,
          "order survives growth");
    CHECK(ol.init(0, NBQ_LSB_FIRST) == NBQ_OK && ol.order() == NBQ_LSB_FIRST && ol.capacity() == 512,
          "re-init with order");
}
#endif

int main()
{
    test_c_headers_link();
    test_header_inlines();
    test_reader();
    test_writer();
#ifndef NBQ_NO_MALLOC
    test_owning_writer();
#endif
    std::printf("nbitq_cxx: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
