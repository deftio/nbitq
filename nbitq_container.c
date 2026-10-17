/**
 * @file nbitq_container.c
 * @brief Optional `.nbq` container. See nbitq_container.h.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include "nbitq_container.h"

/* ── CRC-64/XZ ───────────────────────────────────────────────────────────
 * Reflected ECMA-182. Bitwise: eight iterations per byte, no table. The
 * reflected polynomial is the bit-reverse of 0x42F0E1EBA9EA3693.
 */
#define NBQ_CRC64_POLY_REFLECTED 0xC96C5795D7870F42ull

uint64_t nbq_crc64_update(uint64_t crc, const uint8_t *data, size_t len)
{
    if (!data)
        return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint64_t)data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1u) ? ((crc >> 1) ^ NBQ_CRC64_POLY_REFLECTED) : (crc >> 1);
    }
    return crc;
}

uint64_t nbq_crc64_final(uint64_t crc)
{
    return crc ^ UINT64_MAX;
}

uint64_t nbq_crc64(const uint8_t *data, size_t len)
{
    return nbq_crc64_final(nbq_crc64_update(NBQ_CRC64_INIT, data, len));
}

/* ── Layout ──────────────────────────────────────────────────────────── */

#define MAGIC_BYTES 4u
#define CRC_BYTES 8u

/* Header fields are byte-aligned varints, written through the bit API at
   byte positions so there is one implementation of varint encoding. */
static unsigned int varint_bytes(uint64_t v)
{
    return nbq_varint_bits(v, 8) / 8u;
}

nbq_result nbq_container_size(size_t type_len, uint64_t payload_bits, uint64_t *out_bytes)
{
    if (!out_bytes || type_len > NBQ_TYPE_MAX)
        return NBQ_ERR_PARAM;

    const uint64_t payload_bytes = NBQ_BYTES_FOR_BITS(payload_bits);
    uint64_t total = MAGIC_BYTES;
    total += varint_bytes((uint64_t)type_len);
    total += (uint64_t)type_len;
    total += varint_bytes(payload_bits);
    total += payload_bytes;
    total += CRC_BYTES;
    *out_bytes = total;
    return NBQ_OK;
}

nbq_result nbq_container_write(uint8_t *out, size_t out_cap, const char *type, size_t type_len,
                               const uint8_t *payload, uint64_t payload_bits, size_t *out_len)
{
    if (!out || (type_len > 0u && !type) || (payload_bits > 0u && !payload))
        return NBQ_ERR_PARAM;
    if (type_len > NBQ_TYPE_MAX)
        return NBQ_ERR_PARAM;

    uint64_t need = 0;
    nbq_result rc = nbq_container_size(type_len, payload_bits, &need);
    if (rc != NBQ_OK)
        return rc;
    if ((uint64_t)out_cap < need)
        return NBQ_ERR_RANGE;

    /* Zero the whole container first: the payload's final partial byte and
       nothing else must differ between two writers of the same object. */
    for (uint64_t i = 0; i < need; i++)
        out[i] = 0u;

    const uint64_t cap_bits = need * 8u;
    uint64_t pos = 0;

    out[0] = NBQ_MAGIC_0;
    out[1] = NBQ_MAGIC_1;
    out[2] = NBQ_MAGIC_2;
    out[3] = (uint8_t)NBQ_CONTAINER_VERSION;
    pos = MAGIC_BYTES * 8u;

    rc = nbq_put_varint(out, cap_bits, pos, 8, (uint64_t)type_len, &pos);
    if (rc != NBQ_OK)
        return rc;

    for (size_t i = 0; i < type_len; i++) {
        rc = nbq_put_u32(out, cap_bits, pos, 8, (uint8_t)type[i]);
        if (rc != NBQ_OK)
            return rc;
        pos += 8u;
    }

    rc = nbq_put_varint(out, cap_bits, pos, 8, payload_bits, &pos);
    if (rc != NBQ_OK)
        return rc;

    if (payload_bits > 0u) {
        rc = nbq_copy_bits(payload, payload_bits, 0, out, cap_bits, pos, payload_bits);
        if (rc != NBQ_OK)
            return rc;
        pos += payload_bits;
    }

    /* Payload is padded to a byte; the CRC starts on that boundary. */
    const uint64_t crc_off = need - CRC_BYTES;
    const uint64_t crc = nbq_crc64(out, (size_t)crc_off);
    for (unsigned i = 0; i < CRC_BYTES; i++)
        out[crc_off + i] = (uint8_t)(crc >> (56u - 8u * i)); /* big-endian */

    if (out_len)
        *out_len = (size_t)need;
    return NBQ_OK;
}

static nbq_result container_parse(const uint8_t *buf, size_t len, nbq_container *out, int check_crc)
{
    if (!buf || !out)
        return NBQ_ERR_PARAM;
    if (len < MAGIC_BYTES + 1u + 1u + CRC_BYTES)
        return NBQ_ERR_RANGE;

    if (buf[0] != NBQ_MAGIC_0 || buf[1] != NBQ_MAGIC_1 || buf[2] != NBQ_MAGIC_2)
        return NBQ_ERR_MALFORMED;
    if (buf[3] != (uint8_t)NBQ_CONTAINER_VERSION)
        return NBQ_ERR_MALFORMED; /* a version this build does not know */

    const uint64_t bits = (uint64_t)len * 8u;
    uint64_t pos = MAGIC_BYTES * 8u;
    uint64_t type_len = 0, payload_bits = 0;

    nbq_result rc = nbq_get_varint(buf, bits, pos, 8, &type_len, &pos);
    if (rc != NBQ_OK)
        return rc;
    if (type_len > NBQ_TYPE_MAX)
        return NBQ_ERR_MALFORMED;

    const uint64_t type_off_bytes = pos / 8u;
    if (type_len > (uint64_t)len || type_off_bytes + type_len > (uint64_t)len)
        return NBQ_ERR_MALFORMED;
    pos += type_len * 8u;

    rc = nbq_get_varint(buf, bits, pos, 8, &payload_bits, &pos);
    if (rc != NBQ_OK)
        return rc;

    /* The payload must fit, and the declared bit count must match the bytes
       actually present -- a length read from a file and trusted is how a
       reader is made to walk off its buffer. */
    const uint64_t payload_off = pos / 8u;
    const uint64_t payload_bytes = NBQ_BYTES_FOR_BITS(payload_bits);
    if (payload_bytes > (uint64_t)len)
        return NBQ_ERR_MALFORMED;
    if (payload_off + payload_bytes + CRC_BYTES != (uint64_t)len)
        return NBQ_ERR_MALFORMED; /* short, or trailing bytes */

    if (check_crc) {
        const uint64_t crc_off = (uint64_t)len - CRC_BYTES;
        uint64_t stored = 0;
        for (unsigned i = 0; i < CRC_BYTES; i++)
            stored = (stored << 8) | (uint64_t)buf[crc_off + i];
        if (stored != nbq_crc64(buf, (size_t)crc_off))
            return NBQ_ERR_MALFORMED;
    }

    out->version = buf[3];
    out->type = (type_len > 0u) ? (const char *)(buf + type_off_bytes) : (const char *)"";
    out->type_len = (size_t)type_len;
    out->payload = buf + payload_off;
    out->payload_bits = payload_bits;
    return NBQ_OK;
}

nbq_result nbq_container_read(const uint8_t *buf, size_t len, nbq_container *out)
{
    return container_parse(buf, len, out, 1);
}

nbq_result nbq_container_read_unchecked(const uint8_t *buf, size_t len, nbq_container *out)
{
    return container_parse(buf, len, out, 0);
}
