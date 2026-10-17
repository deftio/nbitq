/**
 * @file nbitq.h
 * @brief nbitq -- arbitrary-width bit field read/write. C99, no dependencies.
 *
 * Put an n-bit quantity into a buffer at a bit position; get it back out.
 * That is the whole library. It has no format, no header, no magic bytes and
 * no opinion about what the bits mean: a run of bits extracted from one place
 * is just a run of bits, and whatever decodes it afterwards -- a trie, a
 * decompressor, your own parser -- is a layer this code knows nothing about.
 *
 * The name is from the API it descends from: `GetNBitQty` / `PutNBitQty` in
 * dio_BitStream (M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a
 * 12-bit jump offset, a 7-bit value into a continuous bit stream, then read
 * them back as native integers."
 *
 * ## Two access styles, one implementation
 *
 * **Indexed** -- `nbq_get_*` / `nbq_put_*`. You pass (buffer, bit_len,
 * bit_pos) and get a value. No object, no state, no allocation. Every call
 * is bounds-checked against bit_len, so a position derived from untrusted
 * data cannot read past the buffer.
 *
 * **Cursor** -- `nbq_reader` / `nbq_writer`. A struct you own, holding a
 * position that advances. The cursor functions are thin wrappers over the
 * indexed ones, so there is a single implementation of every extraction and
 * the two styles cannot disagree.
 *
 * ## Allocation
 *
 * Nothing here needs to allocate. `nbq_reader_init` and `nbq_writer_init`
 * take storage you provide -- stack, static, or ROM for the reader. A
 * growing writer is available via `nbq_writer_init_alloc` for hosted code
 * that would rather not size a buffer up front; it is the only function in
 * the library that calls malloc, and it is optional.
 *
 * ## Conventions, fixed and not negotiable later
 *
 * - **Bit 0 is the most significant bit of byte 0.** A field is written
 *   most-significant bit first. This is the one convention that, left
 *   ambiguous, makes two implementations disagree about every file.
 * - **Addressing is in bits**, everywhere, with no byte-alignment
 *   requirement. Nothing is padded because bytes exist.
 * - **Reads and writes are atomic.** A call either succeeds completely, or
 *   fails and changes nothing -- no partial value, no moved cursor. A varint
 *   spans several groups, and a truncated one that consumed the groups it
 *   managed to read would leave a caller unable to retry or resynchronise.
 * - **Malformed input is refused, never guessed at.** Out-of-range widths,
 *   positions past the end, overlong varints and values too wide for 64 bits
 *   are errors, not approximations.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#ifndef NBITQ_H
#define NBITQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBQ_VERSION_MAJOR 0
#define NBQ_VERSION_MINOR 1
#define NBQ_VERSION_PATCH 0
#define NBQ_VERSION_STRING "0.1.0"

/** The three parts packed as 0x00MMmmpp, so versions compare as integers:
 *  `#if NBQ_VERSION >= 0x000200` */
#define NBQ_VERSION ((NBQ_VERSION_MAJOR << 16) | (NBQ_VERSION_MINOR << 8) | NBQ_VERSION_PATCH)

/** The version of the library that was compiled and linked, which is not
 *  always the version of the header a caller included. Both are constants
 *  compiled into nbitq.c; neither allocates or needs libc. */
const char *nbq_version_string(void);   /**< NBQ_VERSION_STRING, "1.2.3" for 1.2.3 */
uint32_t    nbq_version(void);          /**< NBQ_VERSION, 0x010203 for 1.2.3 */

/* ── Results ─────────────────────────────────────────────────────────────
 * A small closed set. This library returns these and nothing else, which is
 * what lets it be dropped into a project that has its own error type without
 * dragging anything along.
 */
typedef enum {
    NBQ_OK = 0,
    NBQ_ERR_PARAM = -1,    /**< null pointer, or a width/base out of range */
    NBQ_ERR_RANGE = -2,    /**< the operation would cross the end of the buffer */
    NBQ_ERR_OVERFLOW = -3, /**< value does not fit the destination type */
    NBQ_ERR_MALFORMED = -4,/**< varint overlong, or an impossible encoding */
    NBQ_ERR_ALLOC = -5     /**< only from nbq_writer_init_alloc / grow */
} nbq_result;

/** Human-readable form of a result. The strings are the only rodata in the
 *  core; a size-constrained build that never calls this drops them with
 *  -ffunction-sections -fdata-sections and --gc-sections. */
const char *nbq_strerror(nbq_result rc);

/** Widest field a single call moves. */
#define NBQ_MAX_WIDTH 64u

/** Varint group widths. A group carries `base - 1` payload bits plus one
 *  continuation bit; base 8 is LEB128 exactly. */
#define NBQ_VARINT_MIN_BASE 2u
#define NBQ_VARINT_MAX_BASE 64u

/* ════════════════════════════════════════════════════════════════════════
 * INDEXED API -- (buffer, bit_len, bit_pos) -> value. No state. No malloc.
 * ════════════════════════════════════════════════════════════════════════
 *
 * `bit_len` is the number of valid bits in `buf`, and every call refuses an
 * operation that would touch a bit at or past it. Passing it is not
 * ceremony: the predecessor of this API took no length, could not check, and
 * a position computed from file content walked off the end of a heap buffer.
 */

/** Read @p n bits (1..64) at @p bit_pos as an unsigned value. */
nbq_result nbq_get_u64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       uint64_t *out);

/** Read @p n bits (1..64) at @p bit_pos, sign-extended from bit n-1. */
nbq_result nbq_get_s64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       int64_t *out);

/**
 * Read @p n bits (1..32) as unsigned, using no 64-bit arithmetic.
 *
 * On a target without native 64-bit support every `uint64_t` shift is a
 * compiler library call, and a bit-stream walk is nothing but shifts. This
 * is not a convenience wrapper over nbq_get_u64 -- it is a separate path
 * that produces identical bits.
 */
nbq_result nbq_get_u32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       uint32_t *out);

/** Read @p n bits (1..32), sign-extended. No 64-bit arithmetic. */
nbq_result nbq_get_s32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       int32_t *out);

/** A 64-bit value as two 32-bit halves, for targets with no 64-bit type. */
typedef struct {
    uint32_t hi; /**< bits 63..32 */
    uint32_t lo; /**< bits 31..0  */
} nbq_u64_parts;

/** Read @p n bits (1..64) into a hi/lo pair, using only 32-bit arithmetic. */
nbq_result nbq_get_parts(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                         nbq_u64_parts *out);

/** Write @p n bits (1..64) of @p value at @p bit_pos. @p bit_cap is the
 *  capacity of @p buf in bits. Bits outside the field are left alone. */
nbq_result nbq_put_u64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       uint64_t value);

/** Write the low @p n bits of a signed value; see nbq_get_s64 to read back. */
nbq_result nbq_put_s64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       int64_t value);

/** Write @p n bits (1..32) using no 64-bit arithmetic. */
nbq_result nbq_put_u32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       uint32_t value);

/** Write the low @p n bits (1..32) of a signed value, no 64-bit arithmetic. */
nbq_result nbq_put_s32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       int32_t value);

/** Write @p n bits (1..64) from a hi/lo pair, using only 32-bit arithmetic. */
nbq_result nbq_put_parts(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                         nbq_u64_parts value);

/* ── Varints at a caller-chosen group width ──────────────────────────── */

/**
 * Read a varint in @p base -bit groups at @p bit_pos.
 *
 * @p next_pos, if non-NULL, receives the position just past the varint. A
 * varint's width is not known until it is read, so without it an indexed
 * caller cannot advance.
 *
 * Refuses an overlong encoding: two encoders that disagree about padding
 * produce two files for one value.
 */
nbq_result nbq_get_varint(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                          unsigned int base, uint64_t *out, uint64_t *next_pos);

/** Signed (zigzag) varint. */
nbq_result nbq_get_varint_s(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                            unsigned int base, int64_t *out, uint64_t *next_pos);

/** Write @p value as a varint in @p base -bit groups, canonically. */
nbq_result nbq_put_varint(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                          uint64_t value, uint64_t *next_pos);

/** Write a signed (zigzag) varint. */
nbq_result nbq_put_varint_s(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                            int64_t value, uint64_t *next_pos);

/** Bits @p value occupies at @p base, without writing. 0 if base is invalid.
 *  Lets a caller compare widths over a real corpus; this library does not
 *  choose for you. */
unsigned int nbq_varint_bits(uint64_t value, unsigned int base);

/** Bits a signed @p value occupies at @p base. Zigzag makes -1 cheap and
 *  INT64_MIN expensive, so sizing signed data with nbq_varint_bits measures
 *  the wrong number. */
unsigned int nbq_varint_bits_s(int64_t value, unsigned int base);

/* ── Bit arrays ──────────────────────────────────────────────────────────
 *
 * The operation most bit libraries leave out: move a run of bits from any bit
 * offset to any other bit offset, with neither end byte-aligned. An extracted
 * run is just bits; what decodes it is somebody else's problem.
 */

/** Copy @p n bits from @p src at @p src_pos to @p dst at @p dst_pos.
 *  Buffers may be the same; overlapping ranges are handled correctly. */
nbq_result nbq_copy_bits(const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint8_t *dst,
                         uint64_t dst_bits, uint64_t dst_pos, uint64_t n);

/** Set @p n bits at @p bit_pos to 0 or 1. */
nbq_result nbq_fill_bits(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n, int value);

/** Compare @p n bits at two positions. @p out is <0, 0 or >0 like memcmp,
 *  comparing most-significant bit first. */
nbq_result nbq_compare_bits(const uint8_t *a, uint64_t a_bits, uint64_t a_pos, const uint8_t *b,
                            uint64_t b_bits, uint64_t b_pos, uint64_t n, int *out);

/** Count the 1 bits in @p n bits at @p bit_pos. */
nbq_result nbq_popcount_bits(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, uint64_t n,
                             uint64_t *out);

/** Bytes needed to hold @p bits bits. */
#define NBQ_BYTES_FOR_BITS(bits) (((uint64_t)(bits) + 7u) / 8u)

/* ════════════════════════════════════════════════════════════════════════
 * CURSOR API -- a position that advances. Thin wrappers over the above.
 * ════════════════════════════════════════════════════════════════════════
 *
 * The structs are public and contain no hidden state, so a caller may place
 * one on the stack, in static storage, or inside its own object. There is no
 * create/destroy pair to forget.
 */

typedef struct {
    const uint8_t *buf;
    uint64_t bits; /**< valid bits in buf */
    uint64_t pos;  /**< next bit to read */
} nbq_reader;

typedef struct {
    uint8_t *buf;
    uint64_t cap; /**< capacity in bits */
    uint64_t pos; /**< next bit to write */
    int owns_buf; /**< nonzero if nbq_writer_init_alloc allocated buf; always
                       0 under NBQ_NO_MALLOC, where the writer never grows */
} nbq_writer;

/** Point a reader at a buffer. Allocates nothing; @p buf may be in ROM. */
nbq_result nbq_reader_init(nbq_reader *r, const uint8_t *buf, uint64_t bit_len);

/** Point a writer at caller-owned storage. Allocates nothing, never grows;
 *  a write past @p bit_cap returns NBQ_ERR_RANGE. */
nbq_result nbq_writer_init(nbq_writer *w, uint8_t *buf, uint64_t bit_cap);

/**
 * A writer that owns a heap buffer and doubles it as needed.
 *
 * The only allocating entry point in the library, and the only reason it
 * includes <stdlib.h>. Define **NBQ_NO_MALLOC** to compile this out: the rest
 * of the library then pulls in no hosted header at all and builds
 * -ffreestanding, which is the difference between "works on embedded" and
 * "claims to". Everything else -- every get, every put, the indexed API and
 * the fixed-capacity writer -- is unaffected.
 *
 * Release with nbq_writer_free.
 */
nbq_result nbq_writer_init_alloc(nbq_writer *w, uint64_t initial_bits);

/** Free a buffer that nbq_writer_init_alloc allocated. A no-op otherwise. */
void nbq_writer_free(nbq_writer *w);

/** Bits not yet read. */
uint64_t nbq_reader_remaining(const nbq_reader *r);

/** Move the read cursor. Refuses a position past the end. */
nbq_result nbq_reader_seek(nbq_reader *r, uint64_t bit_pos);

/** Move the write cursor, for filling in a field left as a placeholder.
 *  Refuses a position past the capacity. */
nbq_result nbq_writer_seek(nbq_writer *w, uint64_t bit_pos);

/** Advance to the next multiple of @p n bits, writing zeros (writer) or
 *  requiring them to be zero (reader). @p n of 8 is byte alignment. */
nbq_result nbq_reader_align(nbq_reader *r, unsigned int n);
nbq_result nbq_writer_align(nbq_writer *w, unsigned int n);

/* Sequential reads. Each is the indexed call at the cursor, then advance. */
nbq_result nbq_read_u64(nbq_reader *r, unsigned int n, uint64_t *out);
nbq_result nbq_read_s64(nbq_reader *r, unsigned int n, int64_t *out);
nbq_result nbq_read_u32(nbq_reader *r, unsigned int n, uint32_t *out);
nbq_result nbq_read_s32(nbq_reader *r, unsigned int n, int32_t *out);
nbq_result nbq_read_parts(nbq_reader *r, unsigned int n, nbq_u64_parts *out);
nbq_result nbq_read_varint(nbq_reader *r, unsigned int base, uint64_t *out);
nbq_result nbq_read_varint_s(nbq_reader *r, unsigned int base, int64_t *out);
nbq_result nbq_read_bits(nbq_reader *r, uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos,
                         uint64_t n);

/* Sequential writes. */
nbq_result nbq_write_u64(nbq_writer *w, unsigned int n, uint64_t value);
nbq_result nbq_write_s64(nbq_writer *w, unsigned int n, int64_t value);
nbq_result nbq_write_u32(nbq_writer *w, unsigned int n, uint32_t value);
nbq_result nbq_write_s32(nbq_writer *w, unsigned int n, int32_t value);
nbq_result nbq_write_parts(nbq_writer *w, unsigned int n, nbq_u64_parts value);
nbq_result nbq_write_varint(nbq_writer *w, unsigned int base, uint64_t value);
nbq_result nbq_write_varint_s(nbq_writer *w, unsigned int base, int64_t value);
nbq_result nbq_write_bits(nbq_writer *w, const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                          uint64_t n);

#ifdef __cplusplus
}
#endif

#endif /* NBITQ_H */
