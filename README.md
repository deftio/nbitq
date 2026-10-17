# nbitq

[![CI](https://github.com/deftio/nbitq/actions/workflows/ci.yml/badge.svg)](https://github.com/deftio/nbitq/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/deftio/nbitq?sort=semver)](https://github.com/deftio/nbitq/releases)
[![License: BSD-2-Clause](https://img.shields.io/badge/license-BSD--2--Clause-blue.svg)](LICENSE.txt)

**Read and write integers of any bit width, at any bit position, in C.**

```c
#include "nbitq.h"

uint8_t buf[64] = {0};

/* write a 12-bit value starting at bit 13 */
nbq_put_u32(buf, sizeof(buf) * 8, 13, 12, 3000);

/* read it back */
uint32_t v;
nbq_get_u32(buf, sizeof(buf) * 8, 13, 12, &v);   /* v == 3000 */
```

Two files, C99, no dependencies, about 4.7 KB compiled.

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
    GIT_TAG        v0.1.1)
FetchContent_MakeAvailable(nbitq)
```
</details>

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
struct that holds a position and advances it.

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

## Fixed conventions

- **Bit 0 is the most significant bit of byte 0**, and fields are written
  most-significant bit first. This does not change.
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

| target | `.text` |
|---|---:|
| **Cortex-M3/M4/M7** (Thumb-2) | **4,843 bytes** |
| Cortex-M0 (Thumb-1) | 5,085 |
| ARM (v7-A) | 7,317 |
| AArch64 | 4,401 |
| x86-64 | 4,559 |

No stdio, no floating-point arithmetic, no libm, no recursion, no global state, so it
is reentrant per instance. Reproduce with `make targets`.

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
make check      # tests, sanitizers, fuzzing, C++ build, code size
make targets    # cross-compile freestanding for embedded targets
```

The library is small enough to test exhaustively rather than
representatively: **every** width from 1 to 64, **every** varint base from 2
to 64, every start offset within a byte, truncation at every bit position,
and every bounds case — including a position chosen so that `pos + n`
overflows.

The fuzz harness checks properties, not just absence of crashes: anything
that decodes must re-encode to identical bits; the 64-bit, 32-bit and hi/lo
paths must agree read-for-read; a failed read must leave the cursor
untouched; a copied bit run must compare equal to its source.

Measured: 76,875 core checks, 2,065 container checks, and 2,901,892 libFuzzer
runs, all under AddressSanitizer and UndefinedBehaviorSanitizer, all clean.

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

## Contributing

Bug reports and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md)
covers the build, the test expectations and how a release is cut
(`./release.sh` runs everything CI runs; `./release.sh X.Y.Z` bumps the
version once it is green). Security issues go through
[SECURITY.md](SECURITY.md). Changes are listed in [CHANGELOG.md](CHANGELOG.md).

## Licence

BSD-2-Clause. See `LICENSE.txt`.
