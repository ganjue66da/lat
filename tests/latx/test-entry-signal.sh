#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Build the signal-entry guest only when this integration test is executed.
set -euo pipefail
binary=$(realpath "$1")
bits=$2
tests=$(dirname "$(realpath "$0")")

command -v gdb >/dev/null || { echo 'SKIP: GDB required'; exit 77; }
if ! gdb -nx -q -batch \
        -ex "file $binary" \
        -ex 'ptype TranslationBlock' 2>/dev/null |
        grep -q 'type = struct TranslationBlock'; then
    echo 'SKIP: GDB requires a translator build with debug types'
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/latu-entry-signal-guest.XXXXXX")
trap 'rm -rf "$work"' EXIT
compiler=${LATX_TU_TEST_CC:-}
if [[ -z "$compiler" ]]; then
    if [[ $(uname -m) == x86_64 ]]; then
        compiler=${CC:-cc}
    else
        candidates=(x86_64-linux-gnu-gcc)
        if [[ "$bits" == 32 ]]; then
            candidates=(i686-linux-gnu-gcc i386-linux-gnu-gcc
                        x86_64-linux-gnu-gcc)
        fi
        for candidate in "${candidates[@]}"; do
            if command -v "$candidate" >/dev/null 2>&1; then
                compiler=$candidate
                break
            fi
        done
    fi
    if [[ -z "$compiler" ]]; then
        echo 'SKIP: need an x86 compiler or LATX_TU_TEST_CC'
        exit 77
    fi
fi

guest="$work/entry-signal$bits"
args=(-m"$bits" -O2 -g -no-pie -UNDEBUG -pthread -static)
printf '%s\n' 'int main(void) { return 0; }' >"$work/probe.c"
if ! "$compiler" "${args[@]}" "$work/probe.c" -o "$work/probe" \
        >"$work/probe.log" 2>&1; then
    echo "SKIP: compiler cannot link the required $bits-bit x86 guest"
    cat "$work/probe.log"
    exit 77
fi
"$compiler" "${args[@]}" "$tests/entry-signal.c" -o "$guest"
case "$bits" in
    32) arch='Intel 80386' ;;
    64) arch=x86-64 ;;
    *) exit 2 ;;
esac
file "$guest" | grep -q "ELF $bits-bit.*$arch"
bash "$tests/run-tu-entry-signal.sh" "$binary" "$guest"
