# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The GitHub release notes for a version are taken from its section here, so
keep each section self-contained.

## [Unreleased]

## [0.2.0] - 2026-10-17

### Added
- Bit order. Every indexed function has an `_le` twin with the same
  signature that is LSB first (bit 0 is the LSB of byte 0, a value's bit 0
  goes first, as DEFLATE and Vorbis do) and a `_be` macro spelling of the
  plain MSB-first name. `nbq_reader_init_le`, `nbq_writer_init_le` and
  `nbq_writer_init_alloc_le` set an `order` field on the cursor structs,
  which every cursor function follows. One implementation underneath with
  the order as a parameter; the plain names behave exactly as before.
  `nbq_bit_order` is the enum. The C++ wrapper takes the order on `init`
  and reports it with `order()`.
- Tests: a bit-string model of the stream checks every width, offset and
  pattern in both orders; a mirror identity ties the orders to each other;
  raw DEFLATE blocks produced by zlib are read field by field and written
  back byte for byte; the fuzz harness takes the order as an input. The
  core suite is 228,031 checks.
- Documentation of the two backward-reading recipes (fixed widths, and
  varints by their continuation bits) under "Not provided".

### Changed
- `nbq_reader` and `nbq_writer` gained a trailing `order` field. Code that
  initialises them through the init functions is unaffected; code that
  filled the structs by hand must set it. This is the reason for the minor
  version.
- Code size: the core is 5,901 bytes at -Oz on x86-64 and 6,555 on
  Cortex-M4 (was 4,749 and 4,793), from carrying both assembly loops and
  the `_le` entry points. Correctness over size, by design.

## [0.1.2] - 2026-10-17

### Added
- `nbitq.hpp`: a header-only C++11 wrapper over the cursor API, as
  `nbq::Reader`, `nbq::Writer` and `nbq::OwningWriter`. Each method is one
  call into the C library with the `nbq_read_` / `nbq_write_` prefix
  dropped. No exceptions, nothing from the standard library; the indexed
  functions are not wrapped.
- `make coverage`: builds the library with gcov instrumentation, runs every
  test binary against it and fails unless every line of nbitq.c,
  nbitq_container.c, nbitq.h and nbitq.hpp ran. CI runs it with gcc and
  gcov and publishes the result as the README badge.
- Tests for every refusal at every entry point, the cursor forms of signed
  64-bit values, hi/lo parts and bit runs, reader remaining/seek/align
  edges, the allocating writer's limits and malformed container headers.
  The C++ test calls every method of the wrapper and every inline function
  in nbitq.h.

### Fixed
- `NBQ_BYTES_FOR_BITS` wrapped to a small number for bit counts within 7 of
  UINT64_MAX; `nbq_writer_init_alloc(w, UINT64_MAX)` now returns
  NBQ_ERR_ALLOC instead of a zero-capacity writer.

### Changed
- Removed branches the tests proved unreachable: a `shift >= 64` case in
  `nbq_get_varint` that `groups_limit` already prevents, a fill that cannot
  fail after `nbq_writer_align` has reserved its range, and the per-field
  checks in `nbq_container_write` for a buffer `nbq_container_size` has
  already sized. Nothing observable changes; the container is 32 bytes
  smaller at -Oz.
- The website has a favicon and the README badges show GitHub release,
  coverage and licence.

## [0.1.1] - 2026-10-17

### Added
- Floating point: `nbq_get_f32` / `nbq_put_f32` / `nbq_get_f64` /
  `nbq_put_f64` and the cursor forms `nbq_read_f32` / `nbq_write_f32` /
  `nbq_read_f64` / `nbq_write_f64`. A float or double is stored as its
  IEEE-754 bit pattern in a 32- or 64-bit field, so NaN payloads and signed
  zeros survive and no floating-point arithmetic is involved. All are
  `static inline` in `nbitq.h`; `nbitq.c` is unchanged. The reinterpretation
  helpers `nbq_f32_bits` / `nbq_f32_from_bits` / `nbq_f64_bits` /
  `nbq_f64_from_bits` are public.

## [0.1.0] - 2026-10-16

First release as a standalone repository. The API descends from
`GetNBitQty` / `PutNBitQty` in dio_BitStream (M. A. Chatterjee, 2000); the
implementation is new.

### Added
- Indexed API: `nbq_get_u64/s64/u32/s32`, `nbq_put_*`, and `nbq_get_parts` /
  `nbq_put_parts` for 64-bit values using only 32-bit arithmetic.
- Cursor API: `nbq_reader` and `nbq_writer`, with seek and align. A writer
  over caller-owned storage never allocates; `nbq_writer_init_alloc` is the
  one allocating entry point and can be compiled out with `NBQ_NO_MALLOC`.
- Varints at any group width from 2 to 64 bits. Base 8 is LEB128 exactly.
  Signed varints use zigzag. Encodings are canonical and overlong forms are
  refused.
- Bit runs: `nbq_copy_bits` (overlap-safe), `nbq_fill_bits`,
  `nbq_compare_bits`, `nbq_popcount_bits`.
- Version: `NBQ_VERSION_*` macros for the header, and `nbq_version_string()`
  / `nbq_version()` for the library actually linked.
- Optional `.nbq` container (`nbitq_container.{h,c}`): magic, type string,
  bit length and CRC-64/XZ, as a separate translation unit.
- Exhaustive tests over every width, base and alignment; a property-checking
  fuzz harness that builds for libFuzzer or standalone; ASan/UBSan runs; a
  C++ link test; and freestanding cross-compiles for Cortex-M0/M3/M4/M7,
  ARMv7-A, AArch64 and x86-64.

### Changed from dio_BitStream
- Every call takes a buffer length and is bounds-checked.
- An out-of-range width is an error rather than silently clamped.
- The indexed and cursor paths share one implementation.

[Unreleased]: https://github.com/deftio/nbitq/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/deftio/nbitq/releases/tag/v0.2.0
[0.1.2]: https://github.com/deftio/nbitq/releases/tag/v0.1.2
[0.1.1]: https://github.com/deftio/nbitq/releases/tag/v0.1.1
[0.1.0]: https://github.com/deftio/nbitq/releases/tag/v0.1.0
