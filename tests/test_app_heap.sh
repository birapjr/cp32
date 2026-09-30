#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/apps/lib" \
 "$root/tests/apps/test_heap.c" "$root/src/apps/lib/heap.c" -o "$work/test"
"$work/test"
