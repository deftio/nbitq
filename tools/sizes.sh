#!/usr/bin/env bash
# sizes.sh -- compile nbitq for every toolchain on PATH and record the .text
# size of each build in a CSV, for the tables in README.md.
#
#   tools/sizes.sh [outdir]        default outdir: build-sizes
#
# Three numbers per target, all bytes of code, all -Os with NBQ_NO_MALLOC
# and -ffreestanding, which is the configuration an embedded build uses:
#
#   core      nbitq.c as one object: the whole library.
#   u32       what a program that calls only nbq_reader_init, nbq_writer_init,
#             nbq_read_u32 and nbq_write_u32 links in, measured by compiling
#             with -ffunction-sections and asking the target's linker to
#             garbage-collect from those four roots (ld -r --gc-sections; on
#             macOS a dead-stripped static link, since ld64 has no -r with
#             -dead_strip).
#   nbitq32   nbitq32.c as one object: the u32 pair with 32-bit positions,
#             both orders. The number for a target that links only that file.
#
# gcc and clang targets are driven by `target`. SDCC targets (Z80, 8051,
# STM8) are driven by `target_sdcc`: nbitq.c needs a 64-bit type and passes
# a struct by value, which SDCC does not do, so only nbitq32.c is measured
# there and the other two columns say n/a. The host Apple clang row runs
# only on an arm64 Mac.
#
# A toolchain that is not installed gives an "unavailable" row and is not a
# failure. A compile error or any warning is: the library promises to build
# warning-free on every toolchain here. The CSV has one row per target with
# the compiler's version, so the table in the docs can say what it was
# measured with. tools/update_sizes.sh patches the docs.
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

set -uo pipefail
cd "$(dirname "$0")/.."

OUT=${1:-build-sizes}
mkdir -p "$OUT"
CSV=$OUT/sizes.csv
LOG=$OUT/sizes.log
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

FLAGS="-Os -std=c99 -DNBQ_NO_MALLOC -ffreestanding -Wall -Wextra"
ROOTS="nbq_reader_init nbq_writer_init nbq_read_u32 nbq_write_u32"

echo "cpu,bits,compiler,version,core,u32,nbitq32,warnings" > "$CSV"
{ echo "nbitq sizes.sh: $(date -u '+%Y-%m-%d %H:%M UTC')"; echo "flags: $FLAGS"; echo; } > "$LOG"
FAILED=0

# text_size <size-tool> <object>: the .text column of Berkeley `size`.
text_size() {
    "$1" "$2" 2>/dev/null | awk 'NR == 2 { print $1 }'
}

unavailable() { # <cpu> <bits> <label>
    printf '  %-28s %-26s %s\n' "$1" "$3" "unavailable"
    echo "$1,$2,$3,,unavailable,unavailable,unavailable,0" >> "$CSV"
}

record() { # <cpu> <bits> <label> <version> <core> <u32> <nbitq32> <warnings>
    printf '  %-28s %-26s core %6s   u32 %6s   nbitq32 %5s   warnings %s\n' "$1" "$3 $4" "$5" "$6" "$7" "$8"
    echo "$1,$2,$3,$4,$5,$6,$7,$8" >> "$CSV"
    if [ "$8" != "0" ]; then
        echo "   WARNINGS: $8 (above)" >> "$LOG"
        FAILED=1
    fi
}

compiler_version() { # <cc>
    local v
    v=$("$1" -dumpfullversion -dumpversion 2>/dev/null | head -1)
    [ -n "$v" ] || v=$("$1" --version 2>/dev/null | head -1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
    echo "$v"
}

# target <cpu> <bits> <label> <size-tool> <ld> <compiler> [flags...]
#   <ld> may carry an emulation ("ld -m elf_i386") when the binutils default
#   is a different word size from the target's.
target() {
    local cpu=$1 bits=$2 label=$3 sizetool=$4 ld=$5 cc=$6
    shift 6
    local version core u32 small warn roots r

    # The plain gcc and clang rows are the distribution's x86 compilers on
    # Linux; on a Mac those names are Apple clang, which has its own row.
    if ! command -v "$cc" >/dev/null 2>&1 \
       || { [ "$(uname -s 2>/dev/null)" = "Darwin" ] && case "$cc" in gcc|clang) true ;; *) false ;; esac; }; then
        unavailable "$cpu" "$bits" "$label"
        return
    fi
    version=$(compiler_version "$cc")

    {
        echo "== $cpu ($label $version)"
        echo "   $cc $* $FLAGS -c nbitq.c / nbitq32.c"
    } >> "$LOG"

    if ! "$cc" "$@" $FLAGS -c nbitq.c -o "$TMP/core.o" >"$TMP/err" 2>&1 \
       || ! "$cc" "$@" $FLAGS -c nbitq32.c -o "$TMP/small.o" >>"$TMP/err" 2>&1; then
        cat "$TMP/err" >> "$LOG"
        printf '  %-28s %-26s %s\n' "$cpu" "$label $version" "COMPILE FAILED (see $LOG)"
        echo "$cpu,$bits,$label,$version,FAIL,FAIL,FAIL,0" >> "$CSV"
        FAILED=1
        return
    fi
    warn=$(grep -c 'warning:' "$TMP/err" || true)
    cat "$TMP/err" >> "$LOG"
    core=$(text_size "$sizetool" "$TMP/core.o")
    small=$(text_size "$sizetool" "$TMP/small.o")

    # The u32 subset: function sections, then a partial link that keeps only
    # what the four roots reach.
    u32=""
    roots=""; for r in $ROOTS; do roots="$roots -u $r"; done
    if "$cc" "$@" $FLAGS -ffunction-sections -fdata-sections -c nbitq.c -o "$TMP/fs.o" >>"$LOG" 2>&1 \
       && $ld -r --gc-sections $roots "$TMP/fs.o" -o "$TMP/u32.o" >>"$LOG" 2>&1; then
        u32=$(text_size "$sizetool" "$TMP/u32.o")
    fi
    [ -n "$u32" ] || u32="?"

    record "$cpu" "$bits" "$label" "$version" "$core" "$u32" "$small" "$warn"
}

# target_mac <cpu> <bits> <label>: the host Apple clang on an arm64 Mac. Mach-O
# has no Berkeley size and ld64 has no -r with -dead_strip, so the .text of
# the object comes from `size -m` and the u32 subset from a static link with
# the four roots kept and everything else stripped.
target_mac() {
    local cpu=$1 bits=$2 label=$3
    local version core u32 small warn roots r

    if [ "$(uname -s 2>/dev/null)" != "Darwin" ] || [ "$(uname -m 2>/dev/null)" != "arm64" ] \
       || ! clang --version 2>/dev/null | grep -q '^Apple clang'; then
        unavailable "$cpu" "$bits" "$label"
        return
    fi
    version=$(clang --version | head -1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
    {
        echo "== $cpu ($label $version)"
        echo "   clang $FLAGS -c nbitq.c / nbitq32.c"
    } >> "$LOG"
    if ! clang $FLAGS -c nbitq.c -o "$TMP/core.o" >"$TMP/err" 2>&1 \
       || ! clang $FLAGS -c nbitq32.c -o "$TMP/small.o" >>"$TMP/err" 2>&1; then
        cat "$TMP/err" >> "$LOG"
        printf '  %-28s %-26s %s\n' "$cpu" "$label $version" "COMPILE FAILED (see $LOG)"
        echo "$cpu,$bits,$label,$version,FAIL,FAIL,FAIL,0" >> "$CSV"
        FAILED=1
        return
    fi
    warn=$(grep -c 'warning:' "$TMP/err" || true)
    cat "$TMP/err" >> "$LOG"
    core=$(size -m "$TMP/core.o" | awk -F': ' '/Section \(__TEXT, __text\)/ { print $2 }')
    small=$(size -m "$TMP/small.o" | awk -F': ' '/Section \(__TEXT, __text\)/ { print $2 }')
    roots=""; for r in $ROOTS; do roots="$roots -Wl,-u,_$r"; done
    u32=""
    if clang $FLAGS -nostdlib -static -Wl,-dead_strip -Wl,-e,_nbq_read_u32 $roots \
             -o "$TMP/u32.exe" "$TMP/core.o" >>"$LOG" 2>&1; then
        u32=$(size -m "$TMP/u32.exe" | awk -F': ' '/Section __text/ { print $2 }')
    fi
    [ -n "$u32" ] || u32="?"
    record "$cpu" "$bits" "$label" "$version" "$core" "$u32" "$small" "$warn"
}

# target_sdcc <cpu> <bits> <port>: nbitq32.c alone, with SDCC. The size is the
# code area of the .rel object (asxxxx format: "A <area> size <hex>").
target_sdcc() {
    local cpu=$1 bits=$2 port=$3
    local label="sdcc -m$port" version warn small

    if ! command -v sdcc >/dev/null 2>&1; then
        unavailable "$cpu" "$bits" "$label"
        return
    fi
    version=$(sdcc --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    {
        echo "== $cpu ($label $version)"
        echo "   sdcc -m$port --std-c99 --opt-code-size -c nbitq32.c"
    } >> "$LOG"
    if ! (cd "$TMP" && sdcc "-m$port" --std-c99 --opt-code-size -c "$OLDPWD/nbitq32.c" -o "$TMP/small.rel") >"$TMP/err" 2>&1; then
        cat "$TMP/err" >> "$LOG"
        printf '  %-28s %-26s %s\n' "$cpu" "$label $version" "COMPILE FAILED (see $LOG)"
        echo "$cpu,$bits,$label,$version,n/a,n/a,FAIL,0" >> "$CSV"
        FAILED=1
        return
    fi
    warn=$(grep -ci 'warning' "$TMP/err" || true)
    cat "$TMP/err" >> "$LOG"
    small=$(awk '$1 == "A" && ($2 == "_CODE" || $2 == "CODE" || $2 == "CSEG") && $3 == "size" {
                     s += strtonum("0x" $4) } END { print s + 0 }' "$TMP/small.rel" 2>/dev/null)
    # mawk and BSD awk have no strtonum; fall back to the shell.
    if [ -z "$small" ] || [ "$small" = "0" ]; then
        small=0
        while read -r hex; do small=$((small + 0x$hex)); done < <(
            awk '$1 == "A" && ($2 == "_CODE" || $2 == "CODE" || $2 == "CSEG") && $3 == "size" { print $4 }' "$TMP/small.rel")
    fi
    record "$cpu" "$bits" "$label" "$version" "n/a" "n/a" "$small" "$warn"
}

echo "nbitq core and nbitq32, bytes of code, $FLAGS"
echo

echo "8-bit"
target "AVR ATmega328P" 8  avr-gcc        avr-size    avr-ld    avr-gcc -mmcu=atmega328p
target "AVR ATtiny85"   8  avr-gcc        avr-size    avr-ld    avr-gcc -mmcu=attiny85
target_sdcc "Z80"       8  z80
target_sdcc "8051"      8  mcs51
target_sdcc "STM8"      8  stm8

echo "16-bit"
target "MSP430 G2553"   16 msp430-gcc     msp430-size msp430-ld msp430-gcc -mmcu=msp430g2553

echo "32-bit"
target "Cortex-M0 (Thumb-1)"     32 arm-none-eabi-gcc       arm-none-eabi-size       arm-none-eabi-ld       arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb
target "Cortex-M4 (Thumb-2)"     32 arm-none-eabi-gcc       arm-none-eabi-size       arm-none-eabi-ld       arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb
target "ARM v7-A (A32)"          32 arm-none-eabi-gcc       arm-none-eabi-size       arm-none-eabi-ld       arm-none-eabi-gcc -mcpu=cortex-a8 -marm
target "RISC-V rv32imac"         32 riscv64-unknown-elf-gcc riscv64-unknown-elf-size "riscv64-unknown-elf-ld -m elf32lriscv" riscv64-unknown-elf-gcc -march=rv32imac -mabi=ilp32
target "Xtensa LX106 (ESP8266)"  32 xtensa-lx106-elf-gcc    xtensa-lx106-elf-size    xtensa-lx106-elf-ld    xtensa-lx106-elf-gcc
target "Xtensa LX6 (ESP32)"      32 xtensa-esp32-elf-gcc    xtensa-esp32-elf-size    xtensa-esp32-elf-ld    xtensa-esp32-elf-gcc
target "Xtensa LX7 (ESP32-S3)"   32 xtensa-esp32s3-elf-gcc  xtensa-esp32s3-elf-size  xtensa-esp32s3-elf-ld  xtensa-esp32s3-elf-gcc
target "m68k"                    32 m68k-linux-gnu-gcc      m68k-linux-gnu-size      m68k-linux-gnu-ld      m68k-linux-gnu-gcc
target "MIPS32 (little-endian)"  32 mipsel-linux-gnu-gcc    mipsel-linux-gnu-size    mipsel-linux-gnu-ld    mipsel-linux-gnu-gcc
target "PowerPC"                 32 powerpc-linux-gnu-gcc   powerpc-linux-gnu-size   powerpc-linux-gnu-ld   powerpc-linux-gnu-gcc
target "x86-32"                  32 gcc                     size                     "ld -m elf_i386"       gcc -m32

echo "64-bit"
target "x86-64"            64 gcc                         size                         ld                         gcc
target "x86-64"            64 clang                       size                         ld                         clang
target "AArch64"           64 aarch64-linux-gnu-gcc       aarch64-linux-gnu-size       aarch64-linux-gnu-ld       aarch64-linux-gnu-gcc
target "RISC-V rv64gc"     64 riscv64-linux-gnu-gcc       riscv64-linux-gnu-size       riscv64-linux-gnu-ld       riscv64-linux-gnu-gcc
target "MIPS64 (little-endian)" 64 mips64el-linux-gnuabi64-gcc mips64el-linux-gnuabi64-size mips64el-linux-gnuabi64-ld mips64el-linux-gnuabi64-gcc
target_mac "Apple Silicon (arm64 macOS)" 64 "Apple clang"

echo
echo "csv: $CSV   log: $LOG"
if [ "$FAILED" -ne 0 ]; then
    echo "sizes.sh: a target failed to compile or compiled with warnings; see $LOG" >&2
    exit 1
fi
