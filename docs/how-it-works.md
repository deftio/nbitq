# How it works

This page explains what nbitq does with the bits, from first principles and
down to the byte. Every byte value shown was produced by running the
library.

## 1. Bit numbering: MSB first

Within a byte, nbitq numbers bits from the most significant (leftmost) to
the least significant (rightmost). Bit 0 of the buffer is the most
significant bit of byte 0; bit 8 is the most significant bit of byte 1; and
so on.

```
byte 0 = 0xA5 = 1 0 1 0 0 1 0 1
bit index        0 1 2 3 4 5 6 7
                 ^             ^
                 bit 0 (128)   bit 7 (1)
```

A field is written most-significant bit first, so the first bit of a value
lands at the lowest bit index. This matches network byte order and most
codec bit readers; DEFLATE, which packs LSB-first, is the well-known
exception. It is the one convention that, left ambiguous, makes two
implementations disagree about every file, so it is fixed and will not
change.

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

The library handles the boundary. You never need to know which byte you are
in, and nothing is padded because bytes exist.

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
does not know which it is; the caller does.

`nbq_put_s32` writes the low `n` bits of the value. It does not check that
the value fits in `n` bits, because "the low n bits" is a well-defined
operation and sometimes the one you want.

## 5. Varints

A varint stores a number in as many fixed-size groups as it needs. Each
group has `base` bits: `base − 1` payload bits and one continuation bit
that says "another group follows". Groups are written least-significant
payload first.

In nbitq the group width is yours to choose, from 2 to 64. **Base 8 is
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

`nbq_varint_bits(value, base)` tells you the cost without writing, so you
can measure a real corpus at several bases and choose.

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

What you extract is just bits. nbitq has no opinion about what they mean;
hand them to a decompressor, a trie, or your own decoder.

## 7. Bounds and atomicity

Every indexed call takes the number of valid bits in the buffer and checks
the operation against it. The check is written to survive hostile input:

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
was. You can try a read, have it fail, and carry on, which is what a parser
resynchronising on damaged input needs.

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
so *your* code can dispatch. The container never looks inside the payload.

The checksum is CRC-64/XZ (ECMA-182 polynomial, reflected, init and final
XOR all ones), the same one `xz` uses, and the implementation is verified
against the published vector `CRC-64/XZ("123456789") == 995DC9BBDF1939FA`.
It is computed bitwise with no lookup table, because a 2 KB table is a poor
trade in a library whose whole freestanding build is under 5 KB.

A CRC means the bytes are intact. It never means they are trustworthy:
anyone who can hand you a file can hand you a matching checksum. If you
need to detect tampering you need a signature.

## 9. Values wider than 32 bits, on a 32-bit CPU

On a target without native 64-bit arithmetic, every `uint64_t` shift is a
call into compiler support code, and a bit-stream walk is nothing but
shifts. The `u32` functions are a separate path that touches no 64-bit
value, and `nbq_get_parts` / `nbq_put_parts` handle widths up to 64 as a
`{hi, lo}` pair of 32-bit halves:

```c
nbq_u64_parts p;
nbq_get_parts(buf, bit_len, bit_pos, 48, &p);   /* p.hi, p.lo */
```

They produce the same bits as `nbq_get_u64`; the fuzz harness checks that
all three paths agree on every read at every alignment.

## Performance, and why it is bit-at-a-time

nbitq is not optimised. Multi-bit operations loop over single bits rather
than loading words and masking, which costs roughly 3 to 5× on reads
against a word-at-a-time implementation.

That is a deliberate first step. There is one place in the code where a bit
is addressed:

```c
static inline unsigned int bit_get(const uint8_t *buf, uint64_t pos)
{
    return (buf[pos >> 3] >> (7u - (pos & 7u))) & 1u;
}
```

so there is one place a bit can be addressed wrongly. Everything else is
built on it. A word-at-a-time path belongs beside this one, cross-checked
against it by the fuzz harness that already exists, not instead of it. If
you need codec-grade throughput today, use a codec's reader.

## Next

- [API reference](api-reference.md)
- [Getting started](getting-started.md)
