# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The GitHub release notes for a version are taken from its section here, so
keep each section self-contained.

## [Unreleased]

## [0.2.2] - 2026-10-28

### Fixed
- `nbq_get_f32` / `nbq_get_f64`, their `_le` twins and `nbq_read_f32` /
  `nbq_read_f64` returned `NBQ_OK` for a null output pointer without
  producing a value, and the cursor forms advanced the reader. They now
  return `NBQ_ERR_PARAM` and move nothing, as every other getter does.

### Changed
- How it works shows where each bit goes: a value's bits paired with the
  stream positions they land in, in both orders, with the surrounding bits
  untouched; sign extension against the unsigned reading of the same
  field; truncation of a value wider than its field; and a code point
  through its UTF-8 bytes into an unaligned position. The site draws them
  from the value, width and position by the library's rules, and the
  bytes in the docs come from running the library.

## [0.2.1] - 2026-10-27

### Added
- UTF-8, one code point per call: `nbq_get_utf8` / `nbq_put_utf8` with
  `_le` twins and `_be` aliases, `nbq_read_utf8` / `nbq_write_utf8` on the
  cursors, `nbq_utf8_bits`, and `utf8()` on `nbq::Reader` and
  `nbq::Writer`. A `uint32_t` in and out; the 1 to 4 bytes are 8-bit
  fields in the stream's order, so an aligned sequence is plain UTF-8 in
  either order. The decoder accepts exactly the well-formed sequences of
  Unicode Table 3-7: overlong forms, surrogates, lead bytes past U+10FFFF
  and bad continuation bytes are `NBQ_ERR_MALFORMED`; a sequence cut off by
  the end of the buffer is `NBQ_ERR_RANGE`. The encoder refuses surrogates
  and values above U+10FFFF as `NBQ_ERR_PARAM`. No string functions; the
  API reference has the length-prefixed and terminated shapes as recipes.
- Tests: the Unicode table of well-formed sequences as a predicate, with
  every two-byte sequence at every start offset, every three-byte sequence
  with a three-byte lead and the boundary bytes of every four-byte
  sequence decoded against it; every scalar value U+0000..U+10FFFF encoded
  and decoded; every length class at every start offset in both orders;
  the fuzz harness checks that every decoded code point is a scalar value
  that re-encodes to the bits read. The core suite is 9,777,588 checks.
- `nbitq32.h` / `nbitq32.c`: `nbq32_get` / `nbq32_put` with `_le` twins
  and `_be` aliases, the u32 pair with `uint32_t` positions and lengths.
  A separate copy of the fixed-width path with no 64-bit type and no
  dependency on `nbitq.h`, for 8-bit and 16-bit CPUs and for SDCC (Z80,
  8051, STM8), where `nbitq.c` does not build. Same bit numbering, same
  refusals, result codes with the values of `nbq_result`. 354 bytes on
  Cortex-M4 and 1,032 on AVR at `-Os`, against 594 and 2,014 for the u32
  cursor subset of the core. `test_nbitq32.c` holds it to `nbitq.c` byte
  for byte at every width and start offset in both orders, 207,619 checks;
  the fuzz harness and the C++ test call it too. Its own CMake target,
  `nbitq32`.
- `tools/sizes.sh`, `tools/crossbuild.sh`, `tools/Dockerfile.crossbuild`
  and `tools/update_sizes.sh`: compile the core and `nbitq32.c` on
  twenty-three toolchains from 8-bit AVR and SDCC to 64-bit MIPS and Apple
  clang on an arm64 Mac, record the code size of the whole core, of the
  u32-only cursor subset and of `nbitq32.c`, and rewrite the size tables
  in README.md, `docs/getting-started.md` and the pages from the result.
  `make crossbuild`, `make sizes-table`, `./release.sh sizes`. CI runs the
  script on every push and fails on a warning from any toolchain.

### Fixed
- `NBQ_VERSION` shifted an `int` left by 16, which is undefined where `int`
  is 16 bits (AVR, MSP430) and drew a warning there. It is now computed in
  `unsigned long`; the value and its use in `#if` are unchanged.

### Changed
- The fixed-width functions address a bit as a byte pointer plus a small
  index: the 64-bit position is converted once per call, and the loop over
  the field's bits runs in `unsigned int`. Same bytes, smaller code where
  64-bit arithmetic is software: at `-Os` with gcc the core went from
  7,481 to 7,281 bytes on Cortex-M4 and from 28,053 to 26,989 on AVR, and
  the u32 cursor subset from 648 to 594 and from 2,158 to 2,014. On MSP430
  and on x86-64 at `-Oz` it grew by about one percent.
- Code size: the core is 6,881 bytes at -Oz on x86-64 (was 5,901) and
  7,283 on Cortex-M4 (was 6,555), the UTF-8 encoder and decoder with their
  validation. A build that does not call them drops them with
  `-ffunction-sections` and `--gc-sections`.
- `./release.sh libfuzzer` uses the first clang that has the libFuzzer
  runtime: the one on PATH, then Homebrew's LLVM, then `FUZZ_CLANG`. On a
  Mac the stage ran only in CI before; it now runs locally with
  `brew install llvm`.

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

[Unreleased]: https://github.com/deftio/nbitq/compare/v0.2.2...HEAD
[0.2.2]: https://github.com/deftio/nbitq/releases/tag/v0.2.2
[0.2.1]: https://github.com/deftio/nbitq/releases/tag/v0.2.1
[0.2.0]: https://github.com/deftio/nbitq/releases/tag/v0.2.0
[0.1.2]: https://github.com/deftio/nbitq/releases/tag/v0.1.2
[0.1.1]: https://github.com/deftio/nbitq/releases/tag/v0.1.1
[0.1.0]: https://github.com/deftio/nbitq/releases/tag/v0.1.0
