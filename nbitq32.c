/**
 * @file nbitq32.c
 * @brief nbitq32: nbq_get_u32 / nbq_put_u32 with 32-bit positions.
 *
 * The same algorithm as the fixed-width path in nbitq.c, with uint32_t
 * where that file has uint64_t and nothing else. It is a separate copy
 * rather than a configuration of nbitq.c so that this file has no 64-bit
 * type in it at all and builds where nbitq.c cannot (SDCC), and so that a
 * project can copy these two files and nothing else. test_nbitq32.c holds
 * the two copies to the same bytes.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include "nbitq32.h"

typedef enum { MSB_FIRST = 0, LSB_FIRST = 1 } order_t;

/* A bit is a byte pointer plus a small index: the divide by eight happens
 * once per call and the loop runs in unsigned int. */
static unsigned int bit_shift(unsigned int bit, order_t order)
{
    const unsigned int in_byte = bit & 7u;
    return (order == LSB_FIRST) ? in_byte : 7u - in_byte;
}

static unsigned int bit_get(const uint8_t *p, unsigned int bit, order_t order)
{
    return (unsigned int)((p[bit >> 3] >> bit_shift(bit, order)) & 1u);
}

static void bit_set(uint8_t *p, unsigned int bit, unsigned int v, order_t order)
{
    const uint8_t mask = (uint8_t)(1u << bit_shift(bit, order));
    if (v)
        p[bit >> 3] |= mask;
    else
        p[bit >> 3] = (uint8_t)(p[bit >> 3] & (uint8_t)~mask);
}

/* Would [pos, pos+n) fit inside limit bits? Never computes pos + n, which
 * could wrap when pos comes from untrusted input. */
static int in_range(uint32_t limit, uint32_t pos, uint32_t n)
{
    return n <= limit && pos <= limit - n;
}

static nbq32_result get_o(const uint8_t *buf, uint32_t bit_len, uint32_t bit_pos, unsigned int n,
                          uint32_t *out, order_t order)
{
    const uint8_t *p;
    unsigned int bit, i;
    uint32_t v = 0;

    if (!buf || !out || n == 0u || n > 32u)
        return NBQ32_ERR_PARAM;
    if (!in_range(bit_len, bit_pos, n))
        return NBQ32_ERR_RANGE;

    p = buf + (bit_pos >> 3);
    bit = (unsigned int)(bit_pos & 7u);
    if (order == LSB_FIRST) {
        for (i = 0; i < n; i++)
            v |= (uint32_t)bit_get(p, bit + i, order) << i;
    } else {
        for (i = 0; i < n; i++)
            v = (uint32_t)((v << 1) | (uint32_t)bit_get(p, bit + i, order));
    }
    *out = v;
    return NBQ32_OK;
}

static nbq32_result put_o(uint8_t *buf, uint32_t bit_cap, uint32_t bit_pos, unsigned int n,
                          uint32_t value, order_t order)
{
    uint8_t *p;
    unsigned int bit, i;

    if (!buf || n == 0u || n > 32u)
        return NBQ32_ERR_PARAM;
    if (!in_range(bit_cap, bit_pos, n))
        return NBQ32_ERR_RANGE;

    p = buf + (bit_pos >> 3);
    bit = (unsigned int)(bit_pos & 7u);
    if (order == LSB_FIRST) {
        for (i = 0; i < n; i++)
            bit_set(p, bit + i, (unsigned int)((value >> i) & 1u), order);
    } else {
        for (i = 0; i < n; i++)
            bit_set(p, bit + i, (unsigned int)((value >> (n - 1u - i)) & 1u), order);
    }
    return NBQ32_OK;
}

nbq32_result nbq32_get(const uint8_t *buf, uint32_t bit_len, uint32_t bit_pos, unsigned int n,
                       uint32_t *out)
{
    return get_o(buf, bit_len, bit_pos, n, out, MSB_FIRST);
}

nbq32_result nbq32_put(uint8_t *buf, uint32_t bit_cap, uint32_t bit_pos, unsigned int n,
                       uint32_t value)
{
    return put_o(buf, bit_cap, bit_pos, n, value, MSB_FIRST);
}

nbq32_result nbq32_get_le(const uint8_t *buf, uint32_t bit_len, uint32_t bit_pos, unsigned int n,
                          uint32_t *out)
{
    return get_o(buf, bit_len, bit_pos, n, out, LSB_FIRST);
}

nbq32_result nbq32_put_le(uint8_t *buf, uint32_t bit_cap, uint32_t bit_pos, unsigned int n,
                          uint32_t value)
{
    return put_o(buf, bit_cap, bit_pos, n, value, LSB_FIRST);
}
