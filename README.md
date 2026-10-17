# nbitq

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

Two files, C99, no dependencies, ~4.7 KB compiled.

## The problem it solves

Say you are reading a compressed file, a wire protocol, or a hardware
register dump. The spec says:

> bits 0–4: symbol index · bits 5–16: jump offset · bits 17–23: flags

In C you now write shift-and-mask code by hand, and it is tedious and easy to
get wrong — especially where a field straddles a byte boundary, which at
arbitrary widths is most of them. The standard library offers nothing:
`memcpy` moves bytes, bit-fields in structs have implementation-defined
layout, and `std::bitset` cannot hand you back an integer from an offset.

nbitq is the missing primitive:

```c
nbq_reader r;
nbq_reader_init(&r, buf, bit_len);

uint32_t symbol, offset, flags;
nbq_read_u32(&r, 5,  &symbol);
nbq_read_u32(&r, 12, &offset);
nbq_read_u32(&r, 7,  &flags);
```

No alignment requirements, no padding, no hand-rolled masks.

## Install

Copy two files into your project:

```
nbitq.h
nbitq.c
```

That is the whole installation. There is no configuration and nothing to
generate. `nbitq.h` is `extern "C"`, so C++ works too.

Optionally also take `nbitq_container.{h,c}` — see
[Containers](#optional-storing-a-bit-run-in-a-file).

<details>
<summary>CMake, if you prefer</summary>

```cmake
add_subdirectory(nbitq)                       # vendored
target_link_libraries(your_target PRIVATE nbitq)
```

or

```cmake
include(FetchContent)
FetchContent_Declare(nbitq
    GIT_REPOSITORY https://github.com/deftio/nbitq.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(nbitq)
```
</details>

## When to use it, and when not to

**Use it when** you are parsing or emitting bit-packed data: a compressed
format, a codec, a protocol with sub-byte fields, a succinct data structure,
firmware talking to packed registers. Especially if the code has to be small
or run without a heap.

**Don't use it when**

- Your fields are whole bytes. Use `memcpy` and be happy.
- You want a *schema* compiler — something that reads a format description
  and generates a parser. [Kaitai Struct](https://kaitai.io/) does that.
- You want maximum throughput on bulk integer arrays. SIMD bit-packing
  libraries like [simdcomp](https://github.com/lemire/simdcomp) are far
  faster at that specific job. nbitq is not optimised (see
  [Performance](#performance)).
- You are in Python. [bitstring](https://bitstring.readthedocs.io/) is more
  capable and more ergonomic there.

## The API

Everything comes in two flavours. **Indexed** functions take an explicit
position and hold no state. **Cursor** functions walk forward through a
reader or writer you own.

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
end — which matters when the position was computed from data you did not
write.

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

The reader and writer structs are plain and public — put them on the stack,
in static storage, or inside your own object. There is no create/destroy pair
to balance.

`nbq_writer_init_alloc(&w, bits)` gives you a writer that owns a heap buffer
and grows it; `nbq_writer_free(&w)` releases it. It is the only function in
the library that allocates, and you can compile it out — see
[Embedded](#embedded).

### Varints

A varint stores a small number in few bytes. The usual form (LEB128, as used
by protobuf and DWARF) spends 8 bits per group: 7 of payload and 1 to say
"more follows". nbitq lets you pick the group width:

```c
nbq_put_varint(buf, bit_cap, bit_pos, /*base*/ 8, value, &next_pos);  /* = LEB128 */
nbq_put_varint(buf, bit_cap, bit_pos, /*base*/ 5, value, &next_pos);  /* 4+1 bits */
nbq_get_varint(buf, bit_len, bit_pos, 5, &value, &next_pos);
nbq_varint_bits(value, 5);          /* how many bits it would take */
```

Base 8 produces bytes identical to LEB128. Smaller bases cost less on small
numbers, which is often most of them:

| your values | best base | what base 8 costs you |
|---|---|---|
| mostly under 8 | 3 | **+111%** |
| mostly under 100 | 4 | +8% |
| mostly under 10,000 | 5 | +9% |
| spread to millions | 6 | +2% |

`nbq_varint_bits()` lets you measure your own data and choose. Signed varints
(`nbq_put_varint_s`, `nbq_get_varint_s`) use zigzag, so −1 is as cheap as 0.

### Bit runs

Move a span of bits from any offset to any other offset:

```c
nbq_copy_bits(src, src_bits, 3, dst, dst_bits, 11, 157);   /* 157 bits, 3 → 11 */
nbq_fill_bits(buf, bit_cap, 64, 32, 1);                    /* set 32 bits to 1 */
nbq_compare_bits(a, a_bits, 0, b, b_bits, 5, 100, &cmp);
nbq_popcount_bits(buf, bit_len, 0, 1000, &count);
```

Neither end needs to be byte-aligned, and overlapping ranges within one
buffer are handled correctly. What you extract is just bits — hand it to a
decompressor, a trie, or your own decoder.

### Values wider than 32 bits, on a 32-bit CPU

On a target without native 64-bit arithmetic, every `uint64_t` shift becomes
a function call into compiler support code. `nbq_u64_parts` avoids that:

```c
nbq_u64_parts p;
nbq_get_parts(buf, bit_len, bit_pos, 48, &p);   /* p.hi, p.lo — 32-bit maths only */
```

It produces the same bits as `nbq_get_u64`; only the arithmetic differs.

## What you can rely on

- **Bit 0 is the most significant bit of byte 0**, and fields are written
  most-significant bit first. This never changes.
- **Every operation is all-or-nothing.** On failure, nothing is written and no
  cursor moves — so you can try a read, have it fail, and carry on.
- **Bad input is refused, not approximated.** An out-of-range width or
  position, a varint longer than 64 bits can hold, or an overlong varint
  encoding all return errors.
- **Varints are canonical.** One value encodes exactly one way, so two
  programs writing the same data produce the same bytes.

## Embedded

**No allocation is required.** `nbq_reader_init` and `nbq_writer_init` use
storage you supply; the reader's buffer can live in ROM.

Define `NBQ_NO_MALLOC` and the one allocating function disappears, along with
every `#include` of a hosted header — the library then builds
`-ffreestanding` with no libc at all.

| target | `.text` |
|---|---:|
| **Cortex-M3/M4/M7** (Thumb-2) | **4,805 bytes** |
| Cortex-M0 (Thumb-1) | 5,047 |
| ARM (v7-A) | 7,275 |
| AArch64 | 4,375 |
| x86-64 | 4,531 |

No stdio, no floating point, no libm, no recursion, no global state — so it
is reentrant per instance. Reproduce with `make targets`.

## Optional: storing a bit run in a file

The core has no file format, no magic bytes and no version field, and for
most uses that is the whole story. But a bare run of 1,234 bits cannot say
how long it is or what it was, so if you need to hand one to another program
there is an opt-in wrapper:

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

The type string is free-form and never interpreted — it exists so *your* code
can dispatch. The container never looks inside the payload.

This lives in `nbitq_container.c`, a separate file. Don't compile it and the
core stays exactly as described above. It costs 1,173 bytes when you do.

The checksum is CRC-64/XZ (ECMA-182), verified against the published test
vector `CRC-64/XZ("123456789") == 995DC9BBDF1939FA`. Note that a checksum
detects accidental corruption and nothing else — anyone who can hand you a
file can hand you a matching checksum, so if you need to detect tampering you
need a signature, not a hash.

## How it compares

There is no standard C or C++ bit-stream library. C++20's `<bit>` provides
operations *on integers* (`popcount`, `countl_zero`, `bit_width`) but has no
notion of a buffer or a position. The closest existing options:

| | what it is | why you might still want nbitq |
|---|---|---|
| `std::bitset` | fixed, compile-time size | cannot read an *n*-bit integer from a position |
| `std::vector<bool>` | packed bits | no multi-bit integer access |
| `boost::dynamic_bitset` | runtime-sized bit set | a set, not a field accessor |
| [bitstring](https://bitstring.readthedocs.io/) (Python) | rich bit toolkit | Python only |
| [simdcomp](https://github.com/lemire/simdcomp) / `bitpacking` | SIMD packing of integer arrays | bulk, fixed width per block; not "this field here" |
| [bitpacker](https://github.com/CrustyAuklet/bitpacker) (C++) | compile-time bit serialisation | C++17/20, struct-shaped |
| ffmpeg `get_bits.h` | the fast bit reader | LGPL, inside libavcodec, read-only, max 32 bits, bounds checks optional |
| [Kaitai Struct](https://kaitai.io/) | generates a parser from a schema | you write a schema, not calls |

**Codec bit readers are internal by design.** ffmpeg's reads 1–17 bits
(`get_bits`) or 0–32 (`get_bits_long`), has no writer in the same header, and
makes bounds checking optional per decoder because a codec validates its
headers once and then trusts them. zlib keeps a bit accumulator inlined in
the inflate state machine with no API at all — DEFLATE only ever needs "next
*n* bits, sequentially". Both are right for their job and unusable as general
libraries: you cannot ask either for *"the signed 19-bit field at bit
1,234."*

**What `bitstring` has and nbitq does not:** searching for a bit pattern
(`find`, `findall`), length-changing edits (`insert`, `delete`), and format
strings (`read('uint:12')`). It is a toolkit for exploring a format by hand.
nbitq is a field accessor you link into something that needs to be small.

## Performance

nbitq is **not optimised**. Multi-bit operations loop over single bits rather
than loading words and masking, which costs roughly 3–5× on reads against a
word-at-a-time implementation.

That is a deliberate first step, not an oversight. There is one place in the
code where a bit is addressed, so there is one place a bit can be addressed
wrongly — and the differential fuzz harness that a faster path would need to
prove itself against already exists. If you need codec-grade throughput
today, use a codec's reader.

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

Measured: 73,943 core checks, 2,065 container checks, and 2,901,892 libFuzzer
runs, all under AddressSanitizer and UndefinedBehaviorSanitizer, all clean.

## Background

The API follows `GetNBitQty` / `PutNBitQty` from `dio_BitStream`
(M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a 12-bit jump offset,
a 7-bit value into a continuous bit stream, then read them back as native
integers." That shape has held up for twenty-five years and is kept. The
implementation is new.

Three things it does differently, each because the original did not:
bounds are checked (the original took no buffer length); an out-of-range
width is an error rather than silently clamped; and the indexed and cursor
paths share one implementation instead of being written twice.

## Licence

BSD-2-Clause. See `LICENSE.txt`.
