# How it works

This page explains what nbitq does with the bits, from first principles and
down to the byte. Every byte value shown was produced by running the
library.

## 1. Bit order

A bit stream is numbered from one end of each byte or the other. nbitq does
both. The plain names are MSB first; the `_le` names and the `_le` cursor
initialisers are LSB first.

```
byte 0 = 0xA5 = 1 0 1 0 0 1 0 1
MSB first        0 1 2 3 4 5 6 7      bit 0 is the MSB, weight 128
LSB first        7 6 5 4 3 2 1 0      bit 0 is the LSB, weight 1
```

In each order a value's first bit goes to the lowest bit index: MSB first
that is the value's top bit, LSB first its bit 0. So 8 in four bits at bit 0
is `80` MSB first and `08` LSB first. The three fields from the README, 19
in 5 bits, 3000 in 12 and 0x55 in 7:

```
MSB first   9D DC 55     1 0 0 1 1 1 0 1   1 1 0 1 1 1 0 0   0 1 0 1 0 1 0 1
LSB first   13 77 AB     0 0 0 1 0 0 1 1   0 1 1 1 0 1 1 1   1 0 1 0 1 0 1 1
```

Read in its own order, either gives 19, 3000, 0x55. Read `13 77 AB` MSB
first gives 2, 1775, 0x2B: the bits are the same, the numbering is not.

MSB first is network order and what most codec bit readers use, H.264 and
MPEG among them. LSB first is DEFLATE, Vorbis and most PC-heritage packers.
Byte order follows from bit order at aligned positions, which is why the
suffixes are `_be` and `_le`: 0x1234 in 16 bits at bit 0 is `12 34` MSB
first and `34 12` LSB first.

Underneath there is one implementation with the order as a parameter, and
one place a bit is addressed. The order is a property of a stream, declared
once. Within an order the layout is fixed; it is the one convention that,
left ambiguous, makes two implementations disagree about every file.

The sections below show MSB first. LSB first mirrors each example.

## 2. Packing fields

Writing a value narrower than a byte fills bits from the current position
onward, continuing into the next byte when the current one is full.

Two fields in one byte:

```c
nbq_write_u32(&w, 5, 21);    /* 21 = 10101 */
nbq_write_u32(&w, 3,  3);    /*  3 = 011   */
```

```
bit index   0 1 2 3 4 5 6 7
value       1 0 1 0 1 0 1 1      = 0xAB
            └── 21 ──┘ └ 3 ┘
```

A field that crosses a byte boundary:

```c
nbq_write_u32(&w, 5, 21);    /* 10101        -> bits 0..4  */
nbq_write_u32(&w, 7, 97);    /* 1100001      -> bits 5..11 */
```

```
byte 0      1 0 1 0 1 1 1 0      = 0xAE
            └── 21 ──┘ └ 97: first 3 bits

byte 1      0 0 0 1 0 0 0 0      = 0x10
            └ 97: last 4 bits ┘ (unwritten)
```

The library handles the boundary; the caller does not track which byte it
is in, and nothing is padded.

The example from the README, a 5-bit symbol, a 12-bit offset and 7 bits of
flags:

```c
nbq_write_u32(&w, 5,  19);
nbq_write_u32(&w, 12, 3000);
nbq_write_u32(&w, 7,  0x55);
```

```
byte        0x9D              0xDC              0x55
bits        1 0 0 1 1 1 0 1   1 1 0 1 1 1 0 0   0 1 0 1 0 1 0 1
            └─ 19 ──┘ └──────── 3000 ────────┘ └── 0x55 ───┘
index       0       4 5                     16 17            23
```

Twenty-four bits, three fields, no padding.

### Where each bit goes

Putting a field into a buffer that already holds data is the operation
everything else is built from. The value has bits numbered from its bit 0,
the stream has positions numbered from its bit 0, and `put` pairs them off
one at a time, `n` of them, from `bit_pos` on. In an MSB-first stream the
value's top bit, bit `n-1`, goes to `bit_pos`; in an LSB-first stream its
bit 0 does. Bits of the buffer outside the field are neither read nor
written. 3000 in 12 bits at bit 5, into a buffer that holds `A0 00 FF`:

```c
uint8_t buf[3] = { 0xA0, 0x00, 0xFF };
nbq_put_u32(buf, 24, 5, 12, 3000);      /* A5 DC 7F */
```

```
stream bit       0  1  2  3  4  5  6  7    8  9 10 11 12 13 14 15   16 17 18 19 20 21 22 23
before  A0 00 FF 1  0  1  0  0  0  0  0    0  0  0  0  0  0  0  0    1  1  1  1  1  1  1  1
value bit                      11 10  9    8  7  6  5  4  3  2  1    0
after   A5 DC 7F 1  0  1  0  0  1  0  1    1  1  0  1  1  1  0  0    0  1  1  1  1  1  1  1
```

The field starts in the middle of byte 0, fills byte 1 and ends with one
bit in byte 2. Byte 0 keeps its first five bits and byte 2 its last seven.
The same call in the LSB-first order pairs the value's bit 0 with stream
bit 5, which is bit 5 counted from the bottom of byte 0. The bytes are
drawn MSB on the left, as a hex dump shows them, so the stream indices run
the other way:

```c
uint8_t buf[3] = { 0xA0, 0x00, 0xFF };
nbq_put_u32_le(buf, 24, 5, 12, 3000);   /* 00 77 FF */
```

```
stream bit       7  6  5  4  3  2  1  0   15 14 13 12 11 10  9  8   23 22 21 20 19 18 17 16
before  A0 00 FF 1  0  1  0  0  0  0  0    0  0  0  0  0  0  0  0    1  1  1  1  1  1  1  1
value bit        2  1  0                  10  9  8  7  6  5  4  3                        11
after   00 77 FF 0  0  0  0  0  0  0  0    0  1  1  1  0  1  1  1    1  1  1  1  1  1  1  1
```

Three bits of the value land at the top of byte 0, eight fill byte 1 and
the last one is the bottom bit of byte 2.

## 3. Reading

Reading extracts `n` bits starting at a position and assembles them into an
integer, most-significant bit first:

```c
uint32_t offset;
nbq_get_u32(buf, 24, /*bit_pos*/ 5, /*n*/ 12, &offset);   /* 3000 */
```

The cursor reader does the same thing at its own position and then advances:

```c
nbq_reader r;
nbq_reader_init(&r, buf, 24);
nbq_read_u32(&r, 5,  &symbol);   /* r.pos: 0  -> 5  */
nbq_read_u32(&r, 12, &offset);   /* r.pos: 5  -> 17 */
nbq_read_u32(&r, 7,  &flags);    /* r.pos: 17 -> 24 */
```

`nbq_read_u32` is literally `nbq_get_u32` at `r.pos` followed by
`r.pos += n` on success. There is one implementation of every extraction,
so the two styles cannot disagree.

Reading is the pairing in the previous section run the other way. The bits
at positions `bit_pos` to `bit_pos + n - 1` are gathered into the result,
MSB first into bits `n-1` down to 0 and LSB first into bits 0 up to `n-1`,
and no other bit of the buffer is looked at. `A5 DC 7F` read at bit 5 for
12 bits gives 3000. `00 77 FF` read the same way LSB first gives 3000 too,
and MSB first gives 239: the pairing is part of the stream's definition,
not of the bytes.

## 4. Signed fields

A signed field is stored as two's complement in `n` bits. On read, bit
`n-1` is the sign and is extended to fill the destination type.

```c
nbq_put_s32(buf, cap, 0, 4, -3);          /* -3 in 4 bits = 1101 */
```

```
byte 0      1 1 0 1 0 0 0 0      = 0xD0
            └ -3 ─┘
```

```c
int32_t  s; nbq_get_s32(buf, cap, 0, 4, &s);   /* s == -3 */
uint32_t u; nbq_get_u32(buf, cap, 0, 4, &u);   /* u == 13 */
```

The same four bits read as signed or unsigned give −3 or 13. The buffer
does not know which it is; the caller does. Sign extension copies bit
`n-1` into every bit above it; the unsigned read puts zeros there:

```
field, 4 bits                                             1  1  0  1
int32_t   -3   0xFFFFFFFD    1  1  1  1  ...  1  1  1  1  1  1  0  1   bit 3 copied into bits 31..4
uint32_t  13   0x0000000D    0  0  0  0  ...  0  0  0  0  1  1  0  1   zeros above bit 3
```

### Truncation

`nbq_put_u32` and `nbq_put_s32` write the low `n` bits of the value and do
not check that it fits. 3000 into an 8-bit field stores 184, its low byte;
13 into a signed 4-bit field stores `1101`, which reads back as −3:

```c
nbq_put_u32(buf, cap, 0, 8, 3000);   /* B8: reads back as 184 */
nbq_put_s32(buf, cap, 0, 4, 13);     /* D0: reads back as -3 */
```

```
value 3000       1  0  1  1   1  0  1  1  1  0  0  0
                 not written  the low 8 bits
stream bit       0  1  2  3  4  5  6  7
before  00       0  0  0  0  0  0  0  0
value bit        7  6  5  4  3  2  1  0
after   B8       1  0  1  1  1  0  0  0
```

"The low n bits" is a well-defined operation in its own right, and the
range a field may hold is known where the field is defined, so the check
belongs to the caller: `value < (1u << n)` for an unsigned field, or
`nbq_varint_bits` when the width should follow the value instead.

## 5. Varints and UTF-8

A varint stores a number in as many fixed-size groups as it needs. Each
group has `base` bits: `base − 1` payload bits and one continuation bit
that says "another group follows". Groups are written least-significant
payload first.

In nbitq the group width is a parameter, 2 to 64. **Base 8 is
LEB128 exactly**, byte for byte:

```c
nbq_put_varint(buf, cap, 0, 8, 300, &next);
```

```
300 = 0b1_0010_1100
group 0:  payload 0101100 (low 7 bits), continuation 1  ->  1 0101100  = 0xAC
group 1:  payload 0000010 (next 7),     continuation 0  ->  0 0000010  = 0x02
```

Those are the two bytes protobuf writes for 300.

A smaller base costs less on small numbers. Base 5 (4 payload bits + 1):

| value | base 5 | base 8 |
|---|---:|---:|
| 0 | 5 bits | 8 bits |
| 7 | 5 | 8 |
| 15 | 5 | 8 |
| 16 | 10 | 8 |
| 100 | 10 | 8 |
| 123,456 | 25 | 24 |

```c
nbq_put_varint(buf, cap, 0, 5, 16, &next);     /* 10 bits */
```

```
16 = 0b1_0000
group 0:  payload 0000, continuation 1  ->  1 0000
group 1:  payload 0001, continuation 0  ->  0 0001
bits      1 0 0 0 0 0 0 0 0 1 . . . . . .
bytes     0x80          0x40
```

`nbq_varint_bits(value, base)` returns the cost without writing, so a
corpus can be measured at several bases.

### Canonical

One value has exactly one encoding. An encoder never emits a trailing
all-zero group with the continuation bit set, and the decoder refuses to
accept one: `0x80 0x00` in base 8 is `NBQ_ERR_MALFORMED`, not another way to
write zero. A varint whose payload would exceed 64 bits is also refused.
This is what lets two programs write the same data and produce the same
bytes.

### Signed

`nbq_put_varint_s` applies zigzag first: 0 → 0, −1 → 1, 1 → 2, −2 → 3, and
so on. Small negative numbers stay small. Measure signed data with
`nbq_varint_bits_s`, because the zigzag changes the answer.

### UTF-8

A code point is an integer and UTF-8 is a self-delimiting encoding of it,
so it sits beside the varints: the first byte says how many follow, and
one value has one encoding. `nbq_put_utf8` writes the 1 to 4 bytes as
8-bit fields in the stream's order; `nbq_get_utf8` reads the lead byte,
then the continuation bytes it announces.

```c
nbq_put_utf8(buf, cap, 0, 0x20AC, &next);   /* the euro sign */
```

```
U+20AC = 0010 0000 1010 1100, 16 bits: three bytes
byte 0:  1110 0010   lead, "three bytes", top 4 bits 0010       =  0xE2
byte 1:  10 000010   continuation, next 6 bits                  =  0x82
byte 2:  10 101100   continuation, low 6 bits                   =  0xAC
```

Those are the bytes every UTF-8 text has for the euro sign, and because a
byte-aligned 8-bit field is the same byte in either order, `nbq_put_utf8_le`
produces them too. Off alignment the two orders differ: after a 3-bit field
holding 5, the same three bytes are `BC 50 55 80` MSB first and
`15 17 64 05` LSB first, and each reads back only in its own order.

The three bytes are three 8-bit fields, so off alignment they straddle four
bytes of the stream, and `nbq_get_utf8` gathers them back the same way.
After that 3-bit field holding 5:

```c
uint8_t buf[4] = { 0xA0, 0, 0, 0 };         /* 101 in bits 0..2 */
uint64_t next;
nbq_put_utf8(buf, 32, 3, 0x20AC, &next);    /* BC 50 55 80, next == 27 */
```

```
stream bit             0  1  2  3  4  5  6  7    8  9 10 11 12 13 14 15   16 17 18 19 20 21 22 23   24 25 26 27 28 29 30 31
before  A0 00 00 00    1  0  1  0  0  0  0  0    0  0  0  0  0  0  0  0    0  0  0  0  0  0  0  0    0  0  0  0  0  0  0  0
from byte                      E2 E2 E2 E2 E2   E2 E2 E2 82 82 82 82 82   82 82 82 AC AC AC AC AC   AC AC AC
after   BC 50 55 80    1  0  1  1  1  1  0  0    0  1  0  1  0  0  0  0    0  1  0  1  0  1  0  1    1  0  0  0  0  0  0  0
```

Bits 0 to 2 keep the 5, the twenty-four bits of `E2 82 AC` follow from
bit 3, and bits 27 to 31 are untouched. `next` is 27, where the next field
starts.

The decoder accepts exactly the well-formed sequences of the Unicode
standard (Table 3-7) and nothing else. `C0 80` is `NBQ_ERR_MALFORMED`, not
another way to write U+0000; so is a surrogate `ED A0 80`, an `F5` lead
byte, or a continuation byte where a lead byte should be. A sequence cut
off by the end of the buffer, `E2 82` in 16 bits, is `NBQ_ERR_RANGE`: the
bytes so far are valid and more data would finish them. Strings are the
caller's loop; the API reference has the two usual shapes.

## 6. Bit runs

`nbq_copy_bits` moves `n` bits from any bit offset in one buffer to any bit
offset in another, or in the same buffer:

```c
nbq_copy_bits(src, src_bits, 3, dst, dst_bits, 11, 157);   /* 157 bits, 3 -> 11 */
```

Neither end needs to be byte-aligned. Within one buffer, overlapping source
and destination ranges are handled correctly in both directions, like
`memmove`. `nbq_compare_bits`, `nbq_fill_bits` and `nbq_popcount_bits` work
over the same kind of range.

The result is a run of bits; nbitq does not interpret it.

## 7. Bounds and atomicity

Every indexed call takes the number of valid bits in the buffer and checks
the operation against it. The check is written so that a position from
untrusted input cannot wrap:

```c
static inline int in_range(uint64_t limit, uint64_t pos, uint64_t n)
{
    return n <= limit && pos <= limit - n;
}
```

`pos + n` is never computed, so a position near `UINT64_MAX` that came from
file content cannot wrap around to look valid.

Every operation is all-or-nothing. A varint checks that its whole encoding
fits before writing any of it; a read that fails leaves the cursor where it
was, so it can be retried, which a parser resynchronising on damaged input
needs.

## 8. The container

The core has no file format, and for most uses that is right. But a bare
run of 1,234 bits cannot say how long it is or what it was, so an optional
wrapper exists in a separate translation unit:

```
[ "NBQ\1" ][ varint type_len ][ type_str ][ varint n_bits ]
[ payload, zero-padded to a byte ][ CRC-64 ]
```

A real one, wrapping the 12 bits `0xAE 0x10` from §2 under the type `demo`:

```
4E 42 51 01    "NBQ" and container version 1
04             type_len = 4      (base-8 varint)
64 65 6D 6F    "demo"
0C             n_bits = 12       (base-8 varint)
AE 10          payload, padded to 2 bytes
45 42 6B F9 29 F4 61 30          CRC-64 of the 12 bytes above, big-endian
```

Twenty bytes. The type string is free-form and never interpreted; it exists
so the reading code can dispatch. The container does not look inside the
payload.

The checksum is CRC-64/XZ (ECMA-182 polynomial, reflected, init and final
XOR all ones), the same one `xz` uses, and the implementation is verified
against the published vector `CRC-64/XZ("123456789") == 995DC9BBDF1939FA`.
It is computed bitwise with no lookup table, because a 2 KB table is a poor
trade in a library whose whole freestanding build is about 7 KB.

A CRC means the bytes are intact. It does not mean they are trustworthy:
whoever can supply a file can supply a matching checksum. Detecting
tampering needs a signature.

## 9. Values wider than 32 bits, on a 32-bit CPU

On a target without native 64-bit arithmetic, every `uint64_t` shift is a
call into compiler support code, and a bit-stream walk is nothing but
shifts. The `u32` functions are a separate path that assembles the value
in 32-bit arithmetic, and `nbq_get_parts` / `nbq_put_parts` handle widths
up to 64 as a `{hi, lo}` pair of 32-bit halves:

```c
nbq_u64_parts p;
nbq_get_parts(buf, bit_len, bit_pos, 48, &p);   /* p.hi, p.lo */
```

They produce the same bits as `nbq_get_u64`; the fuzz harness checks that
all three paths agree on every read at every alignment.

Positions are `uint64_t` throughout `nbitq.c`, but a field's loop never
touches one. Each fixed-width function converts the position to a byte
pointer and a bit index once, and the loop over the field's bits runs in
`unsigned int`. Where even that one conversion is a cost, `nbitq32.c` is
the u32 pair with `uint32_t` positions: a separate copy with no 64-bit type
in it, held to the same bytes as `nbq_put_u32` by its own test, which also
builds with SDCC for Z80, 8051 and STM8.

## Performance, and why it is bit-at-a-time

nbitq is not optimised. Multi-bit operations loop over single bits rather
than loading words and masking, which costs roughly 3 to 5× on reads
against a word-at-a-time implementation.

There is one place in the code where a bit is addressed, and the bit order
is one select in it:

```c
static inline unsigned int bit_shift(uint64_t pos, nbq_bit_order order)
{
    const unsigned int in_byte = (unsigned int)(pos & 7u);
    return (order == NBQ_LSB_FIRST) ? in_byte : 7u - in_byte;
}

static inline unsigned int bit_get(const uint8_t *buf, uint64_t pos, nbq_bit_order order)
{
    return (buf[pos >> 3] >> bit_shift(pos, order)) & 1u;
}
```

so there is one place a bit can be addressed wrongly. Everything else is
built on it. A word-at-a-time path belongs beside this one, cross-checked
against it by the existing fuzz harness, not instead of it. For codec-grade
throughput, use a codec's reader.

## Next

- [API reference](api-reference.md)
- [Getting started](getting-started.md)
