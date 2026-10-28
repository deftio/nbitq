#!/usr/bin/env bash
# crossbuild.sh -- compile nbitq for every target in tools/sizes.sh inside the
# Docker image from tools/Dockerfile.crossbuild, and leave the measurements in
# build-sizes/sizes.csv. On an arm64 Mac it then runs tools/sizes.sh once
# more on the host, for the Apple clang row, into build-sizes/host/sizes.csv.
#
#   tools/crossbuild.sh            build the image if it is missing, then run
#   tools/crossbuild.sh --build    rebuild the image
#   tools/crossbuild.sh --run      run only; the image must exist
#
# Then: tools/update_sizes.sh      patch the size tables in the docs from the CSVs
#
# The script that does the work, tools/sizes.sh, needs only the toolchains on
# PATH; CI runs it directly on a runner with the packages installed, and this
# wrapper exists so the same table comes out on a laptop.
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=nbitq-crossbuild

build_image() {
    echo "building Docker image $IMAGE (linux/amd64)"
    docker build --platform linux/amd64 -t "$IMAGE" -f tools/Dockerfile.crossbuild .
}

run_image() {
    mkdir -p build-sizes
    # The repository is mounted read-only; only build-sizes/ is written.
    docker run --rm --platform linux/amd64 \
        -v "$PWD/nbitq.c:/nbitq/nbitq.c:ro" \
        -v "$PWD/nbitq.h:/nbitq/nbitq.h:ro" \
        -v "$PWD/nbitq32.c:/nbitq/nbitq32.c:ro" \
        -v "$PWD/nbitq32.h:/nbitq/nbitq32.h:ro" \
        -v "$PWD/tools/sizes.sh:/nbitq/tools/sizes.sh:ro" \
        -v "$PWD/build-sizes:/nbitq/build-sizes" \
        "$IMAGE"
    echo
    echo "written: build-sizes/sizes.csv and build-sizes/sizes.log"
    if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
        echo
        echo "host (Apple clang):"
        bash tools/sizes.sh build-sizes/host | grep -v unavailable
    fi
    echo "next:    tools/update_sizes.sh   (patches the tables in README.md, docs/ and pages/)"
}

command -v docker >/dev/null || { echo "crossbuild.sh: docker not found" >&2; exit 2; }

case "${1:-}" in
    --build) build_image ;;
    --run)   run_image ;;
    "")      docker image inspect "$IMAGE" >/dev/null 2>&1 || build_image; run_image ;;
    *)       echo "crossbuild.sh: unknown argument '$1'" >&2; exit 2 ;;
esac
