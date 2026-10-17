/**
 * @file nbitq_container.h
 * @brief Optional self-describing wrapper for a bit object. `.nbq` files.
 *
 * nbitq proper has no format: it puts bits somewhere and gets them back, and
 * what the bits mean is never its business. That is the right default, and it
 * leaves one gap -- you cannot hand someone a run of 1,234 bits and have them
 * know how long it is or what it was.
 *
 * This file closes that gap and nothing else. It is a container, not a
 * format for the payload: it records how many bits there are, a free-form
 * type string saying what they are so the *next* layer can dispatch, and a
 * checksum. It never looks inside.
 *
 * It is a **separate translation unit on purpose.** Do not compile it and the
 * core stays a few hundred lines with no notion of files, magic numbers or
 * checksums. Embedded users generally want exactly that.
 *
 *     [ "NBQ\1" ][ varint type_len ][ type_str ][ varint n_bits ]
 *     [ payload, n_bits, zero-padded to a byte ][ CRC-64 ]
 *
 * A CRC means the bytes are intact. It never means they are trustworthy:
 * anyone who can hand you a file can hand you a matching checksum.
 *
 * Copyright (c) 2026 M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#ifndef NBITQ_CONTAINER_H
#define NBITQ_CONTAINER_H

#include "nbitq.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Magic, including a container-format version in the last byte. */
#define NBQ_MAGIC_0 'N'
#define NBQ_MAGIC_1 'B'
#define NBQ_MAGIC_2 'Q'
#define NBQ_CONTAINER_VERSION 1

/** Conventional file extension. */
#define NBQ_FILE_EXTENSION ".nbq"

/** Longest type string this accepts. Bounded so a corrupt length cannot make
 *  a reader allocate or scan without limit. */
#define NBQ_TYPE_MAX 255u

/**
 * @brief A parsed container. All pointers alias the caller's buffer; nothing
 *        is copied and nothing is owned.
 */
typedef struct {
    const char *type;      /**< type string; NOT NUL-terminated */
    size_t type_len;       /**< its length in bytes */
    const uint8_t *payload;/**< first byte of the payload */
    uint64_t payload_bits; /**< payload length in BITS */
    uint8_t version;       /**< container version from the magic */
} nbq_container;

/**
 * @brief Bytes a container will occupy. Call before nbq_container_write.
 *
 * @p type_len may be 0. Returns NBQ_ERR_PARAM for a type longer than
 * NBQ_TYPE_MAX.
 */
nbq_result nbq_container_size(size_t type_len, uint64_t payload_bits, uint64_t *out_bytes);

/**
 * @brief Write a container into @p out.
 *
 * @p payload supplies @p payload_bits bits starting at its bit 0. Any bits
 * between the end of the payload and the byte boundary are written as zero,
 * so two writers producing the same object produce the same bytes.
 *
 * @p out_len receives the total bytes written.
 */
nbq_result nbq_container_write(uint8_t *out, size_t out_cap, const char *type, size_t type_len,
                               const uint8_t *payload, uint64_t payload_bits, size_t *out_len);

/**
 * @brief Parse a container, verifying magic, lengths and CRC.
 *
 * Refuses rather than guesses: bad magic, an unknown version, a type or
 * payload length that does not fit the buffer, trailing bytes, or a CRC
 * mismatch. On success @p out aliases @p buf, which must outlive it.
 */
nbq_result nbq_container_read(const uint8_t *buf, size_t len, nbq_container *out);

/**
 * @brief Parse without verifying the CRC.
 *
 * For a buffer whose integrity is already established -- mapped from a file
 * that a transport checksummed, say. Every length is still validated: this
 * skips the checksum, never the bounds checks.
 */
nbq_result nbq_container_read_unchecked(const uint8_t *buf, size_t len, nbq_container *out);

/* ── CRC-64 ──────────────────────────────────────────────────────────────
 * CRC-64/XZ, also called CRC-64/GO-ECMA: ECMA-182 polynomial, reflected,
 * init and final XOR all ones. The same one xz uses, so it has published
 * test vectors and independent implementations to check against.
 *
 * Computed bitwise, with no lookup table. A 256-entry table is 2 KB of
 * rodata, which is a poor trade in a library whose whole freestanding build
 * is under 5 KB, and this is a container checksum rather than a hot loop.
 */

/** CRC-64/XZ of a buffer. */
uint64_t nbq_crc64(const uint8_t *data, size_t len);

/** Incremental form. Start with NBQ_CRC64_INIT, finish with nbq_crc64_final. */
#define NBQ_CRC64_INIT UINT64_MAX
uint64_t nbq_crc64_update(uint64_t crc, const uint8_t *data, size_t len);
uint64_t nbq_crc64_final(uint64_t crc);

#ifdef __cplusplus
}
#endif

#endif /* NBITQ_CONTAINER_H */
