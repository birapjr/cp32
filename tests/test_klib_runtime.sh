#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-klib.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fno-builtin -I"$root/src/include" -I"$root/src/kernel" \
  "$root/tests/test_klib_runtime.c" -o "$tmp/test_klib_runtime"
"$tmp/test_klib_runtime"
