# Contributing to nbitq

How the repository is built and tested, the rules that are stricter than
you might expect, and how a release happens. Security issues go through
[SECURITY.md](SECURITY.md), not the issue tracker. The
[Code of Conduct](CODE_OF_CONDUCT.md) applies.

## What the library promises

Four things are fixed and do not change in a minor release. A pull request
that changes any of them is a format change, not a refactor, and must say so.

1. **Bit 0 is the most significant bit of byte 0**, and fields are written
   most-significant bit first.
2. **Every operation is all-or-nothing.** On failure nothing is written and no
   cursor moves.
3. **Bad input is refused, not approximated.** Out-of-range widths and
   positions, overlong varints, and values too wide for the destination are
   errors.
4. **Varints are canonical.** One value encodes exactly one way.

Two rules are about the repository rather than the bits:

- **No dependencies, no generated files.** `nbitq.h` + `nbitq.c` stays a
  copy-two-files install, and the core keeps building with
  `-DNBQ_NO_MALLOC -ffreestanding -nostdinc`. `make nomalloc` and
  `make targets` check this.
- **Numbers in the docs come from a command.** Byte counts, check counts and
  code sizes in `README.md`, `docs/` and `pages/` are measured. If your
  change moves one, re-run `make check` / `make targets` and update them in
  the same pull request.
- **Every line runs under the tests.** `make coverage` instruments the
  library, runs all four test binaries and fails below 100% of lines in
  `nbitq.c`, `nbitq_container.c`, `nbitq.h` and `nbitq.hpp`. CI runs it with
  gcc and gcov on Linux; locally it works with gcc and gcov, or clang and
  llvm-cov. A line that cannot run on a 64-bit host may be marked
  `LCOV_EXCL_LINE` with its reason; there are two, and the marker is not a
  way to skip a test that could be written. A new cursor function gets a
  forwarder in `nbitq.hpp` and a call in `test_nbitq_cxx.cpp`.

## Building and testing

You need a C99 compiler and `make`. `make targets` needs clang; CMake is
optional.

```bash
make check      # tests, ASan+UBSan, fuzz, C++ tests, NBQ_NO_MALLOC build, sizes
make coverage   # every line of the library executed by the tests, or fail
make targets    # freestanding cross-compile for Cortex-M, ARM, AArch64, x86-64
./release.sh    # everything CI runs, in one go
```

The build must produce **zero warnings** under `-Wall -Wextra -pedantic` on
gcc as well as clang; CI fails on any. gcc is stricter than Apple clang, so a
clean build on macOS is not proof.

The suite is exhaustive rather than representative: every width from 1 to
64, every varint base from 2 to 64, every start offset within a byte,
truncation at every bit position, in both bit orders. Bit order is checked
against a model in `test_nbitq.c` that expands a buffer to one character
per bit and reads members from the string, plus a mirror identity between
the orders and raw DEFLATE blocks from zlib. A new function is tested the
same way, in both orders.
The fuzz harness (`fuzz_nbitq.c`) checks properties, not just the absence of
crashes; a property callers rely on belongs in it. `make fuzz` runs it over
a fixed-seed PRNG anywhere, and `./release.sh libfuzzer` runs it under
libFuzzer where the toolchain has one.

Match the surrounding style: C99, four-space indent, comments that say why.

## Documentation

`README.md` is the short version. `docs/` holds the long-form Markdown and
is the source of record; `pages/` is the same material as a static site built
on [bitwrench](https://github.com/deftio/bitwrench), served by GitHub Pages
from the repository root (`index.html` redirects to `pages/`). The two are
kept in step by hand. The site has no build step; preview it with any static
server (`python3 -m http.server`). Styling lives in `pages/site.js` and pages
use bitwrench's components rather than CSS of their own.

## Pull requests

- One change per pull request.
- Add a line under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).
- `make check` passes locally.

## Releasing

CI is `release.sh`: the workflow runs its stages one per job. A release is a
push to `main` whose version has no tag yet; when every job is green the
`release` job creates the tag `vX.Y.Z` and the GitHub release, with notes
taken from the version's section of `CHANGELOG.md`.

To cut one:

```bash
./release.sh 0.2.0            # full suite, then the bump
./release.sh 0.2.0 --commit   # same, and commit it as "Release v0.2.0"
git push origin main
```

The bump sets the version in `nbitq.h` (`NBQ_VERSION_*`) and
`CMakeLists.txt`, turns `[Unreleased]` in the changelog into a dated section
(so write the notes first; an empty section is refused), and updates the
examples in the docs that quote the version. It refuses to run if the
version is already tagged. The library reports the version it was built
from at runtime through `nbq_version_string()` and `nbq_version()`, and the
test suite checks that those agree with the header.

Versioning is [semantic](https://semver.org/). Before 1.0, a minor bump may
change the API; a patch bump never does.
