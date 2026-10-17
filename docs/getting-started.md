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
    GIT_TAG        v0.1.1)
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
| `NBQ_ERR_MALFORMED` | a varint that is overlong or impossible; a bad container |
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

| target | `.text` |
|---|---:|
| Cortex-M3/M4/M7 (Thumb-2) | 4,843 bytes |
| Cortex-M0 (Thumb-1) | 5,085 |
| ARM (v7-A) | 7,317 |
| AArch64 | 4,401 |
| x86-64 | 4,559 |

No stdio, no floating-point arithmetic, no libm, no recursion, no global state. Each
reader or writer is independent, so the library is reentrant per instance.

On a 32-bit CPU with no native 64-bit arithmetic, use the `u32` functions
for fields up to 32 bits and `nbq_get_parts` / `nbq_put_parts` for wider
ones. They produce the same bits as the `u64` functions; only the
arithmetic differs.

## From C++

```cpp
#include "nbitq.h"       // extern "C" inside

nbq_writer w;
nbq_writer_init(&w, buf, sizeof(buf) * 8);
nbq_write_u32(&w, 12, 3000);
```

`make cxx` compiles and links both headers from a C++ translation unit to
prove this stays true.

## Building and testing the library itself

```sh
make check      # tests, ASan+UBSan, fuzz, C++ link, NBQ_NO_MALLOC, code size
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
