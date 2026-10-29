# nbitq

[![CI](https://github.com/deftio/nbitq/actions/workflows/ci.yml/badge.svg)](https://github.com/deftio/nbitq/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://deftio.github.io/nbitq/coverage-badge.json)](https://github.com/deftio/nbitq/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/deftio/nbitq?sort=semver&logo=github&logoColor=white&label=GitHub&color=24292F)](https://github.com/deftio/nbitq/releases)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD_2--Clause-0A5C9E)](LICENSE.txt)

**Pack and unpack n-bit integers in a bit stream, in C.** Put a 5-bit, a
12-bit and a 7-bit value back to back, or at any bit position, and read them
back as native integers.

```c
#include "nbitq.h"

uint8_t buf[64] = {0};

/* write a 12-bit value starting at bit 13 */
nbq_put_u32(buf, sizeof(buf) * 8, 13, 12, 3000);

/* read it back */
uint32_t v;
nbq_get_u32(buf, sizeof(buf) * 8, 13, 12, &v);   /* v == 3000 */
```

Two files, C99, no dependencies, about 6.9 KB compiled. Values are 1 to 64
bits wide and positions are 64-bit, so a buffer of any size is addressed
directly. The suite checks every width at every offset in both bit orders
against a model, under sanitizers and a fuzzer, with 100% line coverage as
a CI gate; see [Testing](#testing).

## Quickstart

Copy `nbitq.h` and `nbitq.c` into the project. Then:

```c
/* first.c */
#include <stdio.h>
#include "nbitq.h"

int main(void)
{
    uint8_t buf[4] = {0};
    const uint64_t cap = sizeof(buf) * 8;      /* capacity in bits */

    nbq_writer w;                              /* cursor: fields back to back */
    nbq_writer_init(&w, buf, cap);
    nbq_write_u32(&w, 5,  19);
    nbq_write_u32(&w, 12, 3000);
    nbq_write_u32(&w, 7,  0x55);

    uint32_t offset;                           /* indexed: a field by position */
    nbq_get_u32(buf, cap, 5, 12, &offset);

    printf("%02X %02X %02X  offset=%u\n", buf[0], buf[1], buf[2], offset);
    return 0;
}
```

```
$ cc -std=c99 -o first first.c nbitq.c && ./first
9D DC 55  offset=3000
```

Every function returns an `nbq_result` (zero on success, negative
otherwise); the checks are omitted above. [Getting started](docs/getting-started.md)
continues from here, and the [API reference](docs/api-reference.md) lists
every function.

## What it does

A compressed file, a wire protocol or a hardware register map is specified
like this:

> bits 0–4: symbol index · bits 5–16: jump offset · bits 17–23: flags

In C this is normally shift-and-mask code written by hand, and it goes wrong
most often where a field straddles a byte boundary, which at arbitrary
widths is most of them. The standard library does not cover it: `memcpy`
moves bytes, struct bit-fields have implementation-defined layout, and
`std::bitset` cannot return an integer from an offset.

nbitq is that primitive:

```c
nbq_reader r;
nbq_reader_init(&r, buf, bit_len);

uint32_t symbol, offset, flags;
nbq_read_u32(&r, 5,  &symbol);
nbq_read_u32(&r, 12, &offset);
nbq_read_u32(&r, 7,  &flags);
```

There is no alignment requirement and no padding.

## Install

Copy two files into the project:

```
nbitq.h
nbitq.c
```

There is no configuration and nothing to generate. `nbitq.h` is
`extern "C"`, so it compiles as C++ as well. The optional container is
`nbitq_container.{h,c}`; see [Containers](#optional-storing-a-bit-run-in-a-file).
`nbitq.hpp` is an optional header-only wrapper for C++; see [From C++](#from-c).

<details>
<summary>CMake</summary>

```cmake
add_subdirectory(nbitq)                       # vendored
target_link_libraries(your_target PRIVATE nbitq)
```

or

```cmake
include(FetchContent)
FetchContent_Declare(nbitq
    GIT_REPOSITORY https://github.com/deftio/nbitq.git
    GIT_TAG        v0.2.2)
FetchContent_MakeAvailable(nbitq)
```
</details>

## Vendoring

Copying the two files is the intended way to depend on nbitq. To keep a
vendored copy honest, record which release it came from and let the
compiler refuse an old one. The header defines `NBQ_VERSION` as a packed
integer, `(major << 16) | (minor << 8) | patch`, so a guard is one
comparison. Next to the first `#include "nbitq.h"` in the vendoring
project:

```c
#include "nbitq.h"

/* the oldest nbitq this project has been tested with */
#define NBQ_VERSION_NEEDED ((0 << 16) | (2 << 8) | 0)

#if NBQ_VERSION < NBQ_VERSION_NEEDED
#error "vendored nbitq is older than this project needs; see nbitq/CHANGELOG.md"
#endif
```

Set the three numbers to the release the project was tested with and
raise them only when a newer feature is used. At run time,
`nbq_version_string()` returns the same version as a string for logs and
`--version` output, which tells a bug report which copy was in the build.

When updating the copy, take the files from a tagged release
(`git checkout vX.Y.Z` in a clone of nbitq) rather than from `main`, and
mention the tag in the commit that updates them. The bit numbering, varint
format and result codes are fixed across releases, so a newer copy reads
what an older one wrote.

## Where it fits, and where it does not

**Fits:** parsing or emitting bit-packed data. A compressed format, a codec,
a protocol with sub-byte fields, a succinct data structure, firmware
reading packed registers. Particularly where the code has to be small or
run without a heap.

**Does not fit:**

- Fields that are whole bytes. `memcpy` is enough.
- A parser generated from a format description.
  [Kaitai Struct](https://kaitai.io/) does that.
- Bulk integer arrays at high throughput. SIMD bit-packing libraries such as
  [simdcomp](https://github.com/lemire/simdcomp) are much faster at that
  job. nbitq is bit-at-a-time by design (see [Performance](#performance)).
- Python. [bitstring](https://bitstring.readthedocs.io/) is more capable
  there.

## The API

Every operation exists in two forms. **Indexed** functions take an explicit
position and hold no state. **Cursor** functions use a reader or writer
struct that holds a position and advances it. Both come in either bit order;
see [Bit order](#bit-order-msb-first-or-lsb-first).

### Indexed — `(buffer, bit_length, bit_position)`

```c
nbq_get_u32(buf, bit_len, bit_pos, n, &out);   /* n = 1..32 */
nbq_get_s32(buf, bit_len, bit_pos, n, &out);   /* sign-extended */
nbq_get_u64(buf, bit_len, bit_pos, n, &out);   /* n = 1..64 */
nbq_get_s64(buf, bit_len, bit_pos, n, &out);
nbq_put_u32(buf, bit_cap, bit_pos, n, value);  /* and put_s32, put_u64, put_s64 */
```

`bit_length` is the number of valid bits in the buffer. Every call checks the
operation against it and returns `NBQ_ERR_RANGE` rather than reading past the
end, which matters when the position was computed from untrusted data.

### Cursor

```c
nbq_reader r;
nbq_reader_init(&r, buf, bit_len);          /* allocates nothing */
nbq_read_u32(&r, 12, &v);                   /* advances by 12 bits */
nbq_reader_seek(&r, 400);
nbq_reader_align(&r, 8);                    /* to the next byte boundary */

uint8_t out[128];
nbq_writer w;
nbq_writer_init(&w, out, sizeof(out) * 8);  /* fixed buffer, never grows */
nbq_write_u32(&w, 12, 3000);
```

The structs are public and plain: on the stack, in static storage, or as a
member of another struct. There is no create/destroy pair.

`nbq_writer_init_alloc(&w, bits)` makes a writer that owns a heap buffer and
grows it; `nbq_writer_free(&w)` releases it. It is the only function in the
library that allocates, and it can be compiled out; see
[Embedded](#embedded).

### Bit order: MSB first, or LSB first

A bit stream is numbered from one end of each byte or the other, and real
formats are split: H.264, MPEG and most network formats start at the most
significant bit, DEFLATE and Vorbis at the least. nbitq handles both. The
plain names are MSB first. Every indexed function has an `_le` twin with
the same signature that is LSB first, and a `_be` spelling of the plain
name so that code comparing two streams reads symmetrically:

```c
nbq_get_u32_be(h264, bits, pos, 5, &a);     /* MSB first; the same as nbq_get_u32 */
nbq_get_u32_le(deflate, bits, pos, 5, &b);  /* LSB first */
```

A cursor carries its stream's order instead, set once by `nbq_reader_init_le`
or `nbq_writer_init_le`. Every read and write through it follows, and there
are no `_le` cursor functions to mix up mid-stream:

```c
nbq_reader r;
nbq_reader_init_le(&r, deflate, bits);
nbq_read_u32(&r, 1, &bfinal);               /* LSB first, because r is */
```

The same three fields, 19 in 5 bits, 3000 in 12 and 0x55 in 7, in both
orders:

```
MSB first   9D DC 55        LSB first   13 77 AB
```

Underneath there is one implementation with the order as a parameter, so
the two cannot drift apart. Byte order follows from bit order at aligned
positions, which is why the suffixes are `_be` and `_le`: 0x1234 in 16 bits
at bit 0 is `12 34` MSB first and `34 12` LSB first. Signed fields, zigzag,
the float forms and the varint rules act on the assembled integer and are
the same in both orders. A varint's groups land wherever the order puts them,
so its bytes differ between orders unless a group happens to be
byte-aligned. The optional container is MSB first; it is nbitq's own format.

### Varints

A varint stores a small number in few bits. The usual form (LEB128, as used
by protobuf and DWARF) spends 8 bits per group: 7 of payload and 1
continuation flag. nbitq takes the group width as a parameter:

```c
nbq_put_varint(buf, bit_cap, bit_pos, /*base*/ 8, value, &next_pos);  /* = LEB128 */
nbq_put_varint(buf, bit_cap, bit_pos, /*base*/ 5, value, &next_pos);  /* 4+1 bits */
nbq_get_varint(buf, bit_len, bit_pos, 5, &value, &next_pos);
nbq_varint_bits(value, 5);          /* how many bits it would take */
```

Base 8 produces bytes identical to LEB128. Smaller bases cost less on small
numbers, which is often most of them:

| values | best base | base 8 overhead |
|---|---|---|
| mostly under 8 | 3 | **+111%** |
| mostly under 100 | 4 | +8% |
| mostly under 10,000 | 5 | +9% |
| spread to millions | 6 | +2% |

`nbq_varint_bits()` returns the cost of a value at a base without writing
it, so a corpus can be measured at several bases. Signed varints
(`nbq_put_varint_s`, `nbq_get_varint_s`) use zigzag, so −1 costs the same
as 0.

### UTF-8

One Unicode code point per call, as the 1 to 4 bytes of its UTF-8
encoding, each byte an 8-bit field in the stream's order:

```c
nbq_put_utf8(buf, bit_cap, bit_pos, 0x20AC, &next_pos);   /* E2 82 AC when aligned */
nbq_get_utf8(buf, bit_len, bit_pos, &cp, &next_pos);      /* cp == 0x20AC */
nbq_write_utf8(&w, cp);                                   /* cursor forms */
nbq_read_utf8(&r, &cp);
nbq_utf8_bits(cp);                                        /* 8, 16, 24 or 32 */
```

The decoder accepts exactly the well-formed sequences of the Unicode
standard: no overlong forms, no surrogates, nothing past U+10FFFF, each
refused as `NBQ_ERR_MALFORMED`. A sequence cut off by the end of the
buffer is `NBQ_ERR_RANGE`, so a parser can tell "more data" from "corrupt".
Strings are a layer above nbitq: a loop over these calls, with a length or
a terminator chosen by the format.

### Bit runs

Move a span of bits from any offset to any other offset:

```c
nbq_copy_bits(src, src_bits, 3, dst, dst_bits, 11, 157);   /* 157 bits, 3 → 11 */
nbq_fill_bits(buf, bit_cap, 64, 32, 1);                    /* set 32 bits to 1 */
nbq_compare_bits(a, a_bits, 0, b, b_bits, 5, 100, &cmp);
nbq_popcount_bits(buf, bit_len, 0, 1000, &count);
```

Neither end needs to be byte-aligned, and overlapping ranges within one
buffer are handled correctly. The result is a run of bits; nbitq does not
interpret it.

### Floating point

A `float` or `double` as its IEEE-754 bit pattern in a 32- or 64-bit field,
MSB first. Header-only `static inline`; no floating-point arithmetic is
involved, so NaN payloads and signed zeros survive and a core without an FPU
pays for a register move.

```c
nbq_put_f32(buf, bit_cap, bit_pos, 3.14159f);   /* 40 49 0F D0 when aligned */
nbq_get_f64(buf, bit_len, bit_pos, &d);
nbq_write_f32(&w, x);                           /* cursor forms */
nbq_read_f64(&r, &d);
```

### Values wider than 32 bits, on a 32-bit CPU

On a target without native 64-bit arithmetic, every `uint64_t` shift becomes
a function call into compiler support code. `nbq_u64_parts` avoids that:

```c
nbq_u64_parts p;
nbq_get_parts(buf, bit_len, bit_pos, 48, &p);   /* p.hi, p.lo — 32-bit maths only */
```

It produces the same bits as `nbq_get_u64`; only the arithmetic differs.

### From C++

The headers are `extern "C"`, so every function above works unchanged from
C++. `nbitq.hpp` adds the cursor API as classes. Each method is one call
into the C library with the `nbq_read_` or `nbq_write_` prefix dropped, so
`nbq_read_u32(&r, n, &v)` is `r.u32(n, &v)`:

```cpp
#include "nbitq.hpp"     // includes nbitq.h; header-only, C++11

uint8_t buf[64] = {0};
nbq::Writer w(buf, sizeof(buf) * 8);
w.u32(5, 19);
w.u32(12, 3000);
w.varint(8, 300);

nbq::Reader r(buf, w.pos());
uint32_t symbol, offset;
r.u32(5, &symbol);
r.u32(12, &offset);
```

`nbq::OwningWriter` is the growing writer with `nbq_writer_free` in its
destructor; it moves and does not copy. `handle()` returns the C struct for
code that takes one. The wrapper returns `nbq_result` like the C API and
uses nothing from the standard library, so `-fno-exceptions` and
freestanding C++ builds are unaffected. It does not wrap the indexed
functions, which have no state to wrap; call them directly. `make cxx`
compiles, links and runs its tests, one per method.

## Fixed conventions

- **Bit order is a property of the stream, declared once.** MSB first
  unless a call or a cursor says `_le`, and LSB first then. Within an order
  the layout is fixed: bit 0 is the MSB of byte 0 (the LSB, for `_le`) and a
  value's first bit goes first. This does not change.
- **Every operation is all-or-nothing.** On failure, nothing is written and no
  cursor moves, so a failed read can be retried.
- **Bad input is refused, not approximated.** An out-of-range width or
  position, a varint longer than 64 bits can hold, or an overlong varint
  encoding all return errors.
- **Varints are canonical.** One value encodes exactly one way, so two
  programs writing the same data produce the same bytes.

## Embedded

**No allocation is required.** `nbq_reader_init` and `nbq_writer_init` use
caller-supplied storage; the reader's buffer can live in ROM.

Define `NBQ_NO_MALLOC` and the one allocating function disappears, along with
every `#include` of a hosted header — the library then builds
`-ffreestanding` with no libc at all.

The core on every toolchain `tools/sizes.sh` drives, as bytes of `.text`
at `-Os` with `NBQ_NO_MALLOC` and `-ffreestanding`. The `core` column is
the whole library in one object. The `u32 cursor only` column is what a
program that calls only `nbq_reader_init`, `nbq_writer_init`,
`nbq_read_u32` and `nbq_write_u32` links in, with function sections
garbage-collected from those four roots. The `nbitq32` column is
`nbitq32.c` alone, the separate 32-bit-position pair described in
[nbitq32](#nbitq32-the-u32-pair-with-32-bit-positions) below.

<!-- crossbuild-sizes -->
| CPU | bits | compiler | core | u32 cursor only | nbitq32 |
|---|---:|---|---:|---:|---:|
| AVR ATtiny85 | 8 | avr-gcc 5.4.0 | 26,373 | 1,972 | 1,032 |
| AVR ATmega328P | 8 | avr-gcc 5.4.0 | 26,989 | 2,014 | 1,032 |
| 8051 | 8 | sdcc -mmcs51 4.0.0 | n/a | n/a | 1,353 |
| STM8 | 8 | sdcc -mstm8 4.0.0 | n/a | n/a | 750 |
| Z80 | 8 | sdcc -mz80 4.0.0 | n/a | n/a | 1,404 |
| MSP430 G2553 | 16 | msp430-gcc 4.6.3 | 16,784 | 1,412 | 756 |
| Cortex-M4 (Thumb-2) | 32 | arm-none-eabi-gcc 10.3.1 | 7,281 | 594 | 354 |
| Xtensa LX7 (ESP32-S3) | 32 | xtensa-esp32s3-elf-gcc 15.2.0 | 7,589 | 648 | 416 |
| Cortex-M0 (Thumb-1) | 32 | arm-none-eabi-gcc 10.3.1 | 7,609 | 636 | 356 |
| Xtensa LX6 (ESP32) | 32 | xtensa-esp32-elf-gcc 15.2.0 | 7,649 | 646 | 400 |
| RISC-V rv32imac | 32 | riscv64-unknown-elf-gcc 10.2.0 | 8,007 | 760 | 382 |
| Xtensa LX106 (ESP8266) | 32 | xtensa-lx106-elf-gcc 10.3.0 | 9,321 | 803 | 447 |
| m68k | 32 | m68k-linux-gnu-gcc 11.4.0 | 9,955 | 912 | 484 |
| ARM v7-A (A32) | 32 | arm-none-eabi-gcc 10.3.1 | 10,889 | 932 | 596 |
| MIPS32 (little-endian) | 32 | mipsel-linux-gnu-gcc 10.3.0 | 14,400 | 1,164 | 624 |
| PowerPC | 32 | powerpc-linux-gnu-gcc 11.4.0 | 15,229 | 1,384 | 684 |
| x86-32 | 32 | gcc 11.4.0 | 15,639 | 1,306 | 756 |
| RISC-V rv64gc | 64 | riscv64-linux-gnu-gcc 11.4.0 | 5,876 | 660 | 386 |
| x86-64 | 64 | clang 14.0.0 | 6,886 | 583 | 374 |
| Apple Silicon (arm64 macOS) | 64 | Apple clang 21.0.0 | 7,072 | 672 | 428 |
| AArch64 | 64 | aarch64-linux-gnu-gcc 11.4.0 | 9,527 | 1,056 | 664 |
| x86-64 | 64 | gcc 11.4.0 | 9,535 | 1,039 | 710 |
| MIPS64 (little-endian) | 64 | mips64el-linux-gnuabi64-gcc 10.3.0 | 10,112 | 1,024 | 624 |
<!-- crossbuild-sizes -->

Measured with the compilers Ubuntu 22.04 packages, in the Docker image
`tools/Dockerfile.crossbuild`, plus Apple clang on an arm64 Mac; `make
crossbuild` reproduces the CSVs and `make sizes-table` rewrites this table
from them. The library leans on 64-bit arithmetic, which an 8-bit or
16-bit CPU does in software, so the whole core is large there while the
u32 path stays small. The SDCC rows have no core; the next section says
why. `make targets` prints Apple clang `-Oz` numbers for a quicker check:
7,283 bytes on Cortex-M4, 8,235 on Cortex-M0, 6,549 on AArch64, 7,050 on
x86-64.

No stdio, no floating-point arithmetic, no libm, no recursion, no global state, so it
is reentrant per instance.

## nbitq32: the u32 pair with 32-bit positions

Everything above is the library: `nbitq.h` and `nbitq.c`, with 64-bit
positions and values, for any CPU that has a 64-bit type, which includes
every 32-bit microcontroller. This section is for the targets that do not.

On an 8-bit or 16-bit CPU every 64-bit operation is a call into compiler
support code, and the position arithmetic costs more than the value
arithmetic. SDCC, the usual compiler for Z80, 8051 and STM8, has no 64-bit
type at all and cannot compile `nbitq.c`. For those targets `nbitq32.h` and
`nbitq32.c` are `nbq_get_u32` and `nbq_put_u32` with `uint32_t` positions
and lengths, and nothing else:

```c
#include "nbitq32.h"

uint8_t buf[4] = {0};
nbq32_put(buf, 32, 5, 12, 3000);       /* uint32_t capacity and position */
uint32_t v;
nbq32_get(buf, 32, 5, 12, &v);         /* v == 3000; same bits as nbq_get_u32 */
```

Two functions and their `_le` twins, `_be` aliases, the same bit numbering,
the same refusals and the same result-code values as `nbq_result`. No
cursor: `pos += n` is one. The two files do not include `nbitq.h`, so they
are copied alone, and `test_nbitq32.c` checks that they write the bytes
`nbq_put_u32` writes for every width and offset in both orders. In the
table above it is 354 bytes on Cortex-M4, 1,032 on AVR, and with SDCC
1,404 on Z80, 1,353 on 8051 and 750 on STM8.

## Optional: storing a bit run in a file

The core has no file format, no magic bytes and no version field. A bare run
of 1,234 bits cannot say how long it is or what it was, so for handing one
to another program there is an opt-in wrapper:

```c
#include "nbitq_container.h"

uint8_t box[4096];
size_t n;
nbq_container_write(box, sizeof(box), "myformat/v1", 11, bits, 1234, &n);

nbq_container c;
if (nbq_container_read(box, n, &c) == NBQ_OK) {
    /* c.payload, c.payload_bits == 1234, c.type == "myformat/v1" */
}
```

On disk (`.nbq` by convention):

```
[ "NBQ\1" ][ varint type_len ][ type_str ][ varint n_bits ]
[ payload, zero-padded to a byte ][ CRC-64 ]
```

The type string is free-form and never interpreted; it exists so the reading
code can dispatch. The container does not look inside the payload.

This lives in `nbitq_container.c`, a separate file. Left out, the core is
exactly as described above. Compiled in, it costs 1,173 bytes.

The checksum is CRC-64/XZ (ECMA-182), verified against the published test
vector `CRC-64/XZ("123456789") == 995DC9BBDF1939FA`. A checksum detects
accidental corruption and nothing else: whoever can supply a file can supply
a matching checksum. Detecting tampering needs a signature, not a hash.

## How it compares

There is no standard C or C++ bit-stream library. C++20's `<bit>` provides
operations *on integers* (`popcount`, `countl_zero`, `bit_width`) but has no
notion of a buffer or a position. The closest existing options:

| | what it is | what it does not do |
|---|---|---|
| `std::bitset` | fixed, compile-time size | cannot read an *n*-bit integer from a position |
| `std::vector<bool>` | packed bits | no multi-bit integer access |
| `boost::dynamic_bitset` | runtime-sized bit set | a set, not a field accessor |
| [bitstring](https://bitstring.readthedocs.io/) (Python) | rich bit toolkit | Python only |
| [simdcomp](https://github.com/lemire/simdcomp) / `bitpacking` | SIMD packing of integer arrays | bulk, fixed width per block; not "this field here" |
| [bitpacker](https://github.com/CrustyAuklet/bitpacker) (C++) | compile-time bit serialisation | C++17/20, struct-shaped |
| ffmpeg `get_bits.h` | the fast bit reader | LGPL, inside libavcodec, read-only, max 32 bits, bounds checks optional |
| [Kaitai Struct](https://kaitai.io/) | generates a parser from a schema | a schema, not calls |

**Codec bit readers are internal by design.** ffmpeg's reads 1–17 bits
(`get_bits`) or 0–32 (`get_bits_long`), has no writer in the same header, and
makes bounds checking optional per decoder because a codec validates its
headers once and then trusts them. zlib keeps a bit accumulator inlined in
the inflate state machine with no API at all — DEFLATE only ever needs "next
*n* bits, sequentially". Both are right for their job and unusable as general
libraries: neither can be asked for *"the signed 19-bit field at bit
1,234."*

**What `bitstring` has and nbitq does not:** searching for a bit pattern
(`find`, `findall`), length-changing edits (`insert`, `delete`), and format
strings (`read('uint:12')`). It is a toolkit for exploring a format by hand.
nbitq is a field accessor linked into something that needs to be small.

## Performance

nbitq is **not optimised**. Multi-bit operations loop over single bits rather
than loading words and masking, which costs roughly 3–5× on reads against a
word-at-a-time implementation.

There is one place in the code where a bit is addressed, so there is one
place a bit can be addressed wrongly, and the differential fuzz harness that
a faster path would need to prove itself against already exists. For
codec-grade throughput, use a codec's reader.

## Testing

```bash
make check      # tests, sanitizers, fuzzing, C++ tests, code size
make coverage   # every line of the library executed by the tests, or fail
make targets    # cross-compile freestanding for embedded targets
```

The library is small enough to test exhaustively rather than
representatively: **every** width from 1 to 64, **every** varint base from 2
to 64, every start offset within a byte, truncation at every bit position,
and every bounds case — including a position chosen so that `pos + n`
overflows.

The fuzz harness checks properties, not just absence of crashes: anything
that decodes must re-encode to identical bits; the 64-bit, 32-bit and hi/lo
paths, and `nbitq32`, must agree read-for-read; a failed read must leave the cursor
untouched; a copied bit run must compare equal to its source.

UTF-8 is tested against the Unicode standard's table of well-formed byte
sequences (Table 3-7), written into the suite as a predicate: every
two-byte sequence at every start offset, every three-byte sequence with a
three-byte lead, and the boundary bytes of every four-byte sequence must
decode exactly when the table says they are well-formed, to the table's
value, and re-encode to the same bytes. Every scalar value from U+0000 to
U+10FFFF is encoded and decoded, and every length class at every start
offset within a byte in both orders.

Bit order is tested against a model, not against itself. The suite expands
a buffer into one character per bit in stream order and reads members from
that string, so every width, offset and bit pattern is checked in both
orders against something that is right by inspection. The two orders are
then tied to each other by a mirror identity, and the LSB-first path to the
outside world by raw DEFLATE blocks that zlib produced, which nbitq reads
field by field and writes back byte for byte.

Measured: 9,777,588 core checks, 2,080 container checks, 207,619 checks of
`nbitq32` against the core, 297 checks of the C++ wrapper, and 1,290,568
libFuzzer runs in 60 seconds, all under AddressSanitizer and
UndefinedBehaviorSanitizer, all clean.

Line coverage is a gate, not a report. `make coverage` builds the library
once with gcov instrumentation, runs all five test binaries against it and
fails unless every line of `nbitq.c`, `nbitq_container.c`, `nbitq32.c`, `nbitq.h` and
`nbitq.hpp` ran. Two lines are excluded, both guards that only a 32-bit
`size_t` can reach, and each says so where it stands. CI runs the gate with
gcc and gcov on Linux and publishes the number as the badge at the top of
this page.

## Background

The API follows `GetNBitQty` / `PutNBitQty` from `dio_BitStream`
(M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a 12-bit jump offset,
a 7-bit value into a continuous bit stream, then read them back as native
integers." That shape has held up for twenty-five years and is kept. The
implementation is new. [triepack](https://github.com/deftio/triepack) vendors
nbitq.

Three things it does differently, each because the original did not:
bounds are checked (the original took no buffer length); an out-of-range
width is an error rather than silently clamped; and the indexed and cursor
paths share one implementation instead of being written twice.

## Documentation

The website at <https://deftio.github.io/nbitq/> explains what nbitq is for,
who it is for and how it works, with every example worked down to the byte.
The same material is in [`docs/`](docs/) as Markdown:
[what it is](docs/what-is-nbitq.md), [getting started](docs/getting-started.md),
[how it works](docs/how-it-works.md) and the [API reference](docs/api-reference.md).

[`AGENTS.md`](AGENTS.md) is the short version for coding agents: why the
library exists, the fixed conventions, and how to build and test. [`llms.txt`](llms.txt)
is the same map in the llms.txt format.

## Contributing

Bug reports and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md)
covers the build, the test expectations and how a release is cut
(`./release.sh` runs everything CI runs; `./release.sh X.Y.Z` bumps the
version once it is green). Security issues go through
[SECURITY.md](SECURITY.md). Changes are listed in [CHANGELOG.md](CHANGELOG.md).

## Licence

BSD-2-Clause. See `LICENSE.txt`.
