# What nbitq is

nbitq reads and writes integers of any width, from 1 to 64 bits, at any bit
position in a byte buffer. That is the whole library: two C99 files, no
dependencies, about 4.7 KB compiled.

```c
uint8_t buf[64] = {0};
nbq_put_u32(buf, 512, /*bit_pos*/ 13, /*width*/ 12, 3000);   /* write */

uint32_t v;
nbq_get_u32(buf, 512, 13, 12, &v);                           /* v == 3000 */
```

The name comes from the operation it performs: get or put an *n-bit
quantity*.

## The problem

Bytes are the smallest thing C can address, but a great deal of data is not
organised in bytes. A compressed file, a network protocol, a hardware
register map or a succinct data structure will say something like:

> bits 0–4: symbol index · bits 5–16: jump offset · bits 17–23: flags

Three fields, 24 bits, none of them byte-sized and one of them straddling a
byte boundary. In C you write the shifts and masks yourself:

```c
offset = ((buf[0] & 0x07) << 9) | (buf[1] << 1) | (buf[2] >> 7);
```

That line is correct, but it took a minute to write, it is wrong for the
next field, and nobody reviewing it can tell at a glance what it does. The
standard library offers nothing better. `memcpy` moves bytes. Struct
bit-fields have implementation-defined layout, so two compilers can disagree
about where a field lives. C++ `std::bitset` cannot hand you back an integer
from an offset.

nbitq is the missing primitive. The same three fields:

```c
nbq_reader r;
nbq_reader_init(&r, buf, 24);

uint32_t symbol, offset, flags;
nbq_read_u32(&r, 5,  &symbol);
nbq_read_u32(&r, 12, &offset);
nbq_read_u32(&r, 7,  &flags);
```

No alignment requirements, no padding, no hand-rolled masks, and every call
is bounds-checked against the length you passed.

## Who it is for

**People writing or parsing a bit-packed format.** Codecs, compressed
containers, succinct data structures such as packed tries and bit vectors,
anything where fields are sized to their content rather than to a byte.
nbitq is the layer between "here is a buffer" and "here is a parser": it
addresses bits, and your code gives them meaning.

**People talking to hardware or a wire protocol.** Register dumps, CAN
frames, radio packets, sensor telemetry. The spec gives you bit offsets and
widths; nbitq takes bit offsets and widths.

**Firmware.** Nothing in the core allocates. The reader can point at ROM.
With one define the library drops every hosted header and builds
`-ffreestanding` with no libc, in under 5 KB of `.text` on a Cortex-M. There
is a separate code path that never uses 64-bit arithmetic, for targets where
a `uint64_t` shift is a library call.

**People who need the bytes to be the same everywhere.** The bit order is
fixed and documented. Varints are canonical, so one value has one encoding.
Two programs built with different compilers on different machines produce
identical output for the same input.

## Who it is not for

- **Your fields are whole bytes.** Use `memcpy`.
- **You want a schema compiler** that reads a format description and
  generates a parser. [Kaitai Struct](https://kaitai.io/) does that.
- **You want maximum throughput on bulk integer arrays.** SIMD bit-packing
  libraries such as [simdcomp](https://github.com/lemire/simdcomp) are far
  faster at that specific job. nbitq is deliberately not optimised; see
  [Performance](how-it-works.md#performance-and-why-it-is-bit-at-a-time).
- **You are in Python.** [bitstring](https://bitstring.readthedocs.io/) is
  more capable and more ergonomic there.

## What it gives you

Beyond fixed-width get and put, the library covers the things a bit-packed
format needs next:

| Need | nbitq |
|---|---|
| A field at a known position | `nbq_get_u32` / `nbq_put_u32`, and 64-bit and signed variants |
| A sequence of fields | `nbq_reader` / `nbq_writer` cursors, with seek and align |
| A small number in few bits | varints at any group width from 2 to 64 bits; base 8 is LEB128 exactly |
| A span of bits moved somewhere else | `nbq_copy_bits` from any offset to any offset, overlap-safe |
| Compare, fill, count | `nbq_compare_bits`, `nbq_fill_bits`, `nbq_popcount_bits` |
| 64-bit values on a 32-bit CPU | `nbq_get_parts` / `nbq_put_parts`, using only 32-bit arithmetic |
| Hand a bit run to another program | the optional `.nbq` container: length, type string, CRC-64 |

## What you can rely on

- **Bit 0 is the most significant bit of byte 0**, and fields are written
  most-significant bit first. This never changes.
- **Every operation is all-or-nothing.** On failure nothing is written and no
  cursor moves, so you can try a read, have it fail, and carry on.
- **Bad input is refused, not approximated.** An out-of-range width or
  position, a varint longer than 64 bits can hold, or an overlong varint
  encoding all return errors.
- **Varints are canonical.** One value encodes exactly one way.

## Lineage

The API follows `GetNBitQty` / `PutNBitQty` from dio_BitStream
(M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a 12-bit jump offset,
a 7-bit value into a continuous bit stream, then read them back as native
integers." That shape has held up for twenty-five years and is kept. The
implementation is new, and three things are different because the original
did not do them: bounds are checked, an out-of-range width is an error rather
than silently clamped, and the indexed and cursor paths share one
implementation instead of being written twice.

## Next

- [Getting started](getting-started.md): install and first program.
- [How it works](how-it-works.md): bit layout, varints and the container,
  worked down to the byte.
