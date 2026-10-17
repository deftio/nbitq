/**
 * @file nbitq.hpp
 * @brief nbitq from C++: the cursor API as two small classes. Header-only.
 *
 * Every method here is one call into the C library with the `nbq_read_` or
 * `nbq_write_` prefix dropped: `nbq_read_u32(&r, n, &v)` is `r.u32(n, &v)`.
 * Nothing is reinterpreted, widened or checked twice, so a `Reader` returns
 * exactly what an `nbq_reader` returns, and the tests that cover the C
 * library cover the calls made here.
 *
 * What C++ adds, and the only reason this file exists:
 *
 * - **Ownership.** `OwningWriter` wraps `nbq_writer_init_alloc` and calls
 *   `nbq_writer_free` from its destructor. It can be moved, not copied.
 * - **A position that belongs to an object.** `Reader` and `Writer` hold the
 *   C struct; `handle()` hands it to code that takes one.
 *
 * What it does not add: exceptions (every method returns `nbq_result`, as in
 * C, so `-fno-exceptions` builds are unaffected); anything from the standard
 * library (no `<vector>`, `<string>`, `<iostream>` or `<span>`); overloads
 * that guess a width from a type; or a wrapper for the indexed API, which has
 * no state to wrap. Call `nbq_get_u32` and the other indexed functions
 * directly; they are plain C functions and work unchanged from C++.
 *
 * Requires C++11. Define NBQ_NO_MALLOC to remove `OwningWriter`, as it removes
 * `nbq_writer_init_alloc` from the C library.
 *
 * Adding a cursor function to nbitq.h means adding a one-line forwarder here.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#ifndef NBITQ_HPP
#define NBITQ_HPP

#include "nbitq.h"

namespace nbq {

/** A read cursor over a caller-owned buffer. Copyable: it owns nothing. */
class Reader {
public:
    /** An empty reader. Every read fails until init() points it at a buffer. */
    Reader() { clear(); }
    Reader(const uint8_t *buf, uint64_t bit_len) { init(buf, bit_len); }

    /** nbq_reader_init. If it fails the reader is left empty, not half-set. */
    nbq_result init(const uint8_t *buf, uint64_t bit_len)
    {
        clear();
        return nbq_reader_init(&r_, buf, bit_len);
    }

    const uint8_t *buffer() const { return r_.buf; }
    uint64_t length() const { return r_.bits; } /**< valid bits in the buffer */
    uint64_t pos() const { return r_.pos; }
    uint64_t remaining() const { return nbq_reader_remaining(&r_); }

    nbq_result seek(uint64_t bit_pos) { return nbq_reader_seek(&r_, bit_pos); }
    nbq_result align(unsigned int n) { return nbq_reader_align(&r_, n); }

    nbq_result u64(unsigned int n, uint64_t *out) { return nbq_read_u64(&r_, n, out); }
    nbq_result s64(unsigned int n, int64_t *out) { return nbq_read_s64(&r_, n, out); }
    nbq_result u32(unsigned int n, uint32_t *out) { return nbq_read_u32(&r_, n, out); }
    nbq_result s32(unsigned int n, int32_t *out) { return nbq_read_s32(&r_, n, out); }
    nbq_result parts(unsigned int n, nbq_u64_parts *out) { return nbq_read_parts(&r_, n, out); }
    nbq_result varint(unsigned int base, uint64_t *out) { return nbq_read_varint(&r_, base, out); }
    nbq_result varint_s(unsigned int base, int64_t *out) { return nbq_read_varint_s(&r_, base, out); }
    nbq_result bits(uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n)
    {
        return nbq_read_bits(&r_, dst, dst_bits, dst_pos, n);
    }
    nbq_result f32(float *out) { return nbq_read_f32(&r_, out); }
    nbq_result f64(double *out) { return nbq_read_f64(&r_, out); }

    /** The C struct, for code that takes an nbq_reader. */
    nbq_reader *handle() { return &r_; }
    const nbq_reader *handle() const { return &r_; }

private:
    void clear()
    {
        r_.buf = nullptr;
        r_.bits = 0;
        r_.pos = 0;
    }
    nbq_reader r_;
};

/**
 * A write cursor over caller-owned storage of fixed capacity.
 *
 * Not copyable: two cursors over one buffer is an easy way to overwrite a
 * field, and a copy of an OwningWriter would free its buffer twice. Pass it
 * by reference.
 */
class Writer {
public:
    /** An empty writer. Every write fails until init() points it at a buffer. */
    Writer() { clear(); }
    Writer(uint8_t *buf, uint64_t bit_cap) { init(buf, bit_cap); }

    /** nbq_writer_init. If it fails the writer is left empty, not half-set. */
    nbq_result init(uint8_t *buf, uint64_t bit_cap)
    {
        clear();
        return nbq_writer_init(&w_, buf, bit_cap);
    }

    uint8_t *buffer() { return w_.buf; }
    const uint8_t *buffer() const { return w_.buf; }
    uint64_t capacity() const { return w_.cap; } /**< in bits */
    uint64_t pos() const { return w_.pos; }      /**< bits written so far */

    nbq_result seek(uint64_t bit_pos) { return nbq_writer_seek(&w_, bit_pos); }
    nbq_result align(unsigned int n) { return nbq_writer_align(&w_, n); }

    nbq_result u64(unsigned int n, uint64_t value) { return nbq_write_u64(&w_, n, value); }
    nbq_result s64(unsigned int n, int64_t value) { return nbq_write_s64(&w_, n, value); }
    nbq_result u32(unsigned int n, uint32_t value) { return nbq_write_u32(&w_, n, value); }
    nbq_result s32(unsigned int n, int32_t value) { return nbq_write_s32(&w_, n, value); }
    nbq_result parts(unsigned int n, nbq_u64_parts value) { return nbq_write_parts(&w_, n, value); }
    nbq_result varint(unsigned int base, uint64_t value) { return nbq_write_varint(&w_, base, value); }
    nbq_result varint_s(unsigned int base, int64_t value) { return nbq_write_varint_s(&w_, base, value); }
    nbq_result bits(const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint64_t n)
    {
        return nbq_write_bits(&w_, src, src_bits, src_pos, n);
    }
    nbq_result f32(float value) { return nbq_write_f32(&w_, value); }
    nbq_result f64(double value) { return nbq_write_f64(&w_, value); }

    /** The C struct, for code that takes an nbq_writer. */
    nbq_writer *handle() { return &w_; }
    const nbq_writer *handle() const { return &w_; }

    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

protected:
    void clear()
    {
        w_.buf = nullptr;
        w_.cap = 0;
        w_.pos = 0;
        w_.owns_buf = 0;
    }
    nbq_writer w_;
};

#ifndef NBQ_NO_MALLOC
/**
 * A writer over a heap buffer that grows by doubling: nbq_writer_init_alloc
 * with the nbq_writer_free in the destructor. Movable, not copyable. The
 * finished bits are at buffer(), pos() bits long; copy them out before the
 * writer goes out of scope.
 */
class OwningWriter : public Writer {
public:
    OwningWriter() {}
    explicit OwningWriter(uint64_t initial_bits) { init(initial_bits); }
    ~OwningWriter() { nbq_writer_free(&w_); }

    /** nbq_writer_init_alloc. Frees a buffer this writer already owns first.
     *  0 bits picks the library's default size. */
    nbq_result init(uint64_t initial_bits)
    {
        nbq_writer_free(&w_);
        return nbq_writer_init_alloc(&w_, initial_bits);
    }

    OwningWriter(OwningWriter &&other) : Writer() { take(other); }
    OwningWriter &operator=(OwningWriter &&other)
    {
        if (this != &other) {
            nbq_writer_free(&w_);
            take(other);
        }
        return *this;
    }

private:
    void take(OwningWriter &other)
    {
        w_ = other.w_;
        other.clear();
    }
};
#endif /* NBQ_NO_MALLOC */

} /* namespace nbq */

#endif /* NBITQ_HPP */
