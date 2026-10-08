#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/apps/lib" \
 "$root/tests/apps/test_wc.c" "$root/src/apps/wc/wc.c" "$root/src/apps/lib/io.c" "$root/src/apps/lib/stream.c" "$root/src/apps/lib/output.c" -o "$work/test"
for mode in 0 1 2 3 4 5 6; do "$work/test" "$mode"; done
