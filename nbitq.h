/**
 * @file nbitq.h
 * @brief nbitq -- pack and unpack n-bit integers in a bit stream. C99, no dependencies.
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
 * ## Two bit orders, one implementation
 *
 * A bit stream is numbered either from the most significant bit of each byte
 * (MSB first: H.264, MPEG, most network formats) or from the least (LSB
 * first: DEFLATE, Vorbis, most PC-heritage packers). nbitq handles both.
 * The plain names are MSB first. The `_le` names and the `_le` cursor
 * initialisers are LSB first. Underneath there is one implementation that
 * takes the order as a parameter, so the two cannot drift apart. See
 * nbq_bit_order below.
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
 * - **Bit order is a property of the stream, declared once.** MSB first
 *   unless a call or a cursor says `_le`. Within an order, the layout is
 *   fixed: this is the one convention that, left ambiguous, makes two
 *   implementations disagree about every file.
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
#define NBQ_VERSION_MINOR 2
#define NBQ_VERSION_PATCH 0
#define NBQ_VERSION_STRING "0.2.0"

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

/* ── Bit order ───────────────────────────────────────────────────────────
 * Which bit of a byte is bit 0, and which end of a value goes into the
 * stream first. The two always travel together in real formats.
 *
 * This is bit order, not byte order, but byte order follows from it: a
 * 16-bit value at a byte-aligned position comes out big-endian in an
 * MSB-first stream and little-endian in an LSB-first one. That is why the
 * suffixes are `_be` and `_le`, the names codec bit readers use.
 *
 * Everything else is the same in both orders. Signed fields, zigzag, the
 * float forms and the varint rules all act on the assembled integer, not on
 * the stream. A varint's groups land wherever the order puts them, so the
 * bytes differ between orders unless a group happens to be byte-aligned.
 */
typedef enum {
    /** Bit 0 is the MSB of byte 0; a value's MSB goes first. The plain
     *  names use this order. */
    NBQ_MSB_FIRST = 0,
    /** Bit 0 is the LSB of byte 0; a value's LSB goes first. The `_le`
     *  names use this order. */
    NBQ_LSB_FIRST = 1
} nbq_bit_order;

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
 *
 * Every function here is MSB first. Each has an `_le` twin with the same
 * signature that is LSB first, declared together below, and a `_be` spelling
 * of the plain name so that code comparing two streams can read
 * symmetrically. The `_be` names are macros, not second symbols.
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

/* The same ten, LSB first. */
nbq_result nbq_get_u64_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          uint64_t *out);
nbq_result nbq_get_s64_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          int64_t *out);
nbq_result nbq_get_u32_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          uint32_t *out);
nbq_result nbq_get_s32_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          int32_t *out);
nbq_result nbq_get_parts_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                            unsigned int n, nbq_u64_parts *out);
nbq_result nbq_put_u64_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          uint64_t value);
nbq_result nbq_put_s64_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          int64_t value);
nbq_result nbq_put_u32_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          uint32_t value);
nbq_result nbq_put_s32_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          int32_t value);
nbq_result nbq_put_parts_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            nbq_u64_parts value);

#define nbq_get_u64_be   nbq_get_u64
#define nbq_get_s64_be   nbq_get_s64
#define nbq_get_u32_be   nbq_get_u32
#define nbq_get_s32_be   nbq_get_s32
#define nbq_get_parts_be nbq_get_parts
#define nbq_put_u64_be   nbq_put_u64
#define nbq_put_s64_be   nbq_put_s64
#define nbq_put_u32_be   nbq_put_u32
#define nbq_put_s32_be   nbq_put_s32
#define nbq_put_parts_be nbq_put_parts

/* ── Floating point ───────────────────────────────────────────────────
 * A float or double as its IEEE-754 bit pattern in a 32- or 64-bit field.
 * MSB first the bytes are big-endian at an aligned position; `_le` they are
 * little-endian. This is a copy, not a conversion: NaN payloads, signed
 * zeros and denormals come back bit for bit, and no floating-point
 * arithmetic or runtime is involved, so a soft-float target pays for a
 * register move and nothing else. Everything here is static inline: nbitq.c
 * gains no code, and a program that never calls these links nothing for
 * them.
 *
 * The reinterpretation is a union, which C99 defines (6.5.2.3 note 95) and
 * every C++ compiler this library targets treats the same way; memcpy is
 * not used because the freestanding build has no <string.h>.
 */
static inline uint32_t nbq_f32_bits(float f)
{
    union { float f; uint32_t u; } x;
    x.f = f;
    return x.u;
}
static inline float nbq_f32_from_bits(uint32_t u)
{
    union { float f; uint32_t u; } x;
    x.u = u;
    return x.f;
}
static inline uint64_t nbq_f64_bits(double d)
{
    union { double d; uint64_t u; } x;
    x.d = d;
    return x.u;
}
static inline double nbq_f64_from_bits(uint64_t u)
{
    union { double d; uint64_t u; } x;
    x.u = u;
    return x.d;
}

static inline nbq_result nbq_put_f32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, float value)
{
    return nbq_put_u32(buf, bit_cap, bit_pos, 32u, nbq_f32_bits(value));
}
static inline nbq_result nbq_get_f32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                     float *out)
{
    uint32_t u;
    nbq_result rc = nbq_get_u32(buf, bit_len, bit_pos, 32u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f32_from_bits(u);
    return rc;
}
static inline nbq_result nbq_put_f64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, double value)
{
    return nbq_put_u64(buf, bit_cap, bit_pos, 64u, nbq_f64_bits(value));
}
static inline nbq_result nbq_get_f64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                     double *out)
{
    uint64_t u;
    nbq_result rc = nbq_get_u64(buf, bit_len, bit_pos, 64u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f64_from_bits(u);
    return rc;
}

static inline nbq_result nbq_put_f32_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos,
                                        float value)
{
    return nbq_put_u32_le(buf, bit_cap, bit_pos, 32u, nbq_f32_bits(value));
}
static inline nbq_result nbq_get_f32_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                        float *out)
{
    uint32_t u;
    nbq_result rc = nbq_get_u32_le(buf, bit_len, bit_pos, 32u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f32_from_bits(u);
    return rc;
}
static inline nbq_result nbq_put_f64_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos,
                                        double value)
{
    return nbq_put_u64_le(buf, bit_cap, bit_pos, 64u, nbq_f64_bits(value));
}
static inline nbq_result nbq_get_f64_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                        double *out)
{
    uint64_t u;
    nbq_result rc = nbq_get_u64_le(buf, bit_len, bit_pos, 64u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f64_from_bits(u);
    return rc;
}

#define nbq_put_f32_be nbq_put_f32
#define nbq_get_f32_be nbq_get_f32
#define nbq_put_f64_be nbq_put_f64
#define nbq_get_f64_be nbq_get_f64

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

/* The same four, LSB first: each group's bits, and the groups themselves,
   in stream order from the LSB. */
nbq_result nbq_get_varint_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                             unsigned int base, uint64_t *out, uint64_t *next_pos);
nbq_result nbq_get_varint_s_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                               unsigned int base, int64_t *out, uint64_t *next_pos);
nbq_result nbq_put_varint_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                             uint64_t value, uint64_t *next_pos);
nbq_result nbq_put_varint_s_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos,
                               unsigned int base, int64_t value, uint64_t *next_pos);

#define nbq_get_varint_be   nbq_get_varint
#define nbq_get_varint_s_be nbq_get_varint_s
#define nbq_put_varint_be   nbq_put_varint
#define nbq_put_varint_s_be nbq_put_varint_s

/** Bits @p value occupies at @p base, without writing. 0 if base is invalid.
 *  The same in both orders. Lets a caller compare widths over a real
 *  corpus; this library does not choose for you. */
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
 *
 * Positions are in the stream's order on both sides of a copy or compare.
 * Moving a run between streams of different orders is a transcode, not a
 * copy: read it as values in one order and write them in the other.
 */

/** Copy @p n bits from @p src at @p src_pos to @p dst at @p dst_pos.
 *  Buffers may be the same; overlapping ranges are handled correctly. */
nbq_result nbq_copy_bits(const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint8_t *dst,
                         uint64_t dst_bits, uint64_t dst_pos, uint64_t n);

/** Set @p n bits at @p bit_pos to 0 or 1. */
nbq_result nbq_fill_bits(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n, int value);

/** Compare @p n bits at two positions. @p out is <0, 0 or >0 like memcmp,
 *  decided by the first differing bit in stream order. */
nbq_result nbq_compare_bits(const uint8_t *a, uint64_t a_bits, uint64_t a_pos, const uint8_t *b,
                            uint64_t b_bits, uint64_t b_pos, uint64_t n, int *out);

/** Count the 1 bits in @p n bits at @p bit_pos. */
nbq_result nbq_popcount_bits(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, uint64_t n,
                             uint64_t *out);

/* The same four, LSB first. */
nbq_result nbq_copy_bits_le(const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                            uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n);
nbq_result nbq_fill_bits_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n,
                            int value);
nbq_result nbq_compare_bits_le(const uint8_t *a, uint64_t a_bits, uint64_t a_pos,
                               const uint8_t *b, uint64_t b_bits, uint64_t b_pos, uint64_t n,
                               int *out);
nbq_result nbq_popcount_bits_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                uint64_t n, uint64_t *out);

#define nbq_copy_bits_be     nbq_copy_bits
#define nbq_fill_bits_be     nbq_fill_bits
#define nbq_compare_bits_be  nbq_compare_bits
#define nbq_popcount_bits_be nbq_popcount_bits

/* Written without "+ 7" so a bit count near UINT64_MAX cannot wrap to a small number. */
#define NBQ_BYTES_FOR_BITS(bits) (((uint64_t)(bits) / 8u) + (((uint64_t)(bits) & 7u) ? 1u : 0u))

/* ════════════════════════════════════════════════════════════════════════
 * CURSOR API -- a position that advances. Thin wrappers over the above.
 * ════════════════════════════════════════════════════════════════════════
 *
 * The structs are public and contain no hidden state, so a caller may place
 * one on the stack, in static storage, or inside its own object. There is no
 * create/destroy pair to forget.
 *
 * A cursor carries its stream's bit order, set by the initialiser (`_le`
 * for LSB first) and followed by every read or write through it. There are
 * no `_le` read or write functions: a stream has one order, and a per-call
 * order on a cursor is how a stream gets misread from that point on.
 */

typedef struct {
    const uint8_t *buf;
    uint64_t bits;       /**< valid bits in buf */
    uint64_t pos;        /**< next bit to read */
    nbq_bit_order order; /**< set by nbq_reader_init (MSB) or nbq_reader_init_le */
} nbq_reader;

typedef struct {
    uint8_t *buf;
    uint64_t cap;        /**< capacity in bits */
    uint64_t pos;        /**< next bit to write */
    int owns_buf;        /**< nonzero if nbq_writer_init_alloc allocated buf; always
                              0 under NBQ_NO_MALLOC, where the writer never grows */
    nbq_bit_order order; /**< set by nbq_writer_init (MSB) or nbq_writer_init_le */
} nbq_writer;

/** Point a reader at a buffer, MSB first. Allocates nothing; @p buf may be in ROM. */
nbq_result nbq_reader_init(nbq_reader *r, const uint8_t *buf, uint64_t bit_len);

/** The same, LSB first. */
nbq_result nbq_reader_init_le(nbq_reader *r, const uint8_t *buf, uint64_t bit_len);

/** Point a writer at caller-owned storage, MSB first. Allocates nothing,
 *  never grows; a write past @p bit_cap returns NBQ_ERR_RANGE. */
nbq_result nbq_writer_init(nbq_writer *w, uint8_t *buf, uint64_t bit_cap);

/** The same, LSB first. */
nbq_result nbq_writer_init_le(nbq_writer *w, uint8_t *buf, uint64_t bit_cap);

/**
 * A writer that owns a heap buffer and doubles it as needed, MSB first.
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

/** The same, LSB first. */
nbq_result nbq_writer_init_alloc_le(nbq_writer *w, uint64_t initial_bits);

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

/* Sequential reads. Each is the indexed call at the cursor, in the cursor's
   order, then advance. */
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

/* Floating point at the cursor; see the indexed forms above. A growing
   writer grows for these exactly as it does for nbq_write_u32/u64. */
static inline nbq_result nbq_read_f32(nbq_reader *r, float *out)
{
    uint32_t u;
    nbq_result rc = nbq_read_u32(r, 32u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f32_from_bits(u);
    return rc;
}
static inline nbq_result nbq_read_f64(nbq_reader *r, double *out)
{
    uint64_t u;
    nbq_result rc = nbq_read_u64(r, 64u, &u);
    if (rc == NBQ_OK && out)
        *out = nbq_f64_from_bits(u);
    return rc;
}
static inline nbq_result nbq_write_f32(nbq_writer *w, float value)
{
    return nbq_write_u32(w, 32u, nbq_f32_bits(value));
}
static inline nbq_result nbq_write_f64(nbq_writer *w, double value)
{
    return nbq_write_u64(w, 64u, nbq_f64_bits(value));
}

#ifdef __cplusplus
}
#endif

#endif /* NBITQ_H */
