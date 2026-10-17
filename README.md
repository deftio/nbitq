# nbitq

**Arbitrary-width bit field read/write. C99, no dependencies, ~1,000 lines.**

Put an *n*-bit quantity into a buffer at a bit position. Get it back out.
That is the whole library.

```c
uint8_t buf[64] = {0};
nbq_put_u32(buf, sizeof(buf) * 8, /*bit*/ 13, /*width*/ 12, 3000);

uint32_t v;
nbq_get_u32(buf, sizeof(buf) * 8, 13, 12, &v);   /* v == 3000 */
```

Thirteen is a bit offset, not a byte offset, and twelve is a bit width. No
alignment is required anywhere, and nothing is padded because bytes exist.

## It is not a format

nbitq has **no header, no magic bytes, no version field and no opinion about
what the bits mean.** A run of bits you extract is a run of bits; whatever
decodes it afterwards — a trie, a decompressor, your own parser — is a layer
this code knows nothing about and must never need to.

That is deliberate. Most libraries in this space either hand you a bit
*container* (`std::bitset`, `boost::dynamic_bitset`) with no way to read an
integer out of an arbitrary position, or a *serialisation format* that owns
your bytes. This is the primitive in between.

## Two access styles, one implementation

**Indexed** — `(buffer, bit_length, bit_position) → value`. No object, no
state, nothing allocated:

```c
nbq_get_u64(buf, bit_len, bit_pos, n, &out);
nbq_get_varint(buf, bit_len, bit_pos, base, &out, &next_pos);
```

**Cursor** — a position that advances. The struct is public and contains no
hidden state, so put it on the stack, in static storage, or inside your own
object. There is no create/destroy pair to forget:

```c
nbq_reader r;
nbq_reader_init(&r, buf, bit_len);        /* allocates nothing */
nbq_read_u32(&r, 5, &symbol);
nbq_read_varint(&r, 5, &offset);
```

Every cursor function is the indexed call at the cursor plus an advance, so
there is **one** implementation of each extraction. That matters: the two
families in the predecessor library diverged, and the divergence is exactly
where a bug lived — the cursor path bounds-checked, the indexed path could
not, and a position computed from file content walked off a heap buffer.

## `bit_length` is not ceremony

Every indexed call takes the buffer's length in bits and refuses anything
that would touch a bit at or past it. The predecessor API omitted it, so it
*could not* check, and a caller forgot to. The resulting out-of-bounds read
was found by a fuzzer in under a minute.

"Dumb" should mean *no interpretation*. It should not mean *no bounds*.

## What it does

| | |
|---|---|
| Unsigned integers | 1–64 bits, any position (`u64`, `u32`) |
| Signed integers | 1–64 bits, sign-extended from bit *n*−1 (`s64`, `s32`) |
| 64-bit without 64-bit maths | `nbq_u64_parts {hi, lo}`, 32-bit arithmetic only |
| Varints | **any group width 2–64**, signed or unsigned; base 8 is LEB128 exactly |
| Bit arrays | copy, fill, compare, popcount — any run, any two offsets, overlap-safe |
| Alignment | advance to any multiple of *n* bits; reader verifies the padding is zero |
| Backpatching | `nbq_writer_seek` to fill in a field you did not know yet |

### Varints at a width you choose

A group of `base` bits carries `base − 1` payload bits plus one continuation
bit. Base 8 is LEB128. Nothing requires eight, and **eight is rarely best**:

| distribution | best base | cost of base 8 |
|---|---|---|
| tiny, mean ≈ 3 | 3 | **+111%** |
| small, mean ≈ 30 | 4 | +8% |
| medium, mean ≈ 1,000 | 5 | +9% |
| wide, mean ≈ 100,000 | 6 | +2% |

Eight is the default everywhere because bytes are convenient, not because it
is efficient. `nbq_varint_bits(value, base)` sizes a value without writing it,
so you can measure your own corpus and pick. **This library does not choose
for you** — that is a decision for the layer that knows what the numbers mean.

### Bit arrays

The operation most bit libraries omit:

```c
/* 157 bits from offset 3 of one buffer to offset 11 of another */
nbq_copy_bits(src, src_bits, 3, dst, dst_bits, 11, 157);
```

Neither end byte-aligned, overlapping ranges within one buffer handled
correctly. The extracted run is just bits — hand it to a trie, gzip, or your
own decoder.

## The optional `.nbq` container

nbitq proper has no format, which is right as a default and leaves one gap:
you cannot hand someone a run of 1,234 bits and have them know how long it is
or what it was. `nbitq_container.c` closes that gap and nothing else.

```
[ "NBQ\1" ][ varint type_len ][ type_str ][ varint n_bits ]
[ payload, n_bits, zero-padded to a byte ][ CRC-64 ]
```

```c
uint8_t box[4096];
size_t n;
nbq_container_write(box, sizeof(box), "triepack/trie", 13, bits, 1234, &n);
/* ... later, or elsewhere ... */
nbq_container c;
if (nbq_container_read(box, n, &c) == NBQ_OK)
    /* c.payload, c.payload_bits == 1234, c.type == "triepack/trie" */;
```

`type_str` is free-form and never interpreted — it exists so the *next* layer
can dispatch. The container never looks inside the payload.

**It is a separate translation unit on purpose.** Don't compile it and the
core stays a few hundred lines with no notion of files, magic numbers or
checksums. It costs 1,173 bytes at `-Oz` when you do want it.

`nbq_container_read_unchecked` skips the checksum for a buffer whose integrity
is already established — but still validates every length, because skipping
bounds checks is how a reader walks off its buffer.

### Why CRC-64 and not a hash

CRC-64/XZ (ECMA-182), verified against the published vector
`CRC-64/XZ("123456789") == 995DC9BBDF1939FA` — checked against an outside
source rather than against itself, which is the only way to know the
polynomial, reflection, init and final XOR are all right.

SHA-512 was the first thought and is the wrong tool here. It is ~300 lines and
would be the library's only real dependency, in something whose entire
freestanding build is under 5 KB. And it would not buy what it looks like it
buys: **neither a CRC nor a hash is authentication.** Anyone who can hand you
a file can hand you a matching digest. Both detect accidental corruption;
only a MAC or a signature detects a forger, and that belongs in a layer that
knows about keys.

Computed bitwise with no lookup table — a 256-entry table is 2 KB of rodata,
a poor trade here, and this is a container checksum rather than a hot loop.

## Guarantees

- **Bit 0 is the most significant bit of byte 0**, and fields are written
  most-significant bit first. Fixed, not negotiable later: this is the one
  convention that, left ambiguous, makes two implementations disagree about
  every byte.
- **Operations are atomic.** A call either succeeds completely or fails and
  changes nothing — no partial value, no moved cursor. A varint spans groups,
  and a truncated one that consumed what it managed to read would leave you
  unable to retry or resynchronise.
- **Malformed input is refused, never approximated.** Out-of-range widths,
  positions past the end, overlong varints, and values too wide for 64 bits
  are errors. The predecessor clamped a width request of 40 bits to 31 and
  returned success, which produces a file that is wrong rather than a call
  that failed.
- **Canonical varints.** An overlong encoding is rejected on read, because two
  encoders disagreeing about padding is two files for one value.

## Embedded

**Nothing here needs to allocate.** `nbq_reader_init` and `nbq_writer_init`
take storage you provide; the reader's buffer may be in ROM. The only
allocating function is `nbq_writer_init_alloc`, and `-DNBQ_NO_MALLOC`
compiles it out — after which the library includes **no hosted header at all**
and builds `-ffreestanding`.

Measured, `-Oz -ffreestanding -DNBQ_NO_MALLOC`:

| target | `.text` |
|---|---:|
| **Thumb-2** (Cortex-M3/M4/M7) | **4,805 bytes** |
| Thumb-1 (Cortex-M0) | 5,047 |
| ARM mode (v7-A) | 7,275 |
| AArch64 | 4,375 |
| x86-64 | 4,531 |

Hosted, with the allocator: 4,723 bytes at `-Oz`, 8,755 at `-O2`.

No stdio, no floating point, no libm, no recursion, no global state — so it
is reentrant per instance. Reproduce with `make targets`.

## How it compares

**There is no standard C or C++ bitstream.** C++20's `<bit>` gives bit
operations *on integers* (`popcount`, `countl_zero`, `bit_width`) — useful,
but no positions, no cursor, no buffer. The nearest things are these:

| | what it is | why it is not this |
|---|---|---|
| `std::bitset` | fixed, compile-time size | size is a template parameter; cannot read an *n*-bit integer from a position |
| `std::vector<bool>` | packed dynamic bits | proxy references; no multi-bit integer extraction |
| `boost::dynamic_bitset` | runtime-sized bit set | a *set*, not a stream; no field read/write |
| Python `bitstring` | rich, mature bit fields | Python; not embeddable |
| `bitpacking` / `simdcomp` | SIMD bit-packing of integer *arrays* | fixed width per block, bulk-oriented; not "this field, at this bit" |
| `bitpacker` (C++) | type-safe compile-time bit serialisation | C++17/20, struct-shaped, a serialisation format |
| Ruby `bitpack` | arbitrary-bit-length fields | Ruby; closest in spirit |
| ffmpeg `get_bits.h` | the fast bit reader | LGPL, codec-internal, read-only, no bounds parameter |
| sdsl-lite | succinct structures, rank/select | C++11, heavyweight, a different problem |
| Kaitai Struct | declarative binary parsing | generates a parser from a schema — a format, which is the thing this declines to be |

**What nbitq has that none of them has in one place:** signed *and* unsigned
integers of arbitrary width at an arbitrary **bit** position, bit-array
extraction between two unaligned offsets, varints at a caller-chosen group
width, bounds checking on every indexed call, no allocation required, and a
freestanding build under 5 KB.

### Why codec bit readers are not reusable

Every compressor and codec has one of these, and essentially all of them are
**internal by construction**:

- **ffmpeg `get_bits.h`** is the best-known. `get_bits()` reads 1–17 bits,
  `get_bits_long()` 0–32 — there is no 64-bit read, and no writer (that is a
  separate `put_bits.h`). Bounds checking is **optional and off in places**:
  `CONFIG_SAFE_BITSTREAM_READER` globally, `UNCHECKED_BITSTREAM_READER`
  per-decoder, because a codec validates its own headers and then trusts
  them. It is LGPL and lives inside `libavcodec`.
- **zlib / DEFLATE** keeps a bit accumulator (`hold`, `bits`) inlined in the
  inflate state machine. It is not an API at all — there is no function to
  read a field at a position, because DEFLATE only ever needs "next *n* bits,
  LSB-first, sequentially."
- **xz, zstd, brotli** are the same shape: a local accumulator, sequential,
  one direction, no random access.

The pattern is that a codec's reader is **sequential, one-directional,
optionally unchecked, and specialised to its own bit order.** That is correct
for a codec and useless as a general library: you cannot ask any of them for
"the signed 19-bit field at bit 1,234."

### What Python `bitstring` does that this does not

`bitstring` is the closest thing in spirit, and genuinely does more — it has
four classes (`Bits` immutable, `BitArray` mutable, `ConstBitStream` adds a
position, `BitStream` both). Beyond being Python, it offers:

- **Searching for a bit pattern** at any bit offset (`find`, `findall`,
  `rfind`, `readto`). nbitq has no search. This is the one feature that is
  arguably plumbing rather than interpretation, and the obvious candidate to
  add.
- **Length-changing mutation** — `insert`, `delete`, `append`, `prepend`,
  `invert`, `byteswap`. nbitq is strictly positional: it overwrites a field
  in place and never shifts what follows.
- **Format strings** — `read('uint:12')`, `'hex:8'`, `'se'`, `'ue'`
  (Exp-Golomb) — and multiple bit orders and endiannesses.

So the division is clear: `bitstring` is a bit-manipulation *toolkit* with
search and editing; nbitq is a positional field accessor with bounds. The
toolkit is the better tool for exploring a format by hand; the accessor is
what you link into something that has to be small, freestanding and
byte-identical across implementations.

**Where others win:** ffmpeg's reader and simdcomp are far faster (see below);
`bitstring` is more ergonomic and does more if you are in Python; Kaitai
writes your parser for you if a schema is what you want.

## Performance

Not optimised, on purpose. Every multi-bit operation loops over single bits
rather than loading words and masking — roughly 3–5× slower on reads than a
word-at-a-time implementation.

That buys something worth more right now: there is **one place a bit is
addressed**, so there is one place a bit can be addressed wrongly. The
word-at-a-time path belongs *beside* this one, differentially tested against
it, not instead of it — and the fuzz harness that comparison needs already
exists.

A SIMD path is further out and is a real option for bulk operations
(`nbq_copy_bits`, `nbq_popcount_bits`), where the work is regular enough to
vectorise. It is not today's problem: a vectorised implementation of a
primitive that is not yet proven correct optimises the wrong thing.

## Testing

```bash
make check      # test + sanitize + fuzz + C++ build + size
make test       # 73,943 checks
make sanitize   # the same under ASan + UBSan
make fuzz       # randomised, fixed seed, runs anywhere
make targets    # cross-compile freestanding for embedded targets
```

Exhaustive rather than representative: **every** width 1–64, **every** varint
base 2–64, every start offset within a byte, truncation at every bit position,
and every bounds case including a position chosen to make `pos + n` overflow.

The fuzzer asserts more than "does not crash":

- anything that decodes must **re-encode to identical bits**;
- the `u64`, `u32` and hi/lo paths must agree read-for-read, at every offset;
- a failed read must leave the cursor untouched;
- a copied bit run must compare equal to its source.

Measured: **73,943 core checks + 2,065 container checks**, 100k standalone
fuzz iterations, and 2,901,892 libFuzzer runs, all under ASan + UBSan, all
clean. The container tests round-trip **every payload bit length from 0 to
200**, so the final partial byte and its padding are exercised at all eight
positions, and check that two writers of the same object produce identical
bytes.

## Using it

Two files for the core, two more if you want the container, no build system
required:

```
nbitq.h             nbitq_container.h    (optional)
nbitq.c             nbitq_container.c    (optional)
```

`nbitq.h` is `extern "C"` and a C++ build of it is part of `make check`
rather than a claim in a comment.

### Three ways to consume it

**Copy the files.** The expected case, and the one small C libraries actually
spread by. No build system, no configuration, no options to get wrong.

**Vendor the directory.** `add_subdirectory(nbitq)` and link `nbitq`. The
`CMakeLists.txt` detects whether it is being built standalone or from a
parent, so the same file works either way and vendoring needs no edit. A
parent's `BUILD_TESTS` is honoured if set.

**CMake FetchContent.**

```cmake
include(FetchContent)
FetchContent_Declare(nbitq
    GIT_REPOSITORY https://github.com/deftio/nbitq.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(nbitq)
target_link_libraries(your_target PRIVATE nbitq)
```

There is deliberately no Conan recipe or vcpkg port. Neither is a crates.io
or an npm — there is no dominant C++ registry — and a two-file library whose
whole point is being droppable does not need a package manager standing
between it and the project using it. Either is one manifest file if that ever
changes.

### Standalone

```bash
make check                       # everything: tests, sanitizers, fuzz, C++, size
make targets                     # cross-compile freestanding for embedded targets
cmake -S . -B build && ctest --test-dir build   # if you prefer CMake
```

## Lineage

The API descends from `GetNBitQty` / `PutNBitQty` in `dio_BitStream`
(M. A. Chatterjee, 2000), which packed "a 5-bit symbol, a 12-bit jump offset,
a 7-bit value into a continuous bit stream, then read them back as native
integers." That design has aged well and is kept. The implementation is new,
and deliberately does not reproduce the clamping, the unbounded positional
reads, or the inverted range check that the original carried.

## Licence

BSD-2-Clause.
