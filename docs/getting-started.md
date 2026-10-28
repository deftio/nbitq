# Getting started

## Quickstart

Copy `nbitq.h` and `nbitq.c` into the project. There is no configuration
and nothing to generate. Then, in `first.c`:

```c
#include <stdio.h>
#include "nbitq.h"

int main(void)
{
    uint8_t buf[4] = {0};
    const uint64_t cap = sizeof(buf) * 8;   /* capacity in bits */

    /* write a 5-bit, a 12-bit and a 7-bit field, back to back */
    nbq_writer w;
    nbq_writer_init(&w, buf, cap);
    nbq_write_u32(&w, 5,  19);
    nbq_write_u32(&w, 12, 3000);
    nbq_write_u32(&w, 7,  0x55);

    printf("%02X %02X %02X  (%llu bits)\n",
           buf[0], buf[1], buf[2], (unsigned long long)w.pos);

    /* read them back by position */
    uint32_t symbol, offset, flags;
    nbq_get_u32(buf, cap, 0,  5,  &symbol);
    nbq_get_u32(buf, cap, 5,  12, &offset);
    nbq_get_u32(buf, cap, 17, 7,  &flags);
    printf("%u %u 0x%X\n", symbol, offset, flags);
    return 0;
}
```

```
$ cc -std=c99 -o first first.c nbitq.c && ./first
9D DC 55  (24 bits)
19 3000 0x55
```

Three fields in three bytes with no padding. The middle one crosses a byte
boundary.

```
byte 0 = 0x9D      byte 1 = 0xDC      byte 2 = 0x55
1 0 0 1 1 1 0 1    1 1 0 1 1 1 0 0    0 1 0 1 0 1 0 1
└─ 19 ─┘ └──────── 3000 ────────┘ └──── 0x55 ────┘
```

Every function returns an `nbq_result`; `NBQ_OK` is zero and the rest are
negative. The examples above skip the checks for brevity; see
[Result codes](#result-codes).

## Install

`nbitq.h` is `extern "C"`, so it compiles as C++ as well. The optional
`.nbq` file container is `nbitq_container.h` and `nbitq_container.c`, a
separate translation unit.

### With CMake

Vendored as a subdirectory:

```cmake
add_subdirectory(nbitq)
target_link_libraries(your_target PRIVATE nbitq)
```

Or fetched at configure time:

```cmake
include(FetchContent)
FetchContent_Declare(nbitq
    GIT_REPOSITORY https://github.com/deftio/nbitq.git
    GIT_TAG        v0.2.1)
FetchContent_MakeAvailable(nbitq)
target_link_libraries(your_target PRIVATE nbitq)
```

The `nbitq` target includes the container. Set `NBITQ_BUILD_TESTS=OFF` to
skip building the tests when vendored; they are on by default standalone.

## Indexed and cursor forms

Every operation exists in two forms that share one implementation.

**Indexed** functions take an explicit position and hold no state:

```c
nbq_get_u32(buf, bit_len, bit_pos, n, &out);    /* n = 1..32 */
nbq_put_u32(buf, bit_cap, bit_pos, n, value);
```

For fields at known positions: a header with fixed offsets, a table of
packed records, a register map.

**Cursor** functions use a reader or writer struct that holds a position and
advances it:

```c
nbq_reader r;
nbq_reader_init(&r, buf, bit_len);
nbq_read_u32(&r, 12, &v);        /* reads at r.pos, then r.pos += 12 */
```

For fields that follow one another, where each position depends on what
came before, which is most formats.

The structs are public and plain: on the stack, in static storage, or as a
member of another struct. There is no create/destroy pair. `nbq_reader_init`
accepts a `const` buffer, including one in ROM.

## Bit order

The plain names are MSB first: bit 0 is the MSB of byte 0 and a value's top
bit goes first, as in H.264 and most network formats. For a stream numbered
from the LSB, as DEFLATE and Vorbis are, every indexed function has an `_le`
twin with the same signature, and cursors take the order from their
initialiser:

```c
uint32_t bfinal, btype, len;
nbq_reader r;
nbq_reader_init_le(&r, deflate, bit_len);   /* LSB first from here on */
nbq_read_u32(&r, 1, &bfinal);
nbq_read_u32(&r, 2, &btype);
nbq_reader_align(&r, 8);                    /* a stored block pads to a byte */
nbq_read_u32(&r, 16, &len);                 /* LEN, little-endian as DEFLATE writes it */

nbq_get_u32_le(deflate, bit_len, 8, 16, &len);   /* the same field, indexed */
```

The `_be` names are spellings of the plain ones, for code that reads two
streams side by side. The order is a property of the stream: declare it once
and the layout within it is fixed.

## Result codes

Every function returns an `nbq_result`. Zero is success; the rest are
negative:

```c
nbq_result rc = nbq_read_u32(&r, 12, &v);
if (rc != NBQ_OK) {
    fprintf(stderr, "read failed: %s\n", nbq_strerror(rc));
    /* r.pos is unchanged; nothing was consumed */
}
```

| Code | Meaning |
|---|---|
| `NBQ_OK` | success |
| `NBQ_ERR_PARAM` | null pointer, or a width or base out of range |
| `NBQ_ERR_RANGE` | the operation would touch a bit at or past the end |
| `NBQ_ERR_OVERFLOW` | the value does not fit the destination type |
| `NBQ_ERR_MALFORMED` | a varint that is overlong or impossible; a UTF-8 sequence that is not well-formed; a bad container |
| `NBQ_ERR_ALLOC` | only from the growing writer, when `realloc` fails |

`bit_len` is the one input the library cannot check. Every operation is
checked against the `bit_len` it is given, so pass the real number of valid
bits, not a round number.

## Back-patching a length field

A length field whose value is not known until the body has been written. The
writer can seek backwards:

```c
nbq_writer w;
nbq_writer_init(&w, buf, cap);

nbq_write_u32(&w, 16, 0);               /* length: placeholder */
const uint64_t body_start = w.pos;

/* ... write the body ... */

const uint64_t body_bits = w.pos - body_start;
const uint64_t end = w.pos;
nbq_writer_seek(&w, 0);
nbq_write_u32(&w, 16, (uint32_t)body_bits);
nbq_writer_seek(&w, end);
```

## Varints and group width

A varint spends some bits per group on a continuation flag. LEB128, as used
by protobuf and DWARF, uses 8-bit groups: 7 payload bits plus 1. nbitq takes
the group width as a parameter, which matters when most values are small:

```c
nbq_write_varint(&w, 8, 300);     /* bytes AC 02: identical to LEB128   */
nbq_write_varint(&w, 5, 7);       /* 5 bits                              */
nbq_write_varint(&w, 5, 100);     /* 10 bits                             */

unsigned bits = nbq_varint_bits(100, 5);    /* == 10, without writing  */
```

`nbq_varint_bits` returns the cost of a value at a base without writing it,
so a corpus can be measured at several bases. Signed varints
(`nbq_write_varint_s`) use zigzag, so −1 costs the same as 0.

## UTF-8 code points

One code point in, one out, as the 1 to 4 bytes of its UTF-8 encoding:

```c
nbq_write_utf8(&w, 0x00E9);       /* é: C3 A9 when aligned              */
nbq_write_utf8(&w, 0x20AC);       /* €: E2 82 AC                        */
nbq_write_utf8(&w, 0x1F600);      /* 😀: F0 9F 98 80                    */

uint32_t cp;
nbq_read_utf8(&r, &cp);           /* cp == 0xE9; the cursor moves 16 bits */
```

The decoder accepts only well-formed UTF-8 and returns `NBQ_ERR_MALFORMED`
for anything else: an overlong form, a surrogate, a bad continuation byte.
A sequence the buffer ends in the middle of is `NBQ_ERR_RANGE`, so a parser
reading from a stream knows to wait for more. The encoder refuses a
surrogate or a value above U+10FFFF with `NBQ_ERR_PARAM`. A string is a
loop over these calls with a length or a terminator of the format's
choosing; the [API reference](api-reference.md#strings) has both shapes.

## Growing writer

For output whose size is not known up front:

```c
nbq_writer w;
if (nbq_writer_init_alloc(&w, 256) != NBQ_OK) { /* NBQ_ERR_ALLOC */ }
/* ... writes grow the buffer by doubling as needed ... */
/* w.buf and w.pos are public; copy out the result */
nbq_writer_free(&w);
```

`nbq_writer_init_alloc` is the only function in the library that
allocates.

## Embedded

The core needs nothing from libc. Define `NBQ_NO_MALLOC` and the growing
writer disappears, along with every `#include` of a hosted header:

```sh
clang --target=thumbv7em-none-eabi -Oz -std=c99 -ffreestanding -nostdinc \
      -isystem "$(clang -print-resource-dir)/include" \
      -DNBQ_NO_MALLOC -c nbitq.c
```

The core compiled for every toolchain in `tools/sizes.sh`, as bytes of
`.text` at `-Os` with `NBQ_NO_MALLOC` and `-ffreestanding`. `core` is the
whole library. `u32 cursor only` is what links in for a program that calls
only `nbq_reader_init`, `nbq_writer_init`, `nbq_read_u32` and
`nbq_write_u32`, with function sections garbage-collected from those roots.
`nbitq32` is `nbitq32.c` alone; see
[nbitq32](#nbitq32-the-u32-pair-with-32-bit-positions).

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

The compilers are the ones Ubuntu 22.04 packages, run in the Docker image
`tools/Dockerfile.crossbuild`, plus Apple clang on an arm64 Mac; `make
crossbuild` then `make sizes-table` reproduces the table. On an 8-bit or
16-bit CPU every 64-bit shift is a call into compiler support code, which
is why the whole core is several times larger there while the u32 path is
not. The SDCC rows have no core; the next section says why.

No stdio, no floating-point arithmetic, no libm, no recursion, no global state. Each
reader or writer is independent, so the library is reentrant per instance.

On a 32-bit CPU with no native 64-bit arithmetic, use the `u32` functions
for fields up to 32 bits and `nbq_get_parts` / `nbq_put_parts` for wider
ones. They produce the same bits as the `u64` functions; only the
arithmetic differs.

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

## From C++

The headers are `extern "C"`, so every function works unchanged from C++.
`nbitq.hpp` adds the cursor API as classes. Each method is one call into
the C library with the `nbq_read_` or `nbq_write_` prefix dropped, so
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
destructor; it moves and does not copy. `handle()` on any of the three
returns the C struct for code that takes one.

The wrapper returns `nbq_result` like the C API and uses nothing from the
standard library, so `-fno-exceptions` and freestanding C++ builds are
unaffected. It does not wrap the indexed functions, which have no state to
wrap; call `nbq_get_u32` and the rest directly. `make cxx` compiles, links
and runs the wrapper's tests, one per method.

## Building and testing the library itself

```sh
make check      # tests, ASan+UBSan, fuzz, C++ tests, NBQ_NO_MALLOC, code size
make coverage   # every line of the library executed by the tests, or fail
make targets    # freestanding cross-compiles (needs clang)
```

Or with CMake:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The test suite is exhaustive rather than representative: every width from 1
to 64, every varint base from 2 to 64, every start offset within a byte,
truncation at every bit position. See [CONTRIBUTING.md](../CONTRIBUTING.md)
for what is expected of a change.

## Next

- [How it works](how-it-works.md) shows exactly which bit goes where.
- [API reference](api-reference.md) lists every function.
