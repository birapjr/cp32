#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-ramdisk.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -I"$root/src/kernel" \
  "$root/tests/test_ramdisk.c" "$root/src/kernel/ramdisk.c" \
  -o "$tmp/test_ramdisk"
"$tmp/test_ramdisk"
