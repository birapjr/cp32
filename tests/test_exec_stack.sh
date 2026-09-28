#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/mm" \
  "$root/tests/mm/test_exec_stack.c" "$root/src/mm/exec.c" -o "$work/test"
"$work/test"
