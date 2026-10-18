# What nbitq is

nbitq packs and unpacks n-bit integers in a bit stream: values of any width
from 1 to 64 bits, written back to back or at any bit position in a byte
buffer, and read back as native integers. Two C99 files, no dependencies,
about 5.9 KB compiled.

```c
uint8_t buf[64] = {0};
nbq_put_u32(buf, 512, /*bit_pos*/ 13, /*width*/ 12, 3000);   /* write */

uint32_t v;
nbq_get_u32(buf, 512, 13, 12, &v);                           /* v == 3000 */
```

The name is the operation: get or put an *n-bit quantity*.

## What it does

Bytes are the smallest unit C can address, but a lot of data is not
organised in bytes. A compressed file, a network protocol, a hardware
register map or a succinct data structure is specified like this:

> bits 0–4: symbol index · bits 5–16: jump offset · bits 17–23: flags

Three fields, 24 bits, none of them byte-sized and one straddling a byte
boundary. In C this is shift-and-mask code written by hand:

```c
offset = ((buf[0] & 0x07) << 9) | (buf[1] << 1) | (buf[2] >> 7);
```

That line is correct, but it is wrong for the next field, and a reviewer
cannot tell at a glance what it does. The standard library does not cover
it. `memcpy` moves bytes. Struct bit-fields have implementation-defined
layout, so two compilers can disagree about where a field lives. C++
`std::bitset` cannot return an integer from an offset.

nbitq is that primitive. The same three fields:

```c
nbq_reader r;
nbq_reader_init(&r, buf, 24);

uint32_t symbol, offset, flags;
nbq_read_u32(&r, 5,  &symbol);
nbq_read_u32(&r, 12, &offset);
nbq_read_u32(&r, 7,  &flags);
```

There is no alignment requirement and no padding, and every call is
bounds-checked against the length it is given.

## Where it fits

**Bit-packed formats.** Codecs, compressed containers, succinct data
structures such as packed tries and bit vectors: anything where fields are
sized to their content rather than to a byte. nbitq is the layer between
"here is a buffer" and "here is a parser". It addresses bits; the calling
code gives them meaning.

**Hardware and wire protocols.** Register dumps, CAN frames, radio packets,
sensor telemetry. A spec gives bit offsets and widths; nbitq takes bit
offsets and widths.

**Firmware.** Nothing in the core allocates. The reader accepts a const
buffer in ROM. With one define the library drops every hosted header and
builds `-ffreestanding` with no libc, in under 5 KB of `.text` on a
Cortex-M3. A separate code path uses no 64-bit arithmetic, for targets where
a `uint64_t` shift is a library call.

**Portable output.** The bit order is fixed and documented. Varints are
canonical, so one value has one encoding. Two programs built with different
compilers on different machines produce identical output for the same
input.

## Where it does not fit

- **Fields that are whole bytes.** `memcpy` is enough.
- **A parser generated from a format description.**
  [Kaitai Struct](https://kaitai.io/) does that.
- **Bulk integer arrays at high throughput.** SIMD bit-packing libraries
  such as [simdcomp](https://github.com/lemire/simdcomp) are much faster at
  that job. nbitq is bit-at-a-time by design; see
  [Performance](how-it-works.md#performance-and-why-it-is-bit-at-a-time).
- **Python.** [bitstring](https://bitstring.readthedocs.io/) is more capable
  there.

## Functions

Beyond fixed-width get and put, the library covers what a bit-packed format
needs next:

| Operation | nbitq |
|---|---|
| A field at a known position | `nbq_get_u32` / `nbq_put_u32`, and 64-bit and signed variants |
| A sequence of fields | `nbq_reader` / `nbq_writer` cursors, with seek and align |
| A small number in few bits | varints at any group width from 2 to 64 bits; base 8 is LEB128 exactly |
| A span of bits moved elsewhere | `nbq_copy_bits` from any offset to any offset, overlap-safe |
| Compare, fill, count | `nbq_compare_bits`, `nbq_fill_bits`, `nbq_popcount_bits` |
| A float or double | `nbq_get_f32` / `nbq_put_f32`, `nbq_get_f64` / `nbq_put_f64`: IEEE-754 bit patterns, header-only |
| A stream in the other bit order | `_le` twins of every indexed function and `_le` cursor initialisers; MSB first is the default, LSB first is DEFLATE's |
| From C++ | `nbitq.hpp`: `nbq::Reader`, `nbq::Writer`, `nbq::OwningWriter`; each method one call into the C library |
| 64-bit values on a 32-bit CPU | `nbq_get_parts` / `nbq_put_parts`, using only 32-bit arithmetic |
| A bit run stored in a file | the optional `.nbq` container: length, type string, CRC-64 |

## Fixed conventions

- **Bit order is a property of the stream, declared once.** MSB first
  unless a call or a cursor says `_le`, and LSB first then. Within an order
  the layout is fixed. This does not change.
- **Every operation is all-or-nothing.** On failure nothing is written and no
  cursor moves, so a failed read can be retried.
- **Bad input is refused, not approximated.** An out-of-range width or
  position, a varint longer than 64 bits can hold, or an overlong varint
  encoding is an error.
- **Varints are canonical.** One value has exactly one encoding.

## Lineage

The API follows `GetNBitQty` / `PutNBitQty` from dio_BitStream
(M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a 12-bit jump offset,
a 7-bit value into a continuous bit stream, then read them back as native
integers." That shape is kept. The implementation is new, and three things
differ because the original did not do them: bounds are checked, an
out-of-range width is an error rather than silently clamped, and the indexed
and cursor paths share one implementation instead of being written twice.

## Next

- [Getting started](getting-started.md): install and first program.
- [How it works](how-it-works.md): bit layout, varints and the container,
  worked down to the byte.
