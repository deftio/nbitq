# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The GitHub release notes for a version are taken from its section here, so
keep each section self-contained.

## [Unreleased]

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

[Unreleased]: https://github.com/deftio/nbitq/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/deftio/nbitq/releases/tag/v0.1.1
[0.1.0]: https://github.com/deftio/nbitq/releases/tag/v0.1.0
