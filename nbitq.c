/**
 * @file nbitq.c
 * @brief nbitq implementation. Correctness first; see README.md §Performance.
 *
 * Deliberately unoptimised. Every multi-bit operation loops over single bits
 * rather than loading words and masking. That costs roughly 3-5x on read
 * throughput and buys something worth more at this stage: there is one place
 * where a bit is addressed, so there is one place a bit can be addressed
 * wrongly. A word-at-a-time path belongs beside this one, cross-checked
 * against it, not instead of it.
 *
 * Bit order is a parameter of every internal routine. The public MSB-first
 * and LSB-first names are thin wrappers that fix it, so there is one
 * implementation of each operation and the two orders cannot drift apart.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include "nbitq.h"

/* The only hosted headers this library uses, and only for the optional
   allocating writer. Under NBQ_NO_MALLOC nothing here is included, which is
   what makes a -ffreestanding build possible. */
#ifndef NBQ_NO_MALLOC
#include <stdlib.h>
#include <string.h>
#endif

/* ── The single place a bit is addressed ─────────────────────────────────
 * MSB first: bit 0 is the most significant bit of byte 0. LSB first: bit 0
 * is the least significant. Everything else is built on these two functions;
 * if they are right, the library is right.
 */

static inline unsigned int bit_shift(uint64_t pos, nbq_bit_order order)
{
    const unsigned int in_byte = (unsigned int)(pos & 7u);
    return (order == NBQ_LSB_FIRST) ? in_byte : 7u - in_byte;
}

static inline unsigned int bit_get(const uint8_t *buf, uint64_t pos, nbq_bit_order order)
{
    return (unsigned int)((buf[pos >> 3] >> bit_shift(pos, order)) & 1u);
}

static inline void bit_set(uint8_t *buf, uint64_t pos, unsigned int v, nbq_bit_order order)
{
    const uint8_t mask = (uint8_t)(1u << bit_shift(pos, order));
    if (v)
        buf[pos >> 3] |= mask;
    else
        buf[pos >> 3] = (uint8_t)(buf[pos >> 3] & (uint8_t)~mask);
}

/* Would [pos, pos+n) fit inside `limit` bits? Written to be overflow-proof:
 * pos + n can wrap when pos comes from untrusted data. */
static inline int in_range(uint64_t limit, uint64_t pos, uint64_t n)
{
    return n <= limit && pos <= limit - n;
}

/* ── Results ─────────────────────────────────────────────────────────── */

const char *nbq_version_string(void)
{
    return NBQ_VERSION_STRING;
}

uint32_t nbq_version(void)
{
    return (uint32_t)NBQ_VERSION;
}

const char *nbq_strerror(nbq_result rc)
{
    switch (rc) {
    case NBQ_OK:            return "ok";
    case NBQ_ERR_PARAM:     return "invalid parameter";
    case NBQ_ERR_RANGE:     return "operation crosses the end of the buffer";
    case NBQ_ERR_OVERFLOW:  return "value does not fit";
    case NBQ_ERR_MALFORMED: return "malformed encoding";
    case NBQ_ERR_ALLOC:     return "out of memory";
    }
    return "unknown error";
}

/* ════════════════════════════════════════════════════════════════════════
 * Fixed-width integers, with the order as a parameter
 * ════════════════════════════════════════════════════════════════════════
 *
 * MSB first, the first stream bit of a field is the value's top bit, so a
 * value is assembled by shifting left and appending. LSB first, the first
 * stream bit is the value's bit 0, so a value is assembled by placing each
 * bit at its own index. Writing mirrors reading.
 */

static nbq_result get_u64_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                            uint64_t *out, nbq_bit_order order)
{
    if (!buf || !out || n == 0u || n > NBQ_MAX_WIDTH)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_len, bit_pos, n))
        return NBQ_ERR_RANGE;

    uint64_t v = 0;
    if (order == NBQ_LSB_FIRST) {
        for (unsigned int i = 0; i < n; i++)
            v |= (uint64_t)bit_get(buf, bit_pos + i, order) << i;
    } else {
        for (unsigned int i = 0; i < n; i++)
            v = (v << 1) | (uint64_t)bit_get(buf, bit_pos + i, order);
    }
    *out = v;
    return NBQ_OK;
}

/* Sign-extend an n-bit field. Done on the unsigned value because shifting a
 * negative signed value right is implementation-defined. */
static uint64_t sign_extend64(uint64_t raw, unsigned int n)
{
    if (n < 64u && (raw >> (n - 1u)) & 1u)
        raw |= ~((((uint64_t)1) << n) - 1u);
    return raw;
}

static nbq_result get_s64_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                            int64_t *out, nbq_bit_order order)
{
    uint64_t raw;
    nbq_result rc;
    if (!out)
        return NBQ_ERR_PARAM;
    rc = get_u64_o(buf, bit_len, bit_pos, n, &raw, order);
    if (rc != NBQ_OK)
        return rc;
    *out = (int64_t)sign_extend64(raw, n);
    return NBQ_OK;
}

static nbq_result get_u32_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                            uint32_t *out, nbq_bit_order order)
{
    if (!buf || !out || n == 0u || n > 32u)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_len, bit_pos, n))
        return NBQ_ERR_RANGE;

    uint32_t v = 0;
    if (order == NBQ_LSB_FIRST) {
        for (unsigned int i = 0; i < n; i++)
            v |= (uint32_t)bit_get(buf, bit_pos + i, order) << i;
    } else {
        for (unsigned int i = 0; i < n; i++)
            v = (uint32_t)((v << 1) | (uint32_t)bit_get(buf, bit_pos + i, order));
    }
    *out = v;
    return NBQ_OK;
}

static nbq_result get_s32_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                            int32_t *out, nbq_bit_order order)
{
    uint32_t raw;
    nbq_result rc;
    if (!out)
        return NBQ_ERR_PARAM;
    rc = get_u32_o(buf, bit_len, bit_pos, n, &raw, order);
    if (rc != NBQ_OK)
        return rc;
    if (n < 32u && ((raw >> (n - 1u)) & 1u))
        raw |= ~((((uint32_t)1) << n) - 1u);
    *out = (int32_t)raw;
    return NBQ_OK;
}

/* A field wider than 32 bits as two halves. MSB first the high half comes
 * out of the stream first; LSB first the low half does. Either way the halves
 * are the same ones nbq_get_u64 would produce. */
static nbq_result get_parts_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                              unsigned int n, nbq_u64_parts *out, nbq_bit_order order)
{
    if (!buf || !out || n == 0u || n > NBQ_MAX_WIDTH)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_len, bit_pos, n))
        return NBQ_ERR_RANGE;

    const unsigned int n_hi = (n > 32u) ? n - 32u : 0u;
    const unsigned int n_lo = n - n_hi;
    uint32_t hi = 0, lo = 0;
    unsigned int i;

    if (order == NBQ_LSB_FIRST) {
        for (i = 0; i < n_lo; i++)
            lo |= (uint32_t)bit_get(buf, bit_pos + i, order) << i;
        for (i = 0; i < n_hi; i++)
            hi |= (uint32_t)bit_get(buf, bit_pos + n_lo + i, order) << i;
    } else {
        for (i = 0; i < n_hi; i++)
            hi = (uint32_t)((hi << 1) | (uint32_t)bit_get(buf, bit_pos + i, order));
        for (i = 0; i < n_lo; i++)
            lo = (uint32_t)((lo << 1) | (uint32_t)bit_get(buf, bit_pos + n_hi + i, order));
    }
    out->hi = hi;
    out->lo = lo;
    return NBQ_OK;
}

static nbq_result put_u64_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            uint64_t value, nbq_bit_order order)
{
    if (!buf || n == 0u || n > NBQ_MAX_WIDTH)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_cap, bit_pos, n))
        return NBQ_ERR_RANGE;

    if (order == NBQ_LSB_FIRST) {
        for (unsigned int i = 0; i < n; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value >> i) & 1u), order);
    } else {
        for (unsigned int i = 0; i < n; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value >> (n - 1u - i)) & 1u), order);
    }
    return NBQ_OK;
}

static nbq_result put_s64_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            int64_t value, nbq_bit_order order)
{
    uint64_t raw = (uint64_t)value;
    if (n == 0u || n > NBQ_MAX_WIDTH)
        return NBQ_ERR_PARAM;
    if (n < 64u)
        raw &= (((uint64_t)1) << n) - 1u; /* keep the low n bits: two's complement */
    return put_u64_o(buf, bit_cap, bit_pos, n, raw, order);
}

static nbq_result put_u32_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            uint32_t value, nbq_bit_order order)
{
    if (!buf || n == 0u || n > 32u)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_cap, bit_pos, n))
        return NBQ_ERR_RANGE;

    if (order == NBQ_LSB_FIRST) {
        for (unsigned int i = 0; i < n; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value >> i) & 1u), order);
    } else {
        for (unsigned int i = 0; i < n; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value >> (n - 1u - i)) & 1u), order);
    }
    return NBQ_OK;
}

static nbq_result put_s32_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            int32_t value, nbq_bit_order order)
{
    uint32_t raw = (uint32_t)value;
    if (n == 0u || n > 32u)
        return NBQ_ERR_PARAM;
    if (n < 32u)
        raw &= (((uint32_t)1) << n) - 1u;
    return put_u32_o(buf, bit_cap, bit_pos, n, raw, order);
}

static nbq_result put_parts_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                              nbq_u64_parts value, nbq_bit_order order)
{
    if (!buf || n == 0u || n > NBQ_MAX_WIDTH)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_cap, bit_pos, n))
        return NBQ_ERR_RANGE;

    const unsigned int n_hi = (n > 32u) ? n - 32u : 0u;
    const unsigned int n_lo = n - n_hi;
    unsigned int i;

    if (order == NBQ_LSB_FIRST) {
        for (i = 0; i < n_lo; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value.lo >> i) & 1u), order);
        for (i = 0; i < n_hi; i++)
            bit_set(buf, bit_pos + n_lo + i, (unsigned int)((value.hi >> i) & 1u), order);
    } else {
        for (i = 0; i < n_hi; i++)
            bit_set(buf, bit_pos + i, (unsigned int)((value.hi >> (n_hi - 1u - i)) & 1u), order);
        for (i = 0; i < n_lo; i++)
            bit_set(buf, bit_pos + n_hi + i, (unsigned int)((value.lo >> (n_lo - 1u - i)) & 1u),
                    order);
    }
    return NBQ_OK;
}

/* ── Public names: MSB first (plain) and LSB first (_le) ──────────────── */

nbq_result nbq_get_u64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       uint64_t *out)
{
    return get_u64_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_get_s64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       int64_t *out)
{
    return get_s64_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_get_u32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       uint32_t *out)
{
    return get_u32_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_get_s32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                       int32_t *out)
{
    return get_s32_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_get_parts(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                         nbq_u64_parts *out)
{
    return get_parts_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_put_u64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       uint64_t value)
{
    return put_u64_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}
nbq_result nbq_put_s64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       int64_t value)
{
    return put_s64_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}
nbq_result nbq_put_u32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       uint32_t value)
{
    return put_u32_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}
nbq_result nbq_put_s32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                       int32_t value)
{
    return put_s32_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}
nbq_result nbq_put_parts(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                         nbq_u64_parts value)
{
    return put_parts_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}

nbq_result nbq_get_u64_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          uint64_t *out)
{
    return get_u64_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_get_s64_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          int64_t *out)
{
    return get_s64_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_get_u32_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          uint32_t *out)
{
    return get_u32_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_get_s32_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned int n,
                          int32_t *out)
{
    return get_s32_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_get_parts_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                            unsigned int n, nbq_u64_parts *out)
{
    return get_parts_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_put_u64_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          uint64_t value)
{
    return put_u64_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}
nbq_result nbq_put_s64_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          int64_t value)
{
    return put_s64_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}
nbq_result nbq_put_u32_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          uint32_t value)
{
    return put_u32_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}
nbq_result nbq_put_s32_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                          int32_t value)
{
    return put_s32_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}
nbq_result nbq_put_parts_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int n,
                            nbq_u64_parts value)
{
    return put_parts_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}

/* ════════════════════════════════════════════════════════════════════════
 * Varints at a caller-chosen group width
 * ════════════════════════════════════════════════════════════════════════
 *
 * The rules act on group values, so they are the same in both orders; only
 * where each group's bits land differs, through get_u64_o / put_u64_o.
 */

static unsigned int groups_limit(unsigned int payload)
{
    return (64u + payload - 1u) / payload;
}

unsigned int nbq_varint_bits(uint64_t value, unsigned int base)
{
    if (base < NBQ_VARINT_MIN_BASE || base > NBQ_VARINT_MAX_BASE)
        return 0u;

    const unsigned int payload = base - 1u;
    unsigned int groups = 0;
    do {
        groups++;
        value = (payload >= 64u) ? 0u : (value >> payload);
    } while (value != 0u);
    return groups * base;
}

unsigned int nbq_varint_bits_s(int64_t value, unsigned int base)
{
    const uint64_t zz = ((uint64_t)value << 1) ^ (uint64_t)(value >> 63);
    return nbq_varint_bits(zz, base);
}

static nbq_result get_varint_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                               unsigned int base, uint64_t *out, uint64_t *next_pos,
                               nbq_bit_order order)
{
    if (!buf || !out || base < NBQ_VARINT_MIN_BASE || base > NBQ_VARINT_MAX_BASE)
        return NBQ_ERR_PARAM;

    const unsigned int payload = base - 1u;
    const uint64_t mask = (payload >= 64u) ? UINT64_MAX : ((((uint64_t)1) << payload) - 1u);
    const uint64_t cont = ((uint64_t)1) << payload;
    const unsigned int limit = groups_limit(payload);

    uint64_t val = 0, pos = bit_pos;
    unsigned int shift = 0;

    for (unsigned int g = 0; g < limit; g++) {
        uint64_t group;
        nbq_result rc = get_u64_o(buf, bit_len, pos, base, &group, order);
        if (rc != NBQ_OK)
            return rc; /* nothing has been written to *out */
        pos += base;

        const uint64_t chunk = group & mask;

        /* Refuse rather than truncate. A value that does not fit 64 bits is
           malformed input, not a large number. shift is below 64 here:
           groups_limit ends the loop before a group could start past bit 63. */
        const unsigned int room = 64u - shift;
        if (room < 64u && (chunk >> room) != 0u)
            return NBQ_ERR_OVERFLOW;
        val |= chunk << shift;

        if ((group & cont) == 0u) {
            /* Canonical: only a single-group encoding may end in a zero
               payload. Longer ones that do are overlong. */
            if (g > 0u && chunk == 0u)
                return NBQ_ERR_MALFORMED;
            *out = val;
            if (next_pos)
                *next_pos = pos;
            return NBQ_OK;
        }
        shift += payload;
    }
    return NBQ_ERR_OVERFLOW; /* continuation still set past the last group */
}

static nbq_result get_varint_s_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                 unsigned int base, int64_t *out, uint64_t *next_pos,
                                 nbq_bit_order order)
{
    uint64_t zz;
    nbq_result rc;
    if (!out)
        return NBQ_ERR_PARAM;
    rc = get_varint_o(buf, bit_len, bit_pos, base, &zz, next_pos, order);
    if (rc != NBQ_OK)
        return rc;
    *out = (int64_t)((zz >> 1) ^ (~(zz & 1u) + 1u));
    return NBQ_OK;
}

static nbq_result put_varint_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                               uint64_t value, uint64_t *next_pos, nbq_bit_order order)
{
    if (!buf || base < NBQ_VARINT_MIN_BASE || base > NBQ_VARINT_MAX_BASE)
        return NBQ_ERR_PARAM;

    /* Atomic: check the whole encoding fits before writing any of it. */
    const unsigned int total = nbq_varint_bits(value, base);
    if (!in_range(bit_cap, bit_pos, total))
        return NBQ_ERR_RANGE;

    const unsigned int payload = base - 1u;
    const uint64_t mask = (payload >= 64u) ? UINT64_MAX : ((((uint64_t)1) << payload) - 1u);
    const uint64_t cont = ((uint64_t)1) << payload;
    uint64_t pos = bit_pos, v = value;

    do {
        uint64_t group = v & mask;
        v = (payload >= 64u) ? 0u : (v >> payload);
        if (v != 0u)
            group |= cont;
        (void)put_u64_o(buf, bit_cap, pos, base, group, order); /* range pre-checked */
        pos += base;
    } while (v != 0u);

    if (next_pos)
        *next_pos = pos;
    return NBQ_OK;
}

static nbq_result put_varint_s_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos,
                                 unsigned int base, int64_t value, uint64_t *next_pos,
                                 nbq_bit_order order)
{
    const uint64_t zz = ((uint64_t)value << 1) ^ (uint64_t)(value >> 63);
    return put_varint_o(buf, bit_cap, bit_pos, base, zz, next_pos, order);
}

nbq_result nbq_get_varint(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                          unsigned int base, uint64_t *out, uint64_t *next_pos)
{
    return get_varint_o(buf, bit_len, bit_pos, base, out, next_pos, NBQ_MSB_FIRST);
}
nbq_result nbq_get_varint_s(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                            unsigned int base, int64_t *out, uint64_t *next_pos)
{
    return get_varint_s_o(buf, bit_len, bit_pos, base, out, next_pos, NBQ_MSB_FIRST);
}
nbq_result nbq_put_varint(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                          uint64_t value, uint64_t *next_pos)
{
    return put_varint_o(buf, bit_cap, bit_pos, base, value, next_pos, NBQ_MSB_FIRST);
}
nbq_result nbq_put_varint_s(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                            int64_t value, uint64_t *next_pos)
{
    return put_varint_s_o(buf, bit_cap, bit_pos, base, value, next_pos, NBQ_MSB_FIRST);
}

nbq_result nbq_get_varint_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                             unsigned int base, uint64_t *out, uint64_t *next_pos)
{
    return get_varint_o(buf, bit_len, bit_pos, base, out, next_pos, NBQ_LSB_FIRST);
}
nbq_result nbq_get_varint_s_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                               unsigned int base, int64_t *out, uint64_t *next_pos)
{
    return get_varint_s_o(buf, bit_len, bit_pos, base, out, next_pos, NBQ_LSB_FIRST);
}
nbq_result nbq_put_varint_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned int base,
                             uint64_t value, uint64_t *next_pos)
{
    return put_varint_o(buf, bit_cap, bit_pos, base, value, next_pos, NBQ_LSB_FIRST);
}
nbq_result nbq_put_varint_s_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos,
                               unsigned int base, int64_t value, uint64_t *next_pos)
{
    return put_varint_s_o(buf, bit_cap, bit_pos, base, value, next_pos, NBQ_LSB_FIRST);
}

/* ════════════════════════════════════════════════════════════════════════
 * Bit arrays
 * ════════════════════════════════════════════════════════════════════════
 *
 * Positions are logical, in the stream's order, on both sides. The overlap
 * rule below is stated in logical positions too, and that is enough: the map
 * from logical to physical bits is one-to-one, so a logical range is clobbered
 * exactly when it would be in a plain array.
 */

static nbq_result copy_bits_o(const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                              uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n,
                              nbq_bit_order order)
{
    if (!src || !dst)
        return NBQ_ERR_PARAM;
    if (n == 0u)
        return NBQ_OK;
    if (!in_range(src_bits, src_pos, n) || !in_range(dst_bits, dst_pos, n))
        return NBQ_ERR_RANGE;

    /* Same buffer and overlapping: copying forwards would overwrite source
       bits before reading them when dst is ahead of src. */
    if (src == dst && dst_pos > src_pos && dst_pos < src_pos + n) {
        for (uint64_t i = n; i-- > 0;)
            bit_set(dst, dst_pos + i, bit_get(src, src_pos + i, order), order);
    } else {
        for (uint64_t i = 0; i < n; i++)
            bit_set(dst, dst_pos + i, bit_get(src, src_pos + i, order), order);
    }
    return NBQ_OK;
}

static nbq_result fill_bits_o(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n,
                              int value, nbq_bit_order order)
{
    if (!buf)
        return NBQ_ERR_PARAM;
    if (n == 0u)
        return NBQ_OK;
    if (!in_range(bit_cap, bit_pos, n))
        return NBQ_ERR_RANGE;

    for (uint64_t i = 0; i < n; i++)
        bit_set(buf, bit_pos + i, value ? 1u : 0u, order);
    return NBQ_OK;
}

static nbq_result compare_bits_o(const uint8_t *a, uint64_t a_bits, uint64_t a_pos,
                                 const uint8_t *b, uint64_t b_bits, uint64_t b_pos, uint64_t n,
                                 int *out, nbq_bit_order order)
{
    if (!a || !b || !out)
        return NBQ_ERR_PARAM;
    if (!in_range(a_bits, a_pos, n) || !in_range(b_bits, b_pos, n))
        return NBQ_ERR_RANGE;

    for (uint64_t i = 0; i < n; i++) {
        const unsigned int x = bit_get(a, a_pos + i, order);
        const unsigned int y = bit_get(b, b_pos + i, order);
        if (x != y) {
            *out = (x > y) ? 1 : -1;
            return NBQ_OK;
        }
    }
    *out = 0;
    return NBQ_OK;
}

static nbq_result popcount_bits_o(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                  uint64_t n, uint64_t *out, nbq_bit_order order)
{
    if (!buf || !out)
        return NBQ_ERR_PARAM;
    if (!in_range(bit_len, bit_pos, n))
        return NBQ_ERR_RANGE;

    uint64_t c = 0;
    for (uint64_t i = 0; i < n; i++)
        c += bit_get(buf, bit_pos + i, order);
    *out = c;
    return NBQ_OK;
}

nbq_result nbq_copy_bits(const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint8_t *dst,
                         uint64_t dst_bits, uint64_t dst_pos, uint64_t n)
{
    return copy_bits_o(src, src_bits, src_pos, dst, dst_bits, dst_pos, n, NBQ_MSB_FIRST);
}
nbq_result nbq_fill_bits(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n, int value)
{
    return fill_bits_o(buf, bit_cap, bit_pos, n, value, NBQ_MSB_FIRST);
}
nbq_result nbq_compare_bits(const uint8_t *a, uint64_t a_bits, uint64_t a_pos, const uint8_t *b,
                            uint64_t b_bits, uint64_t b_pos, uint64_t n, int *out)
{
    return compare_bits_o(a, a_bits, a_pos, b, b_bits, b_pos, n, out, NBQ_MSB_FIRST);
}
nbq_result nbq_popcount_bits(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, uint64_t n,
                             uint64_t *out)
{
    return popcount_bits_o(buf, bit_len, bit_pos, n, out, NBQ_MSB_FIRST);
}

nbq_result nbq_copy_bits_le(const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                            uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n)
{
    return copy_bits_o(src, src_bits, src_pos, dst, dst_bits, dst_pos, n, NBQ_LSB_FIRST);
}
nbq_result nbq_fill_bits_le(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n,
                            int value)
{
    return fill_bits_o(buf, bit_cap, bit_pos, n, value, NBQ_LSB_FIRST);
}
nbq_result nbq_compare_bits_le(const uint8_t *a, uint64_t a_bits, uint64_t a_pos,
                               const uint8_t *b, uint64_t b_bits, uint64_t b_pos, uint64_t n,
                               int *out)
{
    return compare_bits_o(a, a_bits, a_pos, b, b_bits, b_pos, n, out, NBQ_LSB_FIRST);
}
nbq_result nbq_popcount_bits_le(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos,
                                uint64_t n, uint64_t *out)
{
    return popcount_bits_o(buf, bit_len, bit_pos, n, out, NBQ_LSB_FIRST);
}

/* ════════════════════════════════════════════════════════════════════════
 * Cursor: every function here is an indexed call in the cursor's order,
 * plus an advance
 * ════════════════════════════════════════════════════════════════════════ */

static nbq_result reader_init_o(nbq_reader *r, const uint8_t *buf, uint64_t bit_len,
                                nbq_bit_order order)
{
    if (!r || !buf)
        return NBQ_ERR_PARAM;
    r->buf = buf;
    r->bits = bit_len;
    r->pos = 0;
    r->order = order;
    return NBQ_OK;
}

static nbq_result writer_init_o(nbq_writer *w, uint8_t *buf, uint64_t bit_cap,
                                nbq_bit_order order)
{
    if (!w || !buf)
        return NBQ_ERR_PARAM;
    w->buf = buf;
    w->cap = bit_cap;
    w->pos = 0;
    w->owns_buf = 0;
    w->order = order;
    return NBQ_OK;
}

nbq_result nbq_reader_init(nbq_reader *r, const uint8_t *buf, uint64_t bit_len)
{
    return reader_init_o(r, buf, bit_len, NBQ_MSB_FIRST);
}
nbq_result nbq_reader_init_le(nbq_reader *r, const uint8_t *buf, uint64_t bit_len)
{
    return reader_init_o(r, buf, bit_len, NBQ_LSB_FIRST);
}
nbq_result nbq_writer_init(nbq_writer *w, uint8_t *buf, uint64_t bit_cap)
{
    return writer_init_o(w, buf, bit_cap, NBQ_MSB_FIRST);
}
nbq_result nbq_writer_init_le(nbq_writer *w, uint8_t *buf, uint64_t bit_cap)
{
    return writer_init_o(w, buf, bit_cap, NBQ_LSB_FIRST);
}

#ifndef NBQ_NO_MALLOC
static nbq_result writer_init_alloc_o(nbq_writer *w, uint64_t initial_bits, nbq_bit_order order)
{
    if (!w)
        return NBQ_ERR_PARAM;
    if (initial_bits == 0u)
        initial_bits = 512u;
    const uint64_t bytes = NBQ_BYTES_FOR_BITS(initial_bits);
    if (bytes > (uint64_t)SIZE_MAX)
        return NBQ_ERR_ALLOC; /* LCOV_EXCL_LINE: only when size_t is narrower than 64 bits */
    /* calloc, not malloc: a partially written buffer must read back as zeros
       rather than as whatever the heap last held. */
    w->buf = (uint8_t *)calloc(1, (size_t)bytes);
    if (!w->buf)
        return NBQ_ERR_ALLOC;
    w->cap = bytes * 8u;
    w->pos = 0;
    w->owns_buf = 1;
    w->order = order;
    return NBQ_OK;
}

nbq_result nbq_writer_init_alloc(nbq_writer *w, uint64_t initial_bits)
{
    return writer_init_alloc_o(w, initial_bits, NBQ_MSB_FIRST);
}
nbq_result nbq_writer_init_alloc_le(nbq_writer *w, uint64_t initial_bits)
{
    return writer_init_alloc_o(w, initial_bits, NBQ_LSB_FIRST);
}

void nbq_writer_free(nbq_writer *w)
{
    if (w && w->owns_buf) {
        free(w->buf);
        w->buf = NULL;
        w->cap = 0;
        w->pos = 0;
        w->owns_buf = 0;
    }
}

#endif /* NBQ_NO_MALLOC */

/* Grow an owning writer so that n more bits fit. A fixed writer refuses. */
static nbq_result writer_reserve(nbq_writer *w, uint64_t n)
{
    if (in_range(w->cap, w->pos, n))
        return NBQ_OK;
    if (!w->owns_buf)
        return NBQ_ERR_RANGE;
#ifdef NBQ_NO_MALLOC
    return NBQ_ERR_RANGE; /* no growth without an allocator */
#else

    uint64_t need_bits = w->pos + n;
    uint64_t new_cap = (w->cap < 512u) ? 512u : w->cap;
    while (new_cap < need_bits) {
        if (new_cap > UINT64_MAX / 2u)
            return NBQ_ERR_ALLOC;
        new_cap *= 2u;
    }
    const uint64_t new_bytes = NBQ_BYTES_FOR_BITS(new_cap);
    const uint64_t old_bytes = NBQ_BYTES_FOR_BITS(w->cap);
    if (new_bytes > (uint64_t)SIZE_MAX)
        return NBQ_ERR_ALLOC; /* LCOV_EXCL_LINE: only when size_t is narrower than 64 bits */
    uint8_t *grown = (uint8_t *)realloc(w->buf, (size_t)new_bytes);
    if (!grown)
        return NBQ_ERR_ALLOC;
    memset(grown + old_bytes, 0, (size_t)(new_bytes - old_bytes));
    w->buf = grown;
    w->cap = new_bytes * 8u;
    return NBQ_OK;
#endif /* NBQ_NO_MALLOC */
}

uint64_t nbq_reader_remaining(const nbq_reader *r)
{
    if (!r || r->pos >= r->bits)
        return 0;
    return r->bits - r->pos;
}

nbq_result nbq_reader_seek(nbq_reader *r, uint64_t bit_pos)
{
    if (!r)
        return NBQ_ERR_PARAM;
    if (bit_pos > r->bits)
        return NBQ_ERR_RANGE;
    r->pos = bit_pos;
    return NBQ_OK;
}

nbq_result nbq_writer_seek(nbq_writer *w, uint64_t bit_pos)
{
    if (!w)
        return NBQ_ERR_PARAM;
    if (bit_pos > w->cap) {
        if (!w->owns_buf)
            return NBQ_ERR_RANGE;
        const uint64_t save = w->pos;
        w->pos = bit_pos;
        nbq_result rc = writer_reserve(w, 0);
        if (rc != NBQ_OK) {
            w->pos = save;
            return rc;
        }
    }
    w->pos = bit_pos;
    return NBQ_OK;
}

nbq_result nbq_reader_align(nbq_reader *r, unsigned int n)
{
    if (!r || n == 0u)
        return NBQ_ERR_PARAM;
    const uint64_t rem = r->pos % n;
    if (rem == 0u)
        return NBQ_OK;
    const uint64_t pad = n - rem;
    if (!in_range(r->bits, r->pos, pad))
        return NBQ_ERR_RANGE;
    /* Padding must be zero, or the stream is not what the writer produced. */
    for (uint64_t i = 0; i < pad; i++)
        if (bit_get(r->buf, r->pos + i, r->order))
            return NBQ_ERR_MALFORMED;
    r->pos += pad;
    return NBQ_OK;
}

nbq_result nbq_writer_align(nbq_writer *w, unsigned int n)
{
    if (!w || n == 0u)
        return NBQ_ERR_PARAM;
    const uint64_t rem = w->pos % n;
    if (rem == 0u)
        return NBQ_OK;
    const uint64_t pad = n - rem;
    nbq_result rc = writer_reserve(w, pad);
    if (rc != NBQ_OK)
        return rc;
    (void)fill_bits_o(w->buf, w->cap, w->pos, pad, 0, w->order); /* range reserved above */
    w->pos += pad;
    return NBQ_OK;
}

/* -- reads -- */

#define READ_WRAPPER(name, type, call)                                                             \
    nbq_result name(nbq_reader *r, unsigned int n, type *out)                                       \
    {                                                                                              \
        if (!r)                                                                                    \
            return NBQ_ERR_PARAM;                                                                  \
        nbq_result rc = call(r->buf, r->bits, r->pos, n, out, r->order);                            \
        if (rc == NBQ_OK)                                                                          \
            r->pos += n;                                                                           \
        return rc;                                                                                 \
    }

READ_WRAPPER(nbq_read_u64, uint64_t, get_u64_o)
READ_WRAPPER(nbq_read_s64, int64_t, get_s64_o)
READ_WRAPPER(nbq_read_u32, uint32_t, get_u32_o)
READ_WRAPPER(nbq_read_s32, int32_t, get_s32_o)
READ_WRAPPER(nbq_read_parts, nbq_u64_parts, get_parts_o)

nbq_result nbq_read_varint(nbq_reader *r, unsigned int base, uint64_t *out)
{
    if (!r)
        return NBQ_ERR_PARAM;
    uint64_t next = 0;
    nbq_result rc = get_varint_o(r->buf, r->bits, r->pos, base, out, &next, r->order);
    if (rc == NBQ_OK)
        r->pos = next;
    return rc;
}

nbq_result nbq_read_varint_s(nbq_reader *r, unsigned int base, int64_t *out)
{
    if (!r)
        return NBQ_ERR_PARAM;
    uint64_t next = 0;
    nbq_result rc = get_varint_s_o(r->buf, r->bits, r->pos, base, out, &next, r->order);
    if (rc == NBQ_OK)
        r->pos = next;
    return rc;
}

nbq_result nbq_read_bits(nbq_reader *r, uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos,
                         uint64_t n)
{
    if (!r)
        return NBQ_ERR_PARAM;
    nbq_result rc = copy_bits_o(r->buf, r->bits, r->pos, dst, dst_bits, dst_pos, n, r->order);
    if (rc == NBQ_OK)
        r->pos += n;
    return rc;
}

/* -- writes -- */

#define WRITE_WRAPPER(name, type, call)                                                            \
    nbq_result name(nbq_writer *w, unsigned int n, type value)                                      \
    {                                                                                              \
        if (!w)                                                                                    \
            return NBQ_ERR_PARAM;                                                                  \
        if (n == 0u || n > NBQ_MAX_WIDTH)                                                          \
            return NBQ_ERR_PARAM;                                                                  \
        nbq_result rc = writer_reserve(w, n);                                                       \
        if (rc != NBQ_OK)                                                                          \
            return rc;                                                                             \
        rc = call(w->buf, w->cap, w->pos, n, value, w->order);                                      \
        if (rc == NBQ_OK)                                                                          \
            w->pos += n;                                                                           \
        return rc;                                                                                 \
    }

WRITE_WRAPPER(nbq_write_u64, uint64_t, put_u64_o)
WRITE_WRAPPER(nbq_write_s64, int64_t, put_s64_o)
WRITE_WRAPPER(nbq_write_u32, uint32_t, put_u32_o)
WRITE_WRAPPER(nbq_write_s32, int32_t, put_s32_o)
WRITE_WRAPPER(nbq_write_parts, nbq_u64_parts, put_parts_o)

nbq_result nbq_write_varint(nbq_writer *w, unsigned int base, uint64_t value)
{
    if (!w)
        return NBQ_ERR_PARAM;
    const unsigned int need = nbq_varint_bits(value, base);
    if (need == 0u)
        return NBQ_ERR_PARAM;
    nbq_result rc = writer_reserve(w, need);
    if (rc != NBQ_OK)
        return rc;
    uint64_t next = 0;
    rc = put_varint_o(w->buf, w->cap, w->pos, base, value, &next, w->order);
    if (rc == NBQ_OK)
        w->pos = next;
    return rc;
}

nbq_result nbq_write_varint_s(nbq_writer *w, unsigned int base, int64_t value)
{
    const uint64_t zz = ((uint64_t)value << 1) ^ (uint64_t)(value >> 63);
    return nbq_write_varint(w, base, zz);
}

nbq_result nbq_write_bits(nbq_writer *w, const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                          uint64_t n)
{
    if (!w)
        return NBQ_ERR_PARAM;
    nbq_result rc = writer_reserve(w, n);
    if (rc != NBQ_OK)
        return rc;
    rc = copy_bits_o(src, src_bits, src_pos, w->buf, w->cap, w->pos, n, w->order);
    if (rc == NBQ_OK)
        w->pos += n;
    return rc;
}
