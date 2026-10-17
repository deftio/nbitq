#!/usr/bin/env bash
# release.sh -- everything CI runs, and the version bump that cuts a release.
#
#   ./release.sh                 run the whole suite locally and report
#   ./release.sh 0.2.0           run the whole suite; if it is green, set the
#                                version to 0.2.0 everywhere it is written and
#                                re-run the tests against the new header
#   ./release.sh 0.2.0 --commit  ...and commit the bump as "Release v0.2.0"
#   ./release.sh <stage>         one stage only: check | cmake | coverage |
#                                targets | libfuzzer | version
#
# .github/workflows/ci.yml runs the stages one per job, so this file is the
# definition of "CI is green". The release itself is a push to main: the CI
# release job reads the version (./release.sh version), and if vX.Y.Z has no
# tag yet it creates the tag and the GitHub release from CHANGELOG.md.
#
# Needs: a C99 compiler and make. clang for `targets` (and for `libfuzzer`,
# which Apple's clang cannot run; that stage is skipped outside CI when the
# toolchain lacks it). cmake for `cmake`. gcov or llvm-cov for `coverage`.
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

set -euo pipefail
cd "$(dirname "$0")"

FUZZ_SECONDS=${FUZZ_SECONDS:-60}
BUILD_DIR=build-release
LOG=$(mktemp -d)
trap 'rm -rf "$LOG"' EXIT

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
die()  { printf 'release.sh: %s\n' "$*" >&2; exit 1; }

no_warnings() {
    # CFLAGS carries -Wall -Wextra -pedantic but not -Werror, so the build
    # stays usable; a compiler warning is still a failure here. The pattern
    # wants a source location (file:line:col:) so that a linker complaining
    # about the machine's own LDFLAGS does not fail the library's build.
    if grep -E '^[^ ]+:[0-9]+(:[0-9]+)?: warning:' "$1"; then
        die "build produced warnings (above)"
    fi
}

# ── Stages ──────────────────────────────────────────────────────────────────

stage_check() {
    say "make check (${CC:-cc})"
    make check 2>&1 | tee "$LOG/check.log"
    no_warnings "$LOG/check.log"
}

stage_cmake() {
    say "cmake (Release) + ctest"
    command -v cmake >/dev/null || die "cmake not found"
    rm -rf "$BUILD_DIR"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" 2>&1 | tee "$LOG/cmake.log"
    no_warnings "$LOG/cmake.log"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
}

stage_coverage() {
    say "coverage: every line of nbitq.c, nbitq_container.c, nbitq.h and nbitq.hpp"
    if ! make -s coverage-tool >"$LOG/covtool.log" 2>&1; then
        if [ -n "${CI:-}" ]; then
            cat "$LOG/covtool.log"; die "no gcov tool for ${CC:-cc}"
        fi
        echo "  SKIP: $(cat "$LOG/covtool.log")"
        SKIPPED+=("coverage")
        return 0
    fi
    make coverage 2>&1 | tee "$LOG/coverage.log"
    no_warnings "$LOG/coverage.log"
}

stage_targets() {
    say "freestanding cross-compiles"
    command -v clang >/dev/null || die "clang not found; 'make targets' needs it"
    make targets 2>&1 | tee "$LOG/targets.log"
    if grep -q 'unsupported' "$LOG/targets.log"; then
        die "a cross target did not build (above)"
    fi
}

stage_libfuzzer() {
    say "libFuzzer (${FUZZ_SECONDS}s)"
    if ! printf 'int LLVMFuzzerTestOneInput(const unsigned char*d,unsigned long n){(void)d;(void)n;return 0;}' \
         | clang -fsanitize=fuzzer -x c - -o "$LOG/probe" >/dev/null 2>&1; then
        if [ -n "${CI:-}" ]; then
            die "this clang has no libFuzzer runtime"
        fi
        echo "  SKIP: this clang has no libFuzzer runtime (Apple clang does not ship it)."
        echo "        make check already ran the same harness over a fixed-seed PRNG;"
        echo "        CI runs the real engine on Linux."
        SKIPPED+=("libfuzzer")
        return 0
    fi
    clang -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
          -O1 -g -std=c99 -Wall -Wextra \
          -o "$LOG/nbitq_libfuzzer" fuzz_nbitq.c nbitq.c
    "$LOG/nbitq_libfuzzer" -max_total_time="$FUZZ_SECONDS" -max_len=320
}

# Prints the version on stdout after checking that nbitq.h agrees with
# itself, with CMakeLists.txt, and that CHANGELOG.md has a dated section for
# it. Everything else goes to stderr so `V=$(./release.sh version)` works.
stage_version() {
    local h maj min pat c
    h=$(sed -nE 's/^#define NBQ_VERSION_STRING "([^"]+)"/\1/p' nbitq.h)
    maj=$(sed -nE 's/^#define NBQ_VERSION_MAJOR ([0-9]+)/\1/p' nbitq.h)
    min=$(sed -nE 's/^#define NBQ_VERSION_MINOR ([0-9]+)/\1/p' nbitq.h)
    pat=$(sed -nE 's/^#define NBQ_VERSION_PATCH ([0-9]+)/\1/p' nbitq.h)
    c=$(sed -nE 's/^[[:space:]]*project\(nbitq VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
    [ -n "$h" ] || die "no NBQ_VERSION_STRING in nbitq.h"
    [ "$h" = "$maj.$min.$pat" ] || die "NBQ_VERSION_STRING ($h) disagrees with MAJOR.MINOR.PATCH ($maj.$min.$pat)"
    [ "$h" = "$c" ] || die "nbitq.h says $h but CMakeLists.txt says $c"
    grep -qE "^## \[$h\] - [0-9]{4}-[0-9]{2}-[0-9]{2}" CHANGELOG.md \
        || die "CHANGELOG.md has no '## [$h] - YYYY-MM-DD' section"
    echo "version $h: nbitq.h, CMakeLists.txt and CHANGELOG.md agree" >&2
    echo "$h"
}

# ── Version bump ────────────────────────────────────────────────────────────

# Every file that spells the version out. Examples in the docs pin the
# FetchContent tag and quote NBQ_VERSION_STRING, and they must not go stale.
VERSION_FILES=(nbitq.h CMakeLists.txt CHANGELOG.md
               README.md docs/getting-started.md pages/getting-started.html
               docs/api-reference.md pages/api.html pages/site.js)

# Everything that can refuse a bump is checked before the suite runs, so a
# missing changelog entry is reported in a second rather than after it.
precheck() {
    local new=$1 old
    [[ "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must be MAJOR.MINOR.PATCH, got '$new'"
    old=$(sed -nE 's/^#define NBQ_VERSION_STRING "([^"]+)"/\1/p' nbitq.h)
    [ "$new" != "$old" ] || die "already at $old"
    if git rev-parse -q --verify "refs/tags/v$new" >/dev/null 2>&1; then
        die "tag v$new already exists"
    fi
    # The release notes are the [Unreleased] section, so it must say something.
    if ! awk '/^## \[Unreleased\]/{on=1;next} /^## \[/{on=0} on && NF{found=1} END{exit !found}' CHANGELOG.md; then
        die "CHANGELOG.md's [Unreleased] section is empty; write the notes first"
    fi
    for f in "${VERSION_FILES[@]}"; do [ -f "$f" ] || die "missing $f"; done
    if [ "$COMMIT" = 1 ] && ! git diff --quiet -- "${VERSION_FILES[@]}"; then
        die "--commit would sweep up unrelated edits; these files have uncommitted changes:
$(git diff --name-only -- "${VERSION_FILES[@]}")"
    fi
}

bump() {
    local new=$1 old today
    old=$(sed -nE 's/^#define NBQ_VERSION_STRING "([^"]+)"/\1/p' nbitq.h)

    say "version $old -> $new"
    today=$(date +%Y-%m-%d)
    IFS=. read -r maj min pat <<<"$new"
    perl -pi -e "s/^#define NBQ_VERSION_MAJOR \d+\$/#define NBQ_VERSION_MAJOR $maj/;
                 s/^#define NBQ_VERSION_MINOR \d+\$/#define NBQ_VERSION_MINOR $min/;
                 s/^#define NBQ_VERSION_PATCH \d+\$/#define NBQ_VERSION_PATCH $pat/;
                 s/^#define NBQ_VERSION_STRING \"\Q$old\E\"/#define NBQ_VERSION_STRING \"$new\"/" nbitq.h
    perl -pi -e "s/^(\s*project\(nbitq VERSION )\Q$old\E\b/\${1}$new/" CMakeLists.txt
    perl -pi -e "s/^## \[Unreleased\]\$/## [Unreleased]\n\n## [$new] - $today/;
                 s{^\[Unreleased\]: (.*/compare/)v\Q$old\E\.\.\.HEAD\$}{[Unreleased]: \${1}v$new...HEAD\n[$new]: https://github.com/deftio/nbitq/releases/tag/v$new}" CHANGELOG.md
    perl -pi -e "s/\bGIT_TAG(\s+)v\Q$old\E\b/GIT_TAG\${1}v$new/" \
        README.md docs/getting-started.md pages/getting-started.html
    perl -pi -e "s/(NBQ_VERSION_STRING\s+)\"\Q$old\E\"/\${1}\"$new\"/" \
        docs/api-reference.md pages/api.html
    perl -pi -e "s/^(\s*var VERSION = \x27)\Q$old\E\x27/\${1}$new\x27/" pages/site.js

    # A stale spelling in any tracked file is a bug in VERSION_FILES, so look.
    # CHANGELOG.md keeps every old version on purpose.
    local re stale
    re="(^|[^0-9.])v?${old//./\\.}([^0-9.]|\$)"
    stale=$(git ls-files | grep -vxF CHANGELOG.md | xargs grep -nIE "$re" || true)
    if [ -n "$stale" ]; then
        printf '%s\n' "$stale"
        die "the old version is still spelled somewhere (above); add the file to VERSION_FILES"
    fi

    say "tests against the bumped header"
    make test 2>&1 | tail -1
    stage_version >/dev/null

    git --no-pager diff --stat -- "${VERSION_FILES[@]}"
    if [ "$COMMIT" = 1 ]; then
        git add -- "${VERSION_FILES[@]}"
        git commit -q -m "Release v$new" -- "${VERSION_FILES[@]}"
        echo "committed: $(git log -1 --oneline)"
        echo
        echo "Next:  git push origin main"
        echo "CI tags v$new and publishes the GitHub release when it is green."
    else
        echo
        echo "Next:  git commit -am 'Release v$new' && git push origin main"
        echo "CI tags v$new and publishes the GitHub release when it is green."
    fi
}

# ── Main ────────────────────────────────────────────────────────────────────

COMMIT=0
NEW=
STAGES=()
SKIPPED=()
for a in "$@"; do
    case "$a" in
        check|cmake|coverage|targets|libfuzzer|version) STAGES+=("$a") ;;
        --commit) COMMIT=1 ;;
        --fuzz-seconds=*) FUZZ_SECONDS=${a#*=} ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        [0-9]*) NEW=$a ;;
        *) die "unknown argument '$a' (try --help)" ;;
    esac
done
[ "$COMMIT" = 0 ] || [ -n "$NEW" ] || die "--commit needs a version"

if [ ${#STAGES[@]} -gt 0 ]; then
    [ -z "$NEW" ] || die "give either a stage or a version, not both"
    for s in "${STAGES[@]}"; do "stage_$s"; done
    exit 0
fi

stage_version >/dev/null
[ -z "$NEW" ] || precheck "$NEW"
stage_check
stage_cmake
stage_coverage
stage_targets
stage_libfuzzer

say "all stages green${SKIPPED[*]:+ (skipped: ${SKIPPED[*]})}"
if [ -n "$NEW" ]; then
    bump "$NEW"
fi
