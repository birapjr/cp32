#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-misc.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -Wno-error=main -Wno-error=return-type -Wno-macro-redefined -Wno-deprecated-non-prototype -fno-builtin \
  -I"$root/src/include" -I"$root/src/kernel" \
  "$root/tests/test_misc_unit.c" -o "$tmp/test_misc_unit"
"$tmp/test_misc_unit"
