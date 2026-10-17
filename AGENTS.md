# AGENTS.md

Guidance for coding agents (and people) working in or on this repository.
It says why nbitq exists, how to use it from another project, and how to
change it without breaking the promises it makes.

## Why this repo exists

nbitq packs and unpacks n-bit integers in a bit stream. A value of any
width from 1 to 64 bits goes into a byte buffer back to back with the
previous one or at an explicit bit position, and comes back out as a native
integer. That is what a compressed format, a wire protocol or a succinct
data structure needs and what C does not provide: struct bit-fields have
implementation-defined layout and `memcpy` moves bytes. C99, two files, no
dependencies. The same operations exist in two forms:

- **Indexed**: `nbq_get_u32(buf, bit_len, bit_pos, n, &out)` and
  `nbq_put_u32(buf, bit_cap, bit_pos, n, value)`. Explicit position, no
  state. For fields at known offsets.
- **Cursor**: `nbq_read_u32(&reader, n, &out)` and
  `nbq_write_u32(&writer, n, value)`. A `nbq_reader` / `nbq_writer`
  holds the position and advances it. For fields that follow one another.

Both forms share one implementation, so they cannot disagree. Most bit
libraries offer only one of the two: a bit-stream reader, or a bitset
indexed by position. Rust's bitvec is the usual exception. Having both
over one buffer in C, with a fixed bit numbering, is the reason this
library exists.

nbitq is vendored by sister projects that need exactly this primitive:

- **triepack**, which copies `nbitq.h` and `nbitq.c` into its tree.
- **hutter**, moving to the same arrangement.

Those projects depend on the fixed conventions below. Do not change them.

## Fixed conventions

- **Bit 0 is the most significant bit of byte 0.** Fields are written
  MSB first. This matches network order and most codec bit readers.
- Signed fields are two's complement in `n` bits, sign-extended on read.
- Varints use `base` bits per group (2 to 64): `base - 1` payload bits
  plus one continuation bit, least-significant group first. **Base 8 is
  LEB128 byte for byte.** Encodings are canonical; an overlong varint is
  `NBQ_ERR_MALFORMED`. Signed varints use zigzag.
- Floats are stored as their IEEE-754 bit pattern, big-endian, via
  `nbq_put_f32` / `nbq_put_f64` and the cursor forms. The library does no
  floating-point arithmetic. NaN payloads, signed zero and denormals
  survive a round trip unchanged.
- Every function returns `nbq_result`: `NBQ_OK` is 0, errors are
  negative (`NBQ_ERR_PARAM`, `NBQ_ERR_RANGE`, `NBQ_ERR_OVERFLOW`,
  `NBQ_ERR_MALFORMED`, `NBQ_ERR_ALLOC`). `nbq_strerror` names them.
- Every operation is all-or-nothing. A failed read or write leaves the
  buffer and the cursor position unchanged.
- Bounds checks never compute `pos + n`, so a position from untrusted
  input cannot wrap.
- The library self-reports its version: `NBQ_VERSION_STRING`,
  `NBQ_VERSION` (packed `0x00MMmmpp`), `nbq_version_string()`,
  `nbq_version()`.

## How to use nbitq from another project

Vendoring is the intended route. Copy two files:

```
nbitq.h
nbitq.c
```

Compile `nbitq.c` with `-std=c99`. The header is `extern "C"`, so it
compiles as C++ too. Nothing else is needed.

From C++, `nbitq.hpp` (optional, header-only, C++11) wraps the cursor API
as `nbq::Reader`, `nbq::Writer` and `nbq::OwningWriter`. Each method is one
call into the C library; it adds ownership and nothing else.

Optional pieces:

- `nbitq_container.h` / `nbitq_container.c`: an in-memory `.nbq` wrapper
  (magic, type string, bit length, payload, CRC-64/XZ). It has no stdio
  and is a separate translation unit; leave it out if not needed.
- CMake: `add_subdirectory(nbitq)` then
  `target_link_libraries(t PRIVATE nbitq)`. Set `NBITQ_BUILD_TESTS=OFF`
  when vendored. `FetchContent` with `GIT_TAG vX.Y.Z` also works.
- Freestanding: define `NBQ_NO_MALLOC`. The growing writer disappears
  and the core includes no hosted header. It builds with
  `-ffreestanding -nostdinc`. Core is about 4.8 KB of `.text` at `-Oz`
  on Cortex-M.
- 32-bit targets: use the `u32` functions and `nbq_get_parts` /
  `nbq_put_parts` (a `{hi, lo}` pair) to avoid 64-bit shifts.

Things nbitq does not do, on purpose: insert or delete bits (use
`nbq_copy_bits` to shift the tail; the recipe is in
`docs/api-reference.md` under "Not provided"), file I/O, endianness
conversion of whole words, a word-at-a-time fast path. It is
bit-at-a-time and roughly 3 to 5 times slower than a codec's reader.

Minimal example:

```c
#include "nbitq.h"

uint8_t buf[4] = {0};
nbq_writer w;
nbq_writer_init(&w, buf, sizeof(buf) * 8);
nbq_write_u32(&w, 5,  19);
nbq_write_u32(&w, 12, 3000);
nbq_write_u32(&w, 7,  0x55);         /* buf = 9D DC 55, w.pos = 24 */

uint32_t offset;
nbq_get_u32(buf, 24, 5, 12, &offset); /* offset == 3000 */
```

## Repository map

| Path | What it is |
|---|---|
| `nbitq.h`, `nbitq.c` | the library |
| `nbitq.hpp` | header-only C++ wrapper over the cursor API; forwards, adds nothing |
| `nbitq_container.h`, `nbitq_container.c` | optional `.nbq` container and CRC-64 |
| `test_nbitq.c`, `test_nbitq_container.c` | exhaustive tests (every width, base, offset) |
| `test_nbitq_cxx.cpp` | the headers and `nbitq.hpp` from C++; calls every wrapper method |
| `fuzz_nbitq.c` | libFuzzer harness; also runs standalone with a fixed-seed PRNG |
| `Makefile`, `CMakeLists.txt` | two independent builds; both must stay green |
| `release.sh` | the whole CI suite as one script, plus the version bump |
| `.github/workflows/ci.yml` | runs `release.sh` stages; tags and releases on green `main` |
| `docs/*.md` | long-form documentation; the source of record |
| `pages/*.html`, `pages/site.js` | the static site (bitwrench); mirrors `docs/` |
| `CHANGELOG.md` | Keep a Changelog format; the release notes come from it |

## Building and testing

```sh
make check        # tests, ASan+UBSan, fuzz, C++ tests, NBQ_NO_MALLOC, sizes
make coverage     # every line of the library executed by the tests, or fail
make targets      # six freestanding cross-compiles (needs clang)
./release.sh      # everything CI runs, in order; exits non-zero on any failure
```

Or with CMake:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Expectations for a change:

- Zero warnings under `-Wall -Wextra -pedantic` with gcc and clang.
- `make check` and the CMake build both pass.
- `NBQ_NO_MALLOC` still builds freestanding, and nothing in the core
  includes a hosted header.
- New behaviour gets a test in `test_nbitq.c`. The suite counts checks;
  the count goes up, never down.
- `make coverage` stays at 100% of lines for `nbitq.c`, `nbitq_container.c`,
  `nbitq.h` and `nbitq.hpp`. CI runs it with gcc and gcov. A line that
  cannot run on a 64-bit host is marked `LCOV_EXCL_LINE` with its reason;
  there are two. That marker is not a way to skip a test that could be
  written.
- A new cursor function in `nbitq.h` gets a one-line forwarder in
  `nbitq.hpp` and a call in `test_nbitq_cxx.cpp`.
- Any number quoted in docs (bytes, sizes, check counts) was produced by
  running a command, not estimated. Re-run and update it if it changes.
- `docs/` and `pages/` say the same thing in the same plain register.
  Edit `docs/` first, then mirror. Styling lives only in `pages/site.js`.

## Releasing

`./release.sh X.Y.Z` runs the full suite, then bumps the version in
`nbitq.h`, `CMakeLists.txt`, `CHANGELOG.md` and the docs, and re-runs the
tests. It refuses if `## [Unreleased]` in the changelog is empty or the
tag already exists. CI tags `vX.Y.Z` and publishes the GitHub release
from the changelog section once `main` is green.

Do not commit, tag or push on the maintainer's behalf. Leave changes in
the working tree and say what changed. `release.sh --commit` exists for
the maintainer's use.

## Where to read more

- `README.md`: overview, quickstart, API summary, sizes.
- `docs/api-reference.md`: every function, with examples.
- `docs/how-it-works.md`: the bit-level layout, with byte values.
- `docs/getting-started.md`: install, cursor vs indexed, result codes.
- `llms.txt`: the same map in the llms.txt format.
