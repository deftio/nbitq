/**
 * @file nbitq32.h
 * @brief nbitq32: the u32 indexed pair of nbitq with 32-bit positions.
 *
 * For targets where 64-bit arithmetic is a cost, or does not exist: 8-bit
 * and 16-bit CPUs, and compilers such as SDCC. Two functions and their _le
 * twins, nothing else. A uint32_t value in and out, a uint32_t bit position
 * and length (a buffer of up to 512 MB), no cursor (`pos += n` is one), no
 * dependency on nbitq.h, no 64-bit type anywhere.
 *
 * Same bit numbering, same result codes (numerically), same refusals and
 * the same all-or-nothing rule as nbq_get_u32 / nbq_put_u32 in nbitq.c. The
 * bytes agree bit for bit; test_nbitq32.c proves that against nbitq.c for
 * every width and offset in both orders. The two files are independent:
 * copy nbitq32.h and nbitq32.c alone, or beside nbitq.h and nbitq.c.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#ifndef NBITQ32_H
#define NBITQ32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result codes; the same values as nbq_result in nbitq.h. */
typedef enum {
    NBQ32_OK        = 0,  /**< done */
    NBQ32_ERR_PARAM = -1, /**< null pointer, or a width outside 1..32 */
    NBQ32_ERR_RANGE = -2  /**< the field crosses the end of the buffer */
} nbq32_result;

/**
 * Read @p n bits (1..32) at @p bit_pos as an unsigned value, MSB first:
 * bit 0 is the most significant bit of byte 0 and the field's first bit is
 * the value's top bit. @p bit_len is the length of @p buf in bits. On
 * failure @p *out is untouched.
 */
nbq32_result nbq32_get(const uint8_t *buf, uint32_t bit_len, uint32_t bit_pos, unsigned int n,
                       uint32_t *out);

/**
 * Write the low @p n bits (1..32) of @p value at @p bit_pos, MSB first.
 * @p bit_cap is the capacity of @p buf in bits. Bits outside the field are
 * left alone; on failure nothing is written.
 */
nbq32_result nbq32_put(uint8_t *buf, uint32_t bit_cap, uint32_t bit_pos, unsigned int n,
                       uint32_t value);

/** The same two, LSB first: bit 0 is the least significant bit of byte 0 and
 *  the field's first bit is the value's bit 0 (DEFLATE, Vorbis). */
nbq32_result nbq32_get_le(const uint8_t *buf, uint32_t bit_len, uint32_t bit_pos, unsigned int n,
                          uint32_t *out);
nbq32_result nbq32_put_le(uint8_t *buf, uint32_t bit_cap, uint32_t bit_pos, unsigned int n,
                          uint32_t value);

/** MSB first under its explicit name. */
#define nbq32_get_be nbq32_get
#define nbq32_put_be nbq32_put

#ifdef __cplusplus
}
#endif

#endif /* NBITQ32_H */
