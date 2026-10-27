#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/fs" "$root/tests/fs/test_space.c" "$root"/src/fs/*.c -o "$work/test"
"$work/test"
python3 -B "$root/tests/kernel/test_space_command.py"
