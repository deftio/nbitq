# API reference

Everything is in `nbitq.h` unless marked **container**, which is
`nbitq_container.h`. All functions are `extern "C"`.

Parameter conventions, used throughout:

| name | meaning |
|---|---|
| `buf` | byte buffer; bit 0 is the MSB of `buf[0]` |
| `bit_len` | number of valid bits in a buffer being read |
| `bit_cap` | capacity in bits of a buffer being written |
| `bit_pos` | position of the first bit of the field |
| `n` | field width in bits |
| `base` | varint group width in bits, 2..64 |

## Overview

Every function, by group. The sections below give signatures and the
details. From C++, `nbitq.hpp` wraps the cursor groups as `nbq::Reader`,
`nbq::Writer` and `nbq::OwningWriter`; see [C++ wrapper](#c-wrapper).

**Version and errors**

| Function | Does |
|---|---|
| `nbq_version_string()` / `nbq_version()` | Version of the library that was linked, as a string and as `(major << 16) \| (minor << 8) \| patch` |
| `nbq_strerror(rc)` | Text for a result code |

**Indexed fixed-width integers** (buffer, length, position; no state)

| Function | Does |
|---|---|
| `nbq_get_u64` / `nbq_get_u32` | Read `n` bits at `bit_pos` as an unsigned integer, MSB first |
| `nbq_get_s64` / `nbq_get_s32` | The same, sign-extended from bit `n−1` |
| `nbq_put_u64` / `nbq_put_u32` | Write the low `n` bits of a value at `bit_pos`; bits outside the field are untouched |
| `nbq_put_s64` / `nbq_put_s32` | The same for a signed value (two's complement in `n` bits) |
| `nbq_get_parts` / `nbq_put_parts` | Up to 64 bits as a `{hi, lo}` pair of 32-bit halves, using no 64-bit arithmetic |
| `nbq_get_f32` / `nbq_put_f32` / `nbq_get_f64` / `nbq_put_f64` | A float or double as its IEEE-754 bit pattern in a 32- or 64-bit field; header-only, no floating-point arithmetic |

**Indexed varints**

| Function | Does |
|---|---|
| `nbq_get_varint` / `nbq_put_varint` | Decode or encode an unsigned varint with `base`-bit groups; `next_pos` receives the end |
| `nbq_get_varint_s` / `nbq_put_varint_s` | The same with zigzag for signed values |
| `nbq_varint_bits` / `nbq_varint_bits_s` | Encoded width of a value at a base, without writing |

**Bit runs** (any length)

| Function | Does |
|---|---|
| `nbq_copy_bits` | Move `n` bits from one offset to another, in the same or another buffer; overlap-safe |
| `nbq_fill_bits` | Set `n` bits to 0 or 1 |
| `nbq_compare_bits` | Compare two runs, first differing bit in stream order decides, like `memcmp` |
| `nbq_popcount_bits` | Count the 1 bits in a run |

Every indexed function in the three tables above has an `_le` twin with the
same signature that is LSB first, and a `_be` macro spelling of the plain
name; see [Bit order](#bit-order).

**Cursor** (a reader or writer struct holding a position)

| Function | Does |
|---|---|
| `nbq_reader_init` / `nbq_writer_init` | Point a cursor at caller-owned storage, MSB first; allocates nothing |
| `nbq_reader_init_le` / `nbq_writer_init_le` / `nbq_writer_init_alloc_le` | The same, LSB first; every read or write through the cursor follows |
| `nbq_writer_init_alloc` / `nbq_writer_free` | A writer over a heap buffer that doubles as needed; the only allocation in the library |
| `nbq_reader_remaining` | Bits not yet read |
| `nbq_reader_seek` / `nbq_writer_seek` | Move the position; a fixed writer refuses a position past its capacity |
| `nbq_reader_align` / `nbq_writer_align` | Advance to the next multiple of `n` bits; the writer writes zeros, the reader requires zeros |
| `nbq_read_u64` `_s64` `_u32` `_s32` `_parts` `_varint` `_varint_s` | The indexed get at the cursor position, then advance |
| `nbq_write_u64` `_s64` `_u32` `_s32` `_parts` `_varint` `_varint_s` | The indexed put at the cursor position, then advance |
| `nbq_read_bits` / `nbq_write_bits` | `nbq_copy_bits` to or from the cursor position, then advance |
| `nbq_read_f32` `_f64` / `nbq_write_f32` `_f64` | The float forms at the cursor position, then advance |

**Container** (`nbitq_container.h`, optional)

| Function | Does |
|---|---|
| `nbq_container_size` | Bytes a container of a given type length and payload length occupies |
| `nbq_container_write` | Write magic, type, length, payload and CRC-64 into a buffer |
| `nbq_container_read` | Parse and verify a container; the result aliases the input |
| `nbq_container_read_unchecked` | The same without the CRC check |
| `nbq_crc64` / `nbq_crc64_update` / `nbq_crc64_final` | CRC-64/XZ, one-shot or incremental |

Get and put exist in both the indexed and the cursor form. There is no
insert; see [Not provided](#not-provided) for the way to do it with what is
here.

## Results

```c
typedef enum {
    NBQ_OK            =  0,
    NBQ_ERR_PARAM     = -1,   /* null pointer, or a width/base out of range     */
    NBQ_ERR_RANGE     = -2,   /* the operation would cross the end of the buffer */
    NBQ_ERR_OVERFLOW  = -3,   /* value does not fit the destination type         */
    NBQ_ERR_MALFORMED = -4,   /* varint overlong, or an impossible encoding      */
    NBQ_ERR_ALLOC     = -5    /* only from nbq_writer_init_alloc / grow          */
} nbq_result;

const char *nbq_strerror(nbq_result rc);
```

Every function that can fail returns an `nbq_result`. On any failure nothing
is written and no cursor moves.

## Constants and macros

```c
#define NBQ_VERSION_MAJOR 0
#define NBQ_VERSION_MINOR 1
#define NBQ_VERSION_PATCH 0
#define NBQ_VERSION_STRING "0.2.0"
#define NBQ_VERSION        /* (MAJOR << 16) | (MINOR << 8) | PATCH */

const char *nbq_version_string(void);   /* the linked library's NBQ_VERSION_STRING */
uint32_t    nbq_version(void);          /* the linked library's NBQ_VERSION */

#define NBQ_MAX_WIDTH        64u   /* widest field a single call moves */
#define NBQ_VARINT_MIN_BASE  2u
#define NBQ_VARINT_MAX_BASE  64u

#define NBQ_BYTES_FOR_BITS(bits)   /* bytes needed to hold `bits` bits */
```

Define `NBQ_NO_MALLOC` before compiling `nbitq.c` to remove
`nbq_writer_init_alloc` and every hosted `#include`.

## Bit order

```c
typedef enum { NBQ_MSB_FIRST = 0, NBQ_LSB_FIRST = 1 } nbq_bit_order;
```

The plain names are MSB first: bit 0 is the MSB of byte 0 and a value's top
bit goes first. Every indexed function below has an `_le` twin with the same
signature that is LSB first (bit 0 is the LSB of byte 0, a value's bit 0
goes first) and a `_be` macro spelling of the plain name, so `nbq_get_u32`,
`nbq_get_u32_be` and `nbq_get_u32_le` all exist. Cursors carry the order in
the struct, set by the `_le` initialisers; every cursor function follows it
and there are no `_le` cursor functions.

At a byte-aligned position a 16-bit value is big-endian MSB first and
little-endian LSB first, which is what the suffixes mean. Signed fields,
zigzag, the float forms and the varint rules act on the assembled integer
and are identical in both orders; a varint's groups land wherever the order
puts them. Positions in the bit-run functions are in the stream's order on
both sides. The container is MSB first.

```
19 in 5 bits, 3000 in 12, 0x55 in 7:   MSB first  9D DC 55     LSB first  13 77 AB
0x1234 in 16 bits at bit 0:            MSB first  12 34        LSB first  34 12
```

## Indexed API

No state, no allocation. Each call is bounds-checked against `bit_len` or
`bit_cap`.

### Fixed-width integers

```c
nbq_result nbq_get_u64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned n, uint64_t *out);
nbq_result nbq_get_s64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned n, int64_t  *out);
nbq_result nbq_get_u32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned n, uint32_t *out);
nbq_result nbq_get_s32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned n, int32_t  *out);

nbq_result nbq_put_u64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned n, uint64_t value);
nbq_result nbq_put_s64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned n, int64_t  value);
nbq_result nbq_put_u32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned n, uint32_t value);
nbq_result nbq_put_s32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned n, int32_t  value);
```

- `n` is 1..64 for the 64-bit functions and 1..32 for the 32-bit ones;
  anything else is `NBQ_ERR_PARAM`.
- The signed `get` functions sign-extend from bit `n-1`. The signed `put`
  functions write the low `n` bits of the value.
- `put` leaves bits outside the field untouched.
- The `u32`/`s32` functions use no 64-bit arithmetic. They are a separate
  path that produces identical bits, for targets without a native 64-bit
  type.

### 64-bit values as two halves

```c
typedef struct { uint32_t hi; uint32_t lo; } nbq_u64_parts;

nbq_result nbq_get_parts(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned n, nbq_u64_parts *out);
nbq_result nbq_put_parts(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned n, nbq_u64_parts value);
```

`n` is 1..64. Same bits as `nbq_get_u64` / `nbq_put_u64`, using only 32-bit
arithmetic.

### Floating point

```c
static inline nbq_result nbq_get_f32(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, float  *out);
static inline nbq_result nbq_put_f32(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, float  value);
static inline nbq_result nbq_get_f64(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, double *out);
static inline nbq_result nbq_put_f64(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, double value);

static inline uint32_t nbq_f32_bits(float f);          /* the reinterpretation, both ways */
static inline float    nbq_f32_from_bits(uint32_t u);
static inline uint64_t nbq_f64_bits(double d);
static inline double   nbq_f64_from_bits(uint64_t u);
```

- A `float` or `double` is stored as its IEEE-754 bit pattern in a 32- or
  64-bit field, MSB first. At a byte-aligned position the bytes are
  big-endian: `3.14159f` is `40 49 0F D0`.
- A copy, not a conversion. NaN payloads, signalling NaNs, signed zeros and
  denormals round-trip bit for bit. `nbq_put_f32(.., f)` writes exactly what
  `nbq_put_u32(.., 32, nbq_f32_bits(f))` writes.
- No floating-point arithmetic or runtime is involved, so a core without an
  FPU pays for a register move. The functions are `static inline` in the
  header; `nbitq.c` contains no floating-point code and a program that never
  calls them links nothing for them.
- The position need not be byte-aligned. On a 32-bit target, `nbq_put_parts`
  with `nbq_f64_bits` split into halves avoids 64-bit arithmetic.

### Varints

```c
nbq_result nbq_get_varint  (const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned base, uint64_t *out, uint64_t *next_pos);
nbq_result nbq_get_varint_s(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, unsigned base, int64_t  *out, uint64_t *next_pos);
nbq_result nbq_put_varint  (uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned base, uint64_t value, uint64_t *next_pos);
nbq_result nbq_put_varint_s(uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, unsigned base, int64_t  value, uint64_t *next_pos);

unsigned nbq_varint_bits  (uint64_t value, unsigned base);
unsigned nbq_varint_bits_s(int64_t  value, unsigned base);
```

- A group is `base` bits: `base − 1` payload bits and one continuation bit.
  Base 8 is LEB128 exactly.
- `next_pos`, if non-NULL, receives the position just past the varint. A
  varint's width is not known until it is read, so an indexed caller needs
  this to advance.
- Overlong encodings and values wider than 64 bits are `NBQ_ERR_MALFORMED`.
- The `_s` variants apply zigzag: 0, −1, 1, −2, 2, … map to 0, 1, 2, 3, 4, …
- `nbq_varint_bits` returns the encoded width without writing, or 0 if
  `base` is invalid.

### Bit runs

```c
nbq_result nbq_copy_bits    (const uint8_t *src, uint64_t src_bits, uint64_t src_pos,
                             uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n);
nbq_result nbq_fill_bits    (uint8_t *buf, uint64_t bit_cap, uint64_t bit_pos, uint64_t n, int value);
nbq_result nbq_compare_bits (const uint8_t *a, uint64_t a_bits, uint64_t a_pos,
                             const uint8_t *b, uint64_t b_bits, uint64_t b_pos, uint64_t n, int *out);
nbq_result nbq_popcount_bits(const uint8_t *buf, uint64_t bit_len, uint64_t bit_pos, uint64_t n, uint64_t *out);
```

- `n` may be any length, not just up to 64. `n == 0` succeeds and does
  nothing.
- `nbq_copy_bits`: `src` and `dst` may be the same buffer; overlapping
  ranges are handled correctly.
- `nbq_fill_bits`: `value` is 0 or nonzero.
- `nbq_compare_bits`: `*out` is <0, 0 or >0 like `memcmp`, comparing
  most-significant bit first.

## Cursor API

Thin wrappers over the indexed API. The structs are public and can live
anywhere.

```c
typedef struct {
    const uint8_t *buf;
    uint64_t bits;        /* valid bits in buf  */
    uint64_t pos;         /* next bit to read   */
    nbq_bit_order order;  /* set by the initialiser */
} nbq_reader;

typedef struct {
    uint8_t *buf;
    uint64_t cap;         /* capacity in bits   */
    uint64_t pos;         /* next bit to write  */
    int owns_buf;         /* nonzero if nbq_writer_init_alloc allocated buf */
    nbq_bit_order order;  /* set by the initialiser */
} nbq_writer;
```

### Setup

```c
nbq_result nbq_reader_init(nbq_reader *r, const uint8_t *buf, uint64_t bit_len);       /* MSB first */
nbq_result nbq_reader_init_le(nbq_reader *r, const uint8_t *buf, uint64_t bit_len);    /* LSB first */
nbq_result nbq_writer_init(nbq_writer *w, uint8_t *buf, uint64_t bit_cap);
nbq_result nbq_writer_init_le(nbq_writer *w, uint8_t *buf, uint64_t bit_cap);
nbq_result nbq_writer_init_alloc(nbq_writer *w, uint64_t initial_bits);      /* not under NBQ_NO_MALLOC */
nbq_result nbq_writer_init_alloc_le(nbq_writer *w, uint64_t initial_bits);
void       nbq_writer_free(nbq_writer *w);
```

- The `_le` initialisers set `order` to `NBQ_LSB_FIRST`; the others set
  `NBQ_MSB_FIRST`. Every read, write, align and bit-run call through the
  cursor follows it.

- `nbq_reader_init` and `nbq_writer_init` allocate nothing. The reader's
  buffer may be in ROM. A fixed writer never grows; a write past `bit_cap`
  is `NBQ_ERR_RANGE`.
- `nbq_writer_init_alloc` gives a writer that owns a heap buffer and doubles
  it as needed. It is the only allocating function in the library. Release
  with `nbq_writer_free`, which is a no-op for a writer that does not own
  its buffer.

### Position

```c
uint64_t   nbq_reader_remaining(const nbq_reader *r);
nbq_result nbq_reader_seek (nbq_reader *r, uint64_t bit_pos);   /* refuses pos > bits */
nbq_result nbq_writer_seek (nbq_writer *w, uint64_t bit_pos);   /* fixed: refuses pos > cap; growing: grows */
nbq_result nbq_reader_align(nbq_reader *r, unsigned n);
nbq_result nbq_writer_align(nbq_writer *w, unsigned n);
```

`align` advances to the next multiple of `n` bits. The writer writes zeros;
the reader requires the skipped bits to be zero and returns
`NBQ_ERR_MALFORMED` otherwise. `n` of 8 is byte alignment.

### Sequential reads

Each is the indexed call at `r->pos`, then advance on success.

```c
nbq_result nbq_read_u64     (nbq_reader *r, unsigned n, uint64_t *out);
nbq_result nbq_read_s64     (nbq_reader *r, unsigned n, int64_t *out);
nbq_result nbq_read_u32     (nbq_reader *r, unsigned n, uint32_t *out);
nbq_result nbq_read_s32     (nbq_reader *r, unsigned n, int32_t *out);
nbq_result nbq_read_parts   (nbq_reader *r, unsigned n, nbq_u64_parts *out);
nbq_result nbq_read_varint  (nbq_reader *r, unsigned base, uint64_t *out);
nbq_result nbq_read_varint_s(nbq_reader *r, unsigned base, int64_t *out);
nbq_result nbq_read_bits    (nbq_reader *r, uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n);

static inline nbq_result nbq_read_f32(nbq_reader *r, float  *out);   /* advance 32 */
static inline nbq_result nbq_read_f64(nbq_reader *r, double *out);   /* advance 64 */
```

### Sequential writes

```c
nbq_result nbq_write_u64     (nbq_writer *w, unsigned n, uint64_t value);
nbq_result nbq_write_s64     (nbq_writer *w, unsigned n, int64_t value);
nbq_result nbq_write_u32     (nbq_writer *w, unsigned n, uint32_t value);
nbq_result nbq_write_s32     (nbq_writer *w, unsigned n, int32_t value);
nbq_result nbq_write_parts   (nbq_writer *w, unsigned n, nbq_u64_parts value);
nbq_result nbq_write_varint  (nbq_writer *w, unsigned base, uint64_t value);
nbq_result nbq_write_varint_s(nbq_writer *w, unsigned base, int64_t value);
nbq_result nbq_write_bits    (nbq_writer *w, const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint64_t n);

static inline nbq_result nbq_write_f32(nbq_writer *w, float  value);
static inline nbq_result nbq_write_f64(nbq_writer *w, double value);
```

A growing writer (from `nbq_writer_init_alloc`) enlarges its buffer when a
write would pass `cap`; a fixed writer returns `NBQ_ERR_RANGE`.

## Not provided

### Insert and delete

Insert is not in the API because it is a short composition of what is. The
example was run against the library.

`put` overwrites in place. To insert a field, open a gap with
`nbq_copy_bits`, which handles the overlap, then put into it. `len` is the
number of bits in use; `cap` is the buffer's capacity.

```c
static nbq_result insert_u32(uint8_t *buf, uint64_t cap, uint64_t *len,
                             uint64_t pos, unsigned n, uint32_t value)
{
    nbq_result rc = nbq_copy_bits(buf, cap, pos, buf, cap, pos + n, *len - pos);
    if (rc != NBQ_OK) return rc;              /* no room: nothing moved */
    rc = nbq_put_u32(buf, cap, pos, n, value);
    if (rc != NBQ_OK) return rc;
    *len += n;
    return NBQ_OK;
}

static nbq_result delete_bits(uint8_t *buf, uint64_t cap, uint64_t *len,
                              uint64_t pos, unsigned n)
{
    nbq_result rc = nbq_copy_bits(buf, cap, pos + n, buf, cap, pos, *len - pos - n);
    if (rc != NBQ_OK) return rc;
    *len -= n;
    return NBQ_OK;
}
```

Starting from the 24-bit example `9D DC 55` (fields 19, 3000, 0x55),
inserting the 3-bit value 6 at bit 5 gives `9E BB 8A A0`, 27 bits, and the
fields read back as 19, 6, 3000, 0x55. Deleting those 3 bits restores
`9D DC 55`. An insert that would pass `cap` fails with `NBQ_ERR_RANGE` and
moves nothing. Insertion is linear in the number of bits after `pos`.

### Reading backwards

Some formats are read from the end: a trailer whose members are found from
the last one, or an entropy coder that emits in reverse. Bit order and
traversal direction are independent, so there is no backward reader. The
indexed functions with a position that counts down do it, in either order:

```c
uint64_t pos = bit_len;                      /* start at the end */
pos -= 12; nbq_get_u32_le(buf, bit_len, pos, 12, &offset);
pos -= 5;  nbq_get_u32_le(buf, bit_len, pos, 5,  &symbol);
```

Each member is read at the position where it ends minus its width, which
any backward format must make known. A miscount that subtracts past the
start wraps to a huge position and is refused with `NBQ_ERR_RANGE`, because
the range check compares against the length rather than computing `pos + n`.

A varint can be walked backwards too. Its last group is the one with the
continuation bit clear, and every earlier group of the same varint has it
set, so step back over the group that ends at the cursor, then keep stepping
while the group before is a continued one. Run against the library, at base
8 and at an unaligned base 5:

```c
/* Start of the varint that ends at pos. LSB first; any base. */
static uint64_t varint_start_before(const uint8_t *buf, uint64_t bit_len,
                                    uint64_t pos, unsigned base)
{
    const uint64_t cont = (uint64_t)1 << (base - 1);
    uint64_t start = pos, g;
    for (;;) {
        start -= base;                                   /* the group that ends here */
        if (start < base) break;                         /* nothing before it */
        nbq_get_u64_le(buf, bit_len, start - base, base, &g);
        if ((g & cont) == 0u) break;                     /* the group before ends another varint */
    }
    return start;
}
/* then: nbq_get_varint_le(buf, bit_len, varint_start_before(...), base, &value, NULL) */
```

## C++ wrapper

`#include "nbitq.hpp"`; header-only, C++11, includes `nbitq.h`. Three
classes in namespace `nbq`, every method one call into the C function of
the same name with the `nbq_read_` / `nbq_write_` prefix dropped. Results
are `nbq_result`, as in C. Nothing from the standard library; no exceptions.

```cpp
namespace nbq {

class Reader {                       /* over nbq_reader; copyable */
    Reader();                        /* empty: every read fails until init */
    Reader(const uint8_t *buf, uint64_t bit_len);
    nbq_result init(const uint8_t *buf, uint64_t bit_len);
    const uint8_t *buffer() const;   uint64_t length() const;
    uint64_t pos() const;            uint64_t remaining() const;
    nbq_result seek(uint64_t bit_pos);
    nbq_result align(unsigned n);
    nbq_result u64(unsigned n, uint64_t *out);          /* nbq_read_u64 */
    nbq_result s64(unsigned n, int64_t *out);
    nbq_result u32(unsigned n, uint32_t *out);
    nbq_result s32(unsigned n, int32_t *out);
    nbq_result parts(unsigned n, nbq_u64_parts *out);
    nbq_result varint(unsigned base, uint64_t *out);
    nbq_result varint_s(unsigned base, int64_t *out);
    nbq_result bits(uint8_t *dst, uint64_t dst_bits, uint64_t dst_pos, uint64_t n);
    nbq_result f32(float *out);      nbq_result f64(double *out);
    nbq_reader *handle();            /* the C struct; a const overload too */
};

class Writer {                       /* over nbq_writer; not copyable */
    Writer();                        /* empty: every write fails until init */
    Writer(uint8_t *buf, uint64_t bit_cap);
    nbq_result init(uint8_t *buf, uint64_t bit_cap);
    uint8_t *buffer();               uint64_t capacity() const;
    uint64_t pos() const;
    nbq_result seek(uint64_t bit_pos);
    nbq_result align(unsigned n);
    nbq_result u64(unsigned n, uint64_t value);         /* nbq_write_u64 */
    nbq_result s64(unsigned n, int64_t value);
    nbq_result u32(unsigned n, uint32_t value);
    nbq_result s32(unsigned n, int32_t value);
    nbq_result parts(unsigned n, nbq_u64_parts value);
    nbq_result varint(unsigned base, uint64_t value);
    nbq_result varint_s(unsigned base, int64_t value);
    nbq_result bits(const uint8_t *src, uint64_t src_bits, uint64_t src_pos, uint64_t n);
    nbq_result f32(float value);     nbq_result f64(double value);
    nbq_writer *handle();
};

class OwningWriter : public Writer { /* nbq_writer_init_alloc; absent under NBQ_NO_MALLOC */
    OwningWriter();                  /* empty until init */
    explicit OwningWriter(uint64_t initial_bits);
    nbq_result init(uint64_t initial_bits);  /* frees what it owned; 0 = default size */
    ~OwningWriter();                 /* nbq_writer_free */
    /* movable, not copyable */
};

}
```

A failed `init` leaves the object empty rather than half-set. The indexed
functions have no state to wrap and are called directly from C++. A new
cursor function in `nbitq.h` gets a one-line forwarder here.

## Container

`#include "nbitq_container.h"`, compile `nbitq_container.c`. Optional and
separate on purpose.

```c
#define NBQ_CONTAINER_VERSION 1
#define NBQ_FILE_EXTENSION    ".nbq"
#define NBQ_TYPE_MAX          255u     /* longest type string accepted */

typedef struct {
    const char    *type;          /* NOT NUL-terminated */
    size_t         type_len;
    const uint8_t *payload;
    uint64_t       payload_bits;
    uint8_t        version;
} nbq_container;

nbq_result nbq_container_size (size_t type_len, uint64_t payload_bits, uint64_t *out_bytes);
nbq_result nbq_container_write(uint8_t *out, size_t out_cap, const char *type, size_t type_len,
                               const uint8_t *payload, uint64_t payload_bits, size_t *out_len);
nbq_result nbq_container_read (const uint8_t *buf, size_t len, nbq_container *out);
nbq_result nbq_container_read_unchecked(const uint8_t *buf, size_t len, nbq_container *out);
```

- Layout: `"NBQ\1"`, base-8 varint `type_len`, type bytes, base-8 varint
  `payload_bits`, payload zero-padded to a byte, CRC-64 of everything
  before it, big-endian.
- `nbq_container_read` verifies magic, version, every length, the absence
  of trailing bytes, and the CRC. On success `out` aliases `buf`, which must
  outlive it; nothing is copied.
- `nbq_container_read_unchecked` skips only the CRC. Every bounds check
  still runs.
- `type_len` may be 0. Longer than `NBQ_TYPE_MAX` is `NBQ_ERR_PARAM`.

### CRC-64/XZ

```c
#define NBQ_CRC64_INIT UINT64_MAX
uint64_t nbq_crc64       (const uint8_t *data, size_t len);
uint64_t nbq_crc64_update(uint64_t crc, const uint8_t *data, size_t len);
uint64_t nbq_crc64_final (uint64_t crc);
```

ECMA-182 polynomial, reflected, init and final XOR all ones. Verified
against `CRC-64/XZ("123456789") == 0x995DC9BBDF1939FA`. Bitwise, no table.
