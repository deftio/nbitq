#!/usr/bin/env bash
# update_sizes.sh -- rewrite the compiled-size tables in the docs from the
# CSVs that tools/sizes.sh produced.
#
#   tools/update_sizes.sh [csv...]          default: build-sizes/sizes.csv and,
#                                           if present, build-sizes/host/sizes.csv
#   tools/update_sizes.sh --check [csv...]  exit 1 if any table would change
#
# Each table sits between a pair of markers and is replaced whole:
#
#   README.md, docs/getting-started.md     <!-- crossbuild-sizes -->  (Markdown rows)
#   pages/index.html,
#   pages/getting-started.html             // <crossbuild-sizes> ... // </crossbuild-sizes>
#                                          (one JS object literal per row)
#
# Rows are sorted by word size, then by core size, with the rows that have no
# core (SDCC, which builds only nbitq32.c) after the ones that do. Targets
# whose toolchain was unavailable are left out, so a machine without the
# Espressif compiler does not delete the ESP32 rows and the Docker image does
# not delete the Apple clang row: a row is only ever replaced by a measured
# one. The prose around each table is not touched.
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0
if [ "${1:-}" = "--check" ]; then CHECK=1; shift; fi
if [ $# -eq 0 ]; then
    set -- build-sizes/sizes.csv
    [ -f build-sizes/host/sizes.csv ] && set -- "$@" build-sizes/host/sizes.csv
fi
for f in "$@"; do
    [ -f "$f" ] || { echo "update_sizes.sh: $f not found; run tools/crossbuild.sh or tools/sizes.sh first" >&2; exit 2; }
done

# cpu,bits,compiler,version,core,u32,nbitq32,warn  ->  cpu|bits|compiler version|core|u32|nbitq32
measured() {
    for f in "$@"; do tail -n +2 "$f"; done \
    | grep -v ',unavailable,' | grep -v ',FAIL,' \
    | awk -F, '{ printf "%s|%s|%s %s|%s|%s|%s\n", $1, $2, $3, $4, $5, $6, $7 }' || true
}
rows_measured=$(measured "$@")

# Rows already in README.md: "| cpu | bits | compiler | core | u32 | nbitq32 |"
existing_from_readme() {
    awk '/<!-- crossbuild-sizes -->/ { on = !on; next } on && /^\| [^-|]/ && !/^\| CPU/ { print }' README.md \
    | sed -E 's/^\| *//; s/ *\|$//; s/ *\| */|/g; s/,//g'
}

rows=$(
    {
        printf '%s\n' "$rows_measured"
        existing_from_readme | while IFS='|' read -r cpu bits comp core u32 small; do
            key="$cpu|$bits|$comp"
            printf '%s\n' "$rows_measured" | grep -qF "$key|" \
                || printf '%s|%s|%s|%s|%s|%s\n' "$cpu" "$bits" "$comp" "$core" "$u32" "$small"
        done
    } | sed '/^$/d' \
      | awk -F'|' '{ k = ($4 ~ /^[0-9]+$/) ? $4 : 999999999; print k "|" $0 }' \
      | sort -t'|' -k3,3n -k1,1n \
      | cut -d'|' -f2-
)
[ -n "$rows" ] || { echo "update_sizes.sh: no rows" >&2; exit 2; }

thousands() {
    # 12345 -> 12,345; anything that is not all digits ("?", "n/a") passes through.
    awk -v n="$1" 'BEGIN {
        if (n !~ /^[0-9]+$/) { print n; exit }
        s = ""; while (length(n) > 3) { s = "," substr(n, length(n) - 2) s; n = substr(n, 1, length(n) - 3) }
        print n s }'
}

md="| CPU | bits | compiler | core | u32 cursor only | nbitq32 |
|---|---:|---|---:|---:|---:|"
js=""
while IFS='|' read -r cpu bits comp core u32 small; do
    md="$md
| $cpu | $bits | $comp | $(thousands "$core") | $(thousands "$u32") | $(thousands "$small") |"
    js="$js
            { cpu: '$cpu', bits: '$bits', compiler: '$comp', core: '$(thousands "$core")', u32: '$(thousands "$u32")', nbitq32: '$(thousands "$small")' },"
done <<< "$rows"
js=${js#$'\n'}  # no leading newline
js=${js%,}        # no trailing comma on the last row

# replace <file> <open-marker> <close-marker> <text>
replace() {
    local file=$1 open=$2 close=$3 text=$4 tmp body
    tmp=$(mktemp); body=$(mktemp)
    printf '%s\n' "$text" > "$body"
    awk -v open="$open" -v cl="$close" -v body="$body" '
        index($0, open) && !on { print; while ((getline line < body) > 0) print line; close(body); on = 1; next }
        index($0, cl) && on { on = 0 }
        !on { print }
    ' "$file" > "$tmp"
    rm -f "$body"
    if cmp -s "$file" "$tmp"; then
        echo "  unchanged  $file"
    elif [ "$CHECK" = 1 ]; then
        echo "  STALE      $file"; STALE=1
    else
        mv "$tmp" "$file"; echo "  updated    $file"
    fi
    rm -f "$tmp"
}

STALE=0
echo "size tables from $*:"
# Markdown: the same marker opens and closes.
replace README.md               '<!-- crossbuild-sizes -->' '<!-- crossbuild-sizes -->' "$md"
replace docs/getting-started.md '<!-- crossbuild-sizes -->' '<!-- crossbuild-sizes -->' "$md"
replace pages/index.html           '// <crossbuild-sizes>' '// </crossbuild-sizes>' "$js"
replace pages/getting-started.html '// <crossbuild-sizes>' '// </crossbuild-sizes>' "$js"
[ "$STALE" = 0 ] || { echo "update_sizes.sh: the tables above are stale; run it without --check" >&2; exit 1; }
